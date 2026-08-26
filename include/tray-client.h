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
  /**
   * The item's `com.canonical.dbusmenu` object path, or "" when it published
   * none. Together with #SofiTrayEntry::bus_name this is everything needed to
   * read the menu; **whether this is non-empty is the reliable test for "this
   * item has a menu"**, not #SofiTrayEntry::is_menu (F29).
   */
  gchar *menu_path;
  /**
   * The application's bus name, split from #SofiTrayEntry::service so callers
   * do not each repeat the parse. "" when the service was not of the expected
   * shape.
   */
  gchar *bus_name;
  /** The item's `ItemIsMenu`. **Advisory only** -- see #SofiTrayEntry::menu_path. */
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

/**
 * Ask an item to show its own context menu, via the daemon.
 *
 * Only for an item that published no #SofiTrayEntry::menu_path: when it did,
 * sofi renders that menu itself so every tray menu behaves the same (R46).
 *
 * @param service the item to ask, from #SofiTrayEntry::service.
 * @param x,y screen coordinates to place the menu near.
 */
void sofi_tray_client_context_menu(const gchar *service, gint x, gint y);

/** Called when the daemon reports that the tray changed. */
typedef void (*SofiTrayChangedFunc)(gpointer user_data);

/**
 * Subscribe to the daemon's Changed signal.
 *
 * The task strip is summoned but not momentary -- it stays up across minimising
 * and maximising windows, and `close-on-delete: false` keeps it through closing
 * one. An application starting or a battery icon changing while it is on screen
 * has to be picked up, or the tray is a snapshot of whenever the strip happened
 * to open.
 *
 * The callback fires on the main loop. **One subscription exists at a time**, and
 * that is a property of the view rather than a limitation here: a layout may
 * carry at most one `tray` widget (`sofi_view_add_widget` refuses a second), and
 * a process shows one such surface. Subscribing again replaces what is there.
 */
void sofi_tray_client_watch(SofiTrayChangedFunc callback, gpointer user_data);

/**
 * Drop the subscription, but only if @p owner is the one that registered it.
 *
 * Ownership is checked rather than assumed so that tearing one consumer down
 * cannot silently unsubscribe another. Today only one can exist, so this can
 * never refuse -- which is exactly why it is cheap to be correct about now,
 * instead of discovering the assumption later.
 *
 * @returns TRUE when the subscription was dropped, FALSE when @p owner did not
 *          hold it (including when there was none).
 */
gboolean sofi_tray_client_unwatch(gpointer owner);

/** Drop the snapshot and release the bus connection. */
void sofi_tray_client_cleanup(void);

/**@}*/
#endif // SOFI_TRAY_CLIENT_H
