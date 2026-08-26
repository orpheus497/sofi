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

#ifndef SOFI_MODE_WAYLAND_WINDOW_H
#define SOFI_MODE_WAYLAND_WINDOW_H

#include "mode.h"

/**
 * @defgroup WINDOWMode Window
 * @ingroup MODES
 *
 * @{
 */
#if defined(WINDOW_MODE) && defined(ENABLE_WAYLAND)

extern Mode wayland_window_mode;

/**
 * Begin tracking the compositor's toplevels, for callers outside this mode.
 *
 * **Call this from a mode's `_init`, never later.** It round-trips the display,
 * and a round trip dispatches sofi's own surface events — harmless in `_init`,
 * where no view exists yet, and a re-entrant crash from inside `_result`, which
 * is where an earlier version of this tried to do the work (`TODOS.md` Q21).
 *
 * The listeners stay attached afterwards, so sofi's main loop keeps the list
 * current: a window opened or closed while a panel is up arrives as an ordinary
 * event. Callers get a live list, not a snapshot.
 *
 * Idempotent, and cheap to call when it will not be used: one registry bind and
 * two round trips, which is what the window mode already pays on every
 * invocation.
 *
 * @returns FALSE when there is nothing to track — an X11 session, or a
 *          compositor without wlr-foreign-toplevel-management. Activation then
 *          simply never matches.
 */
gboolean sofi_wayland_window_toplevels_open(void);

/**
 * Raise the window belonging to an application, named by its `desktop-entry`.
 *
 * Exported out of the window mode rather than reimplemented elsewhere: toplevel
 * activation, the protocol listeners and the teardown all live there, and a
 * second copy of ~90 lines of protocol handling was the option this one was
 * chosen over (`DECISIONS_LOG.md` R43, closing Q20).
 *
 * Its caller is the notification history: a notification carries a
 * `desktop-entry` hint and nothing else that could identify the window that
 * produced it.
 *
 * Safe from a mode's `_result`: it matches against the list
 * #sofi_wayland_window_toplevels_open already built and does no more than
 * activate and flush. Without that call first it always returns FALSE.
 *
 * **Best-effort by nature, and it fails closed.** `desktop-entry` and `app_id`
 * are different namespaces that often agree; matching is restricted to exact and
 * reversed-DNS-tail equality, because a looser rule eventually raises the WRONG
 * window — worse than raising none, since the user asked to be taken somewhere
 * and would be taken somewhere else.
 *
 * @param desktop_entry the sender's desktop file basename, from the
 *                      notification's `desktop-entry` hint.
 *
 * @returns TRUE when a window was matched and activated. FALSE covers every
 *          other case — no such window, no compositor support, no seat — and a
 *          caller should treat it as "this notification has nowhere to go"
 *          rather than as an error worth reporting.
 */
gboolean sofi_wayland_window_activate_app_id(const char *desktop_entry);

/**
 * Stop tracking toplevels and release everything
 * #sofi_wayland_window_toplevels_open bound. Safe when it was never opened.
 */
void sofi_wayland_window_toplevels_close(void);

#endif
/** @}*/
#endif // SOFI_MODE_WAYLAND_WINDOW_H
