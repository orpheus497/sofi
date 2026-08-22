# BLUEPRINT

**Last updated:** 2026-08-22 18:55

System architecture, requirements, and how dependencies operate.

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
                     source/modes/*.c            run, drun, ssh, dmenu, script, combi,
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

| Protocol | Source | Required? |
|---|---|---|
| `wl_compositor` / `wl_shm` / `wl_seat` / `wl_output` | core | yes |
| `zwlr_layer_shell_v1` | vendored `protocols/` | **yes — hard requirement, aborts without it** |
| `zwlr_foreign_toplevel_management_v1` | vendored `protocols/` | window mode only |
| `ext_foreign_toplevel_list_v1` | system, staging | window mode only |
| `xdg-shell` | system, stable | compiled but **not used by the backend** |
| `primary-selection-unstable-v1` | system | clipboard |
| `keyboard-shortcuts-inhibit-unstable-v1` | system | `global_kb` |
| `text-input-unstable-v3` | system | IME |
| `cursor-shape-v1` + `tablet-unstable-v2` | system, ≥1.32 | conditional |

**Not implemented:** `wp_fractional_scale_v1` (only integer `buffer_scale`),
`xdg-activation-v1`, `wp_single_pixel_buffer_v1`.

**Consequence.** Mutter and KWin implement neither wlr protocol, so sofi cannot run there at
all — and fails by aborting rather than by falling back. An xdg-shell fallback is the change
that would fix it. See `PLANS.md` Phase 2.

---

## Testing

`test/` holds 14 binaries driven by `check`. Coverage is concentrated on the theme parser
(`theme-parser-test.c`, 1,996 lines), helper/tokenizer, and individual widgets.

**Not covered at all:** both display backends, all modes, the icon fetcher's threading, and
anything Wayland. There is no integration or golden-image test. This is the main reason the
Wayland defects in `AUDIT_REGISTER.md` survived.
