/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2026 orpheus497
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/**
 * @file
 * @brief org.freedesktop.Notifications, served by sofi.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "NotifyService"

#include "config.h"

#ifdef NOTIFY_DAEMON

#include <string.h>

#include <gio/gio.h>

#include "notify-service.h"
#include "notify-store.h"
#include "sofi.h"
#include "view.h"

#define NOTIFY_BUS_NAME "org.freedesktop.Notifications"
#define NOTIFY_OBJECT_PATH "/org/freedesktop/Notifications"
#define NOTIFY_INTERFACE "org.freedesktop.Notifications"

/**
 * Action purpose: bound what an inline image may cost us. width * height * 4
 * at these limits is ~64MB before the stride check, and any real notification
 * icon is two orders of magnitude smaller. A sender picks these numbers, so
 * they are the one place in this file taking hostile input.
 */
#define NOTIFY_IMAGE_MAX_DIM 4096

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.freedesktop.Notifications'>"
    "    <method name='Notify'>"
    "      <arg type='s' name='app_name' direction='in'/>"
    "      <arg type='u' name='replaces_id' direction='in'/>"
    "      <arg type='s' name='app_icon' direction='in'/>"
    "      <arg type='s' name='summary' direction='in'/>"
    "      <arg type='s' name='body' direction='in'/>"
    "      <arg type='as' name='actions' direction='in'/>"
    "      <arg type='a{sv}' name='hints' direction='in'/>"
    "      <arg type='i' name='expire_timeout' direction='in'/>"
    "      <arg type='u' name='id' direction='out'/>"
    "    </method>"
    "    <method name='CloseNotification'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <method name='GetCapabilities'>"
    "      <arg type='as' name='capabilities' direction='out'/>"
    "    </method>"
    "    <method name='GetServerInformation'>"
    "      <arg type='s' name='name' direction='out'/>"
    "      <arg type='s' name='vendor' direction='out'/>"
    "      <arg type='s' name='version' direction='out'/>"
    "      <arg type='s' name='spec_version' direction='out'/>"
    "    </method>"
    "    <signal name='NotificationClosed'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='u' name='reason'/>"
    "    </signal>"
    "    <signal name='ActionInvoked'>"
    "      <arg type='u' name='id'/>"
    "      <arg type='s' name='action_key'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

static struct {
  guint owner_id;
  guint registration_id;
  GDBusConnection *connection;
  GDBusNodeInfo *introspection;
} service = {0, 0, NULL, NULL};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

guint sofi_notify_actions_count(const SofiNotification *n) {
  if (n == NULL || n->actions == NULL) {
    return 0;
  }
  return g_strv_length(n->actions) / 2;
}

const gchar *sofi_notify_action_label(const SofiNotification *n, guint index) {
  if (index >= sofi_notify_actions_count(n)) {
    return NULL;
  }
  return n->actions[index * 2 + 1];
}

static const gchar *action_key(const SofiNotification *n, guint index) {
  if (index >= sofi_notify_actions_count(n)) {
    return NULL;
  }
  return n->actions[index * 2];
}

static void emit_signal(const gchar *name, GVariant *params) {
  if (service.connection == NULL) {
    if (params != NULL) {
      g_variant_unref(g_variant_ref_sink(params));
    }
    return;
  }
  g_dbus_connection_emit_signal(service.connection, NULL, NOTIFY_OBJECT_PATH,
                                NOTIFY_INTERFACE, name, params, NULL);
}

static void on_closed(guint32 id, SofiNotifyCloseReason reason,
                      G_GNUC_UNUSED gpointer user_data) {
  emit_signal("NotificationClosed", g_variant_new("(uu)", id, (guint32)reason));
}

static void on_changed(G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: not sofi_view_reload(). Reload only refreshes a view that
   * already exists, and the common case here is that none does -- the daemon
   * spends most of its life idle with no surface at all. */
  sofi_notify_daemon_refresh();
}

/**
 * Function purpose: make a body string safe to hand to Pango.
 *
 * The specification's body-markup is a small HTML subset that overlaps Pango's
 * markup without matching it, and the text comes from arbitrary applications.
 * Validate first; anything Pango will not parse is escaped and rendered
 * literally rather than dropped, so the user still sees the message.
 */
static gchar *sanitise_body(const gchar *body) {
  if (body == NULL || body[0] == '\0') {
    return g_strdup("");
  }

  if (pango_parse_markup(body, -1, 0, NULL, NULL, NULL, NULL)) {
    return g_strdup(body);
  }
  return g_markup_escape_text(body, -1);
}

/**
 * Function purpose: turn the image-data hint into a cairo surface.
 *
 * The hint is (iiibiiay): width, height, rowstride, has_alpha,
 * bits_per_sample, channels, data. Every one of those is chosen by the sender,
 * so the geometry is checked against the actual byte count before a single
 * pixel is read -- a mismatched rowstride is the obvious way to walk off the
 * end of the array.
 *
 * @returns a surface the caller owns, or NULL when the hint is unusable.
 */
static cairo_surface_t *image_from_hint(GVariant *hint) {
  gint32 width = 0, height = 0, rowstride = 0, bits = 0, channels = 0;
  gboolean has_alpha = FALSE;
  GVariant *data = NULL;

  g_variant_get(hint, "(iiibii@ay)", &width, &height, &rowstride, &has_alpha,
                &bits, &channels, &data);

  if (data == NULL) {
    return NULL;
  }

  gsize length = 0;
  const guchar *pixels = g_variant_get_fixed_array(data, &length, 1);

  gboolean sane = width > 0 && height > 0 && width <= NOTIFY_IMAGE_MAX_DIM &&
                  height <= NOTIFY_IMAGE_MAX_DIM && bits == 8 &&
                  (channels == 3 || channels == 4) &&
                  has_alpha == (channels == 4) &&
                  rowstride >= width * channels;

  /* Action purpose: the geometry must account for exactly the bytes we were
   * given. The last row is only `width * channels` wide, not a full stride,
   * which is why this is not simply rowstride * height. */
  if (sane) {
    gsize needed = (gsize)rowstride * (gsize)(height - 1) +
                   (gsize)width * (gsize)channels;
    sane = pixels != NULL && length >= needed;
  }

  if (!sane) {
    g_warning("Refusing a malformed image-data hint: %dx%d stride %d "
              "channels %d bits %d, %" G_GSIZE_FORMAT " bytes.",
              width, height, rowstride, channels, bits, length);
    g_variant_unref(data);
    return NULL;
  }

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    g_variant_unref(data);
    return NULL;
  }

  guchar *dst = cairo_image_surface_get_data(surface);
  int dst_stride = cairo_image_surface_get_stride(surface);

  /* Action purpose: the hint is straight RGB(A); cairo's ARGB32 is premultiplied
   * native-endian. Convert per pixel rather than memcpy. */
  for (gint32 y = 0; y < height; y++) {
    const guchar *src_row = pixels + (gsize)y * (gsize)rowstride;
    guint32 *dst_row = (guint32 *)(dst + (gsize)y * (gsize)dst_stride);

    for (gint32 x = 0; x < width; x++) {
      const guchar *p = src_row + (gsize)x * (gsize)channels;
      guint a = (channels == 4) ? p[3] : 0xFF;
      guint r = p[0], g = p[1], b = p[2];

      if (a != 0xFF) {
        r = (r * a) / 0xFF;
        g = (g * a) / 0xFF;
        b = (b * a) / 0xFF;
      }
      dst_row[x] = ((guint32)a << 24) | ((guint32)r << 16) |
                   ((guint32)g << 8) | (guint32)b;
    }
  }

  cairo_surface_mark_dirty(surface);
  g_variant_unref(data);

  return surface;
}

/* ------------------------------------------------------------------ */
/* method dispatch                                                     */
/* ------------------------------------------------------------------ */

static void handle_notify(GVariant *parameters,
                          GDBusMethodInvocation *invocation) {
  const gchar *app_name = NULL;
  guint32 replaces_id = 0;
  const gchar *app_icon = NULL;
  const gchar *summary = NULL;
  const gchar *body = NULL;
  GVariantIter *actions_iter = NULL;
  GVariantIter *hints_iter = NULL;
  gint32 expire_timeout = -1;

  g_variant_get(parameters, "(&su&s&s&sasa{sv}i)", &app_name, &replaces_id,
                &app_icon, &summary, &body, &actions_iter, &hints_iter,
                &expire_timeout);

  GPtrArray *actions = g_ptr_array_new();
  const gchar *action = NULL;
  while (actions_iter != NULL &&
         g_variant_iter_next(actions_iter, "&s", &action)) {
    g_ptr_array_add(actions, g_strdup(action));
  }
  /* Action purpose: the spec's array is flat key/label pairs. An odd length is
   * a malformed request; drop the trailing key rather than reading past it
   * when looking up a label. */
  if (actions->len % 2 != 0) {
    g_free(g_ptr_array_index(actions, actions->len - 1));
    g_ptr_array_set_size(actions, actions->len - 1);
  }
  g_ptr_array_add(actions, NULL);

  SofiNotifyUrgency urgency = SOFI_NOTIFY_URGENCY_NORMAL;
  cairo_surface_t *image = NULL;
  gchar *image_path = NULL;

  const gchar *key = NULL;
  GVariant *value = NULL;
  while (hints_iter != NULL &&
         g_variant_iter_next(hints_iter, "{&sv}", &key, &value)) {
    if (g_strcmp0(key, "urgency") == 0 &&
        g_variant_is_of_type(value, G_VARIANT_TYPE_BYTE)) {
      guint8 u = g_variant_get_byte(value);
      urgency = (u > SOFI_NOTIFY_URGENCY_CRITICAL)
                    ? SOFI_NOTIFY_URGENCY_CRITICAL
                    : (SofiNotifyUrgency)u;
    } else if ((g_strcmp0(key, "image-data") == 0 ||
                g_strcmp0(key, "image_data") == 0 ||
                g_strcmp0(key, "icon_data") == 0) &&
               image == NULL &&
               g_variant_is_of_type(value, G_VARIANT_TYPE("(iiibiiay)"))) {
      image = image_from_hint(value);
    } else if ((g_strcmp0(key, "image-path") == 0 ||
                g_strcmp0(key, "image_path") == 0) &&
               g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
      g_free(image_path);
      image_path = g_variant_dup_string(value, NULL);
    }
    g_variant_unref(value);
  }

  gchar *safe_body = sanitise_body(body);

  /* Action purpose: image-path is a better icon than app_icon when both are
   * present -- it is chosen per notification rather than per application. */
  const gchar *icon = (image_path != NULL && image_path[0] != '\0')
                          ? image_path
                          : app_icon;

  guint32 id = sofi_notify_store_add(app_name, replaces_id, icon, summary,
                                     safe_body, (gchar **)g_ptr_array_free(
                                                    actions, FALSE),
                                     urgency, expire_timeout, image);

  g_free(safe_body);
  g_free(image_path);
  if (actions_iter != NULL) {
    g_variant_iter_free(actions_iter);
  }
  if (hints_iter != NULL) {
    g_variant_iter_free(hints_iter);
  }

  g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", id));
}

static void handle_method(G_GNUC_UNUSED GDBusConnection *connection,
                          G_GNUC_UNUSED const gchar *sender,
                          G_GNUC_UNUSED const gchar *object_path,
                          G_GNUC_UNUSED const gchar *interface_name,
                          const gchar *method_name, GVariant *parameters,
                          GDBusMethodInvocation *invocation,
                          G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(method_name, "Notify") == 0) {
    handle_notify(parameters, invocation);
    return;
  }

  if (g_strcmp0(method_name, "CloseNotification") == 0) {
    guint32 id = 0;
    g_variant_get(parameters, "(u)", &id);
    sofi_notify_store_close(id, SOFI_NOTIFY_CLOSED_BY_CALL);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "GetCapabilities") == 0) {
    /* Action purpose: declare only what is implemented. Advertising
     * "persistence" or "sound" we do not honour makes senders change their
     * behaviour on a promise we then break. */
    const gchar *caps[] = {"body", "body-markup", "icon-static", "actions",
                           NULL};
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(^as)", (gchar **)caps));
    return;
  }

  if (g_strcmp0(method_name, "GetServerInformation") == 0) {
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(ssss)", "sofi", "orpheus497", VERSION,
                                  "1.2"));
    return;
  }

  g_dbus_method_invocation_return_error(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
      "Unknown method %s", method_name);
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method,
    .get_property = NULL,
    .set_property = NULL,
    .padding = {NULL},
};

/* ------------------------------------------------------------------ */
/* name ownership                                                      */
/* ------------------------------------------------------------------ */

static void on_bus_acquired(GDBusConnection *connection,
                            G_GNUC_UNUSED const gchar *name,
                            G_GNUC_UNUSED gpointer user_data) {
  GError *error = NULL;

  service.connection = connection;
  service.registration_id = g_dbus_connection_register_object(
      connection, NOTIFY_OBJECT_PATH, service.introspection->interfaces[0],
      &interface_vtable, NULL, NULL, &error);

  if (service.registration_id == 0) {
    g_warning("Could not export %s: %s", NOTIFY_INTERFACE,
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

static void on_name_acquired(G_GNUC_UNUSED GDBusConnection *connection,
                             const gchar *name,
                             G_GNUC_UNUSED gpointer user_data) {
  g_debug("Now serving %s", name);
}

static void on_name_lost(G_GNUC_UNUSED GDBusConnection *connection,
                         const gchar *name,
                         G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: another daemon holds or took the name. Running on without
   * it would leave a process that renders nothing and answers nothing, which
   * looks identical to a hang. Say why and stop. */
  g_warning("Could not take %s -- another notification daemon owns it. "
            "Stop it first, or place a service file in "
            "$XDG_DATA_HOME/dbus-1/services/ so sofi is activated instead.",
            name);
  sofi_quit_main_loop();
}

gboolean sofi_notify_service_start(void) {
  GError *error = NULL;

  service.introspection =
      g_dbus_node_info_new_for_xml(introspection_xml, &error);

  if (service.introspection == NULL) {
    g_warning("Could not parse the notification interface: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  sofi_notify_store_init(on_changed, on_closed, NULL);

  /* Action purpose: ALLOW_REPLACEMENT alongside REPLACE, so the handover works
   * in both directions between two sofi daemons. REPLACE alone only displaces
   * an owner that permitted it -- without ALLOW_REPLACEMENT sofi does not
   * permit it of itself, so a freshly installed daemon could never take over
   * from the running one and had to be swapped by hand.
   *
   * Safe only because the daemon no longer takes a pidfile (see main()): while
   * it did, the incoming daemon died on the lock while the outgoing one was
   * already giving up the name, and neither served. */
  service.owner_id = g_bus_own_name(
      G_BUS_TYPE_SESSION, NOTIFY_BUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_REPLACE | G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE |
          G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT,
      on_bus_acquired, on_name_acquired, on_name_lost, NULL, NULL);

  if (service.owner_id == 0) {
    g_warning("Could not reach the session bus.");
    sofi_notify_store_fini();
    g_dbus_node_info_unref(service.introspection);
    service.introspection = NULL;
    return FALSE;
  }

  return TRUE;
}

void sofi_notify_service_stop(void) {
  if (service.registration_id > 0 && service.connection != NULL) {
    g_dbus_connection_unregister_object(service.connection,
                                        service.registration_id);
    service.registration_id = 0;
  }
  if (service.owner_id > 0) {
    g_bus_unown_name(service.owner_id);
    service.owner_id = 0;
  }
  if (service.introspection != NULL) {
    g_dbus_node_info_unref(service.introspection);
    service.introspection = NULL;
  }
  service.connection = NULL;
  sofi_notify_store_fini();
}

void sofi_notify_service_invoke_action(guint32 id, guint action_index) {
  guint live = sofi_notify_store_live_count();

  for (guint i = 0; i < live; i++) {
    const SofiNotification *n = sofi_notify_store_live_nth(i);
    if (n == NULL || n->id != id) {
      continue;
    }
    const gchar *key = action_key(n, action_index);
    if (key == NULL) {
      return;
    }
    emit_signal("ActionInvoked", g_variant_new("(us)", id, key));
    sofi_notify_store_close(id, SOFI_NOTIFY_CLOSED_DISMISSED);
    return;
  }
}

void sofi_notify_service_dismiss(guint32 id) {
  sofi_notify_store_close(id, SOFI_NOTIFY_CLOSED_DISMISSED);
}

#endif // NOTIFY_DAEMON
