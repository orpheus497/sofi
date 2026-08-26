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
 * @brief Reading org.sofi.Tray from the task strip.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "TrayClient"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <gio/gio.h>

#include "tray-client.h"
#include "tray-service.h"

/**
 * Action purpose: bound the wait. A menu that hangs because a daemon stopped
 * answering is worse than a menu with no tray in it, and this call happens while
 * the panel is being built.
 */
#define TRAY_CLIENT_TIMEOUT_MS 500

static GPtrArray *entries = NULL;

static void entry_free(gpointer data) {
  SofiTrayEntry *e = (SofiTrayEntry *)data;

  if (e == NULL) {
    return;
  }
  g_free(e->service);
  g_free(e->title);
  g_free(e->icon_name);
  g_free(e->icon_theme_path);
  if (e->surface != NULL) {
    cairo_surface_destroy(e->surface);
  }
  g_free(e);
}

/**
 * Function purpose: wrap the daemon's pixels in a cairo surface.
 *
 * The bytes arrive already validated, already premultiplied and already in
 * native byte order -- all of that happened once, in the daemon, where the
 * hostile input actually lands. What is left here is a copy into a surface with
 * cairo's own stride, which is not necessarily `width * 4`.
 */
static cairo_surface_t *surface_from_pixels(const guint8 *pixels, gsize length,
                                            gint width, gint height) {
  if (pixels == NULL || width <= 0 || height <= 0) {
    return NULL;
  }
  /* Defence in depth: the daemon is sofi's own, but this is still a value that
   * arrived over IPC and the loop below trusts it. */
  if (length < (gsize)width * (gsize)height * 4u) {
    g_warning("Tray icon payload is short: %" G_GSIZE_FORMAT " bytes for %dx%d",
              length, width, height);
    return NULL;
  }

  cairo_surface_t *surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return NULL;
  }

  guint8 *dst = cairo_image_surface_get_data(surface);
  int stride = cairo_image_surface_get_stride(surface);

  for (gint y = 0; y < height; y++) {
    memcpy(dst + (gsize)y * (gsize)stride,
           pixels + (gsize)y * (gsize)width * 4u, (gsize)width * 4u);
  }

  cairo_surface_mark_dirty(surface);
  return surface;
}

gboolean sofi_tray_client_refresh(void) {
  GError *error = NULL;
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

  if (entries == NULL) {
    entries = g_ptr_array_new_with_free_func(entry_free);
  }
  g_ptr_array_set_size(entries, 0);

  if (bus == NULL) {
    g_debug("No session bus; the tray zone will be empty: %s",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  /* NO_AUTO_START: a task strip must never start a tray daemon as a side effect
   * of being summoned. The same rule the notification clear flags follow. */
  GVariant *reply = g_dbus_connection_call_sync(
      bus, SOFI_TRAY_BUS_NAME, SOFI_TRAY_OBJECT_PATH, SOFI_TRAY_INTERFACE,
      SOFI_TRAY_METHOD_LIST_ITEMS, NULL,
      G_VARIANT_TYPE("(" SOFI_TRAY_LIST_ITEMS_SIGNATURE ")"),
      G_DBUS_CALL_FLAGS_NO_AUTO_START, TRAY_CLIENT_TIMEOUT_MS, NULL, &error);

  g_object_unref(bus);

  if (reply == NULL) {
    /* Expected whenever no tray daemon runs, which is a configuration and not a
     * fault, so this is debug rather than a warning. */
    g_debug("No tray daemon answered: %s",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  GVariantIter *iter = NULL;
  g_variant_get(reply, "(" SOFI_TRAY_LIST_ITEMS_SIGNATURE ")", &iter);

  const gchar *service = NULL, *title = NULL, *icon_name = NULL,
              *theme_path = NULL;
  guint32 status = 0, width = 0, height = 0;
  gboolean is_menu = FALSE;
  GVariant *pixels = NULL;

  while (g_variant_iter_next(iter, "(&s&s&s&subuu@ay)", &service, &title,
                             &icon_name, &theme_path, &status, &is_menu, &width,
                             &height, &pixels)) {
    SofiTrayEntry *e = g_malloc0(sizeof(SofiTrayEntry));
    e->service = g_strdup(service);
    e->title = g_strdup(title);
    e->icon_name = g_strdup(icon_name);
    e->icon_theme_path = g_strdup(theme_path);
    e->is_menu = is_menu;

    gsize length = 0;
    const guint8 *data = g_variant_get_fixed_array(pixels, &length, 1);
    e->surface = surface_from_pixels(data, length, (gint)width, (gint)height);

    g_ptr_array_add(entries, e);
    g_variant_unref(pixels);
  }

  g_variant_iter_free(iter);
  g_variant_unref(reply);

  g_debug("Tray zone: %u item(s)", entries->len);
  return TRUE;
}

guint sofi_tray_client_count(void) {
  return entries == NULL ? 0 : entries->len;
}

const SofiTrayEntry *sofi_tray_client_nth(guint index) {
  if (entries == NULL || index >= entries->len) {
    return NULL;
  }
  return g_ptr_array_index(entries, index);
}

static void call_activation(const gchar *method, const gchar *service, gint x,
                            gint y) {
  if (service == NULL) {
    return;
  }
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);

  if (bus == NULL) {
    return;
  }
  /* Fire and forget. The daemon forwards this to the application, which may or
   * may not do anything with it; neither outcome is something the user of a
   * task strip can act on. */
  g_dbus_connection_call(bus, SOFI_TRAY_BUS_NAME, SOFI_TRAY_OBJECT_PATH,
                         SOFI_TRAY_INTERFACE, method,
                         g_variant_new("(sii)", service, x, y), NULL,
                         G_DBUS_CALL_FLAGS_NO_AUTO_START,
                         TRAY_CLIENT_TIMEOUT_MS, NULL, NULL, NULL);
  g_dbus_connection_flush_sync(bus, NULL, NULL);
  g_object_unref(bus);
}

void sofi_tray_client_activate(const gchar *service, gint x, gint y) {
  call_activation(SOFI_TRAY_METHOD_ACTIVATE, service, x, y);
}

void sofi_tray_client_secondary_activate(const gchar *service, gint x, gint y) {
  call_activation(SOFI_TRAY_METHOD_SECONDARY_ACTIVATE, service, x, y);
}

void sofi_tray_client_cleanup(void) {
  if (entries != NULL) {
    g_ptr_array_free(entries, TRUE);
    entries = NULL;
  }
}

#endif // SYSTEM_TRAY
