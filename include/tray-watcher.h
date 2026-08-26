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

#ifndef SOFI_TRAY_WATCHER_H
#define SOFI_TRAY_WATCHER_H

#include <glib.h>

#include "tray-item.h"

/**
 * @defgroup TRAYWATCHER StatusNotifier watcher
 *
 * sofi as the session's `org.kde.StatusNotifierWatcher`, and as a
 * StatusNotifierHost registered against it.
 *
 * There is no Wayland system-tray protocol. What every Wayland desktop actually
 * implements is StatusNotifierItem: three D-Bus interfaces and nothing else --
 * no surface, no input routing, no privileged operation. That is why this lives
 * in sofi rather than in the compositor (`DECISIONS_LOG.md` F9-F12).
 *
 * **Why it lives in the daemon and not in a menu.** Applications consult
 * `IsStatusNotifierHostRegistered` when they start, and one that finds no host
 * shows no tray icon at all and never asks again. A host must therefore already
 * be running before the applications are, which a summoned `sofi -show ...`
 * process can never be. The daemon is the session-long process, so it owns the
 * watcher and the item set; the task strip queries it and renders (R39).
 *
 * This file is the registry only. It tracks which items exist and publishes the
 * spec's signals; it reads nothing from them. Item properties, icons and
 * activation are the next layer.
 *
 * @{
 */

/**
 * One registered item.
 *
 * The three name fields are not redundant. `service` is the string the spec's
 * `RegisteredStatusNotifierItems` property carries and the identifier hosts
 * quote back; `bus_name` and `object_path` are what a method call actually needs,
 * and the two are only derivable from the first by re-parsing it.
 */
typedef struct {
  /** Canonical `<bus_name><object_path>`, as published to other hosts. */
  gchar *service;
  /** The name to address the item on. */
  gchar *bus_name;
  /** The item's object, usually but NOT always `/StatusNotifierItem`. */
  gchar *object_path;
  /** g_bus_watch_name id, so an application that exits without unregistering
   * is still reaped. Many do exactly that. */
  guint watch_id;
  /**
   * What the item currently looks like. NULL only if the session bus became
   * unreachable between registration and here, which is not a case worth
   * special-casing anywhere else -- accessors on a NULL proxy answer safely.
   */
  SofiTrayItemProxy *proxy;
} SofiTrayItem;

/**
 * Take `org.kde.StatusNotifierWatcher` and a host name, and start accepting
 * registrations.
 *
 * @returns FALSE when the session bus cannot be reached at all. Losing the race
 *          for the watcher name to another tray is reported asynchronously and
 *          is not a failure of the daemon: notifications carry on, and the tray
 *          is simply empty for the session.
 */
gboolean sofi_tray_watcher_start(void);

/** Release both names, stop watching every item, and drop the registry. */
void sofi_tray_watcher_stop(void);

/** Number of currently registered items. */
guint sofi_tray_watcher_count(void);

/**
 * Registered item at @p index, in registration order.
 *
 * Order is deliberately stable rather than sorted: a tray whose icons move when
 * an unrelated application starts is one you cannot build muscle memory for.
 *
 * @returns NULL when @p index is out of range.
 */
const SofiTrayItem *sofi_tray_watcher_nth(guint index);

/**@}*/
#endif // SOFI_TRAY_WATCHER_H
