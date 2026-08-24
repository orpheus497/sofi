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
 * Requests the name with REPLACE and DO_NOT_QUEUE, so sofi displaces a running
 * daemon that permits it and fails immediately rather than sitting in a queue
 * behind one that does not.
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

/**@}*/
#endif // SOFI_NOTIFY_SERVICE_H
