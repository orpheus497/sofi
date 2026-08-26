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
#if defined(WINDOW_MODE) && defined(ENABLE_WAYLAND)
#include "modes/wayland-window.h"
#endif
#include "notify-service.h"
#include "notify-store.h"
#include "sofi-icon-fetcher.h"
#include "sofi.h"
#include "theme.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/**
 * Function purpose: bring this process's copy of the ring back in step with the
 * daemon's.
 *
 * Two halves, and both are needed. The FILE carries the record -- what arrived,
 * from whom, when -- so reloading it picks up entries this process never saw.
 * The DAEMON carries what is still on screen and what can be done with it, which
 * is deliberately not in the file, because both are facts about a notification
 * being live right now and belong to the process that received it. A file
 * asserting either would be wrong the moment the daemon acted.
 *
 * Inside the daemon neither half applies: the ring in memory is the authority,
 * and overlaying it with a snapshot of itself would at best do nothing.
 */
static void history_refresh(void) {
  if (sofi_view_is_daemon()) {
    return;
  }
  sofi_notify_store_load();
  sofi_notify_service_refresh_live();
}

static int history_mode_init(G_GNUC_UNUSED Mode *sw) {
  /* Action purpose: this mode runs in two different processes. Inside the
   * daemon the store is already up and holds the live ring, and reloading from
   * disk would discard the live flags. Standing alone -- `sofi -show
   * notification-history`, its own surface and its own keyboard -- there is no
   * store at all, so bring one up and read the file the daemon writes. */
  if (sofi_view_is_daemon()) {
    return TRUE;
  }
  if (sofi_notify_store_count() == 0) {
    sofi_notify_store_init(NULL, NULL, NULL);
    sofi_notify_store_load();
  }

  /* Action purpose: run on EVERY init, not only the first. The reload above is
   * skipped once the ring is populated, but liveness has to be asked for again
   * regardless -- it is the one thing that can have changed while this panel was
   * not looking, and without this every entry reads as retired, which is what
   * made Enter, delete and the live stripe all dead in a standalone panel. */
  if (sofi_notify_service_refresh_live() != SOFI_NOTIFY_DAEMON_HANDLED) {
    /* Action purpose: no daemon answered, so nothing is on screen and Dismiss
     * has nothing to act on. It would do nothing -- correctly -- and say
     * nothing, which is the quality that made the original defect so hard to
     * place (R44). Disable it.
     *
     * Through the theme rather than a widget call because there is no hook at
     * the right moment: _init and _get_num_entries both run before the widgets
     * exist, and _get_display_value is never called when the list is empty,
     * which is exactly when this matters most. widget_init() reads `enabled`
     * for every widget already, and a later parse wins at property lookup (F2),
     * so stating it here reaches the button that is built afterwards.
     *
     * Clear is deliberately left ALONE: clearing history genuinely works with
     * no daemon, because history_mutate()'s ABSENT branch mutates the local ring
     * and the file, and nothing exists to overwrite it. */
    g_debug("No notification daemon; disabling the dismiss-all button.");
    sofi_theme_parse_string("button-dismiss-all { enabled: false; }");
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
    history_refresh();
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
 * Function purpose: retire ONE notification, wherever the ring lives.
 *
 * The bulk verbs got this treatment when they were written; the per-entry ones
 * never did, and called straight into the store instead. In a standalone panel
 * that mutated a copy the daemon overwrites within seconds -- the entry appeared
 * to come back, or more often never appeared to go at all.
 *
 * No local fallback, and that is not an omission. Reaching here at all requires
 * a live entry, and an entry is only live because a daemon said so moments ago;
 * with no daemon everything loaded from disk is retired by construction and the
 * caller's guard has already refused.
 */
static void history_dismiss_one(guint32 id) {
  if (sofi_view_is_daemon()) {
    sofi_notify_service_dismiss(id);
    return;
  }
  if (sofi_notify_service_daemon_dismiss(id) == SOFI_NOTIFY_DAEMON_HANDLED) {
    history_refresh();
  }
}

/**
 * Function purpose: invoke one of a notification's actions, wherever the ring
 * lives.
 *
 * This one CANNOT be done locally at all, which is the sharper version of the
 * same problem: invoking an action means emitting ActionInvoked, and only the
 * process holding the daemon's bus connection can emit a signal. The local path
 * checked for a connection, found none, and returned silently -- so pressing
 * Enter on a notification with actions did nothing and told nobody.
 */
static void history_invoke_one(guint32 id, guint index) {
  if (sofi_view_is_daemon()) {
    sofi_notify_service_invoke_action(id, index);
    return;
  }
  if (sofi_notify_service_daemon_invoke_action(id, index) ==
      SOFI_NOTIFY_DAEMON_HANDLED) {
    history_refresh();
  }
}

/**
 * Function purpose: raise the window the notification came from.
 *
 * A notification carries exactly one thing that could identify its sender's
 * window -- the spec's `desktop-entry` hint -- and it is optional, so most of
 * the time there is nothing to go on and this correctly does nothing.
 *
 * @returns TRUE only when a window was actually raised.
 */
static gboolean history_raise_sender(const SofiNotification *n) {
#if defined(WINDOW_MODE) && defined(ENABLE_WAYLAND)
  if (n == NULL || n->desktop_entry == NULL || n->desktop_entry[0] == '\0') {
    return FALSE;
  }
  /* Action purpose: do NOT call sofi_view_hide() here, however tempting.
   *
   * The window mode does exactly that before activating, but it activates
   * through a manager it bound at startup. This binds a fresh registry, and
   * doing so after the surface has been torn down produced a second entry into
   * this function that enumerated zero toplevels and then segfaulted. The panel
   * closes on MODE_EXIT a moment later regardless, so the hide bought nothing
   * and cost a crash. */
  return sofi_wayland_window_activate_app_id(n->desktop_entry);
#else
  (void)n;
  return FALSE;
#endif
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
    /* Action purpose: acting on an entry only makes sense while the sender
     * still considers the notification open. A retired entry is a record, and
     * acting on it would emit ActionInvoked for an id the sender has already
     * forgotten -- or retire a notification that went away by itself.
     *
     * The two live cases end differently on purpose. Running an action sends
     * the user to the application, so the panel gets out of the way. A plain
     * acknowledgement does not, and the whole point of going through a history
     * list is going through it -- exiting after each one would mean summoning
     * the panel once per notification. This mirrors the banner
     * (source/modes/notifications.c), where Enter also means "run the action if
     * there is one, otherwise just dismiss". */
    if (n == NULL) {
      return MODE_EXIT;
    }
    /* An action the sender offered beats anything sofi could infer: the
     * application said what Enter should mean. */
    if (n->live && sofi_notify_actions_count(n) > 0) {
      history_invoke_one(n->id, 0);
      return MODE_EXIT;
    }
    /* Action purpose: otherwise, take the user to the application. This is the
     * one thing a history list is for that a banner is not -- looking at
     * something that arrived an hour ago and wanting to go and deal with it.
     *
     * Deliberately NOT gated on `live`. A retired entry is exactly the case
     * that needs this: the notification is long gone from the screen and the
     * window behind it is still open. `desktop-entry` is persisted for that
     * reason.
     *
     * Falls through when nothing matches, so an entry with no window is not
     * left doing nothing at all. */
    if (history_raise_sender(n)) {
      return MODE_EXIT;
    }
    if (n->live) {
      history_dismiss_one(n->id);
      return RELOAD_DIALOG;
    }
    return MODE_EXIT;
  }

  if ((mretv & MENU_ENTRY_DELETE) == MENU_ENTRY_DELETE) {
    if (n != NULL && n->live) {
      history_dismiss_one(n->id);
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
