# BRIEFING

**Last updated:** 2026-08-26 09:21

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

**Phase 11 — the system menu. Scoped, planned and approved 2026-08-26. Not yet built.**

Phase 10 is committed and merged (PR #4, `5e36f0a0`). Working on branch `tray`.

Phase 11 has two independent tracks: **A**, repairing the notification history panel, which is
broken in five distinct ways today; and **B**, a StatusNotifierItem system tray landing in the task
strip's right-hand corner. Planned in `PLANS.md`, tasked in `TODOS.md`, ruled in `DECISIONS_LOG.md`
R36–R40.

**One product change is already delivered this session**, on USER's explicit instruction: the task
strip's window counter is removed (`doc/panel-window.sasi`, README). It is **unvalidated** — no
build or `-sasi-validate` has been run, because this session was asked to stay off the shell.

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
| **Phase 10 — theming and layout modernisation** | **Delivered and committed. 19/19 tests. PR #4 merged** |
| **Phase 11 — the system menu (notification repairs + system tray)** | **Approved and planned. Not started** |

## Blockers

**Nothing blocks Phase 11 starting.** A1–A4 and all of Track B are ready to build.

One task is blocked, and only one: **A5 (raise the window that sent a notification) waits on Q20** —
how the history mode reaches `wlr_foreign_toplevel_handle_activate()`, which today lives behind
`wayland-window.c`'s private registry binding. Three routes tabled in `TODOS.md`; (b), extracting a
shared activate-by-app-id helper, is recommended. Nothing else in the phase depends on it.

**Q19** is an open question, not a blocker: the task strip's compositor binding may not actually
toggle. Per R17 each surface holds its own pidfile, so a second `sofi -show window` while one is up
is refused by the instance lock rather than dismissing it. R38's autohide model assumes it toggles.
Needs one check on hardware; changes no part of the design either way.

**Deferred by USER ruling (R37), not cancelled:** all power controls — see `TODOS.md` D1–D5. Lock and
logout are blocked on compositor work that R36 puts out of scope; shutdown, reboot and suspend are
blocked on a privilege ruling, since FreeBSD has no `logind`.

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

**2026-08-26 10:31 — R41. The tray is its own process; R39 narrowed.**

USER rejected hosting the tray inside the notification daemon: *"we can't be mapping multiple things
over one another - it creates larger tech debt and bigger refactors ... later on."* The tray is now
`sofi -tray-daemon`, dispatched before display selection and needing **no display at all** —
StatusNotifierItem is D-Bus and nothing else.

R39 had claimed the notification daemon owns the tray state. F13 only ever supported the weaker
claim, that the host must be **resident**; which resident process was a separate question and is now
answered separately. Co-location is not itself an antipattern — Plasma, GNOME Shell and waybar all
do it — but the argument that stands alone is shared fate: tray code parses hostile input from
arbitrary applications, notifications matter more, and one main loop means one crash takes both.

The challenge also surfaced three concrete hazards, one of them unbuilt: **B4's pixmap decode was
heading for the main loop** (16M pixels at the dimension cap) and is now recorded as B4.0, bound for
the existing worker threadpool; **no debounce** on re-fetch (fixed, 100ms); and a **`g_bus_get_sync()`**
in a path registering applications drive (fixed). Two of the three would have been equally wrong in
a separate process — what the shared loop added was that they cost notifications rather than only
the tray.

The split cost ten lines, because `tray-watcher.c` and `tray-item.c` had never included anything but
`gio` and each other. **Consequence, accepted:** autostart now needs two lines.

**2026-08-26 — Phase 11 scoped. R36–R40 ruled, Q18 closed, Q19/Q20 tabled.**

- **R36** Phase 11 is **sofi-side only**; `hikari-sakura` is not modified. Three items in the phase
  each had a cheaper compositor-side variant that would have pulled work across the repository
  boundary one item at a time.
- **R37** **Power controls deferred**, not descoped, into a register with what unblocks each. Lock
  and logout need a control-socket verb, which the compositor's own `include/hikari/ipc.h` rules out
  ("not a general scripting interface"). Shutdown/reboot/suspend need a privilege ruling — no
  `logind` on FreeBSD. The system menu reserves the zone so lifting the deferral is additive.
- **R38** The tray is the **right-hand zone of the task strip**, not a pane of the notification
  history. Supersedes R31's "count" third; the count is removed on USER's explicit instruction.
  **No always-mapped layer-shell taskbar** — that was my recommendation and USER overruled it; the
  strip keeps its summon/dismiss lifetime.
- **R39** **The daemon owns the tray state; the strip is a pure view onto it.** This is what makes an
  autohide tray coherent: applications register with the watcher once at *their* startup and never
  retry, so a summoned process could never be the host. Hiding the strip destroys a view, not state.
- **R40** Icons cross the process boundary as **bytes, not files** — `icon_set_surface()` takes a
  surface directly, and the file route would add a temp-file lifecycle whose failure mode is stale
  icons surviving a crash.

**Ten findings recorded, F9–F18.** The four that decided the design:

1. **The tray needs nothing from a compositor** (F9) — SNI is pure D-Bus: no protocol, no surface,
   no input routing, no privileged operation. Every argument that normally pulls a shell feature
   into a compositor is absent.
2. **hikari links no D-Bus stack at all** (F10), and `hikari-topbar` links nothing but libc and is
   display-only, *"no click events are handled"* (F11). hikari's own recorded rule points the same
   way (F12) — the same argument R20 used for the notification daemon.
3. **The one-listview abort is irrelevant** (F15). A tray is not a listview; `box_add`,
   `box_find_mouse_target` and `icon_set_surface` are already public, and `mode-switcher` is the
   existing precedent for building N clickable widgets from runtime data. This corrected an earlier
   claim in the same session that the tray needed structural surgery on `source/view.c`.
4. **A tray click must not reuse the button trigger path** (F17), which sets `state->quit` and would
   tear the strip down on every click — the same class of silent failure as the dead dismiss button.

**Five defects found in the notification history panel**, all pre-dating Phase 10, all user-visible.
The severe one: **the daemon never loads its own history**, so the first notification after every
restart truncates the persisted file — history is destroyed at every login.

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

1. **Validate and build the counter removal** (~10 min). `doc/panel-window.sasi` and the README were
   changed this session and nothing has been run against them. `sofi -show window -sasi-validate`,
   then a build.
2. **A1 — the daemon must load its own history** (~30 min). Thirty minutes, and it stops history
   being destroyed at every login. Everything else in Track A is cosmetic until data survives.
3. **A2 + A3 — per-entry verbs over the bus, live state overlaid rather than persisted** (~6h).
   Together these are what make Enter, Shift+Delete, the Dismiss button and the live stripe do
   anything at all in a standalone history panel.
4. **A4** (~1h), then **rule Q20** so **A5** can start (~1 day).
5. **Track B — the tray**, B1 through B9 (~4 days). B2 is the first substantial piece and the
   template for it already exists in `source/notify-service.c`.

Carried, unchanged: **tag `1.0.0`** (USER's own task); **Phase 4 — FreeBSD CI** (~half a day);
**Phase 5 — the 59 medium audit findings** in `AUDIT_REGISTER.md`; the sheets keybinding (`L+e`) has
not been confirmed reaching the socket; the icon is still upstream's three-window artwork.

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
