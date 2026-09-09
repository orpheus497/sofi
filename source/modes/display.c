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
 * @brief Output resolution, scale and position. STUB.
 *
 * Script function and purpose: lists the outputs the session has, read-only,
 * and states why none of them can be changed from here yet.
 *
 * **The blocker is the compositor, not this file.** `hikari-sakura/src/server.c`
 * creates `wlr_xdg_output_manager_v1` (read-only geometry) and
 * `wlr_fractional_scale_manager_v1`. It does **not** create
 * `wlr_output_manager_v1`, which is the protocol that sets modes, scales and
 * positions. No client on that compositor can change an output by any protocol
 * it publishes, so a working display mode needs compositor work first.
 *
 * ---------------------------------------------------------------------------
 * AND THE LISTING DOES NOT WORK ON HIKARI-SAKURA EITHER
 *
 * `wlr-randr` is used where it is installed, and it needs no library linked
 * here -- but **it speaks `wlr-output-management-unstable-v1`, which is the
 * exact protocol hikari-sakura does not advertise.** So on the compositor this
 * mode is written for, wlr-randr fails and the list is empty. It works on other
 * wlroots compositors, which is why it is still worth calling.
 *
 * Do not read that as "install wlr-randr and it will work": the row says which
 * of the three cases happened precisely so that is not the conclusion drawn.
 *
 * **The route that would work here is sofi's own Wayland backend.** It already
 * binds `wl_output` and `zxdg_output_manager_v1` and knows every output's name,
 * position and logical size -- that is what `sofi -h` prints. Exposing that
 * through an enumerator both display backends implement, and calling it from
 * here, would list outputs on hikari-sakura with no external tool at all. It is
 * not done yet; `include/display.h` offers `monitor_active()` for the *current*
 * monitor and `display_dump_monitor_layout()`, which prints to stdout, and
 * neither is a list this mode can consume.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Display"

#include "config.h"

#ifdef DISPLAY_MODE

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "helper.h"
#include "modes/display.h"
#include "settings.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** Why the list is empty, so the row can say which of three things happened. */
typedef enum {
  /** wlr-randr is not on $PATH. */
  DISPLAY_PROBE_ABSENT,
  /** It ran and answered. The list is then whatever it reported. */
  DISPLAY_PROBE_OK,
  /** It is installed but could not run or exited non-zero -- which is what
   * happens on a compositor advertising no output-management protocol, and is
   * therefore the *expected* result on hikari-sakura. */
  DISPLAY_PROBE_FAILED,
} DisplayProbe;

typedef struct {
  /** Output names with their geometry, as rows. Empty when nothing enumerated. */
  GPtrArray *outputs;
  DisplayProbe probe;
} DisplayModePrivateData;

/**
 * Function purpose: enumerate outputs with wlr-randr.
 *
 * Action purpose: `wlr-randr` prints one unindented line per output, beginning
 * with its name, and indents everything belonging to it -- so the unindented
 * lines are the list. The indented detail is deliberately not parsed: its shape
 * varies between releases, and this mode cannot act on any of it anyway. The
 * name is what a user is looking for.
 */
static void display_enumerate(DisplayModePrivateData *pd) {
  const char *argv[] = {"wlr-randr", NULL};
  gchar *out = NULL;
  gint status = 0;
  GError *error = NULL;

  char *found = g_find_program_in_path("wlr-randr");
  if (found == NULL) {
    pd->probe = DISPLAY_PROBE_ABSENT;
    g_debug("wlr-randr is not installed; outputs cannot be listed.");
    return;
  }
  g_free(found);

  /* Action purpose: from here on the tool exists, so any failure is a failure to
   * *talk to the compositor* rather than a missing package -- and the two need
   * different messages. Telling a hikari-sakura user to install a tool they
   * already have would send them looking in the wrong place entirely. */
  pd->probe = DISPLAY_PROBE_FAILED;

  if (!g_spawn_sync(NULL, (gchar **)argv, NULL,
                    G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL,
                    NULL, &out, NULL, &status, &error)) {
    g_debug("Could not run wlr-randr: %s", error->message);
    g_error_free(error);
    return;
  }

  if (!g_spawn_check_wait_status(status, &error)) {
    g_debug("wlr-randr exited non-zero: %s", error->message);
    g_error_free(error);
    g_free(out);
    return;
  }

  pd->probe = DISPLAY_PROBE_OK;

  char **lines = g_strsplit(out != NULL ? out : "", "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (lines[i][0] == '\0' || g_ascii_isspace(lines[i][0])) {
      continue;
    }
    g_ptr_array_add(pd->outputs, g_strstrip(g_strdup(lines[i])));
  }

  g_strfreev(lines);
}

static int display_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  DisplayModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->outputs = g_ptr_array_new_with_free_func(g_free);
  mode_set_private_data(sw, (void *)pd);

  display_enumerate(pd);

  /* Action purpose: TRUE even with nothing enumerated. There is no verb here
   * that can fail, because there is no verb here -- the single explanatory row
   * says more than an error dialog would. */
  return TRUE;
}

static unsigned int display_mode_get_num_entries(const Mode *sw) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  /* One explanatory row when there is nothing to list. */
  return pd->outputs->len > 0 ? pd->outputs->len : 1;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  /* Every row here is read-only, and URGENT is the state a theme already dims.
   * A surface that cannot act on its own list should not look like one that
   * can. */
  *state |= URGENT;

  if (!get_entry) {
    return NULL;
  }

  if (pd->outputs->len == 0) {
    switch (pd->probe) {
    case DISPLAY_PROBE_ABSENT:
      return g_strdup("wlr-randr is not installed");
    case DISPLAY_PROBE_FAILED:
      /* Action purpose: says what was observed, not why. The mode never asks
       * the compositor which protocols it advertises, so naming that as the
       * cause states a diagnosis it did not make -- and it is a diagnosis with
       * a shelf life: hikari-sakura implements
       * wlr-output-management-unstable-v1 as of its own commit 60075bd, and
       * this text would then be wrong on the very compositor it was written
       * for. Missing output management is the likeliest cause and is offered
       * as that rather than asserted. */
      return g_strdup("wlr-randr ran but could not read the outputs — the "
                      "compositor may not advertise output management");
    case DISPLAY_PROBE_OK:
    default:
      return g_strdup("No outputs reported");
    }
  }
  if (selected_line >= pd->outputs->len) {
    return g_strdup("");
  }

  return g_markup_escape_text(g_ptr_array_index(pd->outputs, selected_line),
                              -1);
}

/* Action purpose: the read-only limit is sofi's own -- this mode implements no
 * verb that sets anything -- so it is stated as sofi's, without naming a
 * compositor or attributing the limit to one. The previous wording made a
 * claim about hikari-sakura that this mode never verifies and that its own
 * commit 60075bd has since made false. */
static char *display_mode_get_message(G_GNUC_UNUSED const Mode *sw) {
  return g_markup_printf_escaped(
      "Read-only: sofi lists outputs here and does not set modes, scales or "
      "positions.");
}

static ModeMode display_mode_result(G_GNUC_UNUSED Mode *sw, int mretv,
                                    G_GNUC_UNUSED char **input,
                                    G_GNUC_UNUSED unsigned int selected_line) {
  if (mretv & MENU_NEXT) {
    return NEXT_DIALOG;
  }
  if (mretv & MENU_PREVIOUS) {
    return PREVIOUS_DIALOG;
  }
  if (mretv & MENU_QUICK_SWITCH) {
    return (ModeMode)(mretv & MENU_LOWER_MASK);
  }

  return MODE_EXIT;
}

static void display_mode_destroy(Mode *sw) {
  DisplayModePrivateData *pd =
      (DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_ptr_array_free(pd->outputs, TRUE);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int display_token_match(const Mode *sw, sofi_int_matcher **tokens,
                               unsigned int index) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL || index >= pd->outputs->len) {
    return helper_token_match(tokens, "display");
  }

  return helper_token_match(tokens, g_ptr_array_index(pd->outputs, index));
}

Mode display_mode = {.name = "display",
                     .cfg_name_key = "display-display",
                     ._init = display_mode_init,
                     ._get_num_entries = display_mode_get_num_entries,
                     ._result = display_mode_result,
                     ._destroy = display_mode_destroy,
                     ._token_match = display_token_match,
                     ._get_display_value = _get_display_value,
                     ._get_icon = NULL,
                     ._get_completion = NULL,
                     ._preprocess_input = NULL,
                     ._get_message = display_mode_get_message,
                     .private_data = NULL,
                     .free = NULL,
                     .type = MODE_TYPE_SWITCHER};

#endif // DISPLAY_MODE
