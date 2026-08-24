# BRIEFING

**Last updated:** 2026-08-24 10:20

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

**Phase 8 complete and verified on hardware. Phase 9 planned and awaiting approval to begin.**

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
| **Phase 9 — notification daemon** | **Planned, N1–N8 scoped. Awaiting approval** |

## Blockers

**None for Phase 9 development.** Three things gate *using* what Phase 8 built:

1. **Nothing is committed** in either repository.
2. **hikari is not rebuilt or installed.** `/usr/local/bin/hikari` is the Aug 22 build with no
   control socket, so the sheets mode correctly reports `ENOENT`. Needs `make clean && make`
   (clean is mandatory — see below), `sudo make install`, and a compositor restart.
3. **No sheets keybinding exists** in `hikari.conf`. `L+s` and `LS+s` are taken.

Two pre-existing compositor issues, reported and not silently worked around:

- **hikari does not build at HEAD with default flags.** `action.o` was stale from a pre-`NDEBUG`
  build and `main.o` was root-owned from an earlier `sudo make`. The Makefile has no header
  dependency tracking, so this recurs after any header edit.
- **`hikari_server_stop()` appears not to run on SIGTERM** on FreeBSD — the control socket
  survived a signal that should have unlinked it. Unconfirmed, but it affects every teardown
  step, not just the socket.

**Process risk:** another agent session was editing `hikari-sakura/.devdocs/` at 09:11–09:15 on
2026-08-24 while `src/ipc.c` was being written there. Neither repository is committed.

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

1. **Commit Phase 8 in both repositories** (~10 min). Nothing is committed and a second agent is
   active in `hikari-sakura`. This is the first thing that should happen.
2. **Rebuild, install, restart** (~20 min + session restart). `make clean && make` in hikari,
   `sudo make install`, `ninja -C build install` in sofi, restart the compositor. Confirm with
   `strings /usr/local/bin/hikari | grep zwlr_foreign_toplevel` before restarting. This is what
   makes the sheet switcher live.
3. **Phase 9 N1 — GDBus skeleton** (~half a day). Own `org.freedesktop.Notifications`, answer
   `GetServerInformation` and `GetCapabilities` honestly, log `Notify`. Verifiable with
   `notify-send` and `dbus-send` before any UI exists.
4. **Phase 9 N2 — daemon lifetime** (~half a day). Gate `view.c:1536`, wire unmap/remap, and
   force R24's two settings. **This is the phase that retires the plan's only unproven
   assumption** — that sofi can idle with no surface and bring one back.
5. **Phase 9 N3 — ring buffer, notifications mode, bottom-right panel** (~1 day).

Then N4–N8, then Phase 4 (FreeBSD CI) and Phase 5 (the 59 medium findings).

**Phase 9 estimate: ~950 lines of new C across 8 sub-phases. No new dependencies** —
`gio-unix-2.0` is already in `deps`.

## Where things are

- `PLANS.md` — Phase 8 (delivered) and Phase 9 (N1–N8) at the top, historical phases below
- `TODOS.md` — the active Phase 9 task list and carried-over items C1–C6
- `DECISIONS_LOG.md` — R16–R24, Q17's closure, and the two retracted claims
- `BLUEPRINT.md` — the implementation registry for the four surfaces, the compositor socket
  contract, and the verified protocol table
- `PROGRESS.md` — what Phase 8 delivered and what it superseded
- `AUDIT_REGISTER.md` — the original 248-finding audit, historical
- `REBRAND_SURFACES.md` — the 166 identity surfaces, historical
- `SESSION_HANDOFF.md` — session continuity
