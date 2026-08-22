# Audit Register

Generated 2026-08-22 18:55 from a 21-agent read-only audit (10 domain surveys + 10 adversarial verifiers + synthesis).
263 raw findings, 15 refuted by the verify pass, **248 retained**. Severities below are the verifier's corrected values.

Status legend: CONFIRMED = verifier re-read the cited code and reproduced the reasoning. PLAUSIBLE = code is as described, but whether it misbehaves depends on runtime conditions not checkable statically.



---

# HIGH

## `source/wayland/display.c:319` — display_buffer_pool_get_next_buffer() dereferences a NULL pool that its only caller can pass

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

display_buffer_pool_new() has four failure returns (NULL): stride<0 at line 202-204, shm_open failure at 211-215, fcntl failure at 216-219, ftruncate failure at 220-223, mmap failure at 225-230. Its only caller, /home/orpheus497/Projects/sofi/source/wayland/view.c:428-432, does `if (state->pool == NULL) state->pool = display_buffer_pool_new(...); cairo_surface_t *surface = display_buffer_pool_get_next_buffer(state->pool);` with no check in between. display_buffer_pool_get_next_buffer() then executes `buffer = pool->buffers + i;` at line 319 and `if (!buffer->released)` at line 320 on a NULL pool.

**Failure scenario.** A stale /dev/shm/rofi-wayland-surface exists (previous instance was SIGKILLed between the shm_open at line 209 and the shm_unlink at line 210), so shm_open returns -1/EEXIST and display_buffer_pool_new returns NULL. view.c:432 calls display_buffer_pool_get_next_buffer(NULL) -> read of ((wayland_buffer*)NULL)->released at display.c:320 -> SIGSEGV on every launch until the user manually deletes /dev/shm/rofi-wayland-surface.

**Proposed fix.** Add `if (pool == NULL) return NULL;` at the top of display_buffer_pool_get_next_buffer(), and make view.c:428-432 bail out (and log) when display_buffer_pool_new() returns NULL.

**Verifier.** display.c:315-320 dereferences `pool->buffers + i` with no NULL guard; the sole caller view.c:428-432 is exactly `if (state->pool == NULL) state->pool = display_buffer_pool_new(...); cairo_surface_t *surface = display_buffer_pool_get_next_buffer(state->pool);` and only checks the returned surface (view.c:433). display_buffer_pool_new does return NULL at display.c:203, 214, 218, 222, 229. Downgraded from critical: reaching it requires pool_new to actually fail (shm/ftruncate/mmap error), not a normal-path crash.

## `source/wayland/display.c:1756` — g_error() aborts the process on missing layer-shell, defeating the caller's graceful-failure path

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

g_error() is G_LOG_LEVEL_ERROR, which is always fatal in GLib — it calls abort(). It is used at four places here, each followed by unreachable `return FALSE;`:
  line 1490: seat version below WL_SEAT_INTERFACE_MIN_VERSION (inside the registry handler)
  line 1505: output version below WL_OUTPUT_INTERFACE_MIN_VERSION (inside the registry handler)
  line 1752: compositor/shm/outputs/seats missing
  line 1756: `g_error("Rofi on wayland requires support for the layer shell protocol")`
Meanwhile /home/orpheus497/Projects/sofi/source/rofi.c:1264 stores the return value and rofi.c:1280-1291 prints a friendly "No valid backend was found..." message — code that can never run. There is no xdg-shell fallback anywhere in the tree (grep for "xdg" under source/wayland/ returns nothing; meson.build:324 lists only wlr-layer-shell-unstable-v1.xml), so every GNOME/Mutter user hits line 1756.

**Failure scenario.** A GNOME user (Mutter does not implement zwlr_layer_shell_v1) runs `sofi -show drun`. Instead of the intended message, the process aborts: SIGABRT, a core dump written by systemd-coredump, and exit code 134. `sofi --help` is also unreachable on that machine because display_setup() runs at rofi.c:1264 before the -h handling at rofi.c:1268. The registry-handler g_error()s at 1490/1505 abort the client outright when any single old wl_seat(<5) or wl_output(<2) global is advertised, instead of just skipping that global.

**Proposed fix.** Replace all four with g_warning()/g_critical() and let the existing `return FALSE` propagate to rofi.c:1280; in the registry handler, `return` without binding rather than aborting the whole client for one unsupported global.

**Verifier.** g_error (always fatal in GLib) at display.c:1490, 1505, 1752 and 1756 (`g_error("Rofi on wayland requires support for the layer shell protocol"); return FALSE;`), while rofi.c:1264 stores display_setup's result and rofi.c:1280-1298 prints the friendly fallback that can never run; rofi.c:1268 (-h handling) is also after 1264. grep shows no xdg-shell usage anywhere under source/wayland/ (meson.build:318 compiles xdg-shell.xml only for other consumers), so there is no fallback path.

## `source/modes/drun.c:421` — Desktop Action entries are launched via GAppInfo, which ignores the action's Exec line

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

read_desktop_file creates one extra entry per "Desktop Action X" group and stores that group's Exec in entry->exec (line 761-762, with `action` being the action group). exec_cmd_entry expands that exec into `fp` (lines 352-396) but then, when pd->disable_giolaunch is FALSE (the default), builds `g_desktop_app_info_new_from_keyfile(e->key_file)` at line 422 and calls g_app_info_launch at line 431. GDesktopAppInfo is constructed from the [Desktop Entry] group only, so it runs the application's *main* Exec. `fp` is used solely in the `launched == FALSE` fallback at line 465. e->action is also hard-coded to DRUN_GROUP_NAME at line 730, so Terminal/Path/StartupNotify are read from the main group too.

**Failure scenario.** Enable `drun-show-actions` and pick e.g. "Firefox - New Private Window" (a Desktop Action). g_desktop_app_info_new_from_keyfile succeeds, g_app_info_launch runs the plain `firefox %u` main Exec, and a normal window opens instead of a private one. The action is silently unreachable for every desktop file, unless the user sets `gio-launch: false`.

**Proposed fix.** When e->action != DRUN_GROUP_NAME, skip the GAppInfo path and go straight to helper_execute_command_env with the expanded `fp`; or use g_desktop_app_info_launch_action().

**Verifier.** drun.c:797-798 creates an extra entry per "Desktop Action X" group; drun.c:761-763 reads Exec from that `action` group; but drun.c:730 hard-codes `entry_list[..].action = DRUN_GROUP_NAME`. exec_cmd_entry expands e->exec into `str`/`fp` but at drun.c:421-431, with pd->disable_giolaunch FALSE (the default, drun.c:1226), builds g_desktop_app_info_new_from_keyfile(e->key_file) and calls g_app_info_launch — GDesktopAppInfo reads only the [Desktop Entry] group, so the main Exec runs. `fp` is used only in the `launched == FALSE` fallback at drun.c:446-459. Terminal/Path/StartupNotify are likewise read from DRUN_GROUP_NAME (lines 396, 409, 456).

## `source/modes/wayland-window.c:622` — Selected toplevel dereferenced without NULL check after list shrinks (race with async reload)

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

In wayland_window_mode_result the MENU_OK path (lines 622-632) and the MENU_ENTRY_DELETE path (lines 635-637) do `g_list_nth_data(pd->wlr_toplevels, selected_line)` and use the result unconditionally. Every other accessor in the file guards it: _get_display_value checks `toplevel == NULL` (line 808), _get_icon checks it (line 833), wayland_window_token_match uses g_return_val_if_fail (line 691). The list shrinks synchronously in wlr_foreign_toplevel_handle_closed (line 354, g_list_remove) but the UI is only told to reload through wayland_window_update_toplevel -> rofi_view_reload() (line 187), which is rate-limited to one refresh per 66ms by wayland_rofi_view_reload (source/wayland/view.c:305-308). During that window the listview still believes there are N rows.

**Failure scenario.** Two windows are listed; the user has row index 1 highlighted. The second window exits, the compositor sends `closed`, the handle is removed from pd->wlr_toplevels (list length 1) and a 66ms reload timer is armed. Within those 66ms the user presses Enter: selected_line==1, g_list_nth_data returns NULL, wlr_foreign_toplevel_handle_activate(NULL, seat) dereferences `self->handle` at line 195 -> SIGSEGV. Same crash via Shift-Delete (line 637 -> line 199) and via a custom action (line 626 -> line 559 `toplevel->identifier`).

**Proposed fix.** After the g_list_nth_data calls at lines 622-623 and 635-636, bail out (return RELOAD_DIALOG or MODE_EXIT) when toplevel == NULL, the same way _get_display_value already does.

**Verifier.** Read source/modes/wayland-window.c:620-638. Line 622-623 assigns `toplevel = g_list_nth_data(pd->wlr_toplevels, selected_line)` and passes it straight to wayland_act_on_window (626, which derefs `toplevel->identifier` at 559) or wlr_foreign_toplevel_handle_activate (629, which derefs `self->handle` at 195) with no NULL test; the MENU_ENTRY_DELETE path at 635-637 does the same into wlr_foreign_toplevel_handle_close (199). Every other accessor in the same file does guard: 808 `toplevel == NULL ||`, 833 `toplevel == NULL ||`, 691 g_return_val_if_fail. The list is shrunk synchronously at 354 (g_list_remove in wlr_foreign_toplevel_handle_closed) while the UI refresh goes through rofi_view_reload() at 187, which on Wayland is coalesced into a 1000/15 ms g_timeout (source/wayland/view.c:303-308), so the listview index can legitimately outlive the row. Crash requires winning that window, so severity high rather than critical.

## `source/modes/window.c:901` — XCB window mode appends the window title unescaped into a Pango-markup row (the Wayland copy of the same function escapes it)

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

helper_eval_add_str in source/modes/wayland-window.c:723-752 and source/modes/window.c:882-909 are a copy-paste pair that has diverged. The Wayland copy escapes in all three branches (lines 732, 737, 742: g_markup_escape_text). The XCB copy escapes in the two truncation branches (lines 891, 896) but the untruncated/padded branch at line 901 does a bare `g_string_append(str, input_nn)`. Both modes set MARKUP on the row (source/modes/window.c:964, source/modes/wayland-window.c:816), so the string is parsed as Pango markup.

**Failure scenario.** On X11, open a window whose title contains an ampersand or angle bracket — e.g. a browser tab titled 'Tom & Jerry' or a terminal running `vim <foo>` — and run `rofi -show window` with the default `window-format` (`{w} {c} {t}`). Line 901 emits the raw '&', Pango's markup parser fails on the entity, and the row renders blank/garbled with a `Failed to parse markup` warning. The same title renders correctly under the Wayland window mode.

**Proposed fix.** Replace line 901 with the escaped form used by the Wayland twin (g_markup_escape_text(input_nn, -1) + append + free), and then de-duplicate the two implementations into one shared helper so they cannot drift again.

**Verifier.** source/modes/window.c:900-905: the l<=0 branch does a bare `g_string_append(str, input_nn);` while the two truncation branches at 891 and 896 use g_markup_escape_text. The Wayland twin escapes in all three branches (source/modes/wayland-window.c:732,737,742). helper_eval_cb (window.c:917-935) leaves l==0 for any token without a `:N` suffix — i.e. the default window-format — and _get_display_value sets *state |= MARKUP at window.c:964, so the unescaped title is handed to Pango's markup parser.

## `source/rofi.c:847` — Warning messages are passed to g_warning() as the format string

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`g_warning(((GString*)iter->data)->str, NULL);` passes attacker/user-controlled text as a printf format string (g_warning is declared G_GNUC_PRINTF). The trailing NULL is also a bogus vararg. git blame shows commit 30885f2b ("[Display] Print warning messages using g_warning") replaced a safe `fputs(...)` pair with this call, so it is a regression introduced in this tree, not upstream-inherited. Warning strings come from theme/config parsing and contain file paths, property names and quoted values straight from the .rasi file.

**Failure scenario.** Put a widget/property containing a percent sequence in ~/.config/rofi/config.rasi so the theme parser queues a warning whose text contains e.g. `%s %s %s` or `%n`. On the next startup(), list_of_warning_msgs is non-empty and g_warning interprets those as conversions -> reads garbage pointers off the stack (info leak / SIGSEGV), and `%n` is a write primitive.

**Proposed fix.** `g_warning("%s", ((GString *)iter->data)->str);` and drop the NULL argument. Also enable -Wformat=2 / -Wformat-security in meson.build so this class cannot regress.

**Verifier.** source/rofi.c:847 reads exactly `g_warning(((GString*)iter->data)->str, NULL);` with the old safe fputs pair commented out on 848-849. `git log -L 840,850:source/rofi.c` shows commit 30885f2b replacing `fputs(...); fputs("\n", stderr);` with this call, so it is a regression in this tree. g_warning is a G_GNUC_PRINTF macro, so the queued warning text (built from .rasi parse messages containing file paths and quoted property values) is used as the format string, and the trailing NULL is a bogus vararg. Severity high rather than critical: the format text originates from the user's own config/theme files, so it needs a hostile downloaded .rasi to be attacker-controlled.

## `source/wayland/view.c:158` — -x-offset / -y-offset command-line and config options are silently ignored on Wayland

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

rofi_get_offset_px passes a hard-coded default of 0 to rofi_theme_get_distance for the "x-offset"/"y-offset" properties. The xcb backend does the same lookup but seeds it with the config values: source/xcb/view.c:358-361 uses `rofi_theme_get_distance(..., "x-offset", config.x_offset)` and `..., "y-offset", config.y_offset`. config.x_offset/config.y_offset are real, documented options (include/settings.h:116-118, defaults in config/config.c:97-99, wired to `-x-offset`/`-y-offset` in source/xrmoptions.c:117,127). Because the theme property is unset by default, the Wayland path always resolves to 0.

**Failure scenario.** `rofi -show run -location 1 -x-offset 100 -y-offset 50` on Wayland: rofi_get_offset_px returns 0/0, window_update_size_normal passes 0/0 to display_set_surface_dimensions, and the menu is flush against the top-left corner. The identical command on X11 places it 100/50px in. Only `configuration { x-offset: 100; }` inside a .rasi theme works on Wayland.

**Proposed fix.** Change the two defaults at source/wayland/view.c:155-158 to config.x_offset / config.y_offset to match source/xcb/view.c:358-361.

**Verifier.** source/wayland/view.c:154-160: rofi_get_offset_px calls rofi_theme_get_distance(..., property, 0) with a literal 0 default. source/xcb/view.c:358-361 uses the same lookup seeded with config.x_offset / config.y_offset. config.x_offset/y_offset are real options (include/settings.h:116,118; defaults config/config.c:97,99; bound to -x-offset/-y-offset at source/xrmoptions.c:117,127). Both Wayland size paths consume rofi_get_offset_px (view.c:249-250), so the CLI/config values are dropped on Wayland unless a theme sets the property.

## `source/wayland/view.c:396` — calculate_window_width treats the layer surface size as the screen size, so a second view shrinks to 50% of the previous menu width

- **kind** correctness · **severity** high · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wayland_rofi_view_calculate_window_width seeds `screen_width` from display_get_surface_dimensions (line 398) and then computes `state->width = (screen_width / 100.0f) * DEFAULT_MENU_WIDTH` (line 406). display_get_surface_dimensions returns wayland->layer_width (source/wayland/display.c:1828-1839), which display_set_surface_dimensions overwrites with the *menu* width on every resize (source/wayland/display.c:1849-1851) and which the layer-surface configure handler then echoes back (source/wayland/display.c:1634). window_update_size_normal (line 162-171) passes state->width/state->height, so as soon as the first view is laid out, layer_width is the menu width, not the output width. The click_to_exit path masks this because it passes the full screen size (line 240), and config.click_to_exit defaults to TRUE (config/config.c:156) — so the bug only shows with `-no-click-to-exit`. wayland_rofi_view_calculate_window_height has the same dependency for the fullscreen branch (line 481).

**Failure scenario.** `rofi -show run -no-click-to-exit` on a 1920px output: first view width = 960. The user then triggers an error (rofi_view_error_dialog, source/view.c:1964 calls rofi_view_calculate_window_width again) or switches mode; display_get_surface_dimensions now returns 960, so the new window is 480px wide, and each subsequent view halves again.

**Proposed fix.** Cache the real output size once (as wayland_rofi_view_get_current_monitor at line 114 already does) and use that in calculate_window_width/height, or keep a separate wayland->output_width that display_set_surface_dimensions never clobbers.

**Verifier.** source/wayland/view.c:396-410 seeds screen_width from display_get_surface_dimensions (398) and computes state->width = screen_width/100*DEFAULT_MENU_WIDTH (406). source/wayland/display.c:1828-1839 returns wayland->layer_width, and display_set_surface_dimensions overwrites layer_width with its `width` argument at 1849-1851; the configure handler echoes it at 1634. window_update_size_normal (view.c:162-171) passes state->width/state->height, so after one non-click-to-exit layout layer_width IS the menu width. window_update_size_with_outside_click passes WlState.surface_width/height (screen-sized) at 240-241, and config.click_to_exit defaults TRUE (config/config.c:156), matching the claimed masking. calculate_window_width is called again on every rofi_view_create (source/view.c:1890) and on the error dialog (source/view.c:1964), and calculate_window_height's fullscreen branch has the same dependency (view.c:479-483).

## `source/wayland/display.c:439` — Key-repeat GSource pointer is left dangling on three early returns, then g_source_destroy()d

- **kind** memory · **severity** high · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

self->repeat.source is a *borrowed*, unreffed pointer obtained via g_main_context_find_source_by_id() (lines 477 and 517). When a timeout callback returns G_SOURCE_REMOVE, GLib destroys and unrefs the source, so the stored pointer becomes dangling. Line 429-431 in wayland_key_repeat() gets this right (`self->repeat.source = NULL;` before returning G_SOURCE_REMOVE), which proves the intent, but three sibling returns do not:
  - line 439-441: `if (state == NULL) return G_SOURCE_REMOVE;` in wayland_key_repeat()
  - line 455-457: `if (self->repeat.key == 0) return FALSE;` in wayland_key_repeat_delay()
  - line 464-466: `if (state == NULL) return G_SOURCE_REMOVE;` in wayland_key_repeat_delay()
The stale pointer is later passed to g_source_destroy() at lines 495-497, 503-506, 543-546 and 709-712.

**Failure scenario.** User holds a key down; the active view is torn down mid-repeat (rofi_view_get_active() returns NULL) so wayland_key_repeat returns G_SOURCE_REMOVE at line 440 and GLib frees the GSource while self->repeat.source still points at it. The user releases the key: wayland_keyboard_key() reaches line 494-497 and calls g_source_destroy(self->repeat.source) on freed memory -> heap use-after-free (ASAN abort, or corruption of whatever GLib reallocated there).

**Proposed fix.** Set `self->repeat.source = NULL;` immediately before every G_SOURCE_REMOVE/FALSE return in both callbacks, or better, store the source id (guint) instead of the pointer and use g_source_remove()/g_clear_handle_id().

**Verifier.** self->repeat.source is an unreffed borrowed pointer from g_main_context_find_source_by_id (display.c:477, 520). display.c:429-431 clears it before G_SOURCE_REMOVE, but 438-441 (wayland_key_repeat, state==NULL) and 463-466 (wayland_key_repeat_delay, state==NULL) return G_SOURCE_REMOVE with the pointer still set; GLib then destroys/unrefs the source. It is later passed to g_source_destroy at 498, 509, 555 and 990 (wayland_keyboard_release). Cited destroy line numbers were a few lines off but all four sites exist. The 455-457 `return FALSE` path is effectively unreachable (every path that zeroes repeat.key also destroys and NULLs the source).

## `include/widgets/textbox.h:63` — `short cursor` overflows on >32767 chars, then feeds a negative offset to g_utf8_offset_to_pointer

- **kind** memory · **severity** high · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

textbox stores the cursor as `short cursor;` (include/widgets/textbox.h:63) while every producer computes it as an int/glong: textbox.c:433 `tb->cursor = MAX(0, MIN((int)g_utf8_strlen(tb->text,-1), tb->cursor));`, textbox.c:747 `tb->cursor = (int)g_utf8_strlen(tb->text,-1);`, and textbox_cursor() (textbox.c:644). Once the entry text exceeds SHRT_MAX characters the value wraps to a negative short. textbox_draw then does `cursor_offset = MIN(tb->cursor, g_utf8_strlen(text,-1));` (textbox.c:567) into a `size_t`, and passes it to `g_utf8_offset_to_pointer(text, cursor_offset)` (textbox.c:569). The size_t round-trips back to a negative glong in the callee, and g_utf8_offset_to_pointer treats negative offsets as 'walk backwards', so it walks ~32768 characters *before* the start of the heap buffer and returns a wild pointer, which is then subtracted from `text` and handed to pango_layout_get_cursor_pos (textbox.c:573).

**Failure scenario.** Paste a clipboard buffer of >32767 characters into the rofi input bar (source/view.c:998 -> rofi_view_handle_text -> source/view.c:1431 textbox_append_text -> textbox_insert/textbox_cursor per character). At the 32768th character `tb->cursor` becomes -32768; the next repaint calls g_utf8_offset_to_pointer(text, -32768), reading ~32KB before the allocation, and pango is given a garbage byte index -> out-of-bounds read / SIGSEGV.

**Proposed fix.** Widen the field to `int cursor;` (or `glong`) in include/widgets/textbox.h:63 and clamp explicitly, e.g. `tb->cursor = CLAMP(pos, 0, G_MAXINT)` in textbox_cursor(); additionally hard-clamp `cursor_offset` to >= 0 before the g_utf8_offset_to_pointer call at textbox.c:567-570.

**Verifier.** include/widgets/textbox.h:63 really is `short cursor;`. Producers assign int/glong values: textbox.c:433 `tb->cursor = MAX(0, MIN((int)g_utf8_strlen(tb->text,-1), tb->cursor));`, textbox.c:747 `tb->cursor = (int)g_utf8_strlen(tb->text,-1);`, textbox.c:643 `tb->cursor = MAX(0, MIN(length, pos));`. Nothing caps the text length. In textbox_draw the non-password branch is exactly textbox.c:567-570: `cursor_offset = MIN(tb->cursor, g_utf8_strlen(text,-1)); char *offset = g_utf8_offset_to_pointer(text, cursor_offset); cursor_offset = offset - text;` — a negative short survives the MIN (int -25536 < glong 40000), is stored in a size_t, and is converted back to a negative glong by g_utf8_offset_to_pointer's `glong offset` parameter, whose else-branch walks backwards from `str`. That is an out-of-bounds read before the g_malloc'd buffer, and the resulting `offset - text` is negative -> huge size_t handed to pango_layout_get_cursor_pos at textbox.c:573. Note the password branch (textbox.c:563-565) is NOT affected: MIN with strlen(text) picks the positive strlen. Downgraded from critical: it is an OOB *read* and needs a >32767-character entry.

## `source/helper.c:1357` — utf8_strncmp() writes out of bounds when a string is shorter than n characters

- **kind** memory · **severity** high · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

utf8_strncmp() normalizes both inputs and then does `*g_utf8_offset_to_pointer(na, n) = '\0';` (line 1357) and the same for nb (line 1358). g_utf8_offset_to_pointer does not bound-check: for a positive offset it is `while (offset--) p = g_utf8_next_char (p);` and g_utf8_skip['\0'] == 1, so it happily walks past the terminating NUL. Writing '\0' at that address is a heap buffer overflow. The bug is reachable in production from source/modes/combi.c:162 and :320, where the guard `(size_t)bang_len <= mode_name_len` is computed on the *un-normalized* mode name, while the write happens on the G_NORMALIZE_ALL_COMPOSE form, whose character count can be smaller (any decomposed sequence that composes). The project's own test suite triggers it unconditionally: test/helper-test.c:185 calls utf8_strncmp("aapno", "a", 4) and :186 calls utf8_strncmp("a", "aap€", 4) — the normalized "a" is a 2-byte allocation and the code writes at offset 4.

**Failure scenario.** Run the existing `helper test` binary: test/helper-test.c:185 -> utf8_strncmp("aapno","a",4) -> g_utf8_normalize("a") returns a 2-byte heap block; g_utf8_offset_to_pointer(na,4) returns na+4; `*(na+4)='\0'` writes 3 bytes past the end of the block. Under ASAN this is a heap-buffer-overflow WRITE; without ASAN it silently corrupts the glib slab. In production: run combi mode with a mode whose name contains a decomposable character and type `!<bang>` at full length.

**Proposed fix.** Clamp the offset to the actual normalized length before writing, e.g. `glong lna = g_utf8_strlen(na, -1); if ((size_t)lna > n) *g_utf8_offset_to_pointer(na, n) = '\0';` and likewise for nb; or replace the truncate-then-collate with a bounded per-character comparison loop. Also fix the callers in combi.c to compute the length limit from the same normalized form.

**Verifier.** source/helper.c:1354-1358 is exactly as claimed: both inputs are g_utf8_normalize'd (fresh g_malloc buffers) and then `*g_utf8_offset_to_pointer(na, n) = '\0';` / same for nb, with no bound on n vs the normalized length. glib's g_utf8_offset_to_pointer for positive offset is an unconditional `while (offset--) s = g_utf8_next_char(s)` and g_utf8_skip['\0']==1, so it walks past the terminator. test/helper-test.c:185-186 do call utf8_strncmp("aapno","a",4) and utf8_strncmp("a","aap€",4): the normalized "a" is a 2-byte block and the write lands at offset 4 — 3 bytes past the end. Downgraded from critical only because it is a single '\0' byte at a bounded offset, not attacker-controlled data.

## `source/modes/filebrowser.c:301` — collate_key left uninitialized when sorting by time, then g_free()d

- **kind** memory · **severity** high · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

fb_resize_array() (filebrowser.c:233-239) grows pd->array with g_realloc(), which returns *uninitialized* memory. In get_file_browser() the per-entry init block sets collate_key only on the FB_SORT_NAME branch (line 301-302 for DT_REG/DT_DIR, line 326-328 for DT_LNK). Under FB_SORT_TIME the field is never assigned for any real file entry (only the ".." UP entry sets collate_key = NULL at line 264). free_list() then unconditionally calls g_free(fb->collate_key) at line 137 on that indeterminate pointer. compare_name() at line 156 also reads it if directories_first ever falls through.

**Failure scenario.** Put `filebrowser { sorting-method: "mtime"; }` in the theme/config and open the filebrowser mode, then navigate into any directory (which calls free_list() at line 505/527/563). g_free() is invoked on a garbage pointer read out of freshly realloc'd heap -> glibc "free(): invalid pointer" abort or arbitrary heap corruption.

**Proposed fix.** Zero the whole FBFile before filling it (e.g. memset(&pd->array[pd->array_length], 0, sizeof(FBFile)) right after fb_resize_array()), or set both collate_key = NULL and time = -1 unconditionally in every branch, not only in the branch matching the active sorting method.

**Verifier.** filebrowser.c:232-238 fb_resize_array grows pd->array with g_realloc (uninitialized). In get_file_browser, set_collate_key is called ONLY under `if (file_browser_config.sorting_method == FB_SORT_NAME)` at lines 302 (DT_REG/DT_DIR) and 327 (DT_LNK); the FB_SORT_TIME branch calls set_time instead and never touches collate_key. Only the ".." UP entry sets collate_key=NULL (line 264). free_list unconditionally does g_free(fb->collate_key) at line 137, and is reached from file_browser_mode_result at lines 505/527/563 and from mode_destroy. FB_SORT_TIME is reachable via theme `sorting-method: "mtime"|"atime"|"ctime"` (filebrowser.c:381-389). g_free on an indeterminate pointer read out of realloc'd heap.



---

# MEDIUM

## `.build.yml:32` — sourcehut CI fetches upstream rofi from sr.ht, so it never builds this fork

- **kind** build · **severity** medium · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

`sources:` lists `https://sr.ht/~qball/rofi/` and every task then operates on the `rofi/` checkout (`cd rofi` line 35, `ninja -C rofi/builddir` lines 38/40/45). Nothing in the file references this repository or the new origin https://github.com/orpheus497/sofi.git. For a fork this is worse than a dead CI: it reports green while testing somebody else's code.

**Failure scenario.** A commit lands in the fork that breaks the build; the sourcehut job clones upstream qball/rofi instead, compiles it successfully, and reports success - the regression ships unnoticed.

**Proposed fix.** Point `sources:` at the fork's clone URL and rename the checkout directory used by every task (`cd sofi`, `ninja -C sofi/builddir`).

**Verifier.** .build.yml:31-32 is `sources:` / `  - https://sr.ht/~qball/rofi/`; line 35 `cd rofi`, lines 38/40/42/45 `ninja -C rofi/builddir...`, line 47 artifact under `rofi/builddir/`. Nothing in the file names orpheus497/sofi. Downgraded from critical: this manifest is inert for a GitHub-hosted fork (sourcehut only executes .build.yml for repos pushed to sr.ht or manifests submitted to builds.sr.ht), so the 'reports green on someone else's code' outcome requires someone to first mirror the fork to sr.ht. It is stale config that must be rewritten during the rebrand, not an active false-green today.

## `.build.yml:47` — sourcehut build declares a hardcoded artifact for a version that no longer exists, and pulls the source from upstream

- **kind** build · **severity** medium · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Line 47 declares `artifacts: - rofi/builddir/meson-dist/rofi-1.7.8-dev.tar.xz`, but meson.build line 2 sets `version: '2.0.0-dev'`, so the `dist` task at line 44-45 produces `rofi-2.0.0-dev.tar.xz` and the declared artifact is never present. Separately, line 32 sets `sources: - https://sr.ht/~qball/rofi/` — the build clones *upstream's* repo, not this fork, so lines 34-45 (`cd rofi`, `ninja -C rofi/builddir`) would build and test upstream rofi even when triggered from sofi. Line 43's `grep -c warnings` also matches the word `warnings` rather than doxygen's actual `warning:` prefix (contrast .github/actions/doxycheck/action.yml line 12, which correctly greps `warning:`), so the doxygen gate is effectively dead.

**Failure scenario.** A sourcehut build succeeds but publishes nothing (artifact path missing), and everything it validated came from davatorium/rofi rather than orpheus497/sofi.

**Proposed fix.** Point `sources` at https://github.com/orpheus497/sofi.git, change the directory references from `rofi` to `sofi`, replace the artifact line with a glob or a version-derived name, and change line 43's pattern to `warning:`.

**Verifier.** All three sub-claims verified. .build.yml:47 declares `- rofi/builddir/meson-dist/rofi-1.7.8-dev.tar.xz` while meson.build:2 is `version: '2.0.0-dev'`, so the `dist` task at lines 44-45 emits rofi-2.0.0-dev.tar.xz and the declared path never exists. .build.yml:32 is `- https://sr.ht/~qball/rofi/`, i.e. upstream's repo, which lines 35/38/40/42/45 then cd/ninja into -- so a sofi-triggered sourcehut build validates upstream code. Line 43 is `if [ $(grep -c warnings doxygen.log) -gt 0 ]`, contrasted against .github/actions/doxycheck/action.yml:12 `if [[ "$(grep -c warning: builddir/doxygen.log)" != 0 ]]`; doxygen emits `warning:`, so the sourcehut gate is effectively dead.

## `meson.build:319` — Build unconditionally consumes a staging protocol newer than the declared wayland-protocols minimum

- **kind** build · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

meson.build:106 declares `dependency('wayland-protocols', version: '>= 1.17')`, but line 319 unconditionally adds `wayland_sys_protocols_dir + '/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml'` to the scanner inputs. ext-foreign-toplevel-list-v1 first shipped in wayland-protocols 1.32. The file immediately below it (staging/cursor-shape, lines 327-333) *is* correctly gated behind `version_compare('>=1.32')`, which shows the gate was understood and simply not applied to the ext-foreign-toplevel-list entry. source/modes/wayland-window.c:51 includes the generated header unconditionally.

**Failure scenario.** On a distro shipping wayland-protocols between 1.17 and 1.31 (Debian bookworm has 1.31, FreeBSD ports lagged similarly), `meson setup build` satisfies the >=1.17 check and then fails at configure time with 'File .../staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml does not exist' — an opaque failure with no hint that the dependency version is the cause.

**Proposed fix.** Raise the dependency at meson.build:106 to '>= 1.32', or gate line 319 and the corresponding code in source/modes/wayland-window.c behind a HAVE_EXT_FOREIGN_TOPLEVEL_LIST feature macro the way HAVE_WAYLAND_CURSOR_SHAPE is handled.

**Verifier.** meson.build:106 `dependency('wayland-protocols', version: '>= 1.17', ...)`; meson.build:319 adds staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml to the unconditional `protocols` files() list, while meson.build:327-331 gates staging/cursor-shape behind version_compare('>=1.32'). ext-foreign-toplevel-list-v1 is a 1.32-era staging protocol, and source/modes/wayland-window.c:51 includes its generated header unconditionally, so the declared minimum is wrong and configure fails opaquely on 1.17-1.31.

## `source/wayland/display.c:208` — Fixed shm object name with O_CREAT|O_EXCL: concurrent instances collide and a crash leaves a permanently poisoning segment

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 208-215:
  gchar *shm_name = "/rofi-wayland-surface";
  fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
  shm_unlink(shm_name);
  if (fd < 0) { ... }
The name is a compile-time constant shared by every process on the machine. There is no retry on EEXIST and no fallback (no memfd_create, no O_TMPFILE, no randomised name loop). Because the unlink is unconditional and immediate the window is small, but it is not zero, and the segment survives SIGKILL/SIGSEGV inside that window with no cleanup. There is also no PID/user disambiguation, so two different users on the same machine (same /dev/shm) fight over the same name and the first one's 0600 file makes the second fail with EACCES rather than EEXIST.

**Failure scenario.** Two sofi instances launched simultaneously (e.g. a keybind pressed twice, or two Waybar menus): both reach shm_open before either reaches shm_unlink; the loser gets EEXIST, display_buffer_pool_new returns NULL and (per pool-new-null-not-checked-by-caller) the process segfaults. Separately, `kill -9` landing between lines 209 and 210 leaves /dev/shm/rofi-wayland-surface behind forever, and every subsequent launch fails with EEXIST.

**Proposed fix.** Prefer memfd_create("sofi-wayland-surface", MFD_CLOEXEC) on Linux; otherwise generate a unique name (`g_strdup_printf("/sofi-%d-%u", getpid(), g_random_int())`) and retry on EEXIST, still unlinking immediately after a successful open. Free the generated name.

**Verifier.** display.c:208-209 verbatim: `gchar *shm_name = "/rofi-wayland-surface"; fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);` with shm_unlink at 210 and no retry/randomisation/memfd fallback anywhere in the function (189-257). The concurrent-launch and stale-object windows are real. One sub-claim is wrong: with O_CREAT|O_EXCL a pre-existing object owned by another user fails with EEXIST, not EACCES.

## `source/wayland/display.c:1223` — wl_seat.capabilities tests the POINTER bit to decide whether to release the KEYBOARD

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 1213-1226:
  if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && (self->keyboard == NULL)) { ...get keyboard... }
  else if ((!(capabilities & WL_SEAT_CAPABILITY_POINTER)) && (self->keyboard != NULL)) { wayland_keyboard_release(self); }
The else-branch must test WL_SEAT_CAPABILITY_KEYBOARD. As written, a seat that loses its pointer (but keeps its keyboard) destroys the wl_keyboard; and a seat that genuinely loses its keyboard while keeping a pointer keeps a stale wl_keyboard proxy that the compositor considers gone. The keyboard branch also leaks self->text_input: if capabilities fires again with KEYBOARD set after a release, line 1218 overwrites self->text_input without destroying the old zwp_text_input_v3.

**Failure scenario.** A USB mouse is unplugged from a seat that also has a keyboard. The compositor sends capabilities with KEYBOARD set and POINTER cleared. The first `if` is false (self->keyboard != NULL), the else-if is true, and wayland_keyboard_release() destroys the working keyboard -> sofi stops accepting all keyboard input and the user can only close it by killing it.

**Proposed fix.** Change line 1223 to `else if ((!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD)) && (self->keyboard != NULL))`, and destroy self->text_input inside wayland_keyboard_release().

**Verifier.** display.c:1223-1226 is `else if ((!(capabilities & WL_SEAT_CAPABILITY_POINTER)) && (self->keyboard != NULL)) { wayland_keyboard_release(self); }` — the keyboard branch tests the POINTER bit, identical to the pointer branch at 1231-1233. The text_input sub-claim also holds: wayland_keyboard_release (981-995) does not touch self->text_input, so a re-acquire at 1218 overwrites it without zwp_text_input_v3_destroy.

## `source/wayland/display.c:1791` — wayland_display_late_setup() dereferences layer_shell/compositor without NULL checks and always returns TRUE

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Line 1789 `wl_compositor_create_surface(wayland->compositor)` and line 1791-1792 `zwlr_layer_shell_v1_get_layer_surface(wayland->layer_shell, wayland->surface, wlo, layer, "rofi")` have no NULL guards, yet wayland_registry_handle_global_remove() explicitly sets both to NULL (lines 1573-1576 for the compositor, 1583-1586 for the layer shell). The function returns TRUE unconditionally at line 1824 regardless of what happened, and its second caller — wayland_layer_shell_surface_closed() at line 1660 — ignores the result entirely. wl_display_roundtrip() at lines 1747, 1763, 1822 and 2016 is likewise never checked for -1.

**Failure scenario.** The compositor withdraws the zwlr_layer_shell_v1 global (a compositor reload / a nested compositor restarting) while sofi is running. The layer surface is closed, wayland_layer_shell_surface_closed() at line 1656-1660 destroys the surface and calls wayland_display_late_setup(), which calls zwlr_layer_shell_v1_get_layer_surface(NULL, ...) -> wl_proxy_marshal_flags on a NULL proxy -> SIGSEGV.

**Proposed fix.** Return FALSE early from wayland_display_late_setup() when wayland->compositor or wayland->layer_shell is NULL; have wayland_layer_shell_surface_closed() check the result and quit the main loop cleanly on failure; check wl_display_roundtrip() return values.

**Verifier.** wayland_display_late_setup: display.c:1775 `wl_compositor_create_surface(wayland->compositor)` and 1791-1792 `zwlr_layer_shell_v1_get_layer_surface(wayland->layer_shell, ...)` with no NULL guards, unconditional `return TRUE` at 1825, and wayland_layer_shell_surface_closed (1650-1670) calls it ignoring the result. global_remove does NULL both pointers (1556-1557 compositor, 1566-1567 layer_shell). The claim's line numbers for the compositor call (1789) and for the global_remove cases (1573-1586) are off by ~10-15 lines but the code is exactly as described.

## `script/get_git_rev.sh:8` — `.git` is tested with -d, so git worktrees and submodules silently lose version info

- **kind** correctness · **severity** medium · **verdict** n/a · **domain** User-facing and non-C surface: root docs, doc/ m

`if [ -d "${DIR}/.git/" ] && [ -n "${GIT}" ]`. In a linked worktree (`git worktree add`) and in a submodule checkout, `.git` is a regular *file* containing a `gitdir:` pointer, not a directory. The `-d` test is false, so the else branch at line 16 writes `#undef GIT_VERSION` even though git information is fully available. Additionally lines 5-6 capture GIT and SED but only GIT is checked at line 8; if sed is missing, line 11-12's `| ${SED} -e ...` becomes a pipe to the empty string, and the resulting `${REV}`/`${BRTG}` are empty, producing `#define GIT_VERSION " ()"` with no error.

**Failure scenario.** A packager or developer builds from `git worktree add ../sofi-rel next`. `sofi -v` reports the plain version with no git revision, making it impossible to tell which commit a bug report was built from — the exact thing the script exists to provide.

**Proposed fix.** Change line 8 to `if [ -e "${DIR}/.git" ] && [ -n "${GIT}" ]`, and add a `[ -n "${SED}" ]` guard alongside it (or drop sed entirely in favour of shell parameter expansion, which handles both substitutions).

## `script/rofi-theme-selector:40` — mktemp file is leaked and the file actually written is an unprotected predictable path

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`TMP_CONFIG_FILE=$(${MKTEMP}).rasi` calls mktemp, which atomically CREATES a file (e.g. /tmp/tmp.AbC123), then appends `.rasi` to the *name*. Every subsequent operation (line 118 `${ROFI} -dump-config > "${TMP_CONFIG_FILE}"`, line 120 `sed -i`, line 144 `-config "${TMP_CONFIG_FILE}"`, line 241 `rm --`) targets /tmp/tmp.AbC123.rasi, a path mktemp never created and never reserved. Two consequences: (a) /tmp/tmp.AbC123 is never removed — every invocation leaks one empty file in /tmp forever; (b) the .rasi path has no O_EXCL guarantee, so on a shared /tmp another user can pre-create it as a symlink and the `>` redirection at line 118 truncates/writes through it. There is also no `trap ... EXIT INT TERM`, so Ctrl-C anywhere inside select_theme (line 148 `while true` loop) skips line 241 and leaks the .rasi file too.

**Failure scenario.** On a multi-user host with a world-writable /tmp, attacker pre-creates /tmp/tmp.AbC123.rasi as a symlink to ~victim/.bashrc after observing the mktemp name; victim runs rofi-theme-selector; line 118 `rofi -dump-config >` truncates and overwrites ~/.bashrc. Benign case: every run leaves one zero-byte /tmp/tmp.XXXX behind.

**Proposed fix.** Use a directory: `TMPDIR_RUN=$(${MKTEMP} -d) || exit 1; TMP_CONFIG_FILE="${TMPDIR_RUN}/config.rasi"` and add `trap 'rm -rf -- "${TMPDIR_RUN}"' EXIT INT TERM` immediately after, replacing the bare `rm --` on line 241.

**Verifier.** script/rofi-theme-selector:40 is literally `TMP_CONFIG_FILE=$(${MKTEMP}).rasi`. I verified `mktemp` creates a real 0600 file (`$(mktemp)` -> /tmp/tmp.8E93rp2OZ6, mode -rw-------), and every consumer (line 118 `${ROFI} -dump-config > "${TMP_CONFIG_FILE}"`, line 120 `${SED} -i`, line 144 `-config "${TMP_CONFIG_FILE}"`, line 241 `rm --`) uses the .rasi name, so the mktemp-created file is orphaned on every run. `grep -n 'trap|set -e|set -u' script/rofi-theme-selector` returns nothing (rc=1), so there is no cleanup on interrupt either. The symlink angle is real but narrower than stated: the mktemp basename is not predictable in advance, so an attacker must win a race by watching /tmp between line 40 and line 118 on a shared host. Leak is certain; the write-through attack is conditional, hence medium not high.

## `source/helper.c:617` — -replace busy-waits forever if the running instance ignores SIGTERM

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

create_pid_file() — reached from source/rofi.c:1311 with kill_running=TRUE when -replace is given — sends SIGTERM to the pid read from the lock file and then loops `while (1) { flock(...); if (ok) break; g_usleep(100); }` with no attempt limit, no timeout and no escalation to SIGKILL. It also g_usleeps only 100 microseconds per iteration, so it spins the CPU.

**Failure scenario.** The previous instance is stopped (SIGSTOP), wedged on a blocking X/Wayland roundtrip, or blocking SIGTERM: `rofi -replace -show drun` never returns, pinning a core at 100% with a 100us flock loop, and the user has no way out but SIGKILL. A stale pid that has been recycled by an unrelated process yields the same hang after signalling the wrong process.

**Proposed fix.** Bound the loop (e.g. 50 iterations of 100ms), escalate to SIGKILL, then fail with a clear error instead of looping forever.

**Verifier.** source/helper.c:608-625: after `kill(pid, SIGTERM)` (:615) the code runs `while (1) { retv = flock(fd, LOCK_EX|LOCK_NB); if (retv == 0) break; g_usleep(100); }` at :617-623 — no attempt limit, no timeout, no SIGKILL escalation, and g_usleep(100) is 100 microseconds so it spins. Reached from source/rofi.c:1306-1311 with kill_running=TRUE when -replace is passed. If the target ignores/blocks SIGTERM or is stopped, rofi hangs at 100% CPU.

## `source/helper.c:615` — create_pid_file() sends SIGTERM to its own process group and then spins forever on a corrupt pidfile

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

When the flock fails and kill_running is TRUE, the code reads the pidfile into a 64-byte buffer and does `pid_t pid = g_ascii_strtoll(buffer, NULL, 0); kill(pid, SIGTERM);` (lines 615-616). g_ascii_strtoll returns 0 for non-numeric content, and kill(0, SIGTERM) signals *every process in the caller's process group* — including rofi itself and, when launched from a terminal or a WM keybinding, its siblings. A negative value likewise signals a whole foreign process group. The return value of kill() is never checked. The subsequent `while (1) { retv = flock(...); if (retv == 0) break; g_usleep(100); }` (lines 617-623) has no timeout and no bail-out, so if the lock holder is a stale/foreign process that ignores or never receives SIGTERM, rofi busy-spins forever at 100us intervals with no UI.

**Failure scenario.** `printf 'garbage' > /tmp/rofi.pid` (or let a truncated write leave a non-numeric pidfile), then start rofi with `-replace` while another instance holds the lock. kill(0, SIGTERM) terminates the caller's whole process group; if anything survives holding the lock, the loop at line 617 never exits and rofi hangs with 100% of one core.

**Proposed fix.** Validate the parsed pid (`if (pid <= 1) { remove_pid_file(fd); return -1; }`), check kill()'s return value, and bound the retry loop with a deadline (e.g. 2s) after which it gives up and returns -1.

**Verifier.** source/helper.c:612-623 read verbatim: read() into buffer[64], `if (l > 1) { pid_t pid = g_ascii_strtoll(buffer, NULL, 0); kill(pid, SIGTERM); while (1) { retv = flock(...); if (retv == 0) break; g_usleep(100); } }`. kill()'s return is never checked, a non-numeric pidfile yields pid 0 (POSIX: signal the caller's whole process group), and the retry loop has no iteration cap, timeout, or bail-out. Downgraded to medium because it needs a corrupt/foreign pidfile plus -replace to reach.

## `source/helper.c:638` — Unchecked write() in create_pid_file() turns a write error into an infinite loop

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`ssize_t l = 0; while (l < length) { l += write(fd, &buffer[l], length - l); }` (lines 636-639). write() returning -1 (EINTR, ENOSPC, EIO, EDQUOT) makes `l` go negative rather than terminating, so the loop re-issues write() at an increasingly negative offset into `buffer` — `&buffer[l]` with l < 0 is itself out-of-bounds pointer arithmetic — and never converges. There is no errno check anywhere in the loop.

**Failure scenario.** Place the pidfile on a full filesystem or hit the user's disk quota: ftruncate succeeds, the first write() returns -1/ENOSPC, l becomes -1, the loop condition -1 < length stays true forever, and the process pins a CPU inside create_pid_file() before the UI ever comes up. An EINTR from a signal during the write has the same effect.

**Proposed fix.** `ssize_t w = write(fd, &buffer[l], length - l); if (w < 0) { if (errno == EINTR) continue; g_warning(...); break; } l += w;`

**Verifier.** source/helper.c:636-639: `ssize_t l = 0; while (l < length) { l += write(fd, &buffer[l], length - l); }` with no errno/-1 check. A single write() failure makes l negative, the loop condition stays true forever and `&buffer[l]` is out-of-bounds pointer arithmetic. Real, but the write is ~6 bytes to a pidfile, so practical reachability (EINTR/ENOSPC) is narrow.

## `source/helper.c:870` — rofi_force_utf8(data, -1) returns NULL for every valid UTF-8 input

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

The fast path is `if (g_utf8_validate(data, length, &end)) return g_memdup2(data, length + 1);` (lines 869-871). g_memdup2 returns NULL when byte_size is 0, so with the documented-and-used `length == -1` (meaning NUL-terminated) the expression is g_memdup2(data, 0) == NULL. Callers pass -1: source/modes/filebrowser.c:290 and :315, source/modes/recursivebrowser.c:227 and :258. The slow (invalid-UTF-8) path happens to survive a negative length only by accident, because g_utf8_validate treats any negative max_len as NUL-terminated and g_string_append_len re-derives the length via strlen for a negative len. The function additionally assumes data[length] is a NUL when a real length is passed (it copies length+1 bytes), which is not guaranteed by the dmenu callers (source/modes/dmenu.c:155, :190).

**Failure scenario.** Set a non-UTF-8 filename encoding (e.g. `G_FILENAME_ENCODING=ISO-8859-1` or a latin1 locale) and open a directory in filebrowser containing a UTF-8-named file: g_filename_to_utf8 fails, the fallback `rofi_force_utf8(rd->d_name, -1)` returns NULL, and pd->array[i].name is NULL — later dereferenced when the entry is displayed/sorted.

**Proposed fix.** Normalize the length at entry: `if (length < 0) length = (ssize_t)strlen(data);` before the validate/memdup, and use `g_strndup(data, length)` instead of g_memdup2(data, length+1) so the result is always NUL-terminated regardless of what byte follows.

**Verifier.** source/helper.c:862-871: `if (g_utf8_validate(data, length, &end)) return g_memdup2(data, length + 1);`. With length == -1 the argument is 0, and g_memdup2 is documented (and implemented) to return NULL when byte_size is 0, so rofi_force_utf8(valid_utf8, -1) returns NULL. Callers passing -1 confirmed at source/modes/filebrowser.c:290,315 and source/modes/recursivebrowser.c:227,258, and the result is stored unchecked into .name. Downgraded to medium: the -1 call sites are only the fallback after g_filename_to_utf8 fails, which usually means the name is invalid UTF-8 and takes the slow path instead. The dmenu sub-claim about data[length] is weak — dmenu.c:146-155/181-190 guarantee a NUL at data_len.

## `source/helper.c:709` — config_sanity_check() rejects a matching list that enables all five matchers (off-by-one)

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Inside the matcher-parsing loop, after successfully recording a matcher the code does `matching_method_index++; NUMMatchingMethodEnabled = matching_method_index; if (matching_method_index == MM_NUM_MATCHERS) { found_error = 1; g_string_append_printf(msg, "...to many matching options enabled..."); }` (lines 707-715). MM_NUM_MATCHERS is 5 (include/settings.h:44) and MatchingMethodEnabled is a 5-element array (line 60), so index 5 is the *correct* terminal state after all five have been stored — no overflow is possible, and the outer loop conditions at lines 699 and 703 already prevent one. The check therefore fires on a perfectly valid configuration. (The message also reads "to many" instead of "too many".)

**Failure scenario.** Put `matching: "normal,regex,glob,fuzzy,prefix";` in config.rasi (or pass `-matching normal,regex,glob,fuzzy,prefix`), which is the documented way to enable cycling through every matcher. config_sanity_check returns TRUE, an error dialog reading 'config.matching = ... to many matching options enabled' is shown, and rofi refuses the configuration.

**Proposed fix.** Move the guard so it only fires when a *sixth* entry is requested: check `matching_method_index >= MM_NUM_MATCHERS` at the top of the outer loop and report the error there, or simply drop the block at lines 709-715 since the loop bounds already enforce the array size.

**Verifier.** source/helper.c:705-715 read exactly as claimed: after storing a matcher it does matching_method_index++; NUMMatchingMethodEnabled = matching_method_index; and `if (matching_method_index == MM_NUM_MATCHERS) { found_error = 1; g_string_append_printf(msg, "...= %s to many matching options enabled.\n", ...); }`. MM_NUM_MATCHERS is 5 (include/settings.h:44) and MatchingMethodEnabled is int[MM_NUM_MATCHERS] (source/helper.c:60), so index 5 is the correct terminal state; the loop guards at :699 and :703 already stop before overflow. Enabling all five matchers therefore reports a spurious configuration error. The 'to many' typo is on line 712.

## `source/history.c:244` — History is rewritten by truncating the live file in place — a crash mid-write destroys it

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

history_set() reads the whole file, mutates the in-memory list, then does `fd = fopen(filename, "w")` (line 244), which truncates the real history file to zero, and streams the entries back with unchecked fprintf() (line 76). history_remove() does the same at line 303. There is no temp-file-plus-rename, no fflush/fsync, no fprintf/ferror check, and no advisory locking. Between the truncate and the last fprintf the user's entire history is gone from disk. Two rofi instances doing this concurrently interleave arbitrarily: one truncates while the other has already read, and the loser's entries vanish. The value written is also unescaped (`fprintf(fd, "%ld %s\n", ...)`, line 76), so an entry containing a newline injects a fake record on the next read.

**Failure scenario.** Launch drun, select an app, and kill rofi (or lose power / hit ENOSPC) between line 244 and the fclose at 251 — ~/.cache/rofi3.druncache is now empty or half-written and all usage counts are lost. Or: bind rofi -show drun and rofi -show run to two keys, press both; whichever writes second silently discards the other's update.

**Proposed fix.** Write to `filename.tmp` in the same directory, fflush + fsync, fclose with a checked return, then g_rename() over the original (rename is atomic within a filesystem). Take an flock on the history file for the whole read-modify-write. Escape or reject entries containing '\n'.

**Verifier.** source/history.c:244 `fd = fopen(filename, "w")` and :303 `fd = g_fopen(filename, "w")` truncate the live file after the in-memory list is built; __history_write_element_list writes with an unchecked `fprintf(fd, "%ld %s\n", ...)` at line 76; only fclose's return is checked (247-253). No temp file + rename, no fsync, no locking, no escaping of the entry text. Downgraded to medium: the file is a cache, and the loss window is a few hundred microseconds.

## `source/modes/combi.c:158` — '!' prefix enables every mode whose name it prefixes, but result dispatch always picks the first one

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

combi_preprocess_input (line 316-329) *disables* only the modes whose name the bang token does not prefix, so an ambiguous prefix leaves several modes enabled and their rows visible. combi_mode_result (line 158-166) resolves the same prefix with a `break` on the first match and then dispatches to that single mode with `selected_line - pd->starts[switcher]` (lines 172 and 178). When the selected row actually belongs to the second matching mode, the index is computed against the wrong mode's base and handed to the wrong mode. Note the non-bang path at line 188-193 does this correctly by locating the mode from the row range.

**Failure scenario.** `sofi -show combi -combi-modes "window,windowcd,drun"`, type `!w`, which matches both "window" and "windowcd". Rows from both modes are shown. Select any windowcd row (global index >= starts[1]) and press Enter: mode_result is called on `window` with index `selected_line - starts[0]` = selected_line, which is >= window's entry count -> out-of-bounds read of window mode's client array and an attempt to focus a garbage window. Same with `-combi-modes "run,recursivebrowser"` and `!r`.

**Proposed fix.** In combi_mode_result, do not resolve the mode from the bang token. Locate the owning mode from selected_line using the same starts[i]/lengths[i] range scan as line 188, and only use the bang token to strip the prefix from the input string.

**Verifier.** combi.c:150-166: the bang resolver loops over switchers and `break`s on the first prefix match, then dispatches at lines 172/178 with `selected_line - pd->starts[switcher]`. combi_preprocess_input (combi.c:302-334) only sets `disable = TRUE` on modes that do NOT match, so an ambiguous prefix leaves several modes enabled and combi_mode_match (lines 199-208) keeps rows from all of them visible. The non-bang path at combi.c:188-193 correctly locates the mode by row range. So selecting a row owned by the second matching mode dispatches to the first with an index past its entry count.

## `source/modes/combi.c:149` — combi_mode_result dereferences *input without a NULL check

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

process_result in source/rofi.c:241 does `char *input = g_strdup(rofi_view_get_user_input(state));` and rofi_view_get_user_input (source/view.c:409-414) returns NULL whenever state->text is NULL, so g_strdup yields NULL and *input is NULL. combi_mode_result's very first statement is `if (input[0][0] == '!')` — an unguarded load through a NULL pointer. Every sibling mode guards this: drun.c:1392 `*input != NULL`, run.c:499, script.c:410, ssh.c:632; combi's own combi_preprocess_input guards it at line 306 (`input != NULL && input[0] == '!'`).

**Failure scenario.** Use a theme whose `window` children list omits the entry/inputbar textbox (state->text stays NULL), then run `sofi -show combi` and press Enter or any key that produces a mode result: the first line of combi_mode_result segfaults on input[0][0].

**Proposed fix.** Change line 149 to `if (input != NULL && input[0] != NULL && input[0][0] == '!')`, matching combi_preprocess_input.

**Verifier.** combi.c:149 is `if (input[0][0] == '!')`, the first statement of combi_mode_result. source/rofi.c:241 `char *input = g_strdup(rofi_view_get_user_input(state));` and source/view.c:409-414 returns NULL when state->text is NULL, so input can be NULL; rofi.c:244-246 explicitly handles `state->text == NULL`, confirming that state is possible. combi.c:306 guards the same value in combi_preprocess_input. Requires a theme with no entry textbox, so exposure is narrow.

## `source/modes/combi.c:195` — combi_mode_result indexes switchers[0] without checking num_switchers

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

When no row range matched, the fallback is `if ((mretv & MENU_CUSTOM_INPUT)) return mode_result(pd->switchers[0].mode, mretv, input, selected_line);` (lines 195-197). combi_mode_parse_switchers can legitimately end with num_switchers == 0 (empty combi-modes string, or every token rejected at line 94), and combi_mode_init then leaves pd->switchers == NULL and g_malloc0(0) == NULL for starts/lengths (lines 118-119).

**Failure scenario.** `sofi -show combi -combi-modes ""` (or a list of only bogus mode names, which just warns and continues). The empty list renders, the user types anything and presses Enter -> MENU_CUSTOM_INPUT -> pd->switchers[0].mode dereferences NULL -> segfault.

**Proposed fix.** Guard with `if ((mretv & MENU_CUSTOM_INPUT) && pd->num_switchers > 0)`, and have combi_mode_init fail (return FALSE) or warn loudly when no switcher was resolved.

**Verifier.** combi.c:195-197 `if ((mretv & MENU_CUSTOM_INPUT)) { return mode_result(pd->switchers[0].mode, mretv, input, selected_line); }` with no num_switchers check. combi_mode_parse_switchers (combi.c:62-98) can end with num_switchers == 0 (empty combi-modes, or every token rejected at line 94 which only warns and continues), and combi_mode_init leaves pd->switchers NULL while g_malloc0(0) at lines 118-119 also yields NULL.

## `source/modes/dmenu.c:611` — -input expands the path into estr but then opens the unexpanded str

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

Both branches compute the expanded path and then ignore it. Async: `char *estr = rofi_expand_path(str); pd->fd = open(str, O_RDONLY|O_NONBLOCK|O_CLOEXEC);` (lines 612-613). Sync: `char *estr = rofi_expand_path(str); pd->fd_file = fopen(str, "r");` (lines 643-644). estr is used only to build the error message and is then freed. Every other call site in the tree uses the expanded value (filebrowser.c:545-546, ssh.c:554-555, script.c:693).

**Failure scenario.** `sofi -dmenu -input '~/list.txt'` — open("~/list.txt") fails with ENOENT and the user gets "Failed to open file: /home/user/list.txt: No such file or directory", naming a path that does exist. `~user/...` and any other expansion form fail identically.

**Proposed fix.** Open estr, not str, in both branches.

**Verifier.** dmenu.c:612-613 `char *estr = rofi_expand_path(str); pd->fd = open(str, O_RDONLY|O_NONBLOCK|O_CLOEXEC);` and dmenu.c:643-644 `char *estr = rofi_expand_path(str); pd->fd_file = fopen(str, "r");`. estr is used only in the g_markup_printf_escaped error message (lines 616, 647) and then g_free'd (620/623, 651/654). Every other caller uses the expanded value (ssh.c:554, script.c and filebrowser.c).

## `source/modes/dmenu.c:337` — async reader emits a partial line as a complete entry after a 250 ms input stall

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

read_input_thread's select() timeout branch does `if (nread > 0) { line[nread] = '\0'; read_add_block(pd, &block, line, nread); nread = 0; }` (lines 337-343). nread > 0 at that point means bytes are buffered with no separator seen yet — a partial line. It is pushed as a finished entry and the buffer is reset, so the rest of the line becomes a second entry.

**Failure scenario.** `{ printf 'hello'; sleep 1; printf ' world\n'; } | sofi -dmenu` produces two rows, "hello" and " world", instead of one row "hello world". Any slow producer (a network fetch, a script that computes rows incrementally) silently fragments its output.

**Proposed fix.** Only flush the partial buffer on EOF (the `readbytes <= 0` branch at line 322 already does this correctly). On timeout, flush only the already-completed entries accumulated in `block` and leave the partial `line`/nread untouched.

**Verifier.** dmenu.c:335-343: the select() timeout branch (250 ms tv at dmenu.c:275) does `if (nread > 0) { line[nread] = '\0'; read_add_block(pd, &block, line, nread); nread = 0; }` and then pushes the block. At that point nread holds bytes with no separator seen (the inner loop at 301-317 consumes and memmoves everything up to the last separator), so a partial line is committed as a finished entry and the remainder becomes a second entry.

## `source/modes/drun.c:1226` — gio-launch theme option is only read when the desktop cache misses

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

pd->disable_giolaunch is assigned exclusively inside the `if (drun_read_cache(pd, cache_file))` branch of get_apps (lines 1226-1230), i.e. only when the cache could NOT be used. On a cache hit the else branch at line 1240 just logs, and disable_giolaunch keeps its g_malloc0 value of FALSE. exec_cmd_entry reads it at line 421.

**Failure scenario.** Set both `drun-use-desktop-cache: true` and `drun { gio-launch: false; }`. On the first run the cache is written and gio-launch is honoured; on every subsequent run the cache hits, disable_giolaunch stays FALSE, and applications are launched through GAppInfo anyway — the user's explicit opt-out silently stops working after the first launch.

**Proposed fix.** Move the disable_giolaunch lookup out of the if/else so it runs on both the cache-hit and cache-miss paths.

**Verifier.** get_apps: drun.c:1226-1230 `pd->disable_giolaunch = FALSE; p = rofi_theme_find_property(wid, P_BOOLEAN, "gio-launch", TRUE); if (...) pd->disable_giolaunch = TRUE;` sits inside the `if (drun_read_cache(pd, cache_file))` (cache-miss) branch; the cache-hit else branch at drun.c:1239-1241 only calls g_debug. pd comes from g_malloc0 so the flag stays FALSE, and exec_cmd_entry reads it at drun.c:421. The user's `gio-launch: false` is honoured only on the run that writes the cache.

## `source/modes/drun.c:839` — walk_dir follows symlinked directories recursively with no loop or depth guard

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

walk_dir promotes DT_LNK/DT_UNKNOWN entries to DT_DIR via stat() (lines 839-848) and then recurses unconditionally when `recursive` is set (lines 857-860). There is no visited-inode set, no depth cap, and DIR handles stay open across the whole recursion. It is called with recursive=TRUE for ~/.local/share/applications (line 1199) and for every XDG system data dir (line 1220). The sibling scanner in recursivebrowser.c at least keeps a dirs_scanned table (line 187).

**Failure scenario.** `ln -s .. ~/.local/share/applications/loop` then run `sofi -show drun`: walk_dir recurses loop/loop/loop/... until the stack is exhausted (SIGSEGV) or the process runs out of file descriptors, on every invocation. Deep-but-finite symlink fan-out equally produces an exponential rescan.

**Proposed fix.** Track visited (st_dev, st_ino) pairs in a hash table across the recursion, or pass and enforce a depth limit; alternatively use lstat and refuse to descend into symlinks at all.

**Verifier.** drun.c:838-847 promotes DT_LNK/DT_UNKNOWN to DT_DIR via stat(), and drun.c:856-859 `case DT_DIR: if (recursive) walk_dir(pd, root, filename, recursive);` recurses with no visited-inode set and no depth cap, holding the parent DIR* open (closedir only at line 866). Called with recursive=TRUE at drun.c:1199 (user applications dir) and drun.c:1218 (each XDG system data dir). recursivebrowser.c:187 at least keeps a dirs_scanned table.

## `source/modes/recursivebrowser.c:294` — symlinked directories are resolved with g_file_read_link, which yields the raw (possibly relative) target

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

When a link stats as a directory, scan_dir does `char *sym = g_file_read_link(new_full_path, NULL); if (sym) { GFile *dirp = g_file_new_for_path(sym); g_queue_push_tail(dirs_to_scan, dirp); }` (lines 294-299). g_file_read_link returns the literal contents of the symlink; if it is relative ("../shared", "subdir") g_file_new_for_path resolves it against the process CWD, not against cdir.

**Failure scenario.** With `~/projects/a/link -> ../b`, browsing ~ from a shell whose CWD is /home/user makes the recursive browser scan /home/b (or nothing) instead of /home/user/projects/b. Files under the real target never appear; unrelated trees may be scanned and listed instead.

**Proposed fix.** Resolve relative targets against the link's directory: `if (!g_path_is_absolute(sym)) { char *abs = g_build_filename(cdir, sym, NULL); ... }` — or drop the readlink entirely and just push new_full_path, letting the dirs_scanned realpath guard handle cycles.

**Verifier.** recursivebrowser.c:293-300: `char *sym = g_file_read_link(new_full_path, NULL); if (sym) { GFile *dirp = g_file_new_for_path(sym); g_queue_push_tail(dirs_to_scan, dirp); g_free(sym); }`. g_file_read_link returns the literal symlink target, so a relative target is resolved by g_file_new_for_path against the process CWD instead of against cdir. The TODO comment two lines above ("How to handle loops in links") confirms this path is unfinished.

## `source/modes/ssh.c:394` — ssh config Include is followed recursively with no depth limit or cycle detection

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

parse_ssh_config_file recurses directly into itself for every glob match of an `Include` directive (line 392-397). There is no depth counter and no set of already-visited files. OpenSSH itself caps include depth (MAX_READCONF_DEPTH = 16) precisely because this is reachable. The glob is also run on a user-controlled pattern with no GLOB_LIMIT-style bound.

**Failure scenario.** A ~/.ssh/config containing `Include ~/.ssh/config` (or two files that Include each other, or `Include *` inside ~/.ssh which matches config itself) makes sofi recurse until the stack is exhausted -> SIGSEGV on every `sofi -show ssh`. Each frame also holds an open FILE*, so fd exhaustion may hit first.

**Proposed fix.** Thread a depth parameter through parse_ssh_config_file and refuse beyond ~16, and/or keep a GHashTable of realpath()s already parsed (the same guard scan_dir uses in recursivebrowser.c:187).

**Verifier.** ssh.c:377-397: on an `include` keyword the target is globbed and parse_ssh_config_file recurses into itself at lines 394-395 for every match. No depth counter, no visited-file set, and the caller's FILE* stays open across the recursion. `Include ~/.ssh/config` or a mutual include pair recurses until stack/fd exhaustion. Input is the user's own ssh config, so this is robustness rather than an attack surface.

## `source/modes/wayland-window.c:480` — On compositors without wlr-foreign-toplevel-management the window mode registers successfully and then shows an empty list

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

get_wayland_window warns and returns early when pd->manager is NULL (lines 480-484), but wayland_window_mode_init still returns TRUE (line 544). source/rofi.c:684-688 registers wayland_window_mode for any Wayland backend. All entries come from pd->wlr_toplevels (get_num_entries, line 553), which stays empty. Note the two protocols are not interchangeable: ext-foreign-toplevel-list-v1 is list-only — its handle interface has no activate/close requests (only destroy; see /usr/local/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml, interface ext_foreign_toplevel_handle_v1) — so even the toplevels the code *does* receive over ext cannot be focused. Activation only exists on zwlr_foreign_toplevel_handle_v1.activate (protocols/wlr-foreign-toplevel-management-unstable-v1.xml:139).

**Failure scenario.** `rofi -show window` on KWin 6 or GNOME/Mutter 46 (both implement ext-foreign-toplevel-list-v1 but neither implements zwlr_foreign_toplevel_management_v1): a warning goes to stderr, then the user is presented with an empty, silent window switcher rather than a clear 'window mode is unsupported on this compositor' error. README.md:183 documents this as a known gap but the code does not surface it.

**Proposed fix.** Return FALSE from wayland_window_mode_init when pd->manager is NULL so rofi_collectmodes drops the mode, or keep the mode and surface the reason via the mode's _get_message hook.

**Verifier.** source/modes/wayland-window.c:480-484 warns and returns from get_wayland_window when pd->manager is NULL, but wayland_window_mode_init returns TRUE at 544 regardless, and source/rofi.c:684-688 registers wayland_window_mode for any Wayland backend; get_num_entries (553) reads g_list_length(pd->wlr_toplevels), which stays empty. Verified the supporting detail: /usr/local/share/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml declares ext_foreign_toplevel_handle_v1 with only a `destroy` request (line 129) — no activate/close — so ext handles cannot be acted on. README.md:183 does list this as a known gap. UX/robustness only, so low-medium.

## `source/rofi-icon-fetcher.c:746` — IconFetcherEntry fields are shared between the UI thread and workers with no synchronisation, and re-queued entries leak their surface

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

IconFetcherEntry carries `GCond *cond; GMutex *mutex; unsigned int *acount;` (lines 83-85) that are never initialised (g_new0) and never used. The worker writes sentry->surface (line 746) and sentry->query_done (line 748) while the UI thread reads them in rofi_icon_fetcher_get() (line 850) and rofi_icon_fetcher_get_ex() (lines 862-863); the pool's free callback writes sentry->query_started (line 252) while rofi_icon_fetcher_query* reads it (lines 769, 815). No mutex, no atomics, no barriers — the comment at line 531 ("is a pointer write atomic?") concedes this. Separately, line 746 overwrites sentry->surface without cairo_surface_destroy()ing the previous value, so an entry that is re-queued (which is exactly what lines 769-771 / 815-817 do after rofi_icon_fetch_thread_pool_entry_remove clears query_started) leaks the earlier surface.

**Failure scenario.** Scroll a large icon list so the pool drops queued entries (their state.free callback sets query_started=FALSE), then scroll back so the same entries are re-pushed at lines 770/816. The worker runs a second time and line 746 replaces sentry->surface, leaking the first cairo_surface_t for the process lifetime. Concurrently, the UI thread reading sentry->surface at line 850 with no acquire barrier can observe a non-NULL pointer to a surface whose pixel data the worker has not finished writing (lines 437-438), rendering a partially-decoded icon.

**Proposed fix.** Guard the three mutable fields with the GMutex the struct already declares (or use g_atomic_pointer_set/get for surface and g_atomic_int_set/get for the flags), and destroy the old surface before assigning at line 746.

**Verifier.** source/rofi-icon-fetcher.c:83-85 declares GCond *cond / GMutex *mutex / unsigned int *acount in IconFetcherEntry; grep shows no use of those members anywhere (the only ->acount/->cond uses are view.c's own unrelated thread_state_view at view.c:437/497-501), and the entries come from g_new0 (:777, :823) so they are permanently NULL — dead fields. The cross-thread sharing is real: worker writes sentry->surface (:746) and query_done (:748) while the UI thread reads them at :850 and :862-863, and the pool free callback writes query_started (:252) while :769/:815 read it, with no lock or atomic — the comment at :531 admits it. The surface leak is also real: :746 assigns sentry->surface without cairo_surface_destroy of the old value, and :769-771/:815-817 re-push an entry whose query_started was cleared at :252.

## `source/rofi-icon-fetcher.c:557` — Thumbnails are cached forever: only existence is checked, never the source file's mtime

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Both thumbnail paths test only `if (!g_file_test(icon_path, G_FILE_TEST_EXISTS))` before deciding to (re)generate — line 557 for the preview_cmd path and line 607 for the freedesktop thumbnailer path. The generated PNG is written into the shared freedesktop thumbnail store (~/.cache/thumbnails/{normal,large,x-large,xx-large}, lines 477-494) but rofi never writes the spec-mandated Thumb::URI / Thumb::MTime PNG text chunks and never reads them back, so a thumbnail is never invalidated when the source file changes. Because the store is shared with every other freedesktop thumbnailer consumer, a stale entry rofi wrote is also served to file managers. Note the two paths hash different things: line 554 hashes the raw entry_name while line 604 hashes the file:// URI, so the same file gets two unrelated cache keys depending on whether preview-cmd is set.

**Failure scenario.** Preview an image in filebrowser (thumbnail written to ~/.cache/thumbnails/normal/<md5>.png), edit the image, then preview it again: g_file_test still finds the old PNG, no regeneration is triggered, and rofi (and any spec-compliant file manager reading the same store) shows the stale thumbnail indefinitely.

**Proposed fix.** Write Thumb::URI and Thumb::MTime into the generated PNG per the freedesktop thumbnail spec and, before reusing a cached thumbnail, stat() the source and compare its mtime against Thumb::MTime; regenerate on mismatch. Use the file:// URI as the hash input on both paths so the two agree.

**Verifier.** source/rofi-icon-fetcher.c:557 and :607 both gate regeneration solely on `if (!g_file_test(icon_path, G_FILE_TEST_EXISTS))`; rofi_icon_fetcher_get_thumbnail (:463-500) writes into ~/.cache/thumbnails/{normal,large,x-large,xx-large}. `grep -n 'mtime|Thumb::' source/rofi-icon-fetcher.c` returns nothing — the spec-mandated Thumb::URI/Thumb::MTime chunks are neither written nor read, so entries are never invalidated. The differing cache keys are also real: :554-555 hashes the raw entry_name, :604-605 hashes the file:// URI.

## `source/rofi.c:272` — An out-of-enum ModeMode returned by a mode is used directly as an index into modes[]

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

process_result() handles NEXT_DIALOG/PREVIOUS_DIALOG/RELOAD_DIALOG/RESET_DIALOG and `retv < MODE_EXIT` (which is masked with `% num_modes`), but the final `else { mode = retv; }` at line 272 assumes the only remaining value is MODE_EXIT (1000). ModeMode is a plain enum (include/mode.h:54-65) crossing the plugin ABI boundary; nothing validates it. Line 278 then does `rofi_view_switch_mode(state, modes[mode])`.

**Failure scenario.** A third-party plugin (or a future/older plugin built against a different enum) returns 1005 from its `_result`. `mode = 1005`, `mode != MODE_EXIT`, so `modes[1005]` reads ~4KB past a heap array that holds at most a handful of pointers, and the garbage pointer is handed to rofi_view_switch_mode -> mode_get_display_name -> `mode->name` deref. Wild read plus arbitrary pointer dereference.

**Proposed fix.** Validate before use: treat any value >= MODE_EXIT that is not one of the four known dialog codes as MODE_EXIT, and assert `mode < num_modes` before indexing modes[].

**Verifier.** source/rofi.c:264-280: NEXT/PREVIOUS/RELOAD/RESET are handled, `retv < MODE_EXIT` is masked with `% num_modes` (:271), and the final `else { mode = retv; }` at :272 is followed by `if (mode != MODE_EXIT) { rofi_view_switch_mode(state, modes[mode]); }` at :274-278 with no range validation. ModeMode crosses the plugin ABI, so any value >1000 indexes modes[] out of bounds (and a negative return would survive `retv < MODE_EXIT` and produce a negative `% num_modes` index too). Severity medium: needs a third-party or version-mismatched plugin to return an out-of-enum value; no in-tree mode does.

## `source/theme.c:465` — -dump-theme reads the wrong union member for url() images (prints NULL)

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

int_rofi_theme_print_property() case P_IMAGE prints `p->value.s` instead of `p->value.image.url`. PropertyValue (include/rofi-types.h:254-282) is a union: `char *s` occupies bytes 0-7 while `RofiImage image` starts with `RofiImageType type` (4 bytes) at offset 0 and `char *url` at offset 8. Property is allocated with g_slice_new0 (source/theme.c:104) and the parser sets `image.type = ROFI_IMAGE_URL` (== 0) and `image.url = $3` (lexer/theme-parser.y:622-632), so reading `value.s` yields the zeroed first 8 bytes, i.e. NULL. The very next branch (line 470) correctly uses p->value.image.colors, confirming the intent.

**Failure scenario.** `rofi -theme-str '* { background-image: url("/tmp/a.png"); }' -dump-theme` prints `background-image: url ("(null)");` on glibc — the dumped theme is silently lossy and no longer round-trips. On a libc whose printf does not special-case NULL for %s (musl), the same command segfaults.

**Proposed fix.** Use `p->value.image.url` (and guard for NULL) in the ROFI_IMAGE_URL branch; also emit the scaling keyword so the dump round-trips.

**Verifier.** source/theme.c:465 literally reads `printf("url (\"%s\")", p->value.s);` inside `if (p->value.image.type == ROFI_IMAGE_URL)`, while the sibling branch at 468-470 correctly uses p->value.image.colors. include/rofi-types.h:254-281 confirms PropertyValue is a union containing `char *s` and `RofiImage image`; the RofiImage layout (rofi-types.h:187-202) starts with `RofiImageType type` then `char *url`, so `.s` aliases type+padding, not url. Property is g_slice_new0'd in rofi_theme_property_create (source/theme.c:103-107) and theme-parser.y:622-625 sets image.type=ROFI_IMAGE_URL(==0) and image.url=$3, so .s reads as NULL. The dump is lossy; the musl-segfault angle is real but speculative, so high overstates it.

## `source/view.c:1060` — kb-element-copy silently does nothing on Wayland

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

The COPY_SECONDARY branch of rofi_view_trigger_global_action builds the string to copy (lines 1042-1048) and then, for DISPLAY_WAYLAND, hits an empty `// TODO` block at lines 1058-1062. There is no warning, no g_debug, and no indication in the UI. The X11 path immediately above (lines 1050-1056) does the real work.

**Failure scenario.** On Wayland, select an entry and press the kb-element-copy binding (Ctrl+c by default). Nothing is copied and no feedback of any kind is produced; the user's existing clipboard content is silently left in place and they paste the wrong thing.

**Proposed fix.** Implement it via wl_data_device_manager (already bound, include/wayland-internal.h:53) as a display_proxy method alongside get_clipboard_data, or at minimum emit a g_warning so the gap is visible.

**Verifier.** source/view.c:1041-1064: COPY_SECONDARY builds `data` (1042-1048), the XCB arm does xcb_stuff_set_clipboard + xcb_set_selection_owner + xcb_flush (1051-1055), and the Wayland arm at 1058-1062 is `if (config.backend == DISPLAY_WAYLAND) { // TODO }` — no warning, no debug log, no fallback. `data` is also never g_free'd in either arm, incidentally.

## `source/wayland/display.c:1756` — g_error() used for an expected 'compositor unsupported' condition, aborting with a core dump; the following return is dead code

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wayland_display_setup uses g_error for both 'Could not connect to wayland compositor' (line 1752) and 'Rofi on wayland requires support for the layer shell protocol' (line 1756). g_error is fatal by definition in GLib — it logs at G_LOG_LEVEL_ERROR and calls abort(). The `return FALSE;` on lines 1753 and 1757 is therefore unreachable, and the gboolean return of the display_proxy.setup slot (include/display-internal.h:40) is never actually exercised for these cases even though source/display.c:47-52 is written to propagate it.

**Failure scenario.** Run on GNOME/Mutter, which does not implement zwlr_layer_shell_v1: instead of a clean 'this compositor is not supported, try -x11' message and exit(1), the process aborts (SIGABRT), which distro crash reporters (abrt/apport/systemd-coredump) pick up as a crash and users file as a bug.

**Proposed fix.** Use g_warning (or g_critical) plus the existing `return FALSE`, and let display_setup's caller report the failure and exit cleanly.

**Verifier.** source/wayland/display.c:1752 `g_error("Could not connect to wayland compositor"); return FALSE;` and 1756 `g_error("Rofi on wayland requires support for the layer shell protocol"); return FALSE;`. g_error logs at G_LOG_LEVEL_ERROR, which GLib always treats as fatal (abort), so both `return FALSE;` lines are unreachable and the gboolean contract of the setup slot that source/display.c:38-43 propagates is never exercised for these two cases.

## `source/xcb/display.c:376` — Fake transparency only reads ESETROOT_PMAP_ID, never _XROOTPMAP_ID

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

x11_helper_get_bg_surface() calls get_root_pixmap() only with netatoms[ESETROOT_PMAP_ID]. The `_XROOTPMAP_ID` atom is declared in the preload list (include/xcb.h:91) and interned at startup, but grep over source/ and include/ shows it is never read anywhere — clearly the intent was to try both, as every other wallpaper consumer does.

**Failure scenario.** The user sets the wallpaper with hsetroot/xwallpaper/xsetroot (which set _XROOTPMAP_ID but not ESETROOT_PMAP_ID), then uses a theme with `transparency: "background";`. get_root_pixmap() returns XCB_NONE, x11_helper_get_bg_surface() returns NULL, and the background silently renders fully transparent instead of showing the wallpaper.

**Proposed fix.** Try ESETROOT_PMAP_ID first, then fall back to netatoms[_XROOTPMAP_ID] before giving up.

**Verifier.** source/xcb/display.c:374-376: x11_helper_get_bg_surface() calls get_root_pixmap() with netatoms[ESETROOT_PMAP_ID] only. grep over source/ and include/ for XROOTPMAP_ID returns exactly one hit outside the atom table: include/xcb.h:91 in the EWMH_ATOMS X-macro list. The atom is interned and never read.

## `source/xrmoptions.c:905` — xrm_Number/xrm_SNumber write through the wrong union member and never range-check

- **kind** correctness · **severity** medium · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

In __config_parser_set_property, the xrm_Number (unsigned, xrmoptions.h:75) branch writes `*(option->value.snum) = p->value.i;` (line 905) while the xrm_SNumber (signed, xrmoptions.h:77) branch writes `*(option->value.num) = (unsigned int)(p->value.i);` (line 914) — the two union members are swapped relative to their declared types. Because both members alias the same address the bit pattern survives, but the consequence is that no branch rejects a negative literal for an unsigned option: a negative P_INTEGER is reinterpreted as a huge unsigned value.

**Failure scenario.** `rofi -theme-str 'configuration { threads: -1; }'` stores 4294967295 in config.threads (include/settings.h:177, unsigned int). source/view.c:2023 sees it as non-zero so the clamp to MIN(procs,128) is skipped, and source/view.c:2033 passes it to g_thread_pool_new() whose max_threads parameter is gint — it arrives as -1, i.e. "unlimited threads". source/view.c:819's `MIN(nt, config.threads * 4)` also overflows.

**Proposed fix.** Swap the members back (num for xrm_Number, snum for xrm_SNumber) and reject p->value.i < 0 for xrm_Number with the same error-string mechanism used for type mismatches.

**Verifier.** source/xrmoptions.c:60-62 declares `union { unsigned int *num; int *snum; }`. The `option->type == xrm_Number` branch writes `*(option->value.snum) = p->value.i;` at line 905, and the `xrm_SNumber` branch writes `*(option->value.num) = (unsigned int)(p->value.i);` at line 914 — swapped relative to include/xrmoptions.h:75/77. Neither branch range-checks. lexer/theme-parser.y:557-560 (`T_MIN T_INT { $$->value.i = -$2; }`) proves a negative P_INTEGER is producible from a config block, and source/xrmoptions.c:502-505 registers `threads` as xrm_Number with `{.num = &config.threads}` (include/settings.h:177, unsigned int). source/view.c:2022-2031 then skips the `MIN(procs,128l)` clamp and passes the value to g_thread_pool_new_full's gint max_threads; source/view.c:819 multiplies it by 4. Consequence chain verified (cited view.c lines are off by ~2).

## `source/wayland/display.c:380` — wl_keyboard.keymap leaks the fd, the mapping, and (on error) the xkb_keymap on every event

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

wayland_keyboard_keymap() (lines 372-401) mmaps the keymap at line 380 and then never munmaps it and never closes fd on ANY path after that point:
  - line 389-392 (keymap == NULL): returns without munmap(str,size) and without close(fd)
  - line 393-398 (state == NULL): additionally leaks the xkb_keymap (no xkb_keymap_unref)
  - line 400-401 (success): returns without munmap(str,size) and without close(fd)
Only the two pre-mmap paths (lines 375-378, 381-384) close fd. Note also that both error paths use fprintf(stderr,...) rather than g_warning, bypassing the "Wayland" G_LOG_DOMAIN set at line 25.

**Failure scenario.** The compositor re-sends keymap on every layout change. A user cycling layouts (or a seat that re-advertises the keyboard capability) leaks one fd plus one mapping of `size` bytes each time. In a long-lived `-show combi` session with a layout-switch keybind, the process eventually hits RLIMIT_NOFILE and subsequent pipe()/shm_open() calls (including line 209 and line 1994) fail. On the state==NULL path the whole compiled xkb_keymap leaks too.

**Proposed fix.** Add `munmap(str, size); close(fd);` on every path after the successful mmap (a single cleanup label is cleanest), xkb_keymap_unref(keymap) before the state==NULL return, and replace the fprintf calls with g_warning.

**Verifier.** display.c:380 mmaps the keymap; only the two pre-mmap returns (376, 382) close fd. The keymap==NULL path (389-392), the state==NULL path (393-398, which additionally never xkb_keymap_unref's) and the success path (400-401) all return without munmap(str,size) and without close(fd). Both error paths use fprintf(stderr) rather than g_warning despite G_LOG_DOMAIN at line 25.

## `lexer/theme-lexer.l:815` — Start-condition GQueue is only freed in the <<EOF>> rule, which aborted parses never reach

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

`queue` (lexer/theme-lexer.l:94) is allocated lazily at line 334 when it is NULL and is freed + reset to NULL only inside the <<EOF>> action (lines 813-818). That action is not reached when (a) yyparse() aborts on a syntax error — bison returns immediately without draining the lexer — or (b) EOF is hit while in a start condition that has no <<EOF>> rule. The EOF rule is declared only for `<INITIAL,PROPERTIES_ENV,PROPERTIES_VAR_DEFAULT,MEDIA_ENV_VAR_CONTENT>` (line 801); for SECTION, PROPERTIES, NAMESTR, DEFAULTS, INCLUDE, MEDIA, PROPERTIES_ARRAY etc. flex falls back to plain yyterminate(). Neither rofi_theme_parse_file() (lines 946-958) nor rofi_theme_parse_string() (lines 981-993) touches `queue` during cleanup. Consequence beyond the leak: `queue` stays non-NULL forever, so the init block at lines 334-340 never runs again and `yylloc->filename` / first_line / first_column are never re-seeded for any later parse in the same process.

**Failure scenario.** `rofi -theme-str '* {' -theme /path/with/a/typo.rasi`: the first parse aborts in the SECTION start condition, leaking the GQueue and leaving it non-NULL; the error message for the second, file-based parse then omits the `File '...'` line (source/theme.c:633-639 sees yylloc->filename == NULL), so the user is told there is a parse error but not in which file.

**Proposed fix.** Free and NULL `queue` in the cleanup loops of rofi_theme_parse_file()/rofi_theme_parse_string() (and reset `current`, `import_optional`), or add a `<*><<EOF>>` rule that unwinds unconditionally.

**Verifier.** lexer/theme-lexer.l:94 declares `GQueue *queue`; lines 334-340 allocate it lazily and seed yylloc->filename ONLY when queue==NULL; the sole `g_queue_free ( queue ); queue = NULL;` is at lines 815-817 inside the `<INITIAL,PROPERTIES_ENV,PROPERTIES_VAR_DEFAULT,MEDIA_ENV_VAR_CONTENT><<EOF>>` action declared at line 801. rofi_theme_parse_file (946-962) and rofi_theme_parse_string (981-997) drain only file_queue and never touch `queue`. grep for bison error-recovery rules in lexer/theme-parser.y returns nothing, so yyparse returns immediately on a syntax error and the EOF action is never reached. The stale non-NULL queue then suppresses the re-seed at line 336, and source/theme.c:643-648 omits the `File '...'` line when yylloc->filename is NULL. Mechanism holds exactly as claimed.

## `source/helper.c:826` — levenshtein() allocates an attacker-sized VLA on the (worker) thread stack

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`unsigned int column[needlelen + 1];` (line 826) is a variable-length array whose size comes straight from the caller. The only guard is `if (needlelen == G_MAXLONG) return UINT_MAX;` (lines 822-824) — nothing bounds it to anything reasonable, and nothing rejects a negative needlelen (a negative VLA size is undefined behaviour). The sole caller, source/view.c:488, passes `t->plen`, the character length of the user's current filter, and runs on a thread-pool worker whose stack is typically 8 MiB and is not guard-page-probed by this code. Contrast rofi_scorer_fuzzy_evaluate (line 970) and rofi_scorer_fzf_v2_evaluate (line 1278), which both cap at FUZZY_SCORER_MAX_LENGTH and heap-allocate.

**Failure scenario.** With `sort: true;` and the default `sorting-method: normal`, paste a ~4 MB string into the rofi input box (Ctrl-V of a large clipboard buffer). view.c:488 calls levenshtein with needlelen ~4e6, the VLA needs ~16 MB on the worker stack, which blows past it — stack overflow, SIGSEGV, no diagnostic.

**Proposed fix.** Cap needlelen the way the other scorers do (`if (needlelen < 0 || needlelen > FUZZY_SCORER_MAX_LENGTH) return UINT_MAX;`) and/or switch the column array to g_malloc_n/g_free.

**Verifier.** source/helper.c:822-826: the only guard is `if (needlelen == G_MAXLONG) return UINT_MAX;` followed by `unsigned int column[needlelen + 1];` — an unbounded VLA sized from the caller. source/view.c:486-489 passes t->plen (the user's filter length) from filter_elements, which runs on a thread-pool worker. Contrast source/helper.c:970/1278 scorers which cap at FUZZY_SCORER_MAX_LENGTH. Stack exhaustion needs a multi-megabyte filter string, so medium not high.

## `source/modes/dmenu.c:157` — read_add_block writes values[BLOCK_LINES_SIZE], one element past the fixed array

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

Block declares `DmenuScriptEntry values[BLOCK_LINES_SIZE]` followed by `DmenuModePrivateData *pd` (lines 121-125). read_add_block writes the sentinel `(*block)->values[(*block)->length + 1].entry = NULL;` at line 157. The flush in read_input_thread happens only after length has been incremented to BLOCK_LINES_SIZE (line 311), so on entry length can be BLOCK_LINES_SIZE-1 == 2047 and the sentinel write lands on values[2048] — out of bounds. Because `entry` is the first member of DmenuScriptEntry and `pd` immediately follows the array, the write currently silently clobbers block->pd. (block->pd is dead: it is assigned at line 132 and never read anywhere in the tree.)

**Failure scenario.** Pipe >2048 items into `sofi -dmenu` fast enough that a single block fills: on the 2048th item read_add_block writes past values[] and zeroes block->pd. Harmless today only by accident of layout; it is UB, aborts immediately under -fsanitize=address/bounds, and becomes a real pointer-corruption bug the moment a field is added after values[] or the array is made dynamic.

**Proposed fix.** Guard the sentinel: `if ((*block)->length + 1 < BLOCK_LINES_SIZE) (*block)->values[(*block)->length + 1].entry = NULL;` — or drop the dead pd field and the sentinel entirely, since block->length already carries the count.

**Verifier.** dmenu.c:121-125 declares `DmenuScriptEntry values[BLOCK_LINES_SIZE]` (2048) followed by `DmenuModePrivateData *pd`. read_add_block writes the sentinel at dmenu.c:157 `(*block)->values[(*block)->length + 1].entry = NULL;`. The flush at dmenu.c:310-311 tests `block->length == BLOCK_LINES_SIZE` only AFTER read_add_block incremented length, so length can be 2047 on entry and the sentinel lands on values[2048] — one element past the array. Verified by grep that block->pd has no reader anywhere in source/, so today it silently clobbers a dead field.

## `source/modes/dmenu.c:169` — read_add() never initializes DmenuScriptEntry.permanent, which dmenu_token_match reads

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

pd->cmd_list is grown with g_realloc (line 166), so new slots contain indeterminate bytes. read_add's init block (lines 170-180) sets icon_fetch_uid/size/scale, icon_fallback_index, icon_name, display, meta, info, active, urgent and nonselectable — but not `permanent`. dmenu_token_match then reads it as the very first thing: `if (rmpd->cmd_list[index].permanent == TRUE) return 1;` (line 675). script.c:295 sets permanent = FALSE correctly; read_add_block gets away with it only because Block comes from g_malloc0.

**Failure scenario.** Run any sync-mode dmenu (`sofi -dmenu -sync`, or with -select/-no-custom/-only-match/-dump/-selected-row, which all force pd->async = FALSE at dmenu.c:537-542) and type a filter. Entries whose uninitialized `permanent` byte happens to be 1 always match and stay visible regardless of the filter; the result is a filter that non-deterministically leaks unrelated rows.

**Proposed fix.** Add `pd->cmd_list[pd->cmd_list_length].permanent = FALSE;` to read_add's init block, or memset the new slot before filling it.

**Verifier.** dmenu.c:163-168 grows pd->cmd_list with g_realloc; the init block at dmenu.c:169-181 sets icon_fetch_uid/size/scale, icon_fallback_index, icon_name, display, meta, info, active, urgent, nonselectable — and NOT `permanent`. dmenu_token_match reads it first thing at dmenu.c:675 `if (rmpd->cmd_list[index].permanent == TRUE) return 1;`. read_add is the sync path (dmenu.c:271, read_input_sync), forced by -sync/-dump/-select/-no-custom/-only-match/-selected-row at dmenu.c:535-541. One inaccuracy in the claim: read_add_block DOES set permanent = FALSE at dmenu.c:145, and the async path memcpys from that block (dmenu.c:228), so only the sync path is affected. script.c:295 sets it correctly.

## `source/modes/drun.c:948` — drun_read_string() trusts the cache to NUL-terminate; no check on the last byte

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

drun_read_string() reads a size_t length l from the cache file, allocates l+1 bytes and freads exactly l+1 bytes into it (lines 949-967). It never verifies that (*str)[l] == '\0'. Every consumer treats the result as a C string: g_markup_escape_text(dr->name, -1) at line 1521, g_strjoinv on categories/keywords at 1497/1505, helper_token_match(ftokens, ...->exec) at 1609, g_key_file_get_string(e->key_file, e->action, ...) at 398. The cache in $XDG_CACHE_HOME/rofi/rofi-drun-desktop.cache is a genuine trust boundary: it is read back verbatim with only a one-byte version check (line 1064-1076) and no checksum, and read_desktop_file's expensive validation (Type/Name/TryExec/NoDisplay/OnlyShowIn) is entirely bypassed on the cache path.

**Failure scenario.** With `drun-use-desktop-cache: true`, write a cache file whose version byte is 3, count is 1, and whose first string field has length 8 followed by 9 bytes none of which is NUL. drun_read_string returns a 9-byte non-terminated heap buffer as entry->action; g_key_file_get_string(e->key_file, e->action, "Path", NULL) then runs strlen() off the end of the allocation -> heap over-read, and _get_display_value's g_markup_escape_text(dr->name, -1) copies unbounded adjacent heap into the rendered row.

**Proposed fix.** After the fread, assert termination: `if ((*str)[l-1] != '\0') { g_free(*str); *str = NULL; return TRUE; }`. Better, add a per-file checksum/size record and validate that every offset stays inside the file.

**Verifier.** drun.c:948-968: reads size_t l, does l++, bounds-checks against DRUN_MAX_STRING_LENGTH, g_malloc(l), fread exactly l bytes. No check that (*str)[l-1]=='\0'. Result is used as a C string everywhere (e.g. g_key_file_get_string(e->key_file, e->action, ...) at drun.c:396). Cache read path (drun.c:1055-1076) validates only a uint8 version byte and the count; read_desktop_file's validation is bypassed. Trust boundary is the user's own $XDG_CACHE_HOME file and the feature is off by default (config.c:170 drun_use_desktop_cache = FALSE), which caps real-world severity.

## `source/modes/recursivebrowser.c:454` — recursive_browser_mode_destroy frees pd but never removes the g_unix_fd_add source watching pipefd2

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

recursive_browser_mode_init registers `pd->wake_source = g_unix_fd_add(pd->pipefd2[0], G_IO_IN, recursive_browser_async_read_proc, pd)` (line 394-395). recursive_browser_mode_destroy joins the thread and g_free(pd)s at line 468 but never calls g_source_remove(pd->wake_source), never closes pipefd2[0]/[1], and never drains/unrefs pd->async_queue. The source stays attached to the default main context with the freed pd as user_data. Contrast dmenu_finish (dmenu.c:767-784) which does remove the source and close both fds. The worker writes "r" then "q" into the pipe just before exiting (lines 335-336), so bytes are typically still unread in the pipe at destroy time.

**Failure scenario.** Run with `-modes recursivebrowser,run` and switch away from recursivebrowser (or exit while the scan just finished): mode_destroy frees pd, the still-armed GSource dispatches on the leftover 'r'/'q' byte, and recursive_browser_async_read_proc dereferences pd->async_queue / pd->array / pd->loading on freed memory -> use-after-free. Each mode_init/mode_destroy cycle also leaks two fds, the GAsyncQueue and every FBFile still queued (name + path strings).

**Proposed fix.** Mirror dmenu_finish: set end_thread, join, then g_source_remove(pd->wake_source), close(pd->pipefd2[0]), close(pd->pipefd2[1]), pop-and-free every remaining FBFile from the queue (freeing name and path, not just the struct) and g_async_queue_unref().

**Verifier.** recursivebrowser.c:394-395 arms `pd->wake_source = g_unix_fd_add(pd->pipefd2[0], G_IO_IN, recursive_browser_async_read_proc, pd)`. recursive_browser_mode_destroy (lines 454-471) sets end_thread, joins, unrefs regex/current_dir, free_list, g_free(pd) — no g_source_remove(pd->wake_source), no close(pipefd2[0]/[1]), no g_async_queue_unref. The worker writes 'r' and 'q' into the pipe at lines 335-336 before exiting, so data is pending. Caveat on the claim: the usual mode_destroy is rofi.c:558-561 cleanup(), which runs AFTER the main loop exits, so there the damage is only leaked fds/queue at exit. The UAF is reachable on the mid-session path: run.c:526/drun.c:1436 create a completer via mode_create/mode_init and run.c:435 (inside run_mode_destroy, itself invoked mid-session at run.c:510 on MENU_ENTRY_DELETE) destroys it — so with completer-mode=recursivebrowser the source dispatches on freed pd.

## `source/modes/recursivebrowser.c:187` — visited-directory set uses g_str_hash with g_int_equal, reading 4 bytes of every path key

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

scan_dir builds `g_hash_table_new_full(g_str_hash, g_int_equal, g_free, NULL)` — a string hash paired with an integer comparator. g_int_equal does `*(const gint*)a == *(const gint*)b`, i.e. it loads 4 bytes from each key. Keys are g_strdup'd paths (line 198), so any key shorter than 4 bytes is read out of bounds, and equality is decided on the first 4 bytes rather than the whole path.

**Failure scenario.** Set `recursivebrowser { directory: "/"; }`. The first key inserted is "/" — a 2-byte allocation. Every subsequent g_hash_table_lookup_extended that lands in that bucket makes g_int_equal read 4 bytes from the 2-byte block -> heap over-read (immediately flagged by ASan/valgrind). Independently, two distinct directories that collide in g_str_hash *and* share their first 4 bytes are treated as already-scanned and silently skipped.

**Proposed fix.** Use g_str_equal as the key_equal_func.

**Verifier.** recursivebrowser.c:186-188 `g_hash_table_new_full(g_str_hash, g_int_equal, g_free, NULL)`. Keys are g_strdup'd path strings (line 198) but g_int_equal does `*(const gint*)a == *(const gint*)b`, reading 4 bytes from each key — an over-read for any key shorter than 4 bytes (e.g. "/" from `directory: "/"`), and it decides equality on the first 4 bytes plus the g_str_hash bucket rather than the full path.

## `source/modes/wayland-window.c:520` — zwlr_foreign_toplevel_manager_v1_stop() called on a proxy already destroyed by the finished handler

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wlr_foreign_toplevel_manager_finished (lines 399-403) calls zwlr_foreign_toplevel_manager_v1_destroy(manager) but takes `data` as G_GNUC_UNUSED and therefore never clears pd->manager. protocols/wlr-foreign-toplevel-management-unstable-v1.xml:61-68 declares `finished` as type="destructor" and states the server destroys the object 'immediately after sending', i.e. the compositor may send it at any time, not only in response to stop(). After that event pd->manager is a dangling wl_proxy*, and wayland_window_private_free unconditionally calls zwlr_foreign_toplevel_manager_v1_stop(pd->manager) at line 520 followed by wl_display_roundtrip at line 522.

**Failure scenario.** A compositor that tears down its foreign-toplevel manager while rofi/sofi is running (wlroots sends `finished` on zwlr_foreign_toplevel_manager_v1 when the protocol global is removed, e.g. on a compositor config reload) makes the finished handler run wl_proxy_destroy(). On exit, wayland_window_mode_destroy -> wayland_window_private_free writes into the freed wl_proxy through zwlr_foreign_toplevel_manager_v1_stop -> heap use-after-free / abort in libwayland's proxy validation.

**Proposed fix.** Set `pd->manager = NULL` inside wlr_foreign_toplevel_manager_finished (use the `data` argument instead of marking it unused), and mirror the same for ext_foreign_toplevel_list_finished at lines 279-283 which leaves pd->list dangling.

**Verifier.** source/modes/wayland-window.c:399-403: wlr_foreign_toplevel_manager_finished takes `data` as G_GNUC_UNUSED and calls zwlr_foreign_toplevel_manager_v1_destroy(manager) without clearing pd->manager. protocols/wlr-foreign-toplevel-management-unstable-v1.xml:61-68 marks `finished` type="destructor" ("the server will destroy the object immediately after sending"). wayland_window_private_free then calls zwlr_foreign_toplevel_manager_v1_stop(pd->manager) at line 520 on that dangling proxy. Note the ordinary teardown path is safe (520 stop, 521 pd->manager=NULL, 522 roundtrip, so the finished arriving in response destroys a proxy nobody references any more) — the UAF needs a compositor-initiated finished, hence medium.

## `source/modes/wayland-window.c:508` — wayland_window_private_free frees the toplevel list before stopping the manager, so toplevels arriving during the stop roundtrip leak with listeners pointing at freed pd

- **kind** memory · **severity** medium · **verdict** PLAUSIBLE · **domain** wayland backend: source/wayland/view.c, source/m

wayland_window_private_free frees and NULLs pd->wlr_toplevels first (lines 508-512), then destroys the registry (514-517), then calls stop() and wl_display_roundtrip (519-523), then g_free(pd) (529). protocols/wlr-foreign-toplevel-management-unstable-v1.xml:51-58 explicitly says 'the compositor may emit further toplevel_created events, until the finished event is emitted'. Any `toplevel` event delivered during the roundtrip at line 522 runs wlr_foreign_toplevel_manager_toplevel (line 390), which g_malloc0s a WlrForeignToplevelHandle, registers a listener holding `self->view = pd`, and prepends it to the already-emptied pd->wlr_toplevels. Nothing frees that list again; pd is freed at line 529 while those live wl_proxy listeners still hold a pointer to it.

**Failure scenario.** A window opens in the instant between the g_list_free at line 510 and the roundtrip completing at line 522. The compositor sends `toplevel` plus `app_id`/`title`/`done`. wayland_window_update_toplevel (line 176) reads `pd->visible` and, when TRUE, walks pd->wlr_toplevels and calls rofi_view_reload() — all through the pd pointer that is freed six lines later. The new handle struct, its strdup'd title/app_id and its wl_proxy are leaked outright, and any further dispatch on that proxy is a use-after-free read of pd.

**Proposed fix.** Reorder: stop() + roundtrip + destroy the manager and registry FIRST, then walk and free pd->wlr_toplevels and pd->ext_toplevels, then g_free(pd).

**Verifier.** Order at source/modes/wayland-window.c:507-529 is exactly as claimed: wlr_toplevels freed+NULLed (508-512), registry destroyed (514-517), stop() + wl_display_roundtrip (519-523), g_free(pd) (529). protocols/wlr-foreign-toplevel-management-unstable-v1.xml:51-58 does say the compositor may emit further toplevel events until `finished`, and wlr_foreign_toplevel_manager_toplevel (390-397) unconditionally g_malloc0s a handle with self->view = pd and prepends it to the now-NULL list, which nothing frees; wayland_window_update_toplevel (176-188) then reads pd->visible. Whether any toplevel event actually lands inside that roundtrip is a runtime race I cannot establish statically, so PLAUSIBLE; the ordering itself is objectively wrong.

## `source/modes/window.c:640` — _NET_WM_DESKTOP read as uint32 without checking the reply value length

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

`winclient->wmdesktop = *((uint32_t *)xcb_get_property_value(r));` only checks `r->type == XCB_ATOM_CARDINAL` (line 639); it never checks `xcb_get_property_value_length(r) >= 4` nor `r->format == 32`. When the property exists with type CARDINAL but zero length (or 8/16-bit format), xcb_get_property_value() points at the very end of the reply buffer and 4 bytes past the allocation are read. The identical bug is repeated at lines 806-807 in window_mode_result(). Note that the value is fully attacker-controlled: any X client on the display can set an arbitrary-length CARDINAL _NET_WM_DESKTOP on its own window.

**Failure scenario.** A client does `xcb_change_property(..., _NET_WM_DESKTOP, XCB_ATOM_CARDINAL, 32, 0, NULL)` (zero-length CARDINAL). Opening `rofi -show window` reads 4 bytes past the end of the 32-byte xcb reply buffer; under ASan this is a heap-buffer-overflow, in production it yields a garbage desktop id which is then used as an index into the desktop-name list at line 650 and as the desktop to switch to at line 817.

**Proposed fix.** Guard both sites with `if (r && r->type == XCB_ATOM_CARDINAL && r->format == 32 && xcb_get_property_value_length(r) >= (int)sizeof(uint32_t))` before dereferencing.

**Verifier.** source/modes/window.c:637-643: only `if (r)` and `if (r->type == XCB_ATOM_CARDINAL)` guard `winclient->wmdesktop = *((uint32_t *)xcb_get_property_value(r));` — no xcb_get_property_value_length()/format check. A zero-length CARDINAL reply is a 32-byte allocation and xcb_get_property_value() returns (R+1), so 4 bytes are read past the buffer. Repeated verbatim at lines 805-808 (`if (r && r->type == XCB_ATOM_CARDINAL) wmdesktop = *((uint32_t*)xcb_get_property_value(r));`). Downstream use is however safe: _window_name_list_entry (lines 526-540) bounds-checks offset against length, so the impact is a 4-byte heap over-read plus a garbage desktop id, not a further OOB.

## `source/rofi-icon-fetcher.c:412` — cairo_image_surface_create()/get_data() result never checked before being written through

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/rofi-icon-fetcher.c:411-436:
```
  surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cpixels = cairo_image_surface_get_data(surface);
  cstride = cairo_image_surface_get_stride(surface);
  cairo_surface_flush(surface);
  while (pixels < pixels_end) { ... cline[RED_BYTE] = ...; }
```
No cairo_surface_status() check. When cairo refuses the size (CAIRO_STATUS_INVALID_SIZE for any dimension > 32767, or CAIRO_STATUS_NO_MEMORY) it returns an error surface whose get_data() is NULL and get_stride() is 0, but `width`/`height` here come from the *pixbuf*, so `pixels_end > pixels` and the copy loop still runs, dereferencing NULL. The same unchecked pattern exists for the markup-icon surface at line 652.

**Failure scenario.** An image file that gdk-pixbuf loads successfully but that exceeds cairo's 32767-pixel limit in one dimension (e.g. a 40000x64 PNG used as a file icon / thumbnail): cairo_image_surface_create returns an error surface, cpixels == NULL, and the first `cline[RED_BYTE] = ...` writes to address 2 -> SIGSEGV inside the icon worker thread.

**Proposed fix.** After the create at source/rofi-icon-fetcher.c:411, check `if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS || cpixels == NULL) { cairo_surface_destroy(surface); return NULL; }` before the copy loop; do the same at line 652-654.

**Verifier.** source/rofi-icon-fetcher.c:411-436 is exactly as quoted: cairo_image_surface_create(), then cairo_image_surface_get_data()/get_stride() with no cairo_surface_status() check, then the copy loop whose bounds come from the *pixbuf* (pixels_end computed at line 407 from gdk stride/height), so the loop runs even when cpixels is NULL and cstride is 0. cairo returns an error surface (data NULL, stride 0) for any dimension above its 32767 image limit or on OOM, and the loop's very first `cline[RED_BYTE] = ...` at line 425 then writes through NULL. The same unchecked create is at line 652-654 for the markup path. Requires an image gdk-pixbuf accepts but cairo rejects, which a >32767px-wide file supplies.

## `source/rofi-icon-fetcher.c:325` — rofi_icon_fetcher_destroy() frees state that in-flight worker threads are still using

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

rofi_icon_fetcher_destroy() unconditionally unrefs the hash tables (lines 330-335, which run rofi_icon_fetch_entry_free and free every IconFetcherEntry) and then g_free()s rofi_icon_fetcher_data (line 340). Nothing quiesces the thread pool first. The only shutdown of the pool is source/view.c:2053 `g_thread_pool_free(tpool, TRUE, FALSE)` — immediate=TRUE discards *queued* items but wait_=FALSE means it explicitly does NOT wait for the workers already executing. Those workers are inside rofi_icon_fetcher_worker(), which dereferences `sentry->entry->name` (line 539), `rofi_icon_fetcher_data->xdg_context` (lines 582, 592, 630, 680), writes `sentry->surface` (line 746) and `sentry->query_done` (line 748), and calls rofi_view_reload() (line 749) after display_cleanup() has already run (source/rofi.c:568 precedes :588).

**Failure scenario.** Open filebrowser with icons/thumbnails on a slow or network-mounted directory so several worker threads are mid-fetch, then press Escape. cleanup() runs: view.c:2053 returns without waiting, display_cleanup() tears the backend down, rofi_icon_fetcher_destroy() frees rofi_icon_fetcher_data and every sentry — and a still-running worker then writes sentry->surface into freed memory and calls rofi_view_reload() on a destroyed proxy. Use-after-free / crash on exit, non-deterministic.

**Proposed fix.** Give the fetcher a shutdown flag the worker checks, and drain the pool before freeing: call g_thread_pool_free(tpool, TRUE, TRUE) (wait for running tasks) in rofi_view_workers_finalize(), or have rofi_icon_fetcher_destroy() take a mutex/refcount that workers hold for the duration of their run.

**Verifier.** Verified the whole chain: source/view.c:2050-2053 `g_thread_pool_free(tpool, TRUE, FALSE)` (immediate=TRUE, wait_=FALSE, i.e. glib returns without waiting for threads already executing), source/rofi.c:562 workers_finalize -> :568 display_cleanup() -> :588 rofi_icon_fetcher_destroy(); the latter (source/rofi-icon-fetcher.c:325-341) unrefs icon_cache (whose value destructor rofi_icon_fetch_entry_free frees every IconFetcherEntry, line 255-271), frees the xdg_context at :332 and g_free's rofi_icon_fetcher_data at :340. rofi_icon_fetcher_worker does read sentry->entry->name (:539), rofi_icon_fetcher_data->xdg_context (:582,592,630,680) and write sentry->surface/query_done (:746,748) plus rofi_view_reload() (:749). Nothing quiesces the pool. Severity medium: the window is the tail of process exit.

## `source/view.c:1557` — textbox_button_trigger_action indexes line_map with no bounds check; line_map is NULL when the list is empty

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`state->line_map[listview_get_selected(state->list_view)]` is read whenever a themed `button`/`icon` widget with an `action` property is clicked. listview_get_selected() returns 0 for an empty list (source/widgets/listview.c:631-636), and line_map is allocated with `g_malloc0_n(state->num_lines, sizeof(unsigned int))` (source/view.c:1887, and again at :773), which GLib documents as returning NULL when n_blocks is 0. Every other read of line_map in this file is guarded by `selected < state->filtered_lines` (e.g. :1094, :1113, :1247, :1277); this one and :1613 are not.

**Failure scenario.** `printf '' | rofi -dmenu` (0 entries, so num_lines==0 and line_map==NULL) with a theme whose inputbar contains a `button` widget carrying `action: "kb-accept-entry";`. Click the button -> `state->line_map[0]` dereferences NULL -> SIGSEGV. The same happens with a non-empty list but a filter that matches nothing (filtered_lines==0), where it reads a stale/zero entry instead.

**Proposed fix.** Guard exactly like the other call sites: `unsigned int sel = listview_get_selected(state->list_view); state->selected_line = (state->line_map && sel < state->filtered_lines) ? state->line_map[sel] : UINT32_MAX;`. Apply the same guard at source/view.c:1613.

**Verifier.** source/view.c:1557-1558 is `(state->selected_line) = state->line_map[listview_get_selected(state->list_view)];` guarded only by `if (state->list_view)` (1555), not by `selected < state->filtered_lines`. listview_get_selected (source/widgets/listview.c:631-636) returns lv->selected (0 for an empty list). line_map comes from `g_malloc0_n(state->num_lines, ...)` at source/view.c:1887 and :773, and GLib documents g_malloc0_n as returning NULL when n_blocks is 0, so with 0 entries line_map is NULL and this is a NULL[0] read. Same unguarded pattern at :1613 (mouse-activated cb) and :602/:900. Needs a theme whose button carries an `action` property (handler installed at :1787/:1797), so medium rather than high.

## `source/view.c:1006` — Keybinding dispatch dereferences RofiViewState without ever checking for NULL

- **kind** memory · **severity** medium · **verdict** PLAUSIBLE · **domain** core: source/rofi.c, source/view.c, source/mode.

rofi_view_trigger_global_action() takes `RofiViewState *state = rofi_view_get_active();` and then dereferences it unconditionally in most cases (`state->list_view` at :1043, :1069, :1092, :1112, :1158; `state->retv` at :1081; `state->text` at :1210). rofi_view_check_action() (:1365) and rofi_view_trigger_action() (:1398) likewise read `state->mouse.x` with no NULL test. The two nkutils callbacks that feed them pass rofi_view_get_active() straight through without checking (source/keyb.c:428 and source/keyb.c:436), and rofi_view_take_action() does the same (source/view.c:254). rofi_view_get_active() legitimately returns NULL — that is exactly the sentinel rofi_view_maybe_update() tests at :1534.

**Failure scenario.** Input events are dispatched from the display backend independently of the g_idle_add(startup) that creates the first view (source/rofi.c:1336), and remain live after the last view is freed but before the main loop actually stops. A key/mouse event arriving in either window reaches binding_trigger_action -> rofi_view_trigger_global_action with state==NULL; CHANGE_ELLIPSIZE/COPY_SECONDARY/MODE_NEXT then dereference NULL -> SIGSEGV. A theme `timeout { delay: 1; action: "kb-secondary-copy"; }` reaches the same path from a GLib timer (:261-264) with no view at all.

**Proposed fix.** Return early at the top of rofi_view_trigger_global_action, rofi_view_check_action and rofi_view_trigger_action when `state == NULL` (check_action should return FALSE), and/or guard the two callbacks in source/keyb.c.

**Verifier.** The code is exactly as cited: source/view.c:1006 `RofiViewState *state = rofi_view_get_active();` then unconditional derefs at :1043, :1046, :1069, :1081, :1092, :1112, :1158, :1210; rofi_view_check_action reads state->mouse.x at :1364 and rofi_view_trigger_action at :1398; keyb.c:427 and :435-436 pass rofi_view_get_active() through unchecked; view.c:254 does the same. But reachability with NULL is not demonstrable statically: the XCB backend guards the whole event path (source/xcb/display.c:1222-1225 returns early when rofi_view_get_active()==NULL before any nk_bindings dispatch). Only Wayland dispatches bindings before checking (source/wayland/display.c:504-506 and :414 call nk_bindings_seat_handle_key with no view check), and the theme-timeout path needs a timer that outlives the last view. So the missing NULL checks are real, the crash depends on runtime event ordering.

## `source/view.c:938` — Delayed-refilter GSource holds a raw RofiViewState* that rofi_view_free never cancels

- **kind** memory · **severity** medium · **verdict** PLAUSIBLE · **domain** core: source/rofi.c, source/view.c, source/mode.

`CacheState.refilter_timeout = g_timeout_add(200, (GSourceFunc)rofi_view_refilter_real, state);` stores a bare pointer to the view. The source is removed in only three places: at the top of rofi_view_refilter (:923-927), in rofi_view_refilter_force (:952-955), and in the backend global cleanup (source/xcb/view.c:913-916, source/wayland/view.c:506-508). rofi_view_free() (:370-386) frees the state without touching it, and rofi_view_remove_active()/process_result() (source/rofi.c:283-289) free a view while the main loop keeps running whenever another view is stacked underneath (CacheState.views is non-empty).

**Failure scenario.** Delayed mode is entered whenever filtering exceeds config.refilter_timeout_limit (300ms by default) — routine on drun with a large application set. If a view with a pending 200ms refilter source is freed while a stacked view remains (rofi_view_set_active pops rather than quitting, :334-339), the timer fires 200ms later on freed memory: `state->sw` at :782, then `state->text->text` at :797. Use-after-free.

**Proposed fix.** Cancel and zero CacheState.refilter_timeout in rofi_view_free() (and assert it is not pointing at the state being freed), or attach the source to the state and remove it in the state's destructor.

**Verifier.** source/view.c:937-938 does store the bare view pointer: `CacheState.refilter_timeout = g_timeout_add(200, (GSourceFunc)rofi_view_refilter_real, state);`. The only removals are :923-926, :952-954 and the backend cleanups (source/xcb/view.c:913-915, source/wayland/view.c:506-508); rofi_view_free at :369-386 frees line_map/distance/modes/state without touching the source. All ACCEPT_* paths call rofi_view_refilter_force first (:1244, :1259, :1266, :1273), so the exposure needs a quit path that does not — CANCEL (:1169) or the mouse-activated callback (:1607-1615) — combined with a stacked view so the main loop keeps running after the free. I cannot show statically that a view is ever stacked while a delayed refilter is pending, so PLAUSIBLE rather than CONFIRMED.

## `source/widgets/textbox.c:683` — textbox_cursor_inc_word() walks g_utf8_next_char past the NUL terminator

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/textbox.c:682-691:
```
  gchar *c = g_utf8_offset_to_pointer(tb->text, tb->cursor);
  while ((c = g_utf8_next_char(c))) {
    gunichar uc = g_utf8_get_char(c);
    ... if (alphabetic/hebrew/numeric/quotation) break;
  }
```
`g_utf8_next_char(p)` is `p + g_utf8_skip[*p]` and g_utf8_skip[0] == 1, so it happily steps *over* the terminating NUL; it never returns NULL, so the loop condition is always true. The only exit is finding a word character. The post-loop guard at line 692 (`if (c == NULL || *c == '\0')`) is dead for this case because the NUL was already passed. The scan then keeps reading heap memory after the string until it stumbles on a byte classified as ALPHABETIC/NUMERIC, and line 704 computes `g_utf8_pointer_to_offset(tb->text, c)` over that out-of-bounds region.

**Failure scenario.** Type "abc " (trailing space or any non-word char at the end) in the input bar and press Ctrl-Right / Alt-F (MOVE_WORD_FORWARD, textbox.c:963-964). The first loop steps onto the NUL, does not stop, steps past it, and reads unallocated heap until it finds a letter -> heap overread, bogus cursor index, potential SIGSEGV under ASAN or at a page boundary.

**Proposed fix.** Terminate both loops on the NUL explicitly, e.g. `while (*c && (c = g_utf8_next_char(c)))` / check `if (*c == '\0') break;` at the top of each loop body in textbox_cursor_inc_word (source/widgets/textbox.c:683-703).

**Verifier.** source/widgets/textbox.c:682-692 is as quoted: `gchar *c = g_utf8_offset_to_pointer(tb->text, tb->cursor); while ((c = g_utf8_next_char(c))) { ... }` with the only break on a word-class character. g_utf8_next_char is `p + g_utf8_skip[*p]` and g_utf8_skip[0]==1, so it steps over the terminating NUL and never returns NULL; the post-loop guard at line 692 `if (c == NULL || *c == '\0')` is therefore unreachable for the end-of-string case. Cursor at (or before only non-word chars up to) the end of the text — e.g. "abc" with cursor 3, then MOVE_WORD_FORWARD — walks past the allocation calling g_utf8_get_char on unallocated heap. The resulting index is clamped by textbox_cursor (line 643), so the damage is the overread itself, not a bad cursor.

## `source/xcb/display.c:466` — xcb_randr_get_output_info_reply() NULL return dereferenced immediately

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

op_reply is assigned at line 465 and dereferenced at line 466 (`op_reply->crtc`) with no NULL check. xcb_randr_get_output_info_reply() returns NULL on any X error or connection failure. This is the fallback path used whenever RandR < 1.5 or when RandR 1.5 returned no monitors, so it is the normal path on many setups. Every other reply in the same function (crtc_reply, line 473) IS checked, so this is an oversight, not a deliberate contract.

**Failure scenario.** An output listed in GetScreenResourcesCurrent is disconnected/removed by another client (or a GPU hotplug happens) between the resources reply at line 656 and the per-output GetOutputInfo at line 464: the server answers with BadOutput, xcb_randr_get_output_info_reply() returns NULL, and rofi segfaults at line 466 before the window is even shown.

**Proposed fix.** Add `if (op_reply == NULL) { return NULL; }` right after line 465, matching the crtc_reply handling at lines 473-476.

**Verifier.** source/xcb/display.c:465-466: `op_reply = xcb_randr_get_output_info_reply(xcb->connection, it, NULL);` immediately followed by `if (op_reply->crtc == XCB_NONE)`. No NULL check, and the error pointer is NULL so a BadOutput/connection failure yields NULL. The very next reply, crtc_reply, IS checked at line 473 (`if (!crtc_reply)`), confirming the asymmetry. This is the fallback path entered from line 651 `if (xcb->monitors == NULL)`.

## `source/xcb/display.c:572` — Xinerama query_screens reply is passed to the iterator without a NULL check

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

screens_reply (line 569) is handed straight to xcb_xinerama_query_screens_screen_info_iterator() at line 572, which dereferences the reply (`i.rem = R->number`). This is the fallback monitor path used when RANDR is absent but XINERAMA is present.

**Failure scenario.** On a server advertising XINERAMA where the QueryScreens request fails (extension present but request errors, or connection loss), xcb_xinerama_query_screens_reply() returns NULL and the iterator construction at line 572 dereferences NULL — segfault at startup on RandR-less servers.

**Proposed fix.** `if (screens_reply == NULL) { return; }` between lines 570 and 571.

**Verifier.** source/xcb/display.c:568-572: screens_reply is passed straight to xcb_xinerama_query_screens_screen_info_iterator(screens_reply), whose generated body does `i.data = (…)(R + 1); i.rem = R->number;` — an unconditional deref. Aggravating detail the claim did not mention: line 566 uses xcb_xinerama_query_screens_*_unchecked*, so any error is routed to the event loop and _reply returns NULL with no error to inspect.

## `source/xcb/display.c:1466` — XKB MapNotify handler does not check xkb_x11_keymap_new_from_device() for NULL

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

In the XCB_XKB_MAP_NOTIFY branch, keymap (line 1463) is passed straight into xkb_x11_state_new_from_device() at line 1466 and into nk_bindings_seat_update_keymap() at line 1468. The identical call in xcb_display_setup() (line 1738) *does* check for NULL and bails out with a warning (lines 1741-1744), so the handler is missing the same guard.

**Failure scenario.** The user runs `setxkbmap` with a layout that libxkbcommon cannot compile (or the X server sends a map that fails compilation): a MapNotify arrives, xkb_x11_keymap_new_from_device() returns NULL, and xkb_x11_state_new_from_device(NULL, ...) dereferences it — rofi crashes on a keyboard layout switch.

**Proposed fix.** Mirror the setup path: `if (keymap == NULL) { g_warning(...); break; }`, and also check the returned state before calling nk_bindings_seat_update_keymap().

**Verifier.** source/xcb/display.c:1462-1470 (XCB_XKB_MAP_NOTIFY): keymap from xkb_x11_keymap_new_from_device() at 1463 is passed unchecked to xkb_x11_state_new_from_device() at 1466 (which calls xkb_state_new -> xkb_keymap_ref -> deref) and to nk_bindings_seat_update_keymap at 1468. The identical call in xcb_display_setup() at 1738-1744 does check `if (keymap == NULL)` and even checks the resulting state at 1747-1750, so the handler is missing both guards.

## `source/xcb/view.c:593` — open_xim_callback() dereferences rofi_view_get_active() without a NULL check

- **kind** memory · **severity** medium · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

`state` is fetched at line 590 and dereferenced at lines 593-596 (`state->text->widget`). Every other callback in this file guards it: xim_commit_string() checks at lines 534-537, x11_event_handler_fowarding() in display.c checks at lines 1422-1425. xcb_xim_open() is called at view.c:629 from inside xcb___create_window(), i.e. before the view state is fully registered, and the callback fires asynchronously whenever the XIM server answers.

**Failure scenario.** With imdkit enabled (`-enable-imdkit`) and a slow XIM server, the open callback is dispatched from the main loop after the view was cancelled/closed (rofi_view_get_active() == NULL): line 593 dereferences NULL and rofi segfaults.

**Proposed fix.** `if (state == NULL) { return; }` after line 590, or fall back to spot {0,0} when there is no active view.

**Verifier.** source/xcb/view.c:589-596: `RofiViewState *state = rofi_view_get_active();` then unguarded `state->text->widget` at 593 and 595-596. xim_commit_string in the same file checks `if (state == NULL) return;` at 534-537, and x11_event_handler_fowarding in display.c:1422-1425 does the same, so the omission is real. Registered asynchronously via xcb_xim_open() at view.c:629.

## `source/wayland/display.c:472` — wl_keyboard.repeat_info rate==0 means "repeat disabled" but the code repeats at ~33 Hz anyway

- **kind** protocol · **severity** medium · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

wayland_keyboard_repeat_info() (lines 543-552) stores rate/delay verbatim. wayland_keyboard_key() at lines 510-518 unconditionally arms the delay timer for every press, and wayland_key_repeat_delay() at lines 472-476 does `guint repeat_wait_ms = 30; if (self->repeat.rate != 0) repeat_wait_ms = 1000 / self->repeat.rate;` — i.e. rate==0 falls back to a 30 ms repeat. The wl_keyboard protocol explicitly defines rate 0 as "repeat disabled".

**Failure scenario.** User has key repeat turned off in their compositor (sway `input * repeat_rate 0`). Holding a key in sofi's filter box inserts ~33 characters per second, unlike every other Wayland client. Accessibility users who disable repeat for exactly this reason cannot use sofi.

**Proposed fix.** In wayland_keyboard_key(), skip arming the timer entirely when self->repeat.rate == 0; remove the 30 ms fallback at line 472.

**Verifier.** display.c:472-475: `guint repeat_wait_ms = 30; if (self->repeat.rate != 0) repeat_wait_ms = 1000 / self->repeat.rate;` — rate 0 falls back to 30 ms instead of disabling repeat, and wayland_keyboard_repeat_info (547-557) stores rate verbatim while wayland_keyboard_key (518-520) arms the delay timer unconditionally. wl_keyboard.repeat_info defines rate 0 as repeat disabled.

## `source/rofi-icon-fetcher.c:746` — Icon surface and query_done are published from a worker thread with no synchronisation, and rofi_view_reload() is called cross-thread

- **kind** protocol · **severity** medium · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

rofi_icon_fetcher_worker() runs on the GThreadPool (source/view.c:2032) and writes `sentry->surface = icon_surf;` / `sentry->query_done = TRUE;` (source/rofi-icon-fetcher.c:746,748) with no mutex, no atomic store and no release barrier - the code even documents the doubt at line 531 ("is a pointer write atomic?"). The main thread reads the same fields in rofi_icon_fetcher_get() (line 850) and rofi_icon_fetcher_get_ex() (line 862), which icon_draw() calls on every repaint (source/widgets/icon.c:92). The surface's pixel data is written by rofi_icon_fetcher_get_surface_from_pixbuf() before the pointer store, but without a release/acquire pair the reader may observe the pointer before the pixels. Worse, every exit path of the worker calls rofi_view_reload() from the worker thread (lines 545, 646, 675, 694, 749); that dispatches to xcb_rofi_view_reload() (source/xcb/view.c:456-462) / wayland_rofi_view_reload() (source/wayland/view.c:303-308), both of which do an unsynchronised check-then-set of `XcbState.idle_timeout` / `WlState.idle_timeout` around g_timeout_add().

**Failure scenario.** With config.threads > 1 (default = number of CPUs) two icon worker threads finish at the same instant while idle_timeout == 0: both read 0, both call g_timeout_add(), the second overwrites the first source id -> one GSource is leaked and never removed, and the reload timer can no longer be cancelled. Separately, on a weakly-ordered CPU (aarch64) the main thread can read a non-NULL sentry->surface whose ARGB pixels are not yet visible, painting a garbage/blank icon.

**Proposed fix.** Publish with g_atomic_pointer_set/get (or a GMutex) around sentry->surface and sentry->query_done in source/rofi-icon-fetcher.c:746-748 / 850 / 862, and make the reload trampoline thread-safe by scheduling with g_idle_add_full from the worker and doing the idle_timeout bookkeeping only on the main thread.

**Verifier.** All cited code checks out. source/rofi-icon-fetcher.c:531 carries the comment "is a pointer write atomic?"; the worker writes sentry->surface at line 746 (and 673) and sentry->query_done at 748 (and 544, 645, 674, 693) with no lock/atomic/barrier, while the main thread reads both in rofi_icon_fetcher_get (line 850) and rofi_icon_fetcher_get_ex (lines 862-863), called from icon_draw (source/widgets/icon.c:91-92). rofi_view_reload is invoked from the worker on every exit path (545, 646, 675, 694, 749); it is `proxy->reload()` (source/view.c:2186) landing in xcb_rofi_view_reload (source/xcb/view.c:456-462) and wayland_rofi_view_reload (source/wayland/view.c:303-309), both of which are an unsynchronised `if (State.idle_timeout == 0) State.idle_timeout = g_timeout_add(...)`. The pool really is multi-threaded (source/view.c:2023-2039, config.threads defaults to the CPU count). The duplicate-timeout leak is a genuine race; the missing acquire/release is a real but x86-benign ordering hole.

## `source/modes/wayland-window.c:723` — Display-string formatting is copy-pasted between the XCB and Wayland window modes and has already diverged

- **kind** structure · **severity** medium · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

`struct arg`, helper_eval_add_str, helper_eval_cb, _generate_display_string and the literal regex "{[-\\w]+(:-?[0-9]+)?}" exist twice: source/modes/wayland-window.c:472,723-795 and source/modes/window.c:716/735,877-948. None of it is Wayland- or X11-specific — it is pure config.window_format rendering. The copies have already drifted (see window-c-unescaped-markup: the escaping fix at source/modes/wayland-window.c:742 was never back-ported to source/modes/window.c:901). The same duplication exists in the view layer: loc_transtable[9] is byte-identical at source/wayland/view.c:141-144 and source/xcb/view.c:275-278, and the CacheState timer teardown plus input_history_save() is duplicated at source/wayland/view.c:506-523 vs source/xcb/view.c:913-928,956.

**Failure scenario.** Any future fix to window_format rendering (padding, escaping, a new token) applied to one file leaves the other backend behaving differently, exactly as the markup-escaping fix did.

**Proposed fix.** Hoist the format helpers into a shared source/modes/window-format.c taking a small accessor struct, and hoist loc_transtable + the CacheState teardown into source/view.c.

**Verifier.** The regex literal "{[-\\w]+(:-?[0-9]+)?}" appears at source/modes/window.c:716 and 735 and source/modes/wayland-window.c:472; helper_eval_add_str/helper_eval_cb/_generate_display_string are near-identical pairs (window.c:882-948 vs wayland-window.c:723-795) and have already diverged at the escaping branch (window.c:901 vs wayland-window.c:742). loc_transtable[9] is byte-identical at source/wayland/view.c:141-144 and source/xcb/view.c:275-278, and the CacheState timer teardown plus input_history_save() is duplicated at source/wayland/view.c:506-523 vs source/xcb/view.c:913-928 and 956.

## `source/modes/window.c:870` — window and windowcd modes share one global client cache that either one frees on destroy

- **kind** structure · **severity** medium · **verdict** PLAUSIBLE · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

`cache_client` (line 159) is a single process-global winlist shared by both window_mode and window_mode_cd. window_mode_destroy() (line 865) unconditionally calls x11_cache_free() (line 870) even though the *other* mode may still be live and still holds window ids in its own pd->ids that index into that cache. window_client_reload() (lines 403-417) destroys and re-initialises both modes in sequence, so the _cd destroy at line 410 frees the cache that window_mode's _init at line 407 just repopulated. window_match() then does winlist_find() + `g_assert(idx >= 0)` (lines 434-435) on a cache that no longer contains those windows.

**Failure scenario.** Run `rofi -modes window,windowcd -show window`, switch modes once so both are initialised, with windows open on more than one desktop. Open or close any X window: display.c:1243/1254 -> window_client_handle_signal -> 100ms timer -> window_client_reload destroys+inits window_mode, then destroys windowcd, freeing the cache. Type any character to trigger filtering: window_match() -> winlist_find() returns -1 -> `g_assert(idx >= 0)` aborts (and with asserts compiled out, `cache_client->data[-1]` is an OOB read followed by a wild pointer dereference at line 436). x11_cache_create() is only called from _window_mode_load_data, so window_client() at line 391 can also hit winlist_append(NULL, ...) -> NULL deref at line 184.

**Proposed fix.** Refcount the cache (increment in x11_cache_create, only free at zero in x11_cache_free), or move the cache into WindowModePrivateData so each mode owns its own.

**Verifier.** The structural facts hold: `winlist *cache_client` is a file-global (window.c:159), x11_cache_free() (273-276) frees it unconditionally from window_mode_destroy() (line 870), and both window_mode._destroy and window_mode_cd._destroy point at that same function. BUT the asserted crash mechanism is wrong: window_client_reload (403-417) calls _destroy immediately followed by _init inside each `if`, and window_mode_cd._init -> _window_mode_load_data -> x11_cache_create() (line 550) then re-populates the cache with *every* window in the client list (the `cd` filter at line 686 only affects pd->ids, not the cache). So after line 411 the cache is repopulated and winlist_find() at 434 succeeds; there is no live use of the cache between 410 and 411. Only a genuine race survives: if a window present in window_mode's pd->ids disappears from _NET_CLIENT_LIST between the enumeration at 407 and the one at 411, winlist_find returns -1 and `g_assert(idx >= 0)` (435) aborts. The claimed winlist_append(NULL,...) path at line 391 is not reachable — window_client() is only called at 605 (after x11_cache_create at 550) and at 954/1078 where the cache exists.

## `source/rofi-icon-fetcher.c:562` — Icon-fetcher worker thread calls into the view/UI layer through helper_parse_setup()

- **kind** structure · **severity** medium · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

rofi_icon_fetcher_worker() runs on a thread-pool thread and calls helper_parse_setup(config.preview_cmd, ...) at line 562. On a parse failure helper_parse_setup calls rofi_view_error_dialog() (source/helper.c:138), and helper_string_replace_if_exists_v does the same on regex failure (source/helper.c:1787) — both mutate view state that is otherwise owned exclusively by the main thread. The worker also calls rofi_view_reload() at lines 545, 646, 675, 694 and 749; that resolves to source/xcb/view.c:456, which performs an unsynchronised test-and-set of XcbState.idle_timeout (`if (XcbState.idle_timeout == 0) XcbState.idle_timeout = g_timeout_add(...)`) while the main thread clears the same field at source/xcb/view.c:451.

**Failure scenario.** Set an unparseable `preview-cmd` (e.g. `preview-cmd: "foo '{input}";` with an unbalanced quote) and open filebrowser with thumbnails: several worker threads simultaneously enter rofi_view_error_dialog and mutate the shared RofiViewState. Independently, two workers finishing at once can both observe XcbState.idle_timeout == 0 and both call g_timeout_add, so one GSource id is overwritten and that timeout is never removed — a leaked, permanently-armed main-loop source.

**Proposed fix.** Make helper_parse_setup report errors via a GError to the caller instead of driving the UI, and have the worker marshal any user-visible failure and its reload request onto the main context with g_idle_add()/g_main_context_invoke(). Make the idle_timeout test-and-set atomic.

**Verifier.** source/rofi-icon-fetcher.c:562 calls helper_parse_setup(config.preview_cmd, ...) from rofi_icon_fetcher_worker (thread-pool callback, :528), and helper_parse_setup calls rofi_view_error_dialog at source/helper.c:138 on parse failure, as does helper_string_replace_if_exists_v at source/helper.c:1787. The reload race is also as described: source/xcb/view.c:456-461 `if (XcbState.idle_timeout == 0) XcbState.idle_timeout = g_timeout_add(...)` is an unsynchronised test-and-set called from the worker (:545,646,675,694,749) while the main-thread idle handler clears it at source/xcb/view.c:452.

## `source/view.c:364` — Backend-agnostic view.c contains raw XCB calls that belong behind display.h

- **kind** structure · **severity** medium · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

source/view.c is the shared view layer sitting on top of the view_proxy vtable (include/view-internal.h:169-201), yet it includes xcb-internal.h/xcb.h (:58-63) and issues XCB calls directly: xcb_clear_area/xcb_flush at :364-365, xcb_convert_selection at :1012 and :1028, xcb_stuff_set_clipboard/xcb_set_selection_owner at :1052-1054, xcb_map_window/xcb_flush at :1906/:1913 and :1976. The Wayland arms of the same switches are stubbed ("// TODO" at :1060). The abstraction also leaks the other way: the shared CacheState carries an `xcb_window_t main_window` (include/view-internal.h:213) and the proxy signatures take `xcb_configure_notify_event_t*` / `xcb_window_t` even on Wayland, papered over by xcb-dummy.h.

**Failure scenario.** COPY_SECONDARY silently does nothing on Wayland because the clipboard write has no Wayland arm (:1058-1062), while the XCB arm is inlined here rather than expressed as a display_set_clipboard_data() proxy entry. Any new backend must add another #ifdef arm inside view.c rather than implementing a vtable slot.

**Proposed fix.** Add clipboard get/set, map-window and force-expose entries to the view_proxy/display vtable and move these blocks into source/xcb/view.c and source/wayland/view.c. Note that source/view.c:1904-1907 also tests `xcb->connection` without the `config.backend == DISPLAY_XCB` guard used at :1975, an inconsistency the vtable would remove.

**Verifier.** Every citation checks out: source/view.c:58-63 is the `#ifdef ENABLE_XCB / xcb-internal.h / xcb.h / #else / xcb-dummy.h` block; xcb_clear_area+xcb_flush at :363-365; xcb_convert_selection at :1012 and :1028; xcb_stuff_set_clipboard + xcb_set_selection_owner at :1052-1054 with the Wayland arm being a bare `// TODO` at :1058-1061 (so COPY_SECONDARY really is a no-op on Wayland); xcb_map_window at :1906 and :1976. include/view-internal.h:169 opens `typedef struct _view_proxy {` with xcb_configure_notify_event_t*/xcb_window_t in its signatures (:172-174, :189), and :212-213 is `/** main x11 windows */ xcb_window_t main_window;` in the shared cache state. The layering violation and the Wayland clipboard gap are both real.



---

# LOW

## `.build.yml:19` — CI installs libxcb-keysyms1 (runtime) not -dev, and no wayland packages, so meson aborts at the backend assert

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

meson.build:92 requires `dependency('xcb-keysyms')`, which needs xcb-keysyms.pc from libxcb-keysyms1-dev. .build.yml:19 installs the runtime package libxcb-keysyms1 instead. Because `xcb` defaults to the `auto` feature (meson_options.txt:6), the missing .pc does not error at meson.build:92 - it silently sets xcb_enabled = false via the reduction at meson.build:98-100. The package list also contains no libwayland-dev and no wayland-protocols, so wayland_enabled is false too (meson.build:113-116). Control then reaches meson.build:126.

**Failure scenario.** `meson setup builddir . -Db_lto=true` (.build.yml:36) dies at meson.build:126 with "At least one of these options must be found: wayland, xcb", naming none of the twelve dependencies that were actually missing.

**Proposed fix.** Install libxcb-keysyms1-dev, and add libwayland-dev + wayland-protocols so both backends are exercised. Drop the stale libxcb-xrm-dev (see stale-xrm-dep).

**Verifier.** .build.yml:19 is `  - libxcb-keysyms1` (the runtime package; the headers/.pc are in libxcb-keysyms1-dev). I read the whole package list (.build.yml:2-30) — it contains no libwayland-dev and no wayland-protocols. meson_options.txt:5-6 make both wayland and xcb `auto`, so meson.build:92 `dependency('xcb-keysyms', required: xcb_opt)` does not error; the reductions at meson.build:97-100 and 113-116 set both flags false and the assert at meson.build:126-127 fires with the generic 'At least one of these options must be found: wayland, xcb'. The chain is deterministic from what I read. Severity dropped to low for the same reason as buildyml-builds-upstream: this manifest builds upstream, not this repo, so no one is hitting it.

## `.build.yml:47` — Artifact path hardcodes rofi-1.7.8-dev.tar.xz while meson.build declares version 2.0.0-dev

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

`artifacts: - rofi/builddir/meson-dist/rofi-1.7.8-dev.tar.xz`. `meson dist` names the tarball <project_name>-<version>.tar.xz, and meson.build:1-2 declare `project('rofi', ... version: '2.0.0-dev')`, so the produced file is rofi-2.0.0-dev.tar.xz. The path is doubly wrong once the project is renamed to sofi.

**Failure scenario.** The `dist` task (line 45) succeeds and writes builddir/meson-dist/rofi-2.0.0-dev.tar.xz, then artifact collection fails to find rofi-1.7.8-dev.tar.xz and the release tarball is never published.

**Proposed fix.** Use a glob (`rofi/builddir/meson-dist/*.tar.xz`) as .github/actions/release/action.yml:15 already does, so the path survives both the version bump and the rename.

**Verifier.** .build.yml:47 is `  - rofi/builddir/meson-dist/rofi-1.7.8-dev.tar.xz`; meson.build:1-2 are `project('rofi', 'c',` / `    version: '2.0.0-dev',`. meson dist names the tarball <project>-<version>.tar.xz, so the artifact path can never match. Real mismatch; severity low because the manifest never runs against this repo.

## `.build.yml:43` — Doxygen warning gate greps for "warnings" but doxygen emits "warning:"

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

`if [ $(grep -c warnings doxygen.log) -gt 0 ]; then exit 1; fi`. Doxygen diagnostics are of the form <file>:<line>: warning: <text> - singular, with a colon. The sibling GitHub action gets this right: .github/actions/doxycheck/action.yml:12 uses `grep -c warning:`. The sourcehut gate therefore matches nothing and never fails.

**Failure scenario.** A public symbol in include/ loses its doxygen comment; doxygen prints "warning: Member foo() is not documented"; `grep -c warnings` returns 0; the task exits 0 and the undocumented-API regression is merged.

**Proposed fix.** Change the pattern to `warning:` to match .github/actions/doxycheck/action.yml:12.

**Verifier.** .build.yml:43 is `if [ $(grep -c warnings doxygen.log) -gt 0 ]; then exit 1; fi`, and .github/actions/doxycheck/action.yml:12 is `if [[ "$(grep -c warning: builddir/doxygen.log)" != 0 ]]; then`. Doxygen emits `<file>:<line>: warning: <text>`, singular, so the sourcehut gate matches nothing while its GitHub sibling matches correctly. The divergence between the two gates is exactly as described.

## `.build.yml:16` — CI installs libxcb-xrm-dev, which the project no longer uses

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

libxcb-xrm-dev appears at .build.yml:16 and again at .github/actions/setup/action.yml:68. Grepping source/, include/ and meson.build for xcb_xrm or xcb-xrm returns nothing - the dependency list at meson.build:82-95 has no xcb-xrm entry. .gitlab-ci.yml:8-13 goes further and builds Airblader/xcb-util-xrm from git on every run.

**Failure scenario.** n/a - pure waste; every CI run installs (and in the GitLab case compiles from source) a library nothing links against, adding minutes to each job.

**Proposed fix.** Remove libxcb-xrm-dev from .build.yml:16 and .github/actions/setup/action.yml:68, and delete the xcb-util-xrm source build at .gitlab-ci.yml:8-13 along with the rest of that file.

**Verifier.** .build.yml:16 `  - libxcb-xrm-dev` and .github/actions/setup/action.yml:68 `          libxcb-xrm-dev \`; .gitlab-ci.yml:8-13 clones Airblader/xcb-util-xrm and does autogen/make/sudo make install on every run. `grep -rn 'xcb_xrm\|xcb-xrm' source/ include/ meson.build lexer/ config/` returns zero hits, and the dependency list at meson.build:82-95 has no xcb-xrm entry. Pure waste, as the claim itself says.

## `.gitattributes:18` — Release tarball ships dead pkgconfig/, .clang-tidy and AGENTS.MD

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

The export-ignore list covers .build.yml, .gitattributes, .gitignore, .mailmap, .github, .gitlab-ci.yml, .gitmodules, .travis.yml, releasenotes, several doc/ artefacts, mkdocs and script/*.jpg (lines 1-18). It omits `pkgconfig` (dead - see dead-pkgconfig-template), `.clang-tidy` (developer-only; its entire contents are `Checks: -clang-analyzer-optin.core.EnumCastOutOfRange`), and `AGENTS.MD` (5093 bytes of agent instructions added by this fork, at repo root). It also still lists .travis.yml at line 8, which no longer exists.

**Failure scenario.** `meson dist` produces a tarball containing AGENTS.MD and a misleading pkgconfig/rofi.pc.in; distro review (Debian/Fedora/FreeBSD ports) flags the unexplained files or, worse, a packager acts on the stale .pc.in.

**Proposed fix.** Add `pkgconfig export-ignore`, `.clang-tidy export-ignore` and `AGENTS.MD export-ignore`; drop the obsolete .travis.yml line.

**Verifier.** Read .gitattributes in full — 18 lines, exactly the set listed, including line 8 `.travis.yml export-ignore` while `ls -la .travis.yml` returns 'No such file or directory' and it is absent from `git ls-tree HEAD`. No entry for pkgconfig, .clang-tidy or AGENTS.MD; all three are tracked at the repo root (AGENTS.MD is 5093 bytes; `cat .clang-tidy` is the single line `Checks: -clang-analyzer-optin.core.EnumCastOutOfRange`). Every stated fact checks out.

## `.github/actions/setup/action.yml:22` — CI never installs flex, bison or pkg-config, relying entirely on the runner image's preinstalled set

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

meson.build:220 and :224 call find_program('flex') and find_program('bison') - both hard, unguarded requirements. The apt list at .github/actions/setup/action.yml:22-45 contains neither, nor pkg-config, nor libglib2.0-dev (glib headers arrive only transitively via libpango1.0-dev at line 30). INSTALL.md:24-25 correctly lists flex >= 2.5.39 and bison as required. The .build.yml package list does list flex and bison explicitly (lines 27-28), so the two CIs disagree about what is a declared dependency.

**Failure scenario.** GitHub changes the ubuntu-latest image contents and drops preinstalled bison; every job in .github/workflows/build.yml then fails at meson.build:224 with "Program 'bison' not found", with no clue that the CI setup action was the thing that should have installed it.

**Proposed fix.** Add flex, bison, pkg-config and libglib2.0-dev to the apt list at .github/actions/setup/action.yml:22-45 so the dependency is declared rather than inherited.

**Verifier.** meson.build:220 `flex = generator(find_program('flex'),` and :224 `bison = generator(find_program('bison'),` — both unguarded (no required: false). I read .github/actions/setup/action.yml:22-45 in full: the list is discount, doxygen, fluxbox, gdb, graphviz, jq, lcov, libpango1.0-dev, libxkbcommon-dev x2, libxkbcommon-x11-dev, libgdk-pixbuf-2.0-dev, ninja-build, pandoc, python3-*, texi2html, texinfo, xdotool, xfonts-base, xterm, xutils-dev — no flex, no bison, no pkg-config, no libglib2.0-dev. .build.yml:27-28 does list flex and bison, so the two CIs genuinely disagree. One detail in the claim is wrong: glib dev headers arrive transitively via libgdk-pixbuf-2.0-dev (line 34), which Depends on libglib2.0-dev, not via libpango1.0-dev. Works today only because the ubuntu-latest image preinstalls flex/bison, hence low.

## `.gitlab-ci.yml:26` — GitLab CI is 100% autotools and cannot run - no configure.ac/Makefile.am exists

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

The whole pipeline is autotools: `autoreconf -i` (line 26), `./configure --enable-gcov` (27), `make`/`make check`/`make distcheck`/`make coverage`/`make doxy` (28-32). `ls configure.ac Makefile.am` returns "No such file or directory" - the project is meson-only (meson.build:1). It additionally depends on the dead debian.jpleau.ca jessie-backports repo (line 5), `apt-get --force-yes` (removed from modern apt), `python2` (line 33), and `sudo` inside a container image that is never named (no `image:` key at all). Dead since the autotools removal.

**Failure scenario.** Any GitLab mirror of the fork runs the `build-rofi` job; `autoreconf -i` exits non-zero immediately ("no configure.ac"), so the job fails 100% of the time before compiling anything.

**Proposed fix.** Delete .gitlab-ci.yml, or rewrite it as a meson pipeline. If deleted, also drop the `.gitlab-ci.yml export-ignore` line at .gitattributes:6.

**Verifier.** Read .gitlab-ci.yml in full: line 26 `autoreconf -i`, 27 `./configure --enable-gcov`, 28-32 make/check/distcheck/coverage/doxy, 33 `python2 doxy-coverage/...`. `ls configure.ac Makefile.am` -> both 'No such file or directory'; meson.build:1 `project('rofi', 'c',`. Also verified: no `image:` key anywhere in the file, line 4 adds the dead debian.jpleau.ca jessie-backports repo, lines 3/6/7 use `apt-get install --force-yes`, lines 12/19 `sudo` in a container. Every stated fact holds. Downgraded from high to low: the file is dead residue with zero effect unless someone deliberately sets up a GitLab mirror — a cleanup item, not a live breakage.

## `.gitlab-ci.yml:26` — GitLab CI is entirely autotools and cannot build this meson-only project

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

The `build-rofi` job runs `autoreconf -i` (line 26), `./configure --enable-gcov` (line 27), `make` (28), `make check` (29), `make distcheck` (30), `make coverage` (31), `make doxy` (32). There is no configure.ac or Makefile.am anywhere in the tree — meson.build line 1 is the sole build definition and INSTALL.md line 6 states "Rofi uses Meson as build system". Line 33 also invokes `python2 doxy-coverage/doxy-coverage.py`, and python2 has been end-of-life since 2020 (line 7 installs the `python` package, which no longer exists on any current Debian/Ubuntu). Line 4 adds a `jessie-backports` repo from a third-party host (debian.jpleau.ca) that has been dead for years.

**Failure scenario.** Any push to a GitLab mirror fails in `before_script` at line 3 (`--force-yes` was removed from apt in Debian 10) or at line 26 with `autoreconf: 'configure.ac' or 'configure.in' is required`. The pipeline has been red-by-construction since the autotools removal.

**Proposed fix.** Either delete .gitlab-ci.yml (the project's real CI is .github/workflows/build.yml) or rewrite it as `meson setup build && ninja -C build && ninja -C build test`.

**Verifier.** .gitlab-ci.yml lines 26-33 are exactly `autoreconf -i`, `./configure --enable-gcov`, `make`, `make check`, `make distcheck`, `make coverage`, `make doxy`, `python2 doxy-coverage/doxy-coverage.py`. `find . -name configure.ac -o -name Makefile.am` returns only ./subprojects/libnkutils/ -- nothing at the project root -- and meson.build line 1 is `project('rofi', 'c',` with INSTALL.md:6 stating Meson is the build system. Line 4's `debian.jpleau.ca jessie-backports` and line 7's `python` package are as described. Severity corrected down: nothing consumes this file (origin is GitHub per the task brief, and .github/workflows carries the live CI), so it is dead residue with zero runtime impact -- a rebrand-cleanup item, not a high-severity build break.

## `doc/meson.build:28` — Man page target invokes the literal string 'pandoc' instead of the found program object

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

doc/meson.build:16 does `pandoc = find_program('pandoc', required: false, version: '>=2.9')` and line 18 gates on pandoc.found(), but the custom_target command at line 28 begins `command: [ 'pandoc', '--standalone', ...]` - a bare string, not the pandoc object. Meson resolves the bare string against PATH at build time, discarding both the resolved path and the >=2.9 version constraint that find_program enforced.

**Failure scenario.** On a system where the >=2.9 pandoc is installed outside PATH but discoverable by meson (e.g. via a machine file `binaries` entry), configure succeeds because find_program locates it, then ninja invokes an older PATH pandoc - or none at all, failing with "pandoc: command not found" for all ten man pages.

**Proposed fix.** Replace the literal 'pandoc' at doc/meson.build:28 with the pandoc program object.

**Verifier.** doc/meson.build:16 `pandoc = find_program('pandoc', required: false, version: '>=2.9')`, line 18 `if pandoc.found()`, and line 28 `command: [ 'pandoc', '--standalone', '--to=man',` — a bare string, not the `pandoc` program object, so the resolved path and the >=2.9 constraint that find_program enforced are both discarded for all ten targets in man_files (doc/meson.build:1-12).

## `doc/meson.build:57` — abs_builddir is computed by joining two absolute paths

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

`doxy_conf.set('abs_builddir', join_paths(meson.project_build_root(), meson.current_build_dir()))`. meson.current_build_dir() already returns an absolute path, and join_paths of (absolute, absolute) discards the first argument and yields the second. The result is correct by accident.

**Failure scenario.** n/a - currently correct. It misleads anyone editing the doxygen output path, since the first argument looks load-bearing but is silently thrown away.

**Proposed fix.** Reduce doc/meson.build:57 to `doxy_conf.set('abs_builddir', meson.current_build_dir())`.

**Verifier.** doc/meson.build:57 is verbatim `doxy_conf.set('abs_builddir', join_paths(meson.project_build_root(), meson.current_build_dir()))`. meson.current_build_dir() is absolute and join_paths(absolute, absolute) discards the first argument, so the first argument is dead. Confirmed as read, but it is not a defect — the value produced is correct, and the claim concedes as much ('n/a - currently correct'). Severity none; at most a readability nit.

## `include/timings.h:75` — The no-op TIMINGS macros are attached to the include guard's #else and are unreachable

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

The file opens `#ifndef ROFI_TIMINGS_H / #define ROFI_TIMINGS_H` at :33-34 and closes with `#else` at :75 followed by empty TIMINGS_START/TIMINGS_STOP/TICK/TICK_N definitions and `#endif // ROFI_TIMINGS_H` at :96. Because that #else belongs to the include guard, the stub definitions are only ever reached on a *second* inclusion in the same translation unit — where they would redefine the already-defined macros with different bodies (a constraint violation / -Wmacro-redefined). The intended conditional was clearly a TIMINGS build option, which no longer exists anywhere in meson.build.

**Failure scenario.** There is no way to build without timing instrumentation: every TICK()/TICK_N() in source/rofi.c and source/view.c compiles to a real rofi_timings_tick() call plus a g_debug() with a format string, in every build. And if any header ever ends up including timings.h twice in one TU, the stubs fire and the build breaks on macro redefinition.

**Proposed fix.** Either delete the dead #else block, or restore a real `#ifdef TIMINGS` around the two macro sets nested *inside* the include guard and wire a meson option to it.

**Verifier.** include/timings.h has exactly one `#ifndef ROFI_TIMINGS_H` (line 33, `#define ROFI_TIMINGS_H` on 34), one `#else` (line 75) and one `#endif // ROFI_TIMINGS_H` (line 96) — verified with grep of all preprocessor directives in the file. The no-op TIMINGS_START/TIMINGS_STOP/TICK/TICK_N stubs therefore hang off the include guard, so they are reached only on a second inclusion in the same TU, where they would redefine the real macros with different bodies. There is no TIMINGS build option in meson.build, so the instrumentation is unconditional. Build-hygiene issue, not a runtime defect.

## `meson-dist-script:11` — meson dist fails on any machine without pandoc, because the dist script unconditionally copies generated man pages

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

The script runs with `set -eu` (line 3), then `ninja -C build` (10), `cp build/doc/*.1 doc` (11), `cp build/doc/*.5 doc` (12). When pandoc is absent, doc/meson.build:18 takes the else branch, no man custom_targets are created, and build/doc/ contains no .1/.5 files, yet `ninja -C build` still succeeds. The cp at line 11 then aborts the whole dist. There is no fallback to pre-generated man pages either: `ls doc` shows only .markdown sources (rofi.1.markdown etc.), no rofi.1.

**Failure scenario.** A packager without pandoc runs `meson dist -C build`; the script reaches line 11 and exits with "cp: cannot stat 'build/doc/*.1': No such file or directory", leaving a half-populated MESON_DIST_ROOT and no tarball.

**Proposed fix.** Either make pandoc required in doc/meson.build:16 when running under dist, or guard the copies (`for f in build/doc/*.1; do [ -e "$f" ] || continue; cp "$f" doc; done`) and fail loudly with an explanatory message.

**Verifier.** meson-dist-script line 3 `set -eu`, 9 `meson setup build -Dprefix=/usr`, 10 `ninja -C build`, 11 `cp build/doc/*.1 doc`, 12 `cp build/doc/*.5 doc`. doc/meson.build:18 gates man generation on pandoc.found(); the else branch (39-51) only calls install_man on pre-existing files and otherwise just warning()s — it creates no build/doc/*.1, and ninja still succeeds. `ls doc` shows only *.markdown sources (rofi.1.markdown etc.), no rofi.1. So the unguarded cp at line 11 aborts dist under set -e. Registered via meson.build:20 `meson.add_dist_script('meson-dist-script')`. Low because pandoc is a documented build requirement and this only bites the maintainer running meson dist.

## `meson.build:130` — imdkit is probed and linked in wayland-only builds even though it is used only by the xcb backend

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

`if get_option('imdkit')` at meson.build:130 has no xcb_enabled guard, and `deps += imdkit_new` / `deps += imdkit_old` (lines 134, 138) unconditionally add the dependency. imdkit defaults to true (meson_options.txt:4). XCB_IMDKIT is consumed only in source/xcb/display.c (lines 34, 94, 1375, 1389, 1417, 1454, 1644, 1657, 1667) plus an informational printf at source/rofi.c:443 - nothing under source/wayland/ touches it. Since xcb-imdkit.pc pulls in xcb, a wayland-only build acquires a link-time dependency on libxcb-imdkit and libxcb for zero functionality.

**Failure scenario.** `meson setup build -Dxcb=disabled` on a machine with xcb-imdkit installed: the resulting wayland-only binary links libxcb-imdkit + libxcb, and `sofi -help` prints "imdkit enabled" (source/rofi.c:443-445) for a backend that has no IME support at all.

**Proposed fix.** Wrap meson.build:130-145 in `if xcb_enabled and get_option('imdkit')`, and set XCB_IMDKIT false in the else branch.

**Verifier.** meson.build:130 is `if get_option('imdkit')` with no xcb_enabled test; 134 `deps += imdkit_new` and 138 `deps+= imdkit_old` are unconditional inside that block; meson_options.txt:4 defaults imdkit to true. `grep -rn XCB_IMDKIT source/ include/` returns only source/xcb/display.c, source/xcb/view.c, include/xcb.h, include/xcb-internal.h, plus source/rofi.c:443 — I read source/rofi.c:443-449 and it is only the `printf("\t• imdkit  %senabled%s\n", ...)` capability banner. Nothing under source/wayland/ references it. So a -Dxcb=disabled build does acquire libxcb-imdkit (and transitively libxcb) for zero functionality and misreports the capability. Not a build break, hence low.

## `meson.build:98` — A single missing xcb sub-dependency silently disables the entire X11 backend with no diagnostic

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

The reduction is `xcb_enabled = xcb_opt.allowed()` then `foreach dep: xcb_deps / xcb_enabled = xcb_enabled and dep.found() / endforeach` (meson.build:97-100). With the default `auto` feature (meson_options.txt:6), any one of the twelve entries at meson.build:82-95 being absent flips xcb_enabled to false without printing which one. The same pattern applies to wayland at meson.build:113-116. The only downstream signal is the generic assert at meson.build:126-127, "At least one of these options must be found: wayland, xcb", which names no dependency. libstartup-notification-1.0 (line 94) is in this list as a hard xcb requirement - it is genuinely used (source/xcb/display.c:70,1776,1931; source/xcb/view.c:57,784; source/view.c:1926,1983; include/xcb-internal.h:33,59) so it cannot simply be dropped, but it is unmaintained and the most likely one to be missing on a minimal BSD system.

**Failure scenario.** A FreeBSD user without x11/startup-notification installed runs `meson setup build`. Meson reports "Dependency libstartup-notification-1.0 found: NO" among dozens of lines, then quietly builds a wayland-only binary. The user later runs it under Xorg and gets a runtime failure with no memory of a configure-time warning.

**Proposed fix.** Inside the foreach, emit `message('X11 backend disabled: @0@ not found'.format(dep.name()))` on each miss, and include the collected missing names in the assert message at meson.build:126.

**Verifier.** meson.build:97-100 are `xcb_enabled = xcb_opt.allowed()` / `foreach dep: xcb_deps` / `  xcb_enabled = xcb_enabled and dep.found()` / `endforeach`, over the twelve entries at 82-95; the wayland mirror is at 113-116; the only downstream signal is the generic assert at 126-127. libstartup-notification-1.0 at line 94 is genuinely used — I confirmed source/xcb/display.c:70 `#include <libsn/sn.h>`, :731 sn_launcher_context_new, :1776 sn_launchee_context_new_from_environment, :1931 sn_launchee_context_unref, include/xcb-internal.h:33/58. Mitigation the claim understates: meson does print a per-dependency 'Dependency libstartup-notification-1.0 found: NO' line, so the information exists in the configure log — this is a summary/UX gap, not a silent loss of information.

## `meson.build:210` — No AppStream metainfo file exists, so the rebrand has no software-centre identity to carry over

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

meson.build:210-218 installs data/rofi-theme-selector.desktop, data/rofi.desktop and data/rofi.svg, but `find . -name '*.appdata*' -o -name '*.metainfo*'` (excluding .git) returns nothing. `ls data` confirms only the two .desktop files, rofi.png and rofi.svg. The .desktop files carry Icon=rofi, which must move in lockstep with the installed icon basename at meson.build:216.

**Failure scenario.** n/a - the consequence is that GNOME Software / KDE Discover show the app with no description or screenshots, and the rebrand has no component-id to reserve for the new name.

**Proposed fix.** Not required for the rebrand, but if one is added, choose the reverse-DNS component id at the same time as the desktop-file rename so both land in one commit.

**Verifier.** meson.build:210-218 install data/rofi-theme-selector.desktop, data/rofi.desktop (desktop_install_dir) and data/rofi.svg (icondir, meson.build:46). `find . \( -name '*appdata*' -o -name '*metainfo*' \) -not -path './.git/*'` returns nothing, and `ls -la data` shows only rofi-theme-selector.desktop, rofi.desktop, rofi.png, rofi.svg. data/rofi.desktop:8 is `Icon=rofi` (and :6 `Exec=rofi -show`), which must move in lockstep with the installed svg basename. Correct on every point; it is a gap, not a defect.

## `pkgconfig/rofi.pc.in:10` — pkgconfig/rofi.pc.in is dead autotools residue and contradicts the real pkg-config output

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

Nothing references this file: grepping the tree for `rofi.pc` and `.pc.in` outside .git returns no build-system hit (the only pkgconfig mentions in meson.build are wayland_protocols.get_variable(pkgconfig: 'pkgdatadir') at line 315 and import('pkgconfig') at line 417). The real .pc is synthesised by pkg.generate(...) at meson.build:419-428. The stale template also disagrees with reality: line 10 says `Requires.private: glib-2.0 >= 2.40 gmodule-2.0 cairo pango` whereas meson.build:48-55 sets the glib floor to 2.72 and requires_private: plugins_deps covers only glib-2.0/gmodule-2.0/cairo (no pango). It is not export-ignored in .gitattributes, so it ships in every release tarball.

**Failure scenario.** A downstream packager finds pkgconfig/rofi.pc.in in the tarball, wires it into their spec/port as the canonical pkg-config data, and produces a rofi.pc advertising glib >= 2.40 - allowing a plugin to build against a glib too old for the installed headers' GLIB_VERSION_MIN_REQUIRED (meson.build:163).

**Proposed fix.** Delete pkgconfig/rofi.pc.in. If kept for reference, add `pkgconfig export-ignore` to .gitattributes so it stays out of the tarball.

**Verifier.** pkgconfig/rofi.pc.in exists; line 8-10 are `Name: rofi` / `Description: Header files for rofi plugins` / `Requires.private: glib-2.0 >= 2.40 gmodule-2.0 cairo pango`. Grepping the build files for `rofi.pc`/`.pc.in`/`pkgconfig` yields only meson.build:315 (wayland_protocols.get_variable) and meson.build:417 `pkg = import('pkgconfig')`; the real file comes from pkg.generate at meson.build:419-428 with `requires_private: plugins_deps`, and plugins_deps at meson.build:51-55 is glib-2.0 >= 2.72 / gmodule-2.0 / cairo — no pango, and a floor 32 minor versions higher. I read .gitattributes in full (18 lines): no `pkgconfig` entry, so it ships in tarballs.

## `script/get_git_rev.sh:1` — script/get_git_rev.sh is unreferenced autotools residue

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

The script writes a `#define GIT_VERSION "..."` header. Grepping the whole tree for get_git_rev outside .git yields only two historical mentions: Changelog:323 and releasenotes/1.3.0/release-1.3.0.markdown:98. Nothing in meson.build, doc/meson.build, or any CI file invokes it. Version-from-git is instead handled by libnkutils via header_conf.set('USE_NK_GIT_VERSION', true) at meson.build:161 and the git-work-tree= option at meson.build:181. The script is not export-ignored, so it ships in tarballs, and its #!/usr/bin/env bash shebang would be a hard bash requirement on FreeBSD if it were ever wired up.

**Failure scenario.** n/a - dead code; the cost is a maintainer or packager editing it during the rebrand and seeing no effect on the produced version string.

**Proposed fix.** Delete script/get_git_rev.sh.

**Verifier.** script/get_git_rev.sh exists (624 bytes, mode 755) with `#!/usr/bin/env bash` on line 1. `grep -rn get_git_rev` outside .git returns only Changelog:323; `git grep -n get_git_rev HEAD -- releasenotes` adds releasenotes/1.3.0/release-1.3.0.markdown:98 — both historical mentions, exactly as claimed, and nothing in meson.build, doc/meson.build or any CI file invokes it. Version-from-git is handled at meson.build:161 `header_conf.set('USE_NK_GIT_VERSION', true)` and meson.build:181 `'git-work-tree=@0@'.format(meson.project_source_root())`. .gitattributes has no script/get_git_rev.sh entry (only `script/*.jpg` at line 18).

## `test/box-test.c:48` — Most of the test suite asserts via assert(), so it becomes a no-op under NDEBUG

- **kind** build · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

The TASSERT macros in test/box-test.c:48, test/scrollbar-test.c:49, test/widget-test.c:46, test/textbox-test.c:54, test/history-test.c:39, test/helper-test.c:44, test/helper-expand.c and test/helper-pidfile.c are all `{ assert(a); printf(...); }`. With NDEBUG defined, assert() expands to nothing, every check evaporates and the binaries exit 0 unconditionally while still printing 'Test N passed'. Only the check(3)-based tests (mode-test.c, helper-tokenize.c, theme-parser-test.c) survive, and those are themselves compiled only `if check.found()` (meson.build:572-624) — a build without libcheck silently drops the entire matching-engine test suite with no warning.

**Failure scenario.** `meson setup build --buildtype=release -Db_ndebug=true && meson test` — a normal distro release build. Every assertion in 8 of the 12 test files compiles away; a regression that makes history_get_list return NULL, or utf8_strncmp return the wrong sign, still reports 'Ok' for every test.

**Proposed fix.** Replace assert() with an explicit `if (!(a)) { fprintf(stderr, ...); exit(EXIT_FAILURE); }` macro (or g_assert(), which ignores NDEBUG), and make libcheck a hard test dependency — or at minimum emit a meson warning when it is missing so the skipped coverage is visible.

**Verifier.** test/box-test.c:46-50 defines TASSERT(a) as `{ assert(a); printf("Test %3u passed..."); }`; same shape verified in test/history-test.c:38-41, test/helper-test.c:44-48, test/helper-pidfile.c and test/helper-expand.c. Under NDEBUG these become printf-only. meson.build:572 `if check.found()` ... :624 `endif` does gate theme_parser/mode/helper_tokenize tests, so a build without libcheck silently drops them. Severity low — this is test hygiene, not a shipped defect.

## `include/wayland-internal.h:64` — cursor.theme_name is never assigned; XCURSOR_THEME is ignored

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

`char *theme_name;` (include/wayland-internal.h:64) is read exactly once, at display.c:1703 `wl_cursor_theme_load(wayland->cursor.theme_name, cursor_size, wayland->shm)`. grep across source/wayland/ and include/ shows no assignment anywhere, so it is always NULL (wayland_ is a static zero-initialised global at display.c:109). By contrast XCURSOR_SIZE *is* honoured at lines 1693-1701. `size_t scales[3];` at include/wayland-internal.h:80 is likewise never read or written anywhere — pure dead state.

**Failure scenario.** A user sets XCURSOR_THEME=Adwaita (or Bibata) in their environment as every other Wayland client expects. sofi passes NULL to wl_cursor_theme_load and gets libwayland's built-in default theme, so the pointer over sofi's window is visibly a different cursor than over every other window.

**Proposed fix.** Set wayland->cursor.theme_name from g_getenv("XCURSOR_THEME") in wayland_display_setup() (alongside the buffer_count/scale init at lines 1734-1736), free it in cleanup, and delete the unused scales[3] field.

**Verifier.** grep over source/wayland/ and include/ finds exactly two hits for theme_name: the declaration at include/wayland-internal.h:64 and the read at display.c:1703 wl_cursor_theme_load(wayland->cursor.theme_name, ...). wayland_ is a static global (display.c:107) so it is always NULL; XCURSOR_SIZE is honoured at 1694-1701 but XCURSOR_THEME is never read. scales[3] (include/wayland-internal.h:80) likewise has no other reference — dead field.

## `source/wayland/display.c:210` — shm_unlink() is called before the fd<0 check, clobbering the errno that is then reported

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Line 209 shm_open, line 210 shm_unlink, line 211 `if (fd < 0)` and line 212-213 `g_warning("creating a buffer file for %zu B failed: %s", pool_size, g_strerror(errno))`. POSIX permits any successful call to set errno to a non-zero value, and shm_unlink here can also fail with ENOENT (when shm_open itself failed with e.g. ENOSPC and never created the object), overwriting the interesting errno. The diagnostic that fires on the only path that can realistically bite users is therefore unreliable.

**Failure scenario.** shm_open fails with ENOSPC (/dev/shm full). shm_unlink then fails with ENOENT and sets errno=ENOENT. The user sees "creating a buffer file for 33177600 B failed: No such file or directory", which points nowhere near the real cause.

**Proposed fix.** Save errno immediately after shm_open (`int saved = errno;`), or move the shm_unlink() below the `if (fd < 0) { ... return NULL; }` block.

**Verifier.** display.c:209 shm_open, 210 unconditional shm_unlink, 211 `if (fd < 0)`, 212-213 g_strerror(errno). The unlink between the failing call and the errno read can overwrite errno (it fails with ENOENT when shm_open never created the object). Diagnostic-quality issue only.

## `source/wayland/display.c:208` — gchar* (non-const) pointing at a string literal

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

`gchar *shm_name = "/rofi-wayland-surface";` — a writable-typed pointer to a read-only .rodata literal. Compiles today only because -Wwrite-strings is not enabled; it is an error under -Wwrite-strings / C++ and invites a future writable-buffer refactor (e.g. appending a PID) that would segfault.

**Failure scenario.** Anyone extending this to disambiguate the name in place (e.g. `shm_name[5] = '1';`) gets a write to .rodata -> SIGSEGV. Building the file with -Wwrite-strings -Werror fails outright.

**Proposed fix.** `static const char shm_name[] = "/sofi-wayland-surface";`, or a heap-allocated name if it is made unique per instance.

**Verifier.** display.c:208 is literally `gchar *shm_name = "/rofi-wayland-surface";` — non-const pointer to a string literal. No current miscompile or UB as written (nothing writes through it); purely a const-correctness/latent issue.

## `source/wayland/display.c:283` — Only integer buffer_scale is supported; wp_fractional_scale_v1 and wp_viewporter are not bound at all

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Scaling is driven entirely by wl_surface.enter: line 283 `wl_surface_set_buffer_scale(wl_surface, output->current.scale)` and line 285-295 rebuild the buffer pool when wayland->scale changes. wayland->scale is an int32_t (include/wayland-internal.h:81) fed from wl_output.scale (line 1424-1429), which is always an integer. grep for "fractional_scale" and "viewporter" across source/, include/ and meson.build returns nothing, and meson.build:324 lists only wlr-layer-shell-unstable-v1.xml among the extra protocols. So on a 1.25x/1.5x display the compositor advertises wl_output.scale 2 (ceil), sofi renders a 2x buffer, and the compositor downscales it.

**Failure scenario.** A 150%-scaled monitor under sway/Hyprland: wl_output.scale reports 2, sofi renders at 2x and the compositor bilinearly downscales to 1.5x. Text is visibly soft/blurry compared with GTK apps that use wp_fractional_scale_v1, and the DPI autodetect at line 277-281 divides by the integer scale so config.dpi is off by 2/1.5 = 33%.

**Proposed fix.** Bind wp_fractional_scale_manager_v1 + wp_viewporter, add a wp_fractional_scale_v1 listener to the layer surface, keep a fractional numerator (scale*120) alongside the integer scale, and use wp_viewport_set_destination() instead of wl_surface_set_buffer_scale() when available.

**Verifier.** Scaling is integer-only: display.c:283 wl_surface_set_buffer_scale(output->current.scale), 285-295 rebuild on change, scale is int32_t (include/wayland-internal.h:81) fed from wl_output.scale (1425-1430). grep for fractional_scale and viewporter across source/, include/ and meson.build returns nothing. This is a missing feature rather than a defect; the visible symptom (downscaled 2x buffer, DPI derived from the integer scale at 277-281) follows.

## `source/wayland/display.c:297` — wl_surface.leave is an empty stub, so scale is never recomputed when the surface leaves an output

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

wayland_surface_protocol_leave() at lines 297-299 has an empty body, while wayland_surface_protocol_enter() (267-295) unconditionally adopts the entered output's scale into the global wayland->scale and rebuilds the whole buffer pool via rofi_view_pool_refresh() at line 289. The surface can be on several outputs at once (that is exactly why enter/leave is a set, not a single value); the code keeps no set, only "whatever entered last". config.dpi is also latched permanently on the first enter (line 277-281 only re-runs while config.dpi is 0 or 1).

**Failure scenario.** Mixed-DPI dual head: a HiDPI laptop panel (scale 2) plus an external 1080p monitor (scale 1). The layer surface spans/moves between them, so enter fires for both. The last enter wins: if the 1x monitor enters last, wayland->scale drops to 1 and the whole pool is rebuilt at half resolution -> the menu renders blurry on the HiDPI panel. Unplugging the 1x monitor sends leave, which does nothing, so the scale stays wrong until sofi is restarted.

**Proposed fix.** Track the set of entered outputs (a GHashTable or GPtrArray on the surface), recompute scale as the max over that set in both enter and leave, and only call rofi_view_pool_refresh() when the computed max actually changes.

**Verifier.** display.c:298-300 wayland_surface_protocol_leave has an empty body, while enter (267-296) adopts the last-entered output's scale into the global wayland->scale and calls rofi_view_pool_refresh() at 289; no set of entered outputs is kept, and config.dpi is latched by the `config.dpi == 0 || config.dpi == 1` guard at 277. Downgraded: the layer surface is anchored to all four edges with size 0,0 (1796-1801) so it normally occupies a single output and multiple simultaneous enters are uncommon.

## `source/wayland/display.c:1769` — config.monitor is matched by exact name only, so the default "-5" and all numeric selectors silently do nothing

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

wayland_output_by_name() (lines 1346-1363) is a pure g_strcmp0 scan over output->name. wayland_display_late_setup() calls it at line 1769 with config.monitor, whose default is "-5" (/home/orpheus497/Projects/sofi/config/config.c:149) — an X11-ism meaning "monitor with the focused window". No output is ever named "-5", so wayland_output_by_name returns NULL, wlo stays NULL at line 1771-1774, and the layer surface is created with a NULL output, letting the compositor choose. The whole X11 selector vocabulary (-1 focused window, -2 focused monitor, -3 pointer, -4 hovered, -5 focused-window monitor, and bare indices "0", "1") is unimplemented on Wayland, and the failure is only a g_debug at line 1360.

**Failure scenario.** A user with a documented `-m 1` or `-m -4` in their config runs sofi under sway. The menu appears on whatever output the compositor picks rather than the requested one, with no warning — only a debug line the user will not see without G_MESSAGES_DEBUG=Wayland.

**Proposed fix.** Accept a numeric index into the outputs table, map -3/-4 to the output containing the last pointer position (wayland->last_seat has motion data), and g_warning() when the requested selector cannot be honoured on Wayland.

**Verifier.** wayland_output_by_name (display.c:1347-1364) is a plain g_strcmp0 scan with only a g_debug on failure at 1361; late_setup calls it with config.monitor at 1769 and falls back to wlo = NULL at 1771-1774. config/config.c:149 has `.monitor = "-5"`, so the default and every numeric/positional X11 selector silently match nothing.

## `Examples/rofi-file-browser.sh:40` — `[ -n "$@" ]` is true with zero arguments and a syntax error with two or more

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`if [ -n "$@" ]` expands to `[ -n ]` when there are no positional parameters. POSIX `test` with a single argument is true whenever that argument is a non-empty string, and `-n` is non-empty, so the branch is taken on the initial (argument-less) call from rofi. Lines 42/44/46 then treat `"$@"` as one scalar (`[[ "$@" == /* ]]`, `ROFI_FB_CUR_DIR="${ROFI_FB_CUR_DIR}/$@"`), appending an empty component. With two or more arguments `[ -n a b ]` is outright `[: a: binary operator expected`. Verified: `bash -c 'set --; [ -n "$@" ] && echo TRUE'` prints TRUE; `bash -c 'set -- a b; [ -n "$@" ]'` errors.

**Failure scenario.** rofi calls the script with no arguments on the initial listing. Line 40 is true, line 42 `[[ "" == /* ]]` is false, line 46 sets ROFI_FB_CUR_DIR="$HOME/" instead of "$HOME". A selected entry whose name contains a space (rofi passes it as one argv element, but any caller splitting it passes two) triggers the `binary operator expected` error and the script produces no listing.

**Proposed fix.** Use `if [ -n "$1" ]` and replace `"$@"` with `"$1"` on lines 42, 44 and 46.

**Verifier.** Examples/rofi-file-browser.sh:40 is `if [ -n "$@" ]`. I verified both halves: `bash -c 'set --; [ -n "$@" ] && echo BRANCH_TAKEN'` prints BRANCH_TAKEN, and `bash -c 'set -- a b; [ -n "$@" ]'` errors with `[: a: binary operator expected`. Lines 42/44/46 do treat "$@" as a scalar as described. Severity corrected down: rofi passes the selection as a single argv element, so the 2+-argument error path is not reachable through normal rofi use, and the zero-argument path is benign in practice -- line 46 yields "$HOME/", which line 51 `[ -d ]` accepts and line 85 `readlink -e` normalizes back to $HOME. Real bug, cosmetic effect.

## `Examples/rofi-file-browser.sh:63` — History file is read and edited but never created, so every first run errors

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Lines 16-23 create only the *directories* of ROFI_FB_PREV_LOC_FILE and ROFI_FB_HISTORY_FILE. The history file itself (`~/.local/share/rofi/rofi_fb_history`, line 6) is never touched into existence. Yet line 63 `sed -i ... "${ROFI_FB_HISTORY_FILE}"`, line 64 `sed -i '/##deleted##/d'`, line 66 `wc -l < "${ROFI_FB_HISTORY_FILE}"`, line 68 `sed -i 1d`, and line 93 `tac "${ROFI_FB_HISTORY_FILE}"` all assume it exists. There is no `set -e`, so the errors are printed to stderr and the script limps on.

**Failure scenario.** Fresh user runs the script for the first time; line 93 emits `tac: ...rofi_fb_history: No such file or directory` to stderr and rofi shows the directory listing with no history section. Opening the first file emits two `sed: can't read` errors and `wc: ...: No such file` at line 66, and the `-gt` comparison at line 66 fails with `integer expression expected`.

**Proposed fix.** After line 23 add `: > "${ROFI_FB_HISTORY_FILE}"` guarded by `[ -f ... ] ||`, i.e. `[ -f "${ROFI_FB_HISTORY_FILE}" ] || : > "${ROFI_FB_HISTORY_FILE}"`, and the same for ROFI_FB_PREV_LOC_FILE.

**Verifier.** Read Examples/rofi-file-browser.sh lines 16-23: both blocks call `mkdir -p "$(dirname ...)"` only -- the history file at line 6 (~/.local/share/rofi/rofi_fb_history) is never touched into existence. Lines 63, 64, 66, 68 and 93 all consume it unconditionally, and `grep -n 'set -e'` over the file returns nothing, so it limps on. Severity corrected down: the file self-heals on the first file open because line 65 `echo ... >> "${ROFI_FB_HISTORY_FILE}"` creates it, so the damage is one run of stderr noise. Also, `[ "" -gt 5 ]` at line 66 returns 2 so the then-branch is simply skipped rather than misbehaving. First-run stderr noise only.

## `Examples/rofi-file-browser.sh:63` — Filesystem path interpolated unescaped into a sed regex and used as the sed delimiter

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`sed -i "s|${ROFI_FB_CUR_DIR}|##deleted##|g" "${ROFI_FB_HISTORY_FILE}"` splices a user-controlled absolute path directly into a BRE, using `|` as the s/// delimiter. Any `|` in the path terminates the pattern early and produces `unknown option to s'` or a wrong substitution; `.`, `*`, `[`, `\`, `^`, `$` in the path silently match more than intended. Line 93 `grep "${ROFI_FB_CUR_DIR}"` has the same unescaped-and-unanchored problem on the read side.

**Failure scenario.** User opens `/home/u/docs/a|b/report.pdf`. Line 63 runs `sed "s|/home/u/docs/a|b/report.pdf|##deleted##|g"`, which sed parses as `s|/home/u/docs/a|b/report.pdf|` plus trailing garbage — either an error or replacing every `/home/u/docs/a` in the history with `b/report.pdf`, corrupting all recorded history paths. A directory named `/home/u/.` makes line 93's grep match every history line.

**Proposed fix.** Do the filtering in the shell rather than sed: read the history with `grep -F -x -v -- "${ROFI_FB_CUR_DIR}"` into a temp file and move it over, and use `grep -F -- "${ROFI_FB_CUR_DIR}"` on line 93.

**Verifier.** Examples/rofi-file-browser.sh:63 is verbatim `sed -i "s|${ROFI_FB_CUR_DIR}|##deleted##|g" "${ROFI_FB_HISTORY_FILE}"` -- an unescaped filesystem path spliced into a BRE that also uses '|' as the s/// delimiter, so any '|' in the path terminates the pattern and any of . * [ \ ^ $ over-matches. Line 93 `tac ... | grep "${ROFI_FB_CUR_DIR}"` has the same unescaped/unanchored problem. Severity corrected down: worst case is corruption of a 5-line recent-files list in an uninstalled example script (I confirmed nothing in meson.build references Examples/), not a security or data-loss issue.

## `lexer/theme-lexer.l:585` — ${VAR} substitution is re-entered inside its own expansion, allowing unbounded recursion

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The `{ENV}` rule is active in start condition PROPERTIES_ENV (line 585) — which is exactly the state the rule itself switches into (line 602) after pushing a new flex buffer and a PT_ENV ParseObject. Nothing tracks expansion depth, and the recursion guard used for files (g_queue_find_custom / file_queue_find, lines 98-104 and 440) only matches PT_FILE entries, so PT_ENV frames are never checked. Each level allocates a ParseObject (line 591) plus a YY_BUF_SIZE flex buffer (line 598). Nested @import chains of distinct files are likewise unbounded — only cycles are rejected.

**Failure scenario.** `X='${X}' rofi -theme-str '* { font: ${X}; }'` expands ${X} into a buffer whose content is again `${X}`, pushing a new 16KB flex buffer and ParseObject per level until the process is OOM-killed.

**Proposed fix.** Track a depth counter on the file_queue (or count PT_ENV frames) and emit a parse error past a small limit; apply the same limit to nested @import.

**Verifier.** lexer/theme-lexer.l:585 declares the {ENV} rule for `<PROPERTIES,PROPERTIES_ENV,PROPERTIES_ARRAY,PROPERTIES_VAR_DEFAULT>` — PROPERTIES_ENV included — and line 602 ends the action with `BEGIN(PROPERTIES_ENV)` after g_malloc0'ing a ParseObject (591) and yypush_buffer_state(yy_create_buffer(0, YY_BUF_SIZE)) (598). No depth counter exists anywhere in the action. The only recursion guard, g_queue_find_custom with file_queue_find (line 440, helper at 98-104), returns 1 for anything whose `type != PT_FILE`, so PT_ENV frames are never matched. A self-referential env var therefore recurses without bound. Self-inflicted (requires the user's own environment), hence low.

## `lexer/theme-lexer.l:410` — @theme "default" silently does nothing if the GResource lookup fails

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The `<INCLUDE>{DEFAULT}` action looks up the hard-coded resource path "/org/qtools/rofi/default.rasi" and wraps the whole body in `if (theme_data)` (lines 410-428) with no else branch: a failed lookup pops the INCLUDE state and returns as if the import succeeded. The same hard-coded prefix appears in resources/resources.xml:3 and source/rofi.c:1035. This is the exact failure mode a rebrand will hit, because the prefix, the alias, and the two lookup strings must be changed in lockstep and nothing verifies them.

**Failure scenario.** Change the gresource prefix in resources/resources.xml to /org/qtools/sofi without updating lexer/theme-lexer.l:411: the build succeeds, `rofi -dump-theme` prints an empty theme, and the fallback at source/rofi.c:1178-1181 (`rofi_theme_parse_string("@theme \"default\"")`) leaves the UI unstyled with no diagnostic at all.

**Proposed fix.** Report a parse error (rofi_add_error_message) when g_resource_lookup_data returns NULL, and derive the resource prefix from a single #define shared by resources.xml generation, the lexer and rofi.c.

**Verifier.** lexer/theme-lexer.l:409-411 does the lookup of the literal "/org/qtools/rofi/default.rasi"; the whole body 412-427 is wrapped in `if (theme_data) {` and line 428-429 unconditionally `BEGIN(GPOINTER_TO_INT(g_queue_pop_head(queue)));` with no else and no diagnostic. The matching prefix is at resources/resources.xml:3 (`prefix="/org/qtools/rofi"`, aliases default.rasi / default_configuration.rasi) and the second hard-coded lookup is at source/rofi.c:1035. The fallback at source/rofi.c:1178-1180 calls rofi_theme_parse_string("@theme \"default\"") and discards the return value. Rebrand hazard is genuine; as a live defect today it cannot fire in a correctly built tree, so low.

## `lexer/theme-lexer.l:548` — Integer literals are silently truncated from int64 to int

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The {NUMBER} rule does `yylval->ival = (int)g_ascii_strtoll(yytext, NULL, 10);`. Values outside int range are truncated with an implementation-defined conversion and no diagnostic; g_ascii_strtoll's ERANGE/errno is also never inspected. The value flows into T_INT and thence to P_INTEGER properties, distances (theme-parser.y:895) and rgba component range checks (theme-parser.y:922-930).

**Failure scenario.** `* { width: 4294967297px; }` yields width == 1 with no warning. `configuration { threads: 4294967296; }` yields 0. A colour written as `rgba(256000000000, 0, 0, 1)` truncates to a value that can pass check_in_range(0,255).

**Proposed fix.** Parse into gint64, check errno/ERANGE and the int range, and emit a T_ERROR/yyerror on overflow.

**Verifier.** lexer/theme-lexer.l:548 is exactly `... {NUMBER} { yylval->ival = (int)g_ascii_strtoll(yytext, NULL, 10); return T_INT;}` with no errno/ERANGE check, and NUMBER is `[[:digit:]]+` (line 198). The truncated value flows to P_INTEGER (theme-parser.y:549-552), to t_property_number (theme-parser.y:894-897) and to the rgba check_in_range calls (theme-parser.y:922-930), all as verified. Cosmetic/no-diagnostic class of bug.

## `lexer/theme-lexer.l:419` — Built-in default theme is fed to the lexer one byte short

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

`po->str_len = strlen(po->malloc_str)-1;` deliberately drops the final byte of the embedded default theme resource. It is harmless today only because doc/default_theme.rasi ends with "}\n" (verified: last two bytes are 0x7d 0x0a) so the discarded byte is the trailing newline. There is no comment explaining it and no guard for an empty resource (str_len would become -1; only the `len > 0` test in YY_INPUT at line 149 saves it).

**Failure scenario.** If doc/default_theme.rasi is ever regenerated or edited without a trailing newline — e.g. by piping `rofi -dump-theme` output through a tool that strips it — the closing `}` is dropped, `@theme "default"` fails to parse, and rofi starts with an unstyled UI.

**Proposed fix.** Use the size returned by g_bytes_get_data() and feed the full buffer; drop the -1.

**Verifier.** lexer/theme-lexer.l:419 is `po->str_len   = strlen(po->malloc_str)-1;` with no comment or emptiness guard. `tail -c 8 doc/default_theme.rasi | od` gives `... ; \n } \n`, confirming the discarded byte is today's trailing newline. Note the claim understates the empty-resource case: str_len is `int` (theme-lexer.l:83) and YY_INPUT does `MIN(max_size, current->str_len)` (line 148) against a yy_size_t — a -1 would promote to SIZE_MAX and select max_size, so the `len > 0` test would NOT save it; it would memcpy max_size bytes out of an empty buffer. Finding stands, reasoning is if anything too charitable.

## `lexer/theme-lexer.l:398` — @theme reuses the INCLUDE state without resetting import_optional

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

`import_optional` is a file-scope global (line 54) set to FALSE by {INCLUDE} (line 390) and TRUE by {OPT_INCLUDE} (line 395). The {THEME} rule (lines 398-402) enters the same INCLUDE start condition but never assigns it, so the <INCLUDE>{STRING} action's error path (line 462) uses whatever the previous import left behind.

**Failure scenario.** A theme containing `?import "optional-tweaks.rasi"` followed later by `@theme "typo-name"`: the missing mandatory theme is downgraded from a user-visible rofi_add_warning_message to a g_warning that is invisible without G_MESSAGES_DEBUG, so the user sees the default theme with no explanation.

**Proposed fix.** Set import_optional = FALSE in the {THEME} rule (and reset it at the start of each parse).

**Verifier.** `gboolean import_optional = FALSE;` is a file-scope global at lexer/theme-lexer.l:54. `<INITIAL>{INCLUDE}` sets it FALSE (line 390) and `<INITIAL>{OPT_INCLUDE}` sets it TRUE (line 395), but `<INITIAL>{THEME}` at lines 398-402 only pushes YY_START, does BEGIN(INCLUDE) and returns T_RESET_THEME — it never assigns import_optional. The shared `<INCLUDE>{STRING}` failure path at 458-467 branches on `if (!import_optional)` to choose rofi_add_warning_message vs a plain g_warning, so a prior ?import silently demotes a later failed @theme to an invisible warning.

## `mkdocs/docs/themes/capture.sh:4` — Screenshot generator hardcodes a build path and leaks the Xvfb server on failure

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Line 3 `THEMES=../../../themes/*.rasi` and line 4 `ROFI_BIN=../../../build/rofi` assume both the exact build directory name `build` and that the script is run from mkdocs/docs/themes. There is no check that ROFI_BIN exists. Line 43 starts `Xvfb :1234` in the background and line 51 kills it, but with no `trap` — any failure or interrupt in the loop at lines 48-50 leaves an orphaned Xvfb holding display :1234, so the next run silently attaches to a stale server. Line 44's variable is named XEPHYR_PID though the process is Xvfb. Line 20 `BASE="$(basename ${theme})"` is unquoted inside the substitution.

**Failure scenario.** Contributor regenerates theme screenshots after `meson setup builddir` (the name used everywhere in CI — .github/actions/meson/action.yml line 22). ROFI_BIN does not exist, every `run_theme` call fails silently, themes.md is rewritten at line 38 with headers and image links but no images are produced, and Xvfb :1234 is left running.

**Proposed fix.** Validate `[ -x "${ROFI_BIN}" ] || exit 1` after line 4, add `trap 'kill ${XVFB_PID} 2>/dev/null' EXIT`, rename the variable, and quote `"${theme}"` on line 20.

**Verifier.** mkdocs/docs/themes/capture.sh:3 is `THEMES=../../../themes/*.rasi` and line 4 is `ROFI_BIN=../../../build/rofi`, with no existence check anywhere in the 51-line file. CI uses a different directory name -- .github/actions/meson/action.yml:22 is `meson setup builddir -Dxcb=... -Dwayland=...`. Line 43 `Xvfb :1234 -screen 0 1920x1080x24 &`, line 44 `XEPHYR_PID=$!` (misnamed, the process is Xvfb), line 51 `kill ${XEPHYR_PID}`, and `grep -n trap` over the file returns nothing, so an interrupt in the 48-50 loop orphans the server. Line 20 `BASE="$(basename ${theme})"` is unquoted inside the substitution. All accurate; severity low -- a contributor-only screenshot regenerator, not shipped or built.

## `script/rofi-sensible-terminal:12` — $TERMINAL is word-split and glob-expanded in the candidate list and in the exec

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`for terminal in $TERMINAL x-terminal-emulator urxvt ...` (line 12) expands $TERMINAL unquoted, and line 13 `command -v $terminal` / line 14 `exec $terminal "$@"` repeat the mistake. $TERMINAL is documented at doc/rofi-sensible-terminal.1.markdown line 20 as a user-set variable, so users routinely set it to something with arguments.

**Failure scenario.** User sets `TERMINAL="alacritty --class launcher"`. The for-list becomes three words; iteration 1 tries `command -v alacritty` (succeeds) and line 14 runs `exec alacritty "$@"` — the `--class launcher` is silently dropped. If TERMINAL is unset and $IFS is non-default, or if a candidate name were ever to contain a glob char, the list expands against $PWD.

**Proposed fix.** Quote the exec target and handle a multi-word $TERMINAL explicitly, e.g. `if [ -n "${TERMINAL}" ]; then exec ${TERMINAL} "$@"; fi` before the loop (with a comment that splitting is deliberate there), and quote `"$terminal"` on lines 13 and 14.

**Verifier.** script/rofi-sensible-terminal:12 is `for terminal in $TERMINAL x-terminal-emulator urxvt ... wezterm foot ghostty; do` with $TERMINAL unquoted, line 13 `command -v $terminal`, line 14 `exec $terminal "$@"`. doc/rofi-sensible-terminal.1.markdown:20 does document `$TERMINAL` as a user-set variable. TERMINAL="alacritty --class launcher" therefore yields three separate loop candidates and execs bare `alacritty`, silently dropping the flags. Severity is low rather than medium: the script's contract is arguably a command name, not a command line, and the failure is a dropped flag rather than a wrong program being run.

## `script/rofi-theme-selector:102` — Unquoted array append desynchronises theme_names from themes, selecting the wrong theme

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** User-facing and non-C surface: root docs, doc/ m

Line 96 appends exactly one element: `themes+=("${file}")`. Line 104 appends exactly one: `theme_names+=("${NAME} by ${USER}")`. But line 102 appends UNQUOTED: `theme_names+=(${NAME})`. `${NAME}` is derived from `basename` at line 97-98, so it is an arbitrary filename stem. If it contains whitespace it word-splits into N elements; if it contains a glob character it pathname-expands. From that point on the two arrays no longer share indices, yet select_theme uses `${RES}` (the index rofi returns for the theme_names list, line 163/175/176) to index `themes` at line 161 and line 235.

**Failure scenario.** User has ~/.config/rofi/themes/'My Dark Theme.rasi' with no `User:` line. find_themes pushes 1 entry into themes but 3 into theme_names. The user picks the last theme in the list; line 235 does `set_theme "${themes[${SELECTED}]}"` with SELECTED past the end of themes, writing `@theme ""` into ~/.config/rofi/config.rasi and breaking rofi startup. With a stem containing `*`, the glob can expand to unrelated filenames in $PWD.

**Proposed fix.** Quote it: `theme_names+=("${NAME}")` on line 102, matching line 104.

**Verifier.** Line 102 is `theme_names+=(${NAME})` unquoted, and line 96 `themes+=("${file}")` is quoted, exactly as cited. But the claim's headline mechanism is wrong: find_themes sets `IFS=:` at line 57 and does not restore it until line 110, so line 102 executes with IFS=':' and whitespace does NOT word-split. I tested `IFS=:; a=(); NAME="My Dark Theme"; a+=(${NAME})` -> count=1. The stated scenario ('My Dark Theme.rasi' pushing 3 entries) is refuted. A desync is still reachable two other ways, which I also tested: a stem containing ':' splits (`NAME="dark:blue"` -> count=2), and unquoted pathname expansion still applies (`NAME="*"` in /tmp -> count=9). Since real-world theme stems rarely contain ':' or glob metacharacters, this is a genuine but low-probability defect, not the high-severity whitespace bug described.

## `script/rofi-theme-selector:99` — Assigning to USER clobbers the exported login variable for every child process

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`USER=$(${SED} -n 's/^.*User: \(.*\)/\1/p' "${file}" | head -n 1 )` reuses the name of the standard login environment variable. In bash, assigning to an already-exported variable preserves its export attribute, so the theme's author name (or the empty string when no `User:` line exists) is exported into the environment of every command the script runs afterwards. Verified: `bash -c 'export USER=orig; f(){ USER=changed; env | grep ^USER=; }; f'` prints `USER=changed`. The affected children are `${ROFI}` at lines 118, 163 and 220, plus anything rofi itself spawns from those previews.

**Failure scenario.** A theme file contains `User: Bob`. From line 99 onward $USER is `Bob` (or `""` for the last theme scanned with no User: line). rofi is then launched at line 163 with a wrong or empty $USER; any drun/run entry whose Exec expands $USER, or any plugin/prompt that renders $USER, shows the theme author's name instead of the logged-in user.

**Proposed fix.** Rename the variable to something script-local and lowercase, e.g. `theme_user=$(...)`, and update the references on lines 100 and 104.

**Verifier.** script/rofi-theme-selector:99 is `USER=$(${SED} -n 's/^.*User: \(.*\)/\1/p' "${file}" | head -n 1 )`, a plain assignment to the login variable name at global scope (find_themes has no `local`). I verified bash preserves the export attribute: `bash -c 'export USER=orig; f(){ USER=changed; env | grep ^USER=; }; f'` prints USER=changed. So from line 99 onward every child -- ${ROFI} at 118, 163 and 220, plus anything rofi spawns -- sees the theme author's name or the empty string. Severity low: the effect is confined to the lifetime of this helper script and its previews, not the user's session.

## `script/rofi-theme-selector:161` — THEME_FLAG packs a flag and a path into one string and is expanded unquoted

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Line 161 builds `THEME_FLAG="-theme ${themes[${SELECTED}]}"` — a single scalar holding both the option and its argument — and line 163 expands it unquoted: `${ROFI} ${THEME_FLAG} ${MORE_FLAGS[@]} ...`. Correct behaviour depends entirely on the theme path containing no whitespace and no glob characters. Note that MORE_FLAGS is a proper array (line 144) yet is also expanded unquoted at line 163, defeating the point of using an array for `-p "Theme"` and the `-mesg` value.

**Failure scenario.** A theme installed at `${XDG_DATA_HOME}/rofi/themes/My Theme.rasi` is previewed; line 163 passes rofi `-theme`, `My`, `Theme.rasi` as three argv entries, so rofi reports `Failed to open theme: My` and the preview loop shows an error dialog on every keystroke.

**Proposed fix.** Make it an array: `THEME_FLAG=(); ... THEME_FLAG=(-theme "${themes[${SELECTED}]}")` and call `${ROFI} "${THEME_FLAG[@]}" "${MORE_FLAGS[@]}" ...` on line 163.

**Verifier.** script/rofi-theme-selector:161 is `THEME_FLAG="-theme ${themes[${SELECTED}]}"` -- flag and path packed into one scalar -- and line 163 is `RES=$( create_theme_list | ${ROFI} ${THEME_FLAG} ${MORE_FLAGS[@]} -cycle -selected-row "${SELECTED}" -mesg "${MESG}")`, both expansions unquoted. IFS is back to default at line 163 (find_themes restored it at line 110, and create_theme_list's IFS='|' is confined to a pipeline subshell restored at line 134), so a theme path with a space really does split. MORE_FLAGS is built as an array at 144-146 yet expanded unquoted, as claimed. Severity low: only the preview loop breaks -- the actual write path, line 235 `set_theme "${themes[${SELECTED}]}"`, is correctly quoted.

## `script/rofi-theme-selector:118` — No set -eu and the config dump / sed / mkdir return values are never checked

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** User-facing and non-C surface: root docs, doc/ m

The script has no `set -e` and no `set -u`. Line 118 `${ROFI} -dump-config > "${TMP_CONFIG_FILE}"` and line 120 `${SED} -i ... "${TMP_CONFIG_FILE}"` are unchecked; if rofi fails (bad user config, no display) the temp config is empty or truncated and every preview at line 163 runs against a broken `-config`. Line 189 `mkdir -p "${CDIR}"` is unchecked, and line 199 `touch "${get_link}"` is unchecked — if line 195 `readlink -f` returned empty, `touch ""` errors and line 203-204 then append `@theme` to a file that does not exist.

**Failure scenario.** User's existing ~/.config/rofi/config.rasi has a parse error. Line 118 makes rofi exit non-zero with an empty dump; the script continues, line 144 passes `-config <empty file>`, and the user is shown a preview loop that silently ignores their real configuration — and on Alt-a, set_theme appends to the config regardless.

**Proposed fix.** Add `set -eu` after line 1 (auditing the deliberately-failing tests), or at minimum `|| { echo 'rofi -dump-config failed' >&2; exit 1; }` on line 118 and `[ -n "${get_link}" ] || exit 1` after line 195.

**Verifier.** `grep -n 'set -e|set -u|trap'` over script/rofi-theme-selector returns nothing, and lines 118, 120, 189 and 199 are unchecked exactly as cited. The general observation holds. But the specific failure chain in the claim is largely refuted: line 189 `mkdir -p "${CDIR}"` runs before line 195, so by the time `readlink -f "${CDIR}/config.rasi"` executes its parent directory exists, and GNU readlink -f only returns empty when a non-final component is missing (I verified: `readlink -f /nonexistent-dir-xyz/sub/config.rasi` -> empty, rc=1, whereas an existing parent returns the path). So `touch ""` requires mkdir to have already failed. Whether the line 118 dump-config path actually produces a broken preview depends on the user's config state at runtime, which I cannot check statically.

## `source/display.c:88` — display_* dispatchers check `proxy` but not the function pointer, and the XCB proxy leaves two members NULL

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Every wrapper in source/display.c guards only on the proxy object (`if (proxy) { proxy->foo(...); }`). The XCB proxy at source/xcb/display.c:2036-2049 is a designated initialiser that omits both `.get_clipboard_data` and `.set_fullscreen_mode`, so those members are NULL; the Wayland proxy (source/wayland/display.c:2021-2036) sets them. display_get_clipboard_data (line 88) and display_set_fullscreen_mode (line 94) will therefore call through a NULL function pointer under X11. Today it is latent only because both call sites happen to be backend-gated (source/view.c:1020, :1036 check config.backend == DISPLAY_WAYLAND; source/wayland/view.c:383 is Wayland-only) — the abstraction offers no protection of its own. Related layering wart: include/display-internal.h:52 declares the member as `void (*)(int type, ...)` while include/display.h:131 and source/display.c:85 use `enum clipboard_type`, so the two sides of the vtable disagree on the parameter type.

**Failure scenario.** Add any backend-agnostic paste path — or move the PASTE_PRIMARY handling in view.c:1014-1023 out from behind its `config.backend == DISPLAY_WAYLAND` check — and run under X11: proxy is non-NULL, proxy->get_clipboard_data is NULL, and the call at display.c:88 jumps to address 0. Immediate SIGSEGV.

**Proposed fix.** Guard each dispatcher on the member (`if (proxy && proxy->get_clipboard_data)`), give the XCB proxy no-op implementations for the two missing members, and make include/display-internal.h:52 use `enum clipboard_type` so the vtable signature matches the public one.

**Verifier.** source/xcb/display.c:2036-2049 is a designated initialiser that lists setup..scale and .view but omits .get_clipboard_data and .set_fullscreen_mode, so those members are NULL under X11; source/wayland/display.c:2032-2033 sets both. source/display.c:85-96 guards only `if (proxy)` before proxy->get_clipboard_data(...) / proxy->set_fullscreen_mode(). Confirmed latent: the only call sites are source/view.c:1018-1023 and :1034-1039 (both inside `if (config.backend == DISPLAY_WAYLAND)`) and source/wayland/view.c:383. The vtable type disagreement is also real — include/display-internal.h:52 uses `int type` while include/display.h:131 uses `enum clipboard_type` (source/wayland/display.c:1985 matches the int form). Severity low because no current path can reach it.

## `source/helper.c:1775` — The {}-substitution GRegex is recompiled on every call and is unref'd even when compilation failed

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

helper_string_replace_if_exists_v() calls g_regex_new("\\[(.*)({[-\\w]+})(.*)\\]|({[\\w-]+})", G_REGEX_UNGREEDY, 0, &error) on every invocation (line 1775) — the pattern is a compile-time constant, is never cached, and G_REGEX_OPTIMIZE is not requested. This function is on the path of every command launch (helper_parse_setup, source/helper.c:125) and of every thumbnail generation from the icon worker (source/rofi-icon-fetcher.c:562). Additionally, if g_regex_new fails, reg is NULL and line 1782 `g_regex_unref(reg)` unconditionally passes NULL, producing a g_return_if_fail critical rather than a clean error path.

**Failure scenario.** Enable a preview-cmd and browse a directory of 500 files: 500 worker-thread invocations each recompile the same PCRE pattern, and the compilation is not cached so the cost is paid in full each time. In the compile-failure path, g_regex_unref(NULL) emits 'g_regex_unref: assertion regex != NULL failed' to stderr before the intended error dialog.

**Proposed fix.** Hoist the pattern into a `static GRegex *` initialised once via g_once_init_enter (GRegex is refcounted and thread-safe for matching), add G_REGEX_OPTIMIZE, and guard the unref with `if (reg)`.

**Verifier.** source/helper.c:1770-1782: g_regex_new("\\[(.*)({[-\\w]+})(.*)\\]|({[\\w-]+})", G_REGEX_UNGREEDY, 0, &error) is called on every invocation with no caching and no G_REGEX_OPTIMIZE, and `g_regex_unref(reg);` at :1782 is outside the `if (error == NULL)` block at :1777, so on a compile failure it passes NULL and trips glib's g_return_if_fail. The failure branch is dead in practice (constant pattern), so this is a performance/hygiene issue.

## `source/helper.c:212` — utf8_helper_simplify_string() does not handle g_utf8_normalize() returning NULL

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`char *s = g_utf8_normalize(os, -1, G_NORMALIZE_ALL);` (line 212) returns NULL for input that is not valid UTF-8. The very next line does `g_utf8_strlen(s, -1)`, which for a NULL pointer with max == -1 trips glib's g_return_val_if_fail: a g_critical is logged and 0 is returned. The function then allocates a 3-byte zeroed buffer and returns an empty string, silently turning the entry into something that can never match. It is called from helper_token_match (line 546), from R() (line 232) and from rofi_scorer_fzf_v2_evaluate (lines 1288-1289), i.e. once per entry per keystroke when normalize-match is enabled.

**Failure scenario.** Enable `normalize-match` and pipe non-UTF-8 bytes into dmenu (e.g. `find / -print0 | rofi -dmenu -normalize-match` on a filesystem with latin1-named files). Every such entry emits 'g_utf8_strlen: assertion p != NULL failed' to stderr on every keystroke and is silently unmatchable.

**Proposed fix.** `if (s == NULL) return g_strdup("");` (or fall back to rofi_force_utf8(os, -1) then normalize) immediately after line 212.

**Verifier.** source/helper.c:212-214: `char *s = g_utf8_normalize(os, -1, G_NORMALIZE_ALL);` with no NULL check, then `g_utf8_strlen(s, -1)` on the next line — glib's g_utf8_strlen has `g_return_val_if_fail (p != NULL || max == 0, 0)`, so a NULL s yields a critical and 0, g_malloc0(3) at :214, the loop at :216 is skipped because of the `iter &&` guard, and an empty string is returned. Call sites confirmed at :232 (R()) and :546 (helper_token_match). Low: only reachable with invalid UTF-8 input plus normalize-match.

## `source/helper.c:569` — execute_generator() ignores helper_parse_setup()'s failure and calls g_spawn with a NULL argv

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`helper_parse_setup(config.run_command, &args, &argv, "{cmd}", cmd, (char *)0);` (line 565) has its return value discarded, and args stays NULL when parsing fails. Line 569 then calls g_spawn_async_with_pipes(NULL, args, ...) with argv == NULL, which trips glib's g_return_val_if_fail without setting the GError. The `if (error != NULL)` check at line 572 is therefore false and the function returns the still-initialised fd of -1 — correct by accident, but with a spurious critical on stderr and no user-facing diagnostic. helper_execute_command_env does check this (line 1419); execute_generator does not.

**Failure scenario.** Set `run-command: "sh -c 'foo";` (unbalanced quote) and use run mode with a generator: g_shell_parse_argv fails, an error dialog is shown by helper_parse_setup, then g_spawn_async_with_pipes emits 'g_spawn_async_with_pipes: assertion argv != NULL failed' to stderr before -1 is returned.

**Proposed fix.** `if (!helper_parse_setup(config.run_command, &args, &argv, "{cmd}", cmd, (char *)0) || args == NULL) { return -1; }` before line 567.

**Verifier.** source/helper.c:562-580: `char **args = NULL; ... helper_parse_setup(config.run_command, &args, &argv, "{cmd}", cmd, (char *)0);` with the return value discarded, then g_spawn_async_with_pipes(NULL, args, ...) at :569 and `if (error != NULL)` at :572. glib's g_return_val_if_fail on argv does not set the GError, so the failure is silent apart from a critical; fd stays -1 from :567 so the outcome is accidentally correct. helper_execute_command_env does check (source/helper.c:1419).

## `source/history.c:188` — history_set() destructively strtok()s the global config.ignored_prefixes, so only the first prefix works after the first call

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`for (char *checked_prefix = strtok(config.ignored_prefixes, ";"); ...; checked_prefix = strtok(NULL, ";"))` (lines 188-189). strtok writes NUL bytes over the ';' separators of the *shared global* config.ignored_prefixes string. After the first history_set() the string is permanently "a\0b\0c", so every later call sees only "a" — strtok finds no ';' before the first NUL, returns "a", and the follow-up strtok(NULL) immediately returns NULL. All prefixes after the first are silently ignored for the rest of the process lifetime. strtok is also non-reentrant, and history_set can be reached from more than one mode. Additionally config.ignored_prefixes could be NULL (it is a plain `char *` in include/settings.h:124 settable from Xresources/config, source/xrmoptions.c:344); strtok(NULL, ";") in that case continues an unrelated previous scan, which is undefined behaviour.

**Failure scenario.** Set `ignored-prefixes: "sudo ;doas ;pkexec ";` in config.rasi. Run `sudo foo` (ignored, correct), then run `doas bar` — on this second history_set() call strtok only ever yields "sudo ", so "doas bar" is recorded in ~/.cache/rofi-4.runcache despite the config.

**Proposed fix.** Do not mutate the global: `gchar **prefixes = g_strsplit(config.ignored_prefixes ? config.ignored_prefixes : "", ";", -1); ... g_strfreev(prefixes);` (or use strtok_r on a g_strdup copy). Move the check out of history.c entirely, as the existing TODO at line 187 says.

**Verifier.** source/history.c:188-189 is verbatim `for (char *checked_prefix = strtok(config.ignored_prefixes, ";"); checked_prefix != NULL; checked_prefix = strtok(NULL, ";"))`, and config.ignored_prefixes is the shared global (include/settings.h:124, set from source/xrmoptions.c:344). strtok does overwrite the ';' separators in place, and a second full pass over the already-tokenized string yields only the first token. Severity dropped to low: the claim's scenario is wrong — each `rofi` invocation is a fresh process that re-parses config, and within one process history_set is reached at most once per launch (source/modes/drun.c:344/473, run.c:129, ssh.c:145 are all terminal actions). The NULL-pointer sub-claim is speculative: the default is the literal "" (config/config.c:105), on which strtok returns NULL without writing.

## `source/history.c:55` — History sort comparator truncates a long subtraction to int and can return an inconsistent ordering

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`_element::index` is `long int` (line 46) but __element_sort_func returns `b->index - a->index` as an `int` (line 55). On LP64 the long difference is silently truncated, which can invert or zero the comparison and makes the relation non-transitive — undefined behaviour for the g_qsort_with_data at line 64. Related: line 68 stores the same field into an `int min_value`, truncating again, and __history_write_element_list computes min_value from the *full* list but writes only the top config.max_history_size entries (lines 68-77), so the normalised base creeps upward on every trim instead of resetting to zero.

**Failure scenario.** A history file whose index values straddle 2^31 — e.g. a hand-edited or corrupted cache containing '4000000000 foo' alongside '1 bar' — produces a difference that does not fit in int; the comparator reports the wrong order, g_qsort_with_data's preconditions are violated and the entries are written back in an arbitrary order, permanently scrambling the ranking.

**Proposed fix.** Return a sign rather than a difference: `return (b->index > a->index) - (b->index < a->index);` and widen min_value at line 68 to `long int`.

**Verifier.** source/history.c:46 `long int index;`, :51-56 `static int __element_sort_func(...) { ... return b->index - a->index; }` used by g_qsort_with_data at :64, and :68 `int min_value = list[length - 1]->index;` — both truncate a long to int on LP64. Only reachable with index values near 2^31, which for a usage counter means a hand-edited or corrupted cache file (strtol at :148 will happily parse them), so low.

## `source/mode.c:126` — mode_get_completion passes an uninitialized int to _get_display_value

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`int state;` is declared uninitialized and `&state` is handed to `mode->_get_display_value(...)` on the next line. Every mode implementation ORs into it rather than assigning: source/modes/drun.c:1485 `*state |= MARKUP;`, source/modes/dmenu.c:445/453/457/460, source/modes/script.c:485-503, source/modes/window.c:959-964, source/modes/combi.c:243, source/modes/help-keys.c:93. That is a read of an indeterminate value. Contrast source/view.c:665, :687, :714 and :1496, which all initialize `int fstate = 0;` first.

**Failure scenario.** Any call to mode_get_completion() on a mode without a `_get_completion` implementation — e.g. source/view.c:473 during sorted filtering, or ROW_SELECT at source/view.c:629 — evaluates `*state |= MARKUP` on uninitialized stack memory. Under -fsanitize=memory / MSan this is reported immediately; on real builds the value is discarded so it is latent UB rather than visible corruption, but it is exactly the kind of thing that becomes a real bug the moment the value is used.

**Proposed fix.** `int state = 0;`

**Verifier.** source/mode.c:126 is `int state;` with no initializer, and :128 passes `&state` to `mode->_get_display_value(...)`. Callees OR into it without assigning first: source/modes/drun.c:1485 `*state |= MARKUP;`, source/modes/dmenu.c:460, source/modes/combi.c:243. So the callee reads an indeterminate value. The result is discarded by mode_get_completion, so it is latent UB / an MSan report rather than visible corruption; low.

## `source/mode.c:206` — snprintf into cfg_name_key uses a hardcoded 128 instead of sizeof

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`snprintf(mode->cfg_name_key, 128, "display-%s", mode->name);` duplicates the literal from `char cfg_name_key[128];` (include/mode-private.h:199). mode-private.h is an *installed* header (meson.build:195-201), so plugins compile against that struct layout; the two constants can drift independently.

**Failure scenario.** Shrink cfg_name_key in mode-private.h without touching mode.c and every mode name longer than the new size silently overflows the field. The truncated-name case is also unreported: a mode named with >119 characters gets a silently truncated option key that will not match its config entry.

**Proposed fix.** `snprintf(mode->cfg_name_key, sizeof(mode->cfg_name_key), "display-%s", mode->name);`

**Verifier.** source/mode.c:206 is `snprintf(mode->cfg_name_key, 128, "display-%s", mode->name);` and include/mode-private.h:199 declares `char cfg_name_key[128];`, so the literal is duplicated across an installed header that plugins compile against. snprintf is safe today and truncation is silently ignored; maintainability only.

## `source/modes/drun.c:1155` — entry->type is taken verbatim from the cache and later hits g_assert_not_reached()

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

drun_read_cache reads an int32 and assigns it with no range check: `int32_t type = 0; if (drun_read_integer(fd, &type)) {...} entry->type = type;` (lines 1155-1160). drun_mode_result switches on that value and calls g_assert_not_reached() in the default arm (line 1390). DRUN_DESKTOP_ENTRY_TYPE_UNDETERMINED (0) and DRUN_DESKTOP_ENTRY_TYPE_DIRECTORY (4) are both valid enum values that read_desktop_file never produces but the cache path happily restores.

**Failure scenario.** With `drun-use-desktop-cache: true`, a truncated write (power loss during write_cache, which does no atomic rename) or any hand-edited cache leaves an entry with type 0. Selecting that row hits the default arm and g_assert_not_reached() aborts the process with SIGABRT instead of showing an error.

**Proposed fix.** Validate on read (`if (type < DRUN_DESKTOP_ENTRY_TYPE_APPLICATION || type > DRUN_DESKTOP_ENTRY_TYPE_DIRECTORY) { error = 1; continue; }`) and replace g_assert_not_reached() in drun_mode_result with a g_warning + RELOAD_DIALOG.

**Verifier.** drun.c:1155-1160 `int32_t type = 0; if (drun_read_integer(fd, &(type))) {...} entry->type = type;` with no range check. drun_mode_result switches on entry_list[selected_line].type at drun.c:1381-1390 with `default: g_assert_not_reached();`. DRUN_DESKTOP_ENTRY_TYPE_UNDETERMINED (0) and _DIRECTORY are enum values the switch does not handle. Requires the opt-in cache plus a corrupt/hand-edited file.

## `source/modes/filebrowser.c:176` — compare_time truncates a time_t difference to gint

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`return fb->time - fa->time;` where time is time_t (64-bit on all supported targets) and the function returns gint. Differences larger than INT_MAX wrap.

**Failure scenario.** Sorting by mtime a directory containing both a file from 1970 (or with a bogus far-future timestamp, common on FAT/extracted archives) and a recent file: the difference exceeds 2^31 and the sign flips, so the qsort comparator is inconsistent and the listing comes out in a nonsensical order (glibc qsort with an inconsistent comparator can also read out of bounds).

**Proposed fix.** `return (fb->time > fa->time) - (fb->time < fa->time);`

**Verifier.** filebrowser.c:176 `return fb->time - fa->time;` inside `static gint compare_time(...)`, where FBFile.time is time_t (filebrowser.c:101). The 64-bit difference is truncated to gint, so differences past INT_MAX flip sign and the qsort comparator becomes inconsistent. The `< 0` guards at lines 168-174 only handle the sentinel case.

## `source/modes/ssh.c:379` — Include with no argument passes NULL through rofi_expand_path into g_path_is_absolute/glob

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`token = strtok_r(NULL, SSH_TOKEN_DELIM, &strtok_pointer);` can return NULL for a bare `Include` line. The result is passed unchecked to rofi_expand_path (line 381), which returns NULL (source/helper.c:785-787); then g_path_is_absolute(NULL) trips a glib assertion and returns FALSE, and g_build_filename(dirname, NULL, NULL) silently yields just the directory, which is then globbed and parsed.

**Failure scenario.** A ~/.ssh/config containing a lone `Include` line prints a `g_path_is_absolute: assertion 'file_name != NULL' failed` warning to stderr and then globs ~/.ssh itself, attempting to parse every file in the directory (including private keys) as an ssh config.

**Proposed fix.** `if (token == NULL) { g_warning("Include without argument in %s", filename); g_free(low_token); continue; }` before expanding.

**Verifier.** ssh.c:379 `token = strtok_r(NULL, SSH_TOKEN_DELIM, &strtok_pointer);` returns NULL for a bare `Include` line, and ssh.c:381 passes it unchecked to rofi_expand_path, which returns NULL (helper.c:784-786). ssh.c:383 then calls g_path_is_absolute(NULL) — a g_return_val_if_fail assertion warning returning FALSE — and g_build_filename(dirname, NULL, NULL) yields just the directory, which is globbed at ssh.c:392. The claim's scenario is overstated: glob("~/.ssh") matches only that one directory entry, so the recursive call just fails to parse a directory; it does not enumerate and read private keys.

## `source/modes/wayland-window.c:156` — ext<->wlr correlation matches any two windows that both have no app_id

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** wayland backend: source/wayland/view.c, source/m

wlr_toplevels_set_one_identifier correlates an ext_foreign_toplevel_handle_v1 with a zwlr_foreign_toplevel_handle_v1 purely by `g_strcmp0(toplevel->app_id, entry->app_id) != 0`. g_strcmp0(NULL, NULL) returns 0, so a wlr toplevel that never received an app_id event matches an ext toplevel that never received one either. Both protocols make app_id optional (ext-foreign-toplevel-list-v1 `app_id` and wlr `app_id` are both plain events that may never arrive), and neither list is ordered by anything the client controls: both are built with g_list_prepend (lines 276 and 396) from independent event streams. The code comment at lines 560-567 acknowledges the ordering assumption for same-class windows but not the NULL==NULL case, which collapses *all* app_id-less windows into one equivalence class.

**Failure scenario.** Two unrelated windows with no app_id are open (e.g. a GTK file-chooser dialog and a Java/AWT window; both frequently send no app_id). The user selects the Java window and triggers a `window-command` (e.g. `-window-command 'swaymsg [con_id={window}] kill'`). wlr_toplevels_set_one_identifier walks pd->wlr_toplevels from the head and assigns the ext identifier of the GTK dialog to the Java window's handle. The command then runs against the GTK dialog's identifier and kills the wrong window.

**Proposed fix.** Skip entries where either app_id is NULL (`if (entry->app_id == NULL || toplevel->app_id == NULL) continue;`) and, better, stop correlating by app_id at all: bind ext_foreign_toplevel_list_v1 and use its identifiers as the primary key, or refuse the window-command when a unique correlation cannot be established.

**Verifier.** source/modes/wayland-window.c:156 is `if (g_strcmp0(toplevel->app_id, entry->app_id) != 0) continue;` and g_strcmp0(NULL,NULL) is 0, so two app_id-less handles do match, as claimed. But both lists are built by g_list_prepend from the same compositor's event stream (276 and 396), so the app_id-less windows form one class whose relative order is preserved exactly as well as the same-app_id class the comment at 560-567 already documents as an accepted assumption. Mis-targeting therefore requires the compositor to enumerate the two protocols in different orders — the already-acknowledged risk, not a new one. Real but a subset of the documented heuristic, so low.

## `source/modes/wayland-window.c:630` — Activation dereferences wayland->last_seat without a NULL check

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** wayland backend: source/wayland/view.c, source/m

`wlr_foreign_toplevel_handle_activate(toplevel, pd->wayland->last_seat->seat)` chains three unchecked pointers. wayland->last_seat (include/wayland-internal.h:75) is only ever assigned inside input event handlers: wayland_keyboard_enter (source/wayland/display.c:408), wayland_keyboard_key (source/wayland/display.c:490) and wayland_pointer_button (source/wayland/display.c:891). It is never initialised at setup, and there is no fallback to 'any seat' even though wayland->seats is a populated hash table (source/wayland/display.c:1749-1751 asserts it is non-empty).

**Failure scenario.** A seat with no keyboard capability and no pointer click delivered to the layer surface (e.g. a touch-only device, or a compositor that grants no keyboard focus to the overlay layer) leaves last_seat NULL. The MENU_OK branch then reads NULL->seat and crashes. It is also reachable whenever an activation is driven by anything other than a physical key/click on the surface.

**Proposed fix.** Guard the call, and fall back to any entry in wayland->seats when last_seat is NULL; show an error dialog rather than crashing if no seat is available.

**Verifier.** source/modes/wayland-window.c:629-630 does deref pd->wayland->last_seat->seat with no check, and grep shows last_seat (include/wayland-internal.h:75) is assigned only at source/wayland/display.c:408 (keyboard_enter), 490 (keyboard_key) and 891 (pointer_button) — never initialised at setup. But every path that can produce MENU_OK goes through one of those three handlers first (each assigns last_seat before dispatching), and there is no wl_touch handling in source/wayland/display.c at all, so the claimed touch-only route cannot even reach the mode. Missing defensive check is real; practical reachability is not demonstrated.

## `source/modes/wayland-window.c:355` — A window closing during the initial roundtrip permanently widens the title/app_id padding columns

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wlr_foreign_toplevel_handle_closed removes `self` from pd->wlr_toplevels (line 354) and then calls wayland_window_update_toplevel(self) (line 355). When pd->visible is still FALSE — i.e. before line 499 sets it, during the initial wl_display_roundtrip at line 498 — wayland_window_update_toplevel takes the 'initial fetch, just add the current item' branch (lines 179-181) and folds the *closed* toplevel's title_len/app_id_len into pd->title_len/pd->app_id_len. Those maxima are then never recomputed until some other toplevel emits `done` while visible.

**Failure scenario.** A window with a very long title exits in the moment `rofi -show window` is starting up. Its length is baked into pd->title_len; helper_eval_add_str (line 746, `spaces = MAX(0, max_len - nc)`) pads every remaining row out to that width, leaving a wide blank gutter for the whole session.

**Proposed fix.** In the closed handler, skip the update for the removed entry, or always take the recompute-from-scratch branch when the item has already been unlinked.

**Verifier.** source/modes/wayland-window.c:353-356: state set, g_list_remove at 354, then wayland_window_update_toplevel(self) at 355 with `self` still valid. In wayland_window_update_toplevel (176-189) the !pd->visible branch (179-181) calls toplevels_list_update_max_len on that closed toplevel, folding its title_len/app_id_len into pd->title_len/pd->app_id_len; pd->visible is only set TRUE at line 499 after the initial roundtrip at 498. The maxima are only recomputed in the `visible` branch (183-186). helper_eval_add_str pads with `spaces = MAX(0, max_len - nc)` at line 746. Narrow timing window, cosmetic effect.

## `source/modes/wayland-window.c:335` — Unbounded left shift by a compositor-supplied state value

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

`wl_array_for_each(elem, value) { self->state |= 1 << *elem; }` shifts a signed int 1 by an unvalidated uint32_t taken straight off the wire. protocols/wlr-foreign-toplevel-management-unstable-v1.xml:147-157 currently defines values 0-3, but the enum is not marked as a bitfield and nothing constrains a compositor (or a future protocol version) to that range.

**Failure scenario.** A compositor sends a state value of 32 or greater (or any value >= 31, which overflows the signed int) in the `state` array. `1 << 32` is undefined behaviour in C; with -O2 on x86 the shift count is masked to 0, silently setting TOPLEVEL_STATE_MAXIMIZED on an unrelated window.

**Proposed fix.** `if (*elem < 31) self->state |= 1u << *elem;` and make `state` unsigned.

**Verifier.** source/modes/wayland-window.c:330-336: `uint32_t *elem; ... wl_array_for_each(elem, value) { self->state |= 1 << *elem; }` with self->state declared `int` (line 107). The shift count comes straight off the wire with no bound check; `1 << *elem` is UB for *elem >= 31. protocols/wlr-foreign-toplevel-management-unstable-v1.xml only defines 0-3, so this needs a nonconforming or future compositor.

## `source/modes/wayland-window.c:838` — Icon cache keyed on size and scale but not on app_id

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

_get_icon short-circuits on `cached_icon_uid > 0 && cached_icon_size == height && cached_icon_scale == scale` (lines 838-841) and returns the cached icon. wlr_foreign_toplevel_handle_app_id (lines 303-312) can fire again at any time — the protocol permits the app_id to change over a toplevel's lifetime — and it updates self->app_id without invalidating the icon cache fields.

**Failure scenario.** A window changes its app_id while the switcher is open (Electron/Chromium apps re-set app_id when switching profiles; some compositors normalise app_id late). The list row's text updates to the new app_id but the icon stays the one fetched for the old app_id.

**Proposed fix.** Reset cached_icon_uid to 0 inside wlr_foreign_toplevel_handle_app_id.

**Verifier.** source/modes/wayland-window.c:838-841 short-circuits on `cached_icon_uid > 0 && cached_icon_size == height && cached_icon_scale == scale` and returns rofi_icon_fetcher_get(cached_icon_uid); the cache key never includes app_id. wlr_foreign_toplevel_handle_app_id (303-312) frees and replaces self->app_id and recomputes app_id_len but does not reset cached_icon_uid. A late/changed app_id therefore keeps the stale icon. Cosmetic.

## `source/modes/wayland-window.c:153` — wlr_toplevels_set_one_identifier dereferences its user_data list head before testing it

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** wayland backend: source/wayland/view.c, source/m

The function is entered as a GFunc from g_list_foreach(pd->ext_toplevels, ..., pd->wlr_toplevels) at lines 568-569. It immediately enters a do/while whose body reads `wlr_toplevels->data` (lines 152-153) before the loop condition at line 164 has a chance to test the pointer. If pd->wlr_toplevels is NULL while pd->ext_toplevels is not, this is a NULL dereference. (The do/while + `continue` construct itself is correct — `continue` in a do/while jumps to the controlling expression, which performs the g_list_next advance — but the pre-test is missing.)

**Failure scenario.** Currently masked because the only caller reaches this line via a non-NULL `toplevel` taken from pd->wlr_toplevels. It becomes live the moment the guard recommended in wlr-toplevel-null-on-stale-selection is added in a form that still calls into the correlation (e.g. correlating eagerly on `done` rather than lazily on action), or if the correlation is ever invoked from the ext side: an ext-only compositor with zero wlr toplevels then crashes on the first window-command.

**Proposed fix.** Convert to `for (GList *l = wlr_toplevels; l != NULL; l = g_list_next(l))`, which tests before the first dereference.

**Verifier.** source/modes/wayland-window.c:151-164 is a do/while whose body reads `wlr_toplevels->data` at 152-153 before the controlling expression at 164 can test for NULL, so a NULL user_data list would fault. The sole caller (568-569) passes pd->wlr_toplevels from wayland_act_on_window, which is only reached from line 626 with a `toplevel` that was itself taken from that list and already dereferenced at line 559 — so today the list is non-empty whenever this runs (an empty list crashes earlier, which is the wlr-toplevel-null-on-stale-selection finding, not this one). Latent robustness issue only.

## `source/modes/window.c:1102` — Icon-theme fetch uid is not re-queried after an icon size/scale change (non-prefer-icon-theme path)

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

At lines 1082-1090 a size or scale change destroys c->icon and resets the *_checked flags, but c->icon_fetch_uid is left intact. The non-prefer_icon_theme branch then re-queries only `if (c->icon_fetch_uid == 0)` (line 1102), so after the first query it keeps calling rofi_icon_fetcher_get_ex() with the uid that was requested for the *old* size. The prefer_icon_theme branch immediately below does it correctly, testing `c->icon_fetch_uid == 0 || c->icon_fetch_size != size || c->icon_fetch_scale != scale` (lines 1117-1118) — the two branches disagree.

**Failure scenario.** A theme sets a different `size` on the element icon than the one first used for the current-entry icon (or display_scale() changes): window-mode entries keep rendering the icon at the size fetched on the first query, scaled/blurry, and never refresh.

**Proposed fix.** Use the same condition as line 1117 in the branch at line 1102, or reset c->icon_fetch_uid to 0 inside the block at lines 1082-1090.

**Verifier.** source/modes/window.c:1082-1090 resets c->icon, thumbnail_checked, icon_checked and icon_theme_checked on a size/scale change but leaves c->icon_fetch_uid. The non-prefer branch re-queries only under `if (c->icon_fetch_uid == 0)` (line 1102), while the prefer branch at 1117-1118 correctly tests `c->icon_fetch_uid == 0 || c->icon_fetch_size != size || c->icon_fetch_scale != scale`. Lines 1137-1138 then overwrite icon_fetch_size/scale unconditionally, so the stale uid is never revisited.

## `source/rofi-icon-fetcher.c:743` — g_object_unref(NULL) when gdk_pixbuf_new_from_file_at_scale fails without setting GError

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/rofi-icon-fetcher.c:721-744: the `else` branch assumes that a NULL `error` implies a non-NULL `pb`:
```
  } else {
    icon_surf = rofi_icon_fetcher_get_surface_from_pixbuf(pb);
    g_object_unref(pb);
  }
```
gdk_pixbuf_new_from_file_at_scale() has `g_return_val_if_fail (width > 0 || width == -1, NULL)`; when that precondition trips it returns NULL and never touches the GError. The requested size is theme controlled - source/widgets/icon.c:173-180 does `b->size = distance_get_pixel(rofi_theme_get_distance(WIDGET(b), "size", 16), ...)` then `rofi_icon_fetcher_query(filename, b->size)` - and lines 714-718 only multiply by scale when the value is already > 0, so 0 or a negative size reaches gdk-pixbuf unchanged.

**Failure scenario.** A theme with `element-icon { size: 0px; filename: "foo.png"; }` (or a negative size): the worker calls gdk_pixbuf_new_from_file_at_scale(path, 0, 0, TRUE) -> NULL with error == NULL -> rofi_icon_fetcher_get_surface_from_pixbuf(NULL) returns NULL (line 389) -> g_object_unref(NULL) -> GLib CRITICAL 'assertion G_IS_OBJECT (object) failed' (fatal with G_DEBUG=fatal-criticals).

**Proposed fix.** Guard the unref: `if (pb) { icon_surf = ...; g_object_unref(pb); }` at source/rofi-icon-fetcher.c:741-744, and reject wsize/hsize <= 0 early in rofi_icon_fetcher_query()/_advanced() (lines 752, 799).

**Verifier.** source/rofi-icon-fetcher.c:741-744 is `} else { icon_surf = rofi_icon_fetcher_get_surface_from_pixbuf(pb); g_object_unref(pb); }` — the else of `if (error != NULL)`, so it assumes error==NULL implies pb!=NULL. gdk_pixbuf_new_from_file_at_scale's documented preconditions (`width > 0 || width == -1`) return NULL via g_return_val_if_fail without setting the GError. Lines 714-718 confirm a 0 or negative size passes through untouched (`if (width > 0) width *= sentry->scale;`), and the size is theme-derived: source/widgets/icon.c:173-174 `b->size = distance_get_pixel(rofi_theme_get_distance(WIDGET(b), "size", b->size), ...)` feeding rofi_icon_fetcher_query at icon.c:180. rofi_icon_fetcher_get_surface_from_pixbuf(NULL) returns NULL at line 389-391, then g_object_unref(NULL) is a GLib CRITICAL. Needs a nonsensical `size: 0px`, so low rather than medium.

## `source/rofi-icon-fetcher.c:407` — height * stride overflows int when computing the pixbuf end pointer

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** helper / history / icon-fetcher / display / test

`pixels_end = pixels + height * stride;` (line 407) where both `height` and `stride` are gint (line 384, 396). The product is evaluated in int arithmetic before being widened for the pointer addition, so it overflows (signed overflow, UB; in practice a wrapped/negative offset) once height*stride exceeds 2^31. gdk-pixbuf itself supports images well past that. `lo = o * width` (line 409) is a guint product of an int width and can wrap the same way.

**Failure scenario.** Load a very large source image (e.g. a ~24000x24000 PNG, stride ~96000, product ~2.3e9) as an icon or preview. height*stride wraps negative, pixels_end lands *before* pixels, the `while (pixels < pixels_end)` loop is skipped and the surface is silently blank — or, with a different size, wraps to a small positive value and the loop under-copies. With -ftrapv / UBSan it is a hard abort.

**Proposed fix.** Compute in a wide type: `pixels_end = pixels + (gsize)height * (gsize)stride;` and `lo = (gsize)o * (gsize)width;`. Also reject implausible dimensions before allocating the cairo surface.

**Verifier.** source/rofi-icon-fetcher.c:384/396 declare `gint width, height; gint stride;` and :407 computes `pixels_end = pixels + height * stride;` in int arithmetic before the pointer add; :409 `lo = o * width` is likewise an int product stored in guint. The overflow is genuine UB, but only for images whose full-size height*stride exceeds 2^31, which requires the wsize/hsize == -1 path (source/theme.c:1088) plus a ~2GB-of-pixels image — not statically decidable.

## `source/rofi.c:809` — "There are N more errors" undercounts by one and hides the third message

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

After the `index < 2` loop, `iter` points at the third (0-based index 2) element. `g_list_length(iter) > 1` is therefore false when there are exactly 3 errors, so the third one is neither printed nor announced; with 4 errors it reports `g_list_length(iter) - 1 == 1` when 2 are actually hidden. The identical code is duplicated at source/view.c:231-233, where it additionally says "errors" while iterating list_of_warning_msgs.

**Failure scenario.** A config file with three parse errors: the dialog shows the first two and prints nothing about the third. With four errors it says "There are 1 more errors" when two are hidden.

**Proposed fix.** `if (g_list_length(iter) > 0) g_string_append_printf(emesg, "\nThere are <b>%u</b> more errors.", g_list_length(iter));` in both places, and change "errors" to "warnings" in source/view.c:232.

**Verifier.** source/rofi.c:802-812: the loop condition is `iter != NULL && index < 2` and iter is advanced in the loop header, so after two printed messages iter points at the third element. With exactly 3 errors g_list_length(iter)==1, so the `> 1` test at :809 is false and the third error is neither printed nor counted; with 4 errors it prints `g_list_length(iter) - 1 == 1` while 2 are hidden. The same block is duplicated at source/view.c:225-233, where it appends "There are <b>%u</b> more errors." while iterating list_of_warning_msgs.

## `source/rofi.c:1255` — -record-screenshots 0 divides by zero and casts infinity to guint

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`g_timeout_add((guint)(1000 / (double)interval), record, NULL);` — interval comes from find_arg_uint (source/helper.c:373-381), which is a bare strtoul with no validation, so 0 (or any non-numeric argument) is accepted. 1000/0.0 is +inf; converting +inf to guint is undefined behaviour in C99 6.3.1.4.

**Failure scenario.** `rofi -show run -record-screenshots 0` -> the interval passed to g_timeout_add is an unspecified guint (commonly 0 or 0xFFFFFFFF), producing either a busy-loop that screenshots every main-loop iteration or a timer that never fires.

**Proposed fix.** `if (find_arg_uint("-record-screenshots", &interval) && interval > 0)`.

**Verifier.** source/rofi.c:1254-1256: `if (find_arg_uint("-record-screenshots", &interval)) { g_timeout_add((guint)(1000 / (double)interval), record, NULL); }`. find_arg_uint (source/helper.c:373-381) is a bare strtoul with no validation and returns TRUE as long as an argument follows, so interval==0 is accepted; 1000/0.0 is +inf and the conversion of +inf to guint is undefined (C99 6.3.1.4). Low: requires the user to pass 0 explicitly.

## `source/theme.c:452` — P_DOUBLE dump converts an unbounded double to int

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

int_rofi_theme_print_property's P_DOUBLE case does `int top = (int)fabs(p->value.f);` and `int bottom = (fabs(fmod(p->value.f,1.0)))*100;` then prints "%s%d.%02d". A double whose magnitude exceeds INT_MAX makes the conversion undefined, and the two-decimal formatting is lossy for every value regardless. printf_double() (line 264) already exists and does this correctly for distances.

**Failure scenario.** `rofi -theme-str '* { scaling: 1e30; }' -dump-theme` performs an out-of-range double→int conversion (UBSan: "outside the range of representable values of type int") and prints a garbage number, so the dumped theme does not reproduce the input.

**Proposed fix.** Print P_DOUBLE via printf_double()/g_ascii_formatd like the distance printer does.

**Verifier.** source/theme.c:450-455: `case P_DOUBLE: { char sign = (p->value.f < 0); int top = (int)fabs(p->value.f); int bottom = (fabs(fmod(p->value.f, 1.0))) * 100; printf("%s%d.%02d", ...); }`. The double->int conversion is unguarded, so any |f| > INT_MAX is UB, and the fixed two-decimal formatting is lossy for all values. printf_double() at source/theme.c:264-271 does exist and uses g_ascii_formatd with "%.4f", confirming a correct helper is already available and unused here.

## `source/timings.c:60` — rofi_timings_quit dereferences global_timer with no NULL check and does not reset it

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`g_timer_elapsed(global_timer, NULL)` and `g_timer_destroy(global_timer)` run unconditionally; global_timer is left dangling afterwards. cleanup() calls TIMINGS_STOP() (source/rofi.c:585) and cleanup() is reachable from eight different early-return paths in main(). Today TIMINGS_START() at source/rofi.c:966 happens before all of them, so it is safe — but it is safe only by accident of ordering, and any new early exit placed above line 966 (the -log handling at :948-965 is already there) turns it into a NULL dereference at process exit.

**Failure scenario.** Move or add any `cleanup(); return ...;` above source/rofi.c:966 — for example an early validation of -log — and every such exit crashes in g_timer_elapsed(NULL).

**Proposed fix.** `if (global_timer == NULL) return;` at the top of rofi_timings_quit, and set `global_timer = NULL` after g_timer_destroy.

**Verifier.** source/timings.c:59-63: rofi_timings_quit calls `g_timer_elapsed(global_timer, NULL)` at :60 and `g_timer_destroy(global_timer)` at :62 with no NULL check and never sets global_timer=NULL. TIMINGS_STOP() is at source/rofi.c:585 inside cleanup(). I verified the ordering: TIMINGS_START() is at source/rofi.c:966 and every cleanup() call site in main() is at line 983 or later (983, 1049, 1092, 1201, 1232, 1238, 1243, 1271, 1276, 1297, 1314, 1322, 1341), so it is currently unreachable. Accurate as a latent-risk claim only.

## `source/view.c:399` — rofi_view_get_next_position bounds against num_lines instead of filtered_lines

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`if ((selected + 1) < state->num_lines) next_pos = state->line_map[selected + 1];` — line_map only holds `filtered_lines` valid entries after refiltering (source/view.c:869); positions from filtered_lines up to num_lines are stale leftovers from a previous filter pass or zeros from the g_malloc0_n. Every other read of line_map in the file compares against filtered_lines.

**Failure scenario.** With a filter active (say 3 of 500 entries match) and the last match selected, rofi_view_get_next_position returns line_map[3], a leftover index from the previous unfiltered pass, instead of the current selection. Callers such as dmenu's next-position handling then act on the wrong entry.

**Proposed fix.** Compare against `state->filtered_lines`.

**Verifier.** source/view.c:396-402: `if ((selected + 1) < state->num_lines) { (next_pos) = state->line_map[selected + 1]; }`. line_map holds only filtered_lines valid entries after refiltering (set at :868 / :878-880); with a filter active and the last match selected, selected+1 == filtered_lines <= num_lines, so it reads a stale leftover or a zero from g_malloc0_n. Every other read in the file compares against filtered_lines. The value is consumed by source/modes/dmenu.c:834 (`unsigned int next_pos = rofi_view_get_next_position(state);`), so the wrong row can be acted on. In-bounds of the allocation, so wrong-value not memory-unsafe.

## `source/view.c:1992` — rofi_view_error_dialog can never fail, so its callers' error handling is dead code

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

The function ends with an unconditional `return TRUE;` and has no other return statement. source/rofi.c:883-885 and :889-891 both do `if (!rofi_view_error_dialog(msg, markup)) { g_main_loop_quit(main_loop); }` — branches that can never be taken.

**Failure scenario.** `rofi -e ''` with a theme that fails to produce a usable window: the intended fallback (quit the main loop rather than hang with an invisible dialog) never runs, because the failure is never reported. The user gets a hung process instead of an exit.

**Proposed fix.** Either make the function return FALSE on real failure (e.g. when box_create/textbox_create yield an unusable window) or change the signature to void and delete the dead branches.

**Verifier.** rofi_view_error_dialog is source/view.c:1944-1993 and its only return statement is the unconditional `return TRUE;` at :1992. source/rofi.c:883-885 and :889-891 both wrap it in `if (!rofi_view_error_dialog(msg, markup)) { g_main_loop_quit(main_loop); }`, so those branches are dead. Dead error handling, no runtime defect today.

## `source/view.c:1553` — MOUSE_CLICK_DOWN falls through to MOUSE_CLICK_UP when the widget has no action

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

In textbox_button_trigger_action the `case MOUSE_CLICK_DOWN:` block returns only inside `if (type)`; when rofi_theme_get_string returns NULL, control falls through into `case MOUSE_CLICK_UP:` with no `rofi_fallthrough` marker. The codebase uses that marker deliberately elsewhere in this same file (source/view.c:1415, :1418), and meson.build sets warning_level 3.

**Failure scenario.** Currently benign — the fallthrough target just breaks and returns IGNORED, the same result. But it is indistinguishable from an accidental missing break, and adding any statement to the MOUSE_CLICK_UP case silently changes MOUSE_CLICK_DOWN behaviour for action-less buttons.

**Proposed fix.** Add an explicit `break;` at the end of the MOUSE_CLICK_DOWN block.

**Verifier.** source/view.c:1553-1570: the MOUSE_CLICK_DOWN block returns only inside `if (type)`, so when rofi_theme_get_string returns NULL control falls into `case MOUSE_CLICK_UP:` at :1570 with no rofi_fallthrough marker; the marker is used deliberately at :1415 and :1418 in the same file. Behaviourally identical today (the fallthrough target just breaks to the same IGNORED return), so this is a maintainability hazard only.

## `source/view.c:2008` — Thread-pool destroy notify has a self-documented "we still hit it (and crash)" workaround

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** core: source/rofi.c, source/view.c, source/mode.

rofi_thread_pool_state_free() special-cases `GPOINTER_TO_UINT(data) == 1` with the comment "This is a weirdness from glib ... This should be removed from queue to avoid hitting this method. In practice, we still hit it (and crash)". The sentinel check only catches the literal value 1; anything else glib pushes is cast to `thread_state *` and `ts->free` is dereferenced.

**Failure scenario.** g_thread_pool_free(tpool, TRUE, FALSE) at :2053 — called on every page change via page_changed_callback and from cleanup() — runs the destroy notify over whatever is left in the queue. Per the author's own comment this still crashes in practice; the guard is a partial mitigation, not a fix.

**Proposed fix.** Root-cause it: the wake-up sentinel comes from glib pushing a non-NULL dummy when max_threads changes. Track submitted jobs explicitly (a GPtrArray of thread_state owned by the caller) and validate pointers against it in the destroy notify instead of testing for the magic value 1.

**Verifier.** source/view.c:2003-2019 contains rofi_thread_pool_state_free with the verbatim comment ("This is a weirdness from glib... In practice, we still hit it (and crash)") at :2005-2008 and the `if (GPOINTER_TO_UINT(data) == 1)` sentinel at :2009; anything else is cast to thread_state* and ts->free is dereferenced at :2015-2017. g_thread_pool_free(tpool, TRUE, FALSE) at :2053 is indeed called from cleanup() and page_changed_callback (:765). Whether glib pushes any other bogus pointer, and whether the residual crash the comment describes still occurs, cannot be determined statically — and in practice the queue is empty at every call site because refiltering blocks on its cond (:845-851).

## `source/wayland/display.c:208` — Wayland SHM pool uses a fixed global name with O_EXCL - concurrent instances collide

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** build system, packaging and portability (FreeBSD

gchar *shm_name = "/rofi-wayland-surface"; fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600); shm_unlink(shm_name); - The name is a compile-time constant shared by every process on the machine, and O_EXCL makes a collision a hard failure rather than a reuse. There is no retry loop and no randomisation (compare wlroots' randname+retry, or memfd_create, or FreeBSD's SHM_ANON). Grepping the tree for memfd_create and SHM_ANON returns nothing, so there is no platform-specific fast path either.

**Failure scenario.** Two instances start simultaneously (a keybinding fired twice, or two users on one seat/compositor): the second shm_open returns EEXIST, display_buffer_pool_new returns NULL at line 214 after logging "creating a buffer file for N B failed: File exists", and that instance renders nothing. On FreeBSD the same name lives in the kernel shm namespace, so cross-user collisions are equally reachable.

**Proposed fix.** Generate a randomised name and retry on EEXIST, or use memfd_create where available and shm_open(SHM_ANON, ...) on FreeBSD. The name is also a rebrand surface - see the shm-name row in rebrand_surfaces.

**Verifier.** source/wayland/display.c:208-215 read exactly as quoted: `gchar *shm_name = "/rofi-wayland-surface";` then `shm_open(..., O_CREAT | O_EXCL | O_RDWR, 0600)`, `shm_unlink(shm_name)`, and on fd < 0 it logs 'creating a buffer file for %zu B failed' and returns NULL. `grep -rn 'memfd_create\|SHM_ANON'` over the tree returns nothing, so there is no platform fast path. Mitigation the claim omits: shm_unlink is called on the very next line, so the global name exists for only a handful of instructions — the collision window is microseconds, not the lifetime of the process, making this a narrow timing race rather than a systematic conflict between concurrent instances. Whether it is ever hit is a runtime scheduling question.

## `source/wayland/display.c:191` — Unused local `struct wl_buffer *buffer` in display_buffer_pool_new

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

display_buffer_pool_new declares `struct wl_buffer *buffer;` at line 191 and never reads or writes it - the per-buffer objects are stored directly into pool->buffers[i].buffer at lines 247-248. The project builds with warning_level=3 (meson.build:7), i.e. -Wall -Wextra, so this produces a -Wunused-variable diagnostic on every wayland build. Not fatal because -Werror is not set globally (only the four targeted -Werror= flags at meson.build:26 and 38-40).

**Failure scenario.** Anyone building the wayland backend sees a permanent warning in the log; a downstream that adds -Werror (a common distro hardening choice) fails to compile source/wayland/display.c outright.

**Proposed fix.** Delete the declaration at source/wayland/display.c:191.

**Verifier.** Read source/wayland/display.c:189-257 in full. Line 191 declares `  struct wl_buffer *buffer;` and the identifier `buffer` never appears again as a bare local in the function — every use is the struct member `pool->buffers[i].buffer` (246, 250). meson.build:5-8 set `default_options: [ 'c_std=c99', 'warning_level=3' ]`, and -Wunused-variable is in -Wall, so this warns on every wayland build. Not fatal: meson.build:26 and 38-40 are the only -Werror= flags and none of them cover it.

## `source/wayland/view.c:149` — The theme `anchor` property is honoured on X11 and silently ignored on Wayland

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

rofi_get_location reads only the "location" property. source/xcb/view.c:279-283 reads both: `location` and then `anchor` (defaulting to location), and applies them as two independent transforms (position switch at 293-322, anchor switch at 323-356). The Wayland backend has no equivalent — window_update_size_normal (line 162-171) forwards only the location to display_set_surface_dimensions, and wayland_rofi_view_calculate_window_position (lines 146-147) is an empty stub. README.md:181 mentions only that x-offset/y-offset are limited; it does not mention that `anchor` is dropped entirely.

**Failure scenario.** A theme containing `window { location: north; anchor: south; }` renders as intended on X11 (top-anchored placement with the window's own bottom edge at that point) and as plain `location: north` on Wayland, with no warning.

**Proposed fix.** Either map anchor onto the layer-surface anchor bits in display_set_surface_dimensions (source/wayland/display.c:1853-1897) where possible, or emit a one-time g_warning when `anchor` differs from `location` on Wayland, and document the limitation in README.md:175-183.

**Verifier.** source/wayland/view.c:149-152 rofi_get_location reads only "location"; wayland_rofi_view_calculate_window_position at 146-147 is an empty stub; window_update_size_normal (162-171) forwards only rofi_get_location(state) into display_set_surface_dimensions, which maps it to a layer-surface anchor (source/wayland/display.c:1853+). source/xcb/view.c:279-283 reads both "location" and "anchor" and applies them as separate transforms (293-322 and 323-356). README.md:175-183 does not mention `anchor`.

## `source/widgets/box.c:165` — vert_calculate_size() returns before laying out children when content overflows, leaving stale geometry

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/box.c:161-166:
```
  if (b->max_size > rem_height) {
    b->max_size = rem_height;
    g_debug("Widgets to large (height) for box: %d %d", ...);
    return;
  }
```
The early return skips the entire positioning loop (lines 167-192) *and* the `b->max_size += widget_padding_get_padding_height()` normalisation at line 193, so max_size means 'content only' on this path and 'content + padding' on every other path. The mirrored horizontal code deliberately does NOT return (the `return;` at box.c:232 is commented out) and instead clamps `rem` to 0 (lines 238-240), which is the correct behaviour.

**Failure scenario.** A vertical box whose children's desired heights exceed the box height (e.g. a window shrunk below the sum of its rows): the children keep the x/y/w/h from the previous layout pass, so they are drawn at stale positions overlapping the box border instead of being clipped/compressed, and any child added since the last successful pass is drawn at (0,0).

**Proposed fix.** Mirror hori_calculate_size: drop the `return;` at source/widgets/box.c:165, clamp `rem = MAX(0, rem_height - b->max_size)` before the positioning loop, and keep the padding normalisation at line 193 on all paths.

**Verifier.** source/widgets/box.c:161-166 does return early on overflow, skipping the whole positioning loop (167-192) — so no widget_move/widget_resize happens for any child on that pass and they keep the previous pass's geometry — while the mirrored hori_calculate_size deliberately does not (`// return;` at box.c:232) and instead clamps rem to 0 at 238-240. The claim's max_size-semantics half is inert though: I grepped every use of b->max_size (box.c:43,142,155,159,161-169,193,210,224,227-236,263) and it is a scratch field never read outside these two functions, so the missing padding add at line 193 has no observable effect. Visual-only asymmetry.

## `source/widgets/listview.c:330` — Division by lv->menu_columns without a zero check (SIGFPE from a theme setting)

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/listview.c:327-331 (scroll_continious_rows):
```
  selected = lv->selected / lv->menu_columns;
  req_rows = (lv->req_elements + lv->menu_columns - 1) / lv->menu_columns;
```
`lv->menu_columns` is read verbatim from the theme at listview.c:819-820 `rofi_theme_get_integer(WIDGET(lv), "columns", DEFAULT_MENU_COLUMNS)` with no lower bound (rofi_theme_get_integer returns p->value.i unchecked, source/theme.c:836), so `listview { columns: 0; }` yields 0 (and a negative value yields a huge unsigned). scroll_continious_rows() is reached from listview_draw (listview.c:460) *before* the `lv->cur_elements > 0 && lv->max_rows > 0` guard at line 482, so no other check saves it. The same divisor is used unguarded at listview.c:330 and 331.

**Failure scenario.** A theme containing `listview { columns: 0; flow: horizontal; }` combined with `-scroll-method 1` (LISTVIEW_SCROLL_CONTINIOUS, set at source/view.c:1744): the first listview_draw executes `lv->selected / 0` -> SIGFPE, rofi dies on startup.

**Proposed fix.** Clamp on read: `lv->menu_columns = MAX(1, rofi_theme_get_integer(...))` at source/widgets/listview.c:819, and defensively early-return from scroll_continious_rows when menu_columns == 0.

**Verifier.** source/widgets/listview.c:330-331 divides by lv->menu_columns twice with no guard, unlike scroll_per_page which does guard (`(lv->max_elements > 0) ? ... : 0`, listview.c:286-287). lv->menu_columns comes from listview.c:819-820 rofi_theme_get_integer(...,"columns",...) and source/theme.c:836 returns `p->value.i` unchecked. scroll_continious_rows is called from listview_draw:460 before the `lv->cur_elements > 0 && lv->max_rows > 0` guard at line 482, so the guard does not save it. Reachability is narrower than claimed though: it needs `flow: horizontal` AND a non-default scroll method (line 455-460 routes to scroll_per_page otherwise), plus a deliberate `columns: 0`.

## `source/widgets/listview.c:433` — Unsigned underflow of the barview offset when no element fits

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/listview.c:389 resets `lv->barview.cur_visible = 0;` and the RIGHT_TO_LEFT loop at lines 412-432 only increments it inside the body. If the body never executes - `width` (line 388-390, `lv->widget.w` minus padding) is <= 0, or offset is such that the loop condition fails at once - cur_visible stays 0 and line 433 executes `offset -= lv->barview.cur_visible - 1;` on `unsigned int`, i.e. `offset -= UINT_MAX`, which is `offset + 1`. That bogus value is stored as `lv->last_offset` (line 434) and is later used as the base index in listview_element_trigger_action (line 749/755) and listview_element_motion_notify (line 771), and in the `lv->req_elements - lv->last_offset` subtractions at lines 698, 738 and 766 which then underflow to ~4 billion.

**Failure scenario.** A barview layout (`listview { layout: horizontal; }`) whose width is smaller than its own padding, navigated right-to-left: cur_visible == 0, last_offset becomes selected+1 == req_elements, and `lv->req_elements - lv->last_offset` at listview.c:698 wraps to UINT_MAX, so listview_find_mouse_target iterates over all cur_elements rows regardless of how many are actually populated and hit-tests stale rows.

**Proposed fix.** Guard the fix-up: `if (lv->barview.cur_visible > 0) { offset -= lv->barview.cur_visible - 1; lv->last_offset = offset; }` at source/widgets/listview.c:433-434, and clamp the `req_elements - last_offset` subtractions with a MIN().

**Verifier.** Code is as described: listview.c:389 `lv->barview.cur_visible = 0;`, the RIGHT_TO_LEFT loop at 412-432 increments it only in the body, and line 433 does `offset -= lv->barview.cur_visible - 1;` on unsigned int, so cur_visible==0 gives offset+1, stored as lv->last_offset at 434 and later used unguarded in `lv->req_elements - lv->last_offset` at lines 698, 738, 766. Reaching it with cur_visible==0 requires the loop at 413 to make zero iterations, i.e. `width = lv->widget.w - widget_padding_get_padding_width(wid) <= 0` (line 388-390) while widget_draw still called us (so w>=1) — that needs a theme whose listview padding/border exceeds its own width. I cannot confirm such a layout is actually produced at runtime, hence PLAUSIBLE, and the consequence is stale hit-testing, not memory corruption (boxes[] is indexed by i < cur_elements).

## `source/widgets/listview.c:666` — max_rows computation divides by (element_height + spacing) with no zero check

- **kind** correctness · **severity** low · **verdict** PLAUSIBLE · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/listview.c:665-666:
```
    lv->max_rows =
        (spacing_vert + height) / (lv->element_height + spacing_vert);
```
`lv->element_height` is whatever widget_get_desired_height() returned for the probe row at listview.c:809, and `spacing_vert` is the theme's `spacing` distance (listview.c:661, theme-controlled). Neither is validated. box_get_desired_height() legitimately returns 0 for a horizontal box whose only enabled child has zero desired height and zero padding (e.g. an element containing only an element-icon with `size: 0`), so element_height can be 0 while `listview { spacing: 0; }` makes the divisor exactly 0.

**Failure scenario.** Theme with `listview { spacing: 0px; } element { padding: 0; children: [ element-icon ]; } element-icon { size: 0px; }`: the first listview_resize() executes an integer division by zero at listview.c:666 -> SIGFPE.

**Proposed fix.** Clamp the divisor: `unsigned int denom = MAX(1, (int)lv->element_height + spacing_vert);` before the division at source/widgets/listview.c:665-666, and reject element_height == 0 at listview.c:809.

**Verifier.** source/widgets/listview.c:665-666 really is `lv->max_rows = (spacing_vert + height) / (lv->element_height + spacing_vert);` with no zero check; element_height is set only at listview.c:809 from widget_get_desired_height() and spacing_vert from the theme at 661. box_get_desired_height (box.c:95-125) for a horizontal box returns MAX over children + padding, so 0 is arithmetically reachable if every child reports 0 and padding is 0. But I could not construct that from real code — the default element row contains a textbox, and I cannot statically verify that an icon-only element with size:0 actually yields 0 rather than a font-derived minimum. Needs a deliberately degenerate theme; PLAUSIBLE, not confirmed.

## `source/widgets/scrollbar.c:169` — Scrollbar divides by (length - 1), producing inf/NaN converted to unsigned int

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/scrollbar.c:167-173:
```
  double r = (sb->length * wh) / ((double)(sb->length + sb->pos_length));
  unsigned int handle = wid->h - r;
  double sec = ((r) / (double)(sb->length - 1));
  unsigned int height = handle;
  unsigned int y = sb->pos * sec;
```
scrollbar_set_max_value() clamps length to `MAX(1u, max)` (line 135), so length == 1 is a normal, reachable state (exactly one match in the list, listview.c:463). Then `sb->length - 1 == 0`, `sec` is +inf, and `sb->pos * sec` is either +inf (pos > 0) or NaN (pos == 0). Converting an inf/NaN double to `unsigned int` is undefined behaviour in C (C11 6.3.1.4). The identical expression is at scrollbar.c:65 inside scrollbar_scroll_get_line(), where `round(y / sec)` is then converted the same way.

**Failure scenario.** Type a filter that leaves exactly one result with `listview { scrollbar: true; }`: scrollbar_draw computes sec = inf and `unsigned int y = 0 * inf` = NaN -> UB conversion (on x86-64 cvttsd2si yields 0x80000000), and the handle is drawn at a garbage offset before being clamped by the MIN on line 173. Under UBSan this is a hard 'value is outside the range of representable values' report.

**Proposed fix.** Special-case `sb->length <= 1` in scrollbar_draw (source/widgets/scrollbar.c:163-175) and scrollbar_scroll_get_line (line 62-71): treat the handle as filling the whole bar and skip the sec computation.

**Verifier.** source/widgets/scrollbar.c:167-171 is exactly as quoted, and length==1 is a normal state: scrollbar_set_max_value clamps to MAX(1u,max) at line 135 and listview_draw feeds it lv->req_elements at listview.c:463, while the scrollbar's visibility is a pure theme boolean (listview.c:838-839, listview_set_show_scrollbar) that does not auto-hide on short lists — widget_draw at listview.c:555 still runs. So sec = r/0.0 = +inf at line 169 and `unsigned int y = sb->pos * sec;` at line 171 converts +inf (pos==1) or NaN (pos==0, since 0*inf is NaN) to unsigned int, which is UB per C11 6.3.1.4 and a hard UBSan report. Practical impact is small: line 173 `y = MIN(y, wh - handle)` re-clamps the garbage. The companion at line 65 is benign — there sec==inf makes y/sec==0, so round() yields 0.

## `source/widgets/textbox.c:357` — pango_layout_set_markup() failures are silently ignored, leaving the previous row's text in a recycled listview row

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/textbox.c:356-357:
```
  } else if (tb->flags & TB_MARKUP || tb->tbft & MARKUP) {
    pango_layout_set_markup(tb->layout, tb->text, -1);
```
(and the placeholder path at line 343). pango_layout_set_markup() takes no GError; on a parse failure it emits a g_warning and returns *without touching the layout*, so the layout keeps whatever text it held before. Listview rows are recycled objects (source/widgets/listview.c:593-611 reuses lv->boxes[i] across filter changes; update_element() at listview.c:351-369 just re-sets the text), so a row that fails to parse keeps displaying the previous entry's text while `lv->selected` / line_map still point at the new entry.

**Failure scenario.** `printf 'a\n<b>unclosed\nc\n' | rofi -dmenu -markup-rows`: row 2 fails pango markup parsing, so the row still shows "a"; the list now shows "a", "a", "c" while pressing Enter on the second row returns the unparseable entry. The mismatch between what is drawn and what is selected is user-visible and silent.

**Proposed fix.** Use pango_parse_markup()/pango_layout_set_markup_with_accel() with a GError in __textbox_update_pango_text() (source/widgets/textbox.c:339-360) and fall back to pango_layout_set_text() with the raw (escaped) string when parsing fails.

**Verifier.** source/widgets/textbox.c:356-357 `} else if (tb->flags & TB_MARKUP || tb->tbft & MARKUP) { pango_layout_set_markup(tb->layout, tb->text, -1); }` and the placeholder path at line 343 both discard failure; pango_layout_set_markup has no GError and its implementation returns after g_warning without touching the layout, so the stale layout text survives while tb->text is already the new string. Row recycling is real: source/widgets/listview.c:593-611 only frees/creates rows at count boundaries and update_element (listview.c:351-369) merely re-sets text on the reused lv->boxes[i]. So a markup-parse failure leaves the previous entry's glyphs drawn under the new index. Note the modes do pre-escape in some paths (dmenu.c:681, script.c:524, window.c:649 call pango_parse_markup), which narrows but does not close the hole; impact is cosmetic/misleading, not memory-unsafe.

## `source/xcb/view.c:655` — Initial edit_pixmap is created against CacheState.main_window before it is assigned

- **kind** correctness · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

xcb_create_pixmap() at lines 654-655 passes CacheState.main_window as the drawable, but CacheState.main_window is only set to box_window at line 672 — 17 lines later. At this point CacheState.main_window is still XCB_WINDOW_NONE (source/view.c:76 initialises it to XCB_WINDOW_NONE). The cairo surface built on that pixmap (lines 657-659) is therefore built on a pixmap that was never created. The correct drawable is the local `box_window` created at line 633.

**Failure scenario.** Every rofi startup: xcb_create_pixmap is issued with drawable 0, the server answers BadDrawable (silently discarded since the request is unchecked), and all subsequent drawing to XcbState.edit_surf targets a non-existent pixmap generating BadPixmap/BadDrawable errors until xcb_rofi_view_window_update_size() re-creates the pixmap with a valid drawable. All font-option probing at lines 663-669 runs against an error surface.

**Proposed fix.** Pass `box_window` to xcb_create_pixmap at line 655 (or move `CacheState.main_window = box_window;` up to right after line 645).

**Verifier.** source/xcb/view.c:653-655 passes CacheState.main_window as the pixmap drawable; CacheState.main_window is only assigned box_window at line 672. It is XCB_WINDOW_NONE (0) at that point — source/view.c:75-76 initialises `.main_window = XCB_WINDOW_NONE` and xcb/view.c:947 resets it to XCB_WINDOW_NONE on teardown, so it is NONE on every entry. The local `box_window` created at 633 is the correct drawable, and the resize path at 379-382 uses CacheState.main_window correctly (by then it is set). Impact is limited: the bad surface at 657-659 is only used for cairo_surface_get_font_options/pango setup (663-669) before the pixmap is re-created at 379-387.

## `.github/CONTRIBUTING.md:15` — CONTRIBUTING points at freenode while the issue-template config points at Libera

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

CONTRIBUTING.md lines 15 and 25 both link `https://webchat.freenode.net/?channels=#rofi`. The project moved to Libera in 2021 — .github/ISSUE_TEMPLATE/config.yml line 7 uses `https://web.libera.chat/?channels=#rofi` and README.md line 322 says "IRC (#rofi on irc.libera.chat)". So the repo ships two contradictory IRC destinations, one of them to a network the project abandoned. Lines 14 and 24 additionally send users to `https://reddit.com/r/qtools/`, an upstream-controlled community.

**Failure scenario.** A would-be contributor reads CONTRIBUTING.md, joins freenode's #rofi (now an unrelated or empty channel), and gets no response before filing the issue the document was trying to prevent.

**Proposed fix.** Point lines 15 and 25 at whatever chat the fork uses (or remove the IRC lines entirely), and drop or replace the r/qtools links on lines 14 and 24.

**Verifier.** .github/CONTRIBUTING.md lines 15 and 25 both read `[IRC](https://webchat.freenode.net/?channels=#rofi)`, and lines 14 and 24 both read `[FORUM](https://reddit.com/r/qtools/)`. .github/ISSUE_TEMPLATE/config.yml:7 is `url: https://web.libera.chat/?channels=#rofi` and README.md:322 is '- IRC (#rofi on irc.libera.chat)'. The repo does ship two contradictory IRC destinations. Confirmed exactly as described; severity low (docs), though it overlaps the rebrand work since lines 13/16/17/23 also point at davatorium/rofi and DaveDavenport/rofi.

## `.github/workflows/mkdocs.yml:5` — Docs workflow triggers on a `sphinx` branch that does not exist, and mkdocs nav omits index.md

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

The workflow fires on pushes to `sphinx` and `next` (lines 5-6). `git branch -a` shows only `master` and `next` locally and `origin/next` remotely — `sphinx` is dead residue from a docs-toolchain migration. Separately, mkdocs/mkdocs.yml declares an explicit `nav:` (lines 5-100) that never lists `index.md`, even though mkdocs/docs/index.md exists and is the site landing page linked from mkdocs/docs/downloads.md line 6; mkdocs emits an "is not included in the \"nav\" configuration" warning for it on every build. The workflow also pins `mhausenblas/mkdocs-deploy-gh-pages@master` (line 17), an unversioned floating ref on a third-party action.

**Failure scenario.** The `sphinx` trigger is simply never exercised. The nav omission means the rendered site's sidebar starts at "License" and the index page is reachable only by URL, not by navigation.

**Proposed fix.** Drop `sphinx` from line 5, add `- Home: index.md` as the first nav entry in mkdocs/mkdocs.yml, and pin the deploy action to a tag.

**Verifier.** .github/workflows/mkdocs.yml lines 4-6 are `branches: - sphinx - next`, and `git branch -a` shows only master, next, origin/HEAD and origin/next -- no sphinx. mkdocs/mkdocs.yml declares an explicit `nav:` starting line 5, and `grep -n index.md mkdocs/mkdocs.yml` returns nothing while mkdocs/docs/index.md exists (1382 bytes). Line 17 is `uses: mhausenblas/mkdocs-deploy-gh-pages@master`, an unversioned third-party ref. One supporting detail in the claim is wrong: mkdocs/docs/downloads.md line 6 links `[Installation](../INSTALL/)`, not index.md. Core claims hold; severity low (dead trigger plus a build warning).

## `INSTALL.md:28` — Documented `--disable-check` configure flag does not exist under meson

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

INSTALL.md line 28: "check (Can be disabled using the `--disable-check` configure flag)". That is autotools syntax; there is no configure script. meson_options.txt line 3 declares `option('check', type: 'feature', ...)` and meson.build line 148 consumes it as `get_option('check')`, so the real spelling is `meson setup build -Dcheck=disabled`. INSTALL.md line 6 itself says the project uses Meson, so the file contradicts itself.

**Failure scenario.** A packager on a distro without libcheck follows line 28, runs `meson setup build --disable-check`, gets `ERROR: Unknown options: "disable-check"`, and cannot work out how to build without the test dependency.

**Proposed fix.** Change line 28 to reference `-Dcheck=disabled`, matching the style already used at lines 157 and 163 for `-Dxcb=disabled` / `-Dwayland=disabled`.

**Verifier.** INSTALL.md:28 reads '- check (Can be disabled using the `--disable-check` configure flag)'. meson_options.txt:3 is `option('check', type: 'feature', description: 'Build and run libcheck-based tests')` and meson.build:148 is `check = dependency('check', version: '>= 0.11.0', required: get_option('check'))`, so the real spelling is -Dcheck=disabled. INSTALL.md:6 ('Rofi uses [Meson](https://mesonbuild.com/) as build system.') does contradict line 28 in the same file. Confirmed; severity low as a docs-only fix.

## `doc/README.md:6` — Manpage regeneration is documented as a make target but the build is meson/ninja

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

doc/README.md line 6 says to run `make generate-manpage`. There is no Makefile in the tree. `generate-manpage` is a meson `run_target` defined at doc/meson.build line 38, so the actual invocation is `ninja -C build generate-manpage` (or `meson compile -C build generate-manpage`). This is autotools-era residue that survived the meson migration, matching the same residue in .gitlab-ci.yml lines 26-32 and INSTALL.md line 28.

**Failure scenario.** A contributor edits doc/rofi-theme.5.markdown, follows doc/README.md, runs `make generate-manpage`, gets `make: *** No targets specified and no makefile found`, and either gives up or hand-edits generated output.

**Proposed fix.** Change line 6 to `ninja -C build generate-manpage` and note that pandoc >= 2.9 is required (doc/meson.build line 16).

**Verifier.** doc/README.md is 7 lines; line 3 reads 'Manpages can be updated using the following make command:' and line 6 inside the fence is `make generate-manpage`. There is no root Makefile (find for Makefile/Makefile.am/configure.ac hits only subprojects/libnkutils). doc/meson.build:38 is `run_target('generate-manpage', command: ['true'], depends: man_targets)`, so the real invocation is via ninja/meson compile. Autotools residue as described; severity low (a one-line doc fix, contributor confusion only).

## `doc/rofi-sensible-terminal.1.markdown:39` — Manpage terminal list is missing ghostty, which the script does try

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

The script's candidate list at script/rofi-sensible-terminal line 12 ends `... wezterm foot ghostty`. The man page enumerates the same list at lines 20-39 but stops at `foot` — `ghostty` is absent. The man page claims the list is authoritative ("It tries to start one of the following (in that order)").

**Failure scenario.** A ghostty user reads the man page, concludes rofi-sensible-terminal will not find their terminal, and sets $TERMINAL unnecessarily — or a packager auditing the fallback list against the man page reports a phantom discrepancy.

**Proposed fix.** Add `* ghostty` after line 39. Longer term the list is duplicated in two places and drifts; consider generating one from the other.

**Verifier.** script/rofi-sensible-terminal:12 ends `... alacritty kitty wezterm foot ghostty; do`. doc/rofi-sensible-terminal.1.markdown enumerates the list at lines 20-39 and its last entry, line 39, is `* foot` -- ghostty is absent, and line 18 does claim 'It tries to start one of the following (in that order)'. Exactly one missing entry; docs-only, low.

## `doc/rofi-theme-selector.1.markdown:26` — Manpage documents a theme search path the script does not use (`$XDG_DATA_HOME/share/...`)

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

The man page lists the search directories as `${PREFIX}/share/rofi/themes` (line 24), `$XDG_CONFIG_HOME/rofi/themes` (line 25) and `$XDG_DATA_HOME/share/rofi/themes` (line 26). Line 26 is wrong: XDG_DATA_HOME already denotes the data root (`$HOME/.local/share`, as line 31 of this same file correctly states), so `$XDG_DATA_HOME/share/...` is a doubled `share`. The script builds the list at lines 84-88: `DIRS+=":${XDG_DATA_HOME:-${HOME}/.local/share}"` then `TD=${p}/rofi/themes`, i.e. `$XDG_DATA_HOME/rofi/themes`. The man page also omits the XDG_DATA_DIRS sweep (script line 55) and the `$XDG_CONFIG_HOME`-derived entry added at line 85.

**Failure scenario.** User reads the man page, installs a theme to `~/.local/share/share/rofi/themes/mytheme.rasi`, and rofi-theme-selector never finds it. Verified against the script: find_themes only ever looks at `<dir>/rofi/themes`.

**Proposed fix.** Change line 26 to `$XDG_DATA_HOME/rofi/themes` and add the `$XDG_DATA_DIRS` entries the script actually iterates over (line 55).

**Verifier.** doc/rofi-theme-selector.1.markdown lines 24-26 list '${PREFIX}/share/rofi/themes', '$XDG_CONFIG_HOME/rofi/themes' and '$XDG_DATA_HOME/share/rofi/themes'; line 31 of the same file says '$XDG_DATA_HOME is normally unset. Default path is "$HOME/.local/share"', so line 26 is a doubled 'share' by the file's own definition. The script confirms it: line 84 `DIRS+=":${XDG_DATA_HOME:-${HOME}/.local/share}"`, line 85 adds the XDG_CONFIG_HOME entry, and line 88 `TD=${p}/rofi/themes` -- only ever <dir>/rofi/themes. The XDG_DATA_DIRS sweep (line 55) and the $PATH fallback (lines 58-82) are indeed undocumented. Severity low: docs-only.

## `include/rofi-icon-fetcher.h:34` — Installed public header documents a 'file://' prefix the implementation does not accept

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

The doc comments for rofi_icon_fetcher_query (line 34) and rofi_icon_fetcher_query_advanced (line 50) both state 'name can also be a full path, if prefixed with file://'. source/rofi-icon-fetcher.c:539 recognises only the 'thumbnail://' prefix, and line 649 accepts a bare absolute path via g_path_is_absolute(); nothing in the file ever tests for 'file://'. Since this header is installed (meson.build:200) it is the contract third-party plugin authors code against.

**Failure scenario.** A plugin author reads the installed /usr/include/rofi/rofi-icon-fetcher.h and passes "file:///usr/share/icons/foo.png": g_path_is_absolute is false for that string, so it falls through to the nk_xdg_theme_get_icon branch at line 679, the theme lookup fails, and the icon silently never renders with no diagnostic.

**Proposed fix.** Correct both comments to describe the actual accepted forms (absolute path, icon name, 'thumbnail://<path>', pango markup starting with '<span'), or add explicit file:// handling in rofi_icon_fetcher_worker().

**Verifier.** include/rofi-icon-fetcher.h:34 and :50 both read 'name can also be a full path, if prefixed with file://.'. In the implementation the only prefix tested is 'thumbnail://' (source/rofi-icon-fetcher.c:539); absolute paths are accepted bare via g_path_is_absolute at :573 and :649; grep finds no 'file://' anywhere in source/rofi-icon-fetcher.c. The header is installed (meson.build:200).

## `include/widgets/textbox.h:261` — textbox_delete() is documented as deleting bytes but deletes characters

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

include/widgets/textbox.h:256-263:
```
 * @param pos The start position
 * @param dlen The length
 *
 * Remove dlen bytes from position pos.
 */
void textbox_delete(textbox *tb, int pos, int dlen);
```
The implementation treats both parameters as UTF-8 *character* offsets: source/widgets/textbox.c:782 `int len = g_utf8_strlen(tb->text, -1);` and lines 791-792 use g_utf8_offset_to_pointer() for both endpoints. The same confusion exists for textbox_cursor(), whose doc at include/widgets/textbox.h:182 says 'string index'.

**Failure scenario.** n/a - a caller following the header (a plugin, or new in-tree code) passes a byte count for dlen; with any multi-byte text that deletes several times too many characters and, combined with the wrong clamp at textbox.c:788, walks off the buffer.

**Proposed fix.** Correct the doc comments at include/widgets/textbox.h:261 ('Remove dlen characters starting at character position pos') and line 182.

**Verifier.** include/widgets/textbox.h:256-263 does say "Remove dlen bytes from position pos." above `void textbox_delete(textbox *tb, int pos, int dlen);`, and the implementation is character-based: source/widgets/textbox.c:782 `int len = g_utf8_strlen(tb->text, -1);` with g_utf8_offset_to_pointer for both endpoints at 791-792. textbox.h:182 likewise says "Set the cursor position (string index)" for textbox_cursor while textbox.c:642-643 uses g_utf8_strlen. Correct doc bug, but the "a plugin following the header" framing is wrong — textbox.h is not installed (meson.build:195-203), so only in-tree callers can be misled.

## `mkdocs/mkdocs.yml:2` — Frozen historical docs and release notes need an explicit rebrand policy decision

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

Per the audit scope these were not rewritten, but they must be decided on. `ls mkdocs/docs` shows version-frozen trees 1.7.0 through 1.7.9 plus 2.0.0 and `current`, all wired into the nav at mkdocs/mkdocs.yml (the 2.0.0 block is lines 27-35, 1.7.9 starts at 36). `ls releasenotes` shows 0.15.12 through 1.5.3 and beyond - 25 directories. mkdocs/mkdocs.yml:1-4 hardcodes `site_name: Rofi Documentation`, `repo_url: https://github.com/davatorium/rofi/` and issue/discussion links at lines 8-9. .gitattributes:9 and :17 already export-ignore both releasenotes and mkdocs, so neither ships in tarballs. The workflow .github/workflows/mkdocs.yml:19 publishes mkdocs/mkdocs.yml to GitHub Pages on every push to `next`.

**Failure scenario.** n/a - policy item. Left as-is, the fork's GitHub Pages site publishes under the name "Rofi Documentation" with every issue link pointing at davatorium/rofi.

**Proposed fix.** Decide one of: (a) rewrite only mkdocs/mkdocs.yml:1-9 plus mkdocs/docs/current and 2.0.0, freezing 1.7.x verbatim as historical; (b) delete the frozen 1.7.x trees and releasenotes/ entirely since the fork will not ship those versions. Option (a) also requires trimming nav entries for any deleted tree.

**Verifier.** mkdocs/mkdocs.yml:1-4 are `site_name: Rofi Documentation` / `repo_url: https://github.com/davatorium/rofi/` / `edit_uri: mkdocs/docs/` / `theme: readthedocs`, with the issue and discussions links at lines 7-8 (claim said 8-9, off by one). The 2.0.0 nav block is lines 27-35 and 1.7.9 starts at 36, as stated. `ls mkdocs/docs` shows 1.7.0 through 1.7.9, 2.0.0 and current. releasenotes is tracked in HEAD (68 files) spanning 23 version directories, 0.15.12 through 2.0.0 — the claim said 25, a minor overcount. .gitattributes:9 `releasenotes export-ignore` and :17 `mkdocs export-ignore` confirmed, and .github/workflows/mkdocs.yml:19 is `CONFIG_FILE: mkdocs/mkdocs.yml`. A genuine policy item for the rebrand.

## `source/display.c:1` — source/display.c and include/xcb-dummy.h carry no license header

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

Every other file in this domain opens with the 26-line MIT/X11 block (source/wayland/view.c:1-26, source/modes/wayland-window.c:1-26, include/display.h:1-26, include/display-internal.h:1-26, include/modes/wayland-window.h:1-26). source/display.c starts directly at `#include "keyb.h"` and include/xcb-dummy.h starts directly at its include guard.

**Failure scenario.** n/a — licensing/provenance hygiene. It matters for the rebrand because the copyright-header sweep will skip these two files, leaving the fork with two unlicensed source files.

**Proposed fix.** Add the same MIT block, preserving the existing Qball Cow copyright line as the MIT license requires, before adding any fork copyright line.

**Verifier.** source/display.c:1 is `#include "keyb.h"` — no comment block at all — and include/xcb-dummy.h line 1 is `#ifndef ROFI_XCB_DUMMY_H`, also with no license text (its only comment, lines 4-6, is an explanatory note). Both differ from the surrounding files in this domain, which open with the MIT block.

## `source/rofi.c:680` — The `windowcd` mode is X11-only and its absence on Wayland is undocumented

- **kind** docs · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

rofi_collect_modes registers both window_mode and window_mode_cd for DISPLAY_XCB (lines 679-682) but only wayland_window_mode for DISPLAY_WAYLAND (lines 685-687). There is no Wayland equivalent of the current-desktop filter, which is expected (neither foreign-toplevel protocol exposes workspaces), but README.md:175-183 lists the Wayland feature gaps and does not mention `windowcd`.

**Failure scenario.** A user with `-modi windowcd` or `-show windowcd` in their config moves to a Wayland session; rofi reports the mode as unknown rather than explaining that the mode does not exist on Wayland.

**Proposed fix.** Add `windowcd` to the README.md:175-183 'Missing features in Wayland mode' list.

**Verifier.** source/rofi.c:677-689: the ENABLE_XCB block registers both window_mode and window_mode_cd (680-681) under `config.backend == DISPLAY_XCB`, the ENABLE_WAYLAND block registers only wayland_window_mode (686). README.md:175-183 lists the Wayland gaps (normal-window, -monitor -n, x-offset/y-offset, fake transparency, KWin window mode) and does not mention windowcd. Documentation gap only.

## `source/wayland/display.c:242` — size_t pool_size and size*i offsets are passed to wl_shm protocol calls that take int32_t

- **kind** memory · **severity** low · **verdict** PLAUSIBLE · **domain** source/wayland/display.c + include/wayland-inter

Line 205-206 compute `size = (size_t)stride * height; pool_size = size * wayland->buffer_count;` (buffer_count is 3, set at line 1734) with no overflow check. Line 242 `wl_shm_create_pool(wayland->shm, fd, pool_size)` takes an int32_t size, and line 246-247 `wl_shm_pool_create_buffer(wl_pool, size * i, width, height, stride, ...)` takes an int32_t offset. Both silently truncate. On a 32-bit build size_t is 32 bits and `size * 3` itself wraps, after which ftruncate/mmap succeed for a small region while the compositor is told about three full-size buffers inside it. Note also that `width` and `height` are multiplied by wayland->scale at lines 194-195 before stride is computed, so scale 2/3 multiplies the exposure by 4/9.

**Failure scenario.** 32-bit build, 4K output at scale 2 -> width=7680 height=4320, stride=30720, size=132710400, pool_size=398131200 (fits); at scale 3 on a wide multi-head layer surface size grows past 715827882 and pool_size wraps to a small positive number. ftruncate/mmap create a tiny mapping; wl_shm_pool_create_buffer then hands the compositor a buffer whose rows run off the end of the mapping -> compositor reads unmapped memory / client is killed with a wl_shm bad_fd or the client SIGBUSes writing into the cairo surface.

**Proposed fix.** Check the products before use: reject if `height <= 0`, if `size / stride != (size_t)height`, if `pool_size / size != buffer_count`, or if `pool_size > G_MAXINT32`; return NULL with a g_warning.

**Verifier.** display.c:205-206 compute size/pool_size as size_t with no bounds check, and 242 (`wl_shm_create_pool(..., pool_size)`) and 246-247 (`wl_shm_pool_create_buffer(wl_pool, size * i, ...)`) pass them to int32_t protocol parameters; 194-195 do multiply by wayland->scale first. The missing check is real, but on a 64-bit build reaching INT32_MAX needs >1.7 GB of buffers (4K@2x with buffer_count 3 is ~400 MB), so whether this is ever hit depends on runtime resolution/scale and a 32-bit build.

## `source/wayland/display.c:1236` — Every wl_seat.capabilities event creates a fresh wl_data_device and primary-selection device, overwriting (never destroying) the previous one

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 1236-1249 run unconditionally at the end of wayland_seat_capabilities(), with no `self->data_device == NULL` guard and no destroy of the old proxy:
  if (wayland->data_device_manager != NULL) { self->data_device = wl_data_device_manager_get_data_device(...); wl_data_device_add_listener(...); }
  if (wayland->primary_selection_device_manager != NULL) { self->primary_selection_device = ...get_device(...); ...add_listener(...); }
The orphaned proxies stay alive on the wire — the compositor keeps sending them data_offer/selection events, which are still dispatched to data_device_listener/primary_selection_device_listener.

**Failure scenario.** A device is added or removed on the seat, so capabilities fires a second time. Now two wl_data_devices are listening. The next clipboard change delivers two `selection` events; clipboard_handle_selection() (lines 1116-1128) destroys the previously stored offer and keeps the second, but the *first* event's offer is stored and then destroyed on the second event, and every DnD offer created by data_device_handle_data_offer (line 1092-1096) is duplicated. Each capabilities event permanently leaks one wl_data_device and one primary-selection device server-side.

**Proposed fix.** Guard both blocks with `if (self->data_device == NULL)` / `if (self->primary_selection_device == NULL)`, and destroy them in wayland_seat_release().

**Verifier.** display.c:1236-1249 create a wl_data_device and a zwp_primary_selection_device_v1 on every capabilities event with no `== NULL` guard and no destroy of the previous proxy (both are stored into self->data_device / self->primary_selection_device). Impact is bounded because capabilities usually fires once per seat.

## `source/wayland/display.c:1194` — wayland_seat_release() leaks the data devices and leaves a dangling entry in seats_by_name

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 1194-1207 destroy text_input, keyboard and pointer, call wl_seat_release(), remove from wayland->seats, and g_free(self). It never destroys self->data_device or self->primary_selection_device, and never removes self->name from wayland->seats_by_name — so seats_by_name (created with g_str_hash/g_str_equal and a g_free key-destroy at line 1740-1741) is left holding a key string owned by nobody and a *value* pointing at the just-freed wayland_seat. It also calls g_hash_table_remove(wayland->seats, self->seat) at line 1204 *after* wl_seat_release() has destroyed that proxy, and the sole caller (line 1608-1609) has already removed the entry via g_hash_table_iter_remove(), making line 1204 a redundant double-remove.

**Failure scenario.** A seat global is removed (libinput seat teardown, or a nested compositor restarting): wayland_registry_handle_global_remove -> g_hash_table_iter_remove at line 1608 -> wayland_seat_release at 1609 -> g_free(self) at 1206. wayland->seats_by_name still maps that name to the freed pointer; any future lookup or iteration over seats_by_name reads freed memory.

**Proposed fix.** Destroy data_device and primary_selection_device, g_hash_table_remove(wayland->seats_by_name, self->name) before g_free(self), and drop the redundant g_hash_table_remove at line 1204 (or drop the iter_remove at the call site and keep only one).

**Verifier.** display.c:1194-1207 destroys text_input, keyboard and pointer and frees self, but never wl_data_device_destroy(self->data_device) / zwp_primary_selection_device_v1_destroy(...) and never removes self->name from wayland->seats_by_name (inserted at 1260). Line 1204 does g_hash_table_remove(wayland->seats, self->seat) after wl_seat_release destroyed that proxy at 1202 — the freed pointer is only used as a g_direct_hash key, never dereferenced. The dangling seats_by_name value is latent: grep shows seats_by_name is only inserted (1260), removed (1257), created (1740) and unref'd (1922) — there is no lookup anywhere, so nothing reads the freed pointer today.

## `source/wayland/display.c:1916` — wayland_display_cleanup() leaks every seat, every output, all manager proxies and the cursor theme

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 1916-1928 are the entire teardown:
  nk_bindings_seat_free; g_hash_table_unref(seats_by_name); g_hash_table_unref(seats); g_hash_table_unref(outputs); wl_registry_destroy; wl_display_flush; g_water_wayland_source_free.
wayland->seats and wayland->outputs are created at lines 1738-1739 with g_hash_table_new(g_direct_hash, g_direct_equal) — NO value destroy function — so unref'ing them frees only the bucket array. Every wayland_seat struct (and its wl_seat, wl_keyboard, wl_pointer, wl_data_device, primary-selection device, text_input, and repeat GSource) and every wayland_output struct (and its wl_output proxy and g_strdup'd ->name from line 1435) leaks. Also never released: wayland->compositor, wayland->shm, wayland->layer_shell, wayland->kb_shortcuts_inhibit_manager, wayland->cursor_shape_manager, wayland->data_device_manager, wayland->primary_selection_device_manager, wayland->text_input_manager, wayland->cursor.theme / cursor.surface / cursor.frame_cb, wayland->frame_cb, and both wayland->clipboards[].offer. wayland_display_early_cleanup() (1907-1914) destroys the surfaces at line 1912 but leaves wayland->frame_cb (a wl_callback on the now-destroyed surface) allocated.

**Failure scenario.** `valgrind --leak-check=full sofi -show drun` reports definite leaks proportional to the number of seats and outputs on every run; under ASAN the test suite reports leaks and any CI leak gate fails. In a long-lived `-daemon`-style embedding (sofi.c reuses the process for several menus) the outputs/seats hash tables are re-created without the previous contents being freed.

**Proposed fix.** Create both tables with g_hash_table_new_full(..., NULL, (GDestroyNotify)wayland_seat_release / wayland_output_release) — after removing the self-removing g_hash_table_remove calls inside those functions — and add explicit destroys for the manager proxies, the cursor theme/surface, the frame callbacks and the clipboard offers.

**Verifier.** display.c:1916-1928 is the whole cleanup as quoted, and 1738-1739 create outputs/seats with g_hash_table_new(g_direct_hash, g_direct_equal) — no value destroy — so every wayland_seat/wayland_output struct and its proxies leak, as do compositor/shm/layer_shell/managers/cursor.theme and clipboards[].offer (never destroyed; only reassigned at 1130). early_cleanup (1907-1914) indeed leaves wayland->frame_cb. Downgraded: this is process-exit teardown in a short-lived process, so the practical impact is valgrind/ASAN noise; display_setup is not re-run in one process.

## `source/wayland/display.c:1092` — Data offers that never become a selection are never destroyed; g_malloc/g_realloc NULL checks are dead code

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

data_device_handle_data_offer() (lines 1092-1096) and primary_selection_device_handle_data_offer() (1155-1160) attach a listener to every incoming offer but store nothing. Only offers that arrive via `selection` reach clipboard_handle_selection() (1116-1128), which destroys the previous one. Every DnD offer (data_device_handle_enter at 1100-1105 ignores its `id` parameter entirely) is leaked. The stored clipboards[].offer is also never destroyed at teardown (wayland_display_cleanup, 1916-1928). Separately, lines 1023-1030, 1055-1060 and 1066-1072 check g_malloc/g_realloc for NULL — GLib's g_malloc/g_realloc abort on failure and never return NULL, so those branches are unreachable dead code (and g_try_malloc would be the API that actually needs them).

**Failure scenario.** A drag hovers over sofi's layer surface: data_device.data_offer fires, a wl_data_offer proxy is created with a listener, data_device.enter/leave fire and the offer is dropped on the floor — one leaked proxy per drag, client- and server-side, for the process lifetime.

**Proposed fix.** Destroy the offer in data_device_handle_leave/drop (and destroy an unclaimed offer if a new data_offer arrives before a selection), destroy clipboards[0..1].offer in wayland_display_cleanup, and either delete the impossible NULL checks or switch to g_try_malloc/g_try_realloc.

**Verifier.** display.c:1096-1100 and 1158-1163 add a listener to each incoming offer and store nothing; data_device_handle_enter (1102-1106) ignores its `id`; only offers arriving via selection reach clipboard_handle_selection (1119-1131), and clipboards[].offer is never destroyed in cleanup (1916-1928). The dead-code sub-claim also holds: g_realloc/g_malloc NULL checks at 1024-1030, 1055-1059 and 1067-1072 are unreachable since GLib's g_malloc/g_realloc abort on failure.

## `lexer/theme-parser.y:391` — Every @media block leaks its inner ThemeWidget tree

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The five @media productions (lines 384-453) merge the inner `t_entry_list` into the media widget with rofi_theme_parse_merge_widgets(), which deep-copies properties (source/theme.c:1428-1446), then free only `$4` (the media-type string) and `name`. The inner ThemeWidget itself ($9 at line 391, $10 at 405, $9 at 419, $14 at 433, $13 at 447) — allocated by the `t_entry_list: %empty` production at line 355 — is never rofi_theme_free()d. Compare t_main (line 326) which does free its list. The leak includes the widget array, every child ThemeWidget, and every property GHashTable in the block.

**Failure scenario.** Parsing themes/fullscreen-preview.rasi-style content, e.g. `@media (min-width: 1000px) { window { width: 50%; } listview { lines: 10; } }`, leaks one ThemeWidget per selector in the block plus their property tables on every parse; with several @media blocks and repeated -theme-str parses this is a steady per-invocation leak visible under valgrind/ASan.

**Proposed fix.** Add `rofi_theme_free($9);` (etc.) after the merge loop in each of the five @media productions.

**Verifier.** Read lexer/theme-parser.y:384-453: all five @media productions loop over $9/$10/$9/$14/$13 ->widgets calling rofi_theme_parse_merge_widgets, then free only $4 and `name`. The inner ThemeWidget is never rofi_theme_free()d, unlike t_main at line 326 which does free $1. rofi_theme_parse_merge_widgets (source/theme.c:1428-1446) deep-copies: it calls rofi_theme_widget_add_properties (source/theme.c:656-666) which g_hash_table_foreach's rofi_theme_copy_property_int into a NEW table rather than stealing, and copies media into a fresh slice at 1439-1440 — so the source tree is genuinely orphaned. Note the inner list's own top-level `properties` table is not even merged, only leaked. Downgraded to low: rofi is a short-lived process and the leak is bounded by theme size per parse, not unbounded.

## `lexer/theme-parser.y:388` — Duplicate @media queries overwrite ThemeMedia without freeing the old one

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

rofi_theme_find_or_create_name() (source/theme.c:83-99) returns an existing widget when the generated name `@media ( type: value )` already exists. The @media productions then unconditionally do `wid->media = g_slice_new0(ThemeMedia);` (lines 388, 402, 416, 430, 444), dropping the previous allocation. The same pattern exists in rofi_theme_parse_merge_widgets (source/theme.c:1438-1441), which is reached when a media block is merged into an existing widget that already carries media.

**Failure scenario.** A theme (or a theme plus an @import) containing two `@media (min-width: 800px) { ... }` blocks produces the identical widget name twice; the first ThemeMedia slice is leaked and rofi_theme_free() (source/theme.c:250-252) frees only the second.

**Proposed fix.** Reuse the existing ThemeMedia when wid->media != NULL, or g_slice_free the old one before replacing.

**Verifier.** rofi_theme_find_or_create_name (source/theme.c:84-99) returns the existing widget when a name matches, and lexer/theme-parser.y:388, 402, 416, 430, 444 each unconditionally do `wid->media = g_slice_new0(ThemeMedia);` on the returned widget with no check or free of a prior wid->media. Same unconditional overwrite at source/theme.c:1438-1441. rofi_theme_free (source/theme.c:250-252) frees only the surviving pointer. Real but a single 24-ish-byte slice per duplicate query.

## `lexer/theme-parser.y:155` — No %destructor declarations: every syntax error leaks all pending semantic values

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The grammar declares typed tokens carrying heap pointers — <sval> for T_STRING/T_PROP_NAME/T_NAME_ELEMENT/T_LINK/T_ELEMENT/T_MEDIA_TYPE (g_strdup'd in the lexer at lines 535-537, 550-556, 854, 889, 901), <property> Property*, <property_list> GHashTable*, <theme> ThemeWidget*, <distance_unit> RofiDistanceUnit* — but there is not a single %destructor in the file. When yyparse() aborts, bison discards the value stack without calling any cleanup, so everything already shifted/reduced is leaked. This compounds finding lexer-queue-leak-state because a syntax error is the normal outcome for a malformed user theme.

**Failure scenario.** `rofi -theme-str '* { font: "Sans 12"; color: #fff; bogus'` leaks the two Property structs, their names, the "Sans 12" string and the enclosing GHashTable; a script that repeatedly feeds bad themes to a long-lived rofi (or the theme-parser test suite) grows without bound.

**Proposed fix.** Add %destructor blocks for <sval> (g_free), <property> (rofi_theme_property_free), <property_list> (g_hash_table_destroy), <theme> (rofi_theme_free), <distance_unit> and <list>.

**Verifier.** `grep -c '%destructor' lexer/theme-parser.y` returns 0. The %union at lines 141-152 declares `char *`-bearing <sval>, `Property *`, `GHashTable *`, `ThemeWidget *`, `RofiDistanceUnit *`, and the %token block at 155-173 types T_STRING/T_MEDIA_TYPE/T_PROP_NAME/T_NAME_ELEMENT/T_LINK/T_ELEMENT as <sval> (lexer g_strdup/g_strcompress at theme-lexer.l:549-550, 554-556). No bison `error` recovery rule exists, so yyparse aborts and discards the value stack with no cleanup. Confirmed as written; leak is bounded per failed parse in a short-lived process.

## `source/helper.c:1550` — helper_get_theme_path leaks the expanded path and every intermediate candidate

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

Line 1534 computes `filename = rofi_expand_path(file)`. If the name already ends in a known extension, line 1550 overwrites it with a second rofi_expand_path(file) without freeing the first — an unconditional leak on that path. In the else branch, the loop at lines 1561-1569 assigns `filename = g_strconcat(temp, *i, NULL)` on each iteration without freeing the previous candidate, leaking one string per extension that failed. Additionally, when nothing is found the function returns the last *synthesised* candidate (line 1573), so the caller's error message names the wrong file.

**Failure scenario.** `@import "missing"` inside a theme: the loop builds "missing.rasi" (leaked), then "missing.rasinc", and the lexer (lexer/theme-lexer.l:463) reports `Failed to open theme: missing.rasinc` even though the user wrote `missing` and almost certainly meant missing.rasi. Every successful `@import "x.rasi"` leaks one expanded path.

**Proposed fix.** g_free(filename) before the re-assignment at line 1550 and at the top of each loop iteration; on total failure return the first candidate (or the original expanded name) so the diagnostic matches what the user wrote.

**Verifier.** source/helper.c:1534 sets `filename = rofi_expand_path(file)`; line 1550 (inside `if (ext_found)`) re-assigns `filename = rofi_expand_path(file);` with no intervening g_free — an unconditional leak of the first string. In the else branch, `char *temp = filename;` (1560) then the loop at 1561-1569 assigns `filename = g_strconcat(temp, *i, NULL)` each iteration without freeing the previous candidate; only the successful one is freed at 1565. Line 1573 `return filename;` returns the last synthesised candidate on total failure, so lexer/theme-lexer.l:463's `Failed to open theme: %s` names the last extension tried rather than what the user wrote. All three sub-claims verified.

## `source/helper.c:1550` — helper_get_theme_path() leaks the first rofi_expand_path() allocation on the ext_found path

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Line 1534 allocates `char *filename = rofi_expand_path(file);`. If the absolute-path early return at 1539 is not taken and the extension is already present, line 1550 does `filename = rofi_expand_path(file);` again, overwriting the pointer from 1534 with no g_free — that first buffer is unreachable for the rest of the process. helper_get_theme_path is called for every theme, every @import in a .rasi, and once per icon that falls through to source/rofi-icon-fetcher.c:690.

**Failure scenario.** `rofi -theme mytheme.rasi` (or any .rasi with @import lines): each call leaks one expanded-path string. In the icon-fetcher fallback path at rofi-icon-fetcher.c:689-690 it is called once per un-resolvable icon name from a worker thread, so a long filebrowser listing leaks proportionally to the number of entries. Visible under valgrind as 'definitely lost' from rofi_expand_path.

**Proposed fix.** Delete the redundant re-expansion at line 1550 (filename already holds the expanded path), or g_free(filename) before reassigning.

**Verifier.** source/helper.c:1534 `char *filename = rofi_expand_path(file);` and, when the absolute-path early return at :1536-1540 is not taken and ext_found is TRUE, :1550 reassigns `filename = rofi_expand_path(file);` with no g_free — the first allocation is unreachable. (The else branch at :1560-1570 also overwrites filename each loop iteration without freeing, an additional unclaimed leak.) Severity low: rofi is a short-lived process and each leak is one path string, though rofi-icon-fetcher.c:690 does call it per unresolved icon.

## `source/history.c:121` — getline()'s malloc'd buffer is released with g_free() in one reader and free() in the other

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Both readers allocate their line buffer through getline(), which uses malloc/realloc. __history_get_element_list_fields() releases it with `g_free(buffer)` (line 121) while __history_get_element_list() correctly uses `free(buffer)` (line 175). Mixing the two allocators is only benign as long as GLib's g_free is a thin wrapper over libc free — which is true for a default glibc build but is not part of GLib's contract (GLib has historically supported a pluggable GMemVTable, and on platforms where GLib links a different CRT than the caller the pair is genuinely mismatched).

**Failure scenario.** Build against a GLib configured with a non-default allocator, or run under a heap debugger that tracks allocator provenance: the first history_get_list() call on a non-empty ~/.cache/rofi3.druncache passes a malloc'd pointer to g_free and the allocator reports 'free of a pointer it does not own' / corrupts its metadata.

**Proposed fix.** Use free() at line 121 to match the getline() allocation, exactly as line 175 already does.

**Verifier.** source/history.c:141 and :121 — __history_get_element_list_fields fills the buffer via getline() and releases it with `g_free(buffer);` at line 121, while __history_get_element_list uses `free(buffer);` at line 175 for the same getline buffer. The mixing is genuine; harmless on a standard glibc/GLib pairing, hence low.

## `source/modes/dmenu.c:661` — pd->columns (a g_strsplit vector) is never freed

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`pd->columns = g_strsplit(columns, ",", 0);` at line 661. dmenu_mode_free (lines 481-505) frees cmd_list, urgent_list, active_list, selected_list and pd itself, but never g_strfreev(pd->columns).

**Failure scenario.** `sofi -dmenu -display-columns 1,3` leaks the split vector on every run. Small and once-per-process, but it is a real unreleased allocation flagged by any leak checker running the test suite.

**Proposed fix.** Add g_strfreev(pd->columns) to dmenu_mode_free.

**Verifier.** dmenu.c:659-663 `pd->columns = g_strsplit(columns, ",", 0);`. dmenu_mode_free (dmenu.c:481-505) frees cmd_list entries, cmd_list, urgent_list, active_list, selected_list and pd — there is no g_strfreev(pd->columns) anywhere in the file. Once-per-process leak.

## `source/modes/dmenu.c:777` — dmenu_finish frees pending Blocks without freeing the entry strings inside them

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

The drain loop is `while ((block = g_async_queue_try_pop_unlocked(pd->async_queue)) != NULL) { g_free(block); }` (lines 777-779). Each Block holds up to BLOCK_LINES_SIZE DmenuScriptEntry values whose entry/display/meta/info/icon_name were allocated by read_add_block via rofi_force_utf8 and dmenuscript_parse_entry_extras.

**Failure scenario.** Cancel `sofi -dmenu` while a large input is still streaming (e.g. `find / | sofi -dmenu`, Esc after a second): up to 2048 strings per still-queued block are leaked at teardown.

**Proposed fix.** Free each block's values[0..length-1] strings (the same fields dmenu_mode_free releases) before g_free(block).

**Verifier.** dmenu_finish drain loop at dmenu.c:775-779 `while ((block = g_async_queue_try_pop_unlocked(pd->async_queue)) != NULL) { g_free(block); }` — the Block's up-to-2048 DmenuScriptEntry values hold entry (rofi_force_utf8, dmenu.c:154) plus display/meta/info/icon_name from dmenuscript_parse_entry_extras, none of which are freed. Teardown-only, so it does not accumulate during a run.

## `source/modes/drun.c:1088` — cache entry-count size check can be bypassed by size_t overflow on 32-bit

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`gsize newsize = sizeof(DRunModeEntry) * pd->cmd_list_length;` where cmd_list_length is an unsigned int read straight from the cache file (line 1078). On 32-bit targets gsize is 32 bits, so the product wraps. The guard on line 1089 compares against the *constant* DRUN_MAX_NUM_ENTRIES * sizeof(DRunModeEntry) (~35 MB, no overflow) and therefore passes for a wrapped small newsize. g_malloc0(newsize) then allocates the small block while the loop at line 1098 writes cmd_list_length full DRunModeEntry structs into it.

**Failure scenario.** On i386/armhf with `drun-use-desktop-cache: true`, a cache whose count field is 0x03000000 gives newsize = 0x03000000 * 88 mod 2^32, a value far below the 35 MB cap. g_malloc0 returns a small buffer and drun_read_cache writes ~50 million entries into it -> unbounded heap overflow.

**Proposed fix.** Bound the count before multiplying: `if (pd->cmd_list_length > DRUN_MAX_NUM_ENTRIES) { ... return TRUE; }`, then compute newsize.

**Verifier.** drun.c:1078 reads pd->cmd_list_length straight from the file; it is `unsigned int` (drun.c:218). drun.c:1088 `gsize newsize = sizeof(DRunModeEntry) * pd->cmd_list_length;` wraps on a 32-bit gsize, while the guard at drun.c:1089 compares against the constant DRUN_MAX_NUM_ENTRIES(262144) * sizeof(DRunModeEntry), which does not overflow, so a wrapped small newsize passes. g_malloc0(newsize) at 1091 then feeds the loop at 1098 that writes cmd_list_length structs. Only affects 32-bit builds, needs drun-use-desktop-cache (default FALSE, config.c:170) plus a corrupted/hostile cache file.

## `source/modes/drun.c:336` — launch_link_entry alloca()s a buffer sized by the URL field of a desktop file

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`gsize command_len = strlen(config.drun_url_launcher) + strlen(url) + 2; gchar *command = g_newa(gchar, command_len);` — g_newa is alloca. `url` comes from g_key_file_get_string(e->key_file, e->action, "URL", NULL) at line 327, i.e. straight out of a .desktop file with no length bound (DRUN_MAX_STRING_LENGTH is 10 MB and is not applied here at all). ssh.c:110 has the same pattern with the hostname.

**Failure scenario.** A .desktop file of Type=Link whose URL field is a few megabytes long: the alloca moves the stack pointer past the guard page into unrelated mappings, then g_snprintf writes through it — classic stack-clash. Desktop files arrive from packages, downloads and ~/.local/share/applications, so the input is not trusted.

**Proposed fix.** Use g_strdup_printf / g_strconcat and g_free, as the rest of the file does, instead of alloca.

**Verifier.** drun.c:333-337 `gsize command_len = strlen(config.drun_url_launcher) + strlen(url) + 2; gchar *command = g_newa(gchar, command_len); g_snprintf(command, command_len, ...)`, with url from g_key_file_get_string(e->key_file, e->action, "URL", NULL) at drun.c:327 and no length bound applied (DRUN_MAX_STRING_LENGTH only guards the cache reader). ssh.c:109-112 has the same g_newa pattern on the hostname. Real stack-clash shape, but a .desktop file the user can install already grants arbitrary Exec, so practical exploitability is near zero.

## `source/modes/drun.c:361` — exec_cmd_entry leaks the GRegex and the expanded command string on its error returns

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

Three early returns skip cleanup: line 361-366 returns on a g_regex_replace_eval error without g_regex_unref(reg); line 368-372 returns when earg.success == FALSE without g_free(str); line 386-393 returns when the keyfile fails to load without g_free(str) (exec_path and wmclass are not yet allocated there, but str is).

**Failure scenario.** A desktop file with an unrecognized field code (e.g. `Exec=foo %z`) sets earg.success = FALSE; every attempt to launch it leaks the expanded command string, and repeated attempts within one session accumulate. The regex leak at line 365 additionally leaks a compiled PCRE object.

**Proposed fix.** Restructure with a single cleanup label (or move g_regex_unref immediately after g_regex_replace_eval and g_free(str) into each return path).

**Verifier.** exec_cmd_entry (drun.c:349): the g_regex_replace_eval error return at drun.c:361-366 happens before `g_regex_unref(reg)` at line 367, leaking the compiled GRegex. The `if (earg.success == FALSE)` return at drun.c:368-372 and the `if (str == NULL)` return at 373-376 come after str was allocated by g_regex_replace_eval and never g_free it. The keyfile-load failure return at drun.c:386-393 also skips `g_free(str)` (which only happens at line 479).

## `source/modes/drun.c:503` — desktop-id buffer is a VLA sized by the desktop file's path length

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`const ssize_t id_len = strlen(path) - strlen(root); char id[id_len];` with only a comment ("We know strlen(path) > strlen(root)+1") standing in for a check. The size is attacker-influenced (deep nesting under ~/.local/share/applications) and a zero/negative id_len is undefined behaviour.

**Failure scenario.** A path equal in length to root (which the current walk_dir call graph happens to prevent, but nothing enforces) yields a zero-length VLA and g_strlcpy with size 0. A very deep directory tree yields a multi-kilobyte stack allocation per recursion level, compounding the walk_dir recursion issue.

**Proposed fix.** Assert id_len > 1 and use a heap allocation (g_strdup + in-place '/'->'-' substitution), which also removes the VLA.

**Verifier.** drun.c:502-504 `// We know strlen (path ) > strlen(root)+1` / `const ssize_t id_len = strlen(path) - strlen(root); char id[id_len];` followed by g_strlcpy(id, &path[strlen(root)+1], id_len). The comment is the only check; the size is a VLA driven by path depth. Currently the walk_dir call graph (drun.c:830, 858) always passes a path strictly under root, so no zero-length VLA occurs today.

## `source/modes/drun.c:956` — drun_read_string's l++ can wrap, defeating the DRUN_MAX_STRING_LENGTH bound

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`if (l > 0) { l++; if (l > DRUN_MAX_STRING_LENGTH) return TRUE; (*str) = g_malloc(l); ... }` — for l == SIZE_MAX the increment wraps to 0, the bound check passes, g_malloc(0) returns NULL and fread(NULL, 1, 0, fd) returns 0 == l, so the function reports success with *str == NULL.

**Failure scenario.** A cache whose length field is 0xFFFFFFFFFFFFFFFF yields entry->exec == NULL that the code believes was read successfully. Selecting that row reaches g_regex_replace_eval(reg, NULL, ...) at drun.c:359, which trips a glib assertion and then hits the `str == NULL` warning path — a corrupt-input path that reports "Nothing to execute" rather than "cache corrupt".

**Proposed fix.** Check the bound before incrementing: `if (l >= DRUN_MAX_STRING_LENGTH) return TRUE; l++;`

**Verifier.** drun.c:953-965: `if (l > 0) { l++; if (l > DRUN_MAX_STRING_LENGTH) return TRUE; (*str) = g_malloc(l); if (fread((*str), 1, l, fd) != l) ... }`. For l == SIZE_MAX the increment wraps to 0, the bound check passes, g_malloc(0) returns NULL under GLib, fread(NULL,1,0,fd) returns 0 == l, and the function returns FALSE (success) with *str == NULL. Downstream that yields e.g. entry->exec == NULL reaching g_regex_replace_eval(reg, NULL, ...) at drun.c:359. Needs the opt-in cache plus a crafted length field.

## `source/modes/recursivebrowser.c:194` — scan_dir leaks the popped GFile on the already-visited early continue

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

The loop pops a GFile into dir_to_scan (line 191) and unrefs it at line 200, but the already-visited branch at lines 194-197 does `g_free(cdir); continue;` without ever unreffing dir_to_scan. Every duplicate directory pushed into dirs_to_scan therefore leaks one GFile object.

**Failure scenario.** Any tree reachable by more than one path (a directory symlinked from several places, or the same dir queued twice via the symlink-resolution push at line 297) leaks a GFile per duplicate. On a large home directory this is thousands of leaked objects per scan, and the leak persists for the process lifetime because the queue and the objects are never reclaimed.

**Proposed fix.** Move `g_object_unref(dir_to_scan);` above the visited check, or add it inside the early-continue branch.

**Verifier.** recursivebrowser.c:191-200: `dir_to_scan = g_queue_pop_head(...)` then `if (g_hash_table_lookup_extended(dirs_scanned, cdir, NULL, NULL)) { g_free(cdir); continue; }` — the early continue skips the `g_object_unref(dir_to_scan)` that appears at line 200 on the normal path. Every duplicate directory enqueued (including via the symlink push at line 297) leaks one GFile.

## `source/modes/run.c:197` — buffer[strlen(buffer) - 1] underflows when a run-list-command line starts with NUL

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

get_apps_external loops `while (getline(&buffer, &buffer_length, inp) > 0)` and immediately does `if (buffer[strlen(buffer) - 1] == '\n') buffer[strlen(buffer) - 1] = '\0';`. getline returns the byte count including embedded NULs, so a line consisting of a leading '\0' gives strlen(buffer) == 0 and the expression becomes buffer[(size_t)-1] — a read one byte *before* the heap allocation, and a one-byte write there if that byte happens to be 0x0a. The sibling loops in script.c:258-263 and dmenu.c read_input_sync use the getdelim return length instead of strlen and are safe.

**Failure scenario.** Set `run-list-command` to a script that emits a NUL as the first byte of a line (e.g. `printf '\0foo\n'`). strlen returns 0, buffer[-1] is read; if the malloc header byte immediately preceding the buffer is 0x0a it is overwritten with 0 -> glibc heap metadata corruption on the next free/realloc.

**Proposed fix.** Capture the getline return value (`ssize_t n = getline(...)`) and strip with `if (n > 0 && buffer[n-1] == '\n') buffer[n-1] = '\0';`.

**Verifier.** run.c:194-199: `while (getline(&buffer, &buffer_length, inp) > 0) { ... if (buffer[strlen(buffer) - 1] == '\n') buffer[strlen(buffer) - 1] = '\0'; }`. getline's return counts embedded NULs, so a line beginning with '\0' gives strlen==0 and buffer[(size_t)-1]. Contrast script.c:259-262 and dmenu.c:262-265 which use the getdelim return length. Trigger requires a user-configured run-list-command emitting NUL bytes, so it is not an attacker-reachable path.

## `source/modes/run.c:268` — get_apps frees the history string array but not the strings in it

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

Same defect as ssh.c:538. `char **hretv = history_get_list(path, length);` (line 256), the elements are re-split with g_strsplit into retv[i].entry/exec (lines 259-266), and then line 268 does `g_free(hretv)` — freeing only the pointer array and leaking every g_strndup'd element.

**Failure scenario.** Every `sofi -show run` leaks one string per run-history entry. run_mode_result's delete path (lines 510-511) calls run_mode_destroy + run_mode_init, repeating the leak on each entry deletion within one session.

**Proposed fix.** Use g_strfreev(hretv).

**Verifier.** run.c:256 `char **hretv = history_get_list(path, length);`, the elements are re-split at run.c:259-267 (retv[i].entry/exec take ownership of the g_strsplit pieces, g_free(rs) frees only that vector), then run.c:268 `g_free(hretv);` frees only the outer array, leaking every g_strndup'd history element from history.c:110. run_mode_destroy+run_mode_init at run.c:510-511 repeats it per entry deletion.

## `source/modes/run.c:379` — get_apps leaks the duplicated PATH string on the zero-results early return

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`path = g_strdup(g_getenv("PATH"));` at line 273 is freed at line 387, but the early return at lines 379-381 (`if ((*length) == 0) return retv;`) skips that free.

**Failure scenario.** Run mode with an empty history and a PATH containing no executable directories (or only unreadable ones) returns early and leaks the PATH copy. Small, but a genuine unconditional leak on that path.

**Proposed fix.** Move g_free(path) above the `if ((*length) == 0)` check.

**Verifier.** run.c:273 `path = g_strdup(g_getenv("PATH"));` is released at run.c:387, but the early return at run.c:379-381 `if ((*length) == 0) { return retv; }` returns before it. Unconditional leak on that path (empty history + no executables found).

## `source/modes/script.c:126` — dmenuscript_parse_entry_extras leaks the trailing unpaired token

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

The loop consumes key/value pairs and NULLs out both slots as it goes (lines 94-125). The cleanup is `if (*extras != NULL) g_free(*extras);` (lines 127-129) — but *extras is extras[0], which the loop already set to NULL on its first iteration. The genuinely unpaired token sits at the position `extra` stopped at, never at index 0. So the free only fires in the one case where no pair was parsed at all; whenever at least one pair was parsed, the dangling token leaks. This runs once per row for both script mode (script.c:297) and dmenu (dmenu.c:152 and 187).

**Failure scenario.** A script emits rows like `text\0icon\x1ffirefox\x1fdangling`. Each such row leaks the "dangling" string. Feed 100k rows through `sofi -dmenu` and the leak is proportional to the input size, and none of it is reclaimed until process exit.

**Proposed fix.** Free through `extra` rather than `extras`: after the loop, `for (; *extra != NULL; extra++) g_free(*extra);` before g_free(extras) — or just use g_strfreev semantics by not NULLing entries and instead tracking ownership.

**Verifier.** script.c:92-124: the loop condition is `*extra != NULL && *(extra+1) != NULL`, and inside it both slots are set to NULL before being consumed/freed. The cleanup at script.c:126-129 is `if (*extras != NULL) g_free(*extras);` — that is extras[0], which the loop already NULLed on its first iteration. The genuine unpaired token sits at the position `extra` stopped at, and g_free(extras) at line 130 only frees the pointer vector. Called per row from script.c:297 and dmenu.c:152/187.

## `source/modes/script.c:164` — sw->display_name aliases pd->prompt, which script_mode_destroy frees

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

parse_header_entry does `pd->prompt = g_strdup(value); sw->display_name = pd->prompt;` (lines 162-164). script_mode_destroy g_frees pd->prompt at line 458 but leaves sw->display_name pointing at the freed block. script_switcher_free (lines 319-326) then frees sw without touching it. mode_get_display_name (source/mode.c:199-200) returns that field.

**Failure scenario.** Any read of the mode's display name between mode_destroy and mode_free — e.g. a future teardown path that logs the mode name, or a mode re-init that reads the stale display_name before overwriting it — dereferences freed memory.

**Proposed fix.** Set sw->display_name = NULL in script_mode_destroy before freeing pd->prompt.

**Verifier.** script.c:162-164 `pd->prompt = g_strdup(value); sw->display_name = pd->prompt;`. script_mode_destroy (script.c:452-464) does g_free(rmpd->prompt) at line 458 and clears sw->private_data at 462 but never clears sw->display_name, and script_switcher_free (script.c:319-326) frees sw->name/sw->ed/sw without touching it. mode.c:199-200 returns mode->display_name. No current caller reads it between destroy and free, so this is latent.

## `source/modes/ssh.c:538` — get_ssh frees the history string array but not the strings in it

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`char **h = history_get_list(path, length);` (line 503). history_get_list -> __history_get_element_list_fields (source/history.c:80-124) g_strndup()s every element into a NULL-terminated vector. get_ssh copies out of it with g_strsplit (line 508) and then does plain `g_free(h)` at line 538, leaking every element string. drun.c:898 gets this right with g_strfreev(retv).

**Failure scenario.** Every `sofi -show ssh` leaks one string per ssh history entry (default history size 25, configurable higher). With a large history and repeated mode re-entry via ssh_mode_result's destroy/init cycle at lines 641-642, the leak repeats on each MENU_ENTRY_DELETE.

**Proposed fix.** Use g_strfreev(h) instead of g_free(h).

**Verifier.** ssh.c:503 `char **h = history_get_list(path, length);` — history.c:80-124 __history_get_element_list_fields g_strndup's each element into the vector. ssh.c:508-536 re-splits h[i] with g_strsplit and g_strdup's the pieces into retv, then g_strfreev(ssplit); ssh.c:538 is a plain `g_free(h)`, so every g_strndup'd element leaks. drun.c uses g_strfreev for the same pattern.

## `source/modes/wayland-window.c:507` — pd->ext_toplevels list and pd->list protocol object are never freed on destroy

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wayland_window_private_free (lines 507-530) tears down pd->wlr_toplevels, pd->registry, pd->manager and pd->window_regex, but never touches pd->ext_toplevels (declared line 68, populated at line 276) or pd->list (declared line 66, bound at line 424). ext_foreign_toplevel_handle_free exists (line 116) and is only ever called from the `closed` event handler (line 218). The ext_foreign_toplevel_list_v1 proxy is only destroyed if the compositor happens to send `finished` (line 282), which it normally does not.

**Failure scenario.** Run on any compositor that implements ext-foreign-toplevel-list-v1 (sway 1.9+, Hyprland, KWin 6, Mutter 46+) with `-show window`. Every open window allocates an ExtForeignToplevelHandle plus g_strdup'd app_id and identifier (lines 245, 228) and a wl_proxy; on exit wayland_window_mode_destroy frees none of them. Under valgrind/ASan this is a definitely-lost block per window plus one leaked ext_foreign_toplevel_list_v1 proxy, and in a long-lived embedding of the mode the leak grows with every window ever seen.

**Proposed fix.** In wayland_window_private_free, g_list_foreach(pd->ext_toplevels, ...ext_foreign_toplevel_handle_free...) + g_list_free, and ext_foreign_toplevel_list_v1_destroy(pd->list) if non-NULL (after the stop/roundtrip reorder described in private-free-teardown-order).

**Verifier.** wayland_window_private_free (source/modes/wayland-window.c:507-530) touches only wlr_toplevels, registry, manager and window_regex. pd->ext_toplevels (declared 68, populated by g_list_prepend at 276) and pd->list (declared 66) are never freed or destroyed; ext_foreign_toplevel_handle_free (116-125) is called only from the `closed` event handler, and ext_foreign_toplevel_list_v1_destroy only from the `finished` handler at 282. Confirmed leak of one ExtForeignToplevelHandle (plus g_strdup'd app_id/identifier and a wl_proxy) per window plus the list proxy. It is an at-exit leak in a short-lived process, so low.

## `source/rofi-icon-fetcher.c:340` — rofi_icon_fetcher_destroy() frees the global state but does not NULL it

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/rofi-icon-fetcher.c:325-341 ends with `g_free(rofi_icon_fetcher_data);` and never assigns NULL, even though the very first line of the function (line 326) tests `if (rofi_icon_fetcher_data == NULL) return;` and rofi_icon_fetcher_init() opens with `g_assert(rofi_icon_fetcher_data == NULL);` (line 274). Every accessor then dereferences the global unconditionally: rofi_icon_fetcher_file_is_image() (line 453), rofi_icon_fetcher_query() (line 802), rofi_icon_fetcher_get() (line 848). rofi_icon_fetcher_destroy() is called from cleanup() at source/rofi.c:588 while pending worker jobs may still be queued in tpool.

**Failure scenario.** Any icon worker still running (or any file_is_image() call from a mode) after cleanup() reaches source/rofi.c:588 dereferences the freed IconFetcher -> use-after-free; a second init/destroy cycle trips the g_assert at line 274 instead of re-initialising.

**Proposed fix.** Set `rofi_icon_fetcher_data = NULL;` after the g_free at source/rofi-icon-fetcher.c:340, and add NULL guards to file_is_image/query/get.

**Verifier.** source/rofi-icon-fetcher.c:325-341 ends at line 340 with `g_free(rofi_icon_fetcher_data);` and no NULL assignment, while line 326 guards on `rofi_icon_fetcher_data == NULL` and rofi_icon_fetcher_init opens with `g_assert(rofi_icon_fetcher_data == NULL)` at line 274 — so the guard and the assert are both defeated by the missing reset. Accessors dereference it unconditionally: rofi_icon_fetcher_file_is_image (line 453), rofi_icon_fetcher_query (802), rofi_icon_fetcher_get (847-848). It is called from cleanup() at source/rofi.c:588 while tpool workers can still be in flight. Real, but confined to process teardown.

## `source/rofi-icon-fetcher.c:412` — cairo_image_surface_create() result is used without checking status; NULL pixel pointer is written through

- **kind** memory · **severity** low · **verdict** PLAUSIBLE · **domain** helper / history / icon-fetcher / display / test

`surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height); cpixels = cairo_image_surface_get_data(surface);` (lines 411-412) with no cairo_surface_status() check. cairo returns an error surface (CAIRO_STATUS_INVALID_SIZE / NO_MEMORY) for dimensions it cannot handle, and cairo_image_surface_get_data() returns NULL for an error surface. The copy loop at lines 416-436 then does `cline[RED_BYTE] = ...` on a NULL base pointer. The same missing check applies to the markup path at lines 652-654, where sentry->wsize/hsize can legitimately be <= 0 (the code at 715-718 guards `if (width > 0)`, so non-positive sizes are expected), producing an error surface that is handed to cairo_create() and pango.

**Failure scenario.** Point rofi at an image whose scaled dimensions cairo rejects (or run under memory pressure): gdk_pixbuf_new_from_file_at_scale succeeds, cairo_image_surface_create returns an error surface, cairo_image_surface_get_data returns NULL, and the first iteration of the loop at line 425 writes to address 0x2 — SIGSEGV on the worker thread.

**Proposed fix.** After line 411 check `if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(surface); return NULL; }` and likewise guard the markup path at 652 with a `wsize > 0 && hsize > 0` precondition.

**Verifier.** source/rofi-icon-fetcher.c:411-412 do call cairo_image_surface_create() then cairo_image_surface_get_data() with no cairo_surface_status() check, and the copy loop at :416-436 dereferences cpixels unconditionally. Reachability is real but runtime-dependent: width/height come from the pixbuf, and source/theme.c:1088-1108 shows wsize/hsize can be -1 (ROFI_SCALE_NONE), in which case source/rofi-icon-fetcher.c:714-722 loads the image at full size, so a huge theme background-image could produce a size cairo rejects. The markup sub-claim (:652-654) is weaker: cairo swallows operations on an error surface/context, and the '<span' path is only reached from rofi_icon_fetcher_query with a real positive size.

## `source/rofi-icon-fetcher.c:635` — content_type leaks when g_content_type_get_mime_type() returns NULL

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Lines 609-636: `char *content_type = g_content_type_guess(...); char *mime_type = g_content_type_get_mime_type(content_type); if (mime_type) { ... g_free(mime_type); g_free(content_type); }`. Both frees sit inside the `if (mime_type)` block, so when g_content_type_get_mime_type returns NULL the string from g_content_type_guess is never released.

**Failure scenario.** Browse a directory of files whose content type has no registered mime mapping (g_content_type_get_mime_type returns NULL). Each such entry leaks one content-type string from a worker thread; a large recursive browse leaks once per file, growing unboundedly for the session.

**Proposed fix.** Move `g_free(content_type);` out of the `if (mime_type)` block so it runs on both paths.

**Verifier.** source/rofi-icon-fetcher.c:609-636: content_type is allocated at :609, mime_type at :610, and both `g_free(mime_type);` (:634) and `g_free(content_type);` (:635) sit inside the `if (mime_type)` block opened at :612 and closed at :636 — when g_content_type_get_mime_type returns NULL the content_type string is never freed.

## `source/rofi.c:209` — g_string_free(str, FALSE) leaks the message buffer on mode-init failure

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

run_mode_index() builds a GString error message, passes `str->str` to rofi_view_error_dialog() (which copies it into a textbox at source/view.c:1954-1957), then calls `g_string_free(str, FALSE)`. The FALSE means "do not free the character data" and the returned char* is discarded, so the whole message buffer leaks. Every other site in this file uses TRUE (e.g. source/rofi.c:814) or hands ownership to rofi_add_error_message().

**Failure scenario.** Run a mode whose _init returns FALSE (e.g. a script mode whose script is unreadable, or the window mode with no WM). The GString's heap buffer is never freed for the remaining lifetime of the process.

**Proposed fix.** `g_string_free(str, TRUE);`

**Verifier.** source/rofi.c:203-210: the GString is built, `str->str` passed to rofi_view_error_dialog (which copies it into a textbox at source/view.c:1954-1957), then `g_string_free(str, FALSE);` at :209 — FALSE keeps the character buffer alive and the returned char* is discarded, so it leaks. Neighbouring sites use TRUE (:551 area, :814). Severity low, not medium: it is one short message leaked, and this path immediately leads to process exit.

## `source/rofi.c:881` — `-e -` NUL terminator can be written one byte past the buffer on the size-limit break

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

The read loop keeps the invariant capacity == length and index == length - 1024 by growing after each read. But the `if (length >= ROFI_MAX_DMENU_INPUT) break;` at :875-877 fires *before* the g_realloc at :878, so on that path index has already been advanced by the bytes just read while capacity is still the old length. If that final fread returned a full 1024 bytes, index == capacity and `msg[index] = 0` at :881 writes one byte past the allocation.

**Failure scenario.** `rofi -e - < a-4GiB-file` on a build where the accumulated length reaches UINT32_MAX exactly on a full 1024-byte read: one-byte heap overflow writing 0. Requires ~4GiB of stdin, so it is an edge case, but it is a genuine out-of-bounds write.

**Proposed fix.** Move the limit check after the realloc, or realloc one extra byte for the terminator (`g_realloc(msg, length + 1)`).

**Verifier.** source/rofi.c:868-881. The loop keeps capacity==length and index==length-1024 because the g_realloc at :878 runs after `index += i; length += i;`. The `if (length >= ROFI_MAX_DMENU_INPUT) break;` at :875-877 fires before that realloc, so on that path index has already advanced while capacity is still the pre-read length; if that final fread returned a full 1024 bytes then index==capacity and `msg[index] = 0;` at :881 writes one byte past the allocation. ROFI_MAX_DMENU_INPUT is UINT32_MAX (source/rofi.c:80), so it needs ~4GiB on stdin — a genuine one-byte heap overflow, but an extreme edge case.

## `source/rofi.c:729` — Plugin modes are dlclose'd without ever running their destructor

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

rofi_collectmodes_destroy() sets `available_modes[i] = NULL;` *before* the `if (available_modes[i]) mode_free(&(available_modes[i]));` at :732-734, so for any mode that came from a GModule the mode_free path is unconditionally skipped and g_module_close() runs instead. cleanup() calls mode_destroy() only for the *enabled* modes (:559-561), so a plugin that was discovered but never enabled gets neither `_destroy` nor `free` before its .so is unloaded.

**Failure scenario.** Install a plugin whose Mode allocates in a constructor or whose `free` callback releases resources, and run rofi without enabling it: the plugin's allocations are leaked and any cleanup it needed (temp files, sockets) never runs. Under valgrind the leak is also unattributable because the .so's symbols are already unloaded.

**Proposed fix.** Call mode_free()/mode_destroy() on the mode *before* closing its module, then NULL the slot.

**Verifier.** source/rofi.c:725-737: inside the loop, `if (mode_plugin_get_module(available_modes[i])) { GModule *mod = ...; available_modes[i] = NULL; g_module_close(mod); }` at :727-731, and only then `if (available_modes[i]) { mode_free(&(available_modes[i])); }` at :732-734 — always false for plugin modes. mode_free (source/mode.c:167-174) is what invokes the mode's `free` callback, so a plugin's free callback never runs; cleanup() calls mode_destroy only for the enabled modes (source/rofi.c:559-561). Low: leak at process exit only.

## `source/rofi.c:1250` — -list-keybindings returns without cleanup(), unlike every other early exit

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`abe_list_all_bindings(is_term); return EXIT_SUCCESS;` — the six sibling early-exit paths (-rasi-validate :983, -dump-theme :1232, -dump-processed-theme :1238, -dump-config :1243, -h :1271, -info :1276) all call cleanup() first. By this point setup_abe() (:1110) has g_strdup'd every default binding string (source/keyb.c:419), the theme has been parsed, and the modes have been collected.

**Failure scenario.** `rofi -list-keybindings` exits leaking the entire parsed theme, the mode list and ~80 binding strings. Not user-visible, but it makes the binary useless as a leak-check baseline and is an inconsistency a reader will trip over.

**Proposed fix.** Insert `cleanup();` before the return.

**Verifier.** source/rofi.c:1246-1251: `int is_term = isatty(fileno(stdout)); abe_list_all_bindings(is_term); return EXIT_SUCCESS;` with no cleanup(). Verified by grep that every sibling early exit does call it (cleanup() at :983, :1049, :1092, :1201, :1232, :1238, :1243, :1271, :1276, ...). setup_abe (source/keyb.c:411-423) has already g_strdup'd every binding by then. Exit-time leak only.

## `source/rofi.c:1193` — Cache-directory creation failure returns EXIT_FAILURE without cleanup()

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`if (g_mkdir_with_parents(cache_dir, 0700) < 0) { g_warning(...); return EXIT_FAILURE; }` is the only failure path in main() that skips cleanup(); the neighbouring setlocale failure at :1090-1094 and every later failure (:1202, :1298, :1315, :1323) call it. The error message also omits the path that failed, printing only strerror.

**Failure scenario.** Set `cache-dir: "/proc/nope";` in config.rasi: rofi exits with "Failed to create cache directory: Permission denied" without saying which directory, and without releasing the theme, bindings, modes or display.

**Proposed fix.** Call cleanup() before returning, and include `cache_dir` in the g_warning.

**Verifier.** source/rofi.c:1191-1194: `if (g_mkdir_with_parents(cache_dir, 0700) < 0) { g_warning("Failed to create cache directory: %s", g_strerror(errno)); return EXIT_FAILURE; }` — no cleanup(), and the message omits cache_dir. The setlocale failure at :1090-1093 and every later failure path (:1201, :1297, :1314, :1322) call cleanup(). Exit-time leak plus a poor error message.

## `source/view.c:1844` — Entry history is loaded before it can be disabled, then never freed

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

input_history_initialize() runs from __create_window (source/xcb/view.c:608, source/wayland/view.c:325) and allocates the EntryHistoryIndex array plus a g_strdup per line (source/view.c:521-546). CacheState.entry_history_enable is only cleared later, inside rofi_view_create(), for password mode (:1843-1846) and for config.disable_history (:1847-1850). input_history_save() bails out at :549 whenever the flag is FALSE, and that early return is the only path that frees the array (:578-586). So the disable path leaks everything that was already read from disk.

**Failure scenario.** `rofi -show run -disable-history` (or any password-mode dialog) with an existing ~/.cache/rofi-entry-history.txt: the whole history array plus one g_strdup per stored line is allocated and never freed. Also disabling history no longer prevents the file from being read into memory in the first place, which is the point of password mode.

**Proposed fix.** Evaluate `config.disable_history` / MENU_PASSWORD before calling input_history_initialize(), or make the flag check in input_history_save() run the cleanup block unconditionally and only skip the file write.

**Verifier.** input_history_initialize (source/view.c:505-508) only checks entry_history_enable at entry, and it runs from __create_window (source/xcb/view.c:608, source/wayland/view.c:325) which happens in startup() before rofi_view_create. entry_history_enable is cleared later at source/view.c:1843-1846 (password) and :1847-1850 (config.disable_history). input_history_save returns at :549-551 when the flag is FALSE, and the only free of the array + per-line g_strdup is at :578-586, after that early return. So everything already read from ~/.cache/rofi-entry-history.txt is leaked, and password mode does read the file into memory. Severity low: leak is bounded by the history file and lasts only until process exit.

## `source/widgets/listview.c:799` — Unbounded stack VLA sized from the user-controlled `-eh` option

- **kind** memory · **severity** low · **verdict** PLAUSIBLE · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/listview.c:798-806:
```
  if (lv->eh > 1 && row.textbox) {
    char buff[lv->eh * 2 + 1];
    memset(buff, '\0', lv->eh * 2 + 1);
```
`lv->eh` is `unsigned int` (struct field listview.c:101) and comes straight from `config.element_height` at source/view.c:1740. That option is registered as a *signed* number `xrm_SNumber ... "eh" {.snum = &config.element_height}` (source/xrmoptions.c:404-411), so a negative or very large value is accepted and reinterpreted as a huge unsigned. No validation exists anywhere between the option parser and the VLA.

**Failure scenario.** `rofi -show run -eh -1` => lv->eh == 4294967295 => `char buff[8589934591]` allocated on the stack => immediate stack-pointer overflow / SIGSEGV before the menu ever appears. `-eh 10000000` allocates a 20MB VLA and blows the default 8MB stack the same way.

**Proposed fix.** Clamp at the source (reject <1 for config.element_height) and replace the VLA with a heap allocation plus a sanity bound, e.g. `guint eh = CLAMP(lv->eh, 1u, 64u); gchar *buff = g_strnfill(...)` in listview_create (source/widgets/listview.c:798-807).

**Verifier.** The VLA is real: source/widgets/listview.c:799-800 `char buff[lv->eh * 2 + 1]; memset(...)`, lv->eh is `unsigned int` (listview.c:101), assigned at listview.c:789 from the `unsigned int eh` parameter that source/view.c:1740 fills with config.element_height (`int`, include/settings.h:152, registered xrm_SNumber at source/xrmoptions.c:405-411). BUT the claim's central assertion — "No validation exists anywhere between the option parser and the VLA" — is false: source/helper.c:733-740 in config_sanity_check() does `if (config.element_height < 1) { ...; config.element_height = 1; }`, and it runs at source/rofi.c:837 in startup(), before any listview is created and after theme/config-block parsing. So `-eh -1` does NOT produce 4294967295; that whole scenario is refuted. What survives is only the missing upper bound: `-eh 10000000` passes the sanity check and allocates a 20MB stack VLA. Self-inflicted, non-attacker-controlled.

## `source/widgets/textbox.c:788` — textbox_delete() clamps `dlen = len - dlen` instead of `len - pos`, producing a heap overflow write

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/textbox.c:787-789:
```
  if ((pos + dlen) > len) {
    dlen = len - dlen;
  }
```
The clamp is meant to cut the deletion at the end of the string, i.e. `dlen = len - pos`. As written it subtracts the wrong operand and can produce a *negative* dlen, after which `g_utf8_offset_to_pointer(tb->text, pos + dlen)` (line 792) returns a pointer BEFORE `start`, and the memmove at line 794 `memmove(start, end, (tb->text + strlen(tb->text)) - end + 1)` copies a byte count larger than the remaining space at `start`, writing past the end of the g_malloc'd buffer. textbox_delete is a public, installed-header API (include/widgets/textbox.h:263), so third-party/plugin callers can hit it. All in-tree callers (textbox.c:815/880/888/895/903) happen to keep pos+dlen <= len, so this is latent but untested (test/textbox-test.c never exercises the clamp).

**Failure scenario.** textbox_delete(tb, 2, 5) on text "aap" (len 3): pos=2, (2+5)>3 so dlen = 3-5 = -2; start = text+2, end = offset_to_pointer(text, 0) = text; memmove(text+2, text, 4) writes text[2..5] into a 4-byte allocation -> 2-byte heap buffer overflow.

**Proposed fix.** Change source/widgets/textbox.c:788 to `dlen = len - pos;` and add `if (dlen <= 0) return;` before the pointer arithmetic.

**Verifier.** source/widgets/textbox.c:787-789 is verbatim `if ((pos + dlen) > len) { dlen = len - dlen; }`; `len` is g_utf8_strlen (line 782) and pos is clamped at line 786, so the clamp should be `len - pos`. With pos=2,dlen=5,len=3 it yields dlen=-2, end=g_utf8_offset_to_pointer(text,0) < start, and the memmove at line 794 copies more bytes than remain — a heap overflow write. However the claim's exploitability framing is wrong: include/widgets/textbox.h is NOT an installed header (meson.build:195-203 installs only mode.h, mode-private.h, helper.h, rofi-types.h, rofi-icon-fetcher.h), so no third-party plugin can call it, and I checked every in-tree caller (textbox.c:815, 880, 888, 895, 903) — all keep pos+dlen<=len, and textbox_delete returns early at line 783 when len==pos. Latent only.

## `source/widgets/textbox.c:350` — Password masking builds a stack VLA sized by the length of the user-supplied text

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/textbox.c:347-355:
```
  if ((tb->flags & TB_PASSWORD) == TB_PASSWORD) {
    size_t text_len = g_utf8_strlen(tb->text, -1);
    size_t mask_len = strlen(tb->password_mask_char);
    char string[text_len * mask_len + 1];
```
Both factors are attacker/user controlled: `text_len` is the length of whatever was typed or pasted into the entry, `mask_len` is the byte length of the theme's `password-mask` string (textbox.c:254-260, which may be an arbitrarily long multi-byte string). The product is neither bounded nor checked for overflow before being used as a VLA size.

**Failure scenario.** `rofi -dmenu -password` and paste a multi-megabyte clipboard buffer (source/view.c:998): text_len * mask_len becomes multi-megabyte, the VLA at textbox.c:350 exceeds the 8MB stack, and rofi crashes with SIGSEGV on the next repaint. A theme with a long `password-mask` multiplies the effect.

**Proposed fix.** Allocate with g_malloc/g_string (freed after pango_layout_set_text) and bound the mask length, e.g. cap `mask_len` to a few bytes and use g_strnfill-style construction, in __textbox_update_pango_text (source/widgets/textbox.c:347-355).

**Verifier.** source/widgets/textbox.c:347-355 is verbatim as quoted: `size_t text_len = g_utf8_strlen(tb->text,-1); size_t mask_len = strlen(tb->password_mask_char); char string[text_len * mask_len + 1];` with no bound and no overflow check on the product. mask_len is theme-controlled (textbox.c:254-260, `password-mask` string taken as-is when non-empty) and text_len is whatever was typed/pasted. This is an unbounded stack allocation on every __textbox_update_pango_text for a TB_PASSWORD box. Real, but it needs a megabyte-scale password entry, which is self-inflicted rather than attacker-supplied.

## `source/widgets/textbox.c:1050` — textbox_cleanup() leaves p_metrics, tbfc_default and the char-width caches dangling/stale

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/textbox.c:1050-1056:
```
void textbox_cleanup(void) {
  g_hash_table_destroy(tbfc_cache);
  if (p_context) { g_object_unref(p_context); p_context = NULL; }
}
```
Destroying the table runs tbfc_entry_free() (line 1011-1017) on every value including `tbfc_default`, whose `metrics` field IS the static `p_metrics` (assigned at line 1030). After cleanup: `tbfc_cache`, `tbfc_default` and `p_metrics` all point at freed memory, and the memoised `char_width`/`ch_width` statics (lines 1095, 1105) keep their old values. Meanwhile textbox_set_pango_context() opens with `g_assert(p_metrics == NULL)` (line 1026), so a second setup/cleanup cycle aborts the process. textbox_get_estimated_char_height() (line 1092) dereferences tbfc_default with no NULL check.

**Failure scenario.** Any code path that calls textbox_cleanup() and then textbox_setup()/textbox_set_pango_context() again - e.g. a future runtime theme/font reload, or a second test case in test/textbox-test.c - aborts on the g_assert at textbox.c:1026; any call to textbox_get_estimated_char_height() after cleanup is a use-after-free read of the freed tbfc_default.

**Proposed fix.** In textbox_cleanup() (source/widgets/textbox.c:1050) set `tbfc_cache = NULL; tbfc_default = NULL; p_metrics = NULL; char_width = -1; ch_width = -1;` after destroying the table.

**Verifier.** source/widgets/textbox.c:1050-1056 is verbatim as quoted: it destroys tbfc_cache without nulling it and never touches p_metrics or tbfc_default. tbfc_entry_free (1011-1017) calls pango_font_metrics_unref(tbfc->metrics), and tbfc_default->metrics IS the static p_metrics (assigned at 1030, tbfc_default set at 1044), so after cleanup p_metrics/tbfc_default/tbfc_cache all dangle and char_width/ch_width (1095, 1105) keep memoised values. textbox_set_pango_context opens with `g_assert(p_metrics == NULL)` at line 1026, so a second setup cycle aborts, and textbox_get_estimated_char_height (line 1092) dereferences tbfc_default unguarded. Purely latent today — cleanup only runs at exit.

## `source/xcb/display.c:2013` — xcb_get_input_focus_reply(): NULL reply dereferenced and reply never freed

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

In xcb_display_set_input_focus(), only `error` is checked (line 2006). If the connection is shut down, xcb returns both reply == NULL and error == NULL, and line 2013 dereferences freply. Independently, freply is never free()d on any path — it leaks on every successful call.

**Failure scenario.** X server drops the connection (or the request is answered with reply==NULL, error==NULL) while rofi is showing its window: line 2013 dereferences NULL and rofi segfaults instead of exiting cleanly. On the normal path, each rofi_view_show leaks one xcb_get_input_focus_reply_t.

**Proposed fix.** Test `if (error != NULL || freply == NULL) {...}` and add `free(freply);` after reading `freply->focus`.

**Verifier.** source/xcb/display.c:2002-2017: only `if (error != NULL)` is tested; the else branch at 2013 does `xcb->focus_revert = freply->focus;`. GetInputFocus generates no protocol errors, so freply==NULL/error==NULL happens only on connection shutdown — narrow. The leak is unconditional and certain: `freply` is declared at 2003 and the function returns at 2018 with no free(freply) on any path.

## `source/xcb/display.c:557` — xcb_query_extension_reply() NULL return dereferenced in x11_is_extension_present()

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

`int present = randr_reply->present;` at line 557 with no NULL check. This runs at startup for both "RANDR" (line 600) and "XINERAMA" (line 602).

**Failure scenario.** The X connection errors out during startup (server shutdown, or the connection was already in an error state after g_water_xcb_source_new): xcb_query_extension_reply() returns NULL and rofi segfaults at line 557 during x11_build_monitor_layout().

**Proposed fix.** `if (randr_reply == NULL) { return 0; }` before line 557.

**Verifier.** source/xcb/display.c:554-559: `randr_reply = xcb_query_extension_reply(..., NULL); int present = randr_reply->present;` with no NULL check. Called at 600 and 602 from x11_build_monitor_layout(). QueryExtension never returns a protocol error, so NULL only on connection failure — a real but narrow deref.

## `source/xcb/display.c:540` — GetAtomName reply used after only the error pointer was checked

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

x11_get_monitor_from_randr_monitor() checks `err != NULL` (line 516) but never checks atom_reply. xcb_get_atom_name_reply() sets *e only for protocol errors; on a connection failure it returns NULL with *e left NULL. atom_reply is then dereferenced by xcb_get_atom_name_name_length()/xcb_get_atom_name_name() at lines 540-541.

**Failure scenario.** The connection dies during RandR 1.5 monitor enumeration: err stays NULL, atom_reply is NULL, and lines 540-541 dereference NULL inside g_strdup_printf's arguments. Also leaks nothing but crashes before the workarea is linked in.

**Proposed fix.** Change the guard to `if (err != NULL || atom_reply == NULL)` and free the (possibly allocated) reply on the error path.

**Verifier.** source/xcb/display.c:514-521 checks only `if (err != NULL)`; atom_reply is then dereferenced at 540-541 via xcb_get_atom_name_name_length()/xcb_get_atom_name_name(). Since &err is passed, a BadAtom protocol error is caught, so NULL-with-err-NULL only arises on connection failure — real but narrow.

## `source/xcb/display.c:731` — SnLauncherContext allocated per spawn is never unref'd, and sndisplay is not NULL-checked

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

xcb_display_startup_notification() creates a launcher context at line 731 and hands it out as *user_data at line 761. The sole caller, helper_execute_env() (source/helper.c:1379-1381), passes it to g_spawn_async() as child-setup user data and then drops the pointer — nothing ever calls sn_launcher_context_unref(). Also, unlike the launchee path (which guards with `if (xcb->sndisplay != NULL)` at line 1775), this function passes xcb->sndisplay to sn_launcher_context_new() unchecked; sn_launcher_context_new() refs the display and dereferences it.

**Failure scenario.** Leak: in a session where rofi stays open and launches several applications (drun with a mode that does not exit), one SnLauncherContext plus its allocated startup-id string leak per launch. Crash: if sn_xcb_display_new() at line 1768 returned NULL (startup-notification unavailable), the first application launch calls sn_launcher_context_new(NULL, ...) and dereferences NULL.

**Proposed fix.** Return the context through a wrapper that unrefs after g_spawn_async (or have helper_execute_env unref it), and add `if (xcb->sndisplay == NULL) { return; }` at the top of xcb_display_startup_notification().

**Verifier.** source/xcb/display.c:731 `sncontext = sn_launcher_context_new(xcb->sndisplay, xcb->screen_nbr);` and 761 `*user_data = sncontext;`. grep for sn_launcher_context_unref over source/ and include/ returns zero hits. The only caller, helper_execute_env (source/helper.c:1376-1382), passes user_data to g_spawn_async and drops it — a per-spawn leak. The unchecked-sndisplay half is also literally true (the launchee path guards with `if (xcb->sndisplay != NULL)` at 1775 while 731 does not), but sn_xcb_display_new() at 1768 realistically cannot return NULL, so the crash half is theoretical.

## `source/xcb/display.c:281` — Buffers allocated with g_malloc are released with libc free()

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

create_kernel() allocates with g_malloc (line 153) and cairo_image_surface_blur() allocates horzBlur with g_malloc (line 201); both are released with plain free() at lines 281-282. GLib documents that g_malloc'd memory must be released with g_free — the two only coincide because modern GLib hardcodes the system allocator.

**Failure scenario.** Any GLib build where g_malloc is not a thin wrapper over malloc (custom allocator builds, or a debug GLib with allocation tracking) frees these pointers through the wrong allocator, corrupting the heap the moment a theme sets `blur: <n>;` on a transparency background.

**Proposed fix.** Use g_free() at lines 281 and 282 (or switch both allocations to malloc and check for NULL).

**Verifier.** source/xcb/display.c:153 `uint32_t *kernel = (uint32_t *)(g_malloc(...))`, line 201 `horzBlur = (uint32_t *)(g_malloc(...))`, both released at 281-282 by plain `free(kernel); free(horzBlur);`. The mismatch is real API misuse. The claimed heap corruption is not achievable in practice: g_mem_set_vtable has been a no-op since GLib 2.46 and g_malloc/g_free are hardwired to malloc/free, so this is a correctness-of-style issue only.

## `source/xcb/view.c:544` — xcb_compound_text_to_utf8() result is used but never freed

- **kind** memory · **severity** low · **verdict** PLAUSIBLE · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

In xim_commit_string(), `utf8` (line 544, and again in the pre-1.0.3 branch at line 551) is passed to rofi_view_handle_text() and then dropped — no free()/g_free(). The rest of this file frees xcb-imdkit-produced buffers explicitly (see `free(nested.data)` at lines 585 and 603), so the convention in this codebase is that the caller owns these buffers. Worth confirming against the exact xcb-imdkit version being targeted before fixing (the header is not installed in this checkout, so I could not read its ownership doc-comment directly).

**Failure scenario.** With `-enable-imdkit` and an XIM server negotiating COMPOUND_TEXT encoding, every committed string from the input method leaks its converted UTF-8 buffer for the lifetime of the process.

**Proposed fix.** `free(utf8);` after the rofi_view_handle_text() call in both branches (guarded by the same `if (utf8)`).

**Verifier.** source/xcb/view.c:543-547 and 550-554: `char *utf8 = xcb_compound_text_to_utf8(str, length, &newLength);` is passed to rofi_view_handle_text() and never freed on either branch — the code is exactly as described. Ownership cannot be settled statically here: xcb-imdkit headers are not installed on this machine (no /usr/include/xcb-imdkit, no encoding.h anywhere on the filesystem), so whether the returned buffer is caller-owned is unverifiable. The `free(nested.data)` calls at 585 and 603 are a different API (xcb_xim_create_nested_list) and do not establish the convention for this function.

## `test/helper-pidfile.c:110` — Test frees a g_build_filename() result with plain free()

- **kind** memory · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`char *path = g_build_filename(tmpd, "rofi-pid.pid", NULL);` (line 99) is released with `free(path);` (line 110) instead of g_free(). Same allocator-provenance mismatch as the history.c case; it works only because g_free happens to wrap libc free in a default glibc build.

**Failure scenario.** Run the helper_pidfile test against a GLib built with a non-default allocator or under a heap checker that tracks allocator provenance: free() is handed a pointer the libc heap does not own and reports corruption / aborts, failing a test that has nothing to do with pidfiles.

**Proposed fix.** Use g_free(path) at line 110.

**Verifier.** test/helper-pidfile.c:99 `char *path = g_build_filename(tmpd, "rofi-pid.pid", NULL);` and :110 `free(path);`. Allocator mismatch is real but benign on any normal GLib build; test-only.

## `.github/workflows/build.yml:19` — No CI job builds on any BSD, and no __FreeBSD__/BSD conditional exists anywhere in the tree

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** build system, packaging and portability (FreeBSD

All four jobs in .github/workflows/build.yml are runs-on: ubuntu-latest (lines 19, 30, 40, 53); .build.yml:1 is image: ubuntu/lts; .gitlab-ci.yml is Debian/Ubuntu apt. Separately, grepping the whole tree for __FreeBSD__, __OpenBSD__, __NetBSD__ and __DragonFly__ returns nothing - there is not one platform conditional. Meanwhile INSTALL.md:259-263 advertises `sudo pkg install rofi` as a supported install path. The audit found the source otherwise clean of glibc-isms (zero hits for strcasestr, asprintf, qsort_r, memmem, secure_getenv, program_invocation_name, execvpe, pipe2, O_PATH, sighandler_t, strdupa, canonicalize_file_name, MAP_ANONYMOUS, /proc/, /dev/shm), and getline/getdelim, sysexits.h, glob.h, sys/file.h and pwd.h are all BSD-native - so BSD support is plausible but entirely unverified.

**Failure scenario.** A FreeBSD-only build break (the librt-free shm_open path, wayland-scanner discovery, or a future glibc-ism) lands and is discovered only by the FreeBSD ports maintainer weeks later, since nothing in CI compiles on BSD.

**Proposed fix.** Add a FreeBSD job using vmactions/freebsd-vm (or a Cirrus .cirrus.yml) that runs `pkg install meson pkgconf pango cairo glib wayland wayland-protocols libxkbcommon` then `meson setup build && ninja -C build && ninja -C build test`.

**Verifier.** Verified .github/workflows/build.yml lines 19, 30, 40, 53 are all `runs-on: ubuntu-latest` (four jobs: build-gcc, build-clang, build-gcc-wayland-only, build-gcc-xcb-only); .build.yml:1 `image: ubuntu/lts`; .gitlab-ci.yml is apt-based. `grep -rn '__FreeBSD__\|__OpenBSD__\|__NetBSD__\|__DragonFly__'` outside .git returns zero hits. I independently re-ran the glibc-ism grep over source/ include/ config/ lexer/ for strcasestr, asprintf, qsort_r, memmem, secure_getenv, program_invocation_name, execvpe, pipe2, O_PATH, sighandler_t, strdupa, canonicalize_file_name, MAP_ANONYMOUS and for /proc/ and /dev/shm — all zero, so the 'source is otherwise clean' half holds too. INSTALL.md:259-262 does advertise `sudo pkg install rofi` under a '### FreeBSD' heading. This is absence of coverage, not a defect.

## `source/wayland/display.c:1942` — printf("%s", output->name) with a name that is NULL on wl_output < v4

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

output->name is only ever set by wayland_output_name() (lines 1428-1435), which is compiled in only `#ifdef WL_OUTPUT_NAME_SINCE_VERSION` and only fires for wl_output version 4+. wayland_output_done() at line 1388 correctly guards: `self->name ? self->name : "Unknown"`. wayland_display_dump_monitor_layout() at lines 1941-1942 does not: `printf("%s            name%s: %s\n", ..., output->name)`. Passing NULL to %s is undefined behaviour; glibc prints "(null)" as an extension, musl and older FreeBSD libc do not.

**Failure scenario.** `sofi -dump-monitor-layout` (or any -help/-info path that reaches it) on a musl-based distro (Alpine, Void musl) or on FreeBSD, against a compositor advertising wl_output version 2 or 3 -> SIGSEGV inside vfprintf instead of a monitor listing.

**Proposed fix.** Use the same guard as line 1388: `output->name ? output->name : "Unknown"`.

**Verifier.** display.c:1941-1942 passes output->name to %s unguarded, while wayland_output_done at 1395 guards with `self->name ? self->name : "Unknown"`, and name is only assigned in wayland_output_name (1433-1439) inside #ifdef WL_OUTPUT_NAME_SINCE_VERSION. The UB is real, but the portability scenario is overstated: glibc, musl and FreeBSD libc all substitute "(null)" for a NULL %s, so a SIGSEGV is not the expected outcome.

## `Examples/rofi-file-browser.sh:85` — readlink -e and tac are GNU coreutils extensions absent on BSD

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Line 85 `ROFI_FB_CUR_DIR=$(readlink -e "${ROFI_FB_CUR_DIR}")` uses `-e`, which BSD readlink does not implement (FreeBSD readlink has -f but not -e; macOS readlink has neither). Line 93 `tac "${ROFI_FB_HISTORY_FILE}"` — tac is GNU-only; the BSD equivalent is `tail -r`. Lines 63/64/68 use `sed -i` with no backup suffix, which BSD sed rejects (it consumes the next argument as the suffix). Note the sibling script script/rofi-theme-selector *does* branch on OS for exactly these reasons (its lines 10-21 and 191-196), so the inconsistency is within the project itself.

**Failure scenario.** FreeBSD user runs the shipped file-browser example: line 85 prints `readlink: illegal option -- e` and ROFI_FB_CUR_DIR becomes empty, so line 86 writes an empty previous-location file and line 87 `pushd ""` fails; line 93 fails with `tac: not found`. The browser lists $PWD instead of the requested directory and loses history entirely. Line 63's `sed -i "s|..."` consumes the s-expression as the backup suffix and then treats the history file as the script.

**Proposed fix.** Use `readlink -f` (present on both) or `cd -- "$dir" && pwd -P`; replace `tac` with `tail -r` behind an OS check or with `sed '1!G;h;$!d'`; and use the same OS-detection block as script/rofi-theme-selector lines 10-21 for the `sed -i` suffix.

**Verifier.** Examples/rofi-file-browser.sh:85 is `ROFI_FB_CUR_DIR=$(readlink -e "${ROFI_FB_CUR_DIR}")` (-e is GNU-only; FreeBSD readlink has -f/-n but not -e), line 93 is `tac "${ROFI_FB_HISTORY_FILE}" | grep ...` (tac is GNU coreutils; BSD is `tail -r`), and lines 63/64/68 use `sed -i` with no suffix. The intra-project inconsistency is real: script/rofi-theme-selector lines 10-21 and 191-196 do branch on $OSTYPE for exactly gsed and readlink-vs-realpath. Severity low: Examples/ is not installed by meson.build (grep for 'Examples' in meson.build returns nothing), so this is a sample users copy, not shipped behavior.

## `include/widgets/widget-internal.h:60` — Widget geometry is `short`, silently truncating sizes above 32767

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

include/widgets/widget-internal.h:59-66 declares `short x, y, w, h;` and the resize vtable entry is `void (*resize)(struct _widget *, short, short);` (line 103), matched by widget_resize()/widget_move() taking shorts (source/widgets/widget.c:92, 107). Callers routinely pass ints: source/widgets/listview.c:805 passes 100000000 to textbox_moveresize(), listview.c:809 passes 100000 as the probe width, and box.c:138/205 pass computed int widths. Implicit int->short conversion of an out-of-range value is implementation-defined (and the intermediate arithmetic in listview_draw at lines 505-542 is done in unsigned int before being narrowed).

**Failure scenario.** A window wider than 32767 px (a spanned multi-monitor setup at high DPI, or `-width` in px on such a desktop): widget_resize(w=40000) stores -25536 in `wid->w`, widget_draw's `if (wid->h < 1 || wid->w < 1)` at source/widgets/widget.c:147 then skips drawing entirely and the UI is blank.

**Proposed fix.** Widen x/y/w/h to `int` in include/widgets/widget-internal.h:60-66 and change the resize callback/widget_resize/widget_move signatures accordingly (internal header, not installed, so no plugin ABI impact).

**Verifier.** include/widgets/widget-internal.h:60-66 declares `short x; short y; short w; short h;`, line 103 is `void (*resize)(struct _widget *, short, short);`, and source/widgets/widget.c:92 / 107 take shorts. The truncation is not merely theoretical: source/widgets/listview.c:805 calls textbox_moveresize(row.textbox, 0, 0, 100000000, -1), which at source/widgets/textbox.c:468 stores `tb->widget.w = MAX(1, 100000000)` into the short field, yielding -7936, and line 472-474 then feeds that negative width to pango_layout_set_width. It only works by accident (pango treats negative width as unlimited). The claimed blank-UI scenario needs a >32767px surface, which I cannot verify as reachable.

## `lexer/theme-parser.y:475` — Config-error diagnostic passes a NULL filename to a %s conversion

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** theme engine (source/theme.c, lexer/theme-lexer.

The t_config_property action logs `g_warning("%s:%d:%d: %s\n", @$.filename, ...)` and builds the same string with g_string_append_printf at line 477. When the input came from rofi_theme_parse_string (lexer/theme-lexer.l:964-975) the ParseObject is g_malloc0'd and `filename` is never set, so lexer/theme-lexer.l:336 seeds yylloc->filename with NULL and @$.filename is NULL. Passing NULL for %s is undefined; glibc and FreeBSD libc print "(null)", musl does not.

**Failure scenario.** `rofi -theme-str 'configuration { theme: "abc"; }'` — config_parse_set_property (source/xrmoptions.c:945-959) always returns TRUE for the deprecated `theme` option — reaches line 475 with a NULL filename. On a musl-based build (Alpine) this crashes inside vfprintf instead of printing the deprecation notice.

**Proposed fix.** Use `@$.filename ? @$.filename : "<string>"` in both format calls (source/theme.c:633-645 already handles the NULL case correctly).

**Verifier.** lexer/theme-parser.y:475 is `g_warning("%s:%d:%d: %s\n", @$.filename, ...)` and 477 the matching g_string_append_printf. Critically, the grammar defines its OWN YYLLOC_DEFAULT (theme-parser.y:49-66) that propagates `.filename` from YYRHSLOC(Rhs,1) and sets it to NULL when N==0, so @$.filename genuinely tracks yylloc->filename rather than being uninitialised. rofi_theme_parse_string (theme-lexer.l:981-990) g_malloc0's the ParseObject and never sets ->filename, and theme-lexer.l:336 copies that NULL into yylloc->filename. So NULL does reach %s. glib routes these through the system vasprintf, so musl is UB; glibc/FreeBSD print "(null)".

## `meson.build:57` — shm_open() is used but no librt is linked; only libm is probed

- **kind** portability · **severity** low · **verdict** PLAUSIBLE · **domain** build system, packaging and portability (FreeBSD

source/wayland/display.c:209 calls shm_open() and :210 shm_unlink(). The only manual library probe in the build is `dep_lm = c_compiler.find_library('m', required : false)` at meson.build:57; there is no find_library('rt') anywhere in meson.build. On glibc older than 2.34 the POSIX shared-memory functions live in librt, not libc. On FreeBSD they are in libc so this is fine there, but the omission is unconditional and undeclared.

**Failure scenario.** Building the wayland backend on a distro with glibc < 2.34 (Debian bullseye, RHEL 8/9, Ubuntu 20.04) fails at link time with "undefined reference to shm_open" / "shm_unlink" from source/wayland/display.c.o.

**Proposed fix.** Add `dep_rt = c_compiler.find_library('rt', required: false)` and append it to the wayland deps, or probe with c_compiler.has_function('shm_open') and pull librt only when the bare probe fails.

**Verifier.** Facts verified: source/wayland/display.c:209 `fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);` and :210 `shm_unlink(shm_name);`; `grep -rn find_library` across the tree returns exactly one hit, meson.build:57 `dep_lm = c_compiler.find_library('m', required : false)` — there is no 'rt' probe. But the scenario is largely self-refuting: meson.build:48-52 set a hard floor of glib >= 2.72, which excludes every distro the claim names (Debian bullseye ships glib 2.66, RHEL 8 glib 2.56, Ubuntu 20.04 glib 2.64 — none can configure this project at all), and RHEL 9 / glibc 2.34 already has librt merged into libc. Whether any buildable platform actually needs -lrt is a toolchain question I cannot settle statically.

## `meson.build:316` — wayland-scanner is found by bare PATH lookup instead of the wayland-scanner.pc variable

- **kind** portability · **severity** low · **verdict** PLAUSIBLE · **domain** build system, packaging and portability (FreeBSD

meson.build:316 is `wayland_scanner = find_program('wayland-scanner')` - an unqualified PATH search with no native: true. The canonical form reads the wayland_scanner variable out of wayland-scanner.pc (`dependency('wayland-scanner', native: true).get_variable(pkgconfig: 'wayland_scanner')`), which is what handles the host/build split. Relatedly, wayland_protocols (meson.build:106) is a build-time data-only dependency but is appended to the link deps at meson.build:119.

**Failure scenario.** Cross-compiling for aarch64 from an x86_64 host with a sysroot: find_program picks a wayland-scanner from the target sysroot's bin/, and the generated protocol C/H files at meson.build:337-350 are produced by a binary that cannot run on the build machine, failing with "Exec format error".

**Proposed fix.** Use dependency('wayland-scanner', native: true).get_variable(pkgconfig: 'wayland_scanner') with a find_program fallback, and move wayland_protocols out of the link deps list at meson.build:119.

**Verifier.** meson.build:316 is verbatim `    wayland_scanner = find_program('wayland-scanner')` with no native: true and no pkg-config variable lookup, and meson.build:106/107-111/119 confirm wayland_protocols (a data-only .pc) is inside wayland_deps and therefore appended to the link deps at `deps += wayland_deps + [...]`. But the stated failure mechanism is wrong: meson's find_program does not search a cross sysroot's bin/ — it consults the cross file's [binaries] section and then the build-machine PATH, so 'picks a wayland-scanner from the target sysroot' is not how it resolves. Whether a given cross setup misresolves it depends on that cross file, which I cannot inspect. The wayland_protocols-in-link-deps part is harmless (the .pc exports no Libs). Style nit, not a demonstrated break.

## `script/rofi-sensible-terminal:1` — Pure-POSIX script carries a bash shebang, adding a hard bash dependency on BSD

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

`#!/usr/bin/env bash` on line 1, but the whole script (lines 12-18) uses only `for`, `command -v`, `exec` and a plain command — no arrays, no `[[ ]]`, no `+=`, nothing bash-specific. On FreeBSD and OpenBSD, bash is not in base; it is a package from ports installed at /usr/local/bin/bash. README.md line 149 claims "Rofi is known to work on Linux and BSD", and this script is installed as a user-facing binary by meson.build lines 205-207.

**Failure scenario.** On a FreeBSD system without shells/bash installed, rofi's default terminal_emulator (config/config.c line 65, `.terminal_emulator = "rofi-sensible-terminal"`) fails with `env: bash: No such file or directory`, so `rofi -show run` cannot launch anything in a terminal at all.

**Proposed fix.** Change line 1 to `#!/bin/sh`. Nothing else in the file needs to change. (script/get_git_rev.sh has the same needless bash shebang and only uses `echo -n`, which is the one thing that would need adjusting there.)

**Verifier.** script/rofi-sensible-terminal:1 is `#!/usr/bin/env bash`, and I read the entire body (18 lines): only `for ... in` (12), `command -v` (13), `exec` (14), `done` (16) and a plain `rofi -e` (18) -- no arrays, no [[ ]], no +=, nothing outside POSIX sh. README.md:149 does say 'Rofi is known to work on Linux and BSD', meson.build lines 204-208 install_data it into bindir, and config/config.c:65 is `.terminal_emulator = "rofi-sensible-terminal",`. Correct as described; severity low because bash is a near-universal package even where it is not in base, so the failure requires a deliberately bash-free BSD.

## `script/rofi-theme-selector:18` — On BSD/macOS the script requires GNU sed under the name gsed or refuses to run

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Lines 17-21 select `SED=$(command -v gsed)` when OS is bsd (set at line 12 from $OSTYPE) and `SED=$(command -v sed)` otherwise. Lines 24-28 then hard-exit if SED is empty. gsed is not part of FreeBSD base — it comes from the textproc/gsed port — so on a stock FreeBSD or OpenBSD install the script dies at line 27 with "Did not find 'sed', script cannot continue", which is actively misleading because sed *is* present. The reason GNU sed is wanted is the `-i` with no backup suffix used at lines 120 and 203, which BSD sed spells `-i ''`.

**Failure scenario.** FreeBSD user installs the rofi package (INSTALL.md line 262 documents `pkg install rofi`) and runs rofi-theme-selector; it exits immediately claiming sed is missing, and the man page doc/rofi-theme-selector.1.markdown gives no hint that gsed is a dependency.

**Proposed fix.** Fall back to BSD sed with an explicit empty suffix rather than exiting: set `SED_INPLACE="-i ''"` vs `SED_INPLACE="-i"` per OS and always use `command -v sed`. If gsed really is required, document it in doc/rofi-theme-selector.1.markdown and in the package dependencies.

**Verifier.** script/rofi-theme-selector lines 17-21 are `if [ $OS = "bsd" ]; then SED=$(command -v gsed); else SED=$(command -v sed); fi`, with OS set to bsd for *bsd*|*darwin* at line 12, and lines 24-28 `if [ -z "${SED}" ] ... echo "Did not find 'sed', script cannot continue."; exit 1`. gsed is not in FreeBSD base. The GNU-sed requirement is genuine (lines 120 and 203 use `-i` with no backup suffix, which BSD sed spells `-i ''`), so the dependency is legitimate; the defect is really the misleading message and the undocumented dependency -- doc/rofi-theme-selector.1.markdown contains no mention of gsed (I read all 40 lines). Hence low, not medium.

## `source/helper.c:1578` — isblank() is passed a possibly-negative plain char

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`while (input != NULL && isblank(*input))` (line 1578) passes a `char` directly to isblank(). On platforms where char is signed (x86/x86_64 Linux, FreeBSD), any byte >= 0x80 — i.e. every continuation byte of a UTF-8 sequence — converts to a negative int, which is outside the domain of the <ctype.h> functions (only EOF and values representable as unsigned char are defined). glibc's table-based implementation happens to tolerate small negative indices; other libcs are free to read out of bounds.

**Failure scenario.** Pass a non-ASCII range specification, e.g. `rofi -dmenu -selected-rows 'é1-3'`, so parse_pair receives a leading multi-byte character: isblank() is indexed at a negative offset into the ctype table. On glibc this silently returns garbage; on a libc with a smaller table it is an out-of-bounds read.

**Proposed fix.** Cast at the call site: `isblank((unsigned char)*input)`.

**Verifier.** source/helper.c:1576-1579: `static gboolean parse_pair(char *input, ...) { while (input != NULL && isblank(*input)) { ++input; } }` — a plain char is passed to isblank(), which is only defined for EOF and values representable as unsigned char. Textbook portability nit, no practical failure on glibc.

## `source/modes/script.c:422` — script_mode_result calls free() on a pointer allocated with g_strdup

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`free(*input); *input = NULL;` in the switch-mode branch. *input was allocated by g_strdup in source/rofi.c:241. Six lines later the same field is released correctly with g_free (line 445).

**Failure scenario.** Correct only as long as GLib's allocator is the system malloc. Under a GLib built with a different allocator, or with g_mem_set_vtable in play, the free() mismatches the allocation and corrupts the heap; it also defeats G_SLICE/gmem debugging instrumentation.

**Proposed fix.** Use g_free(*input).

**Verifier.** script.c:422 `free(*input);` inside the `if (rmpd->switch_mode >= 0)` branch, while the same field is released with g_free at script.c:445. *input originates from g_strdup at source/rofi.c:241. Benign with GLib's default system-malloc allocator but an allocator mismatch by contract.

## `source/widgets/listview.c:601` — Trigger-action callbacks are installed through incompatible function pointer types

- **kind** portability · **severity** low · **verdict** PLAUSIBLE · **domain** source/widgets/*.c + include/widgets/*.h (box, c

`widget_trigger_action_cb` is declared as taking `guint action` (include/widgets/widget.h:111-114), but the implementations are declared with their own enum parameter and assigned without a cast: `listview_element_trigger_action(widget*, MouseBindingListviewElementAction, ...)` passed to widget_set_trigger_action_handler at source/widgets/listview.c:601 and 198/209, `textbox_editable_trigger_action(widget*, MouseBindingMouseDefaultAction, ...)` assigned at source/widgets/textbox.c:271, and `scrollbar_trigger_action` at source/widgets/scrollbar.c:117. ISO C only guarantees an enumerated type is compatible with `char`, a signed integer type, or an unsigned integer type - which one is implementation-defined - so these assignments are only accepted because GCC/Clang happen to pick `unsigned int` here. The call goes through widget.c:554 `wid->trigger_action(wid, action, x, y, ...)`.

**Failure scenario.** Built with a toolchain that types these enums as `int` (or with -fshort-enums, or with Clang's -fsanitize=function / a CFI-enabled build): the assignment becomes an incompatible-pointer-type error, or the indirect call traps at runtime with 'call to function through pointer to incorrect function type'.

**Proposed fix.** Declare the implementations with the `guint action` parameter and switch on a cast enum inside the body (source/widgets/listview.c:734-736, source/widgets/textbox.c:96-99, source/widgets/scrollbar.c:79-82).

**Verifier.** The declarations are as described: include/widgets/widget.h:111-114 types widget_trigger_action_cb's action parameter as `guint`, while listview_element_trigger_action (source/widgets/listview.c:734-736) takes MouseBindingListviewElementAction and is installed uncast at listview.c:601 (and via widget_set_trigger_action_handler at 198/209), scrollbar_trigger_action (scrollbar.c:80) takes MouseBindingMouseDefaultAction and is assigned at scrollbar.c:117, textbox_editable_trigger_action at textbox.c:271; the indirect call is widget.c:562/551-554. Since it compiles today, the toolchain evidently gives these enums unsigned int as their compatible type, so nothing is broken in practice; the risk (compile error under -fshort-enums, or a trap under clang -fsanitize=function / CFI) is a build-configuration condition I cannot verify here.

## `source/xcb/display.c:707` — printf("%s", NULL) for Xinerama-derived monitors in -dump-monitor-layout

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

x11_build_monitor_layout_xinerama() (lines 564-593) never sets `w->name` — the workarea is g_malloc0'd, so name stays NULL. xcb_display_dump_monitor_layout() then passes iter->name to plain libc printf() at line 707 (this is stdio printf, not g_print), which is undefined behaviour for a NULL %s argument. glibc happens to print "(null)"; other libcs (and some hardened/musl configurations) crash.

**Failure scenario.** On a RANDR-less X server with XINERAMA (e.g. an Xephyr/VNC/remote server, or FreeBSD with a legacy driver), `rofi -dump-monitor-layout` calls printf with a NULL %s and crashes on any libc that does not special-case NULL.

**Proposed fix.** Set a name in x11_build_monitor_layout_xinerama (e.g. g_strdup_printf("xinerama-%d", index)) and/or print `iter->name ? iter->name : "(unnamed)"`.

**Verifier.** source/xcb/display.c:576-586: the workarea is g_malloc0'd and only x/y/w/h/next are set — `w->name` is never assigned in the Xinerama path (grep of `name` assignments in this file shows name set only at 490 and 539, both RandR paths). xcb_display_dump_monitor_layout passes iter->name to libc printf("…%s\n", …) at 706-707. Undefined behaviour for NULL on non-glibc. Note config.monitor matching at line 1016 uses g_strcmp0 and is NULL-safe, so the printf is the only exposure.

## `test/helper-expand.c:126` — helper_expand test assumes a 'root' user exists in the passwd database

- **kind** portability · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

`str = rofi_expand_path("~root/"); TASSERT(str[0] == '/');` (lines 126-127). rofi_expand_path only substitutes the home directory when getpwnam() succeeds (source/helper.c:797-801); otherwise it leaves the component as the literal "~root", so str[0] is '~'. The test also depends on the CWD for the "../AUTHORS" case at line 114.

**Failure scenario.** Run the suite inside a minimal container image or a chroot with no /etc/passwd entry for root (common for distroless / scratch-based CI images), or on a host where NSS cannot reach the user database: getpwnam("root") returns NULL, str is "~root", the assertion fails and CI reports a spurious failure unrelated to any code change.

**Proposed fix.** Resolve the expected home directory with getpwnam() in the test itself and skip the case when it returns NULL, or use g_get_user_name() and assert against that user's real pw_dir.

**Verifier.** test/helper-expand.c:126-127 is `str = rofi_expand_path("~root/"); TASSERT(str[0] == '/');`, and source/helper.c:796-801 only substitutes when getpwnam() returns non-NULL, otherwise the component stays the literal "~root" — so the assertion fails with no root passwd entry. The line-114 "../AUTHORS" CWD dependency is also there (source/helper.c:802-808 leaves relative paths untouched).

## `source/wayland/display.c:1524` — wl_data_device_manager is bound at hardcoded version 3 without MIN(version, 3)

- **kind** protocol · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Line 1522-1524: `wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3)`. Every other bind in this handler clamps: compositor MIN(version, WL_COMPOSITOR_INTERFACE_VERSION) at 1473, layer shell at 1475, shortcuts inhibitor at 1483, shm at 1487, seat MIN at 1495, output MIN at 1509. Binding a global at a version higher than the compositor advertised is a wl_registry `invalid_version` protocol error, which disconnects the client.

**Failure scenario.** A minimal or embedded compositor advertising wl_data_device_manager version 1 or 2 (the interface has existed at v1 since forever, and v3 only since wayland 1.2). sofi binds at 3, the compositor replies with wl_display.error(invalid_version) and closes the connection; sofi dies at startup with "error 0: invalid version" and no menu ever appears.

**Proposed fix.** `wl_registry_bind(registry, name, &wl_data_device_manager_interface, MIN(version, 3))` and gate the wl_data_device_manager_get_data_device call on the bound version. Add a named constant next to the others in include/wayland-internal.h:129-137.

**Verifier.** display.c:1522-1524 `wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3)` with no MIN(version, ...), unlike 1470, 1475, 1483, 1487, 1494 and 1509. Binding above the advertised version is a wl_registry invalid_version error. Downgraded because wl_data_device_manager v3 has been universal since wayland 1.2, so a compositor advertising <3 is rare.

## `source/wayland/display.c:1554` — Four bound globals have no entry in wayland_global_name, so global_remove leaves stale proxies in use

- **kind** protocol · **severity** low · **verdict** PLAUSIBLE · **domain** source/wayland/display.c + include/wayland-inter

wayland_global_name (include/wayland-internal.h:13-20) covers only COMPOSITOR, SHM, LAYER_SHELL, KEYBOARD_SHORTCUTS_INHIBITOR and CURSOR_SHAPE. wayland_registry_handle_global_remove() (lines 1544-1624) can therefore only clean up those five. But wayland_registry_handle_global() also binds and stores wl_data_device_manager (1523), zwp_primary_selection_device_manager_v1 (1528) and zwp_text_input_manager_v3 (1539) without recording their global names anywhere. When those globals go away the pointers stay non-NULL and are still used at lines 1217-1219, 1236-1239 and 1242-1248.

**Failure scenario.** An IME/text-input portal restarts and withdraws zwp_text_input_manager_v3. wayland->text_input_manager still points at a destroyed-server-side object. The next seat capabilities event reaches line 1218 and calls zwp_text_input_manager_v3_get_text_input() on it -> the compositor raises an `invalid object` protocol error and kills the client.

**Proposed fix.** Add WAYLAND_GLOBAL_DATA_DEVICE_MANAGER, WAYLAND_GLOBAL_PRIMARY_SELECTION and WAYLAND_GLOBAL_TEXT_INPUT to the enum, record their names on bind, and destroy+NULL them in the global_remove switch.

**Verifier.** Factually correct: wayland_global_name (include/wayland-internal.h:13-20) has only COMPOSITOR, SHM, LAYER_SHELL, KEYBOARD_SHORTCUTS_INHIBITOR, CURSOR_SHAPE, and display.c:1523, 1528 and 1539 bind data_device_manager, primary_selection_device_manager and text_input_manager without recording global_names, so global_remove (1544-1624) cannot clean them. But the stated harm is wrong: wl_registry.global_remove specifies that the bound object stays valid and requests on it are ignored, so no invalid-object protocol error/kill follows — the real consequence is a leaked proxy and silently non-functional text input.

## `source/wayland/display.c:349` — wl_surface_damage() is given buffer-space dimensions, not surface-local ones

- **kind** protocol · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

display_buffer_pool_new() multiplies width/height by wayland->scale at lines 194-195, so pool->width/pool->height (stored at 236-238) are in *buffer* pixels. display_surface_commit() then calls `wl_surface_damage(wayland->surface, 0, 0, pool->width, pool->height)` at line 349 — wl_surface.damage is defined in surface-local coordinates, which are buffer coordinates divided by buffer_scale (set to wayland->scale on the very next line, 352).

**Failure scenario.** At scale 2 the damage rectangle submitted is 2x wider and 2x taller than the surface. Compositors clamp it, so nothing visibly breaks, but the surface is over-damaged every frame — measurably more compositor work on every keystroke, and the bug becomes real (wrong region, visible stale pixels) the moment partial damage is introduced.

**Proposed fix.** Use wl_surface_damage_buffer() (wl_compositor v4+, already the recommended call) with the buffer-space rect, or divide by wayland->scale for wl_surface_damage. Note WL_COMPOSITOR_INTERFACE_VERSION is currently capped at 3 in include/wayland-internal.h:130, so bump it to 4 to get damage_buffer.

**Verifier.** pool->width/height are stored at display.c:237-238 after width/height were multiplied by wayland->scale at 194-195, and display_surface_commit passes them to wl_surface_damage at 349, immediately before wl_surface_set_buffer_scale(..., wayland->scale) at 352. wl_surface.damage is surface-local, so the rectangle is scale-times too large; compositors clamp it, so it is over-damage, not visible corruption.

## `source/helper.c:125` — helper_parse_setup() substitutes untrusted values into a command template before shell-word-splitting, with no quoting

- **kind** protocol · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

helper_parse_setup() textually substitutes the caller's key/value pairs into the template (line 125, via helper_string_replace_if_exists_v) and only then runs the result through g_shell_parse_argv (line 129). The substituted values are not quoted, so any quote, backslash or whitespace in a value changes the resulting argv *structure* rather than appearing as literal text. Values come from data rofi does not control: {host} from ~/.ssh/config and known_hosts (source/modes/ssh.c), {cmd} from .desktop Exec lines and $PATH scans, {input}/{output} from filenames (source/rofi-icon-fetcher.c:562-564). No shell is spawned by the defaults (config/config.c:70 run_command="{cmd}", :74 run_shell_command="{terminal} -e {cmd}"), so this is argv injection, not shell injection — but the documented pattern of configuring `run-command: "bash -c '{cmd}'"` converts it into full shell injection, and test/helper-config-cmdline-parser.c:107-119 demonstrates exactly that substitution-into-quotes behaviour.

**Failure scenario.** An ~/.ssh/config containing a Host entry with an embedded quote or spaces is read by ssh mode and substituted into `{terminal} -e {ssh-client} {host}`; g_shell_parse_argv then splits the injected text into extra argv words that are passed to ssh. With the widely-documented `run-command: "bash -c '{cmd}'"` setting, a .desktop Exec containing an apostrophe closes the quote and the remainder is executed by bash.

**Proposed fix.** Quote each substituted value with g_shell_quote() before insertion (adding an opt-out for templates that intentionally interpolate), or build the argv vector directly instead of round-tripping through a shell-syntax string.

**Verifier.** source/helper.c:112-129: the va_list key/value pairs go into a hash table and are substituted textually by helper_string_replace_if_exists_v at :125 (which does a g_regex_replace_eval, no quoting), and only then is the result handed to g_shell_parse_argv at :129 — so a value containing a quote or whitespace changes argv structure. test/helper-config-cmdline-parser.c:107-119 demonstrates exactly that: {host} substituted inside `bash -c "{ssh-client} {host}; ..."` becomes part of the -c string. Defaults are argv-only (config/config.c:70 run_command="{cmd}", :74 run_shell_command="{terminal} -e {cmd}"), and the values come from files the user already executes from (.desktop Exec, ~/.ssh/config, source/modes/ssh.c:105), so escalation is limited — hence low, not medium.

## `source/modes/recursivebrowser.c:459` — end_thread cancellation flag is a plain guint shared across threads with no atomics

- **kind** protocol · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

pd->end_thread (declared guint at line 95) is written by the UI thread in recursive_browser_mode_destroy (`pd->end_thread = TRUE;` line 459) and polled by the worker in scan_dir's readdir loop (`while (pd->end_thread == FALSE && ...)` line 203). There is no g_atomic access, no mutex and no volatile, so the compiler is free to hoist the load out of the loop.

**Failure scenario.** Built at -O2, the loop condition may be cached in a register for the duration of a directory's readdir loop (or longer), so g_thread_join at line 460 blocks until the entire scan finishes. Cancelling `sofi -show recursivebrowser` over a large home directory or an NFS mount hangs the UI until the whole tree has been walked.

**Proposed fix.** Use g_atomic_int_get/g_atomic_int_set (and declare the field gint), or a GCancellable.

**Verifier.** recursivebrowser.c:95 declares `guint end_thread;` (not volatile, not atomic). It is written by the UI thread at recursivebrowser.c:459 `pd->end_thread = TRUE;` immediately before g_thread_join at 460, and polled by the worker at recursivebrowser.c:203 `while (pd->end_thread == FALSE && (rd = readdir(dir)) != NULL)`. No g_atomic_int_get/set and no mutex — a genuine data race/UB. Whether the compiler actually hoists the load, and thus whether cancellation visibly hangs, is not statically determinable.

## `.gitattributes:8` — export-ignore rules reference seven files that no longer exist

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** User-facing and non-C surface: root docs, doc/ m

Checked each path in the file: `.travis.yml` (line 8), `doc/help-output.txt` (11), `doc/sizing.svg` (12), `doc/Notes` (13), `doc/old-theme-convert-output.rasi` (14), `doc/test_xr.txt` (15), `doc/create_screenshot.sh` (16) and `script/*.jpg` (18) are all absent from the tree. Only `.build.yml`, `.gitattributes`, `.gitignore`, `.mailmap`, `.github`, `.gitlab-ci.yml`, `.gitmodules`, `releasenotes`, `doc/*.png` and `mkdocs` still match anything. Harmless at runtime but it means nobody has audited what the release tarball actually contains since those files were deleted — relevant now, because `meson dist` output is the artifact .github/actions/release/action.yml lines 15-16 and 22-23 upload.

**Failure scenario.** No runtime failure; the risk is that the tarball composition is unreviewed. themes/iggy.jpg (321 KB) is NOT excluded and does ship in every release tarball, while the (nonexistent) script/*.jpg rule suggests someone intended image exclusion.

**Proposed fix.** Delete lines 8 and 11-16 and line 18, then verify the tarball with `meson dist --no-tests` and `tar tf` before the first sofi release.

**Verifier.** I checked every path in .gitattributes with a per-file existence test. Lines 8, 11, 12, 13, 14, 15, 16 (.travis.yml, doc/help-output.txt, doc/sizing.svg, doc/Notes, doc/old-theme-convert-output.rasi, doc/test_xr.txt, doc/create_screenshot.sh) all report MISSING, and `ls script/*.jpg` (line 18) returns 'No such file or directory'. themes/iggy.jpg does exist at 320985 bytes and is not covered by any rule, so it ships in `meson dist` output -- which .github/actions/release/action.yml uploads via `builddir/meson-dist/*.tar.xz` at lines 15-16 and 22-23. Accurate; no runtime impact, hence low.

## `source/wayland/display.c:228` — Inconsistent close()/g_close() on the shm fd, and two silent error returns

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Within one function the fd is closed with g_close(fd, NULL) at lines 217 and 221 but with close(fd) at lines 228 and 254. Additionally the fcntl failure path (216-219) and the ftruncate failure path (220-223) return NULL with no g_warning at all, unlike the shm_open and mmap paths which do warn. The fcntl(F_SETFD, FD_CLOEXEC) at line 216 is itself redundant: POSIX requires shm_open to set FD_CLOEXEC on the returned descriptor.

**Failure scenario.** ftruncate fails (e.g. /dev/shm quota exceeded); the pool silently becomes NULL, the caller crashes per pool-new-null-not-checked-by-caller, and there is not one log line explaining why.

**Proposed fix.** Use g_close() everywhere (or close() everywhere) and add g_warning() with g_strerror(errno) to both silent paths. Drop the redundant fcntl.

**Verifier.** display.c:217 and 221 use g_close(fd, NULL) while 228 and 254 use close(fd) in the same function; the fcntl path (216-219) and ftruncate path (220-223) return NULL with no g_warning, unlike 212 and 227. Cosmetic/diagnostic, not a behavioural bug.

## `source/wayland/display.c:1608` — global_remove removes the hash entry via the iterator and then the release function removes it again

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 1605-1622 do `g_hash_table_iter_remove(&iter); wayland_seat_release(seat);` and `g_hash_table_iter_remove(&iter); wayland_output_release(output);`. But wayland_seat_release() itself calls g_hash_table_remove(wayland->seats, self->seat) at line 1204 and wayland_output_release() calls g_hash_table_remove(wayland->outputs, self->output) at line 1341 — after having already destroyed the proxy that serves as the key (line 1202 / lines 1335-1339). Harmless today only because neither table has a value destroy function (lines 1738-1739); the moment one is added to fix the teardown leak this becomes a double free.

**Failure scenario.** Adding `(GDestroyNotify)wayland_seat_release` as the value-destroy for wayland->seats (the obvious fix for teardown-leaks-everything) turns line 1608's iter_remove into a call to wayland_seat_release, which re-enters g_hash_table_remove at line 1204 on a table mid-removal, and then line 1609 calls wayland_seat_release on the already-freed struct -> double free.

**Proposed fix.** Pick one owner: either the hash table owns the struct (destroy func, call only g_hash_table_iter_remove) or the caller does (no destroy func, drop the g_hash_table_remove calls from inside *_release).

**Verifier.** display.c:1608-1609 and 1620-1621 do iter_remove followed by wayland_seat_release / wayland_output_release, and those functions call g_hash_table_remove again at 1204 and 1341 after destroying the proxy used as the key (1202, 1335-1339). Harmless today (no value destroy funcs at 1738-1739, g_direct_hash never dereferences the key), purely latent.

## `source/wayland/display.c:26` — -Wunused-variable and -Wunused-parameter are disabled for the entire 2038-line file

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

Lines 26-27:
  #pragma GCC diagnostic ignored "-Wunused-variable"
  #pragma GCC diagnostic ignored "-Wunused-parameter"
There is no matching `#pragma GCC diagnostic push/pop`, so this covers everything below it, including all the headers included at lines 29-69. It is already hiding real dead code: `struct wl_buffer *buffer;` declared at line 191 in display_buffer_pool_new() is never used, and `wayland_seat *self = data;` is unused in wayland_keyboard_leave (line 421) and wayland_pointer_leave (line 858). It would equally hide a genuinely unused result variable in future edits.

**Failure scenario.** A future edit computes a value into a local and forgets to use it (e.g. capturing an error return); the compiler stays silent because of line 26, and the missed check ships.

**Proposed fix.** Delete both pragmas; annotate the genuinely-unused Wayland listener callback parameters with G_GNUC_UNUSED (or (void)param) and delete the dead locals at 191, 421 and 858.

**Verifier.** display.c:26-27 are the two #pragma GCC diagnostic ignored lines with no push/pop anywhere in the file, before all includes at 29-69. It is already masking dead locals: `struct wl_buffer *buffer;` at line 191 is never used in display_buffer_pool_new, and `wayland_seat *self = data;` at 422 in wayland_keyboard_leave is unused (a similar unused self exists in wayland_pointer_leave at 864-866).

## `source/wayland/display.c:1758` — wayland->bindings_seat is created after the roundtrip that can already dispatch keyboard events

- **kind** structure · **severity** low · **verdict** PLAUSIBLE · **domain** source/wayland/display.c + include/wayland-inter

wayland_display_setup() binds wl_seat inside the registry handler (line 1495-1497) during the wl_display_roundtrip() at line 1747, but only creates wayland->bindings_seat at line 1758, after that roundtrip. wayland_keyboard_keymap() (line 387) dereferences it via nk_bindings_seat_get_context(wayland->bindings_seat), and wayland_keyboard_enter/key/modifiers (lines 411, 434, 459, 499, 512, 531) all pass it to nk_bindings_seat_* unconditionally. Nothing enforces the ordering; it holds only because libwayland happens not to flush the bind requests before the first sync callback returns.

**Failure scenario.** Any change to the roundtrip structure (an added wl_display_flush, an extra roundtrip, a compositor that replies fast enough that the bind reply lands in the same dispatch batch) delivers wl_keyboard.keymap with wayland->bindings_seat == NULL -> NULL deref inside nk_bindings_seat_get_context at line 387 before the first menu is drawn.

**Proposed fix.** Move `wayland->bindings_seat = nk_bindings_seat_new(bindings, XKB_CONTEXT_NO_FLAGS);` above the wl_registry_add_listener/roundtrip block at lines 1744-1747, or add an explicit `if (wayland->bindings_seat == NULL) return;` guard to each keyboard handler.

**Verifier.** The ordering is as described — wl_seat is bound inside the registry handler at display.c:1499 during the roundtrip at 1747, and wayland->bindings_seat is only created at 1760 — and nothing enforces it. But it is not reachable today: wl_display_roundtrip's sync request is queued before any bind, so the server emits callback.done before it has even processed the wl_seat bind, and capabilities/keymap can only arrive in the second roundtrip at 1763. Latent fragility only.

## `source/wayland/display.c:150` — Pool iteration uses the global wayland->buffer_count instead of a count stored in the pool

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/wayland/display.c + include/wayland-inter

struct _display_buffer_pool (lines 100-108) stores data/size/width/height/to_free/buffers but not the number of buffers. wayland_buffer_cleanup() at line 150, wayland_buffer_release() at line 174 and display_buffer_pool_get_next_buffer() at line 318 all iterate `i < wayland->buffer_count`, while the array itself was allocated with `g_new0(wayland_buffer, wayland->buffer_count)` at line 240 using whatever the value was *at creation time*. wayland->buffer_count is set once at line 1734 to 3, so it is latent today.

**Failure scenario.** Anyone making buffer_count configurable (a plausible rebrand-era addition: a --buffer-count flag, or lowering it to 2 on memory-constrained systems) and changing it while a pool exists causes the release/cleanup loops to run past the end of pool->buffers -> heap out-of-bounds read at line 150 and out-of-bounds write of `.released` at line 176.

**Proposed fix.** Add `size_t buffer_count;` to struct _display_buffer_pool, set it from wayland->buffer_count in display_buffer_pool_new, and use self->buffer_count in all three loops.

**Verifier.** struct _display_buffer_pool (display.c:100-108) stores no count, while the array is allocated with wayland->buffer_count at 240 and iterated with wayland->buffer_count at 153, 175 and 318. wayland->buffer_count is assigned once, at 1734 (`= 3`), so this is latent coupling rather than a live bug.

## `include/view.h:372` — rofi_view_set_window_title is declared twice in the same header

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

Identical prototypes at include/view.h:364 and include/view.h:372, the second one without the doc comment. view.h is part of the public-ish surface consumed by both backends and by source/rofi.c.

**Failure scenario.** Not a runtime defect, but a rename during the rebrand that edits only one of the two lines leaves the header declaring both `rofi_view_set_window_title` and `sofi_view_set_window_title`, and the resulting mismatch only surfaces at link time.

**Proposed fix.** Delete the duplicate at line 372.

**Verifier.** include/view.h:361-364 has the documented `void rofi_view_set_window_title(const char *title);` and the identical undocumented prototype appears again at :372, between rofi_view_ping_mouse (:370) and rofi_view_pool_refresh (:373).

## `meson.build:434` — All eleven test executables reuse rofi.extract_objects(), which is fragile under the -Db_lto=true that CI enables

- **kind** structure · **severity** low · **verdict** PLAUSIBLE · **domain** build system, packaging and portability (FreeBSD

Every test target links objects pulled out of the main executable - meson.build:434, 444, 462, 481, 499, 517, 533, 547, 561, 581, 596, 613 - rather than linking a shared static library. .build.yml:36 configures with -Db_lto=true, which makes those .o files GIMPLE-only fat objects whose reuse across a different link unit depends on compiler LTO plumbing. The copy-paste structure is also why 'source/css-colors.c' is listed twice within the same list at meson.build:466-468, 485-487, 503-505, 521-523 and 587-589.

**Failure scenario.** The duplicate css-colors.c entries are benign today (meson deduplicates), but the structure means a new core source file added to rofi_sources must be manually threaded into up to twelve object lists; missing one surfaces as an undefined-reference link error in a single test only.

**Proposed fix.** Factor the shared core (theme.c, css-colors.c, rofi-types.c, helper.c, xrmoptions.c, config.c) into a static_library() and link the tests plus the main executable against it, replacing all twelve extract_objects calls.

**Verifier.** The mechanical facts hold: rofi.extract_objects([...]) appears at meson.build:434, 444, 462, 481, 499, 517, 533, 547, 561, 581, 596, 613, and 'source/css-colors.c' is duplicated inside one list at 466/468, 485/487, 503/505, 521/523 and 586/588 (the claim's '587-589' brackets the second occurrence). But the LTO premise is weak: the only -Db_lto=true is .build.yml:36, which builds upstream rofi, not this repo, and the actual GitHub CI at .github/actions/meson/action.yml:22 runs plain `meson setup builddir -Dxcb=... -Dwayland=...` with no LTO. I have no evidence that extract_objects + fat GIMPLE objects actually breaks here; the maintainability point (a new core source must be threaded into twelve lists) is real and is what the claim's own scenario falls back to.

## `source/display.c:85` — display.c dispatches through function pointers the XCB backend never fills in

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

display_get_clipboard_data (source/display.c:85-90) and display_set_fullscreen_mode (source/display.c:92-96) test only `if (proxy)`, never `if (proxy->get_clipboard_data)`. The XCB display_proxy initialiser (source/xcb/display.c:2036-2049) is a designated-initialiser list that omits both `.get_clipboard_data` and `.set_fullscreen_mode`, so both slots are NULL under X11. The only reason this does not crash today is that every call site re-guards on the backend by hand: source/view.c:1018-1023 and 1034-1039 wrap display_get_clipboard_data in `#ifdef ENABLE_WAYLAND` + `if (config.backend == DISPLAY_WAYLAND)`, and display_set_fullscreen_mode is called only from source/wayland/view.c:383. The abstraction therefore provides no isolation: correctness depends on every caller repeating the backend check.

**Failure scenario.** Any new caller — a plugin mode, or moving the PASTE_PRIMARY handling out of the ifdef ladder — calls display_get_clipboard_data() on X11. proxy is non-NULL, proxy->get_clipboard_data is NULL, and the indirect call jumps to address 0.

**Proposed fix.** Either NULL-check each function pointer in source/display.c before calling, or give the XCB proxy no-op/real implementations for both slots so the vtable is total.

**Verifier.** source/display.c:85-96 — display_get_clipboard_data and display_set_fullscreen_mode test only `if (proxy)` before calling proxy->get_clipboard_data / proxy->set_fullscreen_mode. source/xcb/display.c:2036-2049 is a designated initialiser listing setup/late_setup/early_cleanup/cleanup/dump_monitor_layout/startup_notification/monitor_active/set_input_focus/revert_input_focus/scale/view only — both slots are implicitly NULL. Every current caller re-guards by backend (source/view.c:1018-1023, 1034-1039; source/wayland/view.c:383), so no crash today: latent structural hazard.

## `source/modes/combi.c:95` — Dead store: token = NULL in the error branch of the strtok loop

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`g_warning("Invalid script switcher: %s", token); token = NULL;` — the assignment is immediately overwritten by the for-loop's increment expression `token = strtok_r(NULL, sep, &savept)`. The comment above it ("Report error, don't continue.") suggests the intent was to abort the loop, which is not what happens.

**Failure scenario.** n/a — the loop continues either way, so behaviour matches the `continue` case. The misleading dead store just hides whether the author meant to stop parsing after the first bad mode name.

**Proposed fix.** Delete the assignment (or replace it with `break` if aborting was actually intended).

**Verifier.** combi.c:93-95: `// Report error, don't continue.` / `g_warning("Invalid script switcher: %s", token);` / `token = NULL;` — the store is immediately overwritten by the for-loop increment `token = strtok_r(NULL, sep, &savept)` at combi.c:70. Dead store whose comment contradicts the behaviour; no functional effect.

## `source/modes/dmenu.c:510` — dmenu_mode declares cfg_name_key "display-combi"

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

`Mode dmenu_mode = {.name = "dmenu", .cfg_name_key = "display-combi", ...}` — a copy-paste from combi.c:348. Unlike the other modes, dmenu_mode is never passed to rofi_collectmodes_add (see source/rofi.c:676-698), so mode_set_config (source/mode.c:205-209) never overwrites cfg_name_key with the correct "display-dmenu" and never registers the option at all.

**Failure scenario.** `sofi -dmenu -display-dmenu "Pick:"` is silently ignored — the option does not exist for dmenu mode. The stale key also means any code that keys off dmenu_mode.cfg_name_key collides with the combi mode's option name.

**Proposed fix.** Change it to "display-dmenu". If a `display-dmenu` option is actually wanted, dmenu_mode also needs a mode_set_config call on the dmenu path.

**Verifier.** dmenu.c:509-510 `Mode dmenu_mode = {.name = "dmenu", .cfg_name_key = "display-combi", ...}`, identical to combi.c:348. rofi.c:680-698 lists the modes passed to rofi_collectmodes_add and dmenu_mode is not among them (grep over source/rofi.c confirms), so mode_set_config (mode.c:205-209), which would snprintf "display-dmenu" and register the option, never runs for it. dmenu_mode does carry `.display_name = "dmenu"` (dmenu.c:522) so the prompt still renders; the effect is that -display-dmenu is unregistered.

## `source/modes/drun.c:105` — DRunModeEntry.pd and Block.pd are dead, write-only/never-set fields

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

DRunModeEntry declares `DRunModePrivateData *pd;` (drun.c:105) which is never assigned anywhere — read_desktop_file fills every other field but leaves this one holding g_realloc garbage. dmenu's Block declares `DmenuModePrivateData *pd;` (dmenu.c:124) which is assigned at line 132 and never read. grep over source/ and include/ confirms no reader for either.

**Failure scenario.** n/a — dead weight rather than a live defect, but DRunModeEntry.pd is an uninitialized pointer sitting in a struct that gets memmove'd (line 1407) and qsort'd, which will confuse anyone who starts trusting it. Block.pd is also the exact field the out-of-bounds sentinel write clobbers.

**Proposed fix.** Delete both fields.

**Verifier.** drun.c:104-105 `typedef struct { DRunModePrivateData *pd; ...}` — grep for `.pd =` / `->pd =` across source/modes/ returns only dmenu.c:132, so DRunModeEntry.pd is never assigned and holds g_realloc garbage in a struct that is memmove'd and g_qsort_with_data'd. dmenu.c:124 `DmenuModePrivateData *pd;` is assigned at dmenu.c:132 and has no reader anywhere in source/ or include/. Dead weight, not a live defect.

## `source/modes/filebrowser.c:116` — file_browser_config is process-global but the mode is instantiated more than once

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/modes (combi.c, dmenu.c, drun.c, filebrow

Sorting method, sorting time, directories-first and show-hidden live in a single file-scope struct (lines 116-130), yet file_browser_mode declares MODE_TYPE_COMPLETER and _create = create_new_file_browser (lines 741-745), so drun and run each instantiate their own completer copy (drun.c:1438, run.c:528) alongside the standalone filebrowser mode. All instances share and mutate the same state — most visibly file_browser_mode_result's show-hidden toggle at line 562 and file_browser_mode_init_config, which re-reads the theme into the global on every instance init.

**Failure scenario.** Toggle show-hidden inside the standalone filebrowser mode, then use file completion from run mode in the same session: the completer starts with hidden files shown, because the toggle mutated shared global state that has nothing to do with that instance.

**Proposed fix.** Move the four settings into FileBrowserModePrivateData and read them per instance in file_browser_mode_init_config.

**Verifier.** filebrowser.c:116-130 is a file-scope anonymous struct `file_browser_config` holding sorting_method/sorting_time/directories_first/show_hidden. file_browser_mode_init_config (filebrowser.c:369-420) writes into that global on every instance init, and file_browser_mode_result mutates it at filebrowser.c:562 (`show_hidden = !show_hidden`) as does the completer path at filebrowser.c:718. The mode declares `._create = create_new_file_browser` (filebrowser.c:662, 741) and MODE_TYPE_COMPLETER (line 745), and run.c:526 / drun.c:1436 each mode_create+mode_init their own completer instance, so all instances share the same mutable state.

## `source/modes/wayland-window.c:808` — TOPLEVEL_STATE_CLOSED is set on a struct that is freed two lines later, making the 'Window has vanished' branch unreachable

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

wlr_foreign_toplevel_handle_closed sets `self->state = TOPLEVEL_STATE_CLOSED` (line 353), removes self from the list (line 354), and then frees it (line 356). _get_display_value's check `toplevel->state & TOPLEVEL_STATE_CLOSED` (line 808) can therefore never be reached through a live list entry — the only way to reach it would be through the already-freed struct. The TOPLEVEL_STATE_CLOSED enumerator (line 88) exists solely for this dead path.

**Failure scenario.** n/a — dead code. Its presence is misleading: it looks like closed windows are gracefully rendered as 'Window has vanished', which conceals the real behaviour audited in wlr-toplevel-null-on-stale-selection (the entry disappears from the list while the listview index goes stale).

**Proposed fix.** Drop TOPLEVEL_STATE_CLOSED and the line 808 state check, or keep closed toplevels in the list (unlinked from their proxy) until the reload actually happens, which would also fix the stale-index crash.

**Verifier.** source/modes/wayland-window.c:348-357: `self->state = TOPLEVEL_STATE_CLOSED` (353) is immediately followed by g_list_remove (354) and wlr_foreign_toplevel_handle_free (356), so no live list element can ever carry the flag; the `toplevel->state & TOPLEVEL_STATE_CLOSED` half of the test at line 808 is unreachable (the `toplevel == NULL` half is not). The enumerator is defined at line 88 as `1 << 4`. Note one non-obvious way it could be set: line 335 `self->state |= 1 << *elem` would set bit 4 if a compositor ever sent state value 4, which the protocol does not define today.

## `source/rofi-icon-fetcher.c:799` — rofi_icon_fetcher_query() is a verbatim copy of rofi_icon_fetcher_query_advanced()

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/rofi-icon-fetcher.c:752-798 (rofi_icon_fetcher_query_advanced) and 799-844 (rofi_icon_fetcher_query) are 45 lines of identical logic, differing only in that the second passes `size` for both wsize and hsize (lines 813, 825-826 vs 767, 779-780). Both are exported through the installed public header include/rofi-icon-fetcher.h:38,54, so any future fix (e.g. the wsize<=0 guard from the g_object_unref(NULL) finding, or making the surface publication atomic) has to be applied twice and will inevitably drift.

**Failure scenario.** n/a - maintenance hazard: a fix applied to one entry point silently leaves the other broken, and both are reachable (icon.c:180 uses the simple form, view.c uses the advanced form).

**Proposed fix.** Make rofi_icon_fetcher_query() a one-line wrapper: `return rofi_icon_fetcher_query_advanced(name, size, size);` at source/rofi-icon-fetcher.c:799-844.

**Verifier.** source/rofi-icon-fetcher.c:752-798 (rofi_icon_fetcher_query_advanced) and 799-844 (rofi_icon_fetcher_query) are the same ~45-line body, the second differing only by using `size` for both wsize and hsize (lines 813, 825-826 vs the advanced form's separate wsize/hsize). Both are in the installed public header — include/rofi-icon-fetcher.h:38 and :54, and meson.build:195-203 confirms rofi-icon-fetcher.h is installed — so any fix genuinely has to be made twice. Maintenance hazard only.

## `source/rofi-icon-fetcher.c:799` — rofi_icon_fetcher_query() is a near-verbatim copy of rofi_icon_fetcher_query_advanced()

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** helper / history / icon-fetcher / display / test

Lines 799-844 duplicate lines 752-798 essentially character for character; the only difference is that `size` is used for both wsize and hsize. Two copies of the cache-lookup, entry-creation, uid assignment, list-prepend, hash-insert and pool-push logic means every fix (the surface leak at line 746, the missing synchronisation, the uid handling) has to be applied twice and can drift.

**Failure scenario.** Any of the concurrency or lifetime fixes above gets applied to one copy and not the other — e.g. adding a mutex around the query_started check at line 769 but not at line 815 — leaving the single-size query path (the one used by drun and script mode) still racy while the advanced path is fixed.

**Proposed fix.** Make rofi_icon_fetcher_query(name, size) a one-line forward to rofi_icon_fetcher_query_advanced(name, size, size) and delete the duplicate body.

**Verifier.** source/rofi-icon-fetcher.c:752-798 and :799-844 are line-for-line the same logic (cache lookup, entry creation, uid assignment at 778/824, list prepend 787/833, hash insert 788/834, pool push 795/841); the only difference is size used for both wsize and hsize. The divergence risk is concrete — the unguarded `if (!sentry->query_started) g_thread_pool_push(...)` exists twice, at :769-771 and :815-817.

## `source/rofi.c:1143` — rasi_theme_file_extensions is extern-declared inside a function body

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`extern const char *rasi_theme_file_extensions[];` appears in the middle of main(). The array is actually defined in lexer/theme-lexer.l:56 and used there at :437 and :918. No header declares it, so the type is asserted independently in two places with no compiler cross-check — precisely what -Werror=missing-prototypes (meson.build flags list) exists to prevent for functions.

**Failure scenario.** Change the array to `const char *const rasi_theme_file_extensions[]` in the lexer and rofi.c still compiles against the stale declaration, producing a link-time or (with LTO) an -Werror=lto-type-mismatch failure that meson.build:38 turns into a hard error.

**Proposed fix.** Declare it once in include/theme.h (or helper.h next to helper_get_theme_path) and include that.

**Verifier.** source/rofi.c:1143 is `extern const char *rasi_theme_file_extensions[];` inside main(), and the array is defined at lexer/theme-lexer.l:56 as `const char *rasi_theme_file_extensions[] = {".rasi", ".rasinc", NULL};`. No header declares it, so the two type assertions are never cross-checked by the compiler. Style/robustness, not a live bug.

## `source/view.c:1010` — Backend-specific code hand-inlined in the backend-agnostic view.c, bypassing the display_proxy abstraction

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

source/view.c is the shared layer that is supposed to reach the backends only through `proxy` (declared line 65, dispatched at 2143-2200). In practice it contains raw xcb calls guarded by `#ifdef ENABLE_XCB` + `if (config.backend == DISPLAY_XCB)`: lines 361-366 (xcb_clear_area/xcb_flush), 1010-1016 and 1026-1032 (xcb_convert_selection), 1050-1056 (xcb_stuff_set_clipboard/xcb_set_selection_owner), 1904-1915 (xcb_map_window/xcb_flush), 1924-1928 and 1981-1985 (sn_launchee_context_complete), 1973-1978. It also includes xcb-internal.h/xcb.h directly (lines 58-63) and falls back to include/xcb-dummy.h's fake `typedef int xcb_window_t` when XCB is disabled so that CacheState.main_window and the view_proxy signatures still compile. The result is that the display/view proxy indirection buys almost nothing: adding a backend still means editing view.c.

**Failure scenario.** Not a runtime crash today. The concrete cost is the bug above (copy-to-clipboard-silent-noop-wayland at line 1060) and the NULL vtable slots: whenever a feature is added to one ladder arm and not the other, the missing arm degrades silently instead of failing to compile.

**Proposed fix.** Move each of these into display_proxy methods (map_window/flush/startup_complete/set_clipboard) so that adding a backend requires filling a vtable rather than auditing ifdefs, and keep xcb-dummy.h out of shared headers.

**Verifier.** Verified each citation in source/view.c: 58-63 includes xcb-internal.h/xcb.h or falls back to xcb-dummy.h; 361-366 xcb_clear_area/xcb_flush under `#ifdef ENABLE_XCB` + backend test; 1010-1016 and 1026-1032 xcb_convert_selection; 1050-1056 xcb_stuff_set_clipboard/xcb_set_selection_owner; 1904-1915 xcb_map_window/xcb_flush (guarded on xcb->connection); 1924-1928 and 1981-1985 sn_launchee_context_complete; 1973-1978 xcb_map_window. The backend-agnostic layer does contain raw backend code, exactly as described. Structural, no runtime defect of its own.

## `source/view.c:764` — Every listview page change destroys and rebuilds the whole GThreadPool

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** core: source/rofi.c, source/view.c, source/mode.

`page_changed_callback()` is just `rofi_view_workers_finalize(); rofi_view_workers_initialize();`. listview calls it from its scroll-offset computation on every page transition (source/widgets/listview.c:289-292), i.e. while the user is scrolling. workers_finalize uses `g_thread_pool_free(tpool, TRUE, FALSE)` (:2053) — immediate, no wait — so any still-queued filter job is dropped and running workers are not joined, and workers_initialize then spawns a fresh pool.

**Failure scenario.** Hold Down in a list longer than one page in a large mode (drun with thousands of entries): each page boundary tears down and recreates a pool of up to 128 threads. Visible scroll stutter, and any filter job still queued from a concurrent refilter is silently discarded, leaving state->filtered_lines short of the real match count.

**Proposed fix.** The pool does not need to be recycled on page changes at all — drop page_changed_callback or replace it with the icon-prefetch work it was presumably meant to trigger.

**Verifier.** source/view.c:764-767 is exactly `page_changed_callback(){ rofi_view_workers_finalize(); rofi_view_workers_initialize(); }`, registered at :1740 and invoked from source/widgets/listview.c:290-292 inside the scroll-offset computation on every page transition; finalize is `g_thread_pool_free(tpool, TRUE, FALSE)` at :2053. The teardown/rebuild per page is real. The second half of the scenario is wrong: filtering is fully synchronous — rofi_view_refilter_real pushes jobs at :840 and then blocks on `while (count > 0) g_cond_wait(&cond,&mutex)` at :845-851 before returning — so the pool is always idle when page_changed_callback runs from the main thread; no queued job can be dropped and filtered_lines cannot come up short. Downgraded to low (needless churn, not correctness).

## `source/wayland/view.c:552` — The Wayland view_proxy leaves three vtable slots NULL and source/view.c dispatches them unguarded

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** wayland backend: source/wayland/view.c, source/m

source/wayland/view.c sets `.temp_configure_notify = NULL` (line 552), `.temp_click_to_exit = NULL` (line 553) and `.get_window = NULL` (line 570). The corresponding thunks in source/view.c — rofi_view_temp_configure_notify (2147-2150), rofi_view_temp_click_to_exit (2152-2154) and rofi_view_get_window (2192) — call through the pointer with no check. The signatures themselves are X11 shapes (xcb_configure_notify_event_t*, xcb_window_t) that only exist on Wayland builds because include/xcb-dummy.h typedefs them to `int` (include/xcb-dummy.h:8-10). Today all callers happen to be X11-only (source/xcb/display.c:1241,1252,1264,1349 and source/modes/window.c:825), so nothing crashes, but the vtable advertises a contract it does not honour.

**Failure scenario.** Any Wayland-reachable code path that calls rofi_view_get_window() — e.g. a plugin using the installed view API, or moving window-mode's `{window}` handling into shared code — dereferences a NULL function pointer at source/view.c:2192.

**Proposed fix.** Split the X11-only entry points out of view_proxy into an xcb-specific interface, or supply stubs on the Wayland side and drop include/xcb-dummy.h's fake xcb types from the shared header.

**Verifier.** source/wayland/view.c:552 `.temp_configure_notify = NULL`, 553 `.temp_click_to_exit = NULL`, 570 `.get_window = NULL`. source/view.c:2147-2154 and 2192 call through those pointers unguarded. include/xcb-dummy.h:8-10 does typedef xcb_configure_notify_event_t/xcb_window_t/xcb_timestamp_t to int so the X11-shaped signatures compile on Wayland-only builds. No current Wayland-reachable caller, so latent.

## `source/widgets/scrollbar.c:53` — scrollbar_scroll_get_line() reads RofiDistance internals instead of converting to pixels

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** source/widgets/*.c + include/widgets/*.h (box, c

source/widgets/scrollbar.c:52-53:
```
guint scrollbar_scroll_get_line(const scrollbar *sb, int y) {
  y -= sb->widget.border.top.base.distance;
```
Every other consumer of a border/padding goes through `distance_get_pixel(..., ROFI_ORIENTATION_VERTICAL)` (e.g. source/widgets/widget.c:604) or widget_padding_get_top(). Here the raw `base.distance` field is used, which is the numeric literal in whatever unit the theme wrote (em, ch, %, mm) rather than a pixel count, and the border's modtype/calc expression is ignored entirely.

**Failure scenario.** `scrollbar { border: 1em; }` on a 20px font: distance_get_pixel would give 20, but this code subtracts 1, so every click on the scrollbar maps to a row ~19px off, and with `border: 5%` the offset is wrong by an order of magnitude.

**Proposed fix.** Use `y -= widget_padding_get_top(WIDGET(sb));` (or distance_get_pixel(sb->widget.border.top, ROFI_ORIENTATION_VERTICAL)) at source/widgets/scrollbar.c:53.

**Verifier.** source/widgets/scrollbar.c:52-53 is `guint scrollbar_scroll_get_line(const scrollbar *sb, int y) { y -= sb->widget.border.top.base.distance;`. include/rofi-types.h:113-134 shows base is a RofiDistanceUnit whose `distance` is a raw double in the authored unit, with separate `type`, `modtype`, `left`, `right` fields for units and calc expressions — all ignored here. The correct conversion is what every other consumer does, e.g. source/widgets/widget.c:598-606 widget_padding_get_top() which runs padding+border+margin through distance_get_pixel(). So any non-px border unit (em/ch/%/mm) or calc mis-maps scrollbar clicks to rows; it also silently drops padding/margin.

## `source/xcb/display.c:1580` — Atom interning does one blocking round trip per atom instead of pipelining

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

x11_create_frequently_used_atoms() issues xcb_intern_atom() and immediately blocks on xcb_intern_atom_reply() inside the same loop iteration (lines 1581-1584), so the 11 atoms in EWMH_ATOMS cost 11 sequential server round trips on rofi's startup path. Three of those atoms are dead weight: `I3_SOCKET_PATH`, `_XROOTPMAP_ID` and `_NET_WM_WINDOW_OPACITY` (include/xcb.h:90-91) are interned but referenced nowhere in source/ or include/ — I3_SOCKET_PATH is a leftover of a removed i3 IPC workaround.

**Failure scenario.** Over a high-latency X connection (ssh -X, remote/nested X server at ~20ms RTT) this adds ~220ms of pure latency to every rofi invocation for a program whose whole design goal is instant appearance.

**Proposed fix.** Issue all NUM_NETATOMS cookies in one loop, then collect all replies in a second loop; drop the three unused atoms from EWMH_ATOMS.

**Verifier.** source/xcb/display.c:1578-1589: the loop issues xcb_intern_atom() at 1581 and blocks on xcb_intern_atom_reply() at 1583 in the same iteration — no cookie batching. NUM_NETATOMS is 11 (include/xcb.h:90-92). The dead-atom sub-claim checks out: grep over source/ and include/ finds I3_SOCKET_PATH, _XROOTPMAP_ID and _NET_WM_WINDOW_OPACITY only in the X-macro list at include/xcb.h:90-91, never as netatoms[...] uses.

## `source/xcb/display.c:1498` — take_pointer()/take_keyboard() call exit() directly, bypassing all cleanup

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

Both grab helpers call exit(EXIT_FAILURE) on a connection error (lines 1496-1499 and 1525-1528) rather than returning failure to the caller. They are invoked from xcb_display_late_setup() and from the lazy-grab GSource callbacks (lines 1860, 1874), so this kills the process from inside a main-loop callback: xcb_display_cleanup(), rofi_view_cleanup() and input_history_save() (view.c:956) never run.

**Failure scenario.** The X server goes away while rofi is retrying the lazy grab: the process exits from inside a g_timeout callback, the input history accumulated in this session is discarded (input_history_save is never reached) and X resources/grabs are left to the server to reap.

**Proposed fix.** Return 0 from the helpers and let the callers (which already handle a false return) quit the main loop via g_main_loop_quit(xcb->main_loop), matching lazy_grab_keyboard()'s behaviour at line 1871.

**Verifier.** source/xcb/display.c:1496-1499 and 1525-1528 both do `g_warning("Connection has error"); exit(EXIT_FAILURE);` inside the retry loop. Callers include the GSource callbacks lazy_grab_pointer (line 1860) and lazy_grab_keyboard (line 1874) plus xcb_display_late_setup (1896-1907). No atexit() handler is registered anywhere in source/, and input_history_save() is called only from source/xcb/view.c:956 in the normal cleanup path, so exit() from a timeout callback does discard it.

## `source/xcb/display.c:1765` — SUBSTRUCTURE_NOTIFY selected on the root window even when window mode is compiled out

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

xcb_display_setup() unconditionally selects XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY on the root window (lines 1764-1766). The only consumer of the resulting CreateNotify/DestroyNotify events is the `#ifdef WINDOW_MODE` block in main_loop_x11_event_handler_view() (lines 1239-1258). It also replaces this client's whole root event mask rather than adding to it, and the two lines are indented with tabs, inconsistent with the clang-format style used throughout the file.

**Failure scenario.** With -Dwindow-mode=false (or when only running dmenu mode), rofi still wakes up and runs the full event handler for every window creation and destruction on the display — measurable churn on a busy desktop for events that are then discarded.

**Proposed fix.** Guard the change_window_attributes call with `#ifdef WINDOW_MODE`, and re-run clang-format on the block.

**Verifier.** source/xcb/display.c:1764-1766 unconditionally sets `uint32_t val[] = {XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY};` via xcb_change_window_attributes on the root window, and the two lines are tab-indented unlike the rest of the file. The only consumers of the resulting Create/DestroyNotify for other windows are the `#ifdef WINDOW_MODE` blocks at 1242-1244 and 1253-1255; rofi's own window gets its DestroyNotify/ConfigureNotify from XCB_EVENT_MASK_STRUCTURE_NOTIFY selected on the window itself (source/xcb/view.c:617), so nothing else needs the root mask. The 'replaces rather than adds' sub-point is inconsequential — this is the only xcb_change_window_attributes on the root in the whole tree.

## `source/xcb/view.c:152` — Unused non-static global `do_bench`

- **kind** structure · **severity** low · **verdict** CONFIRMED · **domain** xcb backend (source/xcb/display.c, source/xcb/vi

`gboolean do_bench = TRUE;` is defined at file scope and, per a grep over the whole tree, is read nowhere — bench_update() gates on config.benchmark_ui instead (line 169). It is not static, so it is emitted as a global symbol.

**Failure scenario.** n/a — dead code; it exports a generic symbol name from the binary and misleads readers into thinking benchmarking is toggled by it.

**Proposed fix.** Delete the definition.

**Verifier.** source/xcb/view.c:152 `gboolean do_bench = TRUE;` at file scope, non-static. grep for do_bench across all .c/.h in the tree returns that definition and nothing else. bench_update() gates on config.benchmark_ui at line 169.

