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

#ifndef SOFI_MODE_TRAY_MENU_H
#define SOFI_MODE_TRAY_MENU_H

#include "mode.h"

/**
 * @defgroup TRAYMENUMode TrayMenu
 * @ingroup MODES
 *
 * One tray item's `com.canonical.dbusmenu`, rendered as an ordinary sofi list.
 *
 * **The application never draws this menu, and cannot.** Under
 * StatusNotifierItem it publishes a *description* -- labels, separators, toggle
 * state, which rows open submenus -- and rendering it is the host's job. That
 * is the deliberate break from the old X11 XEmbed tray, where an application
 * embedded a window and drew its own menu.
 *
 * Shaped after `source/modes/filebrowser.c` rather than inventing anything: a
 * submenu is entered by replacing the list and returning `RESET_DIALOG`, with a
 * `..` row to come back, in the same surface. That is why tray menus needed no
 * popup primitive (`DECISIONS_LOG.md` R46).
 *
 * @{
 */
#ifdef SYSTEM_TRAY

/**
 * Point the mode at an item's menu, before switching to it.
 *
 * Modes are singletons created at startup, so the target cannot be a
 * constructor argument. Call this from the click handler and then switch; the
 * mode's `_init` picks it up.
 *
 * Copies both strings. Passing NULL or "" for either clears the target, and the
 * mode then shows an empty list rather than the previous item's menu -- stale
 * is worse than empty here, because the rows would look real and act on the
 * wrong application.
 *
 * @param bus_name the application's bus name, from #SofiTrayEntry::bus_name.
 * @param object_path the menu object, from #SofiTrayEntry::menu_path.
 * @param title the item's title, used as the panel's prompt.
 */
void sofi_tray_menu_set_target(const char *bus_name, const char *object_path,
                               const char *title);

/**
 * #Mode object rendering one tray item's menu.
 */
extern Mode tray_menu_mode;

#endif
/**@}*/
#endif // SOFI_MODE_TRAY_MENU_H
