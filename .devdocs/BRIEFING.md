# BRIEFING

**Last updated:** 2026-08-22 20:02

## Project

`sofi` — a fork of `rofi` (window switcher / application launcher / dmenu replacement) being
completely rebranded from the upstream identity, hardened, and made genuinely portable to
FreeBSD. Origin is `https://github.com/orpheus497/sofi.git`; the upstream remote is gone.

- ~42,400 lines of C across `source/`, `include/`, `lexer/`, `config/`, `test/`
- Meson build, C99, glib ≥2.72 / cairo / pango / gdk-pixbuf
- Two display backends: `xcb` (X11) and `wayland` (wlr-layer-shell)
- MIT licensed — attribution obligations survive the rename

## Current phase

**Phase 3 complete — the project is now sofi. Phase 4 (FreeBSD CI) is next.**

Initialization, the deep audit, Phase 0 (baseline) and Phase 1 (pre-rename fixes) are all
done. Q7/Q13 deletions executed. Build and tests are green; ASAN is clean for all sofi code.

## Progress

| Item | State |
|---|---|
| `.devdocs/` scaffolding | Done |
| Deep read-only audit | Done — 248 verified findings, 166 identity surfaces |
| Rebrand decisions | **All 13 ruled.** Nothing outstanding |
| Phase 0 — baseline | **Done. 19/19 tests pass** |
| Phase 1 — pre-rename fixes | **Done. 5 defects + 2 compiler flags. ASAN clean** |
| R7/R13 deletions | Done — 74 files removed |
| Phase 2a — Wayland | **Done. 13 items. ASAN clean** |
| Phase 2b — xdg-shell fallback | **Done. Runs on GNOME/KWin** |
| Phase 6 — ship `sofi-config/` | Planned (USER-added) |
| Phase 7 — window/workspace/task modes | Planned, 7c blocked on Q15 |
| Phase 3 — the rename | **Done. 174 files, all 10 invariants pass** |
| Phase 4 — FreeBSD CI | Next |

## Blockers

**None.** All three prior blockers cleared: `devel/bison` 3.8.2 and `check` 0.15.2 are
installed, a green baseline exists (19/19), and every decision is ruled.

One thing to be aware of rather than fix: `meson test -C build-asan` reads **18/19**, not
19/19. The single failure is a pre-existing heap overflow in the vendored
`subprojects/libnkutils` format-string parser, caught by that subproject's own test. It is
not reachable from sofi — only `nk_bindings_*` and `nk_xdg_theme_*` are ever called. Backlog
item B7.

## Recent architectural decisions

**2026-08-22 19:02 — sofi is a hard fork, not a drop-in replacement.** The USER ruled the
clean-break option on every compatibility question:

- **R1** Rename all 335 `rofi_*`/`ROFI_*`/`Rofi*` identifiers. No compat shim, no `rofi.pc`
  alias. `ABI_VERSION` still bumped so stale plugins fail loudly rather than crashing.
- **R2** On-disk config, theme, script and cache paths hard-break to `sofi/`. No fallback.
- **R4** Script-mode env vars become `SOFI_*` only. `ROFI_*` is not exported.
- **R10** glib/gio/gmodule/gdk-pixbuf are permitted LGPL exceptions alongside pango and
  cairo. `AGENTS.MD` should be amended to record this.

**2026-08-22 19:15 — six further rulings, all Phase 3 gates now closed:**

- **R3** Theme extension renamed to `.sasi`/`.sasinc`. 35 theme files, the lexer array, and
  the config/system file names. The six gruvbox `@import`s are extension-less and need no edit.
- **R5** Layer-shell namespace and X11 WM_CLASS renamed to `sofi`/`"sofi\0Sofi"`.
- **R6** Helper scripts renamed, no symlinks. `config/config.c:65` must change in the same commit.
- **R9** `sofi.pc` / `$libdir/sofi`, no alias.
- **R11** **xdg-shell fallback IS in scope, as new Phase 2b (~2–3 days).** Takes sofi from
  wlroots-only to usable on GNOME/Mutter and KWin — the largest functional gain available.
- **R12** `AUTHORS`, `CODE_OF_CONDUCT.md` and `releasenotes/` stay deleted. `COPYING` is
  verified present and unmodified, already carrying both required MIT notices.

Net: Phase 3 loses the shim/fallback work but gains 3e (`.sasi`) and 3f (identity/scripts/pkgconfig);
Phase 2 gains 2b.

**Not overridden:** MIT attribution. `COPYING`, `AUTHORS`, the per-file copyright headers and
fork provenance are license obligations. Phase 3c's bulk rename must exclude comment blocks
containing a copyright line, and the phase carries a grep invariant asserting exactly that.

**Consequence to manage:** R2 and R4 both fail *silently*. An existing rofi user gets the
default theme, empty history, and broken script modes with no diagnostic. Phase 3d now
carries required companion work — README/release-note wording, a manpage note, and a
suggested one-shot startup hint when `~/.config/rofi/` exists but `~/.config/sofi/` does not.

## Next 3–5 concrete steps

Ordered. Each requires explicit approval before execution.

1. **Unblock the baseline** (~15 min). Install `devel/bison` and `devel/check`, configure,
   build, run the test suite, and record the true starting state. Note that
   `test/helper-test.c:185` is expected to trip a heap overflow (see finding
   `source/helper.c:1357`), so a clean baseline may not be achievable until step 3.
2. **Phase 1** (~1–2 days) — the five defects the rebrand would cement into a new public API.
3. **Phase 2a** (~2 days) — Wayland/layer-shell correctness, including the `g_error` abort.
4. **Phase 2b** (~2–3 days) — xdg-shell fallback per R11.
5. **Phase 3** (~2–3 days) — the rename, 3a–3g, green build + grep invariant per sub-phase.

Then Phase 4 (FreeBSD CI) and Phase 5 (the 59 medium findings).

**Revised total: ~11–15 days.**

## Where things are

- `AUDIT_REGISTER.md` — all 248 verified findings, ranked, with file:line and fix
- `REBRAND_SURFACES.md` — all 166 identity surfaces, grouped by what breaks
- `DECISIONS_LOG.md` — the nine open questions
- `PLANS.md` — the ordered execution plan
- `BLUEPRINT.md` — system architecture and dependency map
- `TODOS.md` — granular task backlog
- `PROGRESS.md` — macro tracking
- `SESSION_HANDOFF.md` — session continuity
