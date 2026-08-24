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
 * @brief The live notification banner.
 *
 * Entries are the live subset of the ring buffer, newest first. The mode holds
 * no state of its own -- the store is the single source of truth, and the
 * store's changed callback drives sofi_view_reload().
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Notifications"

#include "config.h"

#ifdef NOTIFY_DAEMON

#include <glib.h>

#include "helper.h"
#include "modes/notifications.h"
#include "notify-service.h"
#include "notify-store.h"
#include "sofi-icon-fetcher.h"
#include "sofi.h"
#include "widgets/textbox.h"

#include "mode-private.h"

static int notifications_mode_init(Mode *sw) {
  /* Action purpose: the store is owned by the service, which starts before the
   * view. Nothing to allocate here; the private data pointer stays NULL and is
   * never dereferenced. */
  (void)sw;
  return TRUE;
}

static unsigned int
notifications_mode_get_num_entries(G_GNUC_UNUSED const Mode *sw) {
  return sofi_notify_store_live_count();
}

/**
 * Function purpose: render one notification as a single markup row.
 *
 * Summary is bolded and body is dimmed beneath it, which is the shape every
 * notification daemon converges on because it is the one that survives being
 * truncated: the summary is what must remain readable when the row is short.
 */
static char *_get_display_value(G_GNUC_UNUSED const Mode *sw,
                                unsigned int selected_line, int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const SofiNotification *n = sofi_notify_store_live_nth(selected_line);

  if (n == NULL) {
    /* The row outlived its entry: the view refresh is coalesced, so this is
     * expected rather than exceptional. */
    return get_entry ? g_strdup("") : NULL;
  }

  if (n->urgency == SOFI_NOTIFY_URGENCY_CRITICAL) {
    *state |= URGENT;
  } else if (n->urgency == SOFI_NOTIFY_URGENCY_LOW) {
    *state |= ACTIVE;
  }
  *state |= MARKUP;

  if (!get_entry) {
    return NULL;
  }

  char *summary = g_markup_escape_text(n->summary, -1);
  char *result = NULL;

  /* Action purpose: summary on the first line, body on the second. This only
   * renders because the panel sets `eh: 2`, which is what sizes the listview's
   * rows -- the row height is measured once from a probe and every row gets it,
   * so without that the second line would be clipped rather than shown. */
  if (n->body != NULL && n->body[0] != '\0') {
    result = g_strdup_printf("<b>%s</b>\n<span alpha='65%%'>%s</span>",
                             summary, n->body);
  } else {
    result = g_strdup_printf("<b>%s</b>", summary);
  }
  g_free(summary);

  return result;
}

/**
 * Function purpose: supply the icon, preferring an inline image over a name.
 *
 * An image-data hint is chosen per notification and has already been validated
 * into a surface; app_icon is a per-application fallback resolved through the
 * shared icon fetcher.
 */
static cairo_surface_t *_get_icon(G_GNUC_UNUSED const Mode *sw,
                                  unsigned int selected_line,
                                  unsigned int height) {
  const SofiNotification *n = sofi_notify_store_live_nth(selected_line);

  if (n == NULL) {
    return NULL;
  }
  if (n->image != NULL) {
    return n->image;
  }
  if (n->app_icon == NULL || n->app_icon[0] == '\0') {
    return NULL;
  }

  /* Action purpose: the store hands out const pointers, but the icon cache is
   * a per-entry memo rather than part of its observable state. */
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

static ModeMode notifications_mode_result(G_GNUC_UNUSED Mode *sw, int mretv,
                                          G_GNUC_UNUSED char **input,
                                          unsigned int selected_line) {
  const SofiNotification *n = sofi_notify_store_live_nth(selected_line);

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
    if (n == NULL) {
      return RELOAD_DIALOG;
    }
    /* Action purpose: Enter runs the default action when the sender offered
     * one, and otherwise just dismisses. Both paths retire the notification,
     * which is what a user pressing Enter on a banner means. */
    if (sofi_notify_actions_count(n) > 0) {
      sofi_notify_service_invoke_action(n->id, 0);
    } else {
      sofi_notify_service_dismiss(n->id);
    }
    return RELOAD_DIALOG;
  }

  if ((mretv & MENU_ENTRY_DELETE) == MENU_ENTRY_DELETE) {
    if (n != NULL) {
      sofi_notify_service_dismiss(n->id);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    /* Action purpose: kb-custom-1 dismisses every live notification at once --
     * the "clear all" every notification centre has. kb-custom-2 upward invoke
     * the second, third, ... action of the highlighted entry; the first is on
     * Enter. */
    if (custom == 0) {
      sofi_notify_store_close_all(SOFI_NOTIFY_CLOSED_DISMISSED);
      return RELOAD_DIALOG;
    }
    if (n != NULL && custom < sofi_notify_actions_count(n) + 1) {
      sofi_notify_service_invoke_action(n->id, custom);
      return RELOAD_DIALOG;
    }
    return RELOAD_DIALOG;
  }

  /* Action purpose: everything else -- Escape, and the cancellation the daemon
   * itself issues when the last notification goes -- closes the banner without
   * touching the notifications. A daemon must not read "look away" as
   * "dismiss", so the entries stay live and stay in history.
   *
   * MODE_EXIT rather than RELOAD_DIALOG: exiting the view is what hands
   * control to sofi_view_maybe_update(), which finds no view left and drops
   * the surface instead of ending the process. Returning RELOAD_DIALOG here
   * would rebuild the banner the instant the daemon tried to put it away. */
  return MODE_EXIT;
}

static void notifications_mode_destroy(G_GNUC_UNUSED Mode *sw) {}

static int notifications_token_match(G_GNUC_UNUSED const Mode *sw,
                                     sofi_int_matcher **tokens,
                                     unsigned int index) {
  const SofiNotification *n = sofi_notify_store_live_nth(index);

  if (n == NULL) {
    return FALSE;
  }

  char *haystack = g_strdup_printf("%s %s %s", n->app_name, n->summary,
                                   n->body != NULL ? n->body : "");
  int match = helper_token_match(tokens, haystack);
  g_free(haystack);

  return match;
}

Mode notifications_mode = {.name = "notifications",
                           .cfg_name_key = "display-notifications",
                           ._init = notifications_mode_init,
                           ._get_num_entries =
                               notifications_mode_get_num_entries,
                           ._result = notifications_mode_result,
                           ._destroy = notifications_mode_destroy,
                           ._token_match = notifications_token_match,
                           ._get_display_value = _get_display_value,
                           ._get_icon = _get_icon,
                           ._get_completion = NULL,
                           ._preprocess_input = NULL,
                           ._get_message = NULL,
                           .private_data = NULL,
                           .free = NULL,
                           .type = MODE_TYPE_SWITCHER};

#endif // NOTIFY_DAEMON
