# BLUEPRINT

**Last updated:** 2026-08-24 10:20

System architecture, requirements, and how dependencies operate.

---

## Implementation registry — the four system surfaces

Per `AGENTS.MD`, completed `TODOS.md` items land here. Delivered 2026-08-24, uncommitted.
Decisions: `DECISIONS_LOG.md` R16–R24. Plan: `PLANS.md` Phase 8.

sofi is hikari-sakura's shell, not a rofi drop-in that runs on it. Four surfaces, each with its
own compiled-in layout and its own instance lock, **requiring no configuration file**.

| Surface | Invocation | Layout resource | Lock | Geometry observed |
|---|---|---|---|---|
| Application menu | `sofi -show drun` | `/org/sofi/default.sasi` | `sofi-menu.pid` | 280 × 816, west |
| Task / window manager | `sofi -show window` | `/org/sofi/panel-window.sasi` | `sofi-window.pid` | 1920 × 52, south |
| Sheet switcher | `sofi -show sheets` | `/org/sofi/panel-sheets.sasi` | `sofi-sheets.pid` | 190 × 472, east |
| Notification / message | `sofi -e <msg>` | `/org/sofi/panel-notify.sasi` | `sofi-notify.pid` | 380 × 55, north east |

**Selection mechanism.** `sofi_surface_name()` in `source/sofi.c` derives the surface from the
invocation; `sofi_builtin_panel_resource()` maps that to a GResource path. The layout is parsed
after `default_configuration.sasi` and before every user-supplied source, so `~/.config/sofi` and
`-theme` still override it. `default_configuration.sasi` no longer carries `@theme "default"` —
the choice is made in C.

**Why the task strip is a BARVIEW.** `listview { layout: horizontal }` at
`source/widgets/listview.c:835` selects a different renderer, not a rotated grid: elements are
sized to their own content width and the visible window scrolls with the selection. `flow:
horizontal` divides the width into equal cells and is the wrong tool for a task strip.

**Surface isolation, measured.** Captured layer-shell traffic, which is what makes a resident
notification daemon safe:

| Surface | `set_size` | `set_keyboard_interactivity` |
|---|---|---|
| notify | 380 × 55 | 2 — `ON_DEMAND` |
| drun / window | 1920 × 1166 (full output) | 1 — `EXCLUSIVE` |

The menus cover the output deliberately so a click anywhere dismisses them
(`source/wayland/view.c:262`). The notification surface does not, and does not hold the keyboard.

---

## Compositor contract — hikari-sakura control socket

The one part of hikari's model no Wayland protocol reaches. Owned by
`hikari-sakura/src/ipc.c`; consumed by `sofi/source/modes/sheets.c`.

- **Path** `$XDG_RUNTIME_DIR/hikari.sock`, mode 0600, `AF_UNIX` `SOCK_STREAM`.
- **Served from** the compositor's own `wl_event_loop`, so handlers run on the main thread and
  must never block.
- **Bounded** — 512-byte requests, 8 concurrent clients, one exchange per connection so the
  server holds no per-client state machine.
- **Stale sockets** are unlinked at startup before `bind`, which is the real defence given that
  `hikari_server_stop()` appears not to run on SIGTERM (see `DECISIONS_LOG.md`).

| Request | Response | Notes |
|---|---|---|
| `state` | `sheet <n>` / `output <name>` / `counts <c0>…<c9>` / `END` | Unrecognised lines are ignored by the client so the compositor can add fields |
| `sheet <0-9>` | `ok` or `error …` | `hikari_workspace_switch_sheet()` |
| `pin <0-9>` | `ok` or `error …` | Moves the focused view. **The operation no Wayland protocol expresses** — closes Q17 |

**Sheet visibility also arrives over Wayland, for free.** `display_sheet()`
(`hikari/src/workspace.c:160-192`) hides views not on the target sheet, and `hikari_view_hide()`
publishes that as foreign-toplevel's minimised bit. So `TOPLEVEL_STATE_MINIMIZED` means *"not on
the sheet you are looking at"*. Sheet 0's views are never hidden — that asymmetry is the
semantics, not a bug.

---

## Component map

```
                         source/rofi.c            main(), option parsing, plugin loading,
                              │                   config discovery, backend selection
                              ▼
                       source/display.c           backend-agnostic display_proxy dispatch
                        include/display.h
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
      source/xcb/display.c            source/wayland/display.c
      source/xcb/view.c               source/wayland/view.c
      (X11 / EWMH / XRandR)           (wlr-layer-shell / xkb / shm)
              │                               │
              └───────────────┬───────────────┘
                              ▼
                        source/view.c            RofiViewState, event loop, refilter,
                              │                  keybinding dispatch, listview paging
                              ▼
                     source/widgets/*.c          box, container, icon, listview,
                              │                  scrollbar, textbox, widget
                              ▼
                       source/theme.c            RASI theme engine
                   lexer/theme-lexer.l           flex scanner
                   lexer/theme-parser.y          GNU Bison GLR parser
                              ▲
                     source/modes/*.c            run, drun, ssh, dmenu, script, combi, sheets,
                                                 filebrowser, recursivebrowser, help-keys,
                                                 window (xcb), wayland-window (wayland)
```

Support: `source/helper.c` (matching, tokenizing, path expansion, pidfile, process exec),
`source/history.c`, `source/rofi-icon-fetcher.c` (threaded icon/thumbnail loading),
`source/xrmoptions.c` (config option table, X resources, `-dump-config`),
`source/css-colors.c`, `source/keyb.c`, `source/timings.c`, `config/config.c` (defaults).

**Scale.** ~42,400 lines of C. Largest units: `source/view.c` (2,212),
`source/xcb/display.c` (2,051), `source/wayland/display.c` (2,038),
`test/theme-parser-test.c` (1,996), `source/helper.c` (1,795), `source/modes/drun.c` (1,699),
`source/theme.c` (1,660).

---

## Backend selection

`source/rofi.c:1067-1084`. XCB is chosen first when compiled in; Wayland overrides it when
`$WAYLAND_DISPLAY` is set and neither `-x11` nor `-xcb` was passed. There is **no runtime
fallback** — if the chosen backend's `setup()` fails, `source/rofi.c:1280-1298` is supposed
to print a friendly message, but four `g_error()` calls in the Wayland path
(`source/wayland/display.c:1490,1505,1752,1756`) abort the process before it can.

Both backends are cleanly guarded. `ENABLE_XCB` / `ENABLE_WAYLAND` are used symmetrically,
`source/display.c` pulls in no backend headers, and `window.c` / `wayland-window.c` are each
fully wrapped *and* conditionally added to the source list (`meson.build:297-305`, `354-360`).
Wayland-only and xcb-only builds are structurally sound. The one leak is `imdkit`.

---

## Dependencies and their licenses

`AGENTS.MD` requires MIT/BSD permissive licensing, with `pango` and `cairo` as explicitly
permitted LGPL exceptions.

| Dependency | License | Status |
|---|---|---|
| glib-2.0 / gmodule-2.0 / gio-2.0 | LGPL-2.1+ | **Requires a ruling.** Not covered by the stated pango/cairo exception, yet it is the project's most pervasive dependency — used in essentially every file. Flagged in `TODOS.md`. |
| cairo | LGPL-2.1 / MPL-1.1 | Permitted exception |
| pango / pangocairo | LGPL-2.1+ | Permitted exception |
| gdk-pixbuf-2.0 | LGPL-2.1+ | Same question as glib |
| libxkbcommon | MIT | OK |
| wayland-client / wayland-cursor | MIT | OK |
| wayland-protocols | MIT | OK |
| libxcb + xcb-util family | MIT | OK |
| libstartup-notification | LGPL-2.0+ | X11 only; genuinely used (`source/xcb/display.c:70,1776,1931`) |
| xcb-imdkit | LGPL-2.1 | Optional |
| libnkutils (subproject) | MIT | OK |
| libgwater (subproject) | MIT | OK |
| check | LGPL-2.1 | Test-only, not linked into the shipped binary |

**Note.** The dependency set is inherited from upstream rofi and was not chosen under
`AGENTS.MD`. The glib question is pre-existing, not introduced by the rebrand, but it should
be ruled on rather than left implicit.

---

## Build requirements

**Tools.** meson ≥0.59, ninja, pkg-config, **GNU Bison** (not byacc — the grammar uses
`%define api.pure`, `%glr-parser`, `%skeleton "glr.c"` at `lexer/theme-parser.y:28-33`),
flex ≥2.5.39, glib-compile-resources, wayland-scanner, `check` for tests.

**On FreeBSD** (this development host, 15.1-RELEASE) `bison` and `check` are ports, not base.
Base ships `byacc`/`yacc`, which cannot build this grammar. `meson.build:224` currently fails
with a bare "Program 'bison' not found".

**Generated at build time.** `config.h` from `header_conf` (`meson.build:129-177`);
the lexer/parser from flex/bison; Wayland protocol bindings via `wayland-scanner` from five
system protocol XMLs plus two vendored ones in `protocols/`; the default theme via
`gnome.compile_resources`.

---

## Public contracts (what the rebrand must be careful with)

1. **Plugin ABI** — five installed headers (`meson.build:195-203`) → `$includedir/<name>/`;
   `ABI_VERSION 7` at `include/mode.h:36`; the exported `mode` symbol loaded by
   `source/rofi.c:643-662`; plugins found in `$libdir/<name>`; discoverable via `rofi.pc`.
2. **Script-mode protocol** — `ROFI_RETV`/`ROFI_INFO`/`ROFI_DATA`/`ROFI_INPUT`/`ROFI_OUTSIDE`
   (`source/modes/script.c:216-231`), documented in `doc/rofi-script.5.markdown`.
3. **RASI theme format** — `.rasi`/`.rasinc` (`lexer/theme-lexer.l:56`), the four-root theme
   search path (`source/helper.c:1474-1520`), and the element/property vocabulary.
4. **On-disk state** — config, themes, scripts under `$XDG_CONFIG_HOME/rofi/`; five cache
   files under `$XDG_CACHE_HOME`.
5. **Compositor identity** — layer-shell namespace (`source/wayland/display.c:1792`),
   X11 WM_CLASS (`source/xcb/view.c:773-775`), desktop-file `StartupWMClass`.
6. **CLI surface** — every documented flag in `doc/rofi.1.markdown`.

Full inventory with per-surface risk classification: `REBRAND_SURFACES.md`.

---

## Wayland protocol usage

| Protocol | Source | sofi binds at | hikari advertises | Required? |
|---|---|---|---|---|
| `wl_compositor` / `wl_shm` / `wl_seat` / `wl_output` | core | — | v5 / v1 / v9 / v4 | yes |
| `zwlr_layer_shell_v1` | vendored `protocols/` | **v4** (was v1) | **v4** | **yes — hard requirement** |
| `zwlr_foreign_toplevel_management_v1` | vendored `protocols/` | v3 | **v3** | window + sheets modes |
| `ext_foreign_toplevel_list_v1` | system, staging | v1 | v1 | `{window}` identifier only |
| `xdg-shell` | system, stable | v2 | v3 | fallback path only |
| `primary-selection-unstable-v1` | system | v1 | v1 | clipboard |
| `keyboard-shortcuts-inhibit-unstable-v1` | system | v1 | **absent** | `-global-kb` — **inert on hikari** |
| `text-input-unstable-v3` | system | — | **absent** | no IME on hikari |
| `cursor-shape-v1` + `tablet-unstable-v2` | system, ≥1.32 | — | **absent** | falls back to `wl_cursor` |

Verified 2026-08-24 by a `wl_registry` dump against the running compositor, not inferred.
32 globals advertised.

**The v1 → v4 layer-shell bump matters.** `ON_DEMAND` keyboard interactivity arrived in v4;
below it wlroots coerces the argument to `!!interactive`, so requesting it on a v1 binding
silently means `EXCLUSIVE`. `wl_registry_bind` still takes `MIN(advertised, 4)`, so a v1-only
compositor is unaffected. This is what makes a passive notification surface possible at all.

**Not implemented, and now visible:** `wp_fractional_scale_v1` **is advertised by hikari** and
sofi uses integer `buffer_scale` only — every panel renders soft on a fractionally scaled output.
Also unimplemented: `xdg-activation-v1`, `wp_single_pixel_buffer_v1`.

**Absent on hikari, so dead code here:** `-global-kb`, IME, and the `cursor-shape` conditional
path. None of these fail loudly; they simply do nothing.

**Consequence for other compositors.** Mutter and KWin implement neither wlr protocol. The
xdg-shell fallback (Phase 2b) keeps sofi running there, but without layer-shell it cannot
position or anchor itself, and the sheets mode has no socket to reach.

---

## Testing

`test/` holds 14 binaries driven by `check`. Coverage is concentrated on the theme parser
(`theme-parser-test.c`, 1,996 lines), helper/tokenizer, and individual widgets.

**Not covered at all:** both display backends, all modes, the icon fetcher's threading, and
anything Wayland. There is no integration or golden-image test. This is the main reason the
Wayland defects in `AUDIT_REGISTER.md` survived.
