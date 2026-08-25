# BRIEFING

**Last updated:** 2026-08-25 11:52

## Project

`sofi` — **hikari-sakura's shell.** A hard fork of `rofi`, rebranded, hardened and made portable
to FreeBSD, now specialised into the system surfaces of one compositor: an application menu on a side edge, a
task and window manager along the bottom, a sheet switcher, and a notification system with
history. Phase 10 re-places three of them; see R25–R33.

This is a change of identity recorded in `DECISIONS_LOG.md` R16–R24. sofi is no longer a
rofi/dmenu drop-in that happens to run on hikari.

- ~43,000 lines of C across `source/`, `include/`, `lexer/`, `config/`, `test/`
- Meson build, C99, glib ≥2.72 / cairo / pango / gdk-pixbuf / gio
- Two display backends: `xcb` (X11) and `wayland` (wlr-layer-shell, xdg-shell fallback)
- Companion repository: `/home/orpheus497/Projects/hikari-sakura` — the target compositor
- MIT licensed — attribution obligations survive the rename

## Current phase

**Phase 10 — theming and layout modernisation. Delivered, uncommitted on branch `theme`.**

Phases 8 and 9 and the v1 branding/documentation pass are all committed on `master` (through
`23579673`, PR #3 merged).

USER has installed and restarted, and confirmed it visually: `.github/sofi_screenshot.png` shows
four surfaces up at once and is now the README's header image. The only surface still unseen is the
live notification banner, which is unmapped whenever nothing is on screen.

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
| **Phase 8 — the four native surfaces** | **Done and committed. 19/19 tests** |
| **Phase 9 — notification daemon** | **Done and committed** |
| **v1 branding / user-facing docs** | **Done and committed** |
| **Clean separation from upstream, version 1.0.0** | **Done and committed** |
| **Phase 10 — theming and layout modernisation** | **Delivered. 19/19 tests. Uncommitted on `theme`** |

## Blockers

**None outstanding.** Phase 10 is delivered. The one open item is verification that needs a
compositor restart, not a blocker on the work itself.

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

**2026-08-25 — Phase 10 scoped. R25–R33 ruled, four ambiguities closed by USER.**

- **R25** The application menu — and every general mode that falls through to the same layout —
  moves to the **east** edge, with the edge documented as a one-line override.
- **R26** **One palette, one file.** The sixteen values the USER supplied are already
  hikari-sakura's `ui { palette }`, so sofi adopts the same sixteen positional slots and maps its
  semantic aliases onto hikari's own `colorscheme` slot by slot. `doc/palette.sasi` becomes a
  GResource parsed before every panel layout; the seven inlined, already-drifted copies go. Valid
  because `@` links resolve lazily at property lookup, so user config parsed later still wins.
- **R27** "Modernised" is a stated list — 8px grid, radius scale, fill-plus-marker selection,
  two-tier type, real placeholder colour, themed scrollbars, no new theme properties.
- **R28** Notification **history** moves to bottom-centre; the live banner stays bottom-right so a
  toast never covers the task strip you are aiming at.
- **R29** The sheet switcher moves to top-centre as a horizontal chip row. Measured: sofi's
  "monitor size" on Wayland is hikari's *usable area*, so `north` is already flush below the 34px
  bar and the gap is a deliberate 8px, not compensation.
- **R30** Two cleanup actions, not one: dismiss-all and clear-history, each with a clickable
  button. A clear issued outside the daemon travels over a second D-Bus interface on the existing
  object, because the daemon would otherwise rewrite the file within seconds.
- **R31** The task strip is zoned filter | tasks | count, and leads with the window title rather
  than its class.
- **R32** The notification surfaces are distinguished from the menus by **shape**, not hue —
  cards with urgency stripes against the menus' filled rows.
- **R33** The documentation deliverable is scoped: README theming section, task-first `CONFIG.md`,
  a new `sofi-customisation(5)`, and two manpage corrections.

**Three defects found during the investigation**, all filed rather than silently fixed:

1. The banner's `kb-custom-1` "clear all" has never been reachable — the surface is forced to take
   no keyboard, and nothing exposes the action to the pointer.
2. `doc/panel-window.sasi`'s `y-offset: -12px` puts the bottom 12px of the task strip outside the
   usable area. The two positioning paths negate offsets relative to each other and nothing said so.
3. `doc/panel-notify.sasi:58` claims its 48px offset "clears hikari's own top bar". The bar was
   already cleared by the compositor; the 48px is 48px of empty space.


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

1. **Commit Phase 10**, including the new untracked `.github/sofi_screenshot.png`. Uncommitted on
   branch `theme`.
2. **Confirm the notification banner** — the one surface the screenshot could not show, since it is
   unmapped while nothing is live. `notify-send` against the restarted daemon is enough.
3. **Tag `1.0.0`** — USER's own task, still outstanding.
4. **Phase 4 — FreeBSD CI** (~half a day).
5. **Phase 5 — the 59 medium audit findings** in `AUDIT_REGISTER.md`.

Also still open: the sheets keybinding is now present in `hikari.conf` (`L+e`) but has not been
confirmed reaching the socket, and the icon is still upstream's three-window artwork.

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
