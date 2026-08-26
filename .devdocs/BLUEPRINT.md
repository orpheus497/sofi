# BLUEPRINT

**Last updated:** 2026-08-26 22:30

System architecture, requirements, and how dependencies operate.

---

## Implementation registry — the system surfaces

Per `AGENTS.MD`, completed `TODOS.md` items land here. Delivered 2026-08-24, uncommitted.
Decisions: `DECISIONS_LOG.md` R16–R24. Plan: `PLANS.md` Phase 8.

sofi is hikari-sakura's shell, not a rofi drop-in that runs on it. Four surfaces, each with its
own compiled-in layout and its own instance lock, **requiring no configuration file**.

| Surface | Invocation | Layout resource | Lock | Placement |
|---|---|---|---|---|
| Application menu | `sofi -show drun` | `/org/sofi/default.sasi` | `sofi-menu.pid` | south centre, 560 wide × 62%, 80px inset |
| Task / window manager | `sofi -show window` | `/org/sofi/panel-window.sasi` | `sofi-window.pid` | south, 98% wide, 12px inset |
| Sheet switcher | `sofi -show sheets` | `/org/sofi/panel-sheets.sasi` | `sofi-sheets.pid` | north centre, 720 wide, 8px below the bar |
| Notification banner | `sofi -notification-daemon` | `/org/sofi/panel-notifications.sasi` | `sofi-notifyd.pid` | south east, 400 wide |
| Notification history | `sofi -show notification-history` | `/org/sofi/panel-notification-history.sasi` | `sofi-notification-history.pid` | east, 420 wide × 76% |
| Message toast | `sofi -e <msg>` | `/org/sofi/panel-notify.sasi` | `sofi-notify.pid` | north east, 380 × 55 |
| **System tray host** | `sofi -tray-daemon` | *none — no surface* | bus name `org.kde.StatusNotifierWatcher` | Renders inside the task strip's right corner |

**The task strip's zoning changed 2026-08-26.** R38 replaced the matched/total window count with the
system tray, and the R38 amendment removed the count outright on USER's instruction. `mainbox` is now
`[ "inputbar", "listview", "tray" ]`.

Placement revised 2026-08-25 by `DECISIONS_LOG.md` R25, R28, R29 and R34. Net effect against what
Phase 10 started from: the launcher moved west→south-centre, the sheet pane east→north-centre, and
the history menu stayed on the east edge but grew from 460 to 420 wide with the new layout. R34
swapped the launcher and the history menu after R25/R28 had been seen on hardware.

**Measured on hardware, 2026-08-25**, by `WAYLAND_DEBUG=1` against the running compositor:

| Observation | Value | What it settles |
|---|---|---|
| First `layer_surface.configure` | **1920 × 1166** | sofi's "screen" is hikari's USABLE area, not the output. The output is 1200 tall, so the top bar is **34px** (`font.height + HIKARI_BAR_PADDING`) |
| `-e` toast `configure` | 380 × 55 | The non-capture path: surface is exactly its own size, positioned by layer-shell margins |
| Menu surfaces | `set_size(1920,1166)`, `set_anchor(5)` = TOP\|LEFT, `set_margin(0,0,0,0)` | The click-capture path: full usable area, panel placed inside it in software |

**Consequence, and it is the one thing to remember about placing a panel here.** `location: north`
is already flush under the top bar — the compositor subtracted it before sofi ever saw a size.
An offset there is a deliberate gap, never bar clearance. The comment in `panel-notify.sasi`
claiming its 48px offset cleared the bar was wrong and is corrected.

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



## Notification cleanup — why it needs a bus

Delivered 2026-08-25 under R30. Two verbs, deliberately separate: **DismissAll** retires the live
set and keeps the record; **ClearHistory** destroys the record.

The constraint that shapes the design: `sofi -show notification-history` is a SEPARATE PROCESS
from the daemon. It reads the file the daemon persists on every change
(`sofi_notify_store_save()`), so a clear performed by writing that file would be overwritten by
the daemon within seconds. The mutation has to happen where the ring lives.

So both verbs travel over `org.sofi.Notifications`, a second interface exported on the *existing*
`/org/freedesktop/Notifications` object. A second interface costs one extra `register_object` and
no second bus name, and it leaves the advertised freedesktop interface exactly what the spec says
it is.

| Caller | Route |
|---|---|
| History mode, inside the daemon | Direct store call; `sofi_view_is_daemon()` already distinguishes |
| History mode, standalone | D-Bus, then `sofi_notify_store_load()` to refresh its own stale copy |
| History mode, no daemon reachable | Mutates its own copy — correct, because nothing exists to overwrite it |
| `-notification-clear[-history]` | D-Bus only. Exits non-zero when no daemon answers, and changes nothing |

`NO_AUTO_START` throughout: clearing a list must never leave behind a daemon the user did not ask
for.

### Extended 2026-08-26: the per-entry verbs, and why `live` is never persisted

The bulk verbs got a bus route when they were written; the per-entry ones never did, and called
straight into the store. In a standalone panel that mutated a copy the daemon overwrites within
seconds — and `sofi_notify_service_invoke_action()` reached `emit_signal()`, found no connection
outside the daemon, and dropped `ActionInvoked` on the floor. Both were silent.

| Added to `org.sofi.Notifications` | Purpose |
|---|---|
| `Dismiss(u id)` | Retire one entry where the ring actually lives |
| `InvokeAction(u id, u index)` | Emit `ActionInvoked` from the process that owns the connection |
| `GetLive() → a(uus)` | id, action-pair count, `desktop-entry` |

**`live` and `actions` are deliberately absent from the persisted file, and must stay absent.** Both
describe a notification being on screen *right now*; they belong to the process that received it, and
a file asserting either is wrong the instant the daemon acts. The history mode reads the record from
disk and asks the daemon for the rest — `sofi_notify_store_apply_live()` overlays the answer, clearing
before setting so a dismissed entry is demoted rather than left stale.

Before this, every guard of the form `if (n->live)` in the standalone history panel was **dead code**:
Enter did nothing, Shift+Delete did nothing, and the "still on screen" stripe could never render.

**The daemon also now loads its own history at startup.** `sofi_notify_service_start()` called
`init()` and never `load()`, so the ring began empty and the first notification's `save()` truncated
the file — history was destroyed at every login. Measured: without the fix, two seeded entries became
one and the id counter restarted at 1, so a restarted daemon handed out ids senders still held.

**`desktop-entry` is now stored and persisted.** It was parsed and discarded; it is the only key a
notification carries that could identify the window behind it.

**Enter on a history entry raises the window that sent it**, through three functions exported out of
`source/modes/wayland-window.c` so the wlr-foreign-toplevel machinery has one implementation rather
than two (R43, R45):

| Function | Called from | Does |
|---|---|---|
| `sofi_wayland_window_toplevels_open()` | `history_mode_init()` | binds the registry and round-trips twice |
| `sofi_wayland_window_activate_app_id()` | `history_mode_result()` | matches, `activate()`, `wl_display_flush()` |
| `sofi_wayland_window_toplevels_close()` | `history_mode_destroy()` | releases everything |

**The `_init`/`_result` split is load-bearing, not stylistic.** `wl_display_roundtrip()` dispatches
the *default* queue, where sofi's own surface events live, so a round trip from inside `_result`
re-enters the view machinery mid-teardown and segfaults. `_init` runs before any view exists. This
is why the window mode has only ever round-tripped in `_init`, and it is now written down in
`include/modes/wayland-window.h` rather than left implicit.

The listeners stay attached after `open()`, so a panel holds a **live** list: a window opened or
closed while it is up arrives through sofi's main loop as ordinary traffic.

**Matching is exact or reversed-DNS-tail only, and it fails closed.** `desktop-entry` and `app_id`
are different namespaces that often agree. A looser rule eventually raises the *wrong* window, which
is worse than raising none — the user asked to be taken somewhere and would be taken somewhere else.
When nothing matches, the log says how many candidates were enumerated, so an under-report can never
again be mistaken for a no-match.

**The banner's clear button is not a new feature.** `kb-custom-1` has always meant dismiss-all in
`source/modes/notifications.c`, and was unreachable because the daemon's surface is forced to take
no keyboard. Pointer input is unaffected by keyboard interactivity, so a themed `button-*` widget
with `action: "kb-custom-1"` dispatches it (`textbox_button_trigger_action`, `source/view.c:1603`).

---

## The system tray — StatusNotifierItem, in its own process

Delivered 2026-08-26 under R38–R44. Build option `tray` → `SYSTEM_TRAY`, with **no dependency on
`notify`**: the tray was first written inside the notification daemon and R41 split it into its own
process, so the two share no code and no state. A configure-time constraint tying them together
outlived that design and was removed.

There is no Wayland system-tray protocol. What every Wayland desktop implements is
StatusNotifierItem: three D-Bus interfaces, no surface, no input routing, no privileged operation.
That is why this is in sofi and not the compositor (F9–F12) — and hikari links no D-Bus stack at all,
while `hikari-topbar` links nothing but libc and is display-only.

### Why it is its own daemon

R41, ruled by USER after the first implementation put it inside the notification daemon. Two D-Bus
servers on one main loop share their fate: tray code parses hostile input from arbitrary
applications, notifications are the more important service, and one crash takes both. **The split
cost ten lines**, because the tray modules had never included anything but `gio` and each other.

It also needs **no display**, and is dispatched in `main()` before display selection for that reason:
requiring a Wayland session to run a bus service would be a dependency invented rather than
inherited. Measured — with `WAYLAND_DISPLAY` and `DISPLAY` unset, the watcher took its name in 6
polls against 364 through the display path.

Single instance is the bus name, not a pidfile — the same argument the notification daemon makes.

### Four modules, one direction of dependency

```
   tray-watcher.c   registry: which items exist. Owns
        │           org.kde.StatusNotifierWatcher + StatusNotifierHost-<pid>
        ▼
   tray-item.c      one item's proxied state: properties, the New* signals,
        │           icon precedence, the IconPixmap decoder
        ▼
   tray-service.c   org.sofi.Tray — what the task strip is told
                              │
                              │  D-Bus  (separate process)
                              ▼
   tray-client.c    read by `sofi -show window`; feeds the view's tray zone
```

**The load-bearing details, each of which was a defect before it was a rule:**

| Rule | Why |
|---|---|
| `IsStatusNotifierHostRegistered` must answer TRUE | An application asks **once**, at its own startup, and one that gets FALSE shows no icon and never asks again. This one property is the difference between a tray and an empty strip |
| Accept **both** registration forms | `RegisterStatusNotifierItem`'s argument is a bus name from Qt/KDE items and an object path from several GTK and Electron ones. The spec never pinned it down; handling one form shows an empty tray for half the desktop with no diagnostic |
| Reap by `NameOwnerChanged` | There is **no Unregister method in the specification at all**. An item exists exactly as long as its bus name does |
| Re-fetch everything on any `New*` | The change signals carry no payload, and these are **not** `PropertiesChanged`, which most items never emit — so a `GDBusProxy` cache is useless here |
| Debounce 100ms per item, coalesce 50ms per signal | The first bounds how often an application can make us re-read it; the second how often a subscriber is woken. Chatty applets are the norm |
| Never `REPLACE` **either** name, and exit when one is lost | Two trays fighting over the watcher would flap every icon on the desktop. `org.sofi.Tray` first used REPLACE, copying the notification daemon — wrong here, because the tray holds **two** names: a second daemon could take `org.sofi.Tray` while the first kept the watcher and every item, leaving the task strip talking to a daemon with nothing to serve. The names must move together, so both refuse replacement and losing either stops the daemon |

### The icon decoder — the one place taking hostile input

Every dimension and every byte is chosen by the sending application. Validation happens **before a
single pixel is read**; oversized pixmaps are refused rather than scaled.

Two conversions are mandatory and invisible to a type check: the wire format is ARGB32 in **network
byte order** (reassemble, do not copy) and carries **straight** alpha where cairo wants
**premultiplied** (copying puts a bright halo on every anti-aliased edge).

**Capped at 512px, and decoded inline rather than in a threadpool (R42).** The threading requirement
came from a 4096px cap inherited from `image_from_hint()` — right for a notification image, wrong for
a tray icon, which is realistically 16–64px. At 512 the worst case is ~1ms. Decoding is also lazy:
it runs when an icon is first *wanted* after changing, so an applet repainting while the strip is off
screen costs nothing.

### org.sofi.Tray

| Member | Purpose |
|---|---|
| `ListItems() → a(sssssubuuay)` | service, title, icon name, icon theme path, **menu path**, status, is-menu, width, height, premultiplied ARGB32 pixels |
| `Activate(s,i,i)` / `SecondaryActivate(s,i,i)` / `ContextMenu(s,i,i)` | Forwarded to the application |
| `Changed` | Registry or property change, coalesced |

**The menu path is carried but the menu itself is not (R46).** The strip talks
to the application's `com.canonical.dbusmenu` object **directly**; the daemon
only says where it is. The daemon owns registry state, while a menu is a
transient interaction belonging to whoever is displaying it, and mirroring seven
methods and three signals across this interface would be a second protocol for
no gain. The bus name is the same one already embedded in `service`, which
`tray-client.c` splits off at the first `/` once, on arrival.

**`is_menu` is advisory and must not be used as the test for "has a menu".** An
item whose entire interface *is* its menu may omit `ItemIsMenu` altogether — one
on this desktop does (F29). Whether `menu path` is non-empty is a fact; the
property is a hint applications forget to set.

**Items are addressed by service string, never by index.** The strip's list is a snapshot of another
process and an application can exit between the draw and the click; an index would activate whatever
shifted into that slot — the wrong application, silently.

**Pixels rather than a file path (R40).** A tray icon is a couple of kilobytes, so the reply stays
small and the receiver hands the buffer straight to cairo. A file would add a temp-file lifecycle
whose failure mode is stale icons surviving a crash.

### Tray menus — `source/dbusmenu.c` and `source/modes/tray-menu.c`

Delivered 2026-08-26 under R46, after USER found that clicking a tray icon did
nothing and right-clicking closed the panel.

**Root cause of "left click does nothing":** sofi sent `Activate`, and the item
did not implement it. Measured on the live desktop —
`org.freedesktop.DBus.Error.UnknownMethod: No such method "Activate"`. Every
libappindicator item measured is the same shape: `Scroll`, `SecondaryActivate`,
`XAyatanaSecondaryActivate`, and **no `Activate`, no `ContextMenu`**. The error
was discarded by `call_activation()`'s fire-and-forget, so the click was silent.

**Root cause of "right click closes the panel":** `kb-cancel` binds
`MouseSecondary` in `SCOPE_GLOBAL`, whose check passes unconditionally.

| Piece | Does |
|---|---|
| `SCOPE_MOUSE_TRAY` + `WIDGET_TYPE_TRAY` | A scope of the tray's own. nk_bindings walks scopes **descending**, so it is consulted before `SCOPE_GLOBAL` — that is what stops right click cancelling. It also retires the `WIDGET_TYPE_EDITBOX` promotion hack, which only ever existed to borrow a scope that had mouse bindings |
| `mt-activate` / `mt-context-menu` / `mt-secondary-activate` | The three buttons as rebindable bindings. The default mouse bindings could not do this: they are `MousePrimary` only and carry no button identity |
| `source/dbusmenu.c` | The `com.canonical.dbusmenu` client, on GDBus. **No new dependency**: `gio-unix-2.0` is unconditional and every call shape already existed in `tray-item.c`. `libdbusmenu-glib` was rejected on licence (GPLv3 / LGPL2.1 / LGPL3 against MIT) for ~300 lines of saved work |
| `source/modes/tray-menu.c` | The renderer, as a mode. Submenus descend by replacing the list and returning `RESET_DIALOG` with a `..` row — `filebrowser.c`'s shape exactly |
| `tray_open_menu()` in `view.c` | `sofi_enable_mode("tray-menu")` then `MENU_QUICK_SWITCH | index` and quit, which `sofi.c` turns into `sofi_view_switch_mode()` **on the same surface**. The mode-switcher tabs have always worked this way |

**Two rules that are easy to get wrong:**

- **Prefer the menu, fall back to `Activate` — never gate on `ItemIsMenu`.** The
  property is optional and widely omitted by the very items that are nothing but
  a menu.
- **Diagnostics are list rows, not `_get_message`.** A mode message needs a
  `message` widget in the layout, and the task strip's mainbox is
  `[ inputbar, listview, tray ]` — it has none, so a message renders nowhere.
  Every layout has a listview. Placeholder rows are marked disabled, reusing the
  guard that already refuses a row the application greyed out.

Synchronous calls throughout, bounded at 2s: this runs from a mode's `_init` and
`_result` with nothing to show until the answer arrives, the same position
`sofi_notify_service_refresh_live()` is in. A wedged application costs a short
pause and an explained empty menu, not a hung panel.

### In the view

The zone is `icon` widgets packed into a box at runtime — **not a listview**, which is why the
one-listview restriction never applied. `mode-switcher` is the structural precedent. Two additions
were needed: `box_remove_all()` and `icon_set_fetch_id()`.

**A tray click must not reuse the button path (F17).** `textbox_button_trigger_action()` dispatches
through `sofi_view_trigger_global_action()`, whose `CUSTOM_1..19` case sets `state->quit = TRUE` — so
every tray click would tear the strip down. The tray has its own handler that dispatches and returns
without quitting.

### Not implemented

**Context menus.** A tray item's `Menu` property points at a `com.canonical.dbusmenu` object; that
interface name is historical and frozen, it is what Qt/KDE, ayatana-appindicator, Electron and the
rest already export, and there is no alternative — the apparent one, asking the application to draw
its own menu via `ContextMenu(x,y)`, is unreliable on Wayland because a client cannot position a
popup at arbitrary output coordinates. sofi would read the interface over GDBus and **would not link
`libdbusmenu`**, so it is a wire format rather than a dependency and `AGENTS.MD` §2 is not engaged.
v1 ships `Activate` / `SecondaryActivate` only.

---

## The palette — one file, sixteen slots

`doc/palette.sasi`, compiled in as `/org/sofi/palette.sasi` and parsed immediately before whichever
panel layout the invocation selected (`sofi_parse_builtin_resource()` in `source/sofi.c`). It is
the only file in the tree containing a colour value; the six layouts contain none.

**Why this works, mechanically.** `@name` is a `P_LINK` property, and
`sofi_theme_find_property()` (`source/theme.c:744`) resolves it on first *lookup*, not at parse.
Lookup happens at widget construction, after every parse has finished. So the palette does not
have to be parsed last to win — and a `* { }` block in `~/.config/sofi/config.sasi`, parsed after
both, overrides it for every surface at once. Later parses overwrite the same key in the root
property table, which is what makes the override a plain redefinition rather than a merge rule.

**Correspondence with the compositor.** The sixteen slots are byte-identical to hikari-sakura's
`ui { palette }`, and the semantic aliases map onto its `ui { colorscheme }`: sofi `accent` =
`color12` = hikari `selected`; `accent-soft` = `color4` = `first`; `accent-strong` = `color13` =
`insert`; `urgent` = `color9` = `conflict`; `muted` = `color8` = `inactive`; and `background` is
`color0` at 90%, which is hikari's `bar` exactly.

**Contrast gate, computed rather than eyeballed** (WCAG 2.1 relative luminance, against the
`color0` ground):

| Alias | Slot | Ratio | Allowed use |
|---|---|---|---|
| `foreground` | color7 | 10.54 | body text |
| `foreground-bright` | color15 | 13.41 | emphasis |
| `foreground-dim` | color6 | 5.97 | secondary text |
| `warning` | color11 | 10.60 | text or fill |
| `accent-strong` | color13 | 7.96 | text or fill |
| `accent` | color12 | 6.49 | text or fill; `on-accent` on it is 6.49 |
| `urgent` | color9 | 5.89 | text or fill |
| `hint` | derived | 3.66 | placeholder only |
| `accent-soft` | color4 | 4.31 | **large/bold text and non-text marks only** |
| `critical` | color1 | 4.07 | **fills and stripes only, never text** |
| `muted` | color8 | 2.29 | **non-text only** — separators, troughs, disabled fills |

Two corrections to what was assumed when this was scoped. The *old* white-on-`#916778` pair was
**4.76:1** and did pass AA — the reason `on-accent` is `color0` is that white on the *new* accent
is **2.40:1**. And `color8` cannot carry text at all, so every "dimmed" role in the old layouts
(off-sheet windows, empty sheets, retired history entries) now uses `foreground-dim`.

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
