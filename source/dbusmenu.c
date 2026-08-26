/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2013-2022 Qball Cow <qball@gmpclient.org>
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
 * @brief A `com.canonical.dbusmenu` client, on GDBus.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "Dbusmenu"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <gio/gio.h>
#include <string.h>

#include "dbusmenu.h"

/** The interface every method below is called on. */
#define MENU_INTERFACE "com.canonical.dbusmenu"

/**
 * Action purpose: bound every call, because the peer is an arbitrary
 * application and these are synchronous.
 *
 * Shorter than the item property timeout in `tray-item.c` (3s): that one runs
 * in the daemon with nothing waiting on it, while this one runs with a menu
 * half-open in front of the user. Two seconds is long enough for an application
 * that is merely busy and short enough that a wedged one reads as an empty menu
 * rather than a frozen panel.
 */
#define MENU_CALL_TIMEOUT_MS 2000

/**
 * Action purpose: a hard ceiling on rows per level.
 *
 * The peer decides how many rows to send. A menu longer than this is either a
 * defect or hostile, and either way rendering it is not useful -- the listview
 * would be scrolling through thousands of entries nobody can read. Chosen far
 * above any real menu; the one measured during R46 had 14 rows.
 */
#define MENU_MAX_ENTRIES 512

/** The properties worth asking for. Anything else is ignored on arrival. */
static const gchar *const wanted_properties[] = {
    "label",        "enabled",      "visible", "type",
    "children-display", "toggle-type", "toggle-state", NULL};

struct _SofiDbusmenu {
  GDBusConnection *connection;
  gchar *bus_name;
  gchar *object_path;
};

/**
 * Function purpose: strip dbusmenu's mnemonic markers from a label.
 *
 * dbusmenu carries GTK-style mnemonics: a single `_` marks the accelerator and
 * `__` is a literal underscore. sofi has no mnemonic handling and no
 * accelerator row, so a raw label renders as `_Quit`, which looks broken.
 *
 * @param raw the label as the application sent it.
 *
 * @returns a newly allocated label. Never NULL.
 */
static gchar *strip_mnemonics(const gchar *raw) {
  if (raw == NULL) {
    return g_strdup("");
  }

  GString *out = g_string_sized_new(strlen(raw));
  for (const gchar *p = raw; *p != '\0'; p++) {
    if (*p != '_') {
      g_string_append_c(out, *p);
      continue;
    }
    if (*(p + 1) == '_') {
      /* `__` is an escaped underscore: emit one and skip the pair. */
      g_string_append_c(out, '_');
      p++;
      continue;
    }
    /* A lone `_` marks the next character as the accelerator. Drop the marker
     * and keep the character. */
  }
  return g_string_free(out, FALSE);
}

/**
 * Function purpose: turn one dbusmenu property dictionary into an entry.
 *
 * @param id the row's dbusmenu id.
 * @param props the row's properties, as `a{sv}`.
 * @param entry filled in on success.
 *
 * @returns FALSE when the row should not be shown at all, which is the one
 *          case the caller must not render: `visible` false means the
 *          application is hiding the row, not greying it out.
 */
static gboolean entry_from_properties(gint32 id, GVariant *props,
                                      SofiDbusmenuEntry *entry) {
  gboolean visible = TRUE;
  g_variant_lookup(props, "visible", "b", &visible);
  if (!visible) {
    return FALSE;
  }

  const gchar *type = NULL;
  g_variant_lookup(props, "type", "&s", &type);

  const gchar *children = NULL;
  g_variant_lookup(props, "children-display", "&s", &children);

  const gchar *label = NULL;
  g_variant_lookup(props, "label", "&s", &label);

  const gchar *toggle = NULL;
  g_variant_lookup(props, "toggle-type", "&s", &toggle);

  entry->id = id;
  entry->separator = (g_strcmp0(type, "separator") == 0);
  entry->submenu = (g_strcmp0(children, "submenu") == 0);
  entry->label = entry->separator ? g_strdup("") : strip_mnemonics(label);

  entry->enabled = TRUE;
  g_variant_lookup(props, "enabled", "b", &entry->enabled);

  entry->toggle_type = SOFI_DBUSMENU_NONE;
  if (g_strcmp0(toggle, "checkmark") == 0) {
    entry->toggle_type = SOFI_DBUSMENU_CHECKMARK;
  } else if (g_strcmp0(toggle, "radio") == 0) {
    entry->toggle_type = SOFI_DBUSMENU_RADIO;
  }

  /* Absent toggle-state means indeterminate per the specification, which is
   * also what an application sends for a toggle it has not resolved yet. */
  entry->toggle_state = -1;
  gint32 st = 0;
  if (g_variant_lookup(props, "toggle-state", "i", &st)) {
    entry->toggle_state = (int)st;
  }

  return TRUE;
}

SofiDbusmenu *sofi_dbusmenu_open(const gchar *bus_name,
                                 const gchar *object_path) {
  if (bus_name == NULL || bus_name[0] == '\0' || object_path == NULL ||
      object_path[0] == '\0') {
    return NULL;
  }

  GError *error = NULL;
  GDBusConnection *connection =
      g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (connection == NULL) {
    g_warning("Cannot reach the session bus for a tray menu: %s",
              error->message);
    g_error_free(error);
    return NULL;
  }

  SofiDbusmenu *menu = g_malloc0(sizeof(SofiDbusmenu));
  menu->connection = connection;
  menu->bus_name = g_strdup(bus_name);
  menu->object_path = g_strdup(object_path);
  return menu;
}

SofiDbusmenuEntry *sofi_dbusmenu_read(SofiDbusmenu *menu, gint32 parent_id,
                                      unsigned int *count_out) {
  *count_out = 0;
  if (menu == NULL) {
    return NULL;
  }

  /* Action purpose: AboutToShow before GetLayout, in that order, because an
   * application that builds its menu lazily populates it in response. Its reply
   * says whether the layout changed, which does not matter here -- the layout
   * is being fetched either way -- so the reply is discarded and a failure is
   * ignored. Many applications do not implement it at all. */
  GVariant *ignored = g_dbus_connection_call_sync(
      menu->connection, menu->bus_name, menu->object_path, MENU_INTERFACE,
      "AboutToShow", g_variant_new("(i)", parent_id), G_VARIANT_TYPE("(b)"),
      G_DBUS_CALL_FLAGS_NO_AUTO_START, MENU_CALL_TIMEOUT_MS, NULL, NULL);
  if (ignored != NULL) {
    g_variant_unref(ignored);
  }

  GError *error = NULL;
  /* Depth 1: this level's rows and their properties, but not their children's.
   * Descending is a second call, which is what makes a deep menu cost nothing
   * until the user actually opens it. */
  GVariant *reply = g_dbus_connection_call_sync(
      menu->connection, menu->bus_name, menu->object_path, MENU_INTERFACE,
      "GetLayout",
      g_variant_new("(ii^as)", parent_id, 1, (gchar **)wanted_properties),
      G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NO_AUTO_START,
      MENU_CALL_TIMEOUT_MS, NULL, &error);
  if (reply == NULL) {
    g_warning("Cannot read the tray menu at %s: %s", menu->object_path,
              error->message);
    g_error_free(error);
    return NULL;
  }

  guint32 revision = 0;
  GVariant *layout = NULL;
  g_variant_get(reply, "(u@(ia{sv}av))", &revision, &layout);

  gint32 root_id = 0;
  GVariant *root_props = NULL;
  GVariant *children = NULL;
  g_variant_get(layout, "(i@a{sv}@av)", &root_id, &root_props, &children);

  gsize n = g_variant_n_children(children);
  if (n > MENU_MAX_ENTRIES) {
    g_warning("Tray menu at %s reported %zu rows; showing the first %d.",
              menu->object_path, (size_t)n, MENU_MAX_ENTRIES);
    n = MENU_MAX_ENTRIES;
  }

  SofiDbusmenuEntry *entries =
      n > 0 ? g_malloc0(sizeof(SofiDbusmenuEntry) * n) : NULL;
  unsigned int count = 0;

  for (gsize i = 0; i < n; i++) {
    GVariant *wrapper = g_variant_get_child_value(children, i);
    GVariant *child = g_variant_get_variant(wrapper);

    gint32 child_id = 0;
    GVariant *props = NULL;
    GVariant *grandchildren = NULL;
    g_variant_get(child, "(i@a{sv}@av)", &child_id, &props, &grandchildren);

    if (entry_from_properties(child_id, props, &entries[count])) {
      count++;
    }

    g_variant_unref(props);
    g_variant_unref(grandchildren);
    g_variant_unref(child);
    g_variant_unref(wrapper);
  }

  g_variant_unref(root_props);
  g_variant_unref(children);
  g_variant_unref(layout);
  g_variant_unref(reply);

  g_debug("Tray menu %s level %d: %u row(s), revision %u", menu->object_path,
          parent_id, count, revision);

  *count_out = count;
  return entries;
}

void sofi_dbusmenu_entries_free(SofiDbusmenuEntry *entries,
                                unsigned int count) {
  if (entries == NULL) {
    return;
  }
  for (unsigned int i = 0; i < count; i++) {
    g_free(entries[i].label);
  }
  g_free(entries);
}

void sofi_dbusmenu_event(SofiDbusmenu *menu, gint32 id) {
  if (menu == NULL) {
    return;
  }
  /* `clicked` is the event id the specification defines for activation, and the
   * data argument is unused for it. The timestamp is advisory; applications use
   * it for focus-stealing decisions, and 0 means "no timestamp available",
   * which is honest here -- the click happened in a compositor sofi cannot get
   * an X-style server time from. */
  g_dbus_connection_call(menu->connection, menu->bus_name, menu->object_path,
                         MENU_INTERFACE, "Event",
                         g_variant_new("(isvu)", id, "clicked",
                                       g_variant_new_int32(0), (guint32)0),
                         NULL, G_DBUS_CALL_FLAGS_NO_AUTO_START,
                         MENU_CALL_TIMEOUT_MS, NULL, NULL, NULL);
  /* Flush rather than leave it queued: the caller closes the panel immediately
   * after this, and process teardown must not race the send. */
  g_dbus_connection_flush_sync(menu->connection, NULL, NULL);
}

void sofi_dbusmenu_close(SofiDbusmenu *menu) {
  if (menu == NULL) {
    return;
  }
  g_free(menu->bus_name);
  g_free(menu->object_path);
  g_object_unref(menu->connection);
  g_free(menu);
}

#endif // SYSTEM_TRAY
