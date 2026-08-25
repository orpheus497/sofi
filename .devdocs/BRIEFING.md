# BRIEFING

**Last updated:** 2026-08-25 08:30

## Project

`sofi` — **hikari-sakura's shell.** A hard fork of `rofi`, rebranded, hardened and made portable
to FreeBSD, now specialised into the four system surfaces of one compositor: an application menu
on the left edge, a task and window manager along the bottom, a sheet switcher on the right, and
a notification system.

This is a change of identity recorded in `DECISIONS_LOG.md` R16–R24. sofi is no longer a
rofi/dmenu drop-in that happens to run on hikari.

- ~43,000 lines of C across `source/`, `include/`, `lexer/`, `config/`, `test/`
- Meson build, C99, glib ≥2.72 / cairo / pango / gdk-pixbuf / gio
- Two display backends: `xcb` (X11) and `wayland` (wlr-layer-shell, xdg-shell fallback)
- Companion repository: `/home/orpheus497/Projects/hikari-sakura` — the target compositor
- MIT licensed — attribution obligations survive the rename

## Current phase

**Phase 9 delivered and committed. v1 branding and documentation pass complete (uncommitted, on
branch `docs`).**

## Progress

| Item | State |
|---|---|
| Phases 0–3 — baseline, pre-rename fixes, Wayland, the rename | Done |
| Phase 4 — FreeBSD CI | Not started |
| Phase 5 — the 59 medium audit findings | Not started |
| Phase 6 — ship the default config | **Superseded by R16** — layouts are compiled in |
| Phase 7a — window switcher | Done. Works with no sofi changes |
| Phase 7b — workspace switcher | **Superseded by R19** — socket, not `ext_workspace_v1` |
| Phase 7c — task manager | **Done** — minimise/maximise, minimised bit surfaced |
| **Phase 8 — the four native surfaces** | **Done. 19/19 tests. Uncommitted** |
| **Phase 9 — notification daemon** | **Done and committed** |
| **v1 branding / user-facing docs** | **Done. Uncommitted, on branch `docs`** |
| **Clean separation from upstream, version 1.0.0** | **Done. Uncommitted, on branch `docs`** |

## Blockers

**None for the v1 documentation work.** Phase 8 and Phase 9 are both committed on `master`
(through `53488f50`). The branding/docs pass is uncommitted on branch `docs`.

Outstanding for *using* the sheet switcher on hardware:

1. **hikari may still need rebuilding and installing.** The sheets mode reports `ENOENT` until
   `/usr/local/bin/hikari` serves a control socket at `$XDG_RUNTIME_DIR/hikari.sock`. Needs
   `make clean && make` (clean is mandatory — see below), `sudo make install`, and a compositor
   restart.
2. **No sheets keybinding exists** in `hikari.conf`. `L+s` and `LS+s` are taken.

Two pre-existing compositor issues, reported and not silently worked around:

- **hikari does not build at HEAD with default flags.** `action.o` was stale from a pre-`NDEBUG`
  build and `main.o` was root-owned from an earlier `sudo make`. The Makefile has no header
  dependency tracking, so this recurs after any header edit.
- **`hikari_server_stop()` appears not to run on SIGTERM** on FreeBSD — the control socket
  survived a signal that should have unlinked it. Unconfirmed, but it affects every teardown
  step, not just the socket.

## Recent architectural decisions

**2026-08-24 — sofi becomes the shell. R16–R24 ruled, Q17 closed.**

- **R16** Four compiled-in panel layouts selected by mode. No config file required for any
  surface. User config still overrides.
- **R17** Instance locks are per surface, so the panels can coexist.
- **R18** Layer-shell bound at v4, not v1. `ON_DEMAND` keyboard interactivity is unreachable
  below v4 and wlroots degrades the request silently rather than erroring.
- **R19** Sheet control travels over a hikari control socket, not `ext_workspace_v1`. Smaller on
  both sides, and the only route that expresses send-to-sheet.
- **R20** The notification daemon is built in sofi, not the compositor. The decisive argument is
  hikari's own, recorded in `src/topbar.c` about its own bar: in-process work stalls the Wayland
  event loop. Notifications are the harder case — arbitrary apps, arbitrary payloads, on their
  schedule. Lock-screen leakage, the one argument for compositor-side, is already handled by
  `lock_mode.c:988-992` disabling the overlay layer.
- **R21/R22/R23** Notification history is a ring buffer from day one; the stack renders
  bottom-right; `urgency=2` never expires.
- **R24** The daemon's `click-to-exit: false` and `keyboard-interactivity: on-demand` are forced
  in code, not read from a theme. Either one wrong makes the desktop unusable for the session.

**Q17 closed.** Send-to-sheet was recorded as inexpressible by any standards-track protocol. The
socket's `pin <n>` does it, and it is verified.

**Method ruling.** Compositor capability claims are settled by a `wl_registry` dump, never by
inference from binaries or timestamps. Two claims were retracted this session for exactly that
mistake; see `PROGRESS.md`.

## Next 3–5 concrete steps

Ordered. Each requires explicit approval before execution, per `AGENTS.MD`.

1. **Commit the branding/docs pass** (~5 min). It is uncommitted on branch `docs` while the code
   it documents is on `master`. Merge or rebase onto `master`.
2. **Tag `1.0.0`** — USER's own task. `meson.build` is set to `1.0.0` and the repository has no
   tags yet; `sofi -v` picks the tag up through `git describe` once it exists.
3. **Rebuild, install, restart** (~20 min + session restart) to make the sheet switcher live, and
   add its keybinding to `hikari.conf`.
4. **Phase 4 — FreeBSD CI** (~half a day). `.build.yml` targets sourcehut and is written; the
   GitHub `build.yml` now correctly triggers on `master`, which it never did before.
5. **Phase 5 — the 59 medium audit findings** in `AUDIT_REGISTER.md`.

Optional, noted during the docs audit: a purpose-designed logo — the icon is still upstream's
three-window artwork with one letter changed, and is the last piece of inherited visual identity.

**Constraint of record:** MIT requires the retained copyright notices in ~90 source files,
`COPYING` and `AUTHORS`. They are not removable. Everything else from upstream has been stripped.

## Where things are

- `PLANS.md` — Phase 8 (delivered) and Phase 9 (N1–N8) at the top, historical phases below
- `TODOS.md` — the active Phase 9 task list and carried-over items C1–C6
- `DECISIONS_LOG.md` — R16–R24, Q17's closure, and the two retracted claims
- `BLUEPRINT.md` — the implementation registry for the four surfaces, the compositor socket
  contract, and the verified protocol table
- `PROGRESS.md` — what Phase 8 delivered and what it superseded
- `AUDIT_REGISTER.md` — the original 248-finding audit, historical
- `REBRAND_SURFACES.md` — the 166 identity surfaces, historical. Superseded for user-facing
  surfaces by the 2026-08-25 audit in `PROGRESS.md`
- `SESSION_HANDOFF.md` — session continuity
