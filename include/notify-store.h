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

#ifndef SOFI_NOTIFY_STORE_H
#define SOFI_NOTIFY_STORE_H

#include <cairo.h>
#include <glib.h>
#include <stdint.h>

/**
 * @defgroup NOTIFYSTORE Notification store
 *
 * One fixed ring buffer holding every notification this session has seen.
 *
 * It is a ring rather than a queue because history is a first-class feature:
 * the banner renders the entries still flagged live, and the history mode
 * renders the whole ring. Building that in from the start costs one flag;
 * retrofitting it onto a transient queue would mean rewriting every accessor.
 *
 * Nothing here touches D-Bus or the view. The service layer pushes into it,
 * the modes read out of it, and it reports changes through two callbacks so it
 * does not need to know about either.
 *
 * @{
 */

/** Live entries the banner will show at once. Older ones are retired early. */
#define SOFI_NOTIFY_LIVE_MAX 20

/** Total entries retained for history. */
#define SOFI_NOTIFY_RING_CAPACITY 200

/**
 * Reason codes from the org.freedesktop.Notifications specification, passed
 * back to the sender in the NotificationClosed signal.
 */
typedef enum {
  SOFI_NOTIFY_CLOSED_EXPIRED = 1,
  SOFI_NOTIFY_CLOSED_DISMISSED = 2,
  SOFI_NOTIFY_CLOSED_BY_CALL = 3,
  SOFI_NOTIFY_CLOSED_UNDEFINED = 4,
} SofiNotifyCloseReason;

/** Urgency hint values. Critical never expires; see sofi_notify_store_add. */
typedef enum {
  SOFI_NOTIFY_URGENCY_LOW = 0,
  SOFI_NOTIFY_URGENCY_NORMAL = 1,
  SOFI_NOTIFY_URGENCY_CRITICAL = 2,
} SofiNotifyUrgency;

/** One notification. Owned by the store; never freed by a caller. */
typedef struct {
  /** Spec id. Non-zero, monotonically increasing, reused only via replaces_id. */
  guint32 id;
  gchar *app_name;
  gchar *app_icon;
  gchar *summary;
  /** Already validated and, if it failed to parse, escaped. Safe for Pango. */
  gchar *body;
  /** Flat key/label pairs exactly as the spec delivers them. May be NULL. */
  gchar **actions;
  SofiNotifyUrgency urgency;
  /** Microseconds since the epoch, for the history mode's relative times. */
  gint64 received;
  /** FALSE once expired, dismissed or closed. Stays in the ring for history. */
  gboolean live;
  /** Inline image from the image-data hint, already validated. May be NULL. */
  cairo_surface_t *image;
  /** Icon-fetcher handle for app_icon / image-path. Zero when unresolved. */
  guint32 cached_icon_uid;
  int cached_icon_size;
  /** GSource id of the expiry timer, or 0 when this entry cannot expire. */
  guint timer;
} SofiNotification;

/**
 * Called whenever the live set changes, so the view can reload.
 */
typedef void (*SofiNotifyChangedFunc)(gpointer user_data);

/**
 * Called when an entry leaves the live set, so the service can emit
 * NotificationClosed. Never called for an entry that was replaced in place.
 */
typedef void (*SofiNotifyClosedFunc)(guint32 id, SofiNotifyCloseReason reason,
                                     gpointer user_data);

/**
 * Bring the store up. Both callbacks may be NULL.
 */
void sofi_notify_store_init(SofiNotifyChangedFunc changed,
                            SofiNotifyClosedFunc closed, gpointer user_data);

/** Tear the store down, cancelling every outstanding expiry timer. */
void sofi_notify_store_fini(void);

/**
 * Add a notification, or update one in place when replaces_id names a live
 * entry.
 *
 * Ownership, stated once so callers do not have to infer it: the string
 * arguments are COPIED, while @p actions and @p image are CONSUMED. The store
 * takes both on entry and frees them itself -- g_strfreev() for the action
 * vector, cairo_surface_destroy() for the surface -- on replacement, on
 * eviction from the ring, and at teardown. A caller must not free either after
 * this returns, and must not keep using them; pass NULL when there is nothing
 * to hand over. This holds on every path including the replaces_id path, where
 * the previous entry's action vector and surface are released before the new
 * ones are stored.
 *
 * @param expire_timeout Milliseconds; -1 requests the server default, 0 asks
 *                       to never expire. Critical urgency overrides both and
 *                       never expires.
 *
 * @returns the id assigned, which equals replaces_id when a replacement
 *          happened.
 */
guint32 sofi_notify_store_add(const gchar *app_name, guint32 replaces_id,
                              const gchar *app_icon, const gchar *summary,
                              const gchar *body, gchar **actions,
                              SofiNotifyUrgency urgency, gint32 expire_timeout,
                              cairo_surface_t *image);

/**
 * Retire a live entry, firing the closed callback.
 *
 * @returns FALSE when no live entry carries that id, which the spec allows a
 *          caller to treat as an error.
 */
gboolean sofi_notify_store_close(guint32 id, SofiNotifyCloseReason reason);

/** Retire every live entry. Used when the user dismisses the whole stack. */
void sofi_notify_store_close_all(SofiNotifyCloseReason reason);

/**
 * Empty the ring completely -- live entries and history alike -- and persist
 * the empty result.
 *
 * Distinct from sofi_notify_store_close_all(), which retires the live set but
 * keeps every entry for the history mode. This is the destructive one: after it
 * returns there is nothing left to browse.
 *
 * Live entries are retired properly on the way out, so their senders still
 * receive NotificationClosed. The id counter is deliberately not reset; see the
 * implementation.
 */
void sofi_notify_store_clear_history(void);

/** Number of entries currently flagged live. */
guint sofi_notify_store_live_count(void);

/**
 * Live entry at @p index, newest first.
 *
 * @returns NULL when @p index is out of range, which callers must tolerate:
 *          a row can outlive its entry because the view refresh is coalesced.
 */
const SofiNotification *sofi_notify_store_live_nth(guint index);

/** Number of entries in the ring, live or not. */
guint sofi_notify_store_count(void);

/** Ring entry at @p index, newest first. NULL when out of range. */
const SofiNotification *sofi_notify_store_nth(guint index);

/** Default expiry in milliseconds, applied when a sender passes -1. */
#define SOFI_NOTIFY_DEFAULT_EXPIRE_MS 5000

/**
 * Write the ring to disk.
 *
 * History has to outlive the process that collected it: the daemon owns the
 * ring, but the history menu is a separate sofi invocation with its own
 * surface and its own keyboard, and one process cannot read another's memory.
 * Persisting also means history survives a daemon restart, which a memory-only
 * ring would not.
 *
 * Called automatically whenever the live set changes.
 */
void sofi_notify_store_save(void);

/**
 * Load the ring from disk, replacing whatever is held.
 *
 * Entries read back are never live -- a notification's on-screen life belongs
 * to the process that received it, and resurrecting one as live would show a
 * banner for something already dismissed. Expiry timers are not restored for
 * the same reason.
 */
void sofi_notify_store_load(void);

/**@}*/
#endif // SOFI_NOTIFY_STORE_H
