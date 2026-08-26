# PLANS

**Last updated:** 2026-08-25 10:53

Forward-looking execution strategy.

> **Scope note (2026-08-24).** Phases 0–7 below were written while sofi was a rofi drop-in being
> rebranded. Per `DECISIONS_LOG.md` R16–R24 the project is now hikari-sakura's shell. **Phase 8 is
> delivered and Phase 9 is the active plan**; both are at the top of this document, ahead of the
> historical phases.

---

## Phase 11 — the system menu: notification repairs and the system tray (ACTIVE, approved 2026-08-26)

Scoped in `DECISIONS_LOG.md` R36–R40, with findings F9–F18. Estimated **5–6 days**.
**No new dependencies** — `gio-unix-2.0` is already unconditional (`meson.build:67`).
**No compositor changes** (R36). **No power controls** (R37, deferred as `TODOS.md` D1–D5).

### Two tracks, and why they are ordered this way

**Track A — the notification history panel is broken now.** Five defects, all user-visible, all
pre-dating Phase 10. Independent of the tray.
**Track B — the system tray.** New work.

**A before B.** A is a regression the USER is living with; B is a feature nobody has yet. They share
no code beyond the `org.sofi.*` interface pattern, so B does not benefit from waiting, but A does
not benefit from being interrupted either.

Within A: **A1 first, unconditionally.** It is thirty minutes and it stops history being destroyed
at every login; everything else in A is cosmetic until data survives. **A2 before A3**, because A3
consumes the bus surface A2 adds.

---

### TRACK A — notification history repairs

#### A1 · The daemon must load its own history — ~30 min

`sofi_notify_service_start()` (`source/notify-service.c:590`) calls `sofi_notify_store_init()` and
**never** `sofi_notify_store_load()`. The only caller of `load()` in the tree is the history mode.
So the daemon boots with an empty ring, and the first `Notify` runs `notify_changed()` →
`sofi_notify_store_save()`, which **truncates the persisted file to that one entry**. History is
destroyed at every daemon restart, i.e. every login — directly contradicting `notify-store.h:200`
("Persisting also means history survives a daemon restart").

Load immediately after `init()`, before the bus name is requested, so nothing can arrive first.

**Gate:** start the daemon against a populated history file, send one notification, confirm the
earlier entries are still in `~/.cache/sofi/notifications.history`.

**Risk to watch, because it becomes live for the first time.** `load()` advances `store.next_id`
past the highest stored id (`notify-store.c:515-517`). That is the correct behaviour — it stops a
restarted daemon reusing an id a sender still holds — but it has never executed in the daemon
before, so it is new code paths in an old function.

#### A2 · Per-entry verbs must reach the daemon — ~4h

Today `history_mode_result()` calls `sofi_notify_service_dismiss()`, which mutates the **standalone
process's own copy** (`notify-service.c:659-661`) that the daemon overwrites seconds later; and
`sofi_notify_service_invoke_action()`, whose `emit_signal()` returns early because
`service.connection == NULL` outside the daemon (`:212-218`), so `ActionInvoked` is dropped on the
floor. Both are silent no-ops in the panel. This is F6/R30's problem, solved for the two bulk verbs
and never for the per-entry ones.

Extend `org.sofi.Notifications` on the existing object:

| Method | Purpose |
|---|---|
| `Dismiss(u id)` | Retire one entry where the ring actually lives |
| `InvokeAction(u id, u index)` | Emit `ActionInvoked` from the process that owns the connection |
| `GetLive() → a(uus)` | Per live entry: id, action count, `desktop-entry`. Feeds A3 and A5 |

Route all three through the existing `history_mutate()` shape — daemon present → call; confirmed
absent → act locally; call merely failed → decline and change nothing. That three-way distinction is
already reasoned out in `notification-history.c:78-95` and must not be re-derived.

**Gate:** with a daemon running, Shift+Delete on a live entry retires it *and* its sender receives
`NotificationClosed`.

#### A3 · Live state is asked for, never persisted — ~2h

The root cause of the dead Enter, dead Shift+Delete and never-rendering "still on screen" stripe:
`save()` writes no `live` and no `actions` (`notify-store.c:427-439`), and `load()` forces
`live = FALSE` on every entry (`:495`). So in the standalone panel every guard of the form
`if (n->live)` is dead code.

**The fix is not to persist those fields.** Applying R39's principle here: `live` written to disk is
a lie the moment the daemon changes it, and a panel trusting it would offer to dismiss notifications
that are already gone. Instead the history mode overlays the truth from `GetLive()` — in
`history_mode_init()` after `load()`, and again on every `RELOAD_DIALOG`. `actions` need not be
persisted at all: the panel only needs the *count* to decide whether to offer the affordance, and
A2's `GetLive()` carries it.

**Gate:** a live notification renders the `@accent-strong` stripe in a standalone
`sofi -show notification-history`; a retired one renders `@muted`.

#### A4 · The Dismiss button gets an observable effect — ~1h

Mostly falls out of A3: once live-ness is real, `DismissAll` visibly clears the stripes. One
separate defect remains in the no-daemon branch — `dismiss_all_locally()` calls
`sofi_notify_store_close_all()`, which finds nothing live, leaves `any == FALSE` and therefore
**never calls `notify_changed()`** (`notify-store.c:302-316`), so nothing is saved and nothing
redraws.

**Gate:** click Dismiss with a daemon running and with none. The list visibly changes in both.

#### A5 · Raise the window that sent a notification — ~1 day. Largest unknown in Phase 11

Two halves, neither of which exists today.

**Correlation.** `handle_notify()` (`source/notify-service.c:380-403`) parses `urgency`,
`image-data` and `image-path` and silently discards every other hint — including **`desktop-entry`**,
which is the specification's application identifier and the only correlation key on offer. Store it,
persist it, return it from `GetLive()`.

**Activation.** `wlr_foreign_toplevel_handle_activate()` lives behind `wayland-window.c`'s own
registry binding and its own toplevel list (`source/modes/wayland-window.c:689-691`). The history
mode can reach neither.

Blocked on **Q20** (`TODOS.md`) — three routes, no default assumed:

| | Route | Cost |
|---|---|---|
| a | History mode binds its own `zwlr_foreign_toplevel_manager_v1` | Duplicates ~90 lines of registry and list handling |
| b | Extract a shared *activate-by-app-id* helper from `wayland-window.c` | Right answer; also a natural moment to retire the duplicated `helper_eval_add_str` recorded in backlog B6 |
| c | Shell out through `window-command` | Default is `wmctrl`, which is X11-only and already non-functional on hikari |

**Risk, stated rather than discovered later:** `desktop-entry` ↔ `app_id` is best-effort. Many
applications send neither, and some send a desktop-file basename that is not their `app_id`. Where
correlation fails the action must be **absent**, never wrong — raising the wrong window is worse
than raising none.

---

### TRACK B — the system tray

Lands in the task strip's right-hand corner (R38). The strip keeps its summon/dismiss lifetime; the
daemon holds the item set for the session (R39).

#### B1 · `box_remove_all()` — ~1h

`include/widgets/box.h` declares `box_create` and `box_add` and nothing else (F16). Tray items appear
and vanish during a session, so the zone must be rebuildable. Free each child through `widget_free`,
clear the list, `widget_update` the box. This is the **only** widget-layer change the tray needs.

**Gate:** `widget test` still passes; a box rebuilt twice leaks nothing under ASAN.

#### B2 · The watcher and the host, in the daemon — ~1 day

`source/tray-watcher.c`. Own `org.kde.StatusNotifierWatcher` on `/StatusNotifierWatcher` and
`org.kde.StatusNotifierHost-<pid>`; serve `RegisterStatusNotifierItem`,
`RegisterStatusNotifierHost`, the three properties and the four signals. `source/notify-service.c`
is the template for every part of this.

Two interop warts that are not optional, because getting either wrong makes the tray look empty:
**`RegisterStatusNotifierItem`'s argument may be a bus name *or* an object path** — when it is a
path, the sender's unique name is the bus name; and applications consult
`IsStatusNotifierHostRegistered` **at their own startup and never retry**, so the watcher must be up
before them.

**Gate:** with the daemon running, `busctl --user tree org.kde.StatusNotifierWatcher` shows the
object, and a Qt and a GTK tray application both register.

#### B3 · Item tracking — ~1 day

`source/tray-item.c`. Per item: `Properties.GetAll` with per-property fallback (some items error on
`GetAll`), subscription to `NewIcon` / `NewTitle` / `NewStatus` / `NewToolTip` / `NewAttentionIcon`,
and — **mandatory** — a `NameOwnerChanged` watch per item, because applications frequently exit
without unregistering and the tray otherwise accumulates ghosts.

Icon precedence is conditional and must be implemented as such: `Status == NeedsAttention` →
`AttentionIcon*`, else `IconName`, else `IconPixmap`, with `IconThemePath` for applications shipping
private icons that the theme-based fetcher will not find (F14).

#### B4 · `IconPixmap` decode — ~4h

R40. `a(iiay)`, ARGB32 in **network byte order**, dimensions chosen by the sender — byte-swap on
little-endian, premultiply for cairo, validate the byte count against the geometry **before reading a
pixel**, and cap dimensions. `image_from_hint()` (`source/notify-service.c:265-340`) is the model for
the discipline, not for the layout: the two formats differ.

**Gate:** a deliberately malformed pixmap is refused with a warning and the item falls back to its
name, rather than crashing the daemon.

#### B5 · `org.sofi.Tray` — ~4h

A third interface on the daemon, following R30's precedent exactly.

| Member | Purpose |
|---|---|
| `ListItems() → a(ssssay)` | id, title, status, icon name, icon bytes — bytes per R40, empty when a name resolved |
| `Activate(s id, i x, i y)` | Left click |
| `SecondaryActivate(s id, i x, i y)` | Middle click |
| `Changed` (signal) | Item added, removed or repainted. Feeds B8 |

`NO_AUTO_START` throughout, as `sofi_notify_service_call_daemon()` already does.

#### B6 · The tray zone in the view — ~1 day

F15: not a listview. `icon` widgets packed into a themed box, built at runtime — `mode-switcher`
(`source/view.c:1820-1839`) is the structural model, and `box_add`, `box_find_mouse_target` and
`icon_set_surface` are already public and already do what is needed.

**F17 is load-bearing and must not be worked around later.** A tray icon must **not** reuse
`textbox_button_trigger_action()`: it dispatches through `sofi_view_trigger_global_action()`, whose
`CUSTOM_1..19` case sets `state->quit = TRUE` (`source/view.c:1206-1207`), so every tray click would
activate the item and immediately tear the strip down. The tray needs its own handler that
dispatches and returns `HANDLED` **without** quitting — `textbox_sidebar_modes_trigger_action`
(`:1638-1664`) minus its `state->quit`.

**Gate:** clicking a tray icon activates the item and the strip is still on screen afterwards.

#### B7 · Layout — ~2h

`doc/panel-window.sasi` gains `"tray"` as the third `mainbox` child. The count that occupied this
corner was removed in the R38 amendment, so this is an addition to a two-zone bar, not a
replacement. Icon size on the 8px grid, `expand: false`, hairline separator restored now that the
zone has content again.

**Gate:** `sofi -show window -sasi-validate`-clean, and the strip's geometry is unchanged when no
tray items exist.

#### B8 · Live refresh while the strip is open — ~3h

The strip is summoned, but it stays up across minimise/maximise (`close-on-delete: false`, and both
custom verbs return `RELOAD_DIALOG`), so an item changing icon or status while it is visible must be
picked up. Subscribe to B5's `Changed`, rebuild the zone through B1, `widget_queue_redraw`.

#### B9 · Documentation — ~4h

README surface table and a tray section; `CONFIG.md` recipe for restyling the zone;
`sofi-customisation(5)` for the widget names; `sofi(1)` if any flag is added. Written last, so it
describes what shipped.

---

### What Phase 11 deliberately does not do

Stated so each is a decision and not an omission:

- **No power controls** (R37). Deferred register `TODOS.md` D1–D5.
- **No compositor changes** (R36), which is what defers lock and logout specifically.
- **No always-mapped taskbar** — overruled by USER; the strip stays summoned (R38).
- **No dbusmenu / tray context menus** (F18). v1 is `Activate` + `SecondaryActivate`; menus are
  their own later step.
- **No XEmbed tray.** X11 only, and hikari is Wayland.

---

## Phase 10 — theming and layout modernisation (DELIVERED 2026-08-25)

Scoped in `DECISIONS_LOG.md` R25–R33 (2026-08-25 10:53). Estimated **2.5–3 days**.
No new dependencies, no new theme properties, no change to the four-surface architecture.

### Target layout

| Surface | Invocation | Was | Becomes |
|---|---|---|---|
| Application menu (and every general mode) | `sofi -show drun` | west, 280 × 70% | **east**, 300 × 76%, edge documented as an override (R25) |
| Task / window manager | `sofi -show window` | south, full width | south, full width, **zoned** filter \| tasks \| count (R31) |
| Sheet switcher | `sofi -show sheets` | east, 190px column | **north centre**, horizontal chip row, 8px below the bar (R29) |
| Notification banner | `sofi -notification-daemon` | south east | south east, **unchanged position**, restyled + clear button (R28, R32) |
| Notification history | `sofi -show notification-history` | east, 460 × 80% | **south centre**, 520px vertical panel above the strip (R28) |
| Message toast | `sofi -e` | north east | north east, restyled; false bar-clearance comment corrected (F3) |

### Ordering constraint

**S1 before everything.** The palette resource is what the other five steps reference; building the
layouts first would mean editing each one twice.
**S6 last.** The documentation must describe what shipped, and the geometry table in `README.md`
is only truthful after S5's on-hardware measurement.

### S1 · Palette as a single resource (R26) — ~3h

1. `doc/palette.sasi` — sixteen positional slots, then the fourteen semantic aliases as `@colorN`
   references. Header comment states the hikari correspondence and that this is the only file in
   the tree containing a hex value.
2. `resources/resources.xml` — register as `/org/sofi/palette.sasi`.
3. `source/sofi.c` — parse it immediately before `sofi_builtin_panel_resource()`, inside the
   existing `-no-default-config` guard, with the same failure handling as the panel parse. Valid
   by F2 (lazy link resolution); a `* { }` block in user config parsed afterwards still wins.
4. Strip the inlined `* { }` colour block from all six existing layouts.
5. Regenerate `sofi-config/colors-default.sasinc` and `sofi-config/config.sasi` from the same
   source so installed and compiled-in cannot drift (F8).
6. **Gate:** `grep -rn '#[0-9A-Fa-f]\{6\}' doc/*.sasi sofi-config/` returns only `doc/palette.sasi`
   and `sofi-config/colors-default.sasinc`.
7. **Gate:** every text-on-fill pair passes WCAG AA. Computed and recorded in `BLUEPRINT.md`, not
   eyeballed — the pair this replaces was 3.2:1.

### S2 · Application menu → east side panel (R25, R27) — ~4h

`doc/default_theme.sasi`. Anchor east; 300px; the R27 grid, radius scale, two-tier type, leading
selection marker, `muted` placeholder. Add `icon-search` in the inputbar and `textbox-count` bound
to `num-filtered-rows` at the foot. Verify against `run`, `ssh`, `combi` and `filebrowser` as well
as `drun` — this layout is the fallthrough for all of them (`source/sofi.c:1078`).

### S3 · Task strip zoning (R31, R27) — ~5h

`doc/panel-window.sasi`. `mainbox` packs `[ "inputbar", "listview", "textbox-count" ]`.
`window-format` inverted to lead with the title. Icon size to the grid. Hairline separators via
`border-soft`.
**Includes F4:** `y-offset: -12px` → `+12px`, with a comment naming the sign convention on the
software-positioning path so the next reader does not re-break it.

### S4 · Sheet switcher → top-centre chip row (R29) — ~4h

`doc/panel-sheets.sasi`. `location/anchor: north`, `y-offset: 8px`, `listview { layout: horizontal }`,
`dynamic: false`, `fixed-height: true`, ten chips. Display value in `source/modes/sheets.c:362`
shortened for a chip (`"0"` / `"0·3"`) rather than `"Sheet 0    3"`. Current sheet takes
`accent-strong`, empty sheets `muted`, selection the accent fill.
**Risk:** ten fixed chips on the BARVIEW renderer size to content width, so an occupied/empty pair
differs in width and the row is not a fixed grid. Verify positions are stable across invocations;
if they are not, pad the display value to a constant width rather than switching renderer.

### S5 · Notifications: reshape and clean up (R28, R30, R32) — ~1 day

**5a — store and service (C).**
- `include/notify-store.h` / `source/notify-store.c`: `sofi_notify_store_clear_history()` — retire
  live entries, free the ring, delete the persisted file. Cancels every outstanding expiry timer
  first; the teardown path in `sofi_notify_store_fini()` is the model.
- `include/notify-service.h` / `source/notify-service.c`: second interface
  `org.sofi.Notifications` on the existing `/org/freedesktop/Notifications` object, methods
  `DismissAll` and `ClearHistory`. No second bus name.
- `source/modes/notification-history.c`: `kb-custom-1` → dismiss all, `kb-custom-2` → clear
  history; direct store call under `sofi_view_is_daemon()`, D-Bus otherwise, file-only fallback
  when nobody owns the name (F6).
- `source/sofi.c`: `-notification-clear` / `-notification-clear-history` one-shot flags.

**5b — banner (`doc/panel-notifications.sasi`).** Cards on a transparent ground with gaps; 4px
leading urgency stripe; header strip with `textbox-count` and `button-clear-all`
(`action: "kb-custom-1"`), which is what finally makes the existing dismiss-all reachable (F5).
Selection stays invisible. `click-to-exit: false` and keyboard `none` are untouched — R24 forces
both in code and this step must not appear to change them.

**5c — history (`doc/panel-notification-history.sasi`).** `south`, centred, 520px wide, `y-offset`
clearing the task strip. Header strip with both buttons. Selection is a `surface` fill with an
`accent` stripe, deliberately not the menus' solid fill (R32). Themed scrollbar.

**5d — on-hardware verification.** Four surfaces up at once (the RR16 test): daemon banner, task
strip, sheets row, history panel. Measure and record the real geometry of each in `BLUEPRINT.md`
before the README's geometry table is written.

### S6 · Documentation (R33) — ~1 day

- `README.md`: geometry table corrected to what shipped; new **Theming** section — the sixteen
  slots, the semantic table, the three-line recolour example, the panel-move example, and the
  precedence rule (default config → palette → panel layout → user config → `-theme`).
- `CONFIG.md`: restructured task-first — recolour everything / move a panel / resize a panel /
  restyle one surface only / bind the clear actions in `hikari.conf`.
- `doc/sofi-customisation.5.markdown`: new. The long form, plus a worked "port a terminal
  colorscheme into sofi and hikari at once" walkthrough, which is the whole point of the sixteen
  positional slots. Added to `doc/meson.build` and the README manpage list.
- `doc/sofi-theme.5.markdown`: palette-override section; correct "Default theme loading", which
  never mentions the built-in panel layouts.
- `doc/sofi.1.markdown`: the two new flags.

### Verification, every step

`ninja -C build` clean at `warning_level=3`; **19/19 tests**; `sofi -dump-theme` parses back
without warnings; all manpages regenerate through pandoc. Nothing is reported working that has not
been run.


## Phase 8 — The four native surfaces (DELIVERED 2026-08-24, unreleased)

Built, tested and verified on hardware this session. 19/19 tests green in both the before and
after state. **Not yet committed in either repository.**

### 8a. Compiled-in panel layouts (R16)

| Item | Detail |
|---|---|
| `doc/panel-window.sasi` | Bottom strip. `layout: horizontal` selects the listview BARVIEW renderer — content-width elements that scroll on selection, which is a task strip rather than a grid. `flow: horizontal` would have given equal-width cells and is the wrong tool |
| `doc/panel-sheets.sasi` | Right pane. `lines: 10`, `dynamic: false`, `fixed-height: true` so the ten fixed sheets do not move under the user's fingers between invocations |
| `doc/panel-notify.sasi` | Corner toast. Carries `click-to-exit: false` and `wayland-keyboard-interactivity: "on-demand"` |
| `doc/default_theme.sasi` | Unchanged — remains the left sidebar, now used as the fallback for every other mode |
| `source/sofi.c` | `sofi_surface_name()` and `sofi_builtin_panel_resource()`; `@theme "default"` removed from `default_configuration.sasi` so C makes the choice |

**Verified:** with zero configuration, `-show drun` → 280×816 left sidebar, `-show window` →
1920×52 bottom strip, `-show sheets` → 190×472 right pane, `-e` → 380×55 corner toast.

### 8b. Per-surface instance locks (R17)

`sofi-<surface>.pid` in `$XDG_RUNTIME_DIR`. **Verified both ways**: with a shared pidfile the
second panel dies with *"Failed to set lock on pidfile"* and renders nothing; with per-surface
locks the launcher and task strip render simultaneously with no flags.

### 8c. Window mode as a task manager (Phase 7c, R15)

- Minimised bit surfaced as `URGENT`, so windows on another sheet render muted instead of the
  strip claiming they are in front of you. Sheet 0 always reports unminimised — that is the
  semantics, not a bug.
- `kb-custom-1` toggles minimise, `kb-custom-2` toggles maximise. Toggle rather than set, because
  a binding is one key and the state is already tracked. Custom bindings 3+ keep their old
  meaning of exit-with-10+N, so existing scripts are unaffected.
- Maximise only, no fullscreen: hikari maps `set_fullscreen` onto full-maximize, so exposing both
  would be two bindings performing one operation while echoing back an unrequested state.

### 8d. Layer-shell v4 and keyboard interactivity (R18)

Bind version 1 → 4; `-wayland-keyboard-interactivity` added with a runtime version guard that
warns and falls back rather than silently doing the opposite of what was asked.

### 8e. Sheet switcher, both halves (R19, Q17)

**Compositor side** — `hikari-sakura/src/ipc.c`, `include/hikari/ipc.h`, wired into
`hikari_server_stop()` and the Makefile. A request/response text socket at
`$XDG_RUNTIME_DIR/hikari.sock`, mode 0600, served from the compositor's own `wl_event_loop`.
Bounded: 512-byte requests, 8 concurrent clients, one exchange per connection so the server holds
no per-client state machine.

```
-> state      <- sheet 3 / output eDP-1 / counts 2 0 1 0 0 0 0 0 4 0 / END
-> sheet 7    <- ok
-> pin 7      <- ok        # send-to-sheet: closes Q17
```

**sofi side** — `source/modes/sheets.c`, `include/modes/sheets.h`, `SHEETS_MODE` build option.
Occupied sheets show counts, empty ones dim, the displayed one takes the accent.

**Verified against a nested hikari**: one window on sheet 5 → `counts` index 5 = 1; `pin 8` moved
it to index 8; `sheet 8` switched to it. Malformed input, unknown commands and no-focused-view all
return errors rather than misbehaving. Opening the pane does **not** mutate compositor state
(A/B tested).

### 8f. Notification surface groundwork

`sofi_view_error_dialog()` now arms `sofi_view_set_user_timeout()`, so `-e` self-dismisses.
Verified: delay 1 → 1073ms floor; delay 2 with `-no-click-to-exit` → 2096–2466ms across four runs.

---

## Phase 9 — Notification daemon (PLANNED, NOT STARTED)

sofi owns `org.freedesktop.Notifications`. Full analysis, diagrams and per-phase verification:
the published plan artifact. **~950 lines of new C. No new dependencies** — `gio-unix-2.0` is
already in `deps` and five modes already include `gio/gio.h`.

### Why this is smaller than it looks

| Fear | Reality |
|---|---|
| sofi is one-shot; a daemon is persistent | **One conditional.** `view.c:1536` is the only place the loop ends when the last view closes |
| The surface cannot be torn down and rebuilt | Already proven in-tree — `wayland_layer_shell_surface_closed()` at `display.c:1804` does exactly that cycle reactively |
| The daemon will cover the screen and die on the first click | Already false. Captured layer-shell traffic: notify surface asks for `set_size(380, 55)` and `set_keyboard_interactivity(2)`; the menus ask for `set_size(1920, 1166)` and `(1)` |
| Notifications could paint over the lock screen | `lock_mode.c:988-992` disables the overlay layer on lock |

### Sub-phases

| # | Deliverable | Verification | Est. |
|---|---|---|---|
| **N1** | GDBus service; own the name; honest `GetCapabilities`/`GetServerInformation`; log `Notify` | `notify-send` returns; `dbus-send` answers `GetServerInformation` | ~250 lines |
| **N2** | Daemon lifetime: gate `view.c:1536`; unmap/remap cycle; **force R24's two settings** | Daemon survives an empty queue for minutes; a menu still opens and takes the keyboard; clicks land on ordinary windows | ~80 lines |
| **N3** | Ring buffer (R21) + notifications mode + `doc/panel-notifications.sasi` bottom-right (R22) | Three `notify-send` calls → three rows in one surface; `--replace-id` updates in place | ~300 lines |
| **N4** | Expiry, `CloseNotification`, `NotificationClosed` with correct reason codes; `urgency=2` never expires (R23) | `--expire-time=0` persists; `dbus-monitor` shows reason 1/2/3 on the three paths | ~120 lines |
| **N5** | Actions; `ActionInvoked`; add `"actions"` to capabilities only once it works | `notify-send --action=go=Open`, Enter, confirm `ActionInvoked` carries `go` | ~100 lines |
| **N6** | Icons and markup: `app_icon`, `image-path`, validated `image-data`, parse-then-escape body | Inline image renders; malformed `image-data` refused without crashing; unclosed tag renders literally | ~150 lines |
| **N7** | `notification-history` mode over the same ring | Five notifications expire, history shows all five | ~150 lines |
| **N8** | User-level D-Bus service file, hikari autostart, manpage, documented rollback | Log out and in; a real application's notification reaches sofi | config + docs |

**Ordering constraint:** N2 must precede N3. It is the phase that retires the only unproven
assumption in the plan — that sofi can idle with no surface and bring one back.

### Design decisions carried from `DECISIONS_LOG.md`

- One surface holding a list, not one surface per notification. Forced by the single-surface
  backend (`wayland_stuff` holds one `wl_surface`; the view stack is LIFO with only
  `current_active_menu` rendered) and correct anyway — a listview gives stacking, icons, per-row
  states and the whole theming vocabulary for free.
- The queue is a Mode, reloading via `sofi_view_reload()` — the same pattern
  `modes/wayland-window.c` already uses for asynchronous compositor updates.
- `-notification-daemon`, not `-show notifications`: the flag changes process lifetime and
  `-show` does not imply that anywhere else. Own surface name `notifyd`, so it does not collide
  with the one-shot `-e` toast, which stays exactly as it is for scripts.

---

Every phase ends with the tree **building and passing tests**, plus a **grep invariant** that
can be checked mechanically. Phases are ordered so that no phase depends on a later one.

---

## Phase 0 — Establish a baseline (blocking, ~15 min)

Nothing can be verified until the project builds.

| Step | Detail |
|---|---|
| Install `devel/bison` | `meson.build:224` needs GNU Bison; `lexer/theme-parser.y:28-33` uses `%define api.pure`, `%glr-parser`, `%skeleton "glr.c"` which byacc cannot provide |
| Install `devel/check` | otherwise `meson.build:148` silently disables the entire test suite |
| `meson setup build && ninja -C build && ninja -C build test` | record the true starting state |

**Expected complication.** `test/helper-test.c:185` calls `utf8_strncmp("aapno", "a", 4)`,
which the audit confirms writes 3 bytes past a 2-byte heap allocation
(`source/helper.c:1357`). A clean baseline may not be reachable until Phase 1 lands.

**Exit criterion.** A recorded, reproducible build + test result — green or not.

---

## Phase 1 — Fix what the rebrand would otherwise cement (~1–2 days)

These are fixed **first**, before renaming, because they live in installed headers or in
code the rename will touch wholesale. Fixing them afterwards means editing the same lines
twice and reviewing a rename diff that also contains logic changes.

| # | Site | Defect |
|---|---|---|
| 1 | `source/helper.c:1357` | `utf8_strncmp` writes OOB when a string is shorter than `n`; callers `source/modes/combi.c:162,320` compute the guard on the un-normalized string |
| 2 | `include/widgets/textbox.h:63` | `short cursor` overflows past 32767 chars, feeding a negative offset to `g_utf8_offset_to_pointer` — this field is in a header |
| 3 | `source/rofi.c:847` | `g_warning(str, NULL)` — user-controlled text used as a printf format string. Regression introduced in this tree by commit `30885f2b` |
| 4 | `include/rofi-types.h:233` | `WindowLocation` is a bitmask enum but `config.location` is used as a 0–8 index (`source/wayland/view.c:151`, `source/xcb/view.c:281`, written as a raw index at `source/modes/dmenu.c:602`). This type is in an installed public header |
| 5 | `include/mode.h:36` | Bump `ABI_VERSION` from 7 so stale plugins fail loudly at `source/rofi.c:648` |

Also enable `-Wformat=2` / `-Wformat-security` in `meson.build` so defect 3 cannot regress.

**Verification.** Build green; `ninja test` green; ASAN build clean over the test suite.
**Grep invariant.** `git grep -n 'g_warning([^"]' -- source/` returns nothing.

---

## Phase 2 — Wayland / layer-shell correctness (~2 days)

The Wayland backend carries the highest concentration of real defects and is the backend the
project most needs to be correct. Independent of the rename.

**Startup path — makes sofi usable on non-wlr compositors.**
- `source/wayland/display.c:1490,1505,1752,1756` — four `g_error()` calls abort the process
  with a core dump. `source/rofi.c:1264` already stores the return value and
  `source/rofi.c:1280-1298` already prints a friendly fallback message that can never run.
  Replace with `g_warning`/`g_critical` + the existing `return FALSE`. In the registry
  handler, skip the unsupported global rather than aborting the whole client.
  This is why sofi cannot run on GNOME/Mutter today.
- `source/wayland/display.c:1791` — `wayland_display_late_setup()` dereferences
  `layer_shell`/`compositor` without NULL checks and unconditionally returns TRUE.

**Buffer pool.**
- `source/wayland/display.c:208` — fixed shm name `"/rofi-wayland-surface"` with
  `O_CREAT|O_EXCL`: two concurrent instances collide, and a SIGKILL between the `shm_open`
  and the `shm_unlink` poisons the name permanently. Use `SHM_ANON` on FreeBSD /
  `memfd_create` on Linux, or a PID+counter name.
- `source/wayland/display.c:319` — `display_buffer_pool_get_next_buffer()` dereferences a
  NULL pool; its only caller (`source/wayland/view.c:428-432`) passes the unchecked result of
  `display_buffer_pool_new()`, which has five NULL returns. This is the crash the poisoned
  shm name causes.
- Unchecked `stride * height * buffer_count` overflow before the int32 offset handed to
  `wl_shm_pool_create_buffer`.

**Input and lifecycle.**
- `source/wayland/display.c:439` — key-repeat `GSource` pointer left dangling on early
  returns, then `g_source_destroy()`d → use-after-free. Store the `guint` id instead.
- `source/wayland/display.c:472` — `repeat_info` `rate == 0` means *repeat disabled*; the
  code repeats at ~33 Hz anyway.
- `source/wayland/display.c:380` — keymap fd, mapping and `xkb_keymap` leaked per event.
- `source/wayland/display.c:1223` — `wl_seat.capabilities` tests the POINTER bit to decide
  whether to release the KEYBOARD.

**Sizing and positioning.**
- `source/wayland/view.c:396` — `calculate_window_width` reads the layer surface size as if
  it were the screen size, so with `-no-click-to-exit` each successive view halves in width.
- `source/wayland/view.c:158` — `-x-offset`/`-y-offset` are silently ignored on Wayland; the
  xcb path seeds the same lookup from `config.x_offset` (`source/xcb/view.c:358-361`).

**Window mode.**
- `source/modes/wayland-window.c:622,635` — selected toplevel dereferenced without a NULL
  check; the list shrinks synchronously at `:354` while the UI reload is coalesced into a
  66 ms timer (`source/wayland/view.c:305-308`), so Enter inside that window crashes.
- `source/modes/wayland-window.c:480,508,520` — no-wlr compositors get an empty list rather
  than a diagnostic; manager stopped after the list is freed; `stop()` called on an
  already-destroyed proxy.

**Verification.** Run under sway (wlr). Weston with `--use-pixman` for the buffer path.

---

## Phase 2b — xdg-shell fallback (~2–3 days)

Per **R11**. The largest single functional gain available: takes sofi from wlroots-only
(sway, Hyprland, river) to broadly usable on Mutter (GNOME) and KWin (Plasma).

Sequenced *after* Phase 2a, because 2a's `g_error` → `g_warning` change is what makes the
unsupported-compositor case reachable instead of an abort.

**Current state.** `xdg-shell.xml` is already compiled (`meson.build:318`) but entirely
unused — `grep xdg source/wayland/` returns nothing. `zwlr_layer_shell_v1` is bound at
`source/wayland/display.c:1471-1474` and treated as mandatory at `:1755`.

**Work.**
- Bind `xdg_wm_base` in the registry handler alongside `zwlr_layer_shell_v1`; make
  layer-shell preferred, xdg-shell the fallback, and fail only when neither is present.
- Introduce a small surface abstraction so `wayland_display_late_setup()`
  (`source/wayland/display.c:1768-1824`) and `display_set_surface_dimensions()` (`:1846-1904`)
  dispatch to either backend rather than calling `zwlr_layer_surface_v1_*` directly. There
  are 14 direct `zwlr_layer_surface_v1_*` call sites to route.
- Implement `xdg_surface.configure` / `ack_configure` and the `xdg_wm_base.ping`/`pong`
  handshake — omitting pong makes compositors kill the client as unresponsive.
- Positioning: xdg-toplevel cannot self-position. Anchors and `x-offset`/`y-offset` degrade
  to compositor placement. Document this rather than faking it.
- `set_keyboard_interactivity` has no xdg equivalent; focus follows normal window rules.
- `click_to_exit` needs a different mechanism (no screen-sized capture surface available).

**Verification.** sway (layer-shell path unchanged) · GNOME/Mutter and KWin (fallback path)
· confirm the layer-shell path is still chosen when both are advertised.

**Documented limitation.** README's "Missing features in Wayland mode" section gains an
xdg-shell subsection: no precise positioning, no keyboard-interactivity override, degraded
click-to-exit.

---

## Phase 3 — The rename, in dependency order (~2 days)

Governed by the 19:02 and 19:15 rulings: **hard fork, no shims, no fallbacks.** No compat
header, no `rofi.pc` alias, no dual env-var exports, no path probing, no script symlinks.
Each sub-phase is independently buildable. **All gates closed — unblocked.**

### 3a. Build identity
`meson.build:1` `project('rofi')` → `sofi`. This one line cascades to `plugindir` (`:43`),
`themedir` (`:44`), `PACKAGE_NAME` (`:152`), `GETTEXT_PACKAGE` (`:155`), installed-header
subdir (`:202`) and doxygen `PROJECT_NAME`.

Three things do **not** follow and must be edited by hand:
`executable('rofi')` (`:367`), `pkg.generate(filebase:'rofi', name:'rofi')` (`:420-423`), and
every literal `rofi*` filename in the install lists.

Also: `PACKAGE_BUGREPORT`/`PACKAGE_URL` (`:156-157`) still point at `davatorium/rofi`.

**Invariant.** `ninja` produces `sofi`; `pkg-config --modversion sofi` resolves.

### 3b. File renames (`git mv`, no content edits)
30 live files. `source/rofi.c`, `include/rofi.h`, `include/rofi-types.{c,h}`,
`include/rofi-icon-fetcher.{c,h}`, `pkgconfig/rofi.pc.in`, `doc/rofi*.markdown` (10),
`doc/rofi.doxy.in`, `data/rofi*`, `script/rofi-*`, `Examples/rofi-file-browser.sh`.

Remember `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted
relative path.

**Invariant.** `git ls-files | grep -i rofi` returns only intentionally-frozen paths.

### 3c. C identifiers (2,284 lowercase + 516 `ROFI` + 468 `Rofi`, 335 distinct)
Per **R1**: rename all of them to `sofi_*` / `SOFI_*` / `Sofi*`. No compat shim.
Bump `ABI_VERSION` at `include/mode.h:36` (already scheduled in Phase 1).

**Must exclude the per-file MIT copyright headers.** A naive tree-wide substitution rewrites
the license notices, which is a license violation, not a cosmetic slip. Do this as a scripted
rename over a reviewed identifier list that skips comment blocks containing a copyright line
— not a blind `sed`.

**Invariant.** `git grep -lE '\b(rofi_|ROFI_|Rofi)' -- source/ include/` returns nothing.
**Second invariant.** `git diff COPYING AUTHORS` is empty, and
`git grep -c 'Copyright' -- source/ include/` is unchanged from the pre-rename count.

### 3d. On-disk paths, env vars, compositor identity
Per **R2** and **R4**: hard break, no fallback lookups, no dual-name env exports.

- Config/theme/script paths → `sofi/` only (`source/rofi.c:1059,1120,1132`;
  `source/helper.c:1474,1483,1493,1509`; `source/modes/script.c:630,640`)
- Cache files → sofi-named (`source/modes/drun.c:66,69`, `run.c:64`, `ssh.c:83`, filebrowser)
- Env vars → `SOFI_*` only (`source/modes/script.c:216-231`; `source/rofi.c:705,991`;
  `source/view.c:138`)
- Layer-shell namespace and WM_CLASS → pending Q5

**Invariant.** `git grep -n '"rofi' -- source/ config/` returns nothing.

**Required companion work (R2/R4 consequence).** Both breaks are *silent* — no error is
produced, things simply stop being found. Before release:
- README and release notes must state plainly that sofi does not read rofi configuration
- `doc/sofi-script.5.markdown` must lead with the `ROFI_*` → `SOFI_*` rename
- Consider a one-shot startup `g_message` when `~/.config/rofi/` exists but
  `~/.config/sofi/` does not — a pointer, not a fallback. Cheap, and converts the most
  likely support burden into a self-answering message.

### 3e. Theme extension `.rasi`/`.rasinc` → `.sasi`/`.sasinc`
Per **R3**.
- `lexer/theme-lexer.l:56` — the extension array
- 35 theme files renamed (`themes/*.rasi` + `themes/gruvbox-common.rasinc`) and the
  `meson.build:378-415` install list
- `doc/default_theme.rasi`, `doc/default_configuration.rasi`
- `config.rasi` → `config.sasi` (`source/rofi.c:1059`, `:1143-1145`);
  `rofi.rasi` → `sofi.sasi` (`source/rofi.c:1120`, `:1132`)

**No importer edits needed.** The six gruvbox variants use extension-less
`@import "gruvbox-common"` (`themes/gruvbox-*.rasi:61`), which resolves via the extension
array — renaming the include file alone is sufficient. Verified 2026-08-22 19:15.

*Invariant:* `git ls-files | grep -cE '\.rasi(nc)?$'` → 0.

### 3f. Compositor identity, scripts, pkg-config
Per **R5**, **R6**, **R9**.
- Layer-shell namespace `source/wayland/display.c:1792`; WM_CLASS `source/xcb/view.c:773-775`
- `script/sofi-sensible-terminal`, `script/sofi-theme-selector` — **and `config/config.c:65`
  in the same commit**, or `sofi -show run` cannot open a terminal
- `sofi.pc` / `pluginsdir=${libdir}/sofi` (`meson.build:417-428`, `pkgconfig/rofi.pc.in`)

### 3g. Docs, packaging, data
Manpages renamed and their cross-references updated; `data/*.desktop` `Name`/`Exec`/`Icon`/
`StartupWMClass` (must match the R5 WM_CLASS); `data/rofi.svg` → `sofi.svg` (basename must
match `Icon=`); `doc/rofi-thumbnails.5.markdown:62,71` AppArmor path `/usr/bin/rofi` →
`/usr/bin/sofi`; README/INSTALL/CONFIG rewritten; all upstream URLs repointed.

**Attribution work required by R12.** With `AUTHORS` deleted, `COPYING` plus `README.md`
carry the entire MIT provenance. README must gain an explicit fork-provenance statement
crediting Dave Davenport (Qball), Sean Pringle (simpleswitcher) and lbonn (Wayland).
`COPYING` is verified present and unmodified, already carrying both required notices.

---

## Phase 4 — FreeBSD compatibility, proven not assumed (~half day)

**Actually broken today:** GNU Bison not detected/documented (Phase 0);
`shm_open` with a fixed name (Phase 2) — `SHM_ANON` is the native FreeBSD idiom;
`meson.build` never links `librt`, which older glibc needs for `shm_open` (harmless on
FreeBSD, breaks older Linux);
`INSTALL.md:259-262` offers only `pkg install rofi` with no from-source dependency list;
`INSTALL.md:26` documents an autotools `--disable-check` flag meson does not have.

**Merely untested:** the C source is genuinely portable — zero hits for `strcasestr`,
`asprintf`, `qsort_r`, `memmem`, `pipe2`, `execvpe`, `program_invocation_name`,
`canonicalize_file_name`, `__GLIBC__`, `/proc/`. `sysexits.h`, `glob.h`, `sys/file.h`,
`pwd.h` all exist on BSD. `_GNU_SOURCE` at `meson.build:159` is unconditional but is inert
on FreeBSD. There are no `__FreeBSD__` conditionals anywhere — and, encouragingly, none are
needed.

**CI:** `.build.yml` clones `https://sr.ht/~qball/rofi/` (`:32`) and builds *upstream rofi*,
then declares an artifact for version `1.7.8-dev` (`:47`) while `meson.build:2` says
`2.0.0-dev`. `.gitlab-ci.yml` is pure autotools (`autoreconf -i` at `:26`) and cannot run at
all — the project has no `configure.ac`. Both need rewriting or deleting; add a real FreeBSD
job.

---

## Phase 5 — Remaining correctness backlog (~3–5 days)

The 59 medium findings, grouped. Full detail in `AUDIT_REGISTER.md`.

- **Threading / lifetime** — icon fetcher publishes surfaces from worker threads with no
  synchronisation (`source/rofi-icon-fetcher.c:746`), `destroy()` frees state in-flight
  workers still use (`:325`), and a worker calls into the UI layer (`:562`).
- **Unchecked replies (xcb)** — `source/xcb/display.c:466,572,1466`,
  `source/modes/window.c:640`, `source/xcb/view.c:593`.
- **Modes** — dmenu writes one past a fixed array (`source/modes/dmenu.c:157`) and leaves
  `permanent` uninitialized (`:169`); drun trusts the cache to NUL-terminate (`:948`) and
  follows symlinked dirs unbounded (`:839`); ssh follows `Include` with no cycle detection
  (`source/modes/ssh.c:394`); combi misdispatches `!` prefixes (`:158`) and indexes
  `switchers[0]` unchecked (`:195`).
- **Pidfile** — `create_pid_file()` SIGTERMs its own process group and then spins forever on
  a corrupt pidfile (`source/helper.c:615`); unchecked `write()` becomes an infinite loop
  (`:638`); `-replace` busy-waits forever if the target ignores SIGTERM (`:617`).
- **History durability** — rewritten by truncating the live file in place, so a crash
  mid-write destroys it (`source/history.c:244`).
- **Escaping** — `source/modes/window.c:901` appends window titles unescaped into a
  Pango-markup row; the Wayland twin escapes correctly. A title containing `&` renders
  blank on X11.
- **Structure** — `source/view.c:364` makes raw XCB calls from backend-agnostic code;
  `helper_eval_add_str` is copy-pasted between the two window modes and has already
  diverged; `window`/`windowcd` share one global cache either can free
  (`source/modes/window.c:870`).

---

## Phase 6 — Ship `sofi-config/` as the deployed default (~half day) — **SUPERSEDED by R16**

> Superseded 2026-08-24. The four layouts are compiled into the binary and selected by mode, so
> `~/.config/sofi/` is optional rather than required and nothing needs deploying. `sofi-config/`
> remains in the tree as a worked example of a user override. Retained below for history.

Added by the USER 2026-08-22 as the standard config and theme. **Validated against the real
parser**: `rofi -config sofi-config/config.rasi -dump-theme` exits 0 with *zero* warnings on
stderr, the `@import` resolves, and every property lands
(`location: west`, `anchor: west`, `width: 280px`, `height: 70%`, `x-offset: 15px`,
`border-radius: 12px`, `accent: #916778`).

| File | Role |
|---|---|
| `sofi-config/config.rasi` | 112 lines. `configuration {}` block + full widget theme. Sidebar layout, west-anchored, 280px, unified black 75% background, dusty-mauve accent |
| `sofi-config/colors-default.rasi` | 6 colour variables, imported by the above |

**Work:**
1. Decide the canonical location. `sofi-config/` is not currently installed by meson and is
   not on any search path. Options: replace `doc/default_configuration.rasi` +
   `doc/default_theme.rasi` (compiled into the binary via
   `resources/resources.xml` → `gnome.compile_resources`), or install to
   `$datadir/sofi/themes/` and ship as a named theme, or both.
2. Wire into `meson.build` `install_data`.
3. Reconcile with the built-in default theme so `@theme "default"`
   (`source/rofi.c:1179`) resolves to this.

**Two things that must not be missed:**

- **`@import "colors-default.rasi"` names the extension explicitly.** Under **R3** these
  files become `.sasi`/`.sasinc`, and unlike the shipped gruvbox themes — whose
  `@import "gruvbox-common"` is extension-less and resolves through the extension array —
  **this import line must be edited by hand** or the theme breaks. Same for the file pair
  itself. Add to the Phase 3e checklist.
- `modi:` is used in the `configuration {}` block. It is still a live alias for `modes`
  (`source/xrmoptions.c:74-86`, alongside the older `switchers`), so it works — but the
  shipped default should use the current spelling `modes:` rather than the deprecated one.

**Verification.** `sofi -config <path> -dump-theme` exits 0 with empty stderr; a visual check
under sway once a session is available.

---

## Phase 7 — New modes, rescoped for hikari-sakura (2026-08-22 21:35) — **DELIVERED, partly rerouted**

> Status 2026-08-24. **7a** confirmed working with no sofi changes. **7c** delivered in Phase 8c.
> **7b is superseded by R19** — the workspace switcher is a control socket plus a `sheets` mode,
> not an `ext_workspace_v1` client. **Q17 is closed**: the socket's `pin <n>` expresses
> send-to-sheet, which the scoping below correctly identified as impossible over any
> standards-track protocol. Retained for history.

**Target platform is now named:** hikari-sakura, the USER's custom wlroots compositor at
`/home/orpheus497/Projects/hikari-sakura`. Scoping below is against *that* compositor's
actual protocol set, not a generic one.

### Compositor dependency status

| Compositor work | State |
|---|---|
| `ext-foreign-toplevel-list-v1` (listing) | **Delivered**, Phase 88, hardware-confirmed with waybar |
| `zwlr_foreign_toplevel_management_v1` (control) — "Part A" | **Implemented, compiles clean, NOT YET RUN.** hikari Phase 89 |
| `ext_workspace_v1` — "Part B" | **Not started** |

### 7a. Window switcher — needs NO sofi work

sofi already sends `activate` (`source/modes/wayland-window.c:195`) and `close` (`:199`),
both v1 requests, and binds `MIN(version, WLR_FOREIGN_TOPLEVEL_VERSION=3)` so it degrades
gracefully against a lower-version compositor. **When hikari Phase 89 is built and run,
window mode works with zero changes here.**

Two things to watch when it is first run:
- sofi passes `pd->wayland->last_seat` to `activate` — whichever seat last produced input.
  hikari's handler must not route that through `hikari_workspace_focus_view()`; its
  Phase 89 implementation correctly uses the mark path instead.
- The Phase 2a diagnostic (`_init` returning FALSE with a clear message) is what currently
  reports the missing protocol. Once Part A lands that path should stop firing — if it still
  fires, the global is not being advertised.

### 7b. Workspace switcher — the largest sofi-side job

Does not exist; a new mode written from nothing, ~300 lines, on the pattern of the existing
window-mode backend split. Blocked on compositor Part B.

**Naming trap recorded from the compositor brief:** a `hikari_workspace` is a *per-output
viewport*, not the protocol's workspace. What a user switches between is a **sheet** (10 per
workspace, 0–9). So the protocol mapping is one group per output, ten workspace handles per
group, and `ACTIVATE` is the only capability — sheets are fixed in number and permanently
bound to their output, so `create`/`remove`/`assign` must not be advertised. Sheet 0 is
semantically odd (its views stay visible beneath whichever sheet is displayed) and will not
map cleanly onto the protocol's model.

### 7c. Task manager (R15) — real sofi work, and one part is impossible

sofi sends **only** `activate` and `close`. `set_minimized`, `set_maximized` and
`set_fullscreen` appear nowhere in `source/modes/wayland-window.c` — they must be added as
UI actions.

**Expose maximise only, not fullscreen.** hikari has no fullscreen state, only
`HIKARI_MAXIMIZATION_*`; its Phase 89 maps `set_fullscreen` onto full-maximize, matching the
precedent at `xdg_view.c:656`. So the two requests are the same operation and a client sees
back a state it did not ask for. Two buttons doing one thing is worse than one.

**Send-to-workspace cannot be built.** Verified against the protocol XML: zwlr
foreign-toplevel contains zero occurrences of "workspace", and `ext-workspace-v1`'s `assign`
moves a *workspace to an output group*, not a *window to a workspace*. No standards-track
protocol expresses it. Blocked on **Q17** — hikari-specific protocol, or a CLI escape hatch.

### Recommended sequencing

1. `sudo make clean` in hikari-sakura, rebuild as the user, restart the compositor
   → **7a works immediately, no sofi changes.**
2. Rule Q16 (delete the ext binding / `window-command`?) and Q17 (send-to-workspace route).
3. 7c minimise + maximise actions in sofi — small, once Part A is proven on hardware.
4. Compositor Part B, then 7b — the big one.

---

## Risk register

| # | Risk | Likelihood | Detection / mitigation |
|---|---|---|---|
| RR1 | User's config, themes and script dirs silently orphaned | **Certain** — this is R2, accepted deliberately | Fresh-VM test with only `~/.config/rofi/` populated. Mitigate with README wording + the one-shot startup hint in Phase 3d |
| RR2 | Third-party script modes break silently on `SOFI_*` | **Certain** — this is R4, accepted deliberately | Run a known script mode (e.g. rofi-rbw) against the built binary and confirm it fails *visibly*, not subtly |
| RR3 | Installed rofi plugins orphaned by the `$libdir` move | **Certain** — inherent to R1 | `ABI_VERSION` bump converts a silent no-load into a loud version mismatch at `source/rofi.c:648` |
| RR4 | **MIT copyright headers rewritten by the bulk rename** | **High** — raised from Medium by R12 | With `AUTHORS` deleted, `COPYING` + the 78 per-file notices across 73 files are the *entire* attribution mechanism. Phase 3c invariant 2: `git diff COPYING` empty and the `Copyright` count unchanged at 78. Put it in CI |
| RR9 | Every third-party rofi theme needs a file rename (R3) | **Certain**, accepted | Loud failure (file not found), not silent. Release notes + a note in the theme manpage |
| RR10 | xdg-shell fallback silently degrades positioning (R11) | High on GNOME/KWin | xdg-toplevel cannot self-position. Document in README's Wayland limitations rather than faking it — do not let anchors appear to work when they don't |
| RR5 | CI reports green while building upstream rofi | **Certain today** | Fix `.build.yml:32` before trusting any CI result at all |
| RR6 | Rename diff hides logic changes from review | Medium | Phases 3a–3e stay mechanical; every logic change lands in Phase 1/2/5. Review 3c as a scripted transform + its invariants, not line by line |
| RR7 | No green baseline exists, so regressions are undetectable | **Certain today** | Phase 0 is blocking for exactly this reason |
| RR8 | Zero test coverage of either display backend | High, ongoing | Nothing in Phase 0–5 detects a Wayland regression. Backlog item B6 |
| RR11 | **`click-to-exit` left on in daemon mode** | Certain if not forced | An invisible full-screen input trap over the desktop for the whole session — nothing is clickable. R24 forces it in code after theme parsing. Explicit N2 test |
| RR12 | **Exclusive keyboard grab in daemon mode** | Certain if not forced | The daemon owns every keystroke for the session. Same mitigation; needs layer-shell v4, already bumped in 8d |
| RR13 | Notification daemon crashes silently | Medium | Notifications vanish with no symptom, which is worse than an error. Ship the D-Bus activation file so the next `Notify` restarts it |
| RR14 | Malformed `image-data` hint | Medium | Out-of-bounds read on a buffer whose geometry the sender controls. Validate `rowstride × height` against array length, cap dimensions, reject rather than clamp |
| RR15 | A stuck critical notification (R23) | Medium | Never expires by design, so any input-routing mistake becomes permanent rather than transient. Dismissal must always be reachable, never only via an action |
| RR16 | Four concurrent surfaces untested | Medium | Launcher, strip, sheet pane and a live notification have never been up together; two have. Test before N3 lands |
| RR17 | **Two agents in `hikari-sakura` simultaneously** | Observed 2026-08-24 | Another session edited `.devdocs/` there at 09:11–09:15 while `src/ipc.c` was being written. Nothing is committed in either repository. Commit before further compositor work |

**Note on RR1–RR3.** These are not defects in the plan; they are the accepted cost of the
hard-fork ruling. They are recorded here so the release notes cover them and so nobody later
mistakes them for bugs.
