# DECISIONS LOG

Reverse-chronological. Most recent entries at the top.

---

## 2026-08-25 11:47 — R34 and R35, after seeing Phase 10 on hardware

### R34 — the application menu and the notification history swap places

USER, on the delivered layout: *"swap the positioning of the application menu and the notification
menu."* Which two surfaces was ambiguous — the banner is not a menu, but "the notification menu"
had been used for both — so it was tabled and ruled: **the launcher and the history menu.**

| Surface | R25/R28 | R34 |
|---|---|---|
| Application menu | east, 300 × 76% | **south centre, 560 × 62%**, 80px clear of the task strip |
| Notification history | south centre, 520 × 62% | **east, 420 × 76%**, 16px inset |

This reverses the "side panel look and feel" framing R25 was written under, and that is the USER's
call having seen it rather than a misreading: the launcher is now the surface in front of you and
the notification centre takes the edge notification centres conventionally take. Widths were not
swapped with the positions — a history row is two lines of arbitrary application text and its
footer carries a count and both cleanup verbs, so it needs more width than a launcher row at the
same edge.

R25's second half stands: the placement is four properties, and `sofi-customisation(5)` documents
the override, now written as the side-panel form since that is what it is no longer.

### R35 — a packed box that is not meant to grow must say `expand: false`

USER: *"the application menu lost its full panel scrolling and was only populating half its size."*

Confirmed and fixed. `sofi_view_add_widget()` (`source/view.c`) creates any unrecognised widget
name as a box and packs it with `box_add(..., TRUE)` — **expand defaults to TRUE**. The listview is
also packed expanding. So in a vertical `mainbox` the two split the available height between them,
and the launcher's list rendered at roughly half the window with the remainder empty; the row count
`listview_resize()` derives from the widget's real height was halved with it, which is the missing
scroll.

`expand: false` was present on the footer in `panel-window.sasi`, `panel-notifications.sasi` and
`panel-notification-history.sasi`, and absent in `default_theme.sasi` — a plain omission, not a
design difference. `box_add` reads the property with the passed value as its default
(`source/widgets/box.c:303`), so the theme is the right place to fix it.

**Recorded as a rule because it is a trap, not a typo:** the failure is silent, it looks like a
sizing bug rather than a packing bug, and it will recur every time a layout gains a custom box.
Any packed box that should take only its content's space needs `expand: false`, and the reason is
now commented at the site.

---

## 2026-08-25 10:53 — Theming and layout modernisation. R25–R33 ruled.

USER request: *"we need to update and modernise the theming and color schemes"* with a sixteen
entry palette supplied verbatim; *"the right panel application menu needs to be modernised and
have that side panel look and feel"*; *"the bottom bar taskbar needs to be cleaner and better
structured"*; *"the notification menu and history needs to have a cleanup function and not look
the same as the other menus - also it should be a vertical panel at the middle of the bottom of
the screen"*; *"sheets panel ... should be from the middle of the top of the screen slightly below
the clock in the toppbar"*; *"the user facing documentation regarding the readme as well as
cusotmisaiton also needs ot be comprehenively enhanced"*.

Four ambiguities were tabled to the USER before any scoping and are ruled below as R25, R28, R30
and R31.

### Investigation findings that constrain the work

These were measured or read out of the source, not assumed. They are recorded because three of
them change what the panels may say.

**F1 — the supplied palette is already hikari-sakura's palette.** `~/.config/hikari/hikari.conf`
carries the identical sixteen values under `ui { palette }`, and its `ui { colorscheme }` block
already assigns them meaning: `background = color0`, `active = color15`, `inactive = color8`,
`selected = color12`, `first = color4`, `grouped = color5`, `insert = color13`,
`conflict = color9`, and `bar = "#2b1e3ae6"` — color0 at ~90%. The compositor's top bar is
therefore *already* the exact background sofi should be sitting under. This is not a new colour
scheme to invent; it is one scheme two programs must agree on.

**F2 — `@` links resolve lazily, so a shared palette resource works.** `sofi_theme_find_property()`
(`source/theme.c:744`) resolves a `P_LINK` on first *lookup*, against the global root property
table, not at parse time. Property lookup happens at widget construction, after every parse has
completed. So a palette parsed as its own GResource before the panel layout is visible to that
panel, and — the part that matters for customisation — a `* { accent: … }` block in
`~/.config/sofi/config.sasi` parsed *after* both still wins, because nothing has been resolved
yet. One palette file can therefore drive all six layouts and remain user-overridable.

**F3 — sofi's "monitor size" on Wayland is hikari's usable area, not the output.**
`display_get_surface_dimensions()` (`source/wayland/display.c:2090`) returns
`wayland->layer_width/height`, which is written by the layer-shell `configure` event
(`:1739`). The initial `set_size(0,0)` with all four anchors makes the compositor answer with the
usable area. `BLUEPRINT.md` records that answer as **1920 × 1166** on this host; the output is
1200 tall, so **hikari's top bar is 34px** (`font.height + HIKARI_BAR_PADDING`, `hikari/src/bar.c:1181`).
Consequences: `location: north` already lands flush *below* the bar with no manual compensation,
so the sheets pane needs only a small deliberate gap; and the claim in `doc/panel-notify.sasi:58`
that its 48px offset "clears hikari's own top bar" is false — the bar was already cleared and the
48px is 48px of empty space.

**F4 — offset signs are inverted between the two positioning paths.** With `click-to-exit: true`
(`config/config.c:160`, the default) `window_update_size_with_outside_click()`
(`source/wayland/view.c:178`) sizes the surface to the whole usable area and positions the menu in
software, where offsets move the panel **inward** — positive is away from the anchored edge. With
`click-to-exit: false` `window_update_size_normal()` (`:167`) hands the offsets to
`zwlr_layer_surface_v1_set_margin` as `(y, -x, -y, x)` (`display.c:2176`), where the sign is
**negated**. So `doc/panel-notifications.sasi`'s `-80px` is correct for the daemon and
`doc/panel-window.sasi`'s `y-offset: -12px` is not: on the software path it resolves to
`H - h + 12`, putting the bottom 12px of the task strip outside the usable area. Filed as T1.6.

**F5 — the banner's "clear all" is unreachable.** `notifications_mode_result()`
(`source/modes/notifications.c:192`) already implements `kb-custom-1` as dismiss-every-live-entry,
but `sofi_notify_daemon_force_safety()` forces `wayland-keyboard-interactivity: none` on that
surface, so no key ever arrives. The pointer path is unaffected — `wl_pointer` is fully bound
(`display.c:1048`) — so a themed `button-*` widget with `action: "kb-custom-1"`
(`source/view.c:1603`) reaches it. Existing code, no new binding needed.

**F6 — the history mode has no clear of any kind**, and clearing it from a standalone
`sofi -show notification-history` process cannot simply write the file: the daemon holds the
authoritative ring in memory and `sofi_notify_store_save()` runs on every change
(`source/notify-store.c:91`), so it would overwrite the cleared file within seconds. Any clear
issued outside the daemon must travel to the daemon.

**F7 — the theme engine already has the vocabulary for a modern look.** `linear-gradient`
background images, per-side `border`, four-corner `border-radius`, `text-outline`,
`cursor-outline`, `handle-rounded-corners`, and named `textbox-*` / `button-*` / `icon-*` widgets
that can be packed into `mainbox` and carry a keybinding `action`. Nothing here needs new
properties in `source/theme.c`.

**F8 — the palette is inlined six times.** `doc/default_theme.sasi`, the five `doc/panel-*.sasi`
files and `sofi-config/colors-default.sasinc` each carry their own copy of the colour block.
Seven copies of four values, already drifted (`dimmed`, `critical` and `low` exist in some and not
others). R26 removes the duplication rather than updating it seven times.

### R25 — the application menu ships on the **east** edge, and the edge is a documented one-liner

Ruled by USER: *"Both edges available."* The sheets pane vacates the east edge under R29, so the
launcher takes it: right = launcher, top = sheets, bottom = tasks, bottom-centre = notification
history. `doc/default_theme.sasi` ships `location/anchor: east`. `CONFIG.md` documents the
override — a four-line `window { }` block in `~/.config/sofi/config.sasi` — as the first worked
example in a new "Move a panel" section, with west given as the copy-paste alternative.

The default layout is the *general* surface, not only drun: `sofi_builtin_panel_resource()`
(`source/sofi.c:1078`) falls through to it for `run`, `ssh`, `combi`, `filebrowser` and every user
script mode, so this decision moves all of them.

### R26 — one palette, one file, sixteen positional slots plus semantic aliases

The sixteen values become `doc/palette.sasi`, registered in `resources/resources.xml` as
`/org/sofi/palette.sasi` and parsed in `source/sofi.c` immediately before the panel layout. Every
panel drops its inlined `* { }` colour block. Valid by F2.

The file carries **both** layers, mirroring `hikari.conf` so the two configurations read the same
way:

- `color0`…`color15` — positional, no meaning, the only place a hex value appears.
- Semantic aliases, each a `@colorN` reference: `background`, `surface`, `surface-alt`,
  `foreground`, `foreground-dim`, `muted`, `accent`, `accent-soft`, `accent-strong`, `urgent`,
  `critical`, `warning`, `on-accent`, `border-soft`.

**Mapping as shipped**, chosen to agree with hikari's own `colorscheme` slot by slot rather than to
be pretty in isolation. Five rows differ from the sketch this section first carried; the
differences came out of the contrast computation below and are marked.

| Alias | Slot | Value | Ratio | Why |
|---|---|---|---|---|
| `background` | color0 @ 90% | `#2b1e3ae6` | — | Byte-identical to hikari's `bar`. A panel under the top bar reads as the same surface. |
| `background-solid` | color0 | `#2b1e3a` | — | **Added.** Notification cards and the toast land over arbitrary content and cannot be translucent behind body text. |
| `surface` | color0→color8 25% | `#382d45` | — | Inset fields. Replaces the old `#101010`, which was near-black against a violet ground. |
| `surface-alt` | color0→color8 50% | `#443c50` | — | **Changed.** Was color8; that is a foreground tone, not a recess. |
| `hint` | color0→color6 70% | `#7c7986` | 3.66 | **Added.** Placeholder text. Named `hint`, not `placeholder`: `placeholder` and `placeholder-color` are real textbox properties, and a global of that name would be found by the widget's own lookup, fail the type check, and silently suppress the placeholder text. |
| `foreground` | color7 | `#d4d4d9` | 10.54 | Body text. Not color15 — reserving the brightest tone for emphasis is what gives a list hierarchy. |
| `foreground-bright` | color15 | `#f0edf2` | 13.41 | Emphasis. |
| `foreground-dim` | color6 | `#9fa0a6` | 5.97 | Secondary text: app names, timestamps, counts. |
| `muted` | color8 | `#5e5966` | 2.29 | **Changed — non-text only.** Separators, troughs, disabled fills. It was to have carried placeholder text, empty sheets and off-sheet windows; at 2.29:1 it cannot carry text at all, and all three roles moved to `hint` or `foreground-dim`. |
| `accent` | color12 | `#aba0d9` | 6.49 | Selection. Same slot as hikari's `selected`, so a focused row and a focused window agree. |
| `accent-soft` | color4 | `#8e7cc3` | 4.31 | Prompts, leading stripes. AA for large/bold text and for the non-text marks it is mostly used as; **not for body copy**. |
| `accent-strong` | color13 | `#cfaedc` | 7.96 | Live/current markers. hikari's `insert`. |
| `on-accent` | color0 | `#2b1e3a` | 6.49 | Text **on** an accent fill. |
| `urgent` | color9 | `#df8787` | 5.89 | hikari's `conflict`. Critical *as text*. |
| `critical` | color1 | `#c96464` | 4.07 | **Fills and stripes only**, never text. |
| `warning` | color11 | `#f5cf9e` | 10.60 | Unread counts, truncation markers. |
| `border-soft` | color8 @ 60% | `#5e596699` | — | Hairlines. |

`sofi-config/colors-default.sasinc` is regenerated to the same content so the installed theme and
the compiled-in one cannot drift, and `sofi-config/config.sasi` is regenerated from the new
`default_theme.sasi`.

**Contrast is a constraint, not a taste.** Every text-on-fill pair is checked against WCAG AA
(4.5:1 for body, 3:1 for large bold) before it ships.

**Correction to this ruling as first written.** It justified `on-accent` by claiming the old
`#FFFFFF` on `#916778` was 3.2:1. That was asserted before it was computed, and it is wrong — the
old pair is **4.76:1** and did pass AA. The ruling survives on a different number: white on the
*new* accent (`color12`, a much lighter violet) is **2.40:1**, so keeping white would have been the
regression rather than the fix. The table above carries the computed figure for every alias.

### R27 — "modernised" means a stated set of moves, not a vibe

So the result is reviewable. Applied uniformly across all six layouts:

1. **An 8px spacing grid.** Every padding, margin, spacing and radius is a multiple of 4, and
   preferably of 8. Today's `9px`/`15px`/`14px` values are arbitrary.
2. **A radius scale**: 14px window, 10px inset field, 8px row, 4px hairline. One relationship,
   not seven independent numbers.
3. **Selection is a fill plus a leading marker**, not a fill alone — the marker survives the
   ~1.2s where a repaint has not landed and reads at a glance on a dense strip.
4. **Two-tier type.** Primary label at weight, secondary metadata at `foreground-dim` and
   `<small>`. The notification modes already do this; the launcher and task strip do not.
5. **Real placeholder colour.** `#444444` is invisible on the new ground; `muted` replaces it in
   all four places it appears.
6. **Scrollbars themed everywhere they are enabled**, with `handle-rounded-corners: true`.
7. **No new theme properties.** Everything above is expressible today (F7).

### R28 — the notification **history** moves to bottom-centre; the live banner stays bottom-right

Ruled by USER: *"History only → bottom-centre."* The history menu becomes a tall vertical panel
anchored `south`, horizontally centred, rising from above the task strip. The daemon's live banner
keeps `south east`, so an arriving toast never covers the part of the task strip the pointer is
travelling toward.

### R29 — the sheet switcher moves to top-centre

`location: north; anchor: north`, horizontally centred by the existing `WL_NORTH` case
(`source/wayland/view.c:199`). By F3 this is already flush below hikari's 34px bar, so the
"slightly below the clock" gap is a deliberate `y-offset: 8px` and is commented as such rather
than as bar compensation. The pane rotates from a 190px vertical column to a horizontal row of ten
sheet chips (`listview { layout: horizontal }`, the BARVIEW renderer — the same reasoning recorded
in `BLUEPRINT.md` for the task strip), which is the shape that belongs under a top bar. The
`dynamic: false` / `fixed-height: true` / ten-rows guarantee is preserved on the horizontal axis:
sheet positions must not move between invocations.

### R30 — two separate cleanup actions, each with a clickable affordance

Ruled by USER: *"Two separate actions."*

- **Dismiss all** — retires every live entry, history retained. `kb-custom-1`. Already implemented
  in both notification modes; on the banner it is currently unreachable (F5) and is exposed as a
  `button-clear-all` widget in `doc/panel-notifications.sasi`.
- **Clear history** — wipes the ring and the persisted file. `kb-custom-2` in the history mode,
  plus a `button-clear-history` widget. New: `sofi_notify_store_clear_history()`.

Per F6 a clear issued from a standalone history process must reach the daemon. It travels as a new
`ClearHistory` method on a **second interface, `org.sofi.Notifications`, exported on the existing
`/org/freedesktop/Notifications` object**. A second interface on the owned object costs one
`GDBusNodeInfo` and no second bus name, and keeps a private method off the freedesktop interface
where it does not belong. The history mode calls the store directly when it *is* the daemon
(`sofi_view_is_daemon()`, the test `history_mode_init` already makes), and over the bus otherwise;
when no daemon owns the name it falls back to clearing the file itself, which is correct because
there is then nothing to overwrite it.

`-notification-clear` and `-notification-clear-history` are added as one-shot CLI flags so the
compositor can bind them directly, matching how `menu`/`windows`/`sheets`/`notifications` are
already bound in `hikari.conf:427-430`.

### R31 — the task strip is zoned: filter | tasks | count

Ruled by USER: *"Zoned: filter | tasks | count."* `mainbox` packs
`[ "inputbar", "listview", "textbox-count" ]` horizontally. The filter box is fixed-width and
visually inset; the strip expands; the right zone is a `textbox-count` bound to `num-filtered-rows`
showing the window count. Rows become icon + primary title with the window class demoted to
`foreground-dim` `<small>` — inverting today's `"{c}  ·  {t:28}"`, which leads with the least
distinguishing field. Zones are separated by spacing and a `border-soft` hairline, not by boxes.

### R32 — the notification surfaces must not look like the menus

The USER's requirement, and it is met by shape rather than by hue, because the palette is shared.
Menus: filled selection, uniform rows, a search field at the head. Notifications: **no filled
selection**, a 4px leading urgency stripe per card, cards separated by gaps on a transparent
ground rather than rows in a panel, and a header strip carrying the count and the clear buttons.
The banner keeps its deliberately invisible selection (it takes no keyboard, so drawing one would
promise an interaction that does not exist). The history panel gains a real selection because it
does take the keyboard — but as a `surface` fill with an `accent` stripe, not the menus' solid
`accent` fill.

### R33 — the documentation deliverable is scoped, not "enhanced"

`README.md` gains a real theming section (the sixteen slots, the semantic table, the override
example) and a per-surface geometry table that matches what ships. `CONFIG.md` is restructured
around tasks a user actually performs: recolour everything, move a panel, resize a panel, restyle
one surface without touching the others, bind the clear actions in `hikari.conf`. A new
`doc/sofi-customisation.5.markdown` carries the long form and is added to `doc/meson.build` and the
manpage list in `README.md`. `doc/sofi-theme.5.markdown` gains the palette-override section and a
correction to its "Default theme loading" text, which does not mention the built-in panel layouts
at all.

**Not in scope**, stated so it is a decision and not an omission: no new theme properties, no
change to the four-surface architecture, no logo work (still open from Session 5b), and no attempt
to make the panels track a live palette reload — sofi is a one-shot process per invocation, so it
picks up a palette edit on next summon and that is the correct behaviour.

---

## 2026-08-24 10:20 — sofi becomes the hikari-sakura shell. R16–R23 ruled, Q17 closed.

The USER directed that sofi provide **four system surfaces natively** — application menu on the
left, task/window manager along the bottom, sheet switcher on the right, and a notification
system — and that these be *"the config-less native structure and build of this program as it is
a fork made specifically for this current active hikari-sakura window compositor."*

That is a change of project identity, not a feature request. sofi stops being a rofi/dmenu
drop-in that happens to run on hikari, and becomes hikari-sakura's shell. The rulings below
follow from that.

### Corrections to earlier analysis in this session

Two claims I made were wrong and were retracted before any work was built on them.

**1. `sofi -show window` was reported as non-functional. It was not.** I claimed the installed
hikari lacked `zwlr_foreign_toplevel_manager_v1` because `strings hikari` returned zero hits and
the binary predated the implementing commit. Both legs were bad: **wlroots is dynamically
linked**, so the protocol's name string lives in `libwlroots.so` and can never appear in the
compositor binary; and building from a working tree before committing it is the normal order,
not evidence of staleness. `nm -u` shows the symbols, and a `wl_registry` dump from the running
compositor shows `zwlr_foreign_toplevel_manager_v1 v3` advertised — exactly the version sofi
caps at.

**Method ruling that follows from this: capability claims about the compositor are settled by a
registry dump, not by inference from binaries or timestamps.** A 20-line `wl_registry` listener
was compiled for this purpose and should be reached for again rather than re-derived.

**2. The sheet switcher was reported as fully blocked. It was half-blocked.** `display_sheet()`
(hikari `src/workspace.c:160-192`) hides every view not on the target sheet, and
`hikari_view_hide()` publishes that through foreign-toplevel's minimised bit — hikari's own
comment states it outright: *"hikari's `hidden` flag IS minimised as far as foreign-toplevel
clients are concerned."* So "on the displayed sheet" versus "elsewhere" already reaches sofi on
a live protocol. sofi parsed the bit into `TOPLEVEL_STATE_MINIMIZED` and never read it.

### Rulings

- **R16 — Four compiled-in panel layouts, selected by mode.** `default_configuration.sasi` no
  longer pins a theme. `sofi_surface_name()` derives the surface from the invocation and that one
  answer drives both the built-in layout and the instance lock. Parsed after the default
  configuration and before every user source, so `~/.config/sofi` and `-theme` still override.
  **No config file is required for any of the four surfaces.**

- **R17 — Instance locks are per surface, not per process.** A single session-wide pidfile made
  the panels mutually exclusive; the failure was a `g_warning` on a stderr nobody reads. Locks are
  now `sofi-<surface>.pid`. Verified: launcher and task strip render simultaneously with no flags.

- **R18 — Bind `zwlr_layer_shell_v1` at version 4, and expose keyboard interactivity.**
  `ON_DEMAND` arrived in v4; below it wlroots coerces the argument to `!!interactive`, so
  requesting it on a v1 binding silently means EXCLUSIVE. `wl_registry_bind` still takes
  `MIN(advertised, 4)`, so a v1-only compositor is unaffected. New option
  `-wayland-keyboard-interactivity none|exclusive|on-demand`, defaulting to `exclusive`.

- **R19 — Sheet control travels over a hikari control socket, not a Wayland protocol.**
  Chosen by the USER from three options. `ext_workspace_v1` was rejected because it is larger on
  both sides *and still cannot express send-to-sheet*; virtual-keyboard synthesis was rejected
  because it cannot read state and breaks on any rebinding. See the Q17 closure below.

- **R20 — The notification daemon is built in sofi, not in the compositor.** Ruled by the USER
  and confirmed by analysis. The decisive argument is hikari's own, already recorded in
  `src/topbar.c` about its own bar: *"This deliberately remains a SEPARATE PROCESS […] running
  them inside the compositor would stall the Wayland event loop on every tick."* Notifications are
  a harder case than telemetry — arbitrary applications, on their schedule, with a raw pixel array
  whose dimensions the sender chooses. A crash there would take the session, and hikari has no
  crash recovery.

  **The one argument that could have forced compositor-side does not survive the code.**
  Notifications painting over a locked screen would be a real security problem a client cannot fix
  itself — but hikari `src/lock_mode.c:988-992` disables the `bottom`, `views`, `top` and
  `overlay` scene nodes on lock. A sofi notification is on `overlay`, so it is hidden for the
  duration with no work on either side.

- **R21 — Notification history is a ring buffer from day one.** The USER wants history.
  Retrofitting it onto a transient queue would mean rewriting every accessor; a fixed ring with a
  `live` flag costs almost nothing now. Live entries feed the banner, the full ring feeds a
  separate `notification-history` mode.

- **R22 — Notifications render bottom-right.** hikari's own bar owns the top edge and the sheet
  pane owns the right edge, making top-right the busiest corner on screen. The stack grows upward
  from above the task strip.

- **R23 — `urgency=2` never expires.** Critical notifications ignore both `expire_timeout` and the
  server default, persisting until dismissed or closed by the sender. Consequence to design for:
  this is the one case where the banner surface is up indefinitely, which is why R24's forced
  settings are load-bearing rather than a nicety.

- **R24 — The daemon's two safety settings are forced in code, not read from a theme.**
  `click-to-exit: false` and `keyboard-interactivity: on-demand` are overridden after theme parsing
  in daemon mode. Both, wrong, make the desktop unusable for the entire session, and a user editing
  their own theme must not be able to cause that.

### Q17 — CLOSED: send-to-sheet is expressible after all

Q17 asked how send-to-workspace should be expressed given that no standards-track protocol
carries it. **R19's control socket answers it.** `pin <n>` moves the focused view to a sheet, and
it is verified working: with one window on sheet 5, `pin 8` moved the count from index 5 to index
8, and a subsequent `sheet 8` switched to it.

This is the strongest argument for the socket over `ext_workspace_v1`, whose `assign` moves a
workspace to an output group rather than a window to a workspace.

### Q16 — still open

Deleting the ext-foreign-toplevel binding and `window-command`'s `{window}` support remains
unruled. Nothing this session depended on it. Note it has become slightly *less* attractive to
delete: the ext list is the only source of a stable per-window identifier, which a notification
daemon correlating windows to notifications might eventually want.

### Defects found and fixed this session

| Defect | Location | Consequence |
|---|---|---|
| `-e` never armed the auto-dismiss timer | `view.c` — `sofi_view_error_dialog` was not among the two callers of `sofi_view_set_user_timeout` | Message surfaces never self-dismissed. `timeout { delay: N; }` appeared to do nothing |
| Layer-shell bound at v1 | `include/wayland-internal.h:157` | Four versions of protocol left unused; `ON_DEMAND` unreachable |
| Keyboard interactivity hardcoded | `wayland/display.c` | Every surface took the keyboard exclusively, with no way to opt out |
| Wayland-inappropriate `window-format` default | `config/config.c:152` | `{w}` is X11-only; four dead leading spaces per row, and `{c}` alone renders sibling windows identically |
| Minimised bit parsed and discarded | `modes/wayland-window.c` | Sheet visibility was arriving and being thrown away |

### Defect found and NOT fixed — belongs to the compositor

**`click-to-exit` grows the layer surface to cover the whole output** (`wayland/view.c:262`) so it
can catch clicks outside the menu. Correct for a menu. For a resident daemon it is an invisible
full-screen input trap. This is not a bug — it is the mechanism working as designed — but it is
the single most dangerous interaction in the notification work, and it is why R24 exists.

Separately, two pre-existing hikari-sakura issues were hit and are recorded for the USER rather
than fixed silently:

1. **hikari does not build at HEAD with its default flags.** `action.o` was stale from a
   pre-`NDEBUG` build and `main.o` was root-owned from an earlier `sudo make`. The Makefile has no
   header dependency tracking, so this recurs after any header edit. `make clean` is mandatory.
2. **`hikari_server_stop()` appears not to run on SIGTERM.** The control socket survived a
   SIGTERM that should have unlinked it, which suggests `wl_event_loop_add_signal` is not catching
   the signal on FreeBSD and the whole graceful-shutdown chain is skipped. Unconfirmed. Harmless
   for the socket because startup unlinks a stale node, but it affects every other teardown step.

---

## 2026-08-22 21:35 — Target platform named: hikari-sakura. Phase 7 rescoped.

The USER supplied a compositor-side analysis of `hikari-sakura`
(`/home/orpheus497/Projects/hikari-sakura`), their custom wlroots compositor, and confirmed
**sofi's target is specifically that compositor.** This materially changes Phase 7.

### Corrections to my earlier assessment

**I was right that control is missing, wrong about the significance of one detail.** The
compositor brief warns that once `zwlr_foreign_toplevel_management_v1` is advertised
alongside `ext-foreign-toplevel-list-v1`, a client binding both globals sees every window
twice, and advises "sofi should bind zwlr only."

**Verified: sofi is already immune.** It displays from `pd->wlr_toplevels` only
(`source/modes/wayland-window.c:574`). The ext list is a *separate* list used for exactly one
purpose — harvesting the `identifier` string to fill `{window}` in `window-command`
(`:592-598`). No double-listing exists. No change required on that account.

### The real sofi-side finding underneath it

That ext correlation is the fragile app_id-ordering heuristic the code itself warns about at
`source/modes/wayland-window.c:581-588`, and which `AUDIT_REGISTER.md` flagged. Its **only**
consumer is `window-command`, whose default value is `wmctrl -i -R {window}` —
X11-only, and therefore already non-functional on hikari.

So roughly 90 lines exist to serve a feature that does not work on the target platform, at
the cost of a documented-fragile heuristic. **Recommendation: delete the ext binding and the
correlation.** Not done — it is a user-visible feature removal and needs a ruling (Q16).

### Send-to-workspace is not expressible — verified against the protocol XML

R15 defined the task manager as close / minimise / maximise / **send-to-workspace**. That
last one cannot be built on the protocols in question:

- `zwlr-foreign-toplevel-management-unstable-v1.xml` — **zero** occurrences of "workspace".
- `ext-workspace-v1.xml` request set is `commit`, `stop`, `create_workspace`, `destroy`,
  `activate`, `deactivate`, `assign`, `remove`. **`assign` moves a workspace to an output
  group, not a window to a workspace.**

No standards-track protocol expresses it. It needs a hikari-specific protocol or a CLI
escape hatch — and that decision should be made *before* compositor Part B is built, since it
may change what Part B needs to expose. Raised as **Q17**.

### What sofi actually needs, per feature

| Feature | Compositor | sofi work |
|---|---|---|
| Window switcher | Part A (done, unrun) | **none** — sofi already sends `activate` + `close`, both v1 requests, and binds `MIN(version, 3)` so it degrades gracefully |
| Task manager (R15) | Part A | **real work** — sofi sends *only* `activate` and `close`; `set_minimized` / `set_maximized` / `set_fullscreen` appear nowhere in the source. They must be added as UI actions |
| Workspace switcher | Part B (not started) | **a new mode from nothing**, ~300 lines |
| Send-to-workspace | **no protocol exists** | blocked on Q17 |

### Consequence of the compositor's fullscreen mapping

hikari has no fullscreen state, only `HIKARI_MAXIMIZATION_*`, so `set_fullscreen` and
`set_maximized` are the same operation and a client sees back a state it did not request.
**sofi should expose maximise only** — two actions doing one thing, with confusing state
echo, is worse than one. Recorded so the task-manager UI is not designed around a distinction
that does not exist on the target.

### Open questions

- **Q16** — delete the ext-foreign-toplevel binding and the `window-command` `{window}`
  identifier support? Recommended yes; it is a feature removal.
- **Q17** — how should send-to-workspace be expressed: hikari-specific protocol, or a CLI
  escape hatch through `window-command`?

---

## 2026-08-22 19:55 — RULING R15: "task manager" means task/window manager

**Ruled.** Reading (2) of Q15: a richer window mode with close / minimise / maximise /
send-to-workspace actions — **not** a process manager.

Consequences:
- No new platform-specific data source. No `kvm_getprocs`, no `/proc`, no privilege story
  for signalling processes. The tree's clean portability record (zero `/proc` and zero
  `__FreeBSD__` dependencies outside the new `SHM_ANON` guard) is preserved.
- Phase 7c collapses into an extension of 7a (window mode) and 7b (workspace mode) rather
  than a third independent mode. It should be sequenced *after* both.
- On Wayland the actions map to `zwlr_foreign_toplevel_handle_v1` requests
  (`activate`, `close`, `set_maximized`, `set_minimized`, `set_fullscreen`), most of which
  the existing `source/modes/wayland-window.c` already binds but does not expose. On X11
  they map to EWMH client messages.
- Send-to-workspace depends on 7b landing first, on both backends.

**All 15 questions are now ruled. No decisions outstanding.**

---

## 2026-08-22 19:46 — New scope from USER: default config + three modes

The USER added `sofi-config/` (`config.rasi`, `colors-default.rasi`) as the standard,
deployed default config and theme, and asked that enhancements for a task manager, window
switcher and workspace switcher be captured in planning rather than built now.

Recorded as `PLANS.md` **Phase 6** (ship the default config) and **Phase 7** (the three
modes). Backlog items B8 and B9 in `TODOS.md`.

**Validation performed immediately:** the config parses against the real binary with zero
warnings — `rofi -config sofi-config/config.rasi -dump-theme` exits 0, stderr empty, the
`@import` resolves and every property lands.

### Two findings that affect existing decisions

**R3 has an exception this file creates.** `sofi-config/config.rasi:15` is
`@import "colors-default.rasi"` — the extension is written out explicitly. The shipped
gruvbox themes use extension-less `@import "gruvbox-common"`, which resolves through
`rasi_theme_file_extensions[]`, which is why R3 was assessed as needing no importer edits.
That assessment holds for `themes/` but **not** for `sofi-config/`: this import line must be
edited by hand during Phase 3e or the default theme breaks. Added to the Phase 3e checklist.

**`modi:` is deprecated but live.** `sofi-config/config.rasi:7` uses `modi:`. It still works
— `source/xrmoptions.c:74-86` maps `switchers`, `modi` and `modes` to the same
`config.modes` — but the shipped default should use `modes:`. Not a bug, a freshness issue.

### Q15 — OPEN: what does "task manager" mean?

The term admits two readings with very different designs:

1. **Process manager** (enumerate processes, CPU/memory, send signals). Requires a new
   platform-specific data source — `kvm_getprocs` on FreeBSD, `/proc` on Linux. Note the
   audit established the tree currently has **zero** `/proc` or `__FreeBSD__` dependencies;
   this would introduce the first significant platform split, cutting against the project's
   otherwise clean portability record.
2. **Task/window manager** (window mode plus close / minimise / maximise / send-to-workspace
   actions). Mostly an extension of the window and workspace modes, no new data source.

Reading (2) as the likely intent, since it was named alongside a window switcher and a
workspace switcher. **Not assumed — must be confirmed before design work begins.**

### Feasibility confirmed while scoping

`ext-workspace-v1` is present on this host (wayland-protocols 1.49,
`/usr/local/share/wayland-protocols/staging/ext-workspace/`), so a workspace switcher is
viable on Wayland as well as X11. It is not yet in the `meson.build:317-327` protocol list.
On X11 the EWMH groundwork is already partly in use at `source/modes/window.c:559,796`.

---

## 2026-08-22 19:15 — RULINGS: Q3, Q5, Q6, Q9, Q11, Q12 decided by USER

All remaining Phase 3 gates are now closed. Phase 3 is unblocked.

### R3 (was Q3) — Rename the theme extension to `.sasi` / `.sasinc`

**Ruled.** Full consistency with the hard-fork stance: the format extension is rebranded too.

Scope:
- `lexer/theme-lexer.l:56` — `{".rasi", ".rasinc", NULL}` → `{".sasi", ".sasinc", NULL}`
- 35 shipped theme files renamed (`themes/*.rasi`, `themes/gruvbox-common.rasinc`)
- `meson.build:378-415` install list
- `doc/default_theme.rasi`, `doc/default_configuration.rasi`
- `config.rasi` → `config.sasi` (`source/rofi.c:1059`, `:1143-1145`)
- `rofi.rasi` → `sofi.sasi` (`source/rofi.c:1120`, `:1132`)
- Docs: `doc/rofi-theme.5.markdown`, `CONFIG.md`, `README.md`, the theme-selector script

**Useful finding:** the six gruvbox variants use extension-less `@import "gruvbox-common"`
(`themes/gruvbox-*.rasi:61`), which resolves through the extension array. Renaming
`gruvbox-common.rasinc` → `.sasinc` therefore needs **no** edit to the importers.

Consequence accepted: every third-party rofi theme in existence requires a file rename before
it can be used with sofi. This is loud (file not found), not silent, which makes it more
tolerable than R2.

### R5 (was Q5) — Compositor identity renamed

**Ruled.** Wayland layer-surface namespace `"rofi"` → `"sofi"`
(`source/wayland/display.c:1792`). X11 WM_CLASS `"rofi\0Rofi"` → `"sofi\0Sofi"`
(`source/xcb/view.c:773-775`). Must match `StartupWMClass` in the renamed desktop file.

Consequence: users with compositor/WM rules keyed on `rofi` must update them. Loud, and
belongs in the release notes.

### R6 (was Q6) — Helper scripts renamed, no compat symlinks

**Ruled.** `rofi-sensible-terminal` → `sofi-sensible-terminal`, `rofi-theme-selector` →
`sofi-theme-selector`. No compatibility symlinks.

**Hard constraint:** `config/config.c:65` (the compiled-in default terminal) must change in
the *same commit* as the script rename, or `sofi -show run` cannot open a terminal at all.

### R9 (was Q9) — pkg-config and plugin dir renamed, no alias

**Ruled.** `sofi.pc`, `Name: sofi`, `pluginsdir=${libdir}/sofi`. No `rofi.pc` alias.
Sites: `meson.build:417-428` (hardcoded — does *not* follow a `project()` rename),
`pkgconfig/rofi.pc.in`.

### R11 (was Q11) — xdg-shell fallback IS in scope, as Phase 2b

**Ruled.** Add an xdg-shell fallback so sofi runs on compositors without
`zwlr_layer_shell_v1` — Mutter (GNOME) and KWin (Plasma).

This is the largest single functional gain available: it takes sofi from wlroots-only
(sway, Hyprland, river) to broadly usable. `xdg-shell.xml` is already compiled
(`meson.build:318`) but entirely unused by the backend.

Sequenced *after* Phase 2a (the `g_error` abort fix), because 2a is what makes the
unsupported case reachable in the first place. Estimated 2–3 days.

### R12 (was Q12) — `AUTHORS`, `CODE_OF_CONDUCT.md`, `releasenotes/` stay deleted

**Ruled.** The working-tree deletions are deliberate and are kept.

**Attribution is instead carried by `COPYING` and `README.md`.** Verified at ruling time:
`COPYING` is present and unmodified, and already contains both required notices —
`Copyright (c) 2012 Sean Pringle` and `Modified 2013-2024 Qball Cow`. That satisfies the MIT
obligation on its own.

Required follow-up so provenance is not weakened by the loss of `AUTHORS`:
- `README.md` must carry an explicit "sofi is a hard fork of rofi by Dave Davenport (Qball),
  originally derived from simpleswitcher by Sean Pringle, with Wayland support by lbonn"
  statement.
- The 78 per-file copyright notices across 73 files in `source/`, `include/`, `lexer/`,
  `config/` must survive Phase 3c untouched. This is now the *primary* attribution mechanism,
  which raises the severity of risk RR4 from Medium to High.

### Still open

Q7 (partially resolved by R12 — `releasenotes/` deleted; `mkdocs/docs/1.7.*` and `Changelog`
still undecided), Q13 (`.gitlab-ci.yml` delete vs rewrite), Q14 (CI target platforms).
None gate Phase 3.

---

## 2026-08-22 19:02 — RULINGS: Q1, Q2, Q4, Q10 decided by USER

The USER selected the clean-break option on every compatibility question. The governing
principle, now established for all downstream work:

> **sofi is a hard fork with its own identity, not a drop-in rofi replacement.**
> No compatibility shims, no dual-name exports, no fallback path lookups.

This resolves a large amount of design ambiguity and *removes* work from the plan. It also
means the first release must carry an unambiguous "this is not rofi and will not read your
rofi configuration" statement.

### R1 (was Q1) — Plugin ABI: rename everything, no shim

**Ruled.** Rename all 335 `rofi_*` / `ROFI_*` / `Rofi*` identifiers to `sofi_*` / `SOFI_*` /
`Sofi*`. No compatibility header. No `rofi.pc` alias.

Consequences accepted:
- Every out-of-tree rofi plugin breaks at both source and binary level with no migration path.
- `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted relative
  path and must be edited when those files are renamed.
- `ABI_VERSION` at `include/mode.h:36` is still bumped from 7, so a stale plugin fails
  loudly at `source/rofi.c:648` rather than crashing.

### R2 (was Q2) — On-disk paths: hard break, no fallback

**Ruled.** `$XDG_CONFIG_HOME/sofi/config.rasi`, `sofi.rasi` system-wide, `sofi/themes/`,
`sofi/scripts/`, and sofi-named cache files. No fallback read of any `rofi/` path.

Sites: `source/rofi.c:1059`, `:1120`, `:1132`; `source/helper.c:1474`, `:1483`, `:1493`,
`:1509`; `source/modes/script.c:630`, `:640`; `source/modes/drun.c:66`, `:69`;
`source/modes/run.c:64`; `source/modes/ssh.c:83`; filebrowser cache.

Consequence accepted: an existing rofi user launching sofi gets the built-in default theme
and empty history, with no diagnostic pointing at their old config. **This must be stated
prominently in the README and first release notes** — it is the single most likely source of
"sofi is broken" reports.

### R4 (was Q4) — Script env vars: `SOFI_*` only, hard break

**Ruled.** `SOFI_RETV`, `SOFI_OUTSIDE`, `SOFI_INFO`, `SOFI_DATA`, `SOFI_INPUT`,
`SOFI_PLUGIN_PATH`, `SOFI_PNG_OUTPUT`. The `ROFI_*` names are not exported.

Sites: `source/modes/script.c:216-231`; `source/rofi.c:705`, `:991`; `source/view.c:138`;
docs at `doc/rofi-script.5.markdown:55,65,70,74`.

Consequence accepted: every existing third-party script mode fails *silently* — it reads an
unset variable rather than erroring. Because the failure is silent, the script-mode manpage
must document the rename explicitly at the top, and the sofi script protocol should be
presented as its own contract rather than as a delta from rofi's.

### R10 (was Q10) — glib is a permitted dependency

**Ruled.** `glib-2.0`, `gio-2.0`, `gmodule-2.0` and `gdk-pixbuf-2.0` (all LGPL-2.1+) join
`pango` and `cairo` as permitted copyleft exceptions under the `AGENTS.MD` FOSS rule.

Rationale: inherited from upstream, used in essentially every file, and present in the
public header types themselves. Removing it would be a rewrite, not a rebrand. `AGENTS.MD`
should be amended to record the widened exception so the rule and the codebase agree.

### Constraint that these rulings do NOT override

**MIT attribution is a license obligation, not a compatibility preference.** The clean-break
decision applies to identifiers, paths and protocols — not to copyright notices. Regardless
of R1/R2/R4:

- `COPYING` remains unmodified with the original copyright lines intact.
- The per-file MIT headers in `source/` and `include/` must **not** be rewritten by the
  identifier rename. The bulk substitution in Phase 3c must exclude comment blocks
  containing a copyright line, or it will silently alter the license notices.
- `AUTHORS` is retained. (It currently shows as deleted in the working tree — see Q12.)
- Fork provenance credit to Sean Pringle, Dave Davenport / Qball, and lbonn is preserved.

---

## 2026-08-22 18:55 — Open questions raised by the initial audit

**Superseded entries:** Q1 → R1, Q2 → R2, Q4 → R4, Q10 → R10 above. Retained below for the
reasoning that informed each ruling.

Nine rulings are required from the USER before any renaming begins. Each is a compatibility
decision, not a style preference: the answer determines whether an existing rofi user's
configuration, themes, plugins or script modes survive the transition. No default has been
assumed. A recommendation is given for each, with the reasoning.

### Q1 — Public C API/ABI: rename `rofi_*` / `ROFI_*` / `Rofi*` symbols?

**Scope.** 335 distinct identifiers. Five installed headers form the plugin contract:
`include/mode.h`, `include/mode-private.h`, `include/helper.h`, `include/rofi-types.h`,
`include/rofi-icon-fetcher.h` (`meson.build:195-203`), installed to
`$includedir/<project_name>/`.

**Options.**
- (a) Rename everything to `sofi_*`. Clean, self-consistent, breaks every out-of-tree plugin
  at both source and binary level.
- (b) Keep `rofi_*` internally, rename only the user-visible identity. Zero plugin breakage,
  but the codebase permanently reads as rofi and the rebrand is cosmetic.
- (c) Rename to `sofi_*` and ship a compatibility header of `#define rofi_x sofi_x` plus a
  `rofi.pc` that `Requires: sofi`. Source-compatible for plugins that recompile; still
  breaks already-compiled plugin binaries (which the plugin-dir move breaks regardless).

**Recommendation: (c).** The plugin dir moves from `$libdir/rofi` to `$libdir/sofi`
regardless, so already-installed plugin binaries stop being found no matter what is chosen —
binary compatibility is already lost. Given that, source compatibility is the only thing
worth preserving, and a shim header buys it cheaply. Bump `ABI_VERSION` at
`include/mode.h:36` from 7 so a stale plugin fails loudly at `source/rofi.c:648` instead of
crashing.

**Note.** `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted
relative path, so renaming that file requires editing those two lines.

### Q2 — On-disk config and cache paths: migrate, fall back, or hard break?

**Scope.** `$XDG_CONFIG_HOME/rofi/config.rasi` (`source/rofi.c:1059`);
`rofi.rasi` in `$XDG_CONFIG_DIRS` and `$SYSCONFDIR` (`source/rofi.c:1120`, `:1132`);
the four theme search roots (`source/helper.c:1474`, `:1483`, `:1493`, `:1509`);
`$XDG_CONFIG_HOME/rofi/scripts/` (`source/modes/script.c:630`, `:640`);
and five cache files — `rofi3.druncache` (`source/modes/drun.c:66`),
`rofi-drun-desktop.cache` (`:69`), `rofi-4.runcache` (`source/modes/run.c:64`),
`rofi-2.sshcache` (`source/modes/ssh.c:83`), `rofi3.filebrowsercache`.

**Recommendation: search `sofi/` first, fall back to reading `rofi/` when the sofi path is
absent; always write to `sofi/`.** This is the single highest-blast-radius surface in the
project. A hard break is silent — no error, the user just gets the default theme and an empty
history and has to work out why. Caches are regenerable and can hard-break; config and themes
must not. Log at `g_debug` level when a fallback path is used.

### Q3 — The `.rasi` / `.rasinc` extension: keep or rename?

**Scope.** `lexer/theme-lexer.l:56` (`rasi_theme_file_extensions[]`), consumed at
`lexer/theme-lexer.l:437` and `source/rofi.c:1143-1145`; 33 shipped theme files.

**Recommendation: keep `.rasi` and `.rasinc` unchanged.** RASI is the *format* name, not the
product name. Tens of thousands of user configs and every third-party theme repository use
it. Renaming invalidates the entire community theme ecosystem and buys nothing. If a native
suffix is wanted later, *append* to the array rather than replacing:
`{".rasi", ".rasinc", ".sasi", ".sasinc", NULL}`.

Do **not** rename the shipped theme files either — `@theme "solarized"` in a user config
resolves by basename.

### Q4 — Script-mode environment variables: `ROFI_*` → `SOFI_*`?

**Scope.** `ROFI_RETV`, `ROFI_OUTSIDE`, `ROFI_INFO`, `ROFI_DATA`, `ROFI_INPUT`
(`source/modes/script.c:216-231`), plus `ROFI_PLUGIN_PATH` (`source/rofi.c:705`),
`ROFI_OUTSIDE` read back at `source/rofi.c:991`, and `ROFI_PNG_OUTPUT` (`source/view.c:138`).
Documented at `doc/rofi-script.5.markdown:55,65,70,74`.

**Recommendation: export BOTH names for at least one release; read both, preferring `SOFI_*`.**
This is a documented public protocol. Every script mode ever written reads `ROFI_RETV`.
Renaming alone doesn't produce an error — the script reads an unset variable and
misbehaves silently, which is the worst possible failure mode. Exporting both costs five
extra `g_environ_setenv` calls.

### Q5 — Wayland layer-surface namespace and X11 WM_CLASS?

**Scope.** `source/wayland/display.c:1792` (namespace `"rofi"`);
`source/xcb/view.c:773-775` (`wm_class_name[] = "rofi\0Rofi"`).

**Recommendation: rename to `sofi` / `"sofi\0Sofi"`.** These are the compositor-visible
identity and must match `StartupWMClass` in the desktop file. Users with compositor rules
keyed on `rofi` will need to update them — that is unavoidable and belongs in the release
notes, but it is loud rather than silent, so it is acceptable.

### Q6 — Helper scripts `rofi-sensible-terminal` / `rofi-theme-selector`?

**Scope.** `script/rofi-sensible-terminal`, `script/rofi-theme-selector`, installed at
`meson.build:204-208`; the compiled-in default at `config/config.c:65`.

**Recommendation: rename, and change `config/config.c:65` in the same commit.** If the
default terminal string and the installed script name diverge, `sofi -show run` cannot open a
terminal at all. Ship `rofi-sensible-terminal` as a compatibility symlink only if the USER
wants existing user configs containing `terminal: "rofi-sensible-terminal";` to keep working.

### Q7 — Frozen historical content: rebrand, keep verbatim, or delete?

**Scope.** `mkdocs/docs/1.7.0` … `1.7.9` and `2.0.0` (2,690 occurrences), `releasenotes/`
(210), `Changelog` (22).

**Recommendation: leave all of it verbatim and stop shipping it.** These document releases of
*rofi* that sofi never made. Rewriting them would fabricate a history. Keep the files for
provenance, exclude them from the docs site and the dist tarball. Note the git status shows
many of these already deleted in the working tree — that deletion should be a deliberate,
recorded decision rather than incidental.

### Q8 — MIT attribution: what must survive?

**Scope.** `COPYING`, `AUTHORS`, `README.md:31-41`, and the per-file MIT headers
(`Copyright © 2013-2020 Qball Cow <qball@gmpclient.org>` etc.) in essentially every source
file.

**Ruling required, but the license constrains the answer.** The MIT license requires the
copyright notice and permission notice to be retained in all copies. Concretely:
- `COPYING` must remain, unmodified, with the original copyright lines intact.
- The per-file MIT headers must **not** be stripped or reassigned during the rename. A
  bulk `rofi` → `sofi` substitution across `source/` and `include/` will otherwise rewrite
  the word "rofi" inside those headers — that must be excluded explicitly.
- `AUTHORS` must be retained; new contributors may be appended.
- `README.md` credit to Sean Pringle (simpleswitcher), Dave Davenport / Qball, and lbonn
  (Wayland) should be preserved as fork provenance.

Adding a "sofi is a fork of rofi by …" line is the recommended framing.

### Q9 — `pkg-config` module name and plugin directory?

**Scope.** `meson.build:417-428` (hardcoded `filebase: 'rofi'`, `name: 'rofi'` — these do
**not** follow a `project()` rename), `pkgconfig/rofi.pc.in`, `$libdir/rofi`.

**Recommendation: rename to `sofi.pc` / `$libdir/sofi`, and additionally ship a `rofi.pc`
containing `Requires: sofi`** so existing plugin build systems doing `dependency('rofi')`
keep configuring. Note this is one of exactly three places that will *not* follow a
`meson.build:1` rename automatically — the others are `executable('rofi')` at
`meson.build:367` and every literal `rofi*` filename in the install lists.

---

## 2026-08-22 16:40 — Tooling deviation from COMMAND LAWS (recorded for transparency)

`AGENTS.MD` requires IDE-native tooling over shell commands when running inside an IDE. This
session runs in the VSCode extension, but the harness exposes no native search tool (no
Grep/Glob equivalent is available). Read-only inspection of a 42k-line tree therefore used
`git grep` / `sed -n` via the shell, which was the only mechanism capable of the task. File
creation and editing used the native Write/Edit tools as required. Flagged so the USER can
rule on whether this deviation is acceptable going forward.
