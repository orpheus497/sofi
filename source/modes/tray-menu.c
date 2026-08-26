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

/**
 * @file
 * @brief One tray item's menu, as a sofi mode.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "Modes.TrayMenu"

#include "config.h"

#ifdef SYSTEM_TRAY

#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "dbusmenu.h"
#include "helper.h"
#include "mode-private.h"
#include "mode.h"
#include "modes/tray-menu.h"
#include "widgets/textbox.h"

/**
 * Action purpose: how deep a submenu chain may go.
 *
 * The depth is driven by whatever the application published, and the stack is
 * fixed-size so a pathological or hostile menu cannot grow it without bound. No
 * real menu approaches this; nesting past a couple of levels is already
 * unusable with a keyboard.
 */
#define TRAY_MENU_MAX_DEPTH 16

/** The target, set before the mode is switched to. Module-wide: modes are
 * singletons, so there is nowhere else for it to live. */
static struct {
  char *bus_name;
  char *object_path;
  char *title;
} target = {NULL, NULL, NULL};

typedef struct {
  SofiDbusmenu *menu;
  SofiDbusmenuEntry *entries;
  unsigned int count;

  /**
   * The chain of parent ids leading to the level on screen. `depth == 0` is the
   * root, where no `..` row is shown because there is nowhere to go back to.
   */
  gint32 stack[TRAY_MENU_MAX_DEPTH];
  unsigned int depth;

  /** The single row is a diagnostic, not a menu entry. Keeps the `..` row and
   * the filter from treating it as something the application offered. */
  gboolean placeholder;
} TrayMenuModePrivateData;

void sofi_tray_menu_set_target(const char *bus_name, const char *object_path,
                               const char *title) {
  g_free(target.bus_name);
  g_free(target.object_path);
  g_free(target.title);
  target.bus_name = NULL;
  target.object_path = NULL;
  target.title = NULL;

  if (bus_name == NULL || bus_name[0] == '\0' || object_path == NULL ||
      object_path[0] == '\0') {
    return;
  }
  target.bus_name = g_strdup(bus_name);
  target.object_path = g_strdup(object_path);
  target.title = g_strdup(title != NULL ? title : "");
}

/**
 * Function purpose: is row 0 the `..` row rather than a menu entry?
 *
 * One predicate rather than repeating `pd->depth > 0` at every index
 * calculation, because getting it wrong shifts every row by one and fires the
 * entry above or below the one the user chose.
 */
static gboolean has_up_row(const TrayMenuModePrivateData *pd) {
  return pd->depth > 0;
}

/** Function purpose: map a listview row to an entry, NULL for the `..` row. */
static const SofiDbusmenuEntry *entry_at(const TrayMenuModePrivateData *pd,
                                         unsigned int line) {
  if (has_up_row(pd)) {
    if (line == 0) {
      return NULL;
    }
    line--;
  }
  if (line >= pd->count) {
    return NULL;
  }
  return &pd->entries[line];
}

/**
 * Function purpose: stand in for a menu that could not be shown, as a row.
 *
 * **As a list row rather than through `_get_message`, and that is the point.**
 * A mode's message needs a `message` widget in the layout to land in, and the
 * task strip's mainbox is `[ inputbar, listview, tray ]` -- it has none. A
 * message there renders nowhere at all, so the user would see an empty panel
 * and no reason for it. Every layout has a listview.
 *
 * Marked disabled so it cannot be actioned: `tray_menu_mode_result()` already
 * refuses a row the application greyed out, and this reuses that guard rather
 * than adding a second one.
 */
static void set_placeholder(TrayMenuModePrivateData *pd, const char *text) {
  pd->entries = g_malloc0(sizeof(SofiDbusmenuEntry));
  pd->entries[0].id = 0;
  pd->entries[0].label = g_strdup(text);
  pd->entries[0].enabled = FALSE;
  pd->entries[0].toggle_type = SOFI_DBUSMENU_NONE;
  pd->entries[0].toggle_state = -1;
  pd->count = 1;
  pd->placeholder = TRUE;
}

/** Function purpose: read one level into the private data, replacing what is
 * there. The level to read is whatever is on top of the stack. */
static void load_level(TrayMenuModePrivateData *pd) {
  sofi_dbusmenu_entries_free(pd->entries, pd->count);
  pd->entries = NULL;
  pd->count = 0;
  pd->placeholder = FALSE;

  if (pd->menu == NULL) {
    /* Said out loud rather than shown as an empty list: an item with no menu
     * and one whose menu could not be read look identical otherwise, and only
     * one of them is something the user can act on. */
    set_placeholder(pd, "This tray item published no menu.");
    return;
  }

  gint32 parent = pd->depth > 0 ? pd->stack[pd->depth - 1] : 0;
  pd->entries = sofi_dbusmenu_read(pd->menu, parent, &pd->count);

  if (pd->count == 0) {
    sofi_dbusmenu_entries_free(pd->entries, pd->count);
    pd->entries = NULL;
    set_placeholder(pd, "The application returned an empty menu.");
  }
}

static int tray_menu_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  TrayMenuModePrivateData *pd = g_malloc0(sizeof(TrayMenuModePrivateData));
  mode_set_private_data(sw, (void *)pd);

  if (target.bus_name == NULL) {
    /* No target: the mode was reached some other way -- asked for by name, or
     * cycled into with kb-mode-next. Leaving pd->menu NULL is what makes
     * load_level() explain itself rather than show an empty list. */
    g_debug("Tray menu opened with no target.");
  } else {
    pd->menu = sofi_dbusmenu_open(target.bus_name, target.object_path);
  }

  /* Unconditional, including the no-target case: load_level() is the one place
   * that decides what the list contains, so every route into the mode produces
   * either rows or a reason. */
  load_level(pd);
  return TRUE;
}

static unsigned int tray_menu_mode_get_num_entries(const Mode *sw) {
  const TrayMenuModePrivateData *pd =
      (const TrayMenuModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  return pd->count + (has_up_row(pd) ? 1 : 0);
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const TrayMenuModePrivateData *pd =
      (const TrayMenuModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  const SofiDbusmenuEntry *e = entry_at(pd, selected_line);

  if (e == NULL) {
    /* The `..` row. */
    *state |= MARKUP;
    return get_entry ? g_strdup("<span alpha='55%'>..</span>") : NULL;
  }

  *state |= MARKUP;
  if (!e->enabled) {
    /* Action purpose: the same treatment a retired notification gets, so
     * "present but not actionable" reads identically across sofi. */
    *state |= URGENT;
  }
  if (!get_entry) {
    return NULL;
  }

  if (e->separator) {
    /* A separator is a row, because the listview has no notion of anything
     * else. Drawn as a rule rather than left blank so it reads as a divider and
     * not as an empty entry the user should try to pick. */
    return g_strdup("<span alpha='30%'>────────</span>");
  }

  char *label = g_markup_escape_text(e->label, -1);
  char *result = NULL;

  const char *mark = "";
  if (e->toggle_type != SOFI_DBUSMENU_NONE) {
    /* Indeterminate (-1) deliberately renders as neither on nor off: the
     * application is saying it does not know, and guessing "off" would be a
     * claim it did not make. */
    mark = e->toggle_state == 1 ? "● " : (e->toggle_state == 0 ? "○ " : "· ");
  }

  if (e->submenu) {
    result = g_strdup_printf("%s%s<span alpha='55%%'>  ▸</span>", mark, label);
  } else if (!e->enabled) {
    result = g_strdup_printf("<span alpha='45%%'>%s%s</span>", mark, label);
  } else {
    result = g_strdup_printf("%s%s", mark, label);
  }

  g_free(label);
  return result;
}

static ModeMode tray_menu_mode_result(Mode *sw, int mretv,
                                      G_GNUC_UNUSED char **input,
                                      unsigned int selected_line) {
  TrayMenuModePrivateData *pd =
      (TrayMenuModePrivateData *)mode_get_private_data(sw);

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
    if (pd == NULL) {
      return MODE_EXIT;
    }
    const SofiDbusmenuEntry *e = entry_at(pd, selected_line);

    if (e == NULL) {
      /* `..`: back up one level. Guarded rather than assumed, because the row
       * only exists when depth > 0 and a stale selection could name it. */
      if (pd->depth > 0) {
        pd->depth--;
        load_level(pd);
        return RESET_DIALOG;
      }
      return MODE_EXIT;
    }

    /* A separator is not actionable, and neither is a row the application
     * greyed out. Reload rather than exit: the click was on a real row, and
     * closing the panel would look like the action ran. */
    if (e->separator || !e->enabled) {
      return RELOAD_DIALOG;
    }

    if (e->submenu) {
      if (pd->depth >= TRAY_MENU_MAX_DEPTH) {
        g_warning("Tray menu nested deeper than %d levels; not descending.",
                  TRAY_MENU_MAX_DEPTH);
        return RELOAD_DIALOG;
      }
      pd->stack[pd->depth] = e->id;
      pd->depth++;
      load_level(pd);
      return RESET_DIALOG;
    }

    /* A leaf: tell the application, and get out of the way. Exiting matches
     * every other sofi mode -- picking a window, running a command, invoking a
     * notification action all close the panel. */
    sofi_dbusmenu_event(pd->menu, e->id);
    return MODE_EXIT;
  }

  return MODE_EXIT;
}

static void tray_menu_mode_destroy(Mode *sw) {
  TrayMenuModePrivateData *pd =
      (TrayMenuModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return;
  }
  sofi_dbusmenu_entries_free(pd->entries, pd->count);
  sofi_dbusmenu_close(pd->menu);
  g_free(pd);
  mode_set_private_data(sw, NULL);
}

static int tray_menu_token_match(const Mode *sw, sofi_int_matcher **tokens,
                                 unsigned int index) {
  const TrayMenuModePrivateData *pd =
      (const TrayMenuModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return FALSE;
  }
  const SofiDbusmenuEntry *e = entry_at(pd, index);

  /* The `..` row and separators carry no text to match. Keeping `..` visible
   * while filtering would be a row that ignores the filter; hiding it is the
   * same choice the file browser makes. */
  if (e == NULL || e->separator) {
    return helper_token_match(tokens, "");
  }
  return helper_token_match(tokens, e->label);
}

Mode tray_menu_mode = {.name = "tray-menu",
                       .cfg_name_key = "display-tray-menu",
                       ._init = tray_menu_mode_init,
                       ._get_num_entries = tray_menu_mode_get_num_entries,
                       ._result = tray_menu_mode_result,
                       ._destroy = tray_menu_mode_destroy,
                       ._token_match = tray_menu_token_match,
                       ._get_display_value = _get_display_value,
                       ._get_icon = NULL,
                       ._get_completion = NULL,
                       ._preprocess_input = NULL,
                       ._get_message = NULL,
                       .private_data = NULL,
                       .free = NULL,
                       .type = MODE_TYPE_SWITCHER};

#endif // SYSTEM_TRAY
