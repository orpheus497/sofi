# BRIEFING

**Last updated:** 2026-08-29 10:39

## Project

`sofi` — the **Sakura Official Full Indexer**, the **UI display and layer-shell layer of
hikari-sakura**. A hard fork of `rofi`, rebranded, hardened and made portable to FreeBSD, now
specialised into the system surfaces of one compositor: an application menu, a task and window
manager along the bottom, a sheet switcher, a notification system with history, and a system tray.

**One of three programs built as a set** (R48): the `sakura` display manager starts a session,
`hikari-sakura` runs it, sofi is its shell. Joined by published interfaces and one sixteen-slot
palette, not by private coupling — each is independently usable.

This is a change of identity recorded in `DECISIONS_LOG.md` R16–R24, and named in R47–R51. sofi is
no longer a rofi/dmenu drop-in that happens to run on hikari.

- ~43,000 lines of C across `source/`, `include/`, `lexer/`, `config/`, `test/`
- Meson build, C99, glib ≥2.72 / cairo / pango / gdk-pixbuf / gio
- Two display backends: `xcb` (X11) and `wayland` (wlr-layer-shell, xdg-shell fallback)
- Companion repository: `/home/orpheus497/Projects/hikari-sakura` — the target compositor
- MIT licensed — attribution obligations survive the rename

## Current phase

**RESOLVED 2026-08-29 10:39 — the originating report is closed and measured.**

USER, after rebooting with the compositor's P-1 in place: *"it seems ot have all been resolved."*
**Confirmed by measurement rather than left on the hedge**, using `WAYLAND_DEBUG=1` to read the
compositor's own `wl_surface.enter` — which is the compositor telling the client where it put the
surface, so sofi cannot be mistaken about it:

| Summon | Lands on | |
|---|---|---|
| default (no `-monitor`), active output DP-3 | `wl_output#14` = **DP-3** | correct |
| `-monitor eDP-1` while active output is DP-3 | `wl_output#12` = **eDP-1** | correct — a real override, not a coincidence |
| `-monitor DP-3` | `wl_output#14` = **DP-3** | correct |

**The contradiction that opened the investigation no longer exists:** `state` reported DP-3 while the
surface was drawn on eDP-1; now both say DP-3.

**R53 is vindicated by the shape of the fix — not one line of sofi's placement code changed.** The
route it refused would have put a compositor-specific call on the core surface path to work around a
compositor bug, and would now be permanent dead weight.

**S-B's gated check has run and passes.** Three tracker entries warned it would show nothing until
P-1 shipped and said not to read that as failure. P-1 shipped; named-output pinning is the
deterministic override the documentation claims.

Both trees rebuilt and installed by USER — `hikari` 10:27, `sofi` 10:36. **Phase 13 is live.**

**Phase 13 — multi-screen client audit. COMPLETE 2026-08-29. All five work packages delivered.**

**Delivered and verified:** clean build, **19/19 tests**, six layouts pass `-sasi-validate`, 11
manpages regenerate, no warning from either changed file.

* **S-A1 closes F38's undefined behaviour.** `source/theme.c:1601` zero-initialises `workarea mon`
  before `monitor_active()` — which, on Wayland, is a stub that returns `FALSE` without writing it.
  Zeroed rather than skipped because the same traversal also strips `@media` blocks out of the widget
  tree and has to run either way. **No shipped layout uses `@media`**, so no panel's appearance can
  have changed.
* **S-C closes F40.** `zxdg_output_unstable_v1` is bound; `DP-3` now reports `position: 1920,0` where
  both outputs previously read `0,0`. **This also measured the one quantity the multi-screen
  diagnosis had been inferring** — that `eDP-1` holds layout origin — from the client side, with no
  restart and no config change, replacing a compositor-side test that was struck as circular.

**R54 (ruling):** placement follows the compositor's **focused** output, not the pointer directly.
Focus tracks the pointer once the compositor's focus path is fixed, **except on a keyboard or gesture
focus change** (`workspace-cycle-next`/`-prev`, 3-finger swipes) — where a menu opens on the focused
screen rather than the one holding the mouse. **USER ruled that deliberate.** R53 is unaffected: sofi
still selects nothing.

* **S-A2 makes the `@media` queries correct, not merely safe.** `wayland_display_monitor_active()`
  reports the monitor's dimensions and returns `TRUE`. Two design changes came out of building it.
  **The dimensions had to be captured rather than read on demand** — `layer_width/height` is
  overwritten with the *window's* size once a view exists, so new `output_width`/`output_height`
  fields take the value from the first configure, when the surface is still anchored to all four
  corners at size zero. And **a zero-initialised struct is not a sufficient safety net**, which
  S-A1's scoping had assumed: with a zero monitor `max-width` matches *anything*, so an unanswerable
  query would silently apply its block. A `gboolean mon_known` is now carried alongside the struct
  and the monitor-dependent constraints are refused as a group; `enabled:` is exempt and still works.
  **`monitor-id` is refused on its own terms** — the dimensions are known when the theme resolves,
  the identity is not.
* **S-B warns on a `-monitor` value that cannot be honoured**, with distinct messages for a position
  specifier and for a name matching no output. **The compiled-in default `-5` is deliberately demoted
  to `g_debug`** — the first implementation warned on every single run, since every ordinary
  invocation carries that default.
* **S-D documents all of it**, including **F42**, found while writing it: `-monitor -3` overrides
  `location` on *both* backends (`source/helper.c:798`), so under Wayland it moves the window while
  the monitor selection itself is ignored. Documented, not changed.

**F41 is partly retracted.** `README.md:492` sits under the heading `### Missing features in Wayland
mode` and was **already correct**; the finding came from reading the line without its heading. Two
documents right, one wrong — the manpage — not three-one.

**Nothing in Phase 13 changes where menus appear.** That remains the compositor's P-1, owned by the
concurrent `hikari-sakura` session.

**Phase 13 as originally scoped — audit and rulings, 2026-08-29 08:56.**

Ruling **R53**, findings **F38–F41**. USER reported the shell only ever appearing on the built-in
screen with two attached. **The cause is not in this tree.** sofi passes `wl_output = NULL` to
`zwlr_layer_shell_v1_get_layer_surface()` — correct layer-shell behaviour, since the protocol makes
placement the compositor's decision when the client declines it — and the compositor draws the
surface on the wrong output. **Cleared in one query, before any client-side reading:** the
compositor's IPC answered `state` with `output DP-3` while sofi was rendering on `eDP-1`. A
compositor contradicting itself is not something a client can cause or cure. Recorded in the sibling
tree; not restated here. Live topology: `eDP-1` 1920x1200, `DP-3` 1920x1080.

**R53 — sofi does not select an output.** The route was costed and refused: the compositor's `state`
verb already returns `output <name>` and `source/modes/sheets.c` already speaks that socket, so it
needed no new dependency. It would put a compositor-specific call on the **core surface path**, work
on one compositor and nowhere else, and is redundant for the default — `NULL` already means "the
compositor chooses". **`-monitor -1..-5` on Wayland is out of scope permanently, not deferred.**

**Four client defects found on the way, none of them the cause.** **F38 is the one that matters and
is gated on nothing:** `source/theme.c:1601-1604` calls `monitor_active(&mon)` on an uninitialised
`workarea`, and the Wayland implementation is a `// TODO: do something?` stub that returns `FALSE`
without writing it — so **every `@media` query in every theme is tested against indeterminate memory
on the only backend this desktop runs.** Silent and data-dependent, which is why it survived twelve
phases. F39: the `-monitor` specifiers miss a name lookup in silence, identically to a typo. F40: no
`zxdg_output_manager_v1`, so `sofi -h` reports `position: 0,0` for **both** screens — wrong since
the Wayland backend existed. F41: `sofi(1)` and `README.md` document `-1..-5` with no caveat while
`FEATURES.md:955` has it right.

Plan in `PLANS.md` Phase 13; tasks in `TODOS.md`. **Recommended order S-A1 → S-C → S-A2 → S-D → S-B**
— S-A1 is one line and removes undefined behaviour on its own. **S-B builds fine and changes nothing
observable** until the compositor draws layer surfaces on the output it selected; do not read that
null result as a failed change.

**Phase 12 — identity, branding and the documentation suite. Delivered 2026-08-27.**

Rulings R47–R51, findings F32–F37. The acronym, the three-program set and the compositor-layer role
had **never been written down anywhere in the tree** — a grep for each returned nothing. Delivered:
`README.md` rewritten around the indexer framing; a new root **`FEATURES.md`**, the reference by
capability that the flag-indexed manpages never provided; `CONFIG.md` and `INSTALL.md` reframed;
`sofi(1)` given a new NAME/DESCRIPTION **and a FILES section it never had**; `CONTRIBUTING.md`, both
desktop entries, `sofi.pc.in` and `meson.build` updated.

**A new application icon (R51)** — a five-petal sakura blossom hand-authored from the palette slots,
replacing upstream rofi's three-window artwork. That was the last upstream asset in the tree, and it
embedded the upstream author's home directory in an installed file.

**Three defects found and fixed on the way, none of them looked for:** the installed desktop entry
launched an error dialog on every use (F37), it had no `Categories` key, and `INSTALL.md` listed
X11-only libraries as unconditional (F20, previously tabled).

**Phase 11 — the system menu. Delivered on branch `tray`, PR #5 open.**

Phase 10 is committed and merged (PR #4, `5e36f0a0`).

**Track B — the system tray — is complete.** `sofi -tray-daemon` owns
`org.kde.StatusNotifierWatcher`, needs no display, and feeds the task strip's right-hand corner
through `org.sofi.Tray`. Four new modules, one new build option, verified end to end against a
purpose-built StatusNotifierItem fixture.

**Track A — notification history repairs — is complete.** A1–A5 are delivered and verified. A5.2
(raise the window that sent a notification) needed a rework and a fourth ruling, **R45**; the one
step it cannot demonstrate from a harness is named under Blockers, and the reason is not this code.

`19/19` tests, clean build under gcc14 and clang, all four CI configurations, all layouts validate,
11 manpages regenerate. **Nothing in this phase has run outside test harnesses on the real
session** — that is still the largest untested surface.

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
| **Phase 11 Track A — notification history repairs** | **Complete. A1–A3 done, A4 rescoped by R44, A5 done — A5.2 reworked under R45** |
| **Phase 11 Track B — system tray** | **Complete. B1–B9 delivered, one gate unverifiable without a real pointer click** |
| **Phase 12 — identity, branding, documentation suite, new icon** | **Delivered 2026-08-27. 19/19 tests, 11 manpages, all layouts validate** |

## Blockers

**None. Phase 11's four desktop gates are all closed as of 2026-08-29.**

| Gate | Closed by |
|---|---|
| **B2.3** — a genuine Qt/GTK tray application registers with the watcher | USER on hardware, 09:59. Previously only a purpose-built fixture |
| **B6.3** — a real pointer click activates a tray icon and the strip survives | USER on hardware, 09:59. Previously rested on construction, not observation |
| **A5.2** — a history entry raises the window that sent it | USER on hardware, 09:59 |
| **Q19** — does the strip's binding toggle? | **R55, 10:14. It does not, and is not meant to.** The binding summons; **Escape dismisses** |

**R55 corrects an assumption rather than fixing a defect.** All four bindings are `-show` in both
`~/.config/hikari/hikari.conf:449-452` and `/usr/local/etc/hikari/hikari.conf:482-485`, and **sofi has
no `-hide` option at all** — so no binding could ever have closed a surface. R38's autohide model
assumed a toggle; the assumption was wrong and the behaviour it assumed was never wanted. **No code
change, and none is proposed.**

**The tray menus are directly observed, not inferred.** USER confirmed submenus appear on a real
click, so F21–F31 / R46 no longer rests on the B6.3 identity recorded at `TODOS.md:129-133`.

**Nothing in sofi is waiting on an observation. PR #5 has no outstanding gate.**

**Deferred by USER ruling (R37), not cancelled:** all power controls — `TODOS.md` D1–D5. Lock and
logout are blocked on compositor work that R36 puts out of scope; shutdown, reboot and suspend on a
privilege ruling, since FreeBSD has no `logind`.

**One artefact I could not clean.** A stray `A1 gate` entry sits in
`~/.cache/sofi/notifications.history`: the first gate isolated `XDG_CACHE_HOME` inside the D-Bus
session rather than before it, so a system service file let `notify-send` activate a second daemon
that inherited the real environment. The file was already 0 bytes so nothing was lost, and the
sandbox blocked restoring it. The running daemon overwrites that file on its next change.

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
2. ~~**No sheets keybinding exists** in `hikari.conf`.~~ **Retired 2026-08-27 (F35).** It does:
   `hikari-sakura/etc/hikari/hikari.conf` binds `"L+e" = action-sheets`, alongside `action-menu`
   (`L+Space`), `action-windows` (`L+w`) and `action-notifications` (`L+n`). All four sofi surfaces
   are bound in the shipped compositor config. Whether `L+e` reaches the socket is still unconfirmed
   on hardware.

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

0. **Look at the new icon** (~2 min). `data/sofi.svg`, rendered at 16/24/32/48/64. It is the one
   Phase 12 deliverable that is a matter of taste rather than of fact, and the only one that cannot
   be verified by a gate.
1. **Install and restart, then use it** (~30 min of USER's time). Nothing in Phase 11 has run
   outside test harnesses on the real session, and that is now the whole of the remaining risk.
   `ninja -C build install`, then autostart **two** lines now: `sofi -notification-daemon &` and
   `sofi -tray-daemon &`. The installed desktop entry is also worth one check — it launched an
   error dialog before F37 and now opens the application menu.
2. ~~**Close the four desktop-only gates.**~~ **Three of the four are CLOSED 2026-08-29 by USER on
   hardware** — B2.3 (a real Qt/GTK tray application registers), B6.3 (clicking a tray icon activates
   it and the strip stays up) and A5.2's `activate()` (a history entry raises its window):
   *"all tested and verified."* **B6.3 also closed the tray-menu gate by construction**
   (`TODOS.md:129-133`), so F21–F31/R46 is verified end to end. **Only Q19 remains** — whether the
   strip's binding actually toggles it, which needs no code either way.
3. **Merge PR #5.** The gates it was waiting on are clean; only Q19 is outstanding and it changes
   nothing in R38's design.
4. **Tag `1.0.0`** — USER's own task, still outstanding.

Carried, unchanged: **Phase 4 — FreeBSD CI** (~half a day); **Phase 5 — the 59 medium audit
findings** in `AUDIT_REGISTER.md`; the sheets keybinding (`L+e`) has not been confirmed reaching the
socket; the icon is still upstream's three-window artwork; **B7 backlog** — the vendored libnkutils
heap-overflow, unreachable from sofi but it costs the ASAN suite one test.

**Constraint of record:** MIT requires the retained copyright notices in ~90 source files,
`COPYING` and `AUTHORS`. They are not removable. Everything else from upstream has been stripped.

## Where things are

- `PLANS.md` — Phase 8 (delivered) and Phase 9 (N1–N8) at the top, historical phases below
- `TODOS.md` — the active Phase 9 task list and carried-over items C1–C6
- `DECISIONS_LOG.md` — R16–R24, Q17's closure, and the two retracted claims
- `BLUEPRINT.md` — the implementation registry for every surface, the system tray architecture and
  its `org.sofi.Tray` contract, the notification bus contract, the compositor socket
  contract, and the verified protocol table
- `PROGRESS.md` — what Phase 8 delivered and what it superseded
- `AUDIT_REGISTER.md` — the original 248-finding audit, historical
- `REBRAND_SURFACES.md` — the 166 identity surfaces, historical. Superseded for user-facing
  surfaces by the 2026-08-25 audit in `PROGRESS.md`
- `SESSION_HANDOFF.md` — session continuity
