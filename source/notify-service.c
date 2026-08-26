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
#define SOFI_NOTIFY_INTERFACE "org.sofi.Notifications"

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
    /* Action purpose: sofi's own methods live on a second interface, exported
     * on the same object, rather than being bolted onto the freedesktop one.
     *
     * A second interface costs one more register_object call and no second bus
     * name, and it keeps the advertised freedesktop interface exactly what the
     * specification says it is -- a client introspecting for a standard
     * notification server finds no surprises, and anything reaching for these
     * two methods has to ask for sofi by name.
     *
     * They exist because the history menu is a SEPARATE PROCESS. It reads the
     * file the daemon persists, so clearing the ring by writing that file would
     * be undone the moment the daemon's own ring next changed. The clear has to
     * happen where the ring lives. */
    "  <interface name='org.sofi.Notifications'>"
    "    <method name='DismissAll'/>"
    "    <method name='ClearHistory'/>"
    "    <method name='Dismiss'>"
    "      <arg type='u' name='id' direction='in'/>"
    "    </method>"
    "    <method name='InvokeAction'>"
    "      <arg type='u' name='id' direction='in'/>"
    "      <arg type='u' name='index' direction='in'/>"
    "    </method>"
    "    <method name='GetLive'>"
    "      <arg type='a(uus)' name='live' direction='out'/>"
    "    </method>"
    "  </interface>"
    "</node>";

static struct {
  guint owner_id;
  guint registration_id;
  /* The org.sofi.Notifications export. Separate id because it is a separate
   * registration and has to be unregistered separately. */
  guint sofi_registration_id;
  GDBusConnection *connection;
  GDBusNodeInfo *introspection;
} service = {0, 0, 0, NULL, NULL};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

guint sofi_notify_actions_count(const SofiNotification *n) {
  /* Action purpose: read the stored count rather than measuring `actions`. The
   * history mode calls this in a process where the vector is always NULL -- it
   * is not persisted -- and the count it needs arrived from the daemon over
   * GetLive instead. Measuring the vector reported zero actions for every entry
   * there, which is what made Enter do nothing in the history panel. */
  return n == NULL ? 0 : n->action_count;
}

const gchar *sofi_notify_action_label(const SofiNotification *n, guint index) {
  if (index >= sofi_notify_actions_count(n)) {
    return NULL;
  }
  return n->actions[index * 2 + 1];
}

/**
 * Function purpose: decide whether a failed call PROVES no sofi daemon is there.
 *
 * Exactly one answer does: the bus reporting that nothing owns the name. Then
 * there is no ring anywhere to contradict, and a caller holding its own copy of
 * the history is free to act on it. That is also the answer the common case
 * produces -- no daemon started, or one that has exited.
 *
 * Everything else leaves the question open, including a reply that the object,
 * interface or method is unknown. That looks like a foreign notification server
 * holding the name, but it is equally what a *sofi* daemon answers when its
 * org.sofi.Notifications export failed -- see on_bus_acquired(), which warns and
 * carries on -- and that daemon owns a live ring. Guessing wrong there means a
 * standalone process rewriting a history file the daemon is about to overwrite
 * from state it never saw, so the mutation is declined instead.
 */
static gboolean error_means_no_daemon(const GError *error) {
  return g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER);
}

/**
 * Function purpose: one call to the running daemon, with arguments and an
 * optional reply.
 *
 * @param params  floating GVariant of arguments, CONSUMED. NULL for none.
 * @param reply_out where to put the reply, or NULL to discard it. The caller
 *                  owns what lands here and must unref it.
 */
static SofiNotifyDaemonResult call_daemon(const gchar *method, GVariant *params,
                                          GVariant **reply_out) {
  GError *error = NULL;
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (reply_out != NULL) {
    *reply_out = NULL;
  }

  if (bus == NULL) {
    /* Action purpose: g_dbus_connection_call_sync would have consumed the
     * floating reference; nothing else will, on this path. */
    if (params != NULL) {
      g_variant_unref(g_variant_ref_sink(params));
    }
    /* Not proof of absence: a daemon that took the name while the bus was
     * reachable is still running and still owns the ring. All this establishes
     * is that we cannot ask. */
    g_warning("Could not reach the session bus: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return SOFI_NOTIFY_DAEMON_FAILED;
  }

  GVariant *reply = g_dbus_connection_call_sync(
      bus, NOTIFY_BUS_NAME, NOTIFY_OBJECT_PATH, SOFI_NOTIFY_INTERFACE, method,
      params, NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, &error);

  g_object_unref(bus);

  if (reply == NULL) {
    gboolean absent = error_means_no_daemon(error);

    /* Absence is expected and not worth showing a user; a call that failed for
     * any other reason is worth a word, because the caller is about to decline
     * to do the work. */
    if (absent) {
      g_debug("%s reached no sofi daemon: %s", method,
              error != NULL ? error->message : "unknown error");
    } else {
      g_warning("%s could not be delivered: %s", method,
                error != NULL ? error->message : "unknown error");
    }
    g_clear_error(&error);
    return absent ? SOFI_NOTIFY_DAEMON_ABSENT : SOFI_NOTIFY_DAEMON_FAILED;
  }

  if (reply_out != NULL) {
    *reply_out = reply;
  } else {
    g_variant_unref(reply);
  }
  return SOFI_NOTIFY_DAEMON_HANDLED;
}

SofiNotifyDaemonResult sofi_notify_service_call_daemon(const gchar *method) {
  return call_daemon(method, NULL, NULL);
}

SofiNotifyDaemonResult sofi_notify_service_daemon_dismiss(guint32 id) {
  return call_daemon(SOFI_NOTIFY_METHOD_DISMISS, g_variant_new("(u)", id),
                     NULL);
}

SofiNotifyDaemonResult sofi_notify_service_daemon_invoke_action(guint32 id,
                                                                guint index) {
  return call_daemon(SOFI_NOTIFY_METHOD_INVOKE_ACTION,
                     g_variant_new("(uu)", id, (guint32)index), NULL);
}

SofiNotifyDaemonResult sofi_notify_service_refresh_live(void) {
  GVariant *reply = NULL;
  SofiNotifyDaemonResult result =
      call_daemon(SOFI_NOTIFY_METHOD_GET_LIVE, NULL, &reply);

  if (result != SOFI_NOTIFY_DAEMON_HANDLED) {
    /* Action purpose: leave the ring alone. ABSENT is already correct -- with
     * no daemon nothing is on screen, and entries loaded from disk are retired
     * by construction. FAILED must change nothing for the usual reason: a
     * daemon we could not reach may still hold a live set we cannot see, and
     * clearing every stripe would tell the user their notifications were gone
     * when they are on screen in front of them. */
    return result;
  }

  GVariantIter *iter = NULL;
  g_variant_get(reply, "(a(uus))", &iter);

  gsize n = g_variant_iter_n_children(iter);
  SofiNotifyLiveInfo *live = g_new0(SofiNotifyLiveInfo, n + 1);
  guint32 id = 0, action_count = 0;
  const gchar *desktop_entry = NULL;
  guint k = 0;

  /* `&s` borrows from the reply, which outlives the apply call below. */
  while (k < n &&
         g_variant_iter_next(iter, "(uu&s)", &id, &action_count,
                             &desktop_entry)) {
    live[k].id = id;
    live[k].action_count = action_count;
    live[k].desktop_entry = desktop_entry;
    k++;
  }

  sofi_notify_store_apply_live(live, k);

  g_free(live);
  g_variant_iter_free(iter);
  g_variant_unref(reply);

  return SOFI_NOTIFY_DAEMON_HANDLED;
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
  gchar *desktop_entry = NULL;

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
    } else if (g_strcmp0(key, "desktop-entry") == 0 &&
               g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) {
      /* Action purpose: the sender's desktop file basename, and the only key a
       * notification carries that could ever identify the window behind it.
       * Kept because discarding it is irreversible -- the notification is a
       * record, and nothing can recover a hint that was thrown away when it
       * arrived. Nothing consumes it yet; see PLANS.md A5. */
      g_free(desktop_entry);
      desktop_entry = g_variant_dup_string(value, NULL);
    }
    g_variant_unref(value);
  }

  gchar *safe_body = sanitise_body(body);

  /* Action purpose: image-path is a better icon than app_icon when both are
   * present -- it is chosen per notification rather than per application. */
  const gchar *icon = (image_path != NULL && image_path[0] != '\0')
                          ? image_path
                          : app_icon;

  guint32 id = sofi_notify_store_add(
      app_name, replaces_id, icon, summary, safe_body, desktop_entry,
      (gchar **)g_ptr_array_free(actions, FALSE), urgency, expire_timeout,
      image);

  g_free(safe_body);
  g_free(image_path);
  g_free(desktop_entry);
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

/**
 * Function purpose: serve org.sofi.Notifications -- everything the history menu
 * needs to reach the ring from another process.
 *
 * The four mutations are fire-and-forget: they return an empty reply rather than
 * a count, because the caller is a menu that reloads from the file afterwards
 * and has nothing to do with a number.
 *
 * GetLive is the exception and the only reader. It exists because two facts
 * about a notification are NOT in the persisted file and must never be: whether
 * it is still on screen, and what can be done with it. Both belong to the
 * process that received it. A file asserting either would be wrong the instant
 * the daemon acted on the entry, and the history menu would offer to dismiss
 * notifications that had already gone.
 */
static void handle_sofi_method(G_GNUC_UNUSED GDBusConnection *connection,
                               G_GNUC_UNUSED const gchar *sender,
                               G_GNUC_UNUSED const gchar *object_path,
                               G_GNUC_UNUSED const gchar *interface_name,
                               const gchar *method_name,
                               G_GNUC_UNUSED GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(method_name, "DismissAll") == 0) {
    sofi_notify_store_close_all(SOFI_NOTIFY_CLOSED_DISMISSED);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "ClearHistory") == 0) {
    sofi_notify_store_clear_history();
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, SOFI_NOTIFY_METHOD_DISMISS) == 0) {
    guint32 id = 0;
    g_variant_get(parameters, "(u)", &id);
    /* Not an error when the id names nothing live: the caller's list is a
     * snapshot, and an entry it still shows may have expired between the draw
     * and the keystroke. */
    sofi_notify_store_close(id, SOFI_NOTIFY_CLOSED_DISMISSED);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, SOFI_NOTIFY_METHOD_INVOKE_ACTION) == 0) {
    guint32 id = 0, index = 0;
    g_variant_get(parameters, "(uu)", &id, &index);
    /* Action purpose: this is the whole reason the method exists. Invoking an
     * action means emitting ActionInvoked, and only the process holding the
     * daemon's bus connection can emit it -- in a standalone history menu the
     * signal was silently dropped, so Enter appeared to do nothing at all. */
    sofi_notify_service_invoke_action(id, index);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, SOFI_NOTIFY_METHOD_GET_LIVE) == 0) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(uus)"));

    guint live = sofi_notify_store_live_count();
    for (guint i = 0; i < live; i++) {
      const SofiNotification *n = sofi_notify_store_live_nth(i);
      if (n == NULL) {
        continue;
      }
      g_variant_builder_add(&builder, "(uus)", n->id, n->action_count,
                            n->desktop_entry != NULL ? n->desktop_entry : "");
    }
    g_dbus_method_invocation_return_value(
        invocation, g_variant_new("(a(uus))", &builder));
    return;
  }

  g_dbus_method_invocation_return_error(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
      "Unknown method %s", method_name);
}

static const GDBusInterfaceVTable sofi_interface_vtable = {
    .method_call = handle_sofi_method,
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

  /* Action purpose: sofi's own interface on the same object. Its failure is a
   * warning rather than fatal -- losing it costs the history menu its remote
   * clear, while the notification service itself is unaffected. The menu will
   * decline the clear rather than degrade to a local one: this daemon is still
   * running and still owns the ring, and its answer to a call on the missing
   * interface is indistinguishable from a foreign daemon's (see
   * error_means_no_daemon). */
  service.sofi_registration_id = g_dbus_connection_register_object(
      connection, NOTIFY_OBJECT_PATH, service.introspection->interfaces[1],
      &sofi_interface_vtable, NULL, NULL, &error);

  if (service.sofi_registration_id == 0) {
    g_warning("Could not export %s: %s", SOFI_NOTIFY_INTERFACE,
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

  /* Action purpose: read back what the previous daemon persisted, before the
   * bus name is requested and therefore before any Notify can arrive.
   *
   * Without this the ring starts empty, and the first notification's
   * sofi_notify_store_save() -- which runs on every change -- rewrites the
   * history file from that empty ring. Every entry the last session collected
   * is destroyed by the first notification of the next one, which is to say at
   * every login. The promise in notify-store.h that history survives a daemon
   * restart rests entirely on this call.
   *
   * Safe by construction rather than by care: entries come back retired and
   * untimed (see sofi_notify_store_load), so this cannot resurrect a banner for
   * something already dealt with. It also advances next_id past the highest
   * stored id, which is what stops a restarted daemon handing out an id a
   * sender still believes it holds. */
  sofi_notify_store_load();

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
  if (service.sofi_registration_id > 0 && service.connection != NULL) {
    g_dbus_connection_unregister_object(service.connection,
                                        service.sofi_registration_id);
    service.sofi_registration_id = 0;
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
