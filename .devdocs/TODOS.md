# TODOS

**Last updated:** 2026-08-24 10:20

Granular task list. Per `AGENTS.MD`, items enter here as questions tabled under a design
implementation request, move to the active list once scoped in `DECISIONS_LOG.md`, and move
to the implementation registry in `BLUEPRINT.md` on completion.

---

## ACTIVE — Phase 9: notification daemon

Scoped in `DECISIONS_LOG.md` R20–R24, planned in `PLANS.md` Phase 9. **Awaiting approval to
begin.** Ordering constraint: N2 before N3.

| # | Task | Depends on | State |
|---|---|---|---|
| N1.1 | `source/notify-service.c`: GDBus skeleton, own `org.freedesktop.Notifications` with `REPLACE \| DO_NOT_QUEUE`; handle `name_lost` by logging and exiting cleanly | — | Ready |
| N1.2 | `GetServerInformation`; `GetCapabilities` declaring **only** what is implemented | N1.1 | Ready |
| N1.3 | `Notify` accepted, arguments parsed and logged, incrementing id returned | N1.1 | Ready |
| N2.1 | Gate `sofi_quit_main_loop()` at `view.c:1536` on daemon mode | — | Ready |
| N2.2 | Unmap on empty / remap on arrival, reusing the `display.c:1804` destroy → `late_setup` → `pool_refresh` sequence | N2.1 | Ready |
| N2.3 | **Force `click-to-exit: false` and `keyboard-interactivity: on-demand` after theme parsing** (R24, RR11, RR12) | N2.1 | Ready |
| N2.4 | Surface name `notifyd`; own pidfile, distinct from the `-e` toast's `notify` | N2.1 | Ready |
| N3.1 | Notification struct + fixed ring buffer with `live` flag (R21); ~20 live, ~200 total | N2 | Ready |
| N3.2 | `replaces_id` updates in place rather than stacking | N3.1 | Ready |
| N3.3 | `source/modes/notifications.c` rendering the live subset | N3.1 | Ready |
| N3.4 | `doc/panel-notifications.sasi` — `location: south east`, `reverse: true`, clearing the task strip (R22) | N3.3 | Ready |
| N4.1 | Per-notification expiry timer; `-1` server default, `0` never | N3 | Ready |
| N4.2 | `urgency=2` ignores expiry entirely (R23) | N4.1 | Ready |
| N4.3 | `CloseNotification` + `NotificationClosed` with reason 1/2/3 | N4.1 | Ready |
| N5.1 | Actions array (flat key/label pairs); Enter invokes `"default"` | N4 | Ready |
| N5.2 | `kb-custom-N` invokes the Nth action; emit `ActionInvoked` then closed reason 2 | N5.1 | Ready |
| N6.1 | `app_icon` and `image-path` via the existing icon fetcher | N3 | Ready |
| N6.2 | **Validated** `image-data` → `cairo_surface_t`; check `rowstride × height` against array length, cap dimensions, reject not clamp (RR14) | N6.1 | Ready |
| N6.3 | Body markup: `pango_parse_markup` validate, `g_markup_escape_text` fallback | N3 | Ready |
| N7.1 | `source/modes/notification-history.c` over the same ring | N3.1 | Ready |
| N7.2 | Own panel + pidfile, ordinary one-shot invocation | N7.1 | Ready |
| N8.1 | `~/.local/share/dbus-1/services/org.freedesktop.Notifications.service` (user dir beats system, no packaged file touched) | N1 | Ready |
| N8.2 | hikari autostart line; manpage section; documented one-file rollback to xfce4-notifyd | N8.1 | Ready |
| N8.3 | Test four concurrent surfaces (RR16) | N3 | Ready |

## ACTIVE — carried over, not blocking Phase 9

| # | Task | State |
|---|---|---|
| C1 | **Commit Phase 8 in both repositories.** Nothing is committed; another agent is active in `hikari-sakura` (RR17) | **Do first** |
| C2 | Install and restart: `ninja -C build install`, `make install` in hikari, restart compositor. Sheet switcher is inert until then | Blocked on C1 |
| C3 | Add the sheets binding to `hikari.conf`. `L+s` and `LS+s` are taken; free on the `L+` layer: `a b c e j k n t z`, `comma`, `period` | Blocked on C2 |
| C4 | **Q16** — delete the ext-foreign-toplevel binding and `window-command`'s `{window}`? Now slightly less attractive: the ext list is the only stable per-window identifier a notification daemon might want for correlation | Unruled |
| C5 | Report to USER: hikari does not build at HEAD with default flags (stale `action.o` under `-DNDEBUG`, root-owned `main.o`). `make clean` mandatory after any header edit | Reported |
| C6 | Report to USER: `hikari_server_stop()` appears not to run on SIGTERM on FreeBSD; affects every teardown step, not just the socket | Reported, unconfirmed |

---

## Resolved — ruled 2026-08-22 19:02

Governing principle: **sofi is a hard fork with its own identity, not a drop-in rofi
replacement.** No shims, no dual-name exports, no fallback lookups.

| # | Question | Ruling |
|---|---|---|
| R1 | Rename `rofi_*`/`ROFI_*`/`Rofi*` public symbols? | **Rename everything, no shim.** Still bump `ABI_VERSION` so stale plugins fail loudly |
| R2 | Migrate on-disk config/cache paths? | **Hard break, no fallback.** Must be stated prominently in README + release notes |
| R4 | Rename script-mode env vars? | **`SOFI_*` only.** Failure is silent, so the script manpage must lead with the rename |
| R10 | glib/gio/gmodule/gdk-pixbuf under the FOSS rule? | **Permitted exception**, alongside pango and cairo. Amend `AGENTS.MD` to match |

**Not overridden by the above:** MIT attribution. `COPYING`, `AUTHORS`, the per-file
copyright headers and fork provenance are license obligations, not preferences. Phase 3c's
bulk substitution must exclude comment blocks containing a copyright line.

## Resolved — ruled 2026-08-22 19:15

| # | Question | Ruling |
|---|---|---|
| R3 | `.rasi`/`.rasinc` extension? | **Rename to `.sasi`/`.sasinc`.** 35 theme files + lexer array + config/system file names. Gruvbox `@import`s are extension-less — no importer edits |
| R5 | Layer-shell namespace + X11 WM_CLASS? | **Rename** to `sofi` / `"sofi\0Sofi"` |
| R6 | Helper script names? | **Rename, no symlinks.** `config/config.c:65` in the same commit |
| R9 | pkg-config + plugin dir? | **`sofi.pc` / `$libdir/sofi`**, no alias |
| R11 | xdg-shell fallback in scope? | **Yes — new Phase 2b (~2–3 days).** Largest functional gain available |
| R12 | `AUTHORS` / `CODE_OF_CONDUCT.md` / `releasenotes/` deletions? | **Deliberate, keep deleted.** `COPYING` verified present and unmodified with both required MIT notices. Raises RR4 to High |

## Resolved — ruled 2026-08-22 19:26

| # | Question | Ruling |
|---|---|---|
| R7 | Frozen historical docs? | **Delete.** `mkdocs/docs/1.7.*` + `2.0.0` + `Changelog` removed; nav trimmed. Done |
| R13 | `.gitlab-ci.yml`? | **Delete.** Done, plus its `.gitattributes` export-ignore rule |
| R14 | CI targets? | **FreeBSD only.** No OpenBSD/NetBSD jobs |

**All 15 questions are now ruled. No decisions outstanding.**

---

## Active list

**Phase 4 — FreeBSD CI + INSTALL.md**, then Phase 5 (59 medium findings), Phase 6 (ship `sofi-config/`), Phase 7 (new modes).

---

## Backlog — sequenced

### B0 · Baseline — **COMPLETE 2026-08-22**
- [x] `devel/bison` 3.8.2 and `check` 0.15.2 installed by USER
- [x] Baseline recorded: configure OK, build OK, **19/19 tests pass**
- [x] Warning audit: hand-written C is warning-free at `warning_level=3`; all 787 warnings are in `check.h` or flex-generated `theme-lexer.c`

### B1 · Fix before renaming — **COMPLETE 2026-08-22**
- [x] `source/helper.c:1354-1372` — `utf8_strncmp` clamp + NULL guard. ASAN-reproduced then closed
- [x] `include/widgets/textbox.h:63` — `short cursor` → `int cursor`
- [x] `source/rofi.c:847` — `g_warning("%s", str)`; dead `fputs` pair removed
- [x] `include/settings.h:113` + `config/config.c` + `source/helper.c:741` — `WindowLocation` → `unsigned int` position index, documented
- [x] `include/mode.h:36` — `ABI_VERSION` 7u → 8u
- [x] `meson.build:30-31` — `-Werror=format-security`, `-Wformat=2`

### B7 · Vendored dependency defect (new, 2026-08-22)
- [ ] Heap-buffer-overflow WRITE at `subprojects/libnkutils/core/src/format-string.c:690`, caught by libnkutils' own test under ASAN. **Not reachable from sofi** — only `nk_bindings_*` and `nk_xdg_theme_*` are called. Causes ASAN suite to read 18/19. Decide: patch the subproject, or exclude its tests

### B2 · Wayland / layer-shell — **COMPLETE 2026-08-22**
- [x] Four `g_error()` aborts → non-fatal; the friendly backend-failure message in `source/rofi.c:1280` is now reachable
- [x] `SHM_ANON` / unique-name shm allocation; fixed name gone
- [x] Buffer pool NULL-guarded at callee and caller
- [x] Key-repeat stores a `guint` source id, not a borrowed `GSource *`
- [x] `repeat_info` rate == 0 honoured as "repeat disabled"
- [x] Keymap fd, mmap, xkb_keymap and xkb_state all released (all four leaked)
- [x] Seat capabilities tests the KEYBOARD bit
- [x] `late_setup()` re-checks preconditions and returns honestly
- [x] Window/height sizing uses the cached output size, not the live layer size
- [x] `-x-offset`/`-y-offset` honoured on Wayland
- [x] Selected toplevel and `last_seat` NULL-checked
- [x] Manager lifecycle: `finished` clears the pointer; teardown stops before freeing
- [x] Overflow guards on stride/height/buffer_count and against `INT32_MAX`
- [x] `_init` propagates failure so a non-wlr compositor gets an error dialog, not an empty list

### B2b · xdg-shell fallback — **COMPLETE 2026-08-22**
- [x] Bind `xdg_wm_base`; ping/pong handshake (mandatory — unanswered ping = client killed)
- [x] `wayland_shell_kind` selection: layer shell preferred, xdg fallback, clear error if neither
- [x] `xdg_surface.configure` ack; `xdg_toplevel.configure` size adoption; `close` handling
- [x] Branch `late_setup`, `display_set_surface_dimensions`, `set_fullscreen_mode`, `wayland_surface_destroy`
- [x] Seed screen size from output (xdg gets no all-corner configure trick)
- [x] `README.md` documents the degraded positioning honestly
- [ ] **Unverified on hardware** — needs a run under Mutter/KWin, plus a sway regression run

### B8 · Ship `sofi-config/` as the deployed default (new, USER-added 2026-08-22)
- [ ] Decide canonical location: replace `doc/default_configuration.rasi` + `doc/default_theme.rasi` (compiled in via `resources/resources.xml`), install to `$datadir/sofi/themes/`, or both
- [ ] Wire into `meson.build` `install_data`
- [ ] Make `@theme "default"` (`source/rofi.c:1179`) resolve to it
- [ ] **Phase 3e dependency:** `sofi-config/config.rasi:15` `@import "colors-default.rasi"` names the extension explicitly, so the R3 `.sasi` rename MUST edit this line — unlike the extension-less gruvbox imports
- [ ] Switch `modi:` → `modes:` in the shipped default (`modi` still works, but is the deprecated spelling)
- [x] Validated: parses with zero warnings against the real binary

### B9 · New modes (new, USER-requested 2026-08-22 — planning only, not built)
- [ ] **7a Window switcher** — already exists (`window.c`, `wayland-window.c`); treat as hardening. Remaining: KWin/Mutter unsupported, ext↔wlr correlation heuristic, duplicated `helper_eval_add_str`
- [ ] **7b Workspace switcher** — does not exist. Feasible both backends: EWMH on X11 (groundwork at `source/modes/window.c:559,796`), `ext-workspace-v1` on Wayland (present on this host, not yet in `meson.build:317-327`)
- [ ] **7c Task/window manager (R15)** — window actions: close / minimise / maximise / send-to-workspace. No new data source. Wayland: `zwlr_foreign_toplevel_handle_v1` requests already bound in `source/modes/wayland-window.c` but not exposed. X11: EWMH client messages. Sequence after 7a and 7b; send-to-workspace depends on 7b

### B3 · Rename — unblocked, all gates closed
- [ ] 3a build identity · 3b file renames · 3c C identifiers · 3d paths/env · 3e `.sasi` extension · 3f compositor identity/scripts/pkgconfig · 3g docs/packaging/attribution
- [ ] Each sub-phase: green build + its grep invariant. See `PLANS.md`.

### B4 · FreeBSD
- [ ] Document the FreeBSD from-source dependency set in `INSTALL.md` (currently only `pkg install rofi` at `:259-262`)
- [ ] Fix the stale autotools `--disable-check` flag at `INSTALL.md:26`
- [ ] Link `librt` where required (older glibc needs it for `shm_open`)
- [ ] Rewrite `.build.yml` for FreeBSD (R14: FreeBSD only) — it clones upstream rofi from sourcehut (`:32`) and declares a `1.7.8-dev` artifact (`:47`) while `meson.build:2` says `2.0.0-dev`
- [ ] Add a real FreeBSD CI job

### B5 · Remaining correctness (59 medium findings — see `AUDIT_REGISTER.md`)
- [ ] Icon-fetcher threading: `source/rofi-icon-fetcher.c:325,562,746`
- [ ] Unchecked xcb replies: `source/xcb/display.c:466,572,1466`, `source/modes/window.c:640`, `source/xcb/view.c:593`
- [ ] dmenu: `source/modes/dmenu.c:157,169,337,611`
- [ ] drun: `source/modes/drun.c:421,839,948,1226`
- [ ] combi: `source/modes/combi.c:149,158,195`
- [ ] ssh `Include` cycles: `source/modes/ssh.c:394`
- [ ] filebrowser uninitialized `collate_key`: `source/modes/filebrowser.c:301`
- [ ] recursivebrowser: `source/modes/recursivebrowser.c:187,294,454`
- [ ] pidfile: `source/helper.c:615,617,638`
- [ ] history durability: `source/history.c:244`
- [ ] markup escaping: `source/modes/window.c:901`
- [ ] view lifetime: `source/view.c:938,1006,1557`
- [ ] theme/xrmoptions: `source/theme.c:465`, `source/xrmoptions.c:905`, `lexer/theme-lexer.l:815`
- [ ] `script/rofi-theme-selector:40` predictable temp path; `script/get_git_rev.sh:8` `-d .git` breaks worktrees

### B6 · Structure
- [ ] `source/view.c:364` — raw XCB calls in backend-agnostic code
- [ ] De-duplicate `helper_eval_add_str` across `source/modes/window.c:882` and `source/modes/wayland-window.c:723` (already diverged — that divergence *is* the bug at `window.c:901`)
- [ ] `source/modes/window.c:870` — `window`/`windowcd` share a global cache either can free
- [ ] Add Wayland and mode-level test coverage — currently zero
