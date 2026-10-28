/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2013-2023 Qball Cow <qball@gmpclient.org>
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
/** Log domain for this module */
#define G_LOG_DOMAIN "XrmOptions"

#include "xrmoptions.h"
#include "helper.h"
#include "sofi-types.h"
#include "sofi.h"
#include "settings.h"
#include <ctype.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ThemeWidget *sofi_configuration = NULL;

/** Different sources of configuration. */
const char *const ConfigSourceStr[] = {"Default", "File", "Theme File",
                                       "Commandline", "Don't Display"};
/** Enumerator of different sources of configuration. */
enum ConfigSource {
  CONFIG_DEFAULT = 0,
  CONFIG_FILE = 1,
  CONFIG_FILE_THEME = 2,
  CONFIG_CMDLINE = 3,
  CONFIG_NO_DISPLAY = 4
};

typedef struct {
  int32_t type;
  enum ConfigSource source;
  const char *name;
  union {
    unsigned int *num;
    int *snum;
    char **str;
    void *pointer;
    char *charc;
  } value;
  char *mem;
  const char *comment;
} XrmOption;
/**
 * Map X resource and commandline options to internal options
 * Currently supports string, boolean and number (signed and unsigned).
 */
static XrmOption xrmOptions[] = {
    {xrm_String, CONFIG_DEFAULT, "switchers", {.str = &config.modes}, NULL, ""},
    {xrm_String,
     CONFIG_DEFAULT,
     "modi",
     {.str = &config.modes},
     NULL,
     "Enabled modes"},
    {xrm_String,
     CONFIG_DEFAULT,
     "modes",
     {.str = &config.modes},
     NULL,
     "Enable modes"},
    {
        xrm_String,
        CONFIG_DEFAULT,
        "font",
        {.str = &config.menu_font},
        NULL,
        "Font to use",
    },
    {
        xrm_Number,
        CONFIG_DEFAULT,
        "location",
        {.num = &config.location},
        NULL,
        "Location on screen",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "wayland-layer",
        {.str = &config.wayland_layer},
        NULL,
        "On Wayland, specifies the layer where sofi is rendered. "
        "Available layers are background, bottom, top, overlay.",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "wayland-keyboard-interactivity",
        {.str = &config.wayland_keyboard_interactivity},
        NULL,
        "On Wayland, how the layer surface takes keyboard focus: "
        "none, exclusive or on-demand. on-demand requires a "
        "compositor offering zwlr_layer_shell_v1 version 4.",
    },
    {
        xrm_SNumber,
        CONFIG_DEFAULT,
        "yoffset",
        {.snum = &config.y_offset},
        NULL,
        "Y-offset relative to location. *DEPRECATED* see sofi-theme manpage "
        "for "
        "new option",
    },
    {
        xrm_SNumber,
        CONFIG_DEFAULT,
        "xoffset",
        {.snum = &config.x_offset},
        NULL,
        "X-offset relative to location. *DEPRECATED* see sofi-theme manpage "
        "for "
        "new option",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "fixed-num-lines",
        {.num = &config.fixed_num_lines},
        NULL,
        "Always show number of lines",
    },

    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "show-icons",
        {.snum = &config.show_icons},
        NULL,
        "Whether to load and show icons in the default theme",
    },

    {
        xrm_String,
        CONFIG_DEFAULT,
        "preview-cmd",
        {.str = &config.preview_cmd},
        NULL,
        "Custom command to generate preview icons",
    },

    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-selection-changed",
        {.str = &config.on_selection_changed},
        NULL,
        "Custom command to call when menu selection changes",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-mode-changed",
        {.str = &config.on_mode_changed},
        NULL,
        "Custom command to call when menu mode changes",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-entry-accepted",
        {.str = &config.on_entry_accepted},
        NULL,
        "Custom command to call when menu entry is accepted",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-menu-canceled",
        {.str = &config.on_menu_canceled},
        NULL,
        "Custom command to call when menu is canceled",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-menu-error",
        {.str = &config.on_menu_error},
        NULL,
        "Custom command to call when menu finds errors",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "on-screenshot-taken",
        {.str = &config.on_screenshot_taken},
        NULL,
        "Custom command to call when menu screenshot is taken",
    },

    {
        xrm_String,
        CONFIG_DEFAULT,
        "terminal",
        {.str = &config.terminal_emulator},
        NULL,
        "Terminal to use",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "ssh-client",
        {.str = &config.ssh_client},
        NULL,
        "Ssh client to use",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "ssh-command",
        {.str = &config.ssh_command},
        NULL,
        "Ssh command to execute",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "run-command",
        {.str = &config.run_command},
        NULL,
        "Run command to execute",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "run-list-command",
        {.str = &config.run_list_command},
        NULL,
        "Command to get extra run targets",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "run-shell-command",
        {.str = &config.run_shell_command},
        NULL,
        "Run command to execute that runs in shell",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "window-command",
        {.str = &config.window_command},
        NULL,
        "Command to executed when -kb-accept-alt binding is hit on selected "
        "window ",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "network-privilege-command",
        {.str = &config.network_privilege_command},
        NULL,
        "Command prefix for privileged network changes in network mode, for "
        "example \"doas\" or \"sudo -n\". Empty by default; sofi installs "
        "nothing setuid",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "window-match-fields",
        {.str = &config.window_match_fields},
        NULL,
        "Window fields to match in window mode",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "icon-theme",
        {.str = &config.icon_theme},
        NULL,
        "Theme to use to look for icons",
    },

    {
        xrm_String,
        CONFIG_DEFAULT,
        "drun-match-fields",
        {.str = &config.drun_match_fields},
        NULL,
        "Desktop entry fields to match in drun",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "drun-categories",
        {.str = &config.drun_categories},
        NULL,
        "Only show Desktop entry from these categories",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "drun-exclude-categories",
        {.str = &config.drun_exclude_categories},
        NULL,
        "Exclude Desktop entries from these categories",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "drun-show-actions",
        {.num = &config.drun_show_actions},
        NULL,
        "Desktop entry show actions.",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "drun-display-format",
        {.str = &config.drun_display_format},
        NULL,
        "DRUN format string. (Supports: generic,name,comment,exec,categories)",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "drun-url-launcher",
        {.str = &config.drun_url_launcher},
        NULL,
        "Command to open a Desktop Entry that is a Link.",
    },

    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "disable-history",
        {.num = &config.disable_history},
        NULL,
        "Disable history in run/ssh",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "ignored-prefixes",
        {.str = &config.ignored_prefixes},
        NULL,
        "Programs ignored for history",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "sort",
        {.num = &config.sort},
        NULL,
        "Sort menu when filtered",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "sorting-method",
        {.str = &config.sorting_method},
        NULL,
        "Choose sort strategy: normal (levenshtein) or fzf.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "case-sensitive",
        {.num = &config.case_sensitive},
        NULL,
        "Set case-sensitivity",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "case-smart",
        {.num = &config.case_smart},
        NULL,
        "Set smartcase like vim (determine case-sensitivity by input)",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "cycle",
        {.num = &config.cycle},
        NULL,
        "Cycle through the results list",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "sidebar-mode",
        {.num = &config.sidebar_mode},
        NULL,
        "Enable sidebar-mode",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "hover-select",
        {.snum = &config.hover_select},
        NULL,
        "Enable hover-select",
    },
    {
        xrm_SNumber,
        CONFIG_DEFAULT,
        "eh",
        {.snum = &config.element_height},
        NULL,
        "Row height (in chars)",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "auto-select",
        {.num = &config.auto_select},
        NULL,
        "Enable auto select mode",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "parse-hosts",
        {.num = &config.parse_hosts},
        NULL,
        "Parse hosts file for ssh mode",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "parse-known-hosts",
        {.num = &config.parse_known_hosts},
        NULL,
        "Parse known_hosts file for ssh mode",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "combi-modi",
        {.str = &config.combi_modes},
        NULL,
        "Set the modes to combine in combi mode",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "combi-modes",
        {.str = &config.combi_modes},
        NULL,
        "Set the modes to combine in combi mode",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "matching",
        {.str = &config.matching},
        NULL,
        "Set the matching algorithm. (normal, regex, glob, fuzzy, prefix)",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "tokenize",
        {.num = &config.tokenize},
        NULL,
        "Tokenize input string",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "monitor",
        {.str = &config.monitor},
        NULL,
        "",
    },
    /* Alias for dmenu compatibility. */
    {
        xrm_String,
        CONFIG_DEFAULT,
        "m",
        {.str = &config.monitor},
        NULL,
        "Monitor id to show on",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "filter",
        {.str = &config.filter},
        NULL,
        "Pre-set filter",
    },
    {
        xrm_SNumber,
        CONFIG_DEFAULT,
        "dpi",
        {.snum = &config.dpi},
        NULL,
        "DPI",
    },
    {
        xrm_Number,
        CONFIG_DEFAULT,
        "threads",
        {.num = &config.threads},
        NULL,
        "Threads to use for string matching",
    },
    {
        xrm_Number,
        CONFIG_DEFAULT,
        "scroll-method",
        {.num = &config.scroll_method},
        NULL,
        "Scrolling method. (0: Page, 1: Centered)",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "window-format",
        {.str = &config.window_format},
        NULL,
        "Window Format. w (desktop name), t (title), n (name), r (role), c "
        "(class)",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "click-to-exit",
        {.snum = &config.click_to_exit},
        NULL,
        "Click outside the window to exit",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "global-kb",
        {.snum = &config.global_kb},
        NULL,
        "Inhibit compositor shortcuts so they can be reused in sofi (wayland)",
    },
    {
        xrm_String,
        CONFIG_NO_DISPLAY,
        "theme",
        {.str = &config.theme},
        NULL,
        "New style theme file",
    },
    {
        xrm_Number,
        CONFIG_DEFAULT,
        "max-history-size",
        {.num = &config.max_history_size},
        NULL,
        "Max history size (WARNING: can cause slowdowns when set too high).",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "combi-hide-mode-prefix",
        {.snum = &config.combi_hide_mode_prefix},
        NULL,
        "Hide the prefix mode prefix on the combi view.**deprecated** use "
        "combi-display-format",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "combi-display-format",
        {.str = &config.combi_display_format},
        NULL,
        "Combi format string. (Supports: mode, text)",
    },
    {
        xrm_Char,
        CONFIG_DEFAULT,
        "matching-negate-char",
        {.charc = &config.matching_negate_char},
        NULL,
        "Set the character used to negate the matching. ('\\0' to disable)",
    },
    {
        xrm_String,
        CONFIG_DEFAULT,
        "cache-dir",
        {.str = &config.cache_dir},
        NULL,
        "Directory where history and temporary files are stored.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "window-thumbnail",
        {.snum = &config.window_thumbnail},
        NULL,
        "Show window thumbnail (if available) as icon in window switcher.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "drun-use-desktop-cache",
        {.snum = &config.drun_use_desktop_cache},
        NULL,
        "DRUN: build and use a cache with desktop file content.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "drun-reload-desktop-cache",
        {.snum = &config.drun_reload_desktop_cache},
        NULL,
        "DRUN: If enabled, reload the cache with desktop file content.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "normalize-match",
        {.snum = &config.normalize_match},
        NULL,
        "Normalize string when matching (disables match highlighting).",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "steal-focus",
        {.snum = &config.steal_focus},
        NULL,
        "Steal focus on launch and restore to window that had it on sofi start "
        "on "
        "close .",
    },
    {
        xrm_Number,
        CONFIG_DEFAULT,
        "refilter-timeout-limit",
        {.num = &(config.refilter_timeout_limit)},
        NULL,
        "When filtering takes  more then this time (in ms) switch to delayed "
        "filter.",
    },
    {
        xrm_Boolean,
        CONFIG_DEFAULT,
        "xserver-i300-workaround",
        {.snum = &(config.xserver_i300_workaround)},
        NULL,
        "Workaround for XServer issue #300 (issue #611 for sofi.)",
    },
    {xrm_String,
     CONFIG_DEFAULT,
     "completer-mode",
     {.str = &(config.completer_mode)},
     NULL,
     "What completer to use for drun/run."},
    {xrm_Boolean,
     CONFIG_DEFAULT,
     "imdkit",
     {.snum = &config.enable_imdkit},
     NULL,
     "Whether to enable imdkit"},
};

/** Dynamic array of extra options */
XrmOption *extra_options = NULL;
/** Number of entries in extra options array */
unsigned int num_extra_options = 0;

/** This is a big hack, we need to fix this. */
GList *extra_parsed_options = NULL;

static gboolean __config_parser_set_property(XrmOption *option,
                                             const Property *p, char **error);

void config_parser_add_option(XrmOptionType type, const char *key, void **value,
                              const char *comment) {
  extra_options =
      g_realloc(extra_options, (num_extra_options + 1) * sizeof(XrmOption));

  extra_options[num_extra_options].type = type;
  extra_options[num_extra_options].name = key;
  extra_options[num_extra_options].value.pointer = value;
  extra_options[num_extra_options].comment = comment;
  extra_options[num_extra_options].source = CONFIG_DEFAULT;
  switch (type) {
  case xrm_String:
    extra_options[num_extra_options].mem = ((char *)(*value));
    break;
  default:
    extra_options[num_extra_options].mem = NULL;
    break;
  }

  for (GList *iter = g_list_first(extra_parsed_options); iter != NULL;
       iter = g_list_next(iter)) {
    if (g_strcmp0(((Property *)(iter->data))->name, key) == 0) {
      char *error = NULL;
      g_debug("Setting property from backup list: %s", key);
      if (__config_parser_set_property(&(extra_options[num_extra_options]),
                                       (Property *)(iter->data), &error)) {
        g_debug("Failed to set property on custom entry: %s", key);
        g_free(error);
      }
      num_extra_options++;
      return;
    }
  }
  num_extra_options++;
}

/**
 * Parse an option from the commandline vector.
 */
static void config_parse_cmd_option(XrmOption *option) {
  // Prepend a - to the option name.
  char *key = g_strdup_printf("-%s", option->name);
  switch (option->type) {
  case xrm_Number:
    if (find_arg_uint(key, option->value.num) == TRUE) {
      option->source = (option->source & ~3) | CONFIG_CMDLINE;
    }
    break;
  case xrm_SNumber:
    if (find_arg_int(key, option->value.snum) == TRUE) {
      option->source = (option->source & ~3) | CONFIG_CMDLINE;
    }
    break;
  case xrm_String:
    if (find_arg_str(key, option->value.str) == TRUE) {
      if (option->mem != NULL) {
        g_free(option->mem);
        option->mem = NULL;
      }
      option->source = (option->source & ~3) | CONFIG_CMDLINE;
    }
    break;
  case xrm_Boolean:
    if (find_arg(key) >= 0) {
      *(option->value.num) = TRUE;
      option->source = (option->source & ~3) | CONFIG_CMDLINE;
    } else {
      g_free(key);
      key = g_strdup_printf("-no-%s", option->name);
      if (find_arg(key) >= 0) {
        *(option->value.num) = FALSE;
        option->source = (option->source & ~3) | CONFIG_CMDLINE;
      }
    }
    break;
  case xrm_Char:
    if (find_arg_char(key, option->value.charc) == TRUE) {
      option->source = (option->source & ~3) | CONFIG_CMDLINE;
    }
    break;
  default:
    break;
  }
  g_free(key);
}

/**
 * Function purpose: build a one-directive theme out of a command-line option,
 * in the format the theme reader accepts.
 *
 * `-element-text-background-color '#2b1e3a'` arrives here as the token path
 * element, text, background-color and the value, and leaves as
 *
 *   return { { select = "element text", ["background-color"] = "#2b1e3a" } }
 *
 * while a `configuration` option becomes a configuration directive with its
 * nested blocks as nested tables.
 *
 * Action purpose: the property name is always written in the bracketed form,
 * because nearly every one of them carries a hyphen and the bracketed form
 * needs no translation either way. The nesting that the previous format
 * expressed by opening a block per path element is a selector path here, which
 * is one string and so needs no closing pass.
 *
 * Action purpose: @p string decides whether the value is quoted, and the
 * caller tries the unquoted form FIRST and the quoted form after -- the same
 * two attempts it always made. Unquoted, the value is Lua, so
 * `-window-width 'percent(50)'` means what it says; quoted, it is text, which
 * is what `#2b1e3a` and `mono 12` need. Neither spelling can be inferred from
 * the value, which is why both are tried rather than guessed at.
 */
static gboolean config_parser_form_theme(GString *str, char **tokens, int count,
                                         char *argv, gboolean string,
                                         gboolean configuration) {
  if (strlen(argv) > 4096) {
    return FALSE;
  }

  g_string_append(str, "return { { ");

  if (configuration) {
    g_string_append(str, "configuration = { ");

    for (int j = 0; j < (count - 1); j++) {
      g_string_append_printf(str, "[\"%s\"] = { ", tokens[j]);
    }
  } else {
    g_string_append(str, "select = \"");

    for (int j = 0; j < (count - 1); j++) {
      if (j > 0) {
        g_string_append_c(str, ' ');
      }

      g_string_append(str, tokens[j]);
    }

    g_string_append(str, "\", ");
  }

  if (string) {
    char *esc = g_strescape(argv, NULL);
    g_string_append_printf(str, "[\"%s\"] = \"%s\"", tokens[count - 1], esc);
    g_free(esc);
  } else {
    g_string_append_printf(str, "[\"%s\"] = %s", tokens[count - 1], argv);
  }

  if (configuration) {
    for (int j = 0; j < (count - 1); j++) {
      g_string_append(str, " }");
    }

    g_string_append(str, " }");
  }

  g_string_append(str, " } }");

  return TRUE;
}

void config_parse_cmd_options(void) {
  for (unsigned int i = 0; i < sizeof(xrmOptions) / sizeof(XrmOption); ++i) {
    XrmOption *op = &(xrmOptions[i]);
    config_parse_cmd_option(op);
  }
  for (unsigned int i = 0; i < num_extra_options; ++i) {
    XrmOption *op = &(extra_options[i]);
    config_parse_cmd_option(op);
  }

  /** copy of the argc for use in commandline argument parser. */
  extern int stored_argc;
  /** copy of the argv pointer for use in the commandline argument parser */
  extern char **stored_argv;
  for (int in = 1; in < (stored_argc - 1); in++) {
    if (stored_argv[in][0] == '-') {
      if (stored_argv[in + 1][0] == '-') {
        continue;
      }
      /** TODO: This is a hack, and should be fixed in a nicer way. */
      char **tokens = g_strsplit(stored_argv[in], "-", 3);
      int count = 1;
      for (int j = 1; tokens && tokens[j]; j++) {
        count++;
      }
      if (count >= 2) {
        if (g_str_has_prefix(tokens[1], "theme")) {
          g_strfreev(tokens);
          tokens = g_strsplit(stored_argv[in], "+", 0);
          count = g_strv_length(tokens);
          if (count > 2) {
            GString *str = g_string_new("");
            config_parser_form_theme(str, &(tokens[1]), count - 1,
                                     stored_argv[in + 1], FALSE, FALSE);
            if (sofi_theme_try_string(str->str) == 1) {
              /** Unquoted did not read as Lua; try it as text. */
              sofi_clear_error_messages();
              g_string_truncate(str, 0);
              config_parser_form_theme(str, &(tokens[1]), count - 1,
                                       stored_argv[in + 1], TRUE, FALSE);
              if (sofi_theme_try_string(str->str) == 1) {
                sofi_clear_error_messages();
                g_strfreev(tokens);
                g_string_free(str, TRUE);
                return;
              }
            }
            g_string_free(str, TRUE);
          }
        } else if (g_strcmp0(tokens[1], "no") != 0) {
          GString *str = g_string_new("");
          config_parser_form_theme(str, &(tokens[1]), count - 1,
                                   stored_argv[in + 1], FALSE, TRUE);
          g_debug("str: \"%s\"\n", str->str);
          if (sofi_theme_try_string(str->str) == 1) {
            /** Unquoted did not read as Lua; try it as text. */
            sofi_clear_error_messages();
            g_string_truncate(str, 0);
            config_parser_form_theme(str, &(tokens[1]), count - 1,
                                     stored_argv[in + 1], TRUE, TRUE);
            g_debug("str: \"%s\"\n", str->str);
            if (sofi_theme_try_string(str->str) == 1) {
              /** Neither spelling read; the option was not for the theme. */
              sofi_clear_error_messages();
            }
          }
          g_string_free(str, TRUE);
        }
        in++;
      }
      g_strfreev(tokens);
    }
  }
}

static gboolean __config_parser_set_property(XrmOption *option,
                                             const Property *p, char **error) {
  if (option->type == xrm_String) {
    if (p->type != P_STRING && (p->type != P_LIST && p->type != P_INTEGER)) {
      *error =
          g_strdup_printf("Option: %s needs to be set with a string not a %s.",
                          option->name, PropertyTypeName[p->type]);
      return TRUE;
    }
    gchar *value = NULL;
    if (p->type == P_LIST) {
      for (GList *iter = p->value.list; iter != NULL;
           iter = g_list_next(iter)) {
        Property *p2 = (Property *)iter->data;
        if (value == NULL) {
          value = g_strdup((char *)(p2->value.s));
        } else {
          char *nv = g_strjoin(",", value, (char *)(p2->value.s), NULL);
          g_free(value);
          value = nv;
        }
      }
    } else if (p->type == P_INTEGER) {
      value = g_strdup_printf("%d", p->value.i);
    } else {
      value = g_strdup(p->value.s);
    }
    if ((option)->mem != NULL) {
      g_free(option->mem);
      option->mem = NULL;
    }
    *(option->value.str) = value;

    // Memory
    (option)->mem = *(option->value.str);
    option->source = (option->source & ~3) | CONFIG_FILE_THEME;
  } else if (option->type == xrm_Number) {
    if (p->type != P_INTEGER) {
      *error =
          g_strdup_printf("Option: %s needs to be set with a number not a %s.",
                          option->name, PropertyTypeName[p->type]);
      return TRUE;
    }
    *(option->value.snum) = p->value.i;
    option->source = (option->source & ~3) | CONFIG_FILE_THEME;
  } else if (option->type == xrm_SNumber) {
    if (p->type != P_INTEGER) {
      *error =
          g_strdup_printf("Option: %s needs to be set with a number not a %s.",
                          option->name, PropertyTypeName[p->type]);
      return TRUE;
    }
    *(option->value.num) = (unsigned int)(p->value.i);
    option->source = (option->source & ~3) | CONFIG_FILE_THEME;
  } else if (option->type == xrm_Boolean) {
    if (p->type != P_BOOLEAN) {
      *error =
          g_strdup_printf("Option: %s needs to be set with a boolean not a %s.",
                          option->name, PropertyTypeName[p->type]);
      return TRUE;
    }
    *(option->value.num) = (p->value.b);
    option->source = (option->source & ~3) | CONFIG_FILE_THEME;
  } else if (option->type == xrm_Char) {

    if (p->type != P_STRING) {
      *error =
          g_strdup_printf("Option: %s needs to be set with a string not a %s.",
                          option->name, PropertyTypeName[p->type]);
      return TRUE;
    }
    *(option->value.charc) = (p->value.s[0]);
    option->source = (option->source & ~3) | CONFIG_FILE_THEME;
  } else {
    // TODO add type
    *error = g_strdup_printf("Option: %s is not of a supported type: %s.",
                             option->name, PropertyTypeName[p->type]);
    return TRUE;
  }
  return FALSE;
}

gboolean config_parse_set_property(const Property *p, char **error) {
  if (g_ascii_strcasecmp(p->name, "theme") == 0) {
    if (p->type == P_STRING) {
      *error = g_strdup_printf("The option:\n<b>\nconfiguration\n{\n\ttheme: "
                               "\"%s\";\n}</b>\nis deprecated. Please replace "
                               "with: <b>@theme \"%s\"</b> "
                               "after the configuration block.",
                               p->value.s, p->value.s);
    } else {
      *error = g_strdup_printf("The option:\n<b>\nconfiguration\n{\n\ttheme: "
                               "\"%s\";\n}</b>\nis deprecated. Please replace "
                               "with: <b>@theme \"%s\"</b> "
                               "after the configuration block.",
                               "myTheme", "myTheme");
    }
    return TRUE;
  }
  for (unsigned int i = 0; i < sizeof(xrmOptions) / sizeof(XrmOption); ++i) {
    XrmOption *op = &(xrmOptions[i]);
    if (g_strcmp0(op->name, p->name) == 0) {
      return __config_parser_set_property(op, p, error);
    }
  }
  for (unsigned int i = 0; i < num_extra_options; ++i) {
    XrmOption *op = &(extra_options[i]);
    if (g_strcmp0(op->name, p->name) == 0) {
      return __config_parser_set_property(op, p, error);
    }
  }
  //*error = g_strdup_printf("Option: %s is not found.", p->name);
  g_debug("Option: %s is not found.", p->name);

  for (GList *iter = g_list_first(extra_parsed_options); iter != NULL;
       iter = g_list_next(iter)) {
    if (g_strcmp0(((Property *)(iter->data))->name, p->name) == 0) {
      sofi_theme_property_free((Property *)(iter->data));
      iter->data = (void *)sofi_theme_property_copy(p, NULL);
      return FALSE;
    }
  }
  g_debug("Adding option: %s to backup list.", p->name);
  extra_parsed_options =
      g_list_append(extra_parsed_options, sofi_theme_property_copy(p, NULL));

  return FALSE;
}

void config_xresource_free(void) {
  for (unsigned int i = 0; i < (sizeof(xrmOptions) / sizeof(*xrmOptions));
       ++i) {
    if (xrmOptions[i].mem != NULL) {
      g_free(xrmOptions[i].mem);
      xrmOptions[i].mem = NULL;
    }
  }
  for (unsigned int i = 0; i < num_extra_options; ++i) {
    if (extra_options[i].mem != NULL) {
      g_free(extra_options[i].mem);
      extra_options[i].mem = NULL;
    }
  }
  if (extra_options != NULL) {
    g_free(extra_options);
  }
  g_list_free_full(extra_parsed_options,
                   (GDestroyNotify)sofi_theme_property_free);
}

/** How far the declarations of the configuration block are indented. */
#define DUMP_CONFIG_INDENT 6

/**
 * Function purpose: print one option as a Lua assignment.
 *
 * Action purpose: an option still at its DEFAULT is commented out, and that is
 * the point of this output rather than a decoration of it: what is live is what
 * this invocation changed, and the rest is there to be read and uncommented.
 * The marker is `--` and not the block comment the format before this one used,
 * because what the whole output now has to be is a Lua chunk the reader
 * accepts.
 */
static void config_parse_dump_config_option(FILE *out, XrmOption *option) {
  gboolean commented =
      option->type == xrm_Char || (option->source & 3) == CONFIG_DEFAULT;
  char *key = sofi_theme_lua_key(option->name);

  fprintf(out, "%*s%s%s = ", DUMP_CONFIG_INDENT, "", commented ? "-- " : "",
          key);
  g_free(key);

  switch (option->type) {
  case xrm_Number:
    fprintf(out, "%u", *(option->value.num));
    break;
  case xrm_SNumber:
    fprintf(out, "%i", *(option->value.snum));
    break;
  case xrm_String: {
    /* An option with no value has nothing to print and nothing to set. `nil`
     * is what Lua says about a key a table does not have, which is what an
     * unset option is. */
    char *quoted = (*(option->value.str)) != NULL
                       ? sofi_theme_lua_quote(*(option->value.str))
                       : g_strdup("nil");

    fputs(quoted, out);
    g_free(quoted);
    break;
  }
  case xrm_Boolean:
    fprintf(out, "%s", (*(option->value.num) == TRUE) ? "true" : "false");
    break;
  case xrm_Char: {
    /* A single character is not a type this configuration language has, which
     * is why the line is commented out whatever its source. It is still
     * printed as a string, so that what it is set to can be read off. */
    char text[2] = {*(option->value.charc), '\0'};
    char *quoted = sofi_theme_lua_quote(text);

    fputs(quoted, out);
    g_free(quoted);
    break;
  }
  default:
    fputs("nil", out);
    break;
  }

  fputs(",", out);
  if (option->type == xrm_Char) {
    fputs(" -- a single character has no spelling here", out);
  }
  fputs("\n", out);
}

void config_parse_dump_config_lua_format(FILE *out, gboolean changes) {
  fprintf(out,
          "-- sofi -dump-config output.\n"
          "--\n"
          "-- An option that is commented out is one this invocation left at\n"
          "-- its default. Uncomment it to set it.\n"
          "\n"
          "return {\n"
          "\n"
          "  { configuration = {\n");

  unsigned int entries = sizeof(xrmOptions) / sizeof(*xrmOptions);
  for (unsigned int i = 0; i < entries; ++i) {
    // Skip duplicates.
    if ((i + 1) < entries) {
      if (xrmOptions[i].value.str == xrmOptions[i + 1].value.str) {
        continue;
      }
    }
    if ((xrmOptions[i].source & CONFIG_NO_DISPLAY) == CONFIG_NO_DISPLAY) {
      continue;
    }
    if (!changes || (xrmOptions[i].source & 3) != CONFIG_DEFAULT) {
      config_parse_dump_config_option(out, &(xrmOptions[i]));
    }
  }
  for (unsigned int i = 0; i < num_extra_options; i++) {
    if ((extra_options[i].source & CONFIG_NO_DISPLAY) == CONFIG_NO_DISPLAY) {
      continue;
    }
    if (!changes || (extra_options[i].source & 3) != CONFIG_DEFAULT) {

      config_parse_dump_config_option(out, &(extra_options[i]));
    }
  }

  /* A nested block is `name = { ... }` and not a rule: see
   * sofi_theme_print_lua_config_block(). The tree is only built by a
   * configuration that has one, so there may be no tree at all.
   *
   * That printer writes to stdout rather than to @p out, as the theme printer
   * it belongs to does throughout; the one caller of this function passes
   * stdout, which is why the two interleave correctly. */
  for (unsigned int index = 0;
       sofi_configuration != NULL && index < sofi_configuration->num_widgets;
       index++) {
    sofi_theme_print_lua_config_block(sofi_configuration->widgets[index],
                                      DUMP_CONFIG_INDENT);
  }

  fprintf(out, "  } },\n");

  if (config.theme != NULL) {
    char *quoted = sofi_theme_lua_quote(config.theme);

    /* `@theme "x"` was a reset followed by an import, and that is what it is
     * here: the first element throws the tree away, and import() returns the
     * named file's own directives for this array to carry out in place. */
    fprintf(out, "\n  { reset = true },\n  import(%s),\n", quoted);
    g_free(quoted);
  }

  fprintf(out, "}\n");
}

static void print_option_string(XrmOption *xo, int is_term) {
  int l = strlen(xo->name);
  if (is_term) {
    printf("\t" color_bold "-%s" color_reset " [string]%-*c%s\n", xo->name,
           30 - l, ' ', xo->comment);
    printf("\t" color_italic "%s" color_reset,
           (*(xo->value.str) == NULL) ? "(unset)" : (*(xo->value.str)));
    printf(" " color_green "(%s)" color_reset "\n",
           ConfigSourceStr[xo->source & 3]);
  } else {
    printf("\t-%s [string]%-*c%s\n", xo->name, 30 - l, ' ', xo->comment);
    printf("\t\t%s",
           (*(xo->value.str) == NULL) ? "(unset)" : (*(xo->value.str)));
    printf(" (%s)\n", ConfigSourceStr[xo->source & 3]);
  }
}
static void print_option_number(XrmOption *xo, int is_term) {
  int l = strlen(xo->name);
  if (is_term) {
    printf("\t" color_bold "-%s" color_reset " [number]%-*c%s\n", xo->name,
           30 - l, ' ', xo->comment);
    printf("\t" color_italic "%u" color_reset, *(xo->value.num));
    printf(" " color_green "(%s)" color_reset "\n",
           ConfigSourceStr[xo->source & 3]);
  } else {
    printf("\t-%s [number]%-*c%s\n", xo->name, 30 - l, ' ', xo->comment);
    printf("\t\t%u", *(xo->value.num));
    printf(" (%s)\n", ConfigSourceStr[xo->source & 3]);
  }
}
static void print_option_snumber(XrmOption *xo, int is_term) {
  int l = strlen(xo->name);
  if (is_term) {
    printf("\t" color_bold "-%s" color_reset " [number]%-*c%s\n", xo->name,
           30 - l, ' ', xo->comment);
    printf("\t" color_italic "%d" color_reset, *(xo->value.snum));
    printf(" " color_green "(%s)" color_reset "\n",
           ConfigSourceStr[xo->source & 3]);
  } else {
    printf("\t-%s [number]%-*c%s\n", xo->name, 30 - l, ' ', xo->comment);
    printf("\t\t%d", *(xo->value.snum));
    printf(" (%s)\n", ConfigSourceStr[xo->source & 3]);
  }
}
static void print_option_char(XrmOption *xo, int is_term) {
  int l = strlen(xo->name);
  if (is_term) {
    printf("\t" color_bold "-%s" color_reset " [character]%-*c%s\n", xo->name,
           30 - l, ' ', xo->comment);
    printf("\t" color_italic "%c" color_reset, *(xo->value.charc));
    printf(" " color_green "(%s)" color_reset "\n",
           ConfigSourceStr[xo->source & 3]);
  } else {
    printf("\t-%s [character]%-*c%s\n", xo->name, 30 - l, ' ', xo->comment);
    printf("\t\t%c", *(xo->value.charc));
    printf(" (%s)\n", ConfigSourceStr[xo->source & 3]);
  }
}
static void print_option_boolean(XrmOption *xo, int is_term) {
  int l = strlen(xo->name);
  if (is_term) {
    printf("\t" color_bold "-[no-]%s" color_reset " %-*c%s\n", xo->name, 33 - l,
           ' ', xo->comment);
    printf("\t" color_italic "%s" color_reset,
           (*(xo->value.snum)) ? "True" : "False");
    printf(" " color_green "(%s)" color_reset "\n",
           ConfigSourceStr[xo->source & 3]);
  } else {
    printf("\t-[no-]%s %-*c%s\n", xo->name, 33 - l, ' ', xo->comment);
    printf("\t\t%s", (*(xo->value.snum)) ? "True" : "False");
    printf(" (%s)\n", ConfigSourceStr[xo->source & 3]);
  }
}

static void print_option(XrmOption *xo, int is_term) {
  if ((xo->source & CONFIG_NO_DISPLAY) == CONFIG_NO_DISPLAY) {
    return;
  }
  switch (xo->type) {
  case xrm_String:
    print_option_string(xo, is_term);
    break;
  case xrm_Number:
    print_option_number(xo, is_term);
    break;
  case xrm_SNumber:
    print_option_snumber(xo, is_term);
    break;
  case xrm_Boolean:
    print_option_boolean(xo, is_term);
    break;
  case xrm_Char:
    print_option_char(xo, is_term);
    break;
  default:
    break;
  }
}
void print_options(void) {
  // Check output filedescriptor
  int is_term = isatty(fileno(stdout));
  unsigned int entries = sizeof(xrmOptions) / sizeof(*xrmOptions);
  for (unsigned int i = 0; i < entries; ++i) {
    if ((i + 1) < entries) {
      if (xrmOptions[i].value.str == xrmOptions[i + 1].value.str) {
        continue;
      }
    }
    print_option(&xrmOptions[i], is_term);
  }
  for (unsigned int i = 0; i < num_extra_options; i++) {
    print_option(&extra_options[i], is_term);
  }
}

void print_help_msg(const char *option, const char *type, const char *text,
                    const char *def, int isatty) {
  int l = 37 - strlen(option) - strlen(type);
  if (isatty) {
    printf("\t%s%s%s %s %-*c%s\n", color_bold, option, color_reset, type, l,
           ' ', text);
    if (def != NULL) {
      printf("\t\t%s%s%s\n", color_italic, def, color_reset);
    }
  } else {
    printf("\t%s %s %-*c%s\n", option, type, l, ' ', text);
    if (def != NULL) {
      printf("\t\t%s\n", def);
    }
  }
}

static char *config_parser_return_display_help_entry(XrmOption *option,
                                                     size_t l) {
  int ll = (int)l;
  switch (option->type) {
  case xrm_Number:
    return g_markup_printf_escaped(
        "<b%-*s</b> (%u) <span style='italic' size='small'>%s</span>", ll,
        option->name, *(option->value.num), option->comment);
  case xrm_SNumber:
    return g_markup_printf_escaped(
        "<b%-*s</b> (%d) <span style='italic' size='small'>%s</span>", ll,
        option->name, *(option->value.snum), option->comment);
  case xrm_String:
    return g_markup_printf_escaped(
        "<b>%-*s</b> (%s) <span style='italic' size='small'>%s</span>", ll,
        option->name,
        (*(option->value.str) != NULL) ? *(option->value.str) : "null",
        option->comment);
  case xrm_Boolean:
    return g_markup_printf_escaped(
        "<b>%-*s</b> (%s) <span style='italic' size='small'>%s</span>", ll,
        option->name, (*(option->value.num) == TRUE) ? "true" : "false",
        option->comment);
  case xrm_Char:
    if (*(option->value.charc) > 32 && *(option->value.charc) < 127) {
      return g_markup_printf_escaped(
          "<b>%-*s</b> (%c) <span style='italic' size='small'>%s</span>", ll,
          option->name, *(option->value.charc), option->comment);
    } else {
      return g_markup_printf_escaped(
          "<b%-*s</b> (\\x%02X) <span style='italic' size='small'>%s</span>",
          ll, option->name, *(option->value.charc), option->comment);
    }
  default:
    break;
  }

  return g_strdup("failed");
}

char **config_parser_return_display_help(unsigned int *length) {
  unsigned int entries = sizeof(xrmOptions) / sizeof(*xrmOptions);
  char **retv = NULL;
  /**
   * Get length of name
   */
  size_t max_length = 0;
  for (unsigned int i = 0; i < entries; ++i) {
    size_t l = strlen(xrmOptions[i].name);
    max_length = MAX(max_length, l);
  }
  for (unsigned int i = 0; i < num_extra_options; i++) {
    size_t l = strlen(extra_options[i].name);
    max_length = MAX(max_length, l);
  }
  /**
   * Generate entries
   */
  for (unsigned int i = 0; i < entries; ++i) {
    if ((i + 1) < entries) {
      if (xrmOptions[i].value.str == xrmOptions[i + 1].value.str) {
        continue;
      }
    }
    if (strncmp(xrmOptions[i].name, "kb", 2) != 0 &&
        strncmp(xrmOptions[i].name, "ml", 2) != 0 &&
        strncmp(xrmOptions[i].name, "me", 2) != 0) {
      continue;
    }

    retv = g_realloc(retv, ((*length) + 2) * sizeof(char *));

    retv[(*length)] =
        config_parser_return_display_help_entry(&xrmOptions[i], max_length);
    (*length)++;
  }
  for (unsigned int i = 0; i < num_extra_options; i++) {
    if (strncmp(extra_options[i].name, "kb", 2) != 0 &&
        strncmp(extra_options[i].name, "ml", 2) != 0 &&
        strncmp(extra_options[i].name, "me", 2) != 0) {
      continue;
    }
    retv = g_realloc(retv, ((*length) + 2) * sizeof(char *));
    retv[(*length)] =
        config_parser_return_display_help_entry(&extra_options[i], max_length);
    (*length)++;
  }
  if ((*length) > 0) {
    retv[(*length)] = NULL;
  }
  return retv;
}
