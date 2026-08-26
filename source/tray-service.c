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
 * @brief org.sofi.Tray -- the tray daemon's own interface.
 *
 * Translates the StatusNotifierItem world into the one thing sofi's task strip
 * needs: a flat list it can draw, and two verbs it can dispatch.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "TrayService"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <gio/gio.h>

#include "sofi.h"
#include "tray-item.h"
#include "tray-service.h"
#include "tray-watcher.h"

/**
 * Action purpose: collapse a burst of changes into one signal.
 *
 * Separate from, and shorter than, the per-item fetch debounce. That one bounds
 * how often an application can make us re-read it; this one bounds how often we
 * wake a subscriber that is going to rebuild its entire zone regardless of which
 * item changed. 50ms is below perception and long enough to merge the common
 * case of several items reacting to one event.
 */
#define TRAY_CHANGED_COALESCE_MS 50

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.sofi.Tray'>"
    "    <method name='ListItems'>"
    "      <arg type='a(ssssubuuay)' name='items' direction='out'/>"
    "    </method>"
    "    <method name='Activate'>"
    "      <arg type='s' name='service' direction='in'/>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <method name='SecondaryActivate'>"
    "      <arg type='s' name='service' direction='in'/>"
    "      <arg type='i' name='x' direction='in'/>"
    "      <arg type='i' name='y' direction='in'/>"
    "    </method>"
    "    <signal name='Changed'/>"
    "  </interface>"
    "</node>";

static struct {
  guint owner_id;
  guint registration_id;
  guint changed_source;
  GDBusConnection *connection;
  GDBusNodeInfo *introspection;
} service = {0, 0, 0, NULL, NULL};

/* ------------------------------------------------------------------ */
/* change notification                                                 */
/* ------------------------------------------------------------------ */

static gboolean emit_changed_now(G_GNUC_UNUSED gpointer user_data) {
  service.changed_source = 0;

  if (service.connection != NULL) {
    g_dbus_connection_emit_signal(service.connection, NULL,
                                  SOFI_TRAY_OBJECT_PATH, SOFI_TRAY_INTERFACE,
                                  SOFI_TRAY_SIGNAL_CHANGED, NULL, NULL);
  }
  return G_SOURCE_REMOVE;
}

void sofi_tray_service_notify_changed(void) {
  if (service.changed_source > 0) {
    return;
  }
  service.changed_source =
      g_timeout_add(TRAY_CHANGED_COALESCE_MS, emit_changed_now, NULL);
}

static void on_something_changed(G_GNUC_UNUSED gpointer user_data) {
  sofi_tray_service_notify_changed();
}

/* ------------------------------------------------------------------ */
/* method dispatch                                                     */
/* ------------------------------------------------------------------ */

/**
 * Function purpose: find the proxy a caller named.
 *
 * By service string rather than by index, because the two processes are not in
 * lockstep: the strip's list is a snapshot, and an item can vanish between the
 * draw and the click. An index would then activate whichever item happened to
 * shift into that slot -- the wrong application, silently. A stale service
 * string simply matches nothing.
 */
static SofiTrayItemProxy *find_proxy(const gchar *service_name) {
  guint count = sofi_tray_watcher_count();

  for (guint i = 0; i < count; i++) {
    const SofiTrayItem *item = sofi_tray_watcher_nth(i);
    if (item != NULL && g_strcmp0(item->service, service_name) == 0) {
      return item->proxy;
    }
  }
  return NULL;
}

static GVariant *build_item_list(void) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE(SOFI_TRAY_LIST_ITEMS_SIGNATURE));

  guint count = sofi_tray_watcher_count();

  for (guint i = 0; i < count; i++) {
    const SofiTrayItem *item = sofi_tray_watcher_nth(i);
    if (item == NULL || item->proxy == NULL) {
      continue;
    }
    SofiTrayItemProxy *p = item->proxy;

    const guint8 *pixels = NULL;
    gsize length = 0;
    gint width = 0, height = 0;

    /* This is where the lazy decode actually happens -- when an icon is wanted,
     * not when an application announced one. */
    if (!sofi_tray_item_icon_argb32(p, &pixels, &length, &width, &height)) {
      pixels = NULL;
      length = 0;
      width = 0;
      height = 0;
    }

    GVariant *bytes = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, pixels != NULL ? pixels : (const guint8 *)"",
        length, 1);

    g_variant_builder_add(&builder, "(ssssubuu@ay)",
                          sofi_tray_item_service(p), sofi_tray_item_title(p),
                          sofi_tray_item_icon_name(p),
                          sofi_tray_item_icon_theme_path(p),
                          (guint32)sofi_tray_item_status(p),
                          sofi_tray_item_is_menu(p), (guint32)width,
                          (guint32)height, bytes);
  }

  return g_variant_builder_end(&builder);
}

static void handle_method(G_GNUC_UNUSED GDBusConnection *connection,
                          G_GNUC_UNUSED const gchar *sender,
                          G_GNUC_UNUSED const gchar *object_path,
                          G_GNUC_UNUSED const gchar *interface_name,
                          const gchar *method_name, GVariant *parameters,
                          GDBusMethodInvocation *invocation,
                          G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(method_name, SOFI_TRAY_METHOD_LIST_ITEMS) == 0) {
    GVariant *items = build_item_list();
    g_dbus_method_invocation_return_value(invocation,
                                          g_variant_new_tuple(&items, 1));
    return;
  }

  if (g_strcmp0(method_name, SOFI_TRAY_METHOD_ACTIVATE) == 0 ||
      g_strcmp0(method_name, SOFI_TRAY_METHOD_SECONDARY_ACTIVATE) == 0) {
    const gchar *name = NULL;
    gint32 x = 0, y = 0;
    g_variant_get(parameters, "(&sii)", &name, &x, &y);

    SofiTrayItemProxy *proxy = find_proxy(name);

    if (proxy == NULL) {
      /* Not an error: the caller's list is a snapshot and the application may
       * have exited between the draw and the click. Nothing useful happens, and
       * nothing bad does either. */
      g_debug("%s named an item that is no longer registered: %s", method_name,
              name);
      g_dbus_method_invocation_return_value(invocation, NULL);
      return;
    }

    if (g_strcmp0(method_name, SOFI_TRAY_METHOD_ACTIVATE) == 0) {
      sofi_tray_item_activate(proxy, x, y);
    } else {
      sofi_tray_item_secondary_activate(proxy, x, y);
    }
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
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
      connection, SOFI_TRAY_OBJECT_PATH, service.introspection->interfaces[0],
      &interface_vtable, NULL, NULL, &error);

  if (service.registration_id == 0) {
    g_warning("Could not export %s: %s", SOFI_TRAY_INTERFACE,
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

static void on_name_lost(G_GNUC_UNUSED GDBusConnection *connection,
                         const gchar *name,
                         G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: stop, rather than run on without the name.
   *
   * This originally took the name with REPLACE | ALLOW_REPLACEMENT, copying the
   * notification daemon, and simply warned here. That is wrong for the tray, and
   * the reason is that the tray owns TWO names with deliberately different
   * policies. `org.kde.StatusNotifierWatcher` refuses replacement on purpose --
   * two trays fighting over it would flap every icon on the desktop -- so a
   * second daemon could take `org.sofi.Tray` while the FIRST kept the watcher
   * and every registered item. The task strip would then be talking to a daemon
   * that owns no items, and would show an empty tray with no diagnostic
   * anywhere.
   *
   * The two names have to move together. Replacement is off (see
   * sofi_tray_service_start), so reaching here means another sofi tray daemon
   * already holds it -- and this one cannot do the job. */
  g_warning("Could not take %s -- another sofi tray daemon owns it. Stopping; "
            "the running one keeps serving the tray.",
            name);
  sofi_quit_main_loop();
}

gboolean sofi_tray_service_start(void) {
  GError *error = NULL;

  service.introspection = g_dbus_node_info_new_for_xml(introspection_xml,
                                                       &error);
  if (service.introspection == NULL) {
    g_warning("Could not parse the tray interface: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  /* Both sources of change feed one handler: the registry (an item appeared or
   * went) and the items themselves (an icon or title changed). A subscriber
   * rebuilds from ListItems either way, so it does not need to be told which. */
  sofi_tray_item_set_changed_callback(on_something_changed, NULL);
  sofi_tray_watcher_set_changed_callback(on_something_changed, NULL);

  /* Action purpose: DO_NOT_QUEUE alone -- no REPLACE, no ALLOW_REPLACEMENT, and
   * deliberately the same policy the watcher name uses.
   *
   * The notification daemon takes its single name with REPLACE so a freshly
   * installed copy can hand over cleanly. The tray cannot do that, because it
   * holds two names and the watcher half refuses replacement by design. Allowing
   * this half to be stolen desynchronises the pair: the thief serves the task
   * strip while the original still holds every registered item. */
  service.owner_id = g_bus_own_name(
      G_BUS_TYPE_SESSION, SOFI_TRAY_BUS_NAME,
      G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, on_bus_acquired, NULL, on_name_lost,
      NULL, NULL);

  if (service.owner_id == 0) {
    g_warning("Could not reach the session bus for %s.", SOFI_TRAY_BUS_NAME);
    sofi_tray_service_stop();
    return FALSE;
  }

  return TRUE;
}

void sofi_tray_service_stop(void) {
  sofi_tray_item_set_changed_callback(NULL, NULL);
  sofi_tray_watcher_set_changed_callback(NULL, NULL);

  if (service.changed_source > 0) {
    g_source_remove(service.changed_source);
    service.changed_source = 0;
  }
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
}

#endif // SYSTEM_TRAY
