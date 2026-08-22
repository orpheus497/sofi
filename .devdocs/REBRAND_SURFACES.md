# Rebrand Surface Inventory

Generated 2026-08-22 18:55. 166 identity surfaces catalogued across 10 audit domains, grouped by what breaks if the surface is renamed.

| Risk class | Count |
|---|---|
| Breaks third-party plugins | 35 |
| Breaks existing user configuration | 28 |
| Breaks distribution packaging | 13 |
| User-visible but safe | 37 |
| Internal only | 53 |


---

# Breaks third-party plugins

Changing these breaks out-of-tree plugins at build time (headers, pkg-config) or load time (symbols, ABI, plugin dir).

### Meson project name - drives plugindir, themedir, gettext package, installed-header subdir, pkg-config pluginsdir, PACKAGE_NAME and the dist tarball basename

- **Current** `rofi`
- **Proposed** sofi
- **Locations** meson.build:1 project('rofi', 'c', - consumed via meson.project_name() at meson.build:43 (plugindir), :44 (themedir), :152 (PACKAGE_NAME), :155 (GETTEXT_PACKAGE), :202 (install_headers subdir), :425 (pkgconfig pluginsdir variable), and doc/meson.build:55 (doxygen PROJECT_NAME)

### Plugin search directory (compiled into the binary as PLUGIN_PATH and exported to plugin authors via pkg-config)

- **Current** `${libdir}/rofi`
- **Proposed** ${libdir}/sofi
- **Locations** meson.build:43 plugindir = join_paths(get_option('libdir'), meson.project_name()); baked in at meson.build:174 header_conf.set_quoted('PLUGIN_PATH', ...); exported at meson.build:425

### Installed public headers directory for third-party plugins

- **Current** `/usr/include/rofi/`
- **Proposed** /usr/include/sofi/
- **Locations** meson.build:195-203 install_headers([...], subdir: meson.project_name()) - mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h

### Installed header filenames carrying the name (include/mode.h:30 and include/helper.h:30 include "rofi-types.h" by quoted relative path, so renaming the files means editing those includes too)

- **Current** `rofi-types.h, rofi-icon-fetcher.h`
- **Proposed** sofi-types.h, sofi-icon-fetcher.h
- **Locations** meson.build:199 'include/rofi-types.h', meson.build:200 'include/rofi-icon-fetcher.h'; internal includes at include/mode.h:30 and include/helper.h:30

### pkg-config module name and file - hardcoded rather than derived from meson.project_name(), so it will NOT follow a project() rename

- **Current** `rofi.pc / Name: rofi`
- **Proposed** sofi.pc / Name: sofi
- **Locations** meson.build:420 filebase: 'rofi', meson.build:421 name: 'rofi', meson.build:423 description: 'Header files for rofi plugins',

### C source/header filenames carrying the name, plus the meson source-list variable and target handle

- **Current** `rofi.c/rofi.h/rofi-types.*/rofi-icon-fetcher.*, rofi_sources, rofi`
- **Proposed** sofi.c/sofi.h/sofi-types.*/sofi-icon-fetcher.*, sofi_sources, sofi
- **Locations** meson.build:230 'source/rofi.c', :239 'source/rofi-icon-fetcher.c', :250 'source/rofi-types.c', :262 'include/rofi.h', :269 'include/rofi-icon-fetcher.h', :275 'include/rofi-types.h'; the rofi_sources variable at meson.build:229, 298, 352-354, 626-627, 640, 650; the rofi target handle at meson.build:367, 434, 444, 462, 481, 499, 517, 533, 547, 561, 581, 596, 613

### Installed public headers consumed by this file (the /usr/include/rofi plugin ABI surface)

- **Current** `rofi.h / rofi-types.h under /usr/include/rofi`
- **Proposed** sofi.h / sofi-types.h under /usr/include/sofi
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:51 — #include <rofi.h>; :55 — #include "rofi-types.h"; also include/rofi.h, include/rofi-types.h, include/rofi-icon-fetcher.h

### Public cursor-type enum and typedef used by the display proxy API

- **Current** `RofiCursorType, ROFI_CURSOR_*`
- **Proposed** SofiCursorType, SOFI_CURSOR_*
- **Locations** /home/orpheus497/Projects/sofi/include/wayland-internal.h:65 (RofiCursorType type); /home/orpheus497/Projects/sofi/source/wayland/display.c:739, 767, 830 (RofiCursorType params) and :749, 752, 769, 771, 1735 (ROFI_CURSOR_POINTER / ROFI_CURSOR_TEXT / ROFI_CURSOR_DEFAULT)

### View-state type and rofi_view_* function family called throughout the backend

- **Current** `RofiViewState, rofi_view_*`
- **Proposed** SofiViewState, sofi_view_*
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c — RofiViewState at :291, 438, 463, 487, 537, 611, 1287, 1302, 1317, 1666; rofi_view_get_active/handle_text/maybe_update/pool_refresh/set_size/frame_callback/get_menu_rect/cancel/handle_mouse_motion/get_active_text at :121, 127, 289, 293, 363, 444, 447, 469, 479, 515, 525, 539, 622, 637, 666, 669, 681, 730, 1289, 1304-1305, 1319, 1664, 1668, 2018

### Helper execute-context type in the display_proxy vtable signature

- **Current** `RofiHelperExecuteContext`
- **Proposed** SofiHelperExecuteContext
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:1965-1968 — wayland_display_startup_notification(RofiHelperExecuteContext *context, ...)

### Installed public headers directory and the rofi_*/ROFI_*/Rofi* symbol prefixes they export

- **Current** `prefix `rofi_` / `ROFI_` / `Rofi`, include dir /usr/include/rofi`
- **Proposed** prefix `sofi_` / `SOFI_` / `Sofi`, include dir /usr/include/sofi
- **Locations** meson.build:195-202 installs include/mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h into `subdir: meson.project_name()` (i.e. /usr/include/rofi). The domain consumes these names pervasively: source/modes/wayland-window.c:43,49 (#include "rofi.h", "rofi-icon-fetcher.h"), 187 rofi_view_reload, 572/593 rofi_view_error_dialog, 621 rofi_view_hide, 639 rofi_config_find_widget, 641 rofi_theme_find_property, 659 RofiHelperExecuteContext, 684/701 rofi_int_matcher, 840/851/854 rofi_icon_fetcher_*; source/wayland/view.c:44,150,157-158,363,380,408,485 (rofi_theme_*, RofiDistance, RofiOrientation); include/display-internal.h:45 and include/display.h:116 (RofiHelperExecuteContext); include/display.h:204 (ClipboardCb)

### Cross-module C symbols carrying the rofi identity in this domain

- **Current** `rofi_* / xcb_rofi_* / Rofi* prefixes`
- **Proposed** sofi_* / xcb_sofi_* / Sofi* prefixes
- **Locations** source/xcb/view.c:77,79,81 and 963-998 (xcb_rofi_view_* function family), source/xcb/view.c:571 (rofi_set_im_window_pos), source/xcb/display.c:1109 (rofi_view_paste), :1190 (rofi_key_press_event_handler), :1210 (rofi_key_release_event_handler); consumed rofi_* API from shared headers at source/xcb/display.c:1123,1205,1222,1241,1414 etc.

### Project header file names included by this domain (these are the /usr/include/<project> install set)

- **Current** `rofi.h, rofi-types.h, rofi-icon-fetcher.h (installed under <includedir>/rofi per meson.build:195-203)`
- **Proposed** sofi.h, sofi-types.h, sofi-icon-fetcher.h under <includedir>/sofi
- **Locations** source/xcb/display.c:65 ("rofi-types.h"), :76 (<rofi.h>); source/xcb/view.c:56 ("rofi.h"); source/modes/window.c:55 ("rofi.h"), :62 ("rofi-icon-fetcher.h")

### Recursion-guard env var (also exported to script modes)

- **Current** `ROFI_OUTSIDE`
- **Proposed** SOFI_OUTSIDE
- **Locations** read at source/rofi.c:991; written at source/modes/script.c:220

### Script-mode protocol env vars

- **Current** `ROFI_RETV, ROFI_OUTSIDE, ROFI_INFO, ROFI_DATA, ROFI_INPUT`
- **Proposed** SOFI_RETV, SOFI_OUTSIDE, SOFI_INFO, SOFI_DATA, SOFI_INPUT
- **Locations** source/modes/script.c:216, 220, 224, 227, 231

### Exported C symbol prefix and public type names

- **Current** `rofi_* / Rofi* / ROFI_*`
- **Proposed** sofi_* / Sofi* / SOFI_*
- **Locations** include/view.h:54-412 (RofiViewState, rofi_view_* ~45 functions, rofi_capture_screenshot, rofi_set_im_window_pos), include/rofi.h:54-121 (rofi_get_mode, rofi_add_error_message, rofi_quit_main_loop, rofi_collect_modes_search, rofi_get_completer), include/rofi-types.h (RofiHighlightColorStyle, RofiCursorType, ROFI_HL_*), source/rofi.c and source/view.c definitions throughout

### Plugin ABI: entry symbol + version constant

- **Current** `exported symbol "mode", ABI_VERSION 7`
- **Proposed** keep the symbol name "mode" but bump ABI_VERSION — every rofi plugin binary must be recompiled against the renamed headers anyway
- **Locations** include/mode.h:36 (#define ABI_VERSION 7u); loader at source/rofi.c:643-662 (g_module_symbol(mod, "mode", ...) and the ABI_VERSION comparison at :648)

### Installed public headers directory

- **Current** `/usr/include/rofi/{mode.h,mode-private.h,helper.h,rofi-types.h,rofi-icon-fetcher.h}`
- **Proposed** /usr/include/sofi/... — note two of the five filenames also carry the brand (rofi-types.h, rofi-icon-fetcher.h)
- **Locations** meson.build:195-203 (install_headers mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h with subdir: meson.project_name())

### Plugin and theme install directories + compiled-in default

- **Current** `$libdir/rofi, $datadir/rofi/themes`
- **Proposed** $libdir/sofi, $datadir/sofi/themes
- **Locations** meson.build:42 plugindir, meson.build:43 themedir, config/config.c:159 (.plugin_path = PLUGIN_PATH)

### Source filenames carrying the brand

- **Current** `rofi.* / rofi-*`
- **Proposed** sofi.* / sofi-* — the three installed rofi-*.h headers are the only ones where the rename is load-bearing for plugin authors
- **Locations** source/rofi.c, include/rofi.h, include/rofi-types.h, include/rofi-icon-fetcher.h, source/rofi-icon-fetcher.c, pkgconfig/rofi.pc.in, doc/rofi.doxy.in, data/rofi.*, script/rofi-*

### Icon fetcher public API (INSTALLED header, plugin-facing)

- **Current** `file name rofi-icon-fetcher.h, global `rofi_icon_fetcher_data`, functions rofi_icon_fetcher_init/destroy/query/query_advanced/get/get_ex/file_is_image`
- **Proposed** sofi-icon-fetcher.h + sofi_icon_fetcher_*
- **Locations** include/rofi-icon-fetcher.h:19,24,38,54,64,74,83, installed to $includedir/rofi/ by meson.build:195-203; implementation source/rofi-icon-fetcher.c:103,273,325,443,752,799,846,856; consumed in-domain at source/widgets/icon.c:39,92,180

### Script-mode environment variables (the public script protocol)

- **Current** `ROFI_RETV, ROFI_OUTSIDE, ROFI_INFO, ROFI_DATA, ROFI_INPUT`
- **Proposed** SOFI_RETV, SOFI_OUTSIDE, SOFI_INFO, SOFI_DATA, SOFI_INPUT — but set BOTH names for at least one release cycle
- **Locations** source/modes/script.c:216 (ROFI_RETV), :220 (ROFI_OUTSIDE), :224 (ROFI_INFO), :227 (ROFI_DATA), :231 (ROFI_INPUT). ROFI_OUTSIDE is also *read* back at source/rofi.c:991 for the "don't run inside itself" check. Documented at doc/rofi-script.5.markdown:55,65,70,74,157 and exercised by Examples/test_script_env.sh:3,12,14,18 and Examples/test_script_mode_delim.sh:10.

### Installed public plugin headers (the /usr/include/rofi/modes/ tree) and their include guards

- **Current** `ROFI_MODE_*_H / ROFI_MODES_*_H guards, installed under an include dir named rofi`
- **Proposed** SOFI_MODE_*_H, installed under sofi/ — with a compatibility symlink or a shim rofi/ include dir if third-party plugins are to keep building
- **Locations** include/modes/combi.h:28-29,51; dmenu.h:28-29,51; dmenuscriptshared.h:1-2,51; drun.h:28-29,43; filebrowser.h:28-29,58; help-keys.h:28-29,45; modes.h:28-29,49; recursivebrowser.h:28-29,58; run.h:28-29,48; script.h:28-29,74; ssh.h:28-29,50; window.h:28-29,47; wayland-window.h:28-29,45

### rofi_* / Rofi* API symbols and types consumed by every mode (this is the plugin ABI surface, defined outside this domain but used pervasively inside it)

- **Current** `rofi_* functions, ROFI_* macros, Rofi* types, ABI_VERSION from rofi's mode.h`
- **Proposed** Keep the rofi_* symbol prefix, or rename to sofi_* and ship a compatibility header of #defines. Renaming the exported symbols breaks every out-of-tree plugin binary; renaming only the internals is a large but self-contained diff.
- **Locations** rofi_int_matcher (combi.c:200, dmenu.c:59,691, drun.c:1581,1592, filebrowser.c:611, help-keys.c:99, recursivebrowser.c:503, run.c:548, script.c:512,533, ssh.c:698); struct rofi_range_pair (dmenu.c:86,88, script.c:63,66); rofi_force_utf8 (dmenu.c:155,190, filebrowser.c:290,315, recursivebrowser.c:227,258); rofi_icon_fetcher_query/get/get_ex/file_is_image (dmenu.c:731,751, drun.c:1559,1562,1565, filebrowser.c:627-638, recursivebrowser.c:519-531, run.c:586-596, script.c:573,593); rofi_view_* (dmenu.c:235,239,798-799,823-835,871,895-896,1031-1054, drun.c:—, filebrowser.c:416,490, recursivebrowser.c:160,367,425, script.c:245,436-442); rofi_expand_path (dmenu.c:612,643, filebrowser.c:545,702, run.c:293, script.c:693, ssh.c:381,554); rofi_theme_* / rofi_config_find_widget (combi.c:252-253, dmenu.c:578-587, drun.c:1181-1227, filebrowser.c:375-428, recursivebrowser.c:129-172, script.c:187); rofi_set_return_code (dmenu.c:792-796, filebrowser.c:490, recursivebrowser.c:133,425); rofi_output_formatted_line (dmenu.c:809,818,991,1021); rofi_get_completer (drun.c:1436, run.c:526); rofi_collect_modes_search (combi.c:80); RofiHelperExecuteContext (drun.c:405,1394, run.c:119, ssh.c:114); RofiViewState (dmenu.c:762,823,1031)

### Installed public headers (the plugin ABI/API surface in this domain)

- **Current** `#include <rofi/rofi-types.h>, guard INCLUDE_ROFI_TYPES_H`
- **Proposed** either keep the file name rofi-types.h under $includedir/sofi/, or ship sofi-types.h plus a one-line rofi-types.h compatibility shim
- **Locations** meson.build:195-203 installs include/rofi-types.h and include/rofi-icon-fetcher.h (plus mode.h, mode-private.h, helper.h) into $includedir/<project_name>/; include/mode.h:30 and include/helper.h:30 both #include "rofi-types.h"

### ROFI_* / P_* / WL_* enum and type names in the installed header

- **Current** `ROFI_*/Rofi*/rofi_* prefixes`
- **Proposed** keep as-is for this release; renaming to SOFI_*/Sofi* is a source-incompatible change for every out-of-tree plugin and provides no user-visible benefit
- **Locations** include/rofi-types.h: P_INTEGER…P_NUM_TYPES (10-41), ROFI_HL_* (50-77), ROFI_PU_* (82-93), ROFI_DISTANCE_MODIFIER_* (98-111), RofiDistanceUnit/RofiDistance (113-134), ROFI_ORIENTATION_* (139-142), ROFI_CURSOR_* (147-151), ThemeColor (156-165), ROFI_IMAGE_*/ROFI_DIRECTION_*/ROFI_SCALE_* (170-185), RofiImage (187-202), RofiPadding, RofiHighlightColorStyle, WL_* (233-252), PropertyValue/Property (254-294), THEME_MEDIA_TYPE_* (299-318), ThemeWidget (332-344), rofi_range_pair, rofi_int_matcher, thread_state, `extern GThreadPool *tpool`

### rofi_theme_* / rofi_config_* exported symbols used by plugins

- **Current** `rofi_theme_*, rofi_config_find_widget, rofi_theme/rofi_configuration globals`
- **Proposed** unchanged — renaming produces runtime "undefined symbol" failures on plugin load, not compile errors
- **Locations** include/theme.h:38-419 (rofi_theme, rofi_configuration, rofi_theme_get_string/integer/distance/color/image/padding/highlight/list_*, rofi_theme_parse_file/string, rofi_theme_rasi_validate, rofi_theme_set_disp_scale_func, …); definitions in source/theme.c; rofi_configuration defined at source/xrmoptions.c:42. theme.h is NOT in the install_headers list (meson.build:195-203) but the symbols are dynamically exported via gmodule-2.0's -Wl,--export-dynamic and are resolved when a plugin is dlopened

### pkg-config file consumed by plugin builds

- **Current** `rofi.pc, pluginsdir=${libdir}/rofi`
- **Proposed** sofi.pc plus a rofi.pc that Requires: sofi, so existing plugin build systems keep working
- **Locations** meson.build:417-428 (filebase 'rofi', name 'rofi', description 'Header files for rofi plugins', variable pluginsdir=${libdir}/<project_name>); plugin dir also at meson.build:43

### Installed public headers directory (plugin ABI/API root)

- **Current** `$includedir/rofi/{mode.h,mode-private.h,helper.h,rofi-types.h,rofi-icon-fetcher.h}`
- **Proposed** $includedir/sofi/{...}; the file names rofi-types.h / rofi-icon-fetcher.h would also become sofi-types.h / sofi-icon-fetcher.h if the file-name prefix is rebranded
- **Locations** meson.build:195-203 install_headers([...'include/helper.h', 'include/rofi-types.h', 'include/rofi-icon-fetcher.h'...], subdir: meson.project_name())

### pkg-config module used by out-of-tree plugins

- **Current** `rofi.pc, Name: rofi, pluginsdir=$libdir/rofi/, Cflags: -I${includedir}/`
- **Proposed** sofi.pc, Name: sofi, pluginsdir=$libdir/sofi/
- **Locations** meson.build:417-424 (pkg.generate filebase:'rofi', name:'rofi', pluginsdir=$libdir/rofi) and pkgconfig/rofi.pc.in (Name: rofi, Description: 'Header files for rofi plugins', pluginsdir=@libdir@/rofi/)

### Public C symbol prefix in the installed helper.h (every one of these is plugin ABI)

- **Current** `rofi_* / ROFI_*`
- **Proposed** sofi_* / SOFI_* (with rofi_* compatibility aliases if any third-party plugin support is intended)
- **Locations** include/helper.h — rofi_expand_path:196, levenshtein:209, rofi_force_utf8:222, rofi_latin_to_utf8_strdup:232, rofi_scorer_fuzzy_evaluate:268, rofi_scorer_fzf_v2_evaluate:294, utf8_strncmp:310, rofi_output_formatted_line:442, rofi_config_find_widget:486, rofi_theme_find_property:501, rofi_fallthrough macro:522; plus the non-prefixed helper_*/find_arg*/parse_* set. include/rofi-icon-fetcher.h — rofi_icon_fetcher_init:19, _destroy:24, _query:38, _query_advanced:54, _get:64, _get_ex:74, _file_is_image:83

### Public type and enum prefix used across this domain's installed headers

- **Current** `Rofi* / ROFI_* / rofi_*`
- **Proposed** Sofi* / SOFI_* / sofi_*
- **Locations** include/helper.h:331 RofiHelperExecuteContext; include/helper-theme.h:47,62 RofiHighlightColorStyle; source/helper.c:444-494 ROFI_HL_BOLD/UNDERLINE/STRIKETHROUGH/ITALIC/COLOR/UPPERCASE/LOWERCASE/CAPITALIZE (defined include/rofi-types.h:52-76); include/rofi-types.h:350 rofi_range_pair, :358 rofi_int_matcher; include/settings.h:39-44 MM_* enum used at source/helper.c:57-65

### pkg-config module name, installed header directory and plugin directory — the third-party plugin ABI/build contract

- **Current** `pkg-config module `rofi`; ${libdir}/rofi/ plugin dir; headers under ${includedir}/rofi/ ; `pkg-config --variable=pluginsdir rofi``
- **Proposed** pkg-config module `sofi`; ${libdir}/sofi/; headers under ${includedir}/sofi/. Every existing third-party plugin's meson.build/Makefile does `dependency('rofi')` and `#include <rofi/mode.h>` — all of them break at build time and must be ported.
- **Locations** pkgconfig/rofi.pc.in (pluginsdir=@libdir@/rofi/ line 6, Name: rofi line 8, Description line 9); meson.build lines 417-425 (filebase: 'rofi', name: 'rofi', pluginsdir from meson.project_name()); meson.build line 43 plugindir = libdir/<project_name>; headers installed per meson.build lines 195-201 including include/rofi.h, include/rofi-types.h, include/rofi-icon-fetcher.h

### Script-mode environment variables — the documented contract every third-party script mode depends on

- **Current** `ROFI_RETV, ROFI_INFO, ROFI_DATA, ROFI_INPUT`
- **Proposed** Either keep the ROFI_* names verbatim for compatibility with the entire existing ecosystem of rofi script modes, or export BOTH ROFI_* and SOFI_* for a transition period. Renaming to SOFI_* alone silently breaks every third-party script mode ever written — they will read an unset variable rather than error.
- **Locations** doc/rofi-script.5.markdown lines 55 (ROFI_RETV), 65 (ROFI_INFO), 70 (ROFI_DATA), 74 (ROFI_INPUT), 123 (ROFI_DATA in prose), 157 (ROFI_INFO in prose); consumed by Examples/test_script_env.sh lines 12/14/18, Examples/test_script_mode_delim.sh line 10; CONFIG.md has 2 ROFI_ hits

### Project name, PACKAGE_NAME, GETTEXT_PACKAGE and the derived plugin/theme dirs

- **Current** `rofi`
- **Proposed** sofi — a single change at meson.build line 1 cascades to plugindir, themedir, GETTEXT_PACKAGE and the pkgconfig module, which is why those must be reviewed together rather than piecemeal
- **Locations** meson.build line 1 (project('rofi', ...)), lines 152-155, lines 43-44 (plugindir and themedir both derived from meson.project_name()), line 367 (executable('rofi', ...)), lines 420-421 (pkgconfig filebase/name)



---

# Breaks existing user configuration

Changing these silently orphans a user's existing config, themes, history or script modes. Nothing errors; things just stop being found.

### System theme directory (compiled in as THEME_DIR, and the install target for all 33 .rasi/.rasinc files plus iggy.jpg)

- **Current** `${datadir}/rofi/themes`
- **Proposed** ${datadir}/sofi/themes
- **Locations** meson.build:44 themedir = join_paths(get_option('datadir'), meson.project_name(), 'themes'); meson.build:175 THEME_DIR; meson.build:414 install_dir: themedir

### Env vars and the RASI theme extension (not build files, but the same rename decision)

- **Current** `ROFI_PLUGIN_PATH, ROFI_OUTSIDE, .rasi/.rasinc`
- **Proposed** SOFI_PLUGIN_PATH, SOFI_OUTSIDE; keep .rasi/.rasinc (renaming the extension breaks every existing user theme for no gain)
- **Locations** source/rofi.c:705 g_getenv("ROFI_PLUGIN_PATH"), source/rofi.c:991 g_getenv("ROFI_OUTSIDE"); .rasi/.rasinc extensions across the 34 files installed at meson.build:379-414

### wlr-layer-shell namespace string (the only compositor-visible identity in the Wayland backend; the WM_CLASS/app_id equivalent)

- **Current** `rofi`
- **Proposed** sofi
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:1791-1792 — zwlr_layer_shell_v1_get_layer_surface(wayland->layer_shell, wayland->surface, wlo, layer, "rofi")

### Wayland layer-surface namespace string

- **Current** `"rofi"`
- **Proposed** "sofi"
- **Locations** source/wayland/display.c:1791-1792 — zwlr_layer_shell_v1_get_layer_surface(..., "rofi")

### X11 WM_CLASS (instance + class name)

- **Current** `instance "rofi", class "Rofi"`
- **Proposed** instance "sofi", class "Sofi"
- **Locations** source/xcb/view.c:773-775 — `const char wm_class_name[] = "rofi\0Rofi"; xcb_icccm_set_wm_class(...)`

### Plugin search path env var

- **Current** `ROFI_PLUGIN_PATH`
- **Proposed** SOFI_PLUGIN_PATH (accept ROFI_PLUGIN_PATH as a deprecated fallback for one release)
- **Locations** source/rofi.c:704-712 (comment + g_getenv)

### Screenshot output override env var

- **Current** `ROFI_PNG_OUTPUT`
- **Proposed** SOFI_PNG_OUTPUT
- **Locations** source/view.c:138 and the warning text at source/view.c:141

### Per-user config directory and file

- **Current** `$XDG_CONFIG_HOME/rofi/config.rasi`
- **Proposed** $XDG_CONFIG_HOME/sofi/config.rasi, with a one-release fallback read of the rofi path
- **Locations** source/rofi.c:1056-1060 (g_build_filename(cpath, "rofi", "config.rasi"))

### System-wide config file

- **Current** `rofi.rasi`
- **Proposed** sofi.rasi
- **Locations** source/rofi.c:1120 (XDG system config dirs) and source/rofi.c:1132 (SYSCONFDIR)

### X11 WM_CLASS

- **Current** `rofi / Rofi`
- **Proposed** sofi / Sofi
- **Locations** source/xcb/view.c:773-775 (const char wm_class_name[] = "rofi\0Rofi")

### Wayland layer-surface namespace

- **Current** `rofi`
- **Proposed** sofi
- **Locations** source/wayland/display.c:1792 (zwlr_layer_shell_v1_get_layer_surface namespace argument)

### Shipped helper scripts (also the compiled-in terminal default)

- **Current** `rofi-sensible-terminal, rofi-theme-selector`
- **Proposed** sofi-sensible-terminal, sofi-theme-selector
- **Locations** script/rofi-sensible-terminal, script/rofi-theme-selector, installed at meson.build:204-208; referenced as the default at config/config.c:65

### Theme/config file extension

- **Current** `.rasi / .rasinc`
- **Proposed** Recommend KEEPING .rasi — it is the format name, not the product name, and every third-party theme in the wild uses it. Renaming it invalidates all existing community themes for zero benefit.
- **Locations** lexer/theme-lexer.l:56 (const char *rasi_theme_file_extensions[] = {".rasi", ".rasinc", NULL}), consumed at source/rofi.c:1143-1145

### RASI theme element/property vocabulary referenced by the widgets - MUST NOT be renamed

- **Current** `user-facing RASI vocabulary; contains no "rofi" substring anywhere`
- **Proposed** unchanged
- **Locations** source/widgets/listview.c:174,177,183,188,193,200,216,225,228 (element-icon, element-text, element-index, textbox, button, icon, children, element); listview.c:818-838 (spacing, columns, lines, fixed-height, dynamic, reverse, flow, cycle, fixed-columns, require-input, layout, scrollbar); textbox.c:128,233,240,242,245,247,255,268,274,276,530,557,578,590,593,595,597,618,621,627,628,1126 (font, markup, str, content, placeholder, placeholder-markup, password-mask, blink, cursor-*, text-*, tab-stops, width); widget.c:46-60,245,247,267 (padding, border, border-radius, margin, cursor, enabled, border-aa, border-disable-nvidia-workaround, background-color, background-image, border-color); scrollbar.c:109,177,179; icon.c:116,119,173,176,178,182,184; state names at textbox.c:298-305 and listview.c:141-148

### User script mode directory

- **Current** `$XDG_CONFIG_HOME/rofi/scripts/`
- **Proposed** $XDG_CONFIG_HOME/sofi/scripts/ with a fallback read of the rofi path when the sofi one is absent
- **Locations** source/modes/script.c:630 and :640 (g_build_filename(cpath, "rofi", "scripts", ...)), documented at include/modes/script.h:59

### drun history / MRU cache filename

- **Current** `rofi3.druncache`
- **Proposed** sofi3.druncache (or sofi1.druncache to restart the format counter)
- **Locations** source/modes/drun.c:66 (DRUN_CACHE_FILE), used at :342, :471, :875, :883

### run history cache filename

- **Current** `rofi-4.runcache`
- **Proposed** sofi-4.runcache
- **Locations** source/modes/run.c:64 (RUN_CACHE_FILE), used at :118, :148, :255

### ssh history cache filename

- **Current** `rofi-2.sshcache`
- **Proposed** sofi-2.sshcache
- **Locations** source/modes/ssh.c:83 (SSH_CACHE_FILE), used at :138, :159, :502

### RASI file extension list (the .rasi / .rasinc suffixes themselves)

- **Current** `.rasi, .rasinc`
- **Proposed** keep ".rasi", ".rasinc" and, if a sofi-native suffix is wanted, APPEND it: {".rasi", ".rasinc", ".sasi", ".sasinc", NULL}
- **Locations** lexer/theme-lexer.l:56 (`const char *rasi_theme_file_extensions[] = {".rasi", ".rasinc", NULL};`), consumed at lexer/theme-lexer.l:437 (@import/@theme) and :918 (top-level theme file), source/rofi.c:1143-1145 (config file), matching logic in source/helper.c:1542-1571

### Per-user config directory and main config file

- **Current** `$XDG_CONFIG_HOME/rofi/config.rasi`
- **Proposed** $XDG_CONFIG_HOME/sofi/config.rasi, with a read-only fallback to the rofi path when the sofi one is absent
- **Locations** source/rofi.c:1059 (`g_build_filename(cpath, "rofi", "config.rasi")`), script/rofi-theme-selector:186,193,195

### System-wide config file name

- **Current** `<sysconfdir>/rofi.rasi, <xdg config dir>/rofi.rasi`
- **Proposed** sofi.rasi in the same locations; keep probing rofi.rasi as a fallback for one release
- **Locations** source/rofi.c:1120 (`g_build_filename(dirs[i], "rofi.rasi")` over $XDG_CONFIG_DIRS) and source/rofi.c:1132 ($SYSCONFDIR/rofi.rasi)

### @import / @theme search path directories

- **Current** `…/rofi/themes and …/rofi`
- **Proposed** …/sofi/themes and …/sofi, appended to (not replacing) the rofi entries so existing user theme collections keep resolving
- **Locations** source/helper.c:1474 ($XDG_CONFIG_HOME/rofi/themes), :1483 ($XDG_CONFIG_HOME/rofi), :1493 ($XDG_DATA_HOME/rofi/themes), :1509 ($XDG_DATA_DIRS/*/rofi/themes), :1520 (THEME_DIR). Full order: absolute path -> dir of the importing file -> those five -> give up (helper.c:1445-1528)

### Shipped theme files

- **Current** `themes/*.rasi, themes/gruvbox-common.rasinc`
- **Proposed** unchanged filenames (renaming them breaks every `@theme "solarized"` in user configs and the theme-selector list)
- **Locations** meson.build:378-415 (33 install_data entries) and the themes/ directory; themes/iggy.jpg is a data asset referenced by themes/iggy.rasi

### User theme/config search directories (the single highest-impact on-disk path in this domain)

- **Current** `$XDG_CONFIG_HOME/rofi/themes/, $XDG_CONFIG_HOME/rofi/, $XDG_DATA_HOME/rofi/themes/, $XDG_DATA_DIRS/*/rofi/themes/`
- **Proposed** $XDG_CONFIG_HOME/sofi/themes/, $XDG_CONFIG_HOME/sofi/, $XDG_DATA_HOME/sofi/themes/, $XDG_DATA_DIRS/*/sofi/themes/
- **Locations** source/helper.c:1474 (g_build_filename(cpath,"rofi","themes",...)), :1483 (g_build_filename(cpath,"rofi",...)), :1493 (g_build_filename(datadir,"rofi","themes",...)), :1509 (g_build_filename(sdatadir,"rofi","themes",...))

### History cache file names (the format is read/written by source/history.c; the name constants live in the modes)

- **Current** `$XDG_CACHE_HOME/{rofi3.druncache, rofi-drun-desktop.cache, rofi-4.runcache, rofi-2.sshcache, rofi3.filebrowsercache}`
- **Proposed** sofi3.druncache, sofi-drun-desktop.cache, sofi-4.runcache, sofi-2.sshcache, sofi3.filebrowsercache
- **Locations** source/modes/drun.c:66 DRUN_CACHE_FILE, :69 DRUN_DESKTOP_CACHE_FILE, source/modes/run.c:64 RUN_CACHE_FILE, source/modes/ssh.c:83 SSH_CACHE_FILE, source/modes/filebrowser.c:53 FILEBROWSER_CACHE_FILE — all joined with cache_dir and passed to history_set/history_remove/history_get_list

### On-disk config, theme and data paths

- **Current** `${XDG_CONFIG_HOME}/rofi/config.rasi, ${XDG_CONFIG_HOME}/rofi/themes/, ${XDG_DATA_HOME}/rofi/themes/, ${datadir}/rofi/themes/, /etc/rofi.rasi`
- **Proposed** ${XDG_CONFIG_HOME}/sofi/config.rasi and the parallel sofi/ dirs. This silently orphans every existing user's configuration and custom themes — decide explicitly whether to fall back to reading the rofi/ paths when the sofi/ ones are absent, and document it.
- **Locations** doc/rofi-theme.5.markdown lines 12, 1671-1674, 1690; CONFIG.md lines 8, 11, 15, 32, 59, 183, 193-194; README.md lines 263-269; script/rofi-theme-selector lines 88 (TD=${p}/rofi/themes) and 186 (CDIR=.../rofi); Examples/rofi-file-browser.sh lines 5-6 (~/.local/share/rofi/rofi_fb_*); doc/rofi-theme-selector.1.markdown lines 24-26; doc/rofi.1.markdown lines 69-70 (/etc/rofi.rasi, XDG_CONFIG_DIRS + SYSCONFDIR); meson.build line 44 themedir

### RASI file extensions (.rasi / .rasinc) — the theme-format identity

- **Current** `.rasi and .rasinc, resolved in that order (doc/rofi-theme.5.markdown lines 1676-1677)`
- **Proposed** KEEP unchanged. The extension is not the product name — it is the format name, it appears in tens of thousands of user config files and third-party theme repos, and renaming it buys nothing while breaking every theme in the wild.
- **Locations** doc/rofi-theme.5.markdown lines 239-241 and 1676-1677; themes/*.rasi (32 files) and themes/gruvbox-common.rasinc; doc/default_theme.rasi, doc/default_configuration.rasi; script/rofi-theme-selector lines 40, 92, 98; .github/workflows/build.yml paths-ignore entries at lines 10 and 15

### Installed helper scripts (user-facing binaries in $PATH) and the compiled-in default terminal

- **Current** `rofi-sensible-terminal, rofi-theme-selector`
- **Proposed** sofi-sensible-terminal, sofi-theme-selector. config/config.c line 65's default must change in the same commit or `sofi -show run` cannot open a terminal. Users who set `terminal: "rofi-sensible-terminal";` in their config keep working only if the old name is also shipped as a compatibility symlink.
- **Locations** script/rofi-sensible-terminal, script/rofi-theme-selector; installed by meson.build lines 204-207 into bindir; referenced by config/config.c line 65 (.terminal_emulator = "rofi-sensible-terminal"), data/rofi-theme-selector.desktop line 6, mkdocs/docs/themes/capture.sh line 41, doc/rofi-sensible-terminal.1.markdown line 9



---

# Breaks distribution packaging

Changing these requires coordination with packagers: file names, desktop/icon ids, manpage names, AppArmor paths.

### Desktop entry files and their Exec/Name/Icon fields

- **Current** `rofi.desktop / rofi-theme-selector.desktop, Icon=rofi`
- **Proposed** sofi.desktop / sofi-theme-selector.desktop, Icon=sofi
- **Locations** meson.build:211-212 install_data('data/rofi-theme-selector.desktop', 'data/rofi.desktop'); file bodies: data/rofi.desktop (Exec=rofi -show, Name=Rofi, Icon=rofi) and data/rofi-theme-selector.desktop (Exec=rofi-theme-selector, Name=Rofi Theme Selector, Icon=rofi)

### Installed scalable icon basename (must match the desktop files' Icon= key)

- **Current** `rofi.svg`
- **Proposed** sofi.svg
- **Locations** meson.build:216 'data/rofi.svg', installed to icondir defined at meson.build:46; the sibling data/rofi.png is not installed

### pkg-config module and plugin directory variable

- **Current** `rofi.pc, pluginsdir=${libdir}/rofi`
- **Proposed** sofi.pc, pluginsdir=${libdir}/sofi
- **Locations** meson.build:417-426 — pkg.generate(filebase: 'rofi', name: 'rofi', description: 'Header files for rofi plugins', variables: ['pluginsdir=${libdir}/rofi'])

### Project / executable / package name

- **Current** `rofi`
- **Proposed** sofi
- **Locations** meson.build:1 project('rofi'), meson.build:367 executable('rofi', ...); plugindir/themedir derive from meson.project_name() at meson.build:42-43

### pkg-config module and its pluginsdir variable

- **Current** `rofi.pc, pluginsdir=${libdir}/rofi/`
- **Proposed** sofi.pc, pluginsdir=${libdir}/sofi/
- **Locations** pkgconfig/rofi.pc.in (filename, Name: rofi, Description, pluginsdir=@libdir@/rofi/), meson.build:417-425 (filebase/name 'rofi', pluginsdir from meson.project_name())

### Desktop entries and icon

- **Current** `rofi.desktop, Icon=rofi, Name=Rofi`
- **Proposed** sofi.desktop, Icon=sofi, Name=Sofi — the icon basename must match the Icon= key and the new StartupWMClass must match the WM_CLASS change
- **Locations** data/rofi.desktop (Exec=rofi, Name=Rofi, Icon=rofi), data/rofi-theme-selector.desktop, data/rofi.svg, data/rofi.png; installed at meson.build:211-218

### Extension-less @import resolution inside shipped themes

- **Current** `@import "gruvbox-common" -> themes/gruvbox-common.rasinc`
- **Proposed** unchanged (do not drop .rasinc from the array, or rename the include file and all six importers together)
- **Locations** themes/gruvbox-dark.rasi:61, gruvbox-light.rasi:61, gruvbox-dark-soft.rasi:61, gruvbox-light-soft.rasi:61, gruvbox-dark-hard.rasi:61, gruvbox-light-hard.rasi:61 — all `@import "gruvbox-common"`, resolved only because ".rasinc" is in the extension array

### Installed theme directory macro

- **Current** `$prefix/share/rofi/themes (derived from meson.project_name())`
- **Proposed** $prefix/share/sofi/themes — follows automatically from renaming the meson project, which is why the rename must be done together with the fallback paths above
- **Locations** meson.build:44 (`themedir = datadir/<project_name>/themes`), meson.build:175 (`header_conf.set_quoted('THEME_DIR', ...)`), source/helper.c:1520

### Compiled-in system theme directory consumed by helper_get_theme_path_check_file()

- **Current** `$prefix/share/rofi/themes`
- **Proposed** $prefix/share/sofi/themes
- **Locations** meson.build:175 (header_conf.set_quoted('THEME_DIR', join_paths(prefix, themedir))) and meson.build:44 (themedir = datadir/meson.project_name()/themes); consumed at source/helper.c:1520

### {terminal} default expanded by helper_parse_setup()

- **Current** `rofi-sensible-terminal (and rofi-theme-selector), installed into $bindir`
- **Proposed** sofi-sensible-terminal / sofi-theme-selector
- **Locations** source/helper.c:107 (g_hash_table_insert(h, "{terminal}", config.terminal_emulator)) backed by config/config.c:65 .terminal_emulator = "rofi-sensible-terminal"; the scripts are installed at meson.build:205-208 ('script/rofi-sensible-terminal', 'script/rofi-theme-selector')

### Manpage names and section numbers (installed contract; other packages and docs reference these by name)

- **Current** `rofi(1), rofi-theme(5), rofi-script(5), rofi-dmenu(5), rofi-keys(5), rofi-debugging(5), rofi-actions(5), rofi-thumbnails(5), rofi-sensible-terminal(1), rofi-theme-selector(1)`
- **Proposed** sofi(1), sofi-theme(5), sofi-script(5), sofi-dmenu(5), sofi-keys(5), sofi-debugging(5), sofi-actions(5), sofi-thumbnails(5), sofi-sensible-terminal(1), sofi-theme-selector(1)
- **Locations** doc/rofi.1.markdown, doc/rofi-theme.5.markdown, doc/rofi-script.5.markdown, doc/rofi-dmenu.5.markdown, doc/rofi-keys.5.markdown, doc/rofi-debugging.5.markdown, doc/rofi-actions.5.markdown, doc/rofi-thumbnails.5.markdown, doc/rofi-sensible-terminal.1.markdown, doc/rofi-theme-selector.1.markdown; enumerated in doc/meson.build lines 1-12; H1 headers at line 1 and NAME at lines 3-5 of each file; cross-references in doc/rofi.1.markdown lines 1262-1264 (SEE ALSO), doc/rofi-theme-selector.1.markdown line 35, doc/rofi-sensible-terminal.1.markdown line 44, INSTALL.md line 210, README.md lines 205-212

### AppArmor profile path documented for thumbnail support

- **Current** `/usr/bin/rofi`
- **Proposed** /usr/bin/sofi — distro AppArmor profiles are keyed on the binary path, so this must be coordinated with packagers or thumbnails silently fail on Ubuntu/Debian
- **Locations** doc/rofi-thumbnails.5.markdown lines 62 (/usr/bin/rofi {) and 71 (/usr/bin/rofi mr,), inside the AppArmor section beginning at line 47

### sourcehut project URL

- **Current** `https://sr.ht/~qball/rofi/`
- **Proposed** https://github.com/orpheus497/sofi.git (or a sofi sr.ht project if one is created). See the build finding: as written the sourcehut CI clones and tests upstream rofi.
- **Locations** .build.yml line 32



---

# User-visible but safe

Cosmetic or documentation-level identity. Change freely; users notice but nothing breaks.

### Installed executable name

- **Current** `rofi`
- **Proposed** sofi
- **Locations** meson.build:367 rofi = executable('rofi', rofi_sources + [

### Upstream bug/support URLs compiled into the binary (shown by -help / -v)

- **Current** `https://github.com/davatorium/rofi/ and https://github.com/davatorium/rofi/discussions`
- **Proposed** https://github.com/orpheus497/sofi/ and .../discussions
- **Locations** meson.build:156 PACKAGE_BUGREPORT, meson.build:157 PACKAGE_URL

### Helper scripts installed into bindir (also referenced as the default terminal at config/config.c:65 .terminal_emulator = "rofi-sensible-terminal")

- **Current** `rofi-sensible-terminal, rofi-theme-selector`
- **Proposed** sofi-sensible-terminal, sofi-theme-selector
- **Locations** meson.build:205 'script/rofi-sensible-terminal', meson.build:206 'script/rofi-theme-selector'

### Man page names (all ten), their .markdown sources in doc/, and the install_man list

- **Current** `rofi.1, rofi-*.5`
- **Proposed** sofi.1, sofi-*.5
- **Locations** doc/meson.build:2-11 man_files = ['rofi.1','rofi-sensible-terminal.1','rofi-theme-selector.1','rofi-actions.5','rofi-debugging.5','rofi-dmenu.5','rofi-keys.5','rofi-script.5','rofi-theme.5','rofi-thumbnails.5']; matching doc/*.markdown sources

### Installation and README identity: distro package names, clone URL, badges, gdb example

- **Current** `rofi / davatorium / DaveDavenport`
- **Proposed** sofi / orpheus497
- **Locations** INSTALL.md:114 git clone --recursive https://github.com/DaveDavenport/rofi, :204 gdb build/rofi core, :218/:224/:230/:238/:249/:257/:262/:270 (apt/dnf/pacman/emerge/zypper/pkg/port install rofi), :273 master-install link; README.md:2,4,5,6 shields.io/repology badges, :11-22 version links, :155, :162

### User-facing fatal error message naming the project

- **Current** `Rofi on wayland requires support for the layer shell protocol`
- **Proposed** Sofi on Wayland requires compositor support for the wlr-layer-shell protocol
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:1756 — g_error("Rofi on wayland requires support for the layer shell protocol")

### Executable name and meson project name

- **Current** `rofi`
- **Proposed** sofi
- **Locations** meson.build:1 project('rofi', ...); meson.build:367 executable('rofi', ...); meson.build:206 install_data('script/rofi-sensible-terminal')

### User-visible runtime strings mentioning the product by name

- **Current** `"Rofi" / "rofi"`
- **Proposed** "Sofi" / "sofi"
- **Locations** source/wayland/display.c:1756 g_error("Rofi on wayland requires support for the layer shell protocol"); include/xcb-dummy.h:5 comment "so that rofi can be built without xcb headers present"; include/display.h:147 doc comment "the monitor rofi should show on"

### Window title (WM_NAME + _NET_WM_NAME)

- **Current** `"rofi"`
- **Proposed** "sofi"
- **Locations** source/xcb/view.c:772 `xcb_rofi_view_set_window_title("rofi")`; the property write itself is source/xcb/view.c:963-971. Related non-domain callers that pass the same default: source/view.c:1860 and source/view.c:2125.

### Startup-notification launcher application name

- **Current** `"rofi"`
- **Proposed** "sofi"
- **Locations** source/xcb/display.c:757 `sn_launcher_context_initiate(sncontext, "rofi", context->command, xcb->last_timestamp)`

### Pid / lock file name

- **Current** `.rofi.pid / rofi.pid`
- **Proposed** .sofi.pid / sofi.pid
- **Locations** source/rofi.c:1024 (~/.rofi.pid fallback) and source/rofi.c:1026 ($XDG_RUNTIME_DIR/rofi.pid); overridable via the "pid" option registered at source/rofi.c:1029

### Entry-history cache file

- **Current** `rofi-entry-history.txt`
- **Proposed** sofi-entry-history.txt
- **Locations** source/view.c:513 (read) and source/view.c:563 (write), both under cache_dir from source/rofi.c:1185

### Screenshot filename prefix

- **Current** `rofi-YYYY-MM-DD-HHMM-NNNNN.png`
- **Proposed** sofi-YYYY-MM-DD-HHMM-NNNNN.png
- **Locations** source/view.c:148 (g_date_time_format "rofi-%Y-%m-%d-%H%M")

### Window title

- **Current** `"rofi" / "rofi - <mode>"`
- **Proposed** "sofi" / "sofi - <mode>"
- **Locations** source/view.c:1856, 1860 (rofi_view_create) and source/view.c:2121, 2125 (rofi_view_switch_mode); also source/xcb/view.c:772

### Manpages

- **Current** `rofi(1), rofi-*(5)`
- **Proposed** sofi(1), sofi-*(5); source/rofi.c:465 prints "man rofi" and must be updated in lockstep
- **Locations** doc/rofi.1.markdown, doc/rofi-actions.5.markdown, doc/rofi-debugging.5.markdown, doc/rofi-dmenu.5.markdown, doc/rofi-keys.5.markdown, doc/rofi-script.5.markdown, doc/rofi-theme.5.markdown, doc/rofi-thumbnails.5.markdown, doc/rofi-sensible-terminal.1.markdown, doc/rofi-theme-selector.1.markdown, doc/rofi.doxy.in

### User-facing help, usage and error text

- **Current** `rofi / Rofi in prose`
- **Proposed** sofi / Sofi
- **Locations** source/rofi.c:346 ("rofi plugins"), :465 ("man rofi"), :478 ("#rofi @ libera.chat"), :525 ("Rofi is unsure what to show"), :528 ("<b>rofi</b> -show"), :800 ("when starting rofi"), :995 ("Do not launch rofi from inside rofi"); source/view.c:222 ("when starting rofi"); source/keyb.c:199 ("the rofi window"), :215 ("Quit rofi"), :472 ("rofi -list-keybindings"); source/helper.c:606 ("Rofi already running?")

### Bug-report / support URLs baked into --help and the error template

- **Current** `davatorium/rofi GitHub, #rofi @ libera.chat`
- **Proposed** https://github.com/orpheus497/sofi and whatever support channel replaces IRC — this is the one surface that is actively wrong today, since it directs sofi bug reports to upstream rofi
- **Locations** source/rofi.c:474-479 (PACKAGE_BUGREPORT, PACKAGE_URL, IRC channel); include/rofi.h:134-142 ERROR_MSG macro, which hardcodes https://github.com/davatorium/rofi/

### Log domains

- **Current** `"Rofi"`
- **Proposed** "Sofi" — affects G_MESSAGES_DEBUG filters users have configured
- **Locations** source/rofi.c:30 (#define G_LOG_DOMAIN "Rofi"), source/view.c:29 ("View"), source/timings.c:29 ("Timings")

### drun desktop-entry quick-load cache filename

- **Current** `rofi-drun-desktop.cache`
- **Proposed** sofi-drun-desktop.cache
- **Locations** source/modes/drun.c:69 (DRUN_DESKTOP_CACHE_FILE), used at :1178

### filebrowser last-directory cache filename

- **Current** `rofi3.filebrowsercache`
- **Proposed** sofi3.filebrowsercache
- **Locations** source/modes/filebrowser.c:53 (FILEBROWSER_CACHE_FILE), used at :438, :521

### Startup-notification description string shown by the desktop environment when connecting over ssh

- **Current** `Connecting to '%s' via rofi`
- **Proposed** Connecting to '%s' via sofi — note the length at line 109 is computed from the literal, so both lines must change together
- **Locations** source/modes/ssh.c:109 (length calculation) and :112 (g_snprintf format)

### CLI options that embed the rasi/theme identity

- **Current** `-rasi-validate; help text says "rasi format"`
- **Proposed** keep -rasi-validate as an alias if the format keeps the .rasi extension; otherwise add the new name and alias the old one
- **Locations** source/rofi.c:978-986 (-rasi-validate, usage text "Usage: %s -rasi-validate my-theme.rasi"), source/rofi.c:349 and :352 (-dump-config / -dump-theme help text "in rasi format"), source/theme.h:371 and source/rofi.c:1350 (rofi_theme_rasi_validate), include/xrmoptions.h:151 and source/xrmoptions.c:1053 (config_parse_dump_config_rasi_format), source/xrmoptions.c:770 (config_parser_form_rasi_format)

### -dump-theme output banner (also baked into the shipped default theme)

- **Current** `"rofi -dump-theme output. / Rofi version: X"`
- **Proposed** "sofi -dump-theme output. / Sofi version: X" — and regenerate doc/default_theme.rasi so the built-in theme does not advertise rofi
- **Locations** source/theme.c:597 (`printf("/**\n * rofi -dump-theme output.\n * Rofi version: %s\n **/\n", PACKAGE_VERSION)`), and the pre-generated doc/default_theme.rasi:1-3 which still carries "rofi -dump-theme output."

### rofi-theme-selector helper script, its desktop entry and manpage

- **Current** `rofi-theme-selector (binary name, desktop id, manpage)`
- **Proposed** sofi-theme-selector everywhere, including the internal `command -v rofi` lookup, the themes glob directory and the config.rasi path
- **Locations** script/rofi-theme-selector (:16 `ROFI=$(command -v rofi)`, :36 error text, :40 `${MKTEMP}).rasi`, :88 `${p}/rofi/themes`, :92 `${TD}/*.rasi`, :118 `-dump-config`, :186 `${XDG_CONFIG_HOME}/rofi`, :193/:195 config.rasi, :203-204 rewrites `@theme`); installed at meson.build:204-208; data/rofi-theme-selector.desktop installed at meson.build:210-214; doc/rofi-theme-selector.1.markdown listed in doc/meson.build:3

### Theme documentation (manpages) referenced from code

- **Current** `rofi-theme.5, rofi-theme-selector.1`
- **Proposed** sofi-theme.5, sofi-theme-selector.1 — and update the two option comments in xrmoptions.c that name the manpage
- **Locations** doc/rofi-theme.5.markdown (8 occurrences of "rasi"), doc/meson.build:1-11 man_files list (rofi.1, rofi-theme.5, rofi-theme-selector.1, rofi-keys.5, …); referenced in user-visible strings at source/xrmoptions.c:119 and :129 ("*DEPRECATED* see rofi-theme manpage")

### User-visible startup-notification description string

- **Current** `"Launching '<cmd>' via rofi"`
- **Proposed** "Launching '<cmd>' via sofi"
- **Locations** source/helper.c:1431 (gsize l = strlen("Launching '' via rofi") + ...) and :1434 (g_snprintf(description, l, "Launching '%s' via rofi", cmd))

### User-visible pidfile lock warning

- **Current** `"Rofi already running?"`
- **Proposed** "Sofi already running?"
- **Locations** source/helper.c:606 g_warning("Failed to set lock on pidfile: Rofi already running?")

### Symlinks from the docs site into doc/ (mkdocs 'current' version)

- **Current** `mkdocs/docs/current/rofi-*.markdown -> ../../../doc/rofi-*.markdown`
- **Proposed** mkdocs/docs/current/sofi-*.markdown -> ../../../doc/sofi-*.markdown
- **Locations** mkdocs/docs/current/ — 8 symlinks: rofi.1.markdown, rofi-theme.5.markdown, rofi-dmenu.5.markdown, rofi-script.5.markdown, rofi-debugging.5.markdown, rofi-keys.5.markdown, rofi-thumbnails.5.markdown, rofi-actions.5.markdown, each pointing at ../../../doc/<same name>; referenced by mkdocs/mkdocs.yml lines 19-26 and mkdocs/docs/index.md lines 16-23

### Desktop entry: filename, Name, Exec, Icon (and the absent StartupWMClass)

- **Current** `data/rofi.desktop, data/rofi-theme-selector.desktop; Exec=rofi -show; Name=Rofi; Icon=rofi`
- **Proposed** data/sofi.desktop, data/sofi-theme-selector.desktop; Exec=sofi -show; Name=Sofi; Icon=sofi. Note neither file currently sets StartupWMClass — if the X11 WM_CLASS changes with the rebrand, add StartupWMClass=Sofi so window-matching keeps working.
- **Locations** data/rofi.desktop lines 6-8 (Exec=rofi -show, Name=Rofi, Icon=rofi); data/rofi-theme-selector.desktop lines 6-8 (Exec=rofi-theme-selector, Name=Rofi Theme Selector, Icon=rofi); installed by meson.build lines 210-214 into datadir/applications

### Icon name and icon file (freedesktop icon-theme lookup key referenced by both desktop files)

- **Current** `rofi.svg / rofi.png installed as icon name "rofi"`
- **Proposed** sofi.svg / sofi.png installed as icon name "sofi"; also scrub the upstream author's local path from data/rofi.svg line 18
- **Locations** data/rofi.svg (sodipodi:docname="rofi.svg" at line 21, inkscape:export-filename="/home/qball/Desktop/rofi-large.png" at line 18); data/rofi.png; installed by meson.build lines 215-218 into datadir/icons/hicolor/scalable/apps

### Wayland layer-surface namespace string (documented compositor-rule contract)

- **Current** `layer namespace string `rofi`, used in the documented Hyprland `layerrule` examples`
- **Proposed** `sofi` — and the doc examples on lines 1197 and 1205 must change with it. Any user who already wrote a `layerrule` matching `rofi` will find their animation/blur rules stop matching after the rename.
- **Locations** doc/rofi.1.markdown lines 1197 (layerrule = noanim,^(rofi)$) and 1205 (match:namespace = rofi), within the Hyprland section at lines 1193-1211; the `-wayland-layer` option is documented at lines 183-186

### Internal invocations of the `rofi` binary by name inside shipped scripts

- **Current** `hardcoded command name `rofi``
- **Proposed** `sofi`. Note script/rofi-theme-selector line 34-38 already exits gracefully when the binary is missing, but script/rofi-sensible-terminal line 18 does not — after the rename it would call a nonexistent `rofi` with no fallback.
- **Locations** script/rofi-sensible-terminal line 18 (rofi -e "Failed to find a suitable terminal"); script/rofi-theme-selector line 16 (ROFI=$(command -v rofi)) and line 36 (error text); mkdocs/docs/themes/capture.sh line 4 (ROFI_BIN=../../../build/rofi)

### PACKAGE_BUGREPORT / PACKAGE_URL compiled into the binary and shown in -help/-info output

- **Current** `PACKAGE_BUGREPORT = https://github.com/davatorium/rofi/ ; PACKAGE_URL = https://github.com/davatorium/rofi/discussions`
- **Proposed** PACKAGE_BUGREPORT = https://github.com/orpheus497/sofi/issues ; PACKAGE_URL = https://github.com/orpheus497/sofi/discussions. Until changed, every sofi user filing a bug is directed by the binary itself into upstream rofi's tracker.
- **Locations** meson.build lines 156-157

### Upstream repository URLs (GitHub davatorium and DaveDavenport orgs)

- **Current** `https://github.com/davatorium/rofi/... and the older https://github.com/DaveDavenport/rofi/... (both orgs), plus raw.githubusercontent.com/davatorium/rofi/next/... image URLs`
- **Proposed** https://github.com/orpheus497/sofi/... . Two caveats: (1) README.md lines 11-22 link to upstream release tags 1.7.0-2.0.0 — `git tag | wc -l` is 0 in this fork, so these must either point at upstream (as historical references) or be deleted, not naively rewritten to nonexistent sofi tags; (2) README.md lines 288/292/296 embed raw.githubusercontent images from upstream's releasenotes — rewriting the org without the files existing yields broken images.
- **Locations** README.md lines 2-6, 11-22, 200-201, 272, 278, 282, 288, 292, 296, 303, 307-314, 318, 321, 326; INSTALL.md lines 114, 273; doc/rofi.1.markdown lines 1240, 1246, 1255-1257; .github/CONTRIBUTING.md lines 13, 16, 17, 23; .github/ISSUE_TEMPLATE/bug_report.yml lines 10, 25, 29; .github/ISSUE_TEMPLATE/documentation_report.yml lines 11, 24, 28, 41, 67; .github/ISSUE_TEMPLATE/feature_request.yml lines 10, 14; .github/ISSUE_TEMPLATE/config.yml line 4; .github/actions/setup/action.yml line 48; .github/workflows/main.yml line 13; mkdocs/mkdocs.yml lines 2, 7, 8; mkdocs/docs/index.md line 12; mkdocs/docs/themes/capture.sh line 26; mkdocs/docs/downloads.md (30 rofi hits, all upstream release URLs); meson.build lines 156-157

### Wiki and Discussions links (upstream-hosted, not transferable with a fork)

- **Current** `github.com/davatorium/rofi/wiki/* and /discussions`
- **Proposed** A fork does NOT inherit the upstream wiki or discussions. Either recreate the content under orpheus497/sofi or keep the links explicitly labelled as upstream references. README.md line 300 already flags the wiki as unmaintained, which argues for deleting the section outright.
- **Locations** README.md lines 300-314 (whole Wiki section) and 316-322 (Discussion places); .github/CONTRIBUTING.md lines 13, 16-17, 23; .github/ISSUE_TEMPLATE/documentation_report.yml lines 28, 67; mkdocs/docs/index.md line 12; doc/rofi.1.markdown lines 1246, 1256

### mkdocs site identity

- **Current** `site_name: Rofi Documentation; repo_url: https://github.com/davatorium/rofi/; logo image files named rofi-*`
- **Proposed** site_name: Sofi Documentation; repo_url: https://github.com/orpheus497/sofi/; rename/replace the two logo PNGs (they contain the rofi wordmark as pixels, so they need redrawing, not renaming)
- **Locations** mkdocs/mkdocs.yml line 1 (site_name) and line 2 (repo_url); mkdocs/docs/index.md lines 1, 3, 5, 7; mkdocs/docs/images/rofi-logo-full.png and mkdocs/docs/images/rofi.png

### Pandoc man filter hardcoded product name in the generated manpage header line

- **Current** `every generated manpage's header reads `<NAME>(n) rofi | General Commands Manual``
- **Proposed** pandoc.Str("sofi") — easy to miss because it is the only 'rofi' string in the entire 237-line Lua file and it is not near any other identity string
- **Locations** doc/man_filter.lua line 174 (pandoc.Str("rofi") inserted into the title, between the page name and the section description)



---

# Internal only

No external contract. Mechanical rename.

### Dead pkg-config template (unreferenced by the build, but ships in the tarball)

- **Current** `rofi`
- **Proposed** delete the file
- **Locations** pkgconfig/rofi.pc.in:6 pluginsdir=@libdir@/rofi/, :8 Name: rofi, :9 Description: Header files for rofi plugins

### Doxygen configuration template and generated file

- **Current** `rofi.doxy.in / rofi.doxy`
- **Proposed** sofi.doxy.in / sofi.doxy
- **Locations** doc/meson.build:61 input: 'rofi.doxy.in', doc/meson.build:62 output: 'rofi.doxy',; the 127KB template doc/rofi.doxy.in (PROJECT_NAME=@PACKAGE@ at line 45 is already parameterised, so only the filename needs renaming)

### GResource namespace prefix for the built-in default theme and configuration

- **Current** `/org/qtools/rofi`
- **Proposed** /org/orpheus497/sofi (or another reverse-DNS namespace for the fork)
- **Locations** resources/resources.xml:3 <gresource prefix="/org/qtools/rofi">; the matching runtime lookup at source/rofi.c:1035 "/org/qtools/rofi/default_configuration.rasi"

### Wayland SHM object name (a global kernel-namespace identifier, visible in /dev/shm on Linux)

- **Current** `/rofi-wayland-surface`
- **Proposed** /sofi-wayland-surface (better: randomised - see the shm-fixed-name-race finding)
- **Locations** source/wayland/display.c:208 gchar *shm_name = "/rofi-wayland-surface";

### sourcehut CI: upstream source URL, checkout directory used by every task, and the dist artifact filename

- **Current** `sr.ht/~qball/rofi, rofi/builddir, rofi-1.7.8-dev.tar.xz`
- **Proposed** the fork's clone URL, sofi/builddir, glob *.tar.xz
- **Locations** .build.yml:32 - https://sr.ht/~qball/rofi/; .build.yml:35 cd rofi; :38 ninja -C rofi/builddir; :40, :42, :45; :47 - rofi/builddir/meson-dist/rofi-1.7.8-dev.tar.xz

### Documentation site identity published by CI to GitHub Pages

- **Current** `Rofi Documentation / davatorium/rofi`
- **Proposed** Sofi Documentation / orpheus497/sofi
- **Locations** mkdocs/mkdocs.yml:1 site_name: Rofi Documentation, :2 repo_url: https://github.com/davatorium/rofi/, :8-9 issue/discussion links; published by .github/workflows/mkdocs.yml:19

### GitHub issue templates, contributing guide, PR template, and two external repos the CI depends on

- **Current** `davatorium/rofi, DaveDavenport/rofi, davatorium/doxy-coverage, davatorium/auto-close-issues`
- **Proposed** orpheus497/sofi (fork or vendor doxy-coverage and the auto-close action)
- **Locations** .github/CONTRIBUTING.md:13,15,16,17,23,25; .github/ISSUE_TEMPLATE/bug_report.yml:10,25,29,31; .github/ISSUE_TEMPLATE/config.yml:4,6; .github/ISSUE_TEMPLATE/documentation_report.yml:11,24,28,41,67; .github/ISSUE_TEMPLATE/feature_request.yml:10,14; .github/pull_request_template.md:10; .github/actions/setup/action.yml:48 git clone https://github.com/davatorium/doxy-coverage; .github/workflows/main.yml:13 uses: davatorium/auto-close-issues@v1.0.4

### POSIX shared-memory object name for the buffer pool

- **Current** `/rofi-wayland-surface`
- **Proposed** /sofi-wayland-surface (or drop the fixed name entirely in favour of memfd_create/randomised name — see finding shm-fixed-name-o-excl)
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:208 — gchar *shm_name = "/rofi-wayland-surface"; used at 209 (shm_open) and 210 (shm_unlink)

### Header include guard

- **Current** `ROFI_WAYLAND_INTERNAL_H`
- **Proposed** SOFI_WAYLAND_INTERNAL_H
- **Locations** /home/orpheus497/Projects/sofi/include/wayland-internal.h:1-2 — #ifndef ROFI_WAYLAND_INTERNAL_H / #define ROFI_WAYLAND_INTERNAL_H

### File-local static helper function names carrying the brand

- **Current** `rofi_cursor_type_to_wl_cursor, rofi_cursor_type_to_wp_cursor_shape`
- **Proposed** sofi_cursor_type_to_wl_cursor, sofi_cursor_type_to_wp_cursor_shape
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:738 rofi_cursor_type_to_wl_cursor(); :767 rofi_cursor_type_to_wp_cursor_shape(); call sites at :848, 1706 and :784

### Freedesktop-standard environment variables read by this backend — MUST NOT be rebranded

- **Current** `XCURSOR_SIZE, WAYLAND_DISPLAY`
- **Proposed** unchanged (these are cross-desktop standards, not project identity)
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:1694 — g_getenv("XCURSOR_SIZE"); :1723 — g_getenv("WAYLAND_DISPLAY")

### GLib log domain for this translation unit

- **Current** `Wayland`
- **Proposed** unchanged — it is a subsystem name, not the brand; but note that G_MESSAGES_DEBUG=Wayland is what users must set to see this file's g_debug output, so it belongs in the rebranded troubleshooting docs
- **Locations** /home/orpheus497/Projects/sofi/source/wayland/display.c:25 — #define G_LOG_DOMAIN "Wayland"

### Internal header include guards in this domain

- **Current** `ROFI_DISPLAY_H, ROFI_DISPLAY_INTERNAL_H, ROFI_XCB_DUMMY_H, ROFI_MODE_WAYLAND_WINDOW_H`
- **Proposed** SOFI_DISPLAY_H, SOFI_DISPLAY_INTERNAL_H, SOFI_XCB_DUMMY_H, SOFI_MODE_WAYLAND_WINDOW_H
- **Locations** include/display.h:28-29, include/display-internal.h:28-29, include/xcb-dummy.h:1-2, include/modes/wayland-window.h:28-29 and the trailing comment at :45

### MIT copyright headers in every domain source file

- **Current** `"rofi" as the work name, Qball Cow copyright`
- **Proposed** Keep the Qball Cow copyright line verbatim (MIT requires retention); add a separate sofi copyright line rather than renaming the existing work title
- **Locations** source/wayland/view.c:1-26, source/modes/wayland-window.c:1-26, include/modes/wayland-window.h:1-26, include/display.h:1-26, include/display-internal.h:1-26 — all read "rofi / MIT/X11 License / Copyright © 2013-20xx Qball Cow"

### Vendored Wayland protocol XMLs

- **Current** `unmodified upstream wlroots protocol definitions`
- **Proposed** leave unchanged
- **Locations** protocols/wlr-foreign-toplevel-management-unstable-v1.xml, protocols/wlr-layer-shell-unstable-v1.xml — verified free of any rofi identity (the only grep hit is the word "PROFITS" in the boilerplate at line 22 of each)

### Mode identity exposed to config/themes by the Wayland window mode

- **Current** `"window" / "display-window" (no rofi branding)`
- **Proposed** unchanged
- **Locations** source/modes/wayland-window.c:859-860 — Mode wayland_window_mode = {.name = "window", .cfg_name_key = "display-window", ...}; source/rofi.c:686 registration

### Header include guards

- **Current** `ROFI_XCB_H / ROFI_XCB_INTERNAL_H / ROFI_MODE_WINDOW_H`
- **Proposed** SOFI_XCB_H / SOFI_XCB_INTERNAL_H / SOFI_MODE_WINDOW_H
- **Locations** include/xcb.h:28-29 (ROFI_XCB_H), include/xcb-internal.h:28-29 (ROFI_XCB_INTERNAL_H), include/modes/window.h:28-29 (ROFI_MODE_WINDOW_H)

### Copyright/attribution banner naming the project

- **Current** `"rofi" + upstream copyright lines`
- **Proposed** "sofi" while KEEPING the upstream MIT copyright attributions (Sean Pringle / Qball Cow) — a fork may rename the project but must not strip the original copyright holders
- **Locations** source/xcb/display.c:2, source/xcb/view.c:2, source/modes/window.c:2, include/xcb.h:2, include/xcb-internal.h:2, include/modes/window.h:2 (all read ` * rofi`)

### Doc comments / log strings mentioning the product name (cosmetic, no protocol impact)

- **Current** `"rofi" / "Rofi" in comments and debug output`
- **Proposed** "sofi" / "Sofi"
- **Locations** source/xcb/display.c:78,80 ("running rofi"), :941 (g_debug("rofi on current monitor")), :1129 and :1132 (g_debug("rofi_view_paste: ...")); source/xcb/view.c:28 ("The Rofi View log domain"), :859 ("Quit rofi on click")

### Mode names and config keys exposed by window mode (checked — NOT branded, listed so the rebrand does not touch them)

- **Current** `window / windowcd / display-window / display-windowcd`
- **Proposed** unchanged
- **Locations** source/modes/window.c:1143-1144 (.name = "window", .cfg_name_key = "display-window") and :1157-1158 ("windowcd" / "display-windowcd")

### GResource prefix for the built-in theme and default config

- **Current** `/org/qtools/rofi`
- **Proposed** /org/qtools/sofi (or a new vendor prefix) — must change in both files simultaneously or startup fails silently
- **Locations** source/rofi.c:1035 ("/org/qtools/rofi/default_configuration.rasi") and resources/resources.xml:3 (gresource prefix)

### Header include guards and internal macro prefix

- **Current** `ROFI_*_H`
- **Proposed** SOFI_*_H — cosmetic, but the installed headers (mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h) leak these guard names into plugin builds
- **Locations** include/rofi.h:28 ROFI_MAIN_H, include/view.h:28 ROFI_VIEW_H, include/view-internal.h:28 ROFI_VIEW_INTERNAL_H, include/keyb.h:28 ROFI_KEYB_H, include/settings.h:28 ROFI_SETTINGS_H, include/timings.h:33 ROFI_TIMINGS_H; source/rofi.c:80 ROFI_MAX_DMENU_INPUT

### Header include guards in the widget headers

- **Current** `ROFI_HBOX_H / ROFI_CONTAINER_H / ROFI_ICON_H / ROFI_LISTVIEW_H / ROFI_SCROLLBAR_H / ROFI_TEXTBOX_H / ROFI_WIDGET_H / ROFI_ICON_FETCHER_H`
- **Proposed** SOFI_HBOX_H / SOFI_CONTAINER_H / SOFI_ICON_H / SOFI_LISTVIEW_H / SOFI_SCROLLBAR_H / SOFI_TEXTBOX_H / SOFI_WIDGET_H / SOFI_ICON_FETCHER_H
- **Locations** include/widgets/box.h:28,29,69 (ROFI_HBOX_H); include/widgets/container.h:28,29,62; include/widgets/icon.h:28,29,67; include/widgets/listview.h:28,29,293; include/widgets/scrollbar.h:28,29,94; include/widgets/textbox.h:28,29,364; include/widgets/widget.h:28,29,359; include/rofi-icon-fetcher.h:1,2,85

### Theme API symbol prefix used by every widget (rofi_theme_get_*)

- **Current** `rofi_theme_get_boolean / _color / _string / _distance / _padding / _double / _orientation / _integer / _list_strings / _list_distance / _highlight / _image / _cursor_type / rofi_theme_has_property`
- **Proposed** sofi_theme_get_*
- **Locations** 66 call sites across source/widgets/*.c: widget.c:46,47,49,50,53,56,58,60,70,72,245,247,267; textbox.c:128,179,233,240,242,245,247,255,268,274,276,363,530,557,578,590,593,595,597,618,621,627,628,1126; listview.c:204,214,228,818,820,822,823,825,826,827,829,830,833,835,838; box.c:58,303,361,363; icon.c:116,119,173,176,178,182,184; scrollbar.c:109,177,179

### Rofi* typedef names consumed by the widget layer

- **Current** `RofiDistance (10 uses), RofiPadding (12), RofiOrientation (5), RofiCursorType (1), RofiHighlightColorStyle (1)`
- **Proposed** SofiDistance / SofiPadding / SofiOrientation / SofiCursorType / SofiHighlightColorStyle
- **Locations** include/widgets/widget-internal.h:33-51 (WIDGET_DISTANCE_INIT/WIDGET_PADDING_INIT bodies), 68-75, 78; include/widgets/scrollbar.h:48; include/widgets/box.h:58; source/widgets/listview.c:71,78,95; source/widgets/textbox.c:187,362,577; source/widgets/icon.c:173

### ROFI_* enum/macro constants used in layout code

- **Current** `ROFI_ORIENTATION_HORIZONTAL / ROFI_ORIENTATION_VERTICAL / ROFI_PU_PX / ROFI_DISTANCE_MODIFIER_NONE / ROFI_HL_SOLID / ROFI_CURSOR_DEFAULT`
- **Proposed** SOFI_ORIENTATION_* etc.
- **Locations** 29 uses of ROFI_ORIENTATION_HORIZONTAL and 26 of ROFI_ORIENTATION_VERTICAL across widget.c:154-176,581-615; listview.c:51,53,378,457,471,473,504,581,661,828,836; box.c:54,64,100,129,196,292,350,361; textbox.c:189,580,1127; scrollbar.c:111; icon.c:174. Plus include/widgets/widget-internal.h:39 (ROFI_PU_PX), :40 (ROFI_DISTANCE_MODIFIER_NONE), :44 (ROFI_HL_SOLID) and source/widgets/widget.c:53 (ROFI_CURSOR_DEFAULT)

### Cross-module calls from widgets into the view/helper layer

- **Current** `rofi_view_queue_redraw() / rofi_view_reload() / rofi_fallthrough`
- **Proposed** sofi_view_queue_redraw() / sofi_view_reload() / sofi_fallthrough
- **Locations** source/widgets/textbox.c:66 (rofi_view_queue_redraw); source/widgets/listview.c:753 (rofi_fallthrough macro); source/rofi-icon-fetcher.c:545,646,675,694,749 (rofi_view_reload)

### File-header copyright banner

- **Current** `" * rofi"`
- **Proposed** " * sofi (fork of rofi)" - keep the existing upstream copyright lines, the MIT licence requires the notice be retained
- **Locations** line 2 of all 15 domain files: source/widgets/{box,container,icon,listview,scrollbar,textbox,widget}.c and include/widgets/{box,container,icon,listview,scrollbar,textbox,widget,widget-internal}.h

### Widget headers are NOT installed - rename is internal-only

- **Current** `include/widgets/*.h are build-internal only`
- **Proposed** rename freely; rofi-icon-fetcher.h is the only plugin-ABI surface in this domain
- **Locations** meson.build:195-203 installs only include/mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h; none of those include widgets/*.h (mode.h includes rofi-types.h/cairo/gmodule, helper.h includes rofi-types.h/cairo)

### XDG thumbnail cache directory and .thumbnailer discovery - MUST NOT be renamed

- **Current** `~/.cache/thumbnails/... and $XDG_DATA_DIRS/thumbnailers/*.thumbnailer`
- **Proposed** unchanged - freedesktop.org shared specs, not rofi identity; renaming would orphan every existing thumbnail and break third-party thumbnailers
- **Locations** source/rofi-icon-fetcher.c:471-495 (g_get_user_cache_dir() + "/thumbnails/{normal,large,x-large,xx-large}/<md5>.png"); lines 54-56 and 105-159 (THUMBNAILER_ENTRY_GROUP "Thumbnailer Entry", THUMBNAILER_EXTENSION ".thumbnailer", g_get_user_data_dir()/g_get_system_data_dirs() + "/thumbnailers")

### No branded string literals anywhere in this domain

- **Current** `no env var, on-disk path, WM_CLASS, namespace or user-visible message in this domain`
- **Proposed** n/a
- **Locations** verified by grepping for quoted strings containing rofi across source/widgets/*.c, include/widgets/*.h, source/rofi-icon-fetcher.c, include/rofi-icon-fetcher.h - the only hits are #include "rofi-icon-fetcher.h" (source/widgets/icon.c:39), #include "rofi-types.h" (include/widgets/box.h:31) and a comment (source/widgets/listview.c:216)

### Mode names used as config keys, theme widget names and combi '!bang' prefixes

- **Current** `combi, dmenu, drun, filebrowser, keys, recursivebrowser, run, ssh (none contain "rofi")`
- **Proposed** unchanged
- **Locations** combi.c:347-348 ("combi"/"display-combi"), dmenu.c:509-510 ("dmenu"/"display-combi" — note the copy-paste bug reported separately), drun.c:1683-1684, filebrowser.c:729-730, help-keys.c:113-114 ("keys"/"display-keys"), recursivebrowser.c:591-592, run.c:603-604, script.c (per-user script name from the filename, script.c:644-648), ssh.c:710-711

### GResource prefix and aliases for the built-in theme/config

- **Current** `/org/qtools/rofi/default.rasi, /org/qtools/rofi/default_configuration.rasi`
- **Proposed** /org/qtools/sofi/… — internal only, but see finding default-theme-resource-silent-failure: a mismatch fails silently
- **Locations** resources/resources.xml:3 (`prefix="/org/qtools/rofi"`) with aliases default.rasi and default_configuration.rasi; looked up at lexer/theme-lexer.l:411 and source/rofi.c:1035; compiled at meson.build:365

### Theme-engine log domains and header guards (cosmetic, non-branded)

- **Current** `"Theme"/"Parser"/"XrmOptions"; ROFI_*_H guards on non-installed headers`
- **Proposed** log domains need no change (not rofi-branded); guards on non-installed headers can be renamed freely
- **Locations** source/theme.c:29 (G_LOG_DOMAIN "Theme"), lexer/theme-lexer.l:50 (LOG_DOMAIN "Parser"), source/xrmoptions.c:28 ("XrmOptions"), include/css-colors.h:1 (ROFI_INCLUDE_CSS_COLORS_H), include/xrmoptions.h:28 (ROFI_XRMOPTIONS_H)

### Header include guards in this domain

- **Current** `ROFI_*_H`
- **Proposed** SOFI_*_H
- **Locations** include/helper.h:28 ROFI_HELPER_H, include/helper-theme.h:28 ROFI_HELPER_THEME_H, include/history.h:28 ROFI_HISTORY_H, include/rofi-icon-fetcher.h:1 ROFI_ICON_FETCHER_H, include/display.h:28 ROFI_DISPLAY_H, include/display-internal.h:28 ROFI_DISPLAY_INTERNAL_H

### Test-suite hardcoded identity strings

- **Current** `"rofi-pid.pid", "rofi-sensible-terminal ..."`
- **Proposed** "sofi-pid.pid", "sofi-sensible-terminal ..." — these expectations must be updated in lockstep with config/config.c:65 or the helper test fails
- **Locations** test/helper-pidfile.c:99 g_build_filename(tmpd, "rofi-pid.pid", NULL); test/helper-test.c:313, :332, :339 expected values "rofi-sensible-terminal -e aap" / "rofi-sensible-terminal some title blub -e aap"; the '* rofi' banner at line 2 of every test file

### Thumbnail cache directory (NOT a rebrand surface — flagged in the brief, but it is the shared freedesktop store)

- **Current** `$XDG_CACHE_HOME/thumbnails/<size>/<md5>.png — contains no 'rofi' component`
- **Proposed** unchanged; renaming this would break interoperability with the freedesktop thumbnail spec and every other consumer of the shared store
- **Locations** source/rofi-icon-fetcher.c:471 g_get_user_cache_dir(), :477-494 g_strconcat(cache_dir, "/thumbnails/{normal,large,x-large,xx-large}/", md5_hex, ".png")

### Per-file copyright/identity banner in this domain's sources

- **Current** `' * rofi' above the MIT/X11 block, Copyright Sean Pringle / Qball Cow`
- **Proposed** ' * sofi' — the upstream copyright lines must be retained under the MIT licence regardless of the rename
- **Locations** line 2 of source/helper.c, source/history.c, source/rofi-icon-fetcher.c, include/helper.h, include/helper-theme.h, include/history.h, include/display.h, include/display-internal.h and every test/*.c

### GResource path prefix compiled into the binary (must change in lockstep with the C loader)

- **Current** `<gresource prefix="/org/qtools/rofi"> with aliases default.rasi and default_configuration.rasi`
- **Proposed** /org/<new-vendor>/sofi — note this is a *paired* change: the C code that does g_resources_lookup_data on this prefix must change identically or the built-in default theme fails to load at startup
- **Locations** resources/resources.xml line 3; compiled by meson.build line 365 (gnome.compile_resources)

### Repology packaging badge and metapackage name

- **Current** `https://repology.org/metapackage/rofi/versions with badge https://repology.org/badge/tiny-repos/rofi.svg`
- **Proposed** Remove. Repology tracks distro packages; sofi has none, so the badge either 404s or — worse — displays upstream rofi's packaging status as if it were sofi's.
- **Locations** README.md line 6

### Stargazers chart

- **Current** `https://starchart.cc/davatorium/rofi.svg linking to https://starchart.cc/davatorium/rofi`
- **Proposed** Remove, or point at starchart.cc/orpheus497/sofi. As-is it advertises upstream's star history on the fork's README.
- **Locations** README.md line 326

### GitHub shields.io badges (issues, forks, stars, downloads)

- **Current** `img.shields.io/github/{issues,forks,stars,downloads}/davatorium/rofi.svg`
- **Proposed** orpheus497/sofi equivalents, or remove — the downloads badge in particular reads upstream's release-asset counts.
- **Locations** README.md lines 2-5

### IRC channel

- **Current** `#rofi on irc.libera.chat (and dead freenode links in CONTRIBUTING.md)`
- **Proposed** #rofi is upstream's channel — sofi cannot claim it. Either register a sofi channel or remove the IRC references. The freenode links in CONTRIBUTING.md lines 15/25 are dead regardless of the rebrand.
- **Locations** README.md line 322; doc/rofi.1.markdown line 1247 (irc://irc.libera.chat:6697/#rofi); .github/ISSUE_TEMPLATE/config.yml lines 6-7; .github/CONTRIBUTING.md lines 15, 25 (stale freenode)

### Reddit community

- **Current** `https://reddit.com/r/qtools/`
- **Proposed** Remove — r/qtools is the upstream author's community, unrelated to this fork.
- **Locations** .github/CONTRIBUTING.md lines 14, 24

### Third-party GitHub Actions and helper repos owned by the upstream org

- **Current** `davatorium-owned action and doxy-coverage fork`
- **Proposed** Keep as external dependencies (they are functional and MIT-ish tooling, not identity) or vendor/fork them. This is the one class of davatorium URL that should NOT be blindly rewritten to orpheus497 — those repos do not exist under the new org and the workflows would fail immediately.
- **Locations** .github/workflows/main.yml line 13 (davatorium/auto-close-issues@v1.0.4); .github/actions/setup/action.yml line 48 (git clone https://github.com/davatorium/doxy-coverage); .gitlab-ci.yml line 21 (alobbs/doxy-coverage)

### Code of Conduct enforcement contact

- **Current** `qball@gmpclient.org`
- **Proposed** The sofi maintainer's contact address. See the corresponding finding — this is an active routing address, not an attribution, and leaving it is a real harm.
- **Locations** CODE_OF_CONDUCT.md line 37

### Doxygen config filename and generated project name

- **Current** `doc/rofi.doxy.in producing doc/rofi.doxy, PROJECT_NAME=rofi via @PACKAGE@`
- **Proposed** doc/sofi.doxy.in -> doc/sofi.doxy. PROJECT_NAME needs no edit — it already follows meson.build line 1 automatically. Only the two literal filenames at doc/meson.build lines 61-62 need updating.
- **Locations** doc/rofi.doxy.in; consumed by doc/meson.build lines 60-64 (input: 'rofi.doxy.in', output: 'rofi.doxy') and line 71; PROJECT_NAME at doc/rofi.doxy.in line 45 is @PACKAGE@, fed from meson.project_name() at doc/meson.build line 55

### Theme file header comments

- **Current** `'ROFI Color theme' / 'Rofi color theme' comment headers; themes/breaking-themes/2076.rasi line 3 records 'Rofi version: 1.7.7'`
- **Proposed** Prose only — safe to rewrite to 'SOFI Color theme'. But do NOT rewrite themes/gruvbox-*.rasi line 10 and themes/gruvbox-common.rasinc line 5 ('Source: https://github.com/bardisty/gruvbox-rofi'), which is third-party attribution for externally-authored themes and must stay verbatim.
- **Locations** themes/*.rasi line 2 in 32 files (e.g. themes/iggy.rasi:2, themes/Arc-Dark.rasi:2 ' * ROFI Color theme'), themes/breaking-themes/2076.rasi lines 2-3, themes/fullscreen-preview.rasi line 103, themes/docu.rasi line 110

### Example script variable prefix and data file names

- **Current** `ROFI_FB_GENERIC_FO, ROFI_FB_PREV_LOC_FILE, ROFI_FB_HISTORY_FILE, ROFI_FB_* ; data files under ~/.local/share/rofi/`
- **Proposed** SOFI_FB_* and ~/.local/share/sofi/. These are script-local variables (not a rofi-set contract), so renaming is safe — unlike the ROFI_RETV/ROFI_INFO/ROFI_DATA/ROFI_INPUT family.
- **Locations** Examples/rofi-file-browser.sh lines 4-12 (ROFI_FB_* variables) and lines 5-6 (~/.local/share/rofi/rofi_fb_prevloc, rofi_fb_history)

### Manpage AUTHOR sections (attribution, not branding)

- **Current** `Qball Cow, Rasmus Steinke, Morgane Glidic, Sean Pringle (doc/rofi.1.markdown line 1272), Dave Davenport and Michael Stapelberg (doc/rofi-sensible-terminal.1.markdown lines 48-51)`
- **Proposed** PRESERVE VERBATIM, appending the sofi maintainer rather than replacing. doc/rofi.1.markdown line 1274 ('For a full list of authors, check the AUTHORS file') and the embedded MIT text at lines 1213-1236 are the manpage's copy of the licence notice and must remain intact.
- **Locations** doc/rofi.1.markdown lines 1266-1274; doc/rofi-theme-selector.1.markdown lines 37-40; doc/rofi-sensible-terminal.1.markdown lines 46-51; doc/rofi-debugging.5.markdown line 177; doc/rofi-dmenu.5.markdown line 237; doc/rofi-keys.5.markdown line 586; doc/rofi-script.5.markdown line 215

### MIT licence text and copyright holders

- **Current** `MIT/X11; 'Modified 2013-2024 Qball Cow <qball@gmpclient.org>' and 'Copyright (c) 2012 Sean Pringle <sean.pringle@gmail.com>'`
- **Proposed** PRESERVE BOTH LINES UNCHANGED, and add a new line for the sofi copyright above them. Removing or replacing lines 2-3 is a licence violation, not a style choice — see notes.
- **Locations** COPYING lines 1-3 (holders) and 13-14 (the 'above copyright notice ... shall be included' clause); duplicated inside doc/rofi.1.markdown lines 1213-1236 under ## LICENSE; surfaced on the docs site via the symlink mkdocs/docs/COPYING.md -> ../../COPYING, listed at mkdocs/mkdocs.yml line 6

### AUTHORS file

- **Current** `63 upstream contributor names`
- **Proposed** PRESERVE ALL 63 ENTRIES and append sofi contributors. Truncating this list would erase the attribution the manpage explicitly points readers to.
- **Locations** AUTHORS (63 names, lines 1-63, including 'Dave Davenport' line 15, 'lbonn' line 36, 'seanpringle' line 51, 'Morgane Glidic' line 47); .mailmap line 1 maps Morgane Glidic; referenced from doc/rofi.1.markdown line 1274

### README credit paragraph

- **Current** `Credits Sean Pringle (simpleswitcher, line 32), notes Rofi was renamed from simpleswitcher (line 36), and credits lbonn for the Wayland fork (lines 39-41)`
- **Proposed** PRESERVE the substance. The lineage sentence needs one more hop ('sofi is a fork of rofi, which started as a clone of simpleswitcher by Sean Pringle...'), but the named credits to Sean Pringle, Dave Davenport and lbonn must survive the rewrite of the surrounding prose.
- **Locations** README.md lines 31-41

