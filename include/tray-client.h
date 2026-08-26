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

#ifndef SOFI_TRAY_CLIENT_H
#define SOFI_TRAY_CLIENT_H

#include <cairo.h>
#include <glib.h>

/**
 * @defgroup TRAYCLIENT Tray client
 *
 * The reading half of the tray, used by the task strip.
 *
 * `sofi -show window` is a separate, summoned process from `sofi -tray-daemon`.
 * It cannot see the daemon's items, so it asks over `org.sofi.Tray` and renders
 * the answer. Everything here is a snapshot: it was true when the call returned
 * and an application may have exited since.
 *
 * @{
 */

/** One tray item, as the strip needs it. */
typedef struct {
  /** Identifier to quote back when activating. Never NULL. */
  gchar *service;
  /** Display name, already falling back to Id upstream. Never NULL. */
  gchar *title;
  /** Icon name with the attention override applied; "" when there is none. */
  gchar *icon_name;
  /** The application's private icon directory; "" when unset. */
  gchar *icon_theme_path;
  /** Left click should open the item's menu rather than activate it. */
  gboolean is_menu;
  /** Decoded icon, or NULL when the item offered none. Owned here. */
  cairo_surface_t *surface;
} SofiTrayEntry;

/**
 * Fetch the current item list from the tray daemon, replacing what is held.
 *
 * **Synchronous, with a short timeout.** The peer is sofi's own daemon rather
 * than an arbitrary application, and this runs while a menu is being built, so
 * the established pattern in this tree applies -- `source/modes/sheets.c` calls
 * hikari's control socket the same way, for the same reason: failing fast with
 * an empty zone beats a frozen panel. The timeout is what makes that true.
 *
 * @returns FALSE when no tray daemon answered, which is the ordinary case on a
 *          session that does not run one. The zone then renders empty.
 */
gboolean sofi_tray_client_refresh(void);

/** Number of items in the last snapshot. */
guint sofi_tray_client_count(void);

/** Item at @p index, or NULL when out of range. */
const SofiTrayEntry *sofi_tray_client_nth(guint index);

/**
 * Ask the daemon to activate an item.
 *
 * Fire and forget, and deliberately by service string rather than index: the
 * snapshot may be stale, and a stale string matches nothing where a stale index
 * would activate whichever item moved into that slot.
 */
void sofi_tray_client_activate(const gchar *service, gint x, gint y);

/** Secondary activation -- middle click. */
void sofi_tray_client_secondary_activate(const gchar *service, gint x, gint y);

/** Drop the snapshot and release the bus connection. */
void sofi_tray_client_cleanup(void);

/**@}*/
#endif // SOFI_TRAY_CLIENT_H
