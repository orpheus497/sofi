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
 * @brief The sofi control panel.
 *
 * Script function and purpose: presents every sofi indexer and every sofi verb
 * as a button in the bottom strip, with an icon, so one keybinding reaches the
 * whole suite instead of one binding per surface. This is what
 * `sofi -show window` opens.
 *
 * ---------------------------------------------------------------------------
 * EVERY FEATURE SOFI HAS IS ON THIS PANEL
 *
 * The list below is the whole product, minus exactly two things and for exactly
 * one reason: **saber owns the persistent taskbar and the persistent system
 * tray**, so the window switcher and the tray zone have no place here. Nothing
 * else is left off. If sofi grows an indexer, it gets a button.
 *
 * ---------------------------------------------------------------------------
 * WHY THE LIST IS COMPILE-TIME AND NOT LOOKED UP AT RUNTIME
 *
 * An earlier version filtered the buttons with `mode_lookup()`. That was wrong
 * and it showed: `mode_lookup()` searches the *enabled* `modes[]` list, not
 * everything the binary can do, so every button whose mode was not in the
 * user's `modes` setting was silently dropped and the panel came up nearly
 * empty. Which modes exist is decided at build time by the meson switches, so
 * that is where the question is answered -- each button sits behind the same
 * macro that gates its mode, and there is no runtime filtering at all.
 *
 * ---------------------------------------------------------------------------
 * WHY EVERY BUTTON SPAWNS
 *
 * `sofi <verb>` is the same contract saber and hikari.conf use, so a button
 * here and a keybinding there reach a surface by exactly one path -- and each
 * indexer gets its own compiled-in layout, which this strip is the wrong shape
 * for: the application menu rises from the bottom centre, volume and network
 * are top-right panes, the sheet row sits under the compositor's bar.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Launcher"

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "helper.h"
#include "modes/launcher.h"
#include "settings.h"
#include "sofi-icon-fetcher.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** One button. */
typedef struct {
  /** What the button says. */
  const char *label;
  /** freedesktop icon name. A theme without it draws no icon rather than a
   * broken-image glyph, which is the icon fetcher's own behaviour. */
  const char *icon;
  /** First argument after the binary name. */
  const char *arg1;
  /** Second argument, or NULL for a verb that takes one. */
  const char *arg2;
} LauncherButton;

/**
 * Action purpose: order is frequency of use, not alphabetical -- the things
 * launched many times a day sit left, where the strip is read from; the
 * system-management panes sit in the middle; the notification verbs, which are
 * occasional and one of which discards data, sit at the far right.
 *
 * Icon names are freedesktop standard names rather than shipped artwork, so the
 * strip picks up whatever icon theme the desktop already uses instead of
 * looking foreign in it.
 *
 * Each entry sits behind the macro that gates its mode, so this table is
 * exactly right for whatever combination of meson switches produced the binary.
 */
static const LauncherButton launcher_buttons[] = {
    /* First, because it is the one button that explains all the others: a user
     * who does not know what a surface does reaches for the key list before
     * reaching for the surface. */
    {.label = "Keys",
     .icon = "preferences-desktop-keyboard",
     .arg1 = "-show",
     .arg2 = "keys"},
#ifdef ENABLE_DRUN
    {.label = "Applications",
     .icon = "applications-other",
     .arg1 = "-show",
     .arg2 = "drun"},
#endif
    {.label = "Run",
     .icon = "system-run",
     .arg1 = "-show",
     .arg2 = "run"},
    {.label = "Files",
     .icon = "system-file-manager",
     .arg1 = "-show",
     .arg2 = "filebrowser"},
    {.label = "Find Files",
     .icon = "system-search",
     .arg1 = "-show",
     .arg2 = "recursivebrowser"},
    {.label = "SSH",
     .icon = "network-server",
     .arg1 = "-show",
     .arg2 = "ssh"},
#ifdef DISPLAY_MODE
    {.label = "Display",
     .icon = "preferences-desktop-display",
     .arg1 = "-show",
     .arg2 = "display"},
#endif
#ifdef SHEETS_MODE
    {.label = "Sheets",
     .icon = "preferences-desktop-workspaces",
     .arg1 = "-show",
     .arg2 = "sheets"},
#endif
#ifdef VOLUME_MODE
    {.label = "Volume",
     .icon = "audio-volume-high",
     .arg1 = "-show",
     .arg2 = "volume"},
#endif
#ifdef BLUETOOTH_MODE
    {.label = "Bluetooth",
     .icon = "bluetooth",
     .arg1 = "-show",
     .arg2 = "bluetooth"},
#endif
#ifdef NETWORK_MODE
    {.label = "Network",
     .icon = "network-wireless",
     .arg1 = "-show",
     .arg2 = "network"},
#endif
#ifdef NOTIFY_DAEMON
    /* Action purpose: the history panel only. `-notification-clear` and
     * `-notification-clear-history` are deliberately NOT here -- they are verbs
     * of the notification menu and live on its own bindings and buttons, where
     * the list they act on is on screen. A control panel that discards a
     * notification history from a strip showing no notifications is a button
     * with nothing to aim at. */
    {.label = "Notifications",
     .icon = "preferences-system-notifications",
     .arg1 = "-show",
     .arg2 = "notification-history"},
#endif
};

typedef struct {
  /** Icon fetcher request ids, one per button. 0 means "not asked yet". */
  uint32_t icon_uids[G_N_ELEMENTS(launcher_buttons)];
} LauncherModePrivateData;

/** The button behind a row, or NULL when the row is out of range. */
static const LauncherButton *launcher_button_at(unsigned int line) {
  if (line >= G_N_ELEMENTS(launcher_buttons)) {
    return NULL;
  }
  return &launcher_buttons[line];
}

/**
 * Function purpose: run one of sofi's other surfaces or verbs as a fresh
 * process.
 *
 * The strip is hidden before the child starts so the two never fight over the
 * keyboard, and this process exits immediately after -- there is nothing left
 * for it to do, and leaving it mapped would put two layer surfaces on screen.
 */
static void launcher_spawn(const LauncherButton *button) {
  char *self = g_find_program_in_path("sofi");
  const char *argv[4];
  unsigned int n = 0;
  GError *error = NULL;

  argv[n++] = self != NULL ? self : "sofi";
  argv[n++] = button->arg1;
  if (button->arg2 != NULL) {
    argv[n++] = button->arg2;
  }
  argv[n] = NULL;

  if (!g_spawn_async(NULL, (gchar **)argv, NULL, G_SPAWN_SEARCH_PATH, NULL,
                     NULL, NULL, &error)) {
    g_warning("Could not run %s: %s", button->label, error->message);
    g_error_free(error);
  }

  g_free(self);
}

static int launcher_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  LauncherModePrivateData *pd = g_malloc0(sizeof(*pd));
  mode_set_private_data(sw, (void *)pd);

  return TRUE;
}

static unsigned int
launcher_mode_get_num_entries(G_GNUC_UNUSED const Mode *sw) {
  return G_N_ELEMENTS(launcher_buttons);
}

static char *_get_display_value(G_GNUC_UNUSED const Mode *sw,
                                unsigned int selected_line,
                                G_GNUC_UNUSED int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const LauncherButton *button = launcher_button_at(selected_line);

  if (button == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }
  if (!get_entry) {
    return NULL;
  }

  return g_strdup(button->label);
}

/**
 * Function purpose: hand the icon fetcher a name and hand back what it has.
 *
 * Action purpose: the request id is cached per row, because the fetcher is
 * asynchronous -- the first call after a query returns NULL and the row draws
 * without an icon until the fetch lands and the view repaints. Re-querying on
 * every draw would start a new fetch each frame and never settle. This is the
 * same shape drun uses.
 */
static cairo_surface_t *launcher_get_icon(const Mode *sw,
                                          unsigned int selected_line,
                                          unsigned int height) {
  LauncherModePrivateData *pd =
      (LauncherModePrivateData *)mode_get_private_data(sw);
  const LauncherButton *button = launcher_button_at(selected_line);

  if (pd == NULL || button == NULL || button->icon == NULL) {
    return NULL;
  }

  if (pd->icon_uids[selected_line] == 0) {
    pd->icon_uids[selected_line] =
        sofi_icon_fetcher_query(button->icon, (int)height);
  }

  return sofi_icon_fetcher_get(pd->icon_uids[selected_line]);
}

static ModeMode launcher_mode_result(G_GNUC_UNUSED Mode *sw, int mretv,
                                     G_GNUC_UNUSED char **input,
                                     unsigned int selected_line) {
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
    const LauncherButton *button = launcher_button_at(selected_line);

    if (button == NULL) {
      return RELOAD_DIALOG;
    }

    sofi_view_hide();
    launcher_spawn(button);
    return MODE_EXIT;
  }

  return MODE_EXIT;
}

static void launcher_mode_destroy(Mode *sw) {
  LauncherModePrivateData *pd =
      (LauncherModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int launcher_token_match(G_GNUC_UNUSED const Mode *sw,
                                sofi_int_matcher **tokens,
                                unsigned int index) {
  const LauncherButton *button = launcher_button_at(index);

  if (button == NULL) {
    return FALSE;
  }

  return helper_token_match(tokens, button->label);
}

Mode launcher_mode = {.name = "window",
                      .cfg_name_key = "display-window",
                      ._init = launcher_mode_init,
                      ._get_num_entries = launcher_mode_get_num_entries,
                      ._result = launcher_mode_result,
                      ._destroy = launcher_mode_destroy,
                      ._token_match = launcher_token_match,
                      ._get_display_value = _get_display_value,
                      ._get_icon = launcher_get_icon,
                      ._get_completion = NULL,
                      ._preprocess_input = NULL,
                      ._get_message = NULL,
                      .private_data = NULL,
                      .free = NULL,
                      .type = MODE_TYPE_SWITCHER};
