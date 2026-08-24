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

/**
 * @file
 * @brief Ring buffer backing both the notification banner and its history.
 *
 * Entries are held newest-first in a GPtrArray that is trimmed from the tail.
 * A GPtrArray rather than a true circular array because the ordering the modes
 * want is "newest first" in both directions and the capacity is small enough
 * that the memmove on insert is irrelevant next to the cost of rendering.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "NotifyStore"

#include "config.h"

#ifdef NOTIFY_DAEMON

#include <string.h>

#include "notify-store.h"

typedef struct {
  GPtrArray *ring;
  guint32 next_id;
  SofiNotifyChangedFunc changed;
  SofiNotifyClosedFunc closed;
  gpointer user_data;
} NotifyStore;

static NotifyStore store = {
    .ring = NULL,
    .next_id = 1,
    .changed = NULL,
    .closed = NULL,
    .user_data = NULL,
};

static void notification_free(gpointer data) {
  SofiNotification *n = (SofiNotification *)data;

  if (n == NULL) {
    return;
  }
  if (n->timer > 0) {
    g_source_remove(n->timer);
    n->timer = 0;
  }
  g_free(n->app_name);
  g_free(n->app_icon);
  g_free(n->summary);
  g_free(n->body);
  g_strfreev(n->actions);
  if (n->image != NULL) {
    cairo_surface_destroy(n->image);
  }
  g_free(n);
}

static void notify_changed(void) {
  /* Action purpose: persist before notifying. The history menu is a separate
   * process that reads the file, so it must be current by the time anything
   * observable happens. The ring is small and this is not a hot path -- it
   * runs once per notification, not per frame. */
  sofi_notify_store_save();

  if (store.changed != NULL) {
    store.changed(store.user_data);
  }
}

/**
 * Function purpose: retire an entry without freeing it, so it survives in the
 * ring for the history mode.
 *
 * Cancelling the timer first matters: an entry closed by CloseNotification or
 * by the user still has an expiry source armed, and letting that fire later
 * would emit a second NotificationClosed for an id the sender already retired.
 */
static void retire(SofiNotification *n, SofiNotifyCloseReason reason,
                   gboolean emit) {
  if (!n->live) {
    return;
  }
  if (n->timer > 0) {
    g_source_remove(n->timer);
    n->timer = 0;
  }
  n->live = FALSE;

  if (emit && store.closed != NULL) {
    store.closed(n->id, reason, store.user_data);
  }
}

static gboolean expire_cb(gpointer data) {
  SofiNotification *n = (SofiNotification *)data;

  /* Action purpose: the source is about to be destroyed by returning REMOVE,
   * so clear the handle before retire() can try to remove it a second time. */
  n->timer = 0;
  retire(n, SOFI_NOTIFY_CLOSED_EXPIRED, TRUE);
  notify_changed();

  return G_SOURCE_REMOVE;
}

/**
 * Function purpose: arm the expiry timer for one entry, honouring the spec's
 * three cases and the critical-urgency override.
 *
 * Critical notifications never expire (DECISIONS_LOG R23). That is a server
 * decision the spec leaves open, and it is the point of the urgency hint: a
 * critical alert that scrolls away on a timer is the same as no alert.
 */
static void arm_timer(SofiNotification *n, gint32 expire_timeout) {
  if (n->timer > 0) {
    g_source_remove(n->timer);
    n->timer = 0;
  }

  if (n->urgency == SOFI_NOTIFY_URGENCY_CRITICAL) {
    return;
  }
  /* 0 means "never expire" per the specification. */
  if (expire_timeout == 0) {
    return;
  }

  guint delay = (expire_timeout < 0) ? SOFI_NOTIFY_DEFAULT_EXPIRE_MS
                                     : (guint)expire_timeout;

  n->timer = g_timeout_add(delay, expire_cb, n);
}

/**
 * Function purpose: find a live entry by id.
 */
static SofiNotification *find_live(guint32 id) {
  if (id == 0 || store.ring == NULL) {
    return NULL;
  }
  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    if (n->id == id && n->live) {
      return n;
    }
  }
  return NULL;
}

/**
 * Function purpose: hold the live set and the ring inside their caps.
 *
 * Live entries over the cap are retired oldest-first with reason EXPIRED --
 * the sender is told the notification went away on its own, which is true from
 * its point of view and is the only reason code that fits. Entries evicted from
 * the tail are retired the same way first: usually they are already retired,
 * but a notification that never expires can survive at the tail until the ring
 * wraps past it, and freeing that outright would drop its NotificationClosed on
 * the floor and leave the sender believing it is still on screen.
 */
static void enforce_caps(void) {
  guint live = 0;

  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    if (!n->live) {
      continue;
    }
    live++;
    if (live > SOFI_NOTIFY_LIVE_MAX) {
      retire(n, SOFI_NOTIFY_CLOSED_EXPIRED, TRUE);
    }
  }

  while (store.ring->len > SOFI_NOTIFY_RING_CAPACITY) {
    SofiNotification *tail =
        g_ptr_array_index(store.ring, store.ring->len - 1);

    /* retire() is a no-op on an already-retired entry, which is the common
     * case here, so this costs nothing in the normal path. */
    retire(tail, SOFI_NOTIFY_CLOSED_EXPIRED, TRUE);
    g_ptr_array_remove_index(store.ring, store.ring->len - 1);
  }
}

void sofi_notify_store_init(SofiNotifyChangedFunc changed,
                            SofiNotifyClosedFunc closed, gpointer user_data) {
  if (store.ring != NULL) {
    return;
  }
  store.ring = g_ptr_array_new_with_free_func(notification_free);
  store.next_id = 1;
  store.changed = changed;
  store.closed = closed;
  store.user_data = user_data;
}

void sofi_notify_store_fini(void) {
  if (store.ring == NULL) {
    return;
  }
  g_ptr_array_free(store.ring, TRUE);
  store.ring = NULL;
  store.changed = NULL;
  store.closed = NULL;
  store.user_data = NULL;
}

guint32 sofi_notify_store_add(const gchar *app_name, guint32 replaces_id,
                              const gchar *app_icon, const gchar *summary,
                              const gchar *body, gchar **actions,
                              SofiNotifyUrgency urgency, gint32 expire_timeout,
                              cairo_surface_t *image) {
  g_return_val_if_fail(store.ring != NULL, 0);

  SofiNotification *n = find_live(replaces_id);

  if (n != NULL) {
    /* Action purpose: update in place. A well-behaved application that
     * repeats itself -- a volume indicator, a download counter -- passes its
     * previous id here, and stacking those would produce a wall of near
     * identical rows. No NotificationClosed is emitted: from the sender's
     * point of view the notification never went away. */
    g_free(n->app_name);
    g_free(n->app_icon);
    g_free(n->summary);
    g_free(n->body);
    g_strfreev(n->actions);
    if (n->image != NULL) {
      cairo_surface_destroy(n->image);
    }
  } else {
    n = g_malloc0(sizeof(SofiNotification));
    n->id = store.next_id++;
    /* Action purpose: id 0 is reserved by the spec as "no id", and the counter
     * is a guint32 that a long-lived session could in principle wrap. */
    if (store.next_id == 0) {
      store.next_id = 1;
    }
    g_ptr_array_insert(store.ring, 0, n);
  }

  n->app_name = g_strdup(app_name != NULL ? app_name : "");
  n->app_icon = g_strdup(app_icon != NULL ? app_icon : "");
  n->summary = g_strdup(summary != NULL ? summary : "");
  n->body = g_strdup(body != NULL ? body : "");
  n->actions = actions;
  n->urgency = urgency;
  n->image = image;
  n->received = g_get_real_time();
  n->live = TRUE;
  n->cached_icon_uid = 0;
  n->cached_icon_size = 0;

  arm_timer(n, expire_timeout);
  enforce_caps();
  notify_changed();

  return n->id;
}

gboolean sofi_notify_store_close(guint32 id, SofiNotifyCloseReason reason) {
  SofiNotification *n = find_live(id);

  if (n == NULL) {
    return FALSE;
  }
  retire(n, reason, TRUE);
  notify_changed();

  return TRUE;
}

void sofi_notify_store_close_all(SofiNotifyCloseReason reason) {
  g_return_if_fail(store.ring != NULL);

  gboolean any = FALSE;
  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    if (n->live) {
      retire(n, reason, TRUE);
      any = TRUE;
    }
  }
  if (any) {
    notify_changed();
  }
}

guint sofi_notify_store_live_count(void) {
  if (store.ring == NULL) {
    return 0;
  }
  guint count = 0;
  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    if (n->live) {
      count++;
    }
  }
  return count;
}

const SofiNotification *sofi_notify_store_live_nth(guint index) {
  if (store.ring == NULL) {
    return NULL;
  }
  guint seen = 0;
  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    if (!n->live) {
      continue;
    }
    if (seen == index) {
      return n;
    }
    seen++;
  }
  return NULL;
}

guint sofi_notify_store_count(void) {
  return store.ring == NULL ? 0 : store.ring->len;
}

const SofiNotification *sofi_notify_store_nth(guint index) {
  if (store.ring == NULL || index >= store.ring->len) {
    return NULL;
  }
  return g_ptr_array_index(store.ring, index);
}

/* ------------------------------------------------------------------ */
/* persistence                                                         */
/* ------------------------------------------------------------------ */

/**
 * Function purpose: locate the history file.
 *
 * Under the cache directory rather than config: it is derived data the user
 * never edits, and losing it costs a list of things they have already read.
 */
static gchar *history_path(void) {
  const gchar *cache = g_get_user_cache_dir();

  if (cache == NULL) {
    return NULL;
  }
  gchar *dir = g_build_filename(cache, "sofi", NULL);

  if (g_mkdir_with_parents(dir, 0700) < 0) {
    g_free(dir);
    return NULL;
  }
  gchar *path = g_build_filename(dir, "notifications.history", NULL);
  g_free(dir);

  return path;
}

void sofi_notify_store_save(void) {
  if (store.ring == NULL) {
    return;
  }

  gchar *path = history_path();
  if (path == NULL) {
    return;
  }

  GKeyFile *kf = g_key_file_new();

  /* Action purpose: a key file rather than a hand-rolled delimited format.
   * Summaries and bodies are arbitrary user-facing text containing newlines,
   * tabs and non-UTF8 sequences from whatever sent them; GKeyFile escapes all
   * of it and, more importantly, un-escapes it identically on read. */
  for (guint i = 0; i < store.ring->len; i++) {
    SofiNotification *n = g_ptr_array_index(store.ring, i);
    gchar group[32];

    g_snprintf(group, sizeof(group), "%u", i);
    g_key_file_set_uint64(kf, group, "id", n->id);
    g_key_file_set_string(kf, group, "app", n->app_name ? n->app_name : "");
    g_key_file_set_string(kf, group, "icon", n->app_icon ? n->app_icon : "");
    g_key_file_set_string(kf, group, "summary", n->summary ? n->summary : "");
    g_key_file_set_string(kf, group, "body", n->body ? n->body : "");
    g_key_file_set_integer(kf, group, "urgency", (int)n->urgency);
    g_key_file_set_int64(kf, group, "received", n->received);
  }

  GError *error = NULL;
  if (!g_key_file_save_to_file(kf, path, &error)) {
    g_debug("Could not write notification history to %s: %s", path,
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
  }

  g_key_file_free(kf);
  g_free(path);
}

void sofi_notify_store_load(void) {
  g_return_if_fail(store.ring != NULL);

  gchar *path = history_path();
  if (path == NULL) {
    return;
  }

  GKeyFile *kf = g_key_file_new();
  GError *error = NULL;

  if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error)) {
    /* A missing file is the normal first-run case, not a fault. */
    if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_debug("Could not read notification history from %s: %s", path,
              error != NULL ? error->message : "unknown error");
    }
    g_clear_error(&error);
    g_key_file_free(kf);
    g_free(path);
    return;
  }

  g_ptr_array_set_size(store.ring, 0);

  gsize count = 0;
  gchar **groups = g_key_file_get_groups(kf, &count);

  for (gsize i = 0; i < count && i < SOFI_NOTIFY_RING_CAPACITY; i++) {
    SofiNotification *n = g_malloc0(sizeof(SofiNotification));

    n->id = (guint32)g_key_file_get_uint64(kf, groups[i], "id", NULL);
    n->app_name = g_key_file_get_string(kf, groups[i], "app", NULL);
    n->app_icon = g_key_file_get_string(kf, groups[i], "icon", NULL);
    n->summary = g_key_file_get_string(kf, groups[i], "summary", NULL);
    n->body = g_key_file_get_string(kf, groups[i], "body", NULL);
    n->urgency =
        (SofiNotifyUrgency)g_key_file_get_integer(kf, groups[i], "urgency", NULL);
    n->received = g_key_file_get_int64(kf, groups[i], "received", NULL);

    /* Action purpose: never live, never timed. An entry read from disk is a
     * record of something already shown; flagging it live would pop a banner
     * for a notification the user dealt with in a previous session. */
    n->live = FALSE;
    n->timer = 0;
    n->image = NULL;

    /* g_key_file_get_string returns NULL on a missing key, and every accessor
     * downstream assumes these are strings. */
    if (n->app_name == NULL) {
      n->app_name = g_strdup("");
    }
    if (n->app_icon == NULL) {
      n->app_icon = g_strdup("");
    }
    if (n->summary == NULL) {
      n->summary = g_strdup("");
    }
    if (n->body == NULL) {
      n->body = g_strdup("");
    }

    g_ptr_array_add(store.ring, n);
    if (n->id >= store.next_id) {
      store.next_id = n->id + 1;
    }
  }

  g_strfreev(groups);
  g_key_file_free(kf);
  g_free(path);
}

#endif // NOTIFY_DAEMON
