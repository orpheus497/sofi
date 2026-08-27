# TODOS

**Last updated:** 2026-08-26 22:30

Granular task list. Per `AGENTS.MD`, items enter here as questions tabled under a design
implementation request, move to the active list once scoped in `DECISIONS_LOG.md`, and move
to the implementation registry in `BLUEPRINT.md` on completion.

---

## DEFERRED — awaiting replanning and scoping

Items removed from an active phase by an explicit USER ruling, retained here so the deferral is a
decision on the record rather than an omission. Each names what unblocks it.

| # | Item | Deferred by | Blocked on |
|---|---|---|---|
| D1 | **Power control: shutdown / reboot** | R37, USER 2026-08-26 | A privilege ruling. FreeBSD has no `logind`; needs either `doas`/`sudo`/`operator` rules on this machine, or a ruling that ConsoleKit2 over D-Bus is acceptable as a *client* under `AGENTS.MD` §2 (it is GPL, but sofi would not link it) |
| D2 | **Power control: suspend** | R37, USER 2026-08-26 | Same ruling as D1, for `acpiconf -s3` |
| D3 | **Power control: lock** | R37 + R36, USER 2026-08-26 | **Compositor work.** hikari already locks (`src/lock_mode.c`, setuid PAM `hikari-unlocker`) but exposes it only through its own keybinding. Needs a control-socket verb — which `include/hikari/ipc.h` explicitly rules out growing into ("not a general scripting interface"). Needs a deliberate conversation with the compositor, in its own scoping pass |
| D4 | **Power control: logout** | R37 + R36, USER 2026-08-26 | Same as D3 |
| D5 | **Always-visible tray in the top bar** | R36, USER 2026-08-26 | **Compositor work.** `hikari-topbar` links nothing but libc and is display-only ("no click events are handled"); `src/bar.c` would need a new icon render path *and* click routing. Distinct from the summoned tray in the system menu, which is sofi-side and stays in Phase 11 |

**Design obligation this deferral creates, so it costs nothing to lift later:** the system menu's
layout reserves a power zone and its mode reserves a section. Both render as absent while the
register above is unresolved, and filling them is additive — one layout block and one section in
one mode, with no restructure.

---

## OPEN FINDINGS — from the 2026-08-26 documentation audit

Not blocking anything. Each needs a ruling rather than more investigation.

| # | Finding | Needs |
|---|---|---|
| F19 | ~~`-application-fallback-icon` is a dead option~~ | **CLOSED 2026-08-27 by R52.** USER ruled remove. Deleted from `include/settings.h`, `config/config.c` and `source/xrmoptions.c` — its only three occurrences, none a read. The per-mode `fallback-icon` theme property (`source/mode.c:110`) is untouched and is now the only mechanism. **No migration:** an existing config still setting the key loads with no warning and no error, measured against the new binary |
| F20 | ~~`INSTALL.md` lists X11-only libraries without marking them so~~ | **CLOSED 2026-08-27.** The list is split core / X11 / Wayland, with a note that a Wayland-only build needs the core list and nothing else. The feared "wider rewrite" did not materialise — the section already had a "For wayland support" heading to mirror |
| F25 | **`wayland_pointer_enter()` discards the coordinates the protocol delivers**, so a first click with no intervening motion is tested at `(0,0)` | Still open. Not what USER hit in the tray-menu reports, but the mechanism stands |
| F27 | **`skip_absorb` is write-only** — inherited dead state | Still open. Removing it touches five call sites for no behaviour change |
| F37 | ~~`Exec=sofi -show` in the installed desktop entry launched an error dialog~~ | **CLOSED 2026-08-27.** Fixed to `sofi -show drun`; `Categories` added; both entries pass `desktop-file-validate` clean. **Worth one check on hardware** — selecting Sofi from a desktop menu should now open the application menu |


## DELIVERED — tray menus, 2026-08-26 (F21–F31, R46)

USER: *"the ability to click/right click on the tray icons just makes the panel disappear, it does
not show the menus/submenus"*, then *"cursor becomes a pointer, right click closes, left click does
nothing"*, then *"we need this to be functional."* Ruled in R46: **all seven items, menu in the
strip, switched in place.**

| # | Item | State |
|---|---|---|
| 1 | Call `ContextMenu(x,y)` when the item published no menu | **Done** — `sofi_tray_item_context_menu()`, forwarded through `org.sofi.Tray.ContextMenu` |
| 2 | Menu object path into `ListItems()` | **Done** — signature is now `a(sssssubuuay)`; verified over the bus |
| 3 | `com.canonical.dbusmenu` client on GDBus | **Done** — `source/dbusmenu.c`. **No new dependency**; `libdbusmenu-glib` rejected on licence |
| 4 | A `tray-menu` mode | **Done** — `source/modes/tray-menu.c`, on `filebrowser.c`'s descend/return shape |
| 5 | Wiring: stash target, `MENU_QUICK_SWITCH`, mode into `modes[]` | **Done** — `sofi_enable_mode()`; switch verified in place by screenshot |
| 6 | Right click must stop reaching `kb-cancel` over the tray | **Done** — `SCOPE_MOUSE_TRAY` outranks `SCOPE_GLOBAL`; `kb-cancel`'s default untouched |
| 7 | Middle click → `SecondaryActivate` | **Done** — `mt-secondary-activate` |

**F27 also fixed on the way?** No — `skip_absorb` is still write-only. Left alone deliberately: it
is inherited dead state, removing it touches five call sites for no behaviour change, and this
change was already large. **Still open.**

**F25 remains open and is still real.** `wayland_pointer_enter()` discards the coordinates the
protocol delivers, so a first click with no intervening motion is tested at `(0,0)`. It was **not**
what USER hit — they reported left click doing nothing rather than closing the panel — but the
mechanism stands.

### The one step still needing a human

Everything either side of it is measured (see `PROGRESS.md`): the dbusmenu client against the real
item, the menu path over the bus, submenu descent, `Event` delivery, and the in-place mode switch.
What no harness on this machine can do is **press a mouse button** — `wlrctl`, `ydotool` and `wtype`
are all absent and hikari's IPC has only `state`, `sheet`, `pin`. So `tray_open_menu()` being reached
from a real click is the same gate as **B6.3**, and closing one closes the other.

---

## TABLED QUESTION — Phase 11, unruled

Per `AGENTS.MD` these sit here as questions until ruled in `DECISIONS_LOG.md`. No default is
assumed and nothing below is being built.

### Q21 — CLOSED by R45, 2026-08-26 15:22

Ruled: option (a). Enumerate in `history_mode_init()` where no view exists and the display is idle,
hold the list for the panel's lifetime, and let `_result` do nothing but match, `activate()` and
`wl_display_flush()`. Enumeration went from a flat 2 regardless of the desktop to a count that
tracks it — 7 when the desktop held 7, 6 after one window closed — and the target is now matched. The list is live rather than a snapshot, because the listeners stay attached and sofi's main
loop delivers toplevel events as ordinary traffic.

The original tabling is kept below for the reasoning.

### Q21 — the options as tabled

Q20 chose *where* the helper lives (R43). This is the question its implementation raised and could
not answer: enumerating on demand from inside a mode's `_result` under-reports deterministically — 2
toplevels where the desktop has 7. See the A5.2 note below for the two defects fixed on the way.

| | Route | Cost |
|---|---|---|
| a | **Enumerate in `history_mode_init()`**, hold the list for the panel's lifetime, `activate()` + flush on Enter | Recommended — it is exactly the window mode's proven shape, and the reason that shape exists. Costs ~3 round trips per history summon, which the window mode already pays on every invocation |
| b | Keep enumerating on demand and find why the private queue under-reports | Unbounded: the protocol has no list-complete event and the failure is in libwayland's queue routing under re-entrant dispatch |
| c | Drop A5.2 | The `desktop-entry` hint stays stored and persisted (A5.1), so it can be picked up later at no cost |

Blocks **A5.2 only**. Everything else in Phase 11 is delivered.

### Q20 — CLOSED by R43, 2026-08-26 11:05

Ruled: extract a shared activate-by-app-id helper rather than duplicating ~90 lines or shelling out
through `window-command`. Implemented; the helper is exported from `wayland-window.c` and there is
only one copy of the protocol machinery. Its *timing* remains open as Q21.

### Q19 — does the task strip's keybinding actually toggle?

R38 rests on the strip being shown *and hidden* by its compositor binding. Per R17 each surface
holds its own pidfile, so a second `sofi -show window` while one is up is **refused by the instance
lock**, with the warning going to a stderr nobody reads — that is not a toggle. Unless the binding
passes `-replace` (a kill-and-relaunch flicker, not a toggle) or the compositor binding toggles it
itself, the autohide behaviour may not exist yet.

Changes no part of R38's design — only whether the summon side behaves as USER expects. Needs one
check on hardware, and a ruling only if it turns out not to toggle.

### Q18 — CLOSED by R38, 2026-08-26 09:15

Ruled by USER: the tray is the **right-hand zone of the task strip**, not a pane of the notification
history. Options A–D were all rejected in favour of `icon` widgets packed into the strip's existing
`footer` zone, where the window count sits today. **No always-mapped layer-shell taskbar** — option
B was my recommendation and was overruled. The strip keeps its summon/dismiss lifetime; the daemon
keeps the tray state, which is what makes an autohide tray coherent.

See `DECISIONS_LOG.md` R38–R40 and F15–F18.


## DELIVERED — Phase 11: the system menu

Scoped in `DECISIONS_LOG.md` R36–R45 (F9–F18), planned in `PLANS.md` Phase 11. **Delivered
2026-08-26 on branch `tray`.** Track A and Track B both complete, A5.2 included as of R45.
Registered in `BLUEPRINT.md` per `AGENTS.MD`.

**Three gates could not be closed from a shell** and need the real desktop, none needing code:
**B2.3** (a genuine Qt/GTK tray application registering), **B6.3** (a real pointer click on a tray
icon) and **Q19** (whether the strip's binding toggles). A5.2's final `activate()` shares B6.3's
shape — it needs one human keypress, and the control test in R45 shows the shipped window switcher
is refused the same way under a synthetic one.

### Track A — notification history repairs

| # | Task | Depends on | State |
|---|---|---|---|
| A1.1 | `sofi_notify_store_load()` in `sofi_notify_service_start()`, before the bus name is taken | — | **Done 2026-08-26** |
| A1.2 | **Gate:** populated history survives a daemon restart plus one new notification | A1.1 | **Done — passed, with a baseline run proving the defect** |
| A2.1 | `org.sofi.Notifications` gains `Dismiss(u)`, `InvokeAction(u,u)`, `GetLive() → a(uus)` | — | **Done 2026-08-26** |
| A2.2 | History mode routes per-entry verbs through the existing `history_mutate()` three-way shape | A2.1 | **Done** |
| A2.3 | **Gate:** Shift+Delete retires a live entry *and* its sender gets `NotificationClosed` | A2.2 | **Done — `ActionInvoked` then `NotificationClosed(id,2)` captured on the bus** |
| A3.1 | `history_mode_init()` overlays live-ness from `GetLive()` after `load()`; refresh on every `RELOAD_DIALOG` | A2.1 | **Done** |
| A3.2 | **Gate:** a live entry renders the `@accent-strong` stripe standalone; a retired one renders `@muted` | A3.1 | **Done — screenshot, with the cursor moved off the live row as a control** |
| A4.1 | **R44:** disable the Dismiss button when no daemon is reachable; leave Clear alone | A3.1 | **Done 2026-08-26** |
| A4.2 | **Gate:** Dismiss absent with no daemon, present with one; Clear always present | A4.1 | **Done — both screenshotted** |
| A5.1 | `handle_notify()` stores and persists the discarded `desktop-entry` hint | A2.1 | **Done** — landed with A2, whose `GetLive()` signature carries it |
| A5.2 | Activate-by-app-id from the history mode; absent affordance where correlation fails, never a wrong window | A5.1 | **Done 2026-08-26 (R45)** — enumerate in `_init`, activate in `_result`. Full toplevel list, match verified |

### A5.2 is DONE (R45). What was wrong, and what is verified.

Two defects were found and fixed on the way to the answer, and both are worth keeping:

1. **Re-entrancy.** `wl_display_roundtrip()` dispatches the DEFAULT queue, where sofi's own surface
   events live. Calling one from inside a mode's `_result` re-entered the view machinery mid-teardown
   -- a second entry into the same function, zero toplevels, then a segmentation fault. **This is why
   the window mode only ever roundtrips in `_init`**, a constraint that was implicit in that file and
   is now written down.
2. **A fixed round-trip count is racy**, and a private `wl_event_queue` then under-reported
   deterministically -- 2 toplevels on a desktop holding 7. Enumerating on demand simply does not
   work; the answer was to stop trying and use the window mode's own shape.

**Verified after the rework:** the enumeration matches the desktop — 7 toplevels when it held 7,
6 after one closed, against a flat 2 before — deterministic across five runs, target matched, no
crash, 19/19 tests, and all four CI configurations build under gcc14 and clang.

**One step is not exercisable in a harness, and the reason is not this code.** `wayland->last_seat`
is set only by real input (`wayland_keyboard_enter`, `wayland_keyboard_key`,
`wayland_pointer_button`), so a timer-driven or `-auto-select` action reaches the match and is then
refused with "no seat has been used yet". Confirmed by control rather than assumed: **`sofi -show
window`, the shipped window switcher, reports the identical message under the same timer-driven
action.** The activate call is the one that switcher runs successfully every day. Closing it needs a
human keypress -- the same gap as B6.3 -- and no inference about real keyboard focus should be drawn
from it.

### Track B — system tray

| # | Task | Depends on | State |
|---|---|---|---|
| B1.1 | `box_remove_all()` — the only widget-layer change the tray needs (F16) | — | **Done 2026-08-26** |
| B2.1 | `source/tray-watcher.c` — own `org.kde.StatusNotifierWatcher` + `StatusNotifierHost-<pid>` | — | **Done** |
| B2.2 | Accept **both** registration forms: bus name and object path (sender's unique name) | B2.1 | **Done — both verified on the bus** |
| B2.3 | **Gate:** a Qt and a GTK tray application both register against the running daemon | B2.2 | **Partial — see note** |
| B2.4 | New `tray` meson option → `SYSTEM_TRAY`; errors at configure time if `notify` is off | B2.1 | **Done** |
| B2.5 | Reap items whose bus name vanishes — the spec has no Unregister method at all | B2.1 | **Done — verified end to end** |
| B3.1 | `source/tray-item.c` — `GetAll` with per-property fallback; the `New*` signals | B2.1 | **Done 2026-08-26** |
| B3.2 | `NameOwnerChanged` watch per item — mandatory, apps exit without unregistering | B3.1 | **Done in B2.5; re-verified against a real item** |
| B3.3 | Conditional icon precedence: `NeedsAttention` → attention icon, else name, else pixmap, plus `IconThemePath` (F14) | B3.1 | **Done — the override verified live** |
| B3.4 | Test fixture: a real StatusNotifierItem exporting properties and emitting signals | B3.1 | **Done — `fake-sni.c` in the session scratchpad** |
| B3.5 | **R41:** split the tray out as `sofi -tray-daemon`, dispatched before display selection | B3.1 | **Done 2026-08-26 — verified headless** |
| B3.6 | **R41:** debounce re-fetches (`ITEM_REFETCH_DEBOUNCE_MS`, 100ms) | B3.1 | **Done** |
| B3.7 | **R41:** drop `g_bus_get_sync()` from the item constructor; take the connection from the watcher | B3.1 | **Done** |
| B4.0 | ~~**R41:** the pixmap decode goes in the worker threadpool~~ | B3.1 | **Superseded by R42** — bounded instead of threaded; see note |
| B4.1 | `IconPixmap` decode — byte-swap, premultiply, validate byte count against geometry, cap dimensions (R40) | B3.3 | **Done 2026-08-26** |
| B4.2 | **Gate:** a malformed pixmap is refused with a warning and falls back to the name | B4.1 | **Done — six cases, all verified** |
| B4.3 | **R42:** cap at 512px and decode inline+lazily rather than in a threadpool | B4.1 | **Done** |

**B4.0 was superseded by R42, and the reversal is deliberate rather than dropped.** Threading was
required to defend against 16M pixels of work, a figure that came from the 4096px cap inherited from
`image_from_hint()` — correct for a notification image, wrong for a tray icon. Capped at 512 the
worst case is ~1ms, and R41 already moved the tray to its own process, so an inline stall is bounded
to the tray anyway. The threadpool route remains available: the decode is one static function behind
a lazy accessor.

| B5.1 | `org.sofi.Tray`: `ListItems()`, `Activate`, `SecondaryActivate`, `Changed` | B3.1, B4.1 | **Done 2026-08-26** |
| B5.2 | **Its own bus name**, not a second interface on the watcher's — sofi may not own that one | B5.1 | **Done** |
| B5.3 | **Gate:** `Activate` reaches the application with coordinates; a stale service is harmless | B5.1 | **Done — first end-to-end activation test** |
| B6.0 | `source/tray-client.c` — read `org.sofi.Tray` from the strip; `icon_set_fetch_id()` on the icon widget | B5.1 | **Done 2026-08-26** |
| B6.1 | Tray zone built from `icon` widgets at runtime, `mode-switcher` as the model (F15) | B1.1, B5.1 | **Done** |
| B6.2 | **Its own trigger handler that does NOT set `state->quit`** (F17) | B6.1 | **Done — by construction; see the gap note** |
| B6.3 | **Gate:** clicking a tray icon activates the item and the strip is still on screen | B6.2 | **NOT VERIFIED — needs a real pointer click** |
| B7.1 | `doc/panel-window.sasi` — `"tray"` as the third `mainbox` child, **no** hairline | B6.1 | **Done** |
| B7.2 | **Gate:** `-sasi-validate` clean, and geometry unchanged when there are no tray items | B7.1 | **Done — both cases screenshotted** |

**B6.3 is the one thing in Track B that could not be scripted.** There is no way to synthesise a
pointer click at a screen coordinate from a shell, so "the strip survives a tray click" rests on
construction rather than observation: `tray_icon_trigger_action()` never touches `state->quit`, and
the only path that sets it for a custom action is `sofi_view_trigger_global_action()`, which this
handler deliberately does not call. Activation itself *was* proven end to end in B5.3. Closing this
needs one real click on a running strip.

**The hairline was dropped rather than restored, against what B7.1 originally said.** A box with no
children still has padding, therefore width, therefore a drawn border — and an empty tray is the
ordinary case on a session with no tray daemon. A rule floating in an empty corner reads as a
defect. Separation comes from `mainbox`'s own spacing. Verified by screenshotting the empty case.
| B8.1 | Subscribe to `Changed`; rebuild the zone through B1.1 while the strip is open | B5.1, B6.1 | **Done 2026-08-26** |
| B8.2 | **Gate:** an application registering while the strip is open appears in it | B8.1 | **Done — screenshotted, 58ms** |
| B9.1 | README tray section + surface table + autostart; `CONFIG.md` recipe; `sofi-customisation(5)` widget names; `sofi(1)` flag and SYNOPSIS | B7.1 | **Done — 11 manpages regenerate** |

**B2.3 is partial, and the gap is worth stating.** Both registration forms were exercised against
the running watcher over a real bus, and both work — but by synthetic callers, not by a Qt and a GTK
application. That proves the code path and not the toolkits' actual behaviour, which is where SNI
interop usually goes wrong. Close it by starting the daemon with a real tray application running
and reading `RegisteredStatusNotifierItems`; it needs no code, only a desktop with something in the
tray.

**Not in Phase 11**, each a decision rather than an omission: power controls (R37, D1–D5); any
compositor change (R36); an always-mapped taskbar (overruled by USER, R38); dbusmenu context menus
(F18 — v1 is `Activate`/`SecondaryActivate`); XEmbed (X11 only).

---

## ACTIVE — Phase 10: theming and layout modernisation

Scoped in `DECISIONS_LOG.md` R25–R33, planned in `PLANS.md` Phase 10. **Approved and delivered
2026-08-25.** Every task below is complete except T5.7, which is partially complete — see the note
under the table. Moved to the implementation registry in `BLUEPRINT.md`.

| # | Task | Depends on | State |
|---|---|---|---|
| T1.1 | `doc/palette.sasi` — 16 positional slots + 14 semantic aliases as `@colorN` refs | — | **Done** |
| T1.2 | Register `/org/sofi/palette.sasi` in `resources/resources.xml` | T1.1 | **Done** |
| T1.3 | Parse the palette before the panel layout in `source/sofi.c`, same failure handling | T1.2 | **Done** |
| T1.4 | Strip the inlined `* { }` colour block from all six layouts | T1.3 | **Done** |
| T1.5 | Regenerate `sofi-config/colors-default.sasinc` + `config.sasi` from the same source | T1.4 | **Done** |
| T1.6 | **Gate:** no hex outside the palette files; **gate:** every text-on-fill pair passes WCAG AA, computed and recorded | T1.5 | **Done** |
| T2.1 | `doc/default_theme.sasi` → east, 300px, R27 grid/radius/type/marker | T1 | **Done** |
| T2.2 | `icon-search` in the inputbar, `textbox-count` on `num-filtered-rows` at the foot | T2.1 | **Done** |
| T2.3 | Verify against `run`, `ssh`, `combi`, `filebrowser` — this layout is the fallthrough for all of them | T2.2 | **Done** |
| T3.1 | `doc/panel-window.sasi` → `[ "inputbar", "listview", "textbox-count" ]`, hairline zone separators | T1 | **Done** |
| T3.2 | `window-format` inverted to lead with the title, class demoted to dim `<small>` | T3.1 | **Done** |
| T3.3 | **Fix F4:** `y-offset: -12px` → `+12px`; comment the sign convention on the software path | T3.1 | **Done** |
| T4.1 | `doc/panel-sheets.sasi` → `north`, centred, `y-offset: 8px`, horizontal chip row | T1 | **Done** |
| T4.2 | `source/modes/sheets.c:362` display value shortened for a chip | T4.1 | **Done** |
| T4.3 | Verify chip positions are stable between invocations; pad to constant width if not | T4.2 | **Done** |
| T5.1 | `sofi_notify_store_clear_history()` — retire live, free ring, delete file, cancel timers | — | **Done** |
| T5.2 | Second D-Bus interface `org.sofi.Notifications` on the existing object; `DismissAll`, `ClearHistory` | T5.1 | **Done** |
| T5.3 | History mode `kb-custom-1`/`kb-custom-2`; direct call in-daemon, D-Bus otherwise, file fallback when unowned | T5.2 | **Done** |
| T5.4 | `-notification-clear` / `-notification-clear-history` one-shot flags | T5.2 | **Done** |
| T5.5 | `doc/panel-notifications.sasi` — cards, stripe, header, `button-clear-all` (makes the existing F5 dismiss-all reachable) | T1, T5.3 | **Done** |
| T5.6 | `doc/panel-notification-history.sasi` → `south` centred, 520px, both buttons, stripe selection (R32) | T1, T5.3 | **Done** |
| T5.7 | Four surfaces up at once; measure real geometry and record it in `BLUEPRINT.md` | T2–T5 | **Done** |
| T6.1 | `README.md` — corrected geometry table + new Theming section (slots, semantics, overrides, precedence) | T5.7 | **Done** |
| T6.2 | `CONFIG.md` restructured task-first | T6.1 | **Done** |
| T6.3 | `doc/sofi-customisation.5.markdown` (new) + `doc/meson.build` + README manpage list | T6.1 | **Done** |
| T6.4 | `doc/sofi-theme.5.markdown` palette-override section; correct "Default theme loading" | T6.1 | **Done** |
| T6.5 | `doc/sofi.1.markdown` — the two new flags | T5.4 | **Done** |

**T5.7 closed 2026-08-25 11:52** by a USER screenshot at `.github/sofi_screenshot.png`, which
shows four surfaces up at once on the installed build: the sheet chips in the new `N · M` form
under the top bar, the application menu at bottom centre reading `52 / 52` with a full-height
scrolling list (confirming the R35 fix), the notification history on the east edge with its
`0 shown` count and both cleanup buttons, and the zoned task strip along the bottom. **Still not
seen: the live notification banner** — nothing was on screen at the time, which is exactly when
that surface is unmapped.

The original note, kept for the record:

**T5.7 was partial, and this was the one thing Phase 10 did not finish.** Measured on the live
compositor: the usable area (1920 × 1166, so a 34px bar), the `-e` toast at 380 × 55, and the
menus' capture surface at 1920 × 1166 anchored TOP\|LEFT. Every surface was launched against the
running hikari and exited cleanly with no warnings. **Not done:** the four surfaces have not been
seen up at once, and nothing has been confirmed visually. Both need the daemon replaced, which
means `ninja -C build install` and restarting `sofi -notification-daemon` — the running one is a
stale `2.0.0-dev` build. That is USER's call, not something to do to a live session unasked.

---

## DELIVERED — Phase 9: notification daemon

Scoped in `DECISIONS_LOG.md` R20–R24, planned in `PLANS.md` Phase 9. **Delivered and committed
2026-08-24**; the table below still reads "Ready" throughout because it was never marked up on
completion, and is kept for the task breakdown rather than for its state column.

**Corrected 2026-08-26.** Phase 11 found four defects in what this phase delivered, all of which
this table would have called done: the daemon never loaded its own history (destroying it at every
login), `live` and `actions` were never persisted so every guard on them was dead code outside the
daemon, the per-entry verbs had no bus route, and the `desktop-entry` hint was parsed and discarded.
See `PROGRESS.md` 2026-08-26.

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
