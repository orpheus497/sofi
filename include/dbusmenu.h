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

#ifndef SOFI_DBUSMENU_H
#define SOFI_DBUSMENU_H

#include <glib.h>

/**
 * @defgroup DBUSMENU Dbusmenu
 *
 * A client for `com.canonical.dbusmenu`, the protocol a StatusNotifierItem
 * publishes its menu through.
 *
 * **Written against GDBus rather than linked from `libdbusmenu-glib`, and that
 * is a deliberate choice (`DECISIONS_LOG.md` R46).** The protocol is seven
 * methods and three signals, `gio-unix-2.0` is already an unconditional
 * dependency, and `source/tray-item.c` already performs every call shape used
 * here. Canonical's library is licensed GPLv3 / LGPL2.1 / LGPL3 against sofi's
 * MIT, so taking it would have been a licensing decision made for convenience.
 *
 * **The application publishes a description of a menu, never a menu.** Nothing
 * in this protocol asks it to display anything -- there is no such method. That
 * is the design: StatusNotifierItem moved the menu out of the application's
 * process so the panel renders it. Drawing it is the host's job, which means
 * sofi's.
 *
 * @{
 */

/** One row of a menu. Separators and submenus are rows too. */
typedef struct {
  /** dbusmenu's own id for the row, needed to send #sofi_dbusmenu_event. */
  gint32 id;
  /** Display text, dbusmenu's `_` mnemonic markers stripped. Never NULL. */
  gchar *label;
  /** TRUE when the row is a separator: no label, not selectable. */
  gboolean separator;
  /** TRUE when the row opens a submenu rather than doing something. */
  gboolean submenu;
  /** FALSE when the application greyed the row out. */
  gboolean enabled;
  /**
   * `checkmark` or `radio` when the row carries one, else #SOFI_DBUSMENU_NONE.
   */
  int toggle_type;
  /** 0 off, 1 on, -1 indeterminate. Only meaningful with a toggle_type. */
  int toggle_state;
} SofiDbusmenuEntry;

/** #SofiDbusmenuEntry::toggle_type when the row carries no toggle. */
#define SOFI_DBUSMENU_NONE 0
/** #SofiDbusmenuEntry::toggle_type for a checkbox row. */
#define SOFI_DBUSMENU_CHECKMARK 1
/** #SofiDbusmenuEntry::toggle_type for a radio row. */
#define SOFI_DBUSMENU_RADIO 2

/** An opaque handle on one application's menu. */
typedef struct _SofiDbusmenu SofiDbusmenu;

/**
 * Open a menu published by an application.
 *
 * Does no I/O beyond taking the session bus: the layout is fetched by
 * #sofi_dbusmenu_read, so a caller can open, read, descend and read again
 * without reconnecting.
 *
 * @param bus_name the application's bus name, e.g. `:1.42`.
 * @param object_path the menu's object path, from the item's `Menu` property.
 *
 * @returns a handle, or NULL when the session bus is unreachable.
 */
SofiDbusmenu *sofi_dbusmenu_open(const gchar *bus_name,
                                 const gchar *object_path);

/**
 * Fetch one level of the menu, synchronously.
 *
 * Synchronous on purpose. This runs from a mode's `_init` and `_result`, where
 * there is nothing to display until the answer arrives and no view to keep
 * responsive -- the same position `sofi_notify_service_refresh_live()` is in.
 * The call is bounded by its own timeout, so a wedged application costs a short
 * pause and an empty menu rather than a hung panel.
 *
 * Sends `AboutToShow` first, as the specification requires, so an application
 * that builds its menu lazily has a chance to populate it. A failure there is
 * not fatal: many applications do not implement it.
 *
 * @param menu the handle.
 * @param parent_id the row to read the children of. 0 is the root.
 * @param count_out set to the number of entries returned. Never NULL.
 *
 * @returns a newly allocated array of @p count_out entries, to be released with
 *          #sofi_dbusmenu_entries_free. NULL with `*count_out == 0` when the
 *          menu could not be read.
 */
SofiDbusmenuEntry *sofi_dbusmenu_read(SofiDbusmenu *menu, gint32 parent_id,
                                      unsigned int *count_out);

/** Release what #sofi_dbusmenu_read returned. Safe on NULL. */
void sofi_dbusmenu_entries_free(SofiDbusmenuEntry *entries, unsigned int count);

/**
 * Tell the application a row was chosen.
 *
 * Fire and forget, like the item activation calls in `tray-item.c`: the reply
 * carries nothing a user could act on, and the panel is closing regardless.
 *
 * @param menu the handle.
 * @param id the row's #SofiDbusmenuEntry::id.
 */
void sofi_dbusmenu_event(SofiDbusmenu *menu, gint32 id);

/** Close the handle and release it. Safe on NULL. */
void sofi_dbusmenu_close(SofiDbusmenu *menu);

/** @}*/
#endif // SOFI_DBUSMENU_H
