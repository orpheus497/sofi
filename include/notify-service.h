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

#ifndef SOFI_NOTIFY_SERVICE_H
#define SOFI_NOTIFY_SERVICE_H

#include <glib.h>

#include "notify-store.h"

/**
 * @defgroup NOTIFYSERVICE Notification service
 *
 * sofi as the session's org.freedesktop.Notifications server.
 *
 * The service owns the bus name for the whole session, pushes incoming
 * notifications into the ring buffer, and emits the two signals the
 * specification requires. It knows nothing about the view: the store's changed
 * callback is what wakes the banner.
 *
 * @{
 */

/**
 * Take the bus name and export the interface.
 *
 * Requests the name with REPLACE, ALLOW_REPLACEMENT and DO_NOT_QUEUE: a newly
 * started sofi daemon takes over from a running one, which exits cleanly
 * through name_lost, and a daemon that cannot take the name gives up at once
 * rather than sitting in a queue behind another notification server.
 *
 * @returns FALSE when the session bus cannot be reached at all. Losing the
 *          race for the name is reported asynchronously through name_lost.
 */
gboolean sofi_notify_service_start(void);

/** Release the bus name and drop the store. */
void sofi_notify_service_stop(void);

/**
 * Invoke one of a notification's actions and retire it.
 *
 * Emits ActionInvoked followed by NotificationClosed with reason DISMISSED,
 * which is the order the specification requires: a client that removes its
 * bookkeeping on the close signal must have seen the action first.
 *
 * @param action_index index into the notification's action pairs, not into the
 *                     flat array.
 */
void sofi_notify_service_invoke_action(guint32 id, guint action_index);

/**
 * Dismiss a notification as though the user clicked it away.
 */
void sofi_notify_service_dismiss(guint32 id);

/**
 * Number of actions on a notification. The spec's array is flat key/label
 * pairs, so this is half its length.
 */
guint sofi_notify_actions_count(const SofiNotification *n);

/**
 * Human-readable label of the nth action, or NULL when out of range.
 */
const gchar *sofi_notify_action_label(const SofiNotification *n, guint index);

/** Method names on org.sofi.Notifications. */
#define SOFI_NOTIFY_METHOD_DISMISS_ALL "DismissAll"
#define SOFI_NOTIFY_METHOD_CLEAR_HISTORY "ClearHistory"
#define SOFI_NOTIFY_METHOD_DISMISS "Dismiss"
#define SOFI_NOTIFY_METHOD_INVOKE_ACTION "InvokeAction"
#define SOFI_NOTIFY_METHOD_GET_LIVE "GetLive"

/**
 * Outcome of sofi_notify_service_call_daemon().
 *
 * ABSENT and FAILED are kept apart because the callers must not treat them
 * alike, and the bar for ABSENT is deliberately high: only the bus answering
 * that nothing owns the name clears it. A caller with its own copy of the
 * history may mutate it then, and only then. Everything else -- an unreachable
 * bus, a timeout, a reply saying the interface or method is unknown -- is
 * FAILED, because a daemon may be running and own the authoritative ring, and
 * acting locally would write a history file it is about to overwrite from state
 * we never saw.
 */
typedef enum {
  /** A sofi daemon received the call and performed the mutation. */
  SOFI_NOTIFY_DAEMON_HANDLED,
  /** The bus reports that nothing owns the name: no daemon exists to ask. */
  SOFI_NOTIFY_DAEMON_ABSENT,
  /** The call could not be completed; whether a daemon is running is unknown. */
  SOFI_NOTIFY_DAEMON_FAILED,
} SofiNotifyDaemonResult;

/**
 * Ask the RUNNING daemon to mutate its ring, from another process.
 *
 * The history menu and the `-notification-clear*` flags are separate
 * invocations of sofi with no access to the daemon's memory. Reading the
 * persisted file gives them a copy; mutating that copy would be overwritten the
 * next time the daemon saved, which it does on every change. So the mutation is
 * sent to the owner of the ring instead, over org.sofi.Notifications.
 *
 * Never activates a daemon: if none is running there is no authoritative ring
 * to contradict, and starting one to service a clear would leave a daemon the
 * user did not ask for.
 *
 * @param method SOFI_NOTIFY_METHOD_DISMISS_ALL or
 *               SOFI_NOTIFY_METHOD_CLEAR_HISTORY.
 *
 * @returns SOFI_NOTIFY_DAEMON_HANDLED, _ABSENT or _FAILED. The caller owns the
 *          decision about what to do with the last two, and should not treat
 *          them as the same answer.
 */
SofiNotifyDaemonResult sofi_notify_service_call_daemon(const gchar *method);

/**
 * Ask the running daemon to retire ONE notification.
 *
 * The per-entry counterpart to DismissAll, and the reason it cannot simply call
 * sofi_notify_service_dismiss(): that mutates the calling process's own copy of
 * the ring, which the daemon overwrites on its next change. From the history
 * menu the effect was invisible and then reverted.
 */
SofiNotifyDaemonResult sofi_notify_service_daemon_dismiss(guint32 id);

/**
 * Ask the running daemon to invoke one of a notification's actions.
 *
 * Must go to the daemon rather than being done locally, because invoking an
 * action means emitting ActionInvoked and only the process that owns the bus
 * connection can emit anything. Called in a standalone menu, the local path
 * dropped the signal on the floor and the sender never heard.
 *
 * @param index index into the action PAIRS, not into the flat array.
 */
SofiNotifyDaemonResult sofi_notify_service_daemon_invoke_action(guint32 id,
                                                                guint index);

/**
 * Ask the running daemon which entries are still live, and overlay the answer
 * onto this process's copy of the ring.
 *
 * The two facts this carries -- liveness and action count -- are deliberately
 * absent from the persisted history file, because both describe a notification
 * being on screen *now* and are owned by the process that received it. See
 * sofi_notify_store_apply_live().
 *
 * @returns HANDLED after the overlay is applied. On ABSENT and FAILED the ring
 *          is left untouched, which is right in both cases for different
 *          reasons: with no daemon nothing can be live, and with an unreachable
 *          one a live set may exist that we simply cannot see.
 */
SofiNotifyDaemonResult sofi_notify_service_refresh_live(void);

/**@}*/
#endif // SOFI_NOTIFY_SERVICE_H
