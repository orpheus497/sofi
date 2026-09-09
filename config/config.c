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

#include "config.h"
#include "sofi-types.h"
#include "settings.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

Settings config = {
/** List of enabled modes. */
/** -modes */
    /* `window` is the control panel and is always present -- it is the entry
     * point to every other indexer, so it does not depend on the window
     * switcher being compiled in. The switcher itself is `windowlist`.
     *
     * Action purpose: this list is deliberately SHORT, and it is not the list
     * of what the control panel offers. Those are different questions and
     * conflating them breaks the program.
     *
     * `run_mode_index()` calls `mode_init()` on EVERY entry of this list before
     * any surface opens, and **aborts into an error dialog if a single one
     * fails**. Listing every mode here would therefore mean that opening the
     * application menu first probes wpctl/pactl/mixer, runs a wireless scan,
     * and contacts hikari's control socket -- and that a laptop with no audio
     * server, or any machine that is not running hikari, could not open the
     * application menu at all, because the sheet switcher failed to initialise.
     *
     * The control panel does not need this list. Each of its buttons spawns
     * `sofi -show <mode>`, and `-show` adds a mode on demand in that child
     * process (see `sofi_enable_mode()`), so every button works whether or not
     * its mode is named here -- and the cost of initialising it is paid once,
     * in the child, by the user who asked for it. */
    .modes = "window,drun,run,ssh",
    /** Font */
    .menu_font = "mono 12",

    /** Whether to load and show icons */
    .show_icons = FALSE,

    /** Custom command to generate preview icons */
    .preview_cmd = NULL,

    /** Custom command to call when menu selection changes */
    .on_selection_changed = NULL,
    /** Custom command to call when menu mode changes */
    .on_mode_changed = NULL,
    /** Custom command to call when menu entry is accepted */
    .on_entry_accepted = NULL,
    /** Custom command to call when menu is canceled */
    .on_menu_canceled = NULL,
    /** Custom command to call when menu finds errors */
    .on_menu_error = NULL,
    /** Custom command to call when menu screenshot is taken */
    .on_screenshot_taken = NULL,
    /** Terminal to use. (for ssh and open in terminal) */
    .terminal_emulator = "sofi-sensible-terminal",
    .ssh_client = "ssh",
    /** Command when executing ssh. */
    .ssh_command = "{terminal} -e {ssh-client} {host} [-p {port}]",
    /** Command when running */
    .run_command = "{cmd}",
    /** Command used to list executable commands. empty -> internal */
    .run_list_command = "",
    /** Command executed when running application in terminal */
    .run_shell_command = "{terminal} -e {cmd}",
    /** Command executed on accep-entry-custom for window modus */
    .window_command = "wmctrl -i -R {window}",
    .network_privilege_command = "",
    /** No default icon theme, we search Adwaita and gnome as fallback */
    .icon_theme = NULL,
    /**
     * Location of the window, as a position index 0-8 (not a WindowLocation
     * value; the backends map it through loc_transtable[]):
     *
     *   1  2  3
     *   8  0  4
     *   7  6  5
     */
    .location = 0,
    /**
     * On Wayland, specifies the layer where sofi is rendered. Available layers are
     * `background`, `bottom`, `top`, `overlay`. The default layer is `overlay`.
     */
    .wayland_layer = "overlay",
    /**
     * On Wayland, how the layer surface takes keyboard focus: `none`,
     * `exclusive` or `on-demand`. Menus need `exclusive`, which is the
     * default; passive surfaces such as notifications should not hold the
     * keyboard for their lifetime.
     */
    .wayland_keyboard_interactivity = "exclusive",
    /** Y offset */
    .y_offset = 0,
    /** X offset */
    .x_offset = 0,
    /** Always show config.menu_lines lines, even if less lines are available */
    .fixed_num_lines = TRUE,
    /** Do not use history */
    .disable_history = FALSE,
    /** Programs ignored for history */
    .ignored_prefixes = "",
    /** Sort the displayed list */
    .sort = FALSE,
    /** Use levenshtein sorting when matching */
    .sorting_method = "normal",
    /** Case sensitivity of the search */
    .case_sensitive = FALSE,
    /** Case smart of the search */
    .case_smart = FALSE,
    /** Cycle through in the element list */
    .cycle = TRUE,
    /** Height of an element in #chars */
    .element_height = 1,
    /** Sidebar mode, show the modes */
    .sidebar_mode = FALSE,
    /** auto select */
    .auto_select = FALSE,
    /** Parse /etc/hosts file in ssh view. */
    .parse_hosts = FALSE,
    /** Parse ~/.ssh/known_hosts file in ssh view. */
    .parse_known_hosts = TRUE,
    /** Modes to combine into one view. */
    /* The Everything button on the control panel opens this. Windows are not in
     * it: saber lists those, and combi is sofi's own indexes merged. */
    .combi_modes = "drun,run,ssh,filebrowser",
    .tokenize = TRUE,
    .matching = "normal",
    .matching_method = MM_NORMAL,

    /** Desktop entries to match in drun */
    .drun_match_fields = "name,generic,exec,categories,keywords",
    /** Only show entries in this category */
    .drun_categories = NULL,
    /** Exclude entries in this category */
    .drun_exclude_categories = NULL,
    /** Desktop entry show actions */
    .drun_show_actions = FALSE,
    /** Desktop format display */
    .drun_display_format =
        "{name} [<span weight='light' size='small'><i>({generic})</i></span>]",
    /** Desktop Link launch command */
    .drun_url_launcher = "xdg-open",

    /** Window fields to match in window mode*/
    .window_match_fields = "all",
    /** Monitor */
    .monitor = "-5",
    /** Set filter */
    .filter = NULL,
    .dpi = -1,
    .threads = 0,
    .scroll_method = 0,
    .window_format = "{w}    {c}   {t}",
    .click_to_exit = TRUE,
    .global_kb = FALSE,
    .theme = NULL,
    .plugin_path = PLUGIN_PATH,
    .max_history_size = 25,
    .combi_hide_mode_prefix = FALSE,
    .combi_display_format = "{mode} {text}",

    .matching_negate_char = '-',

    .cache_dir = NULL,
    .window_thumbnail = FALSE,

    /** drun cache */
    .drun_use_desktop_cache = FALSE,
    .drun_reload_desktop_cache = FALSE,

    /** Benchmarks */
    .benchmark_ui = FALSE,

    /** normalize match */
    .normalize_match = FALSE,
    /** steal focus */
    .steal_focus = FALSE,
    /** refilter limit in ms*/
    .refilter_timeout_limit = 300,
    /** workaround for broken xserver (#300 on xserver, #611) */
    .xserver_i300_workaround = FALSE,
    /** What browser to use for completion */
    .completer_mode = "filebrowser",
    /** Whether to enable imdkit, see #2123 */
    .enable_imdkit = TRUE,
};
