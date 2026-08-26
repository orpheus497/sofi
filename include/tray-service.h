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

#ifndef SOFI_TRAY_SERVICE_H
#define SOFI_TRAY_SERVICE_H

#include <glib.h>

/**
 * @defgroup TRAYSERVICE org.sofi.Tray
 *
 * What the tray daemon offers the rest of sofi.
 *
 * The task strip is a separate, summoned invocation (`sofi -show window`) and
 * cannot read the daemon's memory, so everything it needs about the tray travels
 * over this interface. That is the same shape the notification history already
 * uses against `org.sofi.Notifications`, and for the same reason.
 *
 * **A dedicated bus name, not a second interface on the watcher's.** The
 * notification daemon puts `org.sofi.Notifications` on the object it already
 * owns, and this deliberately does not follow it: sofi may LOSE the race for
 * `org.kde.StatusNotifierWatcher` to another tray, and a private interface
 * hanging off a name sofi might not hold is a private interface the strip cannot
 * rely on reaching. `org.sofi.Tray` is sofi's own name and is always there while
 * the daemon is.
 *
 * @{
 */

/** Bus name, object and interface the task strip talks to. */
#define SOFI_TRAY_BUS_NAME "org.sofi.Tray"
#define SOFI_TRAY_OBJECT_PATH "/org/sofi/Tray"
#define SOFI_TRAY_INTERFACE "org.sofi.Tray"

/** Methods and signals on it. */
#define SOFI_TRAY_METHOD_LIST_ITEMS "ListItems"
#define SOFI_TRAY_METHOD_ACTIVATE "Activate"
#define SOFI_TRAY_METHOD_SECONDARY_ACTIVATE "SecondaryActivate"
#define SOFI_TRAY_SIGNAL_CHANGED "Changed"

/**
 * The reply signature of ListItems, spelled out because the strip has to parse
 * it and a mistake here is a silent mismatch rather than an error:
 *
 *   s   service          -- the identifier to quote back to Activate
 *   s   title            -- already falls back to Id when the app set none
 *   s   icon name        -- attention override already applied; "" if none
 *   s   icon theme path  -- the app's private icon directory; "" if none
 *   u   status           -- SofiTrayStatus
 *   b   is menu          -- left click should open the menu, not activate
 *   u   icon width       -- 0 when there is no pixmap
 *   u   icon height
 *   ay  pixels           -- premultiplied ARGB32, native endian, w*h*4 bytes
 *
 * Pixels rather than a file path (R40): a tray icon is a couple of kilobytes, so
 * the reply stays small, and the receiving side hands the buffer straight to
 * cairo. A file would add a temp-file lifecycle whose failure mode is stale
 * icons surviving a crash.
 */
#define SOFI_TRAY_LIST_ITEMS_SIGNATURE "a(ssssubuuay)"

/**
 * Export org.sofi.Tray.
 *
 * @returns FALSE when the session bus cannot be reached. Failing to take the
 *          name is reported asynchronously and costs the strip its tray, not the
 *          daemon its watcher.
 */
gboolean sofi_tray_service_start(void);

/** Release the name and unexport. */
void sofi_tray_service_stop(void);

/**
 * Tell subscribers the tray changed.
 *
 * Coalesced: a burst -- several items reacting to the same event, or one item
 * announcing a status and an icon together -- becomes one signal. A subscriber
 * rebuilds its whole zone from ListItems anyway, so a second signal describing
 * the same rebuild is pure noise.
 */
void sofi_tray_service_notify_changed(void);

/**@}*/
#endif // SOFI_TRAY_SERVICE_H
