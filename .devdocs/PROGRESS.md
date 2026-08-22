# PROGRESS

Macro progress tracking. Completed, superseded, removed and archived items.
Most recent at the top.

---

## 2026-08-22 20:14 — Phase 6 (early): bundled themes removed, README scrubbed

USER directive: drop the inherited theme collection in favour of the supplied config, and
scrub the README of external links and inherited references. Build clean, **19/19 tests**,
zero project-code warnings.

### Themes: 37 files removed, 2 shipped

`themes/` is gone entirely (556 KB, 35 themes + `iggy.jpg` + `breaking-themes/`). Those were
upstream rofi's theme collection, none of it sofi's.

`sofi-config/` is now the single shipped theme **and** the compiled-in default:

- `doc/default_theme.sasi` regenerated from `sofi-config/config.sasi` with the colour
  variables **inlined** — an `@import` cannot resolve from inside a GResource, so the
  compiled-in copy has to be self-contained.
- `doc/default_configuration.sasi` regenerated from the file's `configuration {}` block.
- `meson.build:380-384` installs `config.sasi` + `colors-default.sasinc` to
  `$datadir/sofi/themes/`, replacing the 36-entry install list.

Verified: `sofi -no-config -dump-theme` succeeds with **zero stderr**, so the theme is
genuinely live with no config file present.

### `colors-default.sasi` → `.sasinc`

`script/sofi-theme-selector:92` globs `${TD}/*.sasi`, so the colour file would have been
offered as a selectable theme and produced a broken selection. It is an *include*, which is
exactly what `.sasinc` means. Renamed, and the import changed to extension-less
`@import "colors-default"` so it resolves through the extension array and survives any
future extension change — the failure mode recorded against R3.

### README scrubbed

External links reduced from 24 to **4**, all of them MIT attribution
(rofi, simpleswitcher, superswitcher, lbonn).

Removed: shields.io badges, upstream's demo video, the `## Screenshots` section (pointed at
`releasenotes/`, deleted under R12), `## Wiki` and its seven sub-links, `## Discussion
places`, and the starchart.cc graph — none of which exist for this fork. Config/theme links
now point at local files (`CONFIG.md`, `doc/sofi-theme.5.markdown`) rather than a web tree.
Table of contents trimmed to sections that still exist. A new Themes section documents
copying the shipped theme.

### Corrections to inherited docs

- **`INSTALL.md` claimed `pkg install sofi`** on FreeBSD, plus openSUSE and MacPorts
  packages. No distribution packages sofi. Replaced with an honest "not yet packaged"
  note and the actual FreeBSD build-dependency list, calling out `bison` explicitly since
  base `byacc` cannot build the GLR grammar. *(This lands part of Phase 4 early.)*
- **`INSTALL.md:28`** documented an autotools `--disable-check` flag; meson uses
  `-Dcheck=disabled`.
- **`.github/` templates** pointed at `DaveDavenport/*/wiki` pages that do not exist;
  repointed or replaced with manpage references.
- **`.gitattributes`** still had a `releasenotes export-ignore` rule for a deleted directory.

Legitimate external links were left alone: `freedesktop.org` spec references in
`doc/sofi.1.markdown`, and the Wikipedia/BibTeX links inside the generated
`doc/sofi.doxy.in`.

### Install layout now

24 files, down from 57 — the difference is the 35 removed themes.

---

## 2026-08-22 20:02 — Phase 3 complete: the rename

**sofi is now sofi.** 174 files changed, +3837/−3841, 64 renamed. Clean rebuild,
**19/19 tests**, all 12 sofi tests ASAN-clean, **zero project-code warnings**.

### All ten invariants pass

| Invariant | Result |
|---|---|
| 3a binary is `sofi` | PASS |
| 3b no rofi filenames | PASS |
| 3c no `rofi_`/`ROFI_`/`Rofi` in any C code | PASS |
| **3c2 `COPYING` md5 unchanged** | **PASS** (`166cdc06…`) |
| **3c3 copyright notices still exactly 90** | **PASS** |
| 3d no `"rofi` string literals in `source/`/`config/` | PASS |
| 3e no `.rasi`/`.rasinc` files remain | PASS |
| 3f pkg-config module is `sofi` | PASS |
| 3g no upstream URLs outside frozen docs | PASS |
| build + 19/19 tests | PASS |

### RR4 — the attribution risk — did not materialise

The bulk rename ran as a scripted transform with a protection regex
(`Copyright|sean\.pringle|qball@|gmpclient|Sean Pringle|Qball`) that skipped **91 lines**
across the tree. Copyright lines were snapshotted before the rename and diffed after:
byte-identical. `COPYING` md5 unchanged. 86 files still carry the full permission notice.

The project-title line `* rofi` → `* sofi` in 68 file headers *was* changed — correctly, as
those files are now part of sofi — while the `Copyright ©` lines beneath were not touched.

### Sub-phases

- **3a** `meson.build:1` `project('sofi')`. The three things that do not follow a project
  rename were edited by hand as predicted: `executable()`, `pkg.generate(filebase/name)`,
  and every literal filename in the install lists. `PACKAGE_BUGREPORT`/`PACKAGE_URL`
  repointed to `orpheus497/sofi`.
- **3b** 30 files `git mv`'d.
- **3c** 335 identifiers across 94 files, then a **second pass** for 9 more files — see
  "Two passes were needed" below. Plus `ABI_VERSION` already bumped in Phase 1.
- **3d** Config/theme/script paths → `sofi/`; cache files → `sofi3.druncache`,
  `sofi-4.runcache`, `sofi-2.sshcache`, `sofi3.filebrowsercache`,
  `sofi-drun-desktop.cache`, `sofi-entry-history.txt`; pidfile → `sofi.pid`;
  env vars → `SOFI_RETV`/`SOFI_OUTSIDE`/`SOFI_INFO`/`SOFI_DATA`/`SOFI_INPUT`/
  `SOFI_PLUGIN_PATH`/`SOFI_PNG_OUTPUT`. Hard break per R2/R4, no fallbacks.
- **3e** 39 theme/config files → `.sasi`/`.sasinc`; extension array, gresource aliases,
  `config.sasi`, `sofi.sasi`, `-sasi-validate`.
- **3f** WM_CLASS `"sofi\0Sofi"`, layer-shell namespace and xdg title/app_id `"sofi"`,
  desktop files with `StartupWMClass=Sofi` added to match R5, helper scripts,
  `sofi.pc` / `$libdir/sofi`.
- **3g** Docs, examples, issue templates. `README.md` hand-rewritten (see below).
  `.build.yml` replaced with a real FreeBSD job per R14.

### Two passes were needed for 3c

The first pass used `\b`-anchored patterns and missed 48 identifiers where `rofi_` is
preceded by an underscore — `wayland_rofi_view_*`, `xcb_rofi_view_*`,
`__rofi_view_state_create`, `int_rofi_theme_print_property`, `INCLUDE_ROFI_TYPES_H`.
`\b` does not match between `_` and `r` because both are word characters. A second
unanchored pass caught them.

**This is why the invariant is a grep over the whole tree and not a trust in the script.**
An initial invariant check also false-passed because `git grep` rejected the `\b` regex and
the `||` branch fired; re-run with `-E` it correctly reported 7 remaining files.

### Deliberate exceptions — things NOT renamed

- **`themes/gruvbox-*` `Source: https://github.com/bardisty/gruvbox-rofi`** (7 files).
  Third-party source attribution for where those themes came from. Rewriting it would
  falsify their provenance.
- **`README.md` mentions of rofi** (6). All intentional: the fork-provenance paragraph
  required by R12, and two references that genuinely mean upstream rofi.
- **`mkdocs/`** — frozen historical docs, untouched per R7 (the per-version trees were
  already deleted).
- **`.devdocs/`** — this workspace is process history; rewriting it would falsify the record.

### README rewritten by hand

The bulk substitution produced `davatorium/sofi` — correct name, wrong org — and left an
upstream release-history list (1.7.0–2.0.0) for releases sofi never made. Both fixed.
Added, per R12 and the R2/R4 "make it loud" requirement:

- Explicit fork provenance crediting Dave Davenport (Qball), Sean Pringle (simpleswitcher)
  and lbonn (Wayland), pointing at `COPYING`.
- A blockquote stating plainly that sofi is **not** a drop-in replacement: it does not read
  rofi's config, themes, cache or env vars, and rofi plugins will not load.

### Incidental fixes found during the rename

- **Two gresource prefix mismatches.** Renaming `/org/qtools/rofi` → `/org/sofi` in
  `resources/resources.xml` desynced from `source/sofi.c` (which the script had made
  `/org/qtools/sofi`) and from `lexer/theme-lexer.l:411`, which my first `sed` did not
  cover. Both caught and aligned; a mismatch would have made the built-in default theme
  fail to load at runtime with no build error.
- **Stale `extern` declaration.** `source/sofi.c:1143` declared
  `extern const char *rasi_theme_file_extensions[]` locally, so 3e's rename produced a link
  error — `undefined symbol: rasi_theme_file_extensions`. Caught by the build.
- **Test expectations updated.** `test/theme-parser-test.c:1274,1279,1328,1330` hardcoded
  `.rasi` paths. With `.rasi` no longer a recognised extension the resolver appends
  `.sasinc`, producing `/not-existing-file.rasi.sasinc`. The new behaviour is correct; the
  expectation was stale. Updated to `.sasi`.

### Install layout verified

A `DESTDIR` staging install was run: **57 files, zero `rofi`-named**.

```
/usr/local/bin/sofi, sofi-sensible-terminal, sofi-theme-selector
/usr/local/include/sofi/{mode,mode-private,helper,sofi-types,sofi-icon-fetcher}.h
/usr/local/libdata/pkgconfig/sofi.pc   (Name: sofi, pluginsdir=/usr/local/lib/sofi/)
/usr/local/share/applications/sofi.desktop, sofi-theme-selector.desktop
/usr/local/share/icons/hicolor/scalable/apps/sofi.svg
/usr/local/share/man/man1/sofi{,-sensible-terminal,-theme-selector}.1
/usr/local/share/man/man5/sofi-{actions,debugging,dmenu,keys,script,theme,thumbnails}.5
/usr/local/share/sofi/themes/*.sasi (+ gruvbox-common.sasinc)
```

---

## 2026-08-22 19:52 — Phase 2b complete: xdg-shell fallback

sofi now runs on compositors without `zwlr_layer_shell_v1` — Mutter (GNOME) and KWin
(Plasma). Clean rebuild, **19/19 tests**, all 12 sofi tests ASAN-clean, zero project-code
warnings, 20 xdg symbols linked.

Before this, layer shell was mandatory: Phase 2a made its absence a clean error instead of a
`SIGABRT`, and this phase makes it not an error at all.

### Design

Layer shell stays **preferred** — it can position and size itself, which xdg-shell cannot.
xdg-shell is a fallback selected only when layer shell is absent. A new
`wayland_shell_kind` enum (`include/wayland-internal.h:23-33`) records the choice once at
setup, and the four functions that drive the shell branch on it.

| Component | Detail |
|---|---|
| `wayland_shell_kind` | `NONE` / `LAYER` / `XDG`, plus `xdg_wm_base`, `xdg_surface`, `xdg_toplevel` in `wayland_stuff` |
| Registry bind | `xdg_wm_base` at version ≤2, `WAYLAND_GLOBAL_XDG_WM_BASE` added to the globals enum and to the `global_remove` switch |
| **ping/pong** | `xdg_wm_base.ping` → `xdg_wm_base_pong`. Mandatory — an unanswered ping makes the compositor kill the client as unresponsive |
| `xdg_surface.configure` | acked immediately, as the protocol requires |
| `xdg_toplevel.configure` | adopts width/height only when positive (0 means "choose your own") and feeds the same `layer_width`/`layer_height` the layer-shell configure does, so `display_get_surface_dimensions()` is unchanged |
| `xdg_toplevel.close` | hides the view and quits the main loop |
| Selection | `source/wayland/display.c:1917-1929` — layer shell, else xdg, else fail with a clear message |
| `late_setup` | branches at `:1962`; the xdg arm creates surface + toplevel, sets title/app_id |
| `display_set_surface_dimensions` | returns early at `:2091` for xdg after recording the size and setting window geometry — it does **not** pretend anchors/margins applied |
| `set_fullscreen_mode` | `xdg_toplevel_set_fullscreen()` vs the layer-shell exclusive-zone path |
| `wayland_surface_destroy` | tears down toplevel then xdg_surface, reverse of creation |

### The screen-size problem, and how it is handled

The layer-shell path learns the usable screen size from a trick: anchor to all four corners
with size 0 and read the resulting configure (`source/wayland/display.c:1971-1980`).
xdg-shell has no equivalent — a toplevel is never told the screen size.

Without a fix, `layer_width` stays 0, `display_get_surface_dimensions()` returns FALSE, and
every consumer silently falls back to hardcoded 1920x1080. The xdg arm therefore seeds the
dimensions from the selected output, falling back to *any* output with a valid mode when
`config.monitor` matched nothing (`source/wayland/display.c:2015-2038`). Verified that
`wayland_output_done` commits `pending`→`current` (`:1466-1469`) and that
`wayland_display_setup` performs an explicit second roundtrip to wait for output information,
so the data is populated by the time `late_setup` runs.

### Protocol ordering verified

xdg-shell requires: create surface → create xdg_surface → create toplevel → commit **with no
buffer** → await configure → ack → only then attach a buffer. The existing tail of
`late_setup` (`wl_surface_commit` then `wl_display_roundtrip`) already provides exactly that
sequence, and the roundtrip is what delivers the first configure to the new handler.

### Documented, not faked

`README.md` gains a "Shell protocols on Wayland" section stating plainly that under
xdg-shell `location`/`anchor`/`x-offset`/`y-offset` have no effect, keyboard interactivity
cannot be forced, `click-to-exit` cannot capture outside clicks, and `wayland-layer` is
ignored. Making anchors *appear* to work would be worse than the limitation.

### Notes for later phases

- Two new brand literals introduced: `xdg_toplevel_set_title(..., "rofi")` and
  `..._set_app_id(..., "rofi")` (`source/wayland/display.c:2012-2013`). Left as `"rofi"`
  deliberately, for consistency with the layer-shell namespace at `:1978` which is also
  still `"rofi"`. **Verified both are caught by the Phase 3d grep invariant**
  (`git grep -n '"rofi' -- source/ config/`). The app_id must end up matching
  `StartupWMClass` in the desktop file, same constraint as R5.
- xdg-shell does not solve window mode on KWin/Mutter: that needs
  `zwlr_foreign_toplevel_management_v1`, which neither implements. Window mode remains
  wlroots-only. Only the launcher itself gains portability here.

### Not verified

**No compositor was available to test against.** Everything above is verified by build, test
suite, ASAN and protocol reading — not by running. The xdg path has never actually been
executed. It needs a run under Mutter or KWin (and a regression run under sway to confirm the
layer-shell path still wins when both are advertised) before it can be called working.

---

## 2026-08-22 19:37 — Phase 2a complete: Wayland / layer-shell correctness

All 13 items done. Clean rebuild, **19/19 tests pass**, all 12 sofi tests pass under ASAN,
**zero project-code compiler warnings**.

### Startup path — sofi no longer aborts on unsupported compositors

| Site | Change |
|---|---|
| `source/wayland/display.c:1490` | seat below min version: `g_error` → `g_warning` + skip that global only |
| `source/wayland/display.c:1505` | output below min version: same |
| `source/wayland/display.c:1833` | missing compositor/shm/output/seat: `g_error` → `g_warning`, and the message now names *which* one is missing |
| `source/wayland/display.c:1840` | missing layer shell: `g_error` → `g_warning`, returns FALSE |
| `source/wayland/display.c:1860` | `late_setup()` now re-checks `compositor`/`layer_shell`, checks the surface, and returns FALSE honestly instead of unconditional TRUE |

`source/rofi.c:1280-1298` already contained the friendly "No valid backend was found"
message; it was unreachable because `g_error()` calls `abort()`. It now runs. Same for
`-h`/`--help`, which sits after `display_setup()` at `source/rofi.c:1264` and was therefore
also unreachable on a compositor without layer shell.

### Buffer pool

- **Fixed shm name removed.** New `wayland_shm_alloc_fd()` helper uses `SHM_ANON` on FreeBSD
  (the native anonymous mechanism, no name to collide on) and a per-pid unique name with
  retry + immediate unlink elsewhere. This is the one place in the tree where a
  platform conditional is genuinely warranted.
- **Overflow guards** on `stride * height`, on `size * buffer_count`, and against
  `INT32_MAX` — `wl_shm_pool_create_buffer` takes the size and per-buffer offsets as
  `int32_t`, so an oversized pool would have been silently truncated into a wrong offset.
- Rejects non-positive dimensions up front.
- `display_buffer_pool_get_next_buffer()` NULL-guards its argument, and the sole caller
  (`source/wayland/view.c:428`) now checks `display_buffer_pool_new()` and skips the frame
  with a warning instead of dereferencing NULL.
- `close()` → `g_close()` on the mmap failure path for consistency.

### Input and lifecycle

- **Key-repeat use-after-free eliminated by design change.** `wayland_seat.repeat.source`
  was a borrowed `GSource *` from `g_main_context_find_source_by_id()`; GLib destroys and
  unrefs the source when a callback returns `G_SOURCE_REMOVE`, leaving it dangling for the
  four later `g_source_destroy()` sites. Field is now `guint source_id`
  (`include/wayland-internal.h:97-103`), cleared on every `G_SOURCE_REMOVE` path, cancelled
  through a new `wayland_key_repeat_cancel()` helper.
- **`repeat_info` rate == 0 now means disabled**, per the protocol. Previously fell through
  to a 30 ms default and repeated at ~33 Hz. Also guards a division that could yield 0 ms.
- **Keymap handler leaks fixed** (`source/wayland/display.c:436`). Every path now
  `munmap()`s the mapping and `close()`s the fd — both leaked on *every* keymap event.
  Additionally `nk_bindings_seat_update_keymap()` takes its own references
  (`xkb_keymap_ref`/`xkb_state_ref` at `subprojects/libnkutils/bindings/src/bindings.c:1005-1006`),
  so the local keymap and state are now unreffed too; they leaked as well. Error paths
  cleaned up. `fprintf(stderr, ...)` → `g_warning` to match the rest of the backend.
- **Seat capability bug** (`source/wayland/display.c:1223`): the branch releasing the
  keyboard tested `WL_SEAT_CAPABILITY_POINTER` instead of `WL_SEAT_CAPABILITY_KEYBOARD`.

### Sizing

- `wayland_rofi_view_calculate_window_width()` and the fullscreen branch of
  `..._calculate_window_height()` now read the **cached output size** via
  `wayland_rofi_view_get_current_monitor()` rather than the live layer-surface size.
  `display_set_surface_dimensions()` overwrites `layer_width`/`layer_height` with the *menu*
  size on every resize, so with `-no-click-to-exit` each successive view was sized as a
  percentage of the previous menu — halving every time.
- `rofi_get_offset_px()` seeds the theme lookup with `config.x_offset` / `config.y_offset`,
  matching `source/xcb/view.c:358-361`. `-x-offset` and `-y-offset` were silently discarded
  on Wayland.

### Window mode

- Both `g_list_nth_data()` results in `wayland_window_mode_result()` are NULL-checked and
  return `RELOAD_DIALOG`. The list shrinks synchronously on window close while the view
  reload is coalesced into a ~66 ms timer, so the highlighted row can outlive its entry.
- `pd->wayland->last_seat` NULL-checked before activation.
- `wlr_foreign_toplevel_manager_finished()` now clears `pd->manager`. It destroyed the proxy
  but left the pointer set, so teardown called `_stop()` on freed memory.
- `wayland_window_private_free()` reordered: stop the manager and drain the queue *before*
  freeing the toplevel list, not after.
- `get_wayland_window()` returns `gboolean` and `wayland_window_mode_init()` propagates it.
  Previously a compositor without wlr-foreign-toplevel-management got a warning plus a
  permanently empty list; now `source/rofi.c:202-210` shows a proper error dialog.

### Verified invariants

```
git grep -nE 'g_error\('     -- source/wayland source/modes/wayland-window.c  → none
git grep -n 'rofi-wayland-surface' -- source/                                 → none
git grep -nE 'repeat\.source[^_]|find_source_by_id' -- source/wayland         → none
```

### Incidental correction

`g_clear_handle_id()` was used first, then replaced: it expands to `_Static_assert`, which is
C11, and this project builds `c_std=c99` (`meson.build:6`). It produced
`-Wc11-extensions` warnings. Replaced with an explicit `wayland_key_repeat_cancel()` helper.

### Not changed, deliberately

`wlr_toplevels_set_one_identifier()` (`source/modes/wayland-window.c:143`) is a `do/while`
that would dereference NULL on an empty list. Left alone: the only caller
(`source/modes/wayland-window.c:589`) is reached from a toplevel that lives in that list, so
it cannot be empty. The correlation heuristic's fragility is already documented in the code
at `:581-588` and remains a known design limitation, not a defect to patch blindly.

---

## 2026-08-22 19:26 — Phase 0 and Phase 1 complete; Q7/Q13 deletions executed

### Phase 0 — Baseline: GREEN

`devel/bison` (GNU Bison 3.8.2) and `check` (0.15.2) were installed by the USER, clearing the
blocker. Baseline established on FreeBSD 15.1-RELEASE:

| Step | Result |
|---|---|
| `meson setup build` | OK — 62 targets |
| `ninja -C build` | OK |
| `meson test -C build` | **19/19 pass** |

**Warning audit.** 787 compiler warnings, but every one is in generated or third-party code:
761 `-Wgnu-zero-variadic-macro-arguments` from `/usr/local/include/check.h`, and 24 from
flex-generated `theme-lexer.c` (`-Wunreachable-code`, `-Wmisleading-indentation`).
**The hand-written C compiles warning-free at `warning_level=3`** with `-Wshadow`,
`-Werror=missing-prototypes` and the rest of the flag set. That is a better starting position
than the audit assumed.

### Phase 1 — Fix what the rebrand would cement: COMPLETE

All five items done. Build clean, 19/19 tests pass, and all 12 sofi tests pass under ASAN.

| # | File | Change | Verified by |
|---|---|---|---|
| 1 | `source/helper.c:1354-1372` | `utf8_strncmp` clamps truncation to each string's own normalized length; also guards `g_utf8_normalize` returning NULL on invalid UTF-8 | ASAN: overflow reproduced, then gone |
| 2 | `include/widgets/textbox.h:63` | `short cursor` → `int cursor` | build + textbox test |
| 3 | `source/rofi.c:847` | `g_warning(str, NULL)` → `g_warning("%s", str)`; dead commented-out `fputs` pair removed | build |
| 4 | `include/settings.h:113-119`, `config/config.c:79-86`, `source/helper.c:741-746` | `WindowLocation location` → `unsigned int location`, documented as a 0-8 position index; range check and format specifier corrected for the unsigned type | build + tests |
| 5 | `include/mode.h:35-39` | `ABI_VERSION` 7u → 8u | build |
| — | `meson.build:30-31` | added `-Werror=format-security` and `-Wformat=2` | build |

**ASAN reproduction of finding #1, before the fix:**

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 1 at 0x502000001434
    #0 utf8_strncmp source/helper.c:1358:36
    #1 main test/helper-test.c:185:5
0x502000001434 is located 2 bytes after 2-byte region
```

After the fix the same binary exits 0 with no ASAN report. This is the first finding in the
register to be independently reproduced *and* closed.

**Two corrections to the audit's own framing**, recorded so the register is not trusted
blindly:
- `include/widgets/textbox.h` and `include/settings.h` are **not** in the installed header
  set (`meson.build:195-203` installs only `mode.h`, `mode-private.h`, `helper.h`,
  `rofi-types.h`, `rofi-icon-fetcher.h`). Findings 2 and 4 are memory-safety and type-safety
  fixes, not plugin-ABI concerns. The plan's Phase 1 rationale overstated this.
- The `WindowLocation` enum in the installed `rofi-types.h` is correct as a bitmask. The
  defect was only the *field* in `settings.h` that misused it.

**Incidental improvement found while fixing #4:** `source/xrmoptions.c:61` declares the
option-table union member as `unsigned int *num`, so before this change the table was
aliasing a `WindowLocation` enum through an `unsigned int *`. Now correctly typed.

### Q7 / Q13 rulings executed

| Action | Scope |
|---|---|
| Removed frozen per-version doc trees | `mkdocs/docs/1.7.0` … `1.7.9`, `mkdocs/docs/2.0.0` — 71 files, ~7.9 MB |
| Removed upstream `Changelog` | nothing in the tree referenced it |
| Removed `.gitlab-ci.yml` | pure autotools against a tree with no `configure.ac`; failed 100% of the time |
| Trimmed `mkdocs/mkdocs.yml` nav | dropped the 12 per-version sections; `Current`, `Guides` and top-level pages retained |
| Dropped `.gitattributes:6` | the now-dead `.gitlab-ci.yml export-ignore` rule |
| Added `/subprojects/.wraplock` to `.gitignore` | meson ≥1.10 in-tree artifact |

Verified: every remaining nav target resolves, and `git grep` finds no dangling references to
any deleted version directory. Build and tests still green afterwards.

### NEW FINDING — not in the original register

**Heap-buffer-overflow in the vendored libnkutils, exposed by its own test suite.**

```
WRITE of size 8 at 0x50300000d9e0
    #0 _nk_format_string_parse subprojects/libnkutils/core/src/format-string.c:690:36
    #1 nk_format_string_parse  subprojects/libnkutils/core/src/format-string.c:740:12
0 bytes after a 32-byte region allocated at format-string.c:670
```

Pre-existing and unrelated to any change in this session — `subprojects/` was never touched.
**Not reachable from sofi:** the only nkutils APIs sofi calls are `nk_bindings_*` and
`nk_xdg_theme_*` (verified by grep over `source/` and `include/`); `nk_format_string_*` is
never called, and `meson.build:181-184` enables only `bindings=true`.

Consequence: `meson test -C build-asan` reports 18/19 rather than 19/19. Recorded so the
discrepancy is not mistaken for a sofi regression later. Filed as backlog item B7.

---

## 2026-08-22 18:55 — Phase 1 Initialization complete

**Completed**

- Read the full tree: 408 tracked files, ~42,400 lines of C, all root documentation,
  the meson build, both display backends, all modes, the theme engine, and CI config.
- Ran a 21-agent read-only audit: 10 domain surveys, 10 adversarial verifiers, 1 synthesis.
  263 raw findings → 15 refuted → **248 retained**. 166 identity surfaces catalogued.
  (The synthesis agent hit a session limit; the synthesis was completed directly.)
- Established the baseline build state on this host (FreeBSD 15.1-RELEASE): **does not
  configure** — GNU Bison absent, `check` absent.
- Generated the `.devdocs/` workspace: BRIEFING, PROGRESS, SESSION_HANDOFF, DECISIONS_LOG,
  TODOS, PLANS, BLUEPRINT, plus AUDIT_REGISTER and REBRAND_SURFACES.

**Not done — deliberately**

- Zero product-code modifications. Per the Zero Unapproved Action directive, the audit was
  strictly read-only and no rename, fix, or refactor has been applied.
- No system packages installed.

**Findings by severity**

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 12 |
| Medium | 59 |
| Low | 177 |
| **Total** | **248** |

**Findings by kind**

| Kind | Count |
|---|---|
| correctness | 95 |
| memory | 71 |
| structure | 29 |
| build | 21 |
| portability | 14 |
| docs | 11 |
| protocol | 7 |

**Rebrand surfaces by blast radius**

| Risk class | Count |
|---|---|
| Breaks third-party plugins | 35 |
| Breaks existing user config | 28 |
| Breaks distribution packaging | 13 |
| User-visible but safe | 37 |
| Internal only | 53 |
| **Total** | **166** |

**Rename scale**

| Measure | Count |
|---|---|
| `rofi` occurrences, whole tree (case-insensitive) | 6,035 lowercase + 751 `Rofi` + 741 `ROFI` |
| Excluding frozen historical docs | 3,912 |
| In C code (`source` `include` `lexer` `config` `test`) | 2,284 + 468 `Rofi` + 516 `ROFI` |
| Distinct identifiers to rename | 335 |
| Files requiring rename | 30 live, ~90 frozen |

**Superseded / archived**

Nothing yet — this is the first recorded session.
