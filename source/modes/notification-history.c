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
 * @brief Everything the ring buffer still holds, live or not.
 *
 * A different view over one store, not a second store.
 *
 * It runs in two processes. Inside the daemon it reads the live ring directly.
 * Standing alone as `sofi -show notification-history` it reads the file the
 * daemon persists on every change -- which is also what makes history survive
 * a daemon restart, and what lets the history menu take the keyboard while the
 * banner deliberately does not.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.NotificationHistory"

#include "config.h"

#ifdef NOTIFY_DAEMON

#include <glib.h>

#include "helper.h"
#include "modes/notification-history.h"
#include "notify-service.h"
#include "notify-store.h"
#include "sofi-icon-fetcher.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

static int history_mode_init(G_GNUC_UNUSED Mode *sw) {
  /* Action purpose: this mode runs in two different processes. Inside the
   * daemon the store is already up and holds the live ring, and reloading from
   * disk would discard the live flags. Standing alone -- `sofi -show
   * notification-history`, its own surface and its own keyboard -- there is no
   * store at all, so bring one up and read the file the daemon writes. */
  if (sofi_notify_store_count() == 0 && !sofi_view_is_daemon()) {
    sofi_notify_store_init(NULL, NULL, NULL);
    sofi_notify_store_load();
  }
  return TRUE;
}

static unsigned int history_mode_get_num_entries(G_GNUC_UNUSED const Mode *sw) {
  return sofi_notify_store_count();
}

/**
 * Function purpose: run one ring mutation wherever the authoritative ring is.
 *
 * Four cases. Inside the daemon the ring is right here, and reloading would be
 * wrong for the opposite reason -- it owns the live flags, and entries read back
 * from disk are never live. Once the daemon has done the work on our behalf our
 * own copy is stale and the view is about to redraw from it; the daemon persists
 * inside the call and the call is synchronous, so the file is already current by
 * the time it returns and a reload is enough.
 *
 * The two failure cases are deliberately not the same. A confirmed absent daemon
 * means nobody else owns the ring, so mutating our own copy -- and the file it
 * came from -- is the correct thing to do and is what keeps the list on screen
 * consistent with what the user just asked for. A call that merely failed proves
 * nothing: a daemon may be running with a ring we cannot see, and clearing
 * locally would write a history file it is about to overwrite, destroying
 * notifications the user never asked to lose. So that case changes nothing.
 */
static void history_mutate(const gchar *method, void (*locally)(void)) {
  if (sofi_view_is_daemon()) {
    locally();
    return;
  }
  switch (sofi_notify_service_call_daemon(method)) {
  case SOFI_NOTIFY_DAEMON_HANDLED:
    sofi_notify_store_load();
    break;
  case SOFI_NOTIFY_DAEMON_ABSENT:
    locally();
    break;
  case SOFI_NOTIFY_DAEMON_FAILED:
  default:
    /* The call itself already reported why. Leave the store, and the file it
     * was loaded from, exactly as they are. */
    break;
  }
}

static void dismiss_all_locally(void) {
  sofi_notify_store_close_all(SOFI_NOTIFY_CLOSED_DISMISSED);
}

/**
 * Function purpose: describe how long ago a notification arrived.
 *
 * Relative rather than absolute because the question a history list answers is
 * "how recent is this", and a wall-clock time forces the reader to do the
 * subtraction themselves.
 */
static char *format_age(gint64 received) {
  gint64 seconds = (g_get_real_time() - received) / G_USEC_PER_SEC;

  if (seconds < 60) {
    return g_strdup("now");
  }
  if (seconds < 3600) {
    return g_strdup_printf("%" G_GINT64_FORMAT "m", seconds / 60);
  }
  if (seconds < 86400) {
    return g_strdup_printf("%" G_GINT64_FORMAT "h", seconds / 3600);
  }
  return g_strdup_printf("%" G_GINT64_FORMAT "d", seconds / 86400);
}

static char *_get_display_value(G_GNUC_UNUSED const Mode *sw,
                                unsigned int selected_line, int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const SofiNotification *n = sofi_notify_store_nth(selected_line);

  if (n == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  /* Action purpose: a still-live notification is the one thing a reader wants
   * separated from the rest of the list -- it is on screen right now. */
  if (n->live) {
    *state |= ACTIVE;
  } else if (n->urgency == SOFI_NOTIFY_URGENCY_CRITICAL) {
    *state |= URGENT;
  }
  *state |= MARKUP;

  if (!get_entry) {
    return NULL;
  }

  char *summary = g_markup_escape_text(n->summary, -1);
  char *app = g_markup_escape_text(n->app_name, -1);
  char *age = format_age(n->received);

  /* Two lines, matching the banner. The panel sets `eh: 2` to size the rows. */
  char *result = g_strdup_printf(
      "<b>%s</b>  <small>%s · %s</small>\n<span alpha='55%%'>%s</span>",
      summary, app, age, (n->body != NULL) ? n->body : "");
  g_free(summary);
  g_free(app);
  g_free(age);

  return result;
}

static cairo_surface_t *_get_icon(G_GNUC_UNUSED const Mode *sw,
                                  unsigned int selected_line,
                                  unsigned int height) {
  const SofiNotification *n = sofi_notify_store_nth(selected_line);

  if (n == NULL) {
    return NULL;
  }
  if (n->image != NULL) {
    return n->image;
  }
  if (n->app_icon == NULL || n->app_icon[0] == '\0') {
    return NULL;
  }

  SofiNotification *mutable_n = (SofiNotification *)n;

  if (mutable_n->cached_icon_uid > 0 &&
      mutable_n->cached_icon_size == (int)height) {
    return sofi_icon_fetcher_get(mutable_n->cached_icon_uid);
  }
  mutable_n->cached_icon_size = (int)height;
  mutable_n->cached_icon_uid =
      sofi_icon_fetcher_query(mutable_n->app_icon, (int)height);

  return sofi_icon_fetcher_get(mutable_n->cached_icon_uid);
}

static ModeMode history_mode_result(G_GNUC_UNUSED Mode *sw, int mretv,
                                    G_GNUC_UNUSED char **input,
                                    unsigned int selected_line) {
  const SofiNotification *n = sofi_notify_store_nth(selected_line);

  if (mretv & MENU_NEXT) {
    return NEXT_DIALOG;
  }
  if (mretv & MENU_PREVIOUS) {
    return PREVIOUS_DIALOG;
  }
  if (mretv & MENU_QUICK_SWITCH) {
    return (ModeMode)(mretv & MENU_LOWER_MASK);
  }

  if (mretv & MENU_OK) {
    /* Action purpose: invoking an action only makes sense while the sender
     * still considers the notification open. A retired entry is a record, and
     * acting on it would emit ActionInvoked for an id the sender has already
     * forgotten. */
    if (n != NULL && n->live && sofi_notify_actions_count(n) > 0) {
      sofi_notify_service_invoke_action(n->id, 0);
    }
    return MODE_EXIT;
  }

  if ((mretv & MENU_ENTRY_DELETE) == MENU_ENTRY_DELETE) {
    if (n != NULL && n->live) {
      sofi_notify_service_dismiss(n->id);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    /* Action purpose: the two cleanup verbs, deliberately separate. kb-custom-1
     * dismisses what is still on screen and keeps the record; kb-custom-2
     * destroys the record. Collapsing them into one action would mean a user
     * clearing a banner off their screen also lost the list of what they had
     * missed. Both are reachable by pointer through the buttons in
     * doc/panel-notification-history.sasi.
     *
     * Every other custom binding keeps its old meaning of "exit with 10+N", so
     * existing scripts are unaffected. */
    if (custom == 0) {
      history_mutate(SOFI_NOTIFY_METHOD_DISMISS_ALL, dismiss_all_locally);
      return RELOAD_DIALOG;
    }
    if (custom == 1) {
      history_mutate(SOFI_NOTIFY_METHOD_CLEAR_HISTORY,
                     sofi_notify_store_clear_history);
      return RELOAD_DIALOG;
    }
    return (ModeMode)custom;
  }

  return MODE_EXIT;
}

static void history_mode_destroy(G_GNUC_UNUSED Mode *sw) {}

static int history_token_match(G_GNUC_UNUSED const Mode *sw,
                               sofi_int_matcher **tokens, unsigned int index) {
  const SofiNotification *n = sofi_notify_store_nth(index);

  if (n == NULL) {
    return FALSE;
  }

  char *haystack = g_strdup_printf("%s %s %s", n->app_name, n->summary,
                                   n->body != NULL ? n->body : "");
  int match = helper_token_match(tokens, haystack);
  g_free(haystack);

  return match;
}

Mode notification_history_mode = {
    .name = "notification-history",
    .cfg_name_key = "display-notification-history",
    ._init = history_mode_init,
    ._get_num_entries = history_mode_get_num_entries,
    ._result = history_mode_result,
    ._destroy = history_mode_destroy,
    ._token_match = history_token_match,
    ._get_display_value = _get_display_value,
    ._get_icon = _get_icon,
    ._get_completion = NULL,
    ._preprocess_input = NULL,
    ._get_message = NULL,
    .private_data = NULL,
    .free = NULL,
    .type = MODE_TYPE_SWITCHER};

#endif // NOTIFY_DAEMON
