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
 * @brief org.kde.StatusNotifierWatcher, served by sofi.
 *
 * The registry half of the system tray: which items exist, and telling everyone
 * when that changes. It reads nothing out of the items themselves.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "TrayWatcher"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <unistd.h>

#include <gio/gio.h>

#include "tray-watcher.h"

#define WATCHER_BUS_NAME "org.kde.StatusNotifierWatcher"
#define WATCHER_OBJECT_PATH "/StatusNotifierWatcher"
#define WATCHER_INTERFACE "org.kde.StatusNotifierWatcher"

/**
 * The object path an item is assumed to live at when it registers by bus name
 * alone. KDE's own implementation uses this and so does every item that
 * registers the short way.
 */
#define ITEM_DEFAULT_OBJECT_PATH "/StatusNotifierItem"

/**
 * Reported through the ProtocolVersion property. 0 is what KDE's watcher has
 * always reported and what every item in the wild is written against; there has
 * never been a version 1.
 */
#define WATCHER_PROTOCOL_VERSION 0

static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.kde.StatusNotifierWatcher'>"
    "    <method name='RegisterStatusNotifierItem'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <method name='RegisterStatusNotifierHost'>"
    "      <arg type='s' name='service' direction='in'/>"
    "    </method>"
    "    <property name='RegisteredStatusNotifierItems' type='as' "
    "access='read'/>"
    "    <property name='IsStatusNotifierHostRegistered' type='b' "
    "access='read'/>"
    "    <property name='ProtocolVersion' type='i' access='read'/>"
    "    <signal name='StatusNotifierItemRegistered'>"
    "      <arg type='s' name='service'/>"
    "    </signal>"
    "    <signal name='StatusNotifierItemUnregistered'>"
    "      <arg type='s' name='service'/>"
    "    </signal>"
    "    <signal name='StatusNotifierHostRegistered'/>"
    "    <signal name='StatusNotifierHostUnregistered'/>"
    "  </interface>"
    "</node>";

static struct {
  guint watcher_owner_id;
  guint host_owner_id;
  guint registration_id;
  GDBusConnection *connection;
  GDBusNodeInfo *introspection;
  /** SofiTrayItem*, in registration order. */
  GPtrArray *items;
  /** Foreign hosts registered with us, keyed by bus name -> watch id. */
  GHashTable *hosts;
  /** TRUE once we hold the watcher name and have registered ourselves. */
  gboolean self_host_registered;
} watcher = {0, 0, 0, NULL, NULL, NULL, NULL, FALSE};

/* ------------------------------------------------------------------ */
/* registry                                                            */
/* ------------------------------------------------------------------ */

static SofiTrayItemChangedFunc registry_changed = NULL;
static gpointer registry_changed_data = NULL;

void sofi_tray_watcher_set_changed_callback(SofiTrayItemChangedFunc callback,
                                            gpointer user_data) {
  registry_changed = callback;
  registry_changed_data = user_data;
}

static void notify_registry_changed(void) {
  if (registry_changed != NULL) {
    registry_changed(registry_changed_data);
  }
}

static void tray_item_free(gpointer data) {
  SofiTrayItem *item = (SofiTrayItem *)data;

  if (item == NULL) {
    return;
  }
  if (item->watch_id > 0) {
    g_bus_unwatch_name(item->watch_id);
    item->watch_id = 0;
  }
  /* Before the strings: freeing the proxy cancels its in-flight calls, and
   * those hold pointers into it. */
  sofi_tray_item_free(item->proxy);
  item->proxy = NULL;
  g_free(item->service);
  g_free(item->bus_name);
  g_free(item->object_path);
  g_free(item);
}

static void emit_signal(const gchar *name, GVariant *params) {
  if (watcher.connection == NULL) {
    if (params != NULL) {
      g_variant_unref(g_variant_ref_sink(params));
    }
    return;
  }
  g_dbus_connection_emit_signal(watcher.connection, NULL, WATCHER_OBJECT_PATH,
                                WATCHER_INTERFACE, name, params, NULL);
}

static gint find_item(const gchar *service) {
  if (watcher.items == NULL) {
    return -1;
  }
  for (guint i = 0; i < watcher.items->len; i++) {
    const SofiTrayItem *item = g_ptr_array_index(watcher.items, i);
    if (g_strcmp0(item->service, service) == 0) {
      return (gint)i;
    }
  }
  return -1;
}

/**
 * Function purpose: drop an item whose application has gone.
 *
 * Reached both from the name watch and from teardown. Emitting before removing
 * would hand listeners a service string they could still look up and find; the
 * order here means a host that reacts synchronously sees the registry the
 * signal describes.
 */
static void unregister_item(guint index) {
  SofiTrayItem *item = g_ptr_array_index(watcher.items, index);
  gchar *service = g_strdup(item->service);

  g_ptr_array_remove_index(watcher.items, index);
  g_debug("Tray item gone: %s (%u left)", service, watcher.items->len);
  emit_signal("StatusNotifierItemUnregistered", g_variant_new("(s)", service));
  g_free(service);
  notify_registry_changed();
}

static void item_vanished(G_GNUC_UNUSED GDBusConnection *connection,
                          const gchar *name,
                          G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: this is not a fallback for a missing UnregisterX method --
   * the specification has no such method at all. An item exists for exactly as
   * long as its bus name does, so watching the name IS the deregistration
   * mechanism, and without it the tray accumulates icons for applications that
   * exited. */
  if (watcher.items == NULL) {
    return;
  }
  for (guint i = watcher.items->len; i > 0; i--) {
    const SofiTrayItem *item = g_ptr_array_index(watcher.items, i - 1);
    if (g_strcmp0(item->bus_name, name) == 0) {
      unregister_item(i - 1);
    }
  }
}

/**
 * Function purpose: work out what an application actually meant by its
 * registration argument.
 *
 * The argument is documented as a service name, and in practice it is one of
 * two different things depending on the toolkit: a bus name, or an object path
 * with the bus name left implicit in the sender. Both are common -- Qt/KDE
 * items send the name, several GTK and Electron ones send the path -- and a
 * watcher that handles only one of them shows an empty tray for half the
 * desktop with no diagnostic. Neither form is wrong; the specification simply
 * never pinned it down.
 */
static void split_service(const gchar *service, const gchar *sender,
                          gchar **bus_name, gchar **object_path) {
  if (service != NULL && service[0] == '/') {
    *bus_name = g_strdup(sender);
    *object_path = g_strdup(service);
    return;
  }
  *bus_name = g_strdup((service != NULL && service[0] != '\0') ? service
                                                               : sender);
  *object_path = g_strdup(ITEM_DEFAULT_OBJECT_PATH);
}

static void register_item(const gchar *service, const gchar *sender) {
  gchar *bus_name = NULL;
  gchar *object_path = NULL;

  split_service(service, sender, &bus_name, &object_path);

  gchar *canonical = g_strconcat(bus_name, object_path, NULL);

  if (find_item(canonical) >= 0) {
    /* Registering twice is not an error worth refusing: an application that
     * reconnects to the bus re-registers, and it has no way of knowing whether
     * we still hold its previous entry. */
    g_debug("Tray item already registered: %s", canonical);
    g_free(canonical);
    g_free(bus_name);
    g_free(object_path);
    return;
  }

  SofiTrayItem *item = g_malloc0(sizeof(SofiTrayItem));
  item->service = canonical;
  item->bus_name = bus_name;
  item->object_path = object_path;
  item->watch_id = g_bus_watch_name(
      G_BUS_TYPE_SESSION, item->bus_name, G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
      item_vanished, NULL, NULL);

  /* Action purpose: start reading the item's properties immediately. It returns
   * at once and fetches in the background, so a slow or wedged application
   * delays nothing here -- which is the whole reason this daemon can host a
   * tray at all. */
  item->proxy = sofi_tray_item_new(watcher.connection, item->service,
                                   item->bus_name, item->object_path);

  g_ptr_array_add(watcher.items, item);
  g_message("Tray item registered: %s (%u total)", item->service,
            watcher.items->len);

  emit_signal("StatusNotifierItemRegistered",
              g_variant_new("(s)", item->service));
  notify_registry_changed();
}

static void host_vanished(G_GNUC_UNUSED GDBusConnection *connection,
                          const gchar *name,
                          G_GNUC_UNUSED gpointer user_data) {
  if (watcher.hosts == NULL || !g_hash_table_contains(watcher.hosts, name)) {
    return;
  }
  g_hash_table_remove(watcher.hosts, name);

  /* Action purpose: only claim the host is gone when OURS is gone too, which
   * it is not while this process is running. Reporting otherwise would tell
   * every item to stop publishing to a tray that is still on screen. */
  if (!watcher.self_host_registered && g_hash_table_size(watcher.hosts) == 0) {
    emit_signal("StatusNotifierHostUnregistered", NULL);
  }
}

static void host_watch_destroy(gpointer data) {
  guint id = GPOINTER_TO_UINT(data);
  if (id > 0) {
    g_bus_unwatch_name(id);
  }
}

static void register_host(const gchar *service, const gchar *sender) {
  const gchar *name = (service != NULL && service[0] != '\0') ? service
                                                              : sender;
  if (name == NULL || g_hash_table_contains(watcher.hosts, name)) {
    return;
  }

  guint id = g_bus_watch_name(G_BUS_TYPE_SESSION, name,
                              G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
                              host_vanished, NULL, NULL);

  g_hash_table_insert(watcher.hosts, g_strdup(name), GUINT_TO_POINTER(id));
  g_debug("Tray host registered: %s", name);

  emit_signal("StatusNotifierHostRegistered", NULL);
}

/* ------------------------------------------------------------------ */
/* method and property dispatch                                        */
/* ------------------------------------------------------------------ */

static void handle_method(G_GNUC_UNUSED GDBusConnection *connection,
                          const gchar *sender,
                          G_GNUC_UNUSED const gchar *object_path,
                          G_GNUC_UNUSED const gchar *interface_name,
                          const gchar *method_name, GVariant *parameters,
                          GDBusMethodInvocation *invocation,
                          G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(method_name, "RegisterStatusNotifierItem") == 0) {
    const gchar *service = NULL;
    g_variant_get(parameters, "(&s)", &service);
    register_item(service, sender);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method_name, "RegisterStatusNotifierHost") == 0) {
    const gchar *service = NULL;
    g_variant_get(parameters, "(&s)", &service);
    register_host(service, sender);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "Unknown method %s", method_name);
}

static GVariant *handle_get_property(
    G_GNUC_UNUSED GDBusConnection *connection,
    G_GNUC_UNUSED const gchar *sender, G_GNUC_UNUSED const gchar *object_path,
    G_GNUC_UNUSED const gchar *interface_name, const gchar *property_name,
    GError **error, G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(property_name, "RegisteredStatusNotifierItems") == 0) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    for (guint i = 0; i < watcher.items->len; i++) {
      const SofiTrayItem *item = g_ptr_array_index(watcher.items, i);
      g_variant_builder_add(&builder, "s", item->service);
    }
    return g_variant_builder_end(&builder);
  }

  if (g_strcmp0(property_name, "IsStatusNotifierHostRegistered") == 0) {
    /* Action purpose: the single most load-bearing value in this file. An
     * application asks this once, when it starts; if the answer is FALSE it
     * shows no tray icon at all and never asks again. Answering it correctly is
     * the difference between a tray and an empty strip. */
    return g_variant_new_boolean(watcher.self_host_registered ||
                                 g_hash_table_size(watcher.hosts) > 0);
  }

  if (g_strcmp0(property_name, "ProtocolVersion") == 0) {
    return g_variant_new_int32(WATCHER_PROTOCOL_VERSION);
  }

  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
              "Unknown property %s", property_name);
  return NULL;
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = handle_method,
    .get_property = handle_get_property,
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

  watcher.connection = connection;
  watcher.registration_id = g_dbus_connection_register_object(
      connection, WATCHER_OBJECT_PATH, watcher.introspection->interfaces[0],
      &interface_vtable, NULL, NULL, &error);

  if (watcher.registration_id == 0) {
    g_warning("Could not export %s: %s", WATCHER_INTERFACE,
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
  }
}

static void on_watcher_name_acquired(G_GNUC_UNUSED GDBusConnection *connection,
                                     const gchar *name,
                                     G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: we are the watcher AND a host, so mark ourselves registered
   * the moment the name lands rather than round-tripping a call to ourselves.
   * The signal still goes out, because items that were already running when
   * sofi started are listening for exactly it -- that is their one chance to
   * appear without being restarted. */
  watcher.self_host_registered = TRUE;
  g_message("sofi is serving %s", name);
  emit_signal("StatusNotifierHostRegistered", NULL);
}

static void on_watcher_name_lost(G_GNUC_UNUSED GDBusConnection *connection,
                                 const gchar *name,
                                 G_GNUC_UNUSED gpointer user_data) {
  /* Action purpose: another tray owns the session's watcher. Deliberately NOT
   * fatal and deliberately not a REPLACE: two watchers fighting over the name
   * would flap every item on the desktop between them. sofi's tray is empty for
   * the session, notifications are unaffected, and the reason is stated once. */
  watcher.self_host_registered = FALSE;
  g_warning("Could not take %s -- another system tray owns it. sofi's tray "
            "will stay empty; notifications are unaffected.",
            name);
}

gboolean sofi_tray_watcher_start(void) {
  GError *error = NULL;

  watcher.introspection = g_dbus_node_info_new_for_xml(introspection_xml,
                                                       &error);
  if (watcher.introspection == NULL) {
    g_warning("Could not parse the tray watcher interface: %s",
              error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  watcher.items = g_ptr_array_new_with_free_func(tray_item_free);
  watcher.hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                        host_watch_destroy);

  /* Action purpose: the host name is per-process by specification, and owning
   * it is what makes sofi visible as a host to any OTHER watcher -- which is
   * what would let the tray work if sofi loses the race for the watcher name
   * and a future version learns to read from the winner instead. */
  gchar *host_name = g_strdup_printf("org.kde.StatusNotifierHost-%d",
                                     (int)getpid());
  watcher.host_owner_id =
      g_bus_own_name(G_BUS_TYPE_SESSION, host_name,
                     G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL, NULL);
  g_free(host_name);

  /* No REPLACE: see on_watcher_name_lost. DO_NOT_QUEUE so that losing is
   * reported at once rather than leaving sofi silently queued behind a tray
   * that is not going to exit. */
  watcher.watcher_owner_id = g_bus_own_name(
      G_BUS_TYPE_SESSION, WATCHER_BUS_NAME, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
      on_bus_acquired, on_watcher_name_acquired, on_watcher_name_lost, NULL,
      NULL);

  if (watcher.watcher_owner_id == 0) {
    g_warning("Could not reach the session bus for the tray watcher.");
    sofi_tray_watcher_stop();
    return FALSE;
  }

  return TRUE;
}

void sofi_tray_watcher_stop(void) {
  if (watcher.registration_id > 0 && watcher.connection != NULL) {
    g_dbus_connection_unregister_object(watcher.connection,
                                        watcher.registration_id);
    watcher.registration_id = 0;
  }
  if (watcher.watcher_owner_id > 0) {
    g_bus_unown_name(watcher.watcher_owner_id);
    watcher.watcher_owner_id = 0;
  }
  if (watcher.host_owner_id > 0) {
    g_bus_unown_name(watcher.host_owner_id);
    watcher.host_owner_id = 0;
  }
  if (watcher.items != NULL) {
    g_ptr_array_free(watcher.items, TRUE);
    watcher.items = NULL;
  }
  if (watcher.hosts != NULL) {
    g_hash_table_destroy(watcher.hosts);
    watcher.hosts = NULL;
  }
  if (watcher.introspection != NULL) {
    g_dbus_node_info_unref(watcher.introspection);
    watcher.introspection = NULL;
  }
  watcher.connection = NULL;
  watcher.self_host_registered = FALSE;
}

guint sofi_tray_watcher_count(void) {
  return watcher.items == NULL ? 0 : watcher.items->len;
}

const SofiTrayItem *sofi_tray_watcher_nth(guint index) {
  if (watcher.items == NULL || index >= watcher.items->len) {
    return NULL;
  }
  return g_ptr_array_index(watcher.items, index);
}

#endif // SYSTEM_TRAY
