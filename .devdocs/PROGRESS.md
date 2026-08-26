# PROGRESS

Macro progress tracking. Completed, superseded, removed and archived items.
Most recent at the top.

---

## 2026-08-26 10:37 — B4 delivered: the icon decoder. R42 supersedes B4.0

Tasks `TODOS.md` B4.1–B4.3. Decision `DECISIONS_LOG.md` R42. **19/19 tests, clean build.**

The one part of the tray taking genuinely hostile input: the sender chooses every dimension and
supplies the byte array. Two conversions are mandatory and neither is visible in a type check — the
wire format is ARGB32 in **network byte order** (so it must be reassembled, not copied) and carries
**straight** alpha where cairo wants **premultiplied** (so copying gives every anti-aliased icon
edge a bright halo).

### R42: bounded rather than threaded — a reversal, recorded as one

R41 required the decode to run in the worker threadpool. It does not. The threading requirement came
from 16 million pixels of worst-case work, and that figure came from the 4096px cap inherited from
`image_from_hint()` — right for a notification image, wrong for a tray icon, which is realistically
16–64px. Capped at **512** the worst case is ~1ms, and R41 had already bounded the blast radius to
the tray's own process. Threading would have added cancellation, marshalling and a second failure
mode to defend a cost a constant removes.

Decoding is also **lazy**: it runs when an icon is first wanted after changing, not when an
application announces a change. That removes the cost that was actually likely — a chatty applet
repainting while the strip is off screen now costs nothing.

### Verified

| Case | Result |
|---|---|
| Wire `FF,FF,00,00` | `0xFFFF0000` — byte order right |
| Wire `80,FF,00,00` | `0x80800000`, not `0x80FF0000` — premultiplied |
| 8×8 declared, 4 bytes given | refused before reading a pixel |
| 9999×9999 declared | refused on dimension, not clamped |
| Sizes 16/64/32 out of order | 32 chosen |
| One bad entry beside a good one | 2 refusals, good entry decoded |

**A gate defect found and fixed in the same pass.** The first version asserted with a grep the
*initial* state also matched, so it reported a pass for a case that had never run — and the debounce
had coalesced that change with the next, so no such decode existed. Assertions now test the latest
decode rather than any matching line. Recorded because a false pass is worse than a failure.

---

## 2026-08-26 10:31 — R41: the tray becomes its own daemon

Tasks `TODOS.md` B3.5–B3.7. Decision `DECISIONS_LOG.md` R41. **19/19 tests, clean build.**

USER rejected hosting the tray inside the notification daemon. The tray is now `sofi -tray-daemon`,
its own process, dispatched in `main()` **before display selection** — StatusNotifierItem is D-Bus
and nothing else, so requiring a Wayland session to run it would be a dependency invented rather
than inherited.

### Three hazards the challenge surfaced, and what happened to each

| Hazard | Outcome |
|---|---|
| B4's pixmap decode was heading for the main loop — 16M pixels of scalar work at the dimension cap, in the loop that also drew the banner | Not yet built; recorded as **B4.0**, must go in the existing worker threadpool |
| Every `New*` signal triggered a full `GetAll`; "items don't spam" was assumed and never checked | **Fixed** — 100ms debounce. Visible in the gate timings: 04.710 → 04.817 → 04.920 |
| `g_bus_get_sync()` in the item constructor, in a path registering applications drive | **Fixed** — the connection is passed in from the watcher |

Two of those would have been equally wrong in a separate process. What the shared loop added was
that they cost *notifications* rather than only the tray. The argument that stands on its own is
shared fate, and it is USER's: tray code parses hostile input from arbitrary applications,
notifications matter more, and one main loop means one crash takes both.

**The split cost ten lines**, because `tray-watcher.c` and `tray-item.c` had never included anything
but `gio` and each other. That is simultaneously why the original arrangement was defensible and why
USER's point about tech debt lands: it was cheap *this* week.

### Measured

With `WAYLAND_DISPLAY` and `DISPLAY` both unset: watcher name taken in **6 polls** against 364
through the display path, item registered, properties read, `RegisteredStatusNotifierItems`
populated. The A2 notification gate still passes untouched.

The B3 gate passes against `-tray-daemon` for registration, the attention override, the title
re-fetch and the reap. Its "back to normal" step is **inconclusive in the post-split run**: the
assertion greps for `status=1` and `tail -1`, which matched the *initial* fetch rather than the
revert, because the debounce moved the revert past the poll. It passed cleanly in the pre-split run
of the same gate, so this is harness imprecision rather than a regression — but it was not re-proved
after the split and is recorded as such rather than counted.

**Consequence, accepted rather than hidden:** autostart now needs two lines, `-notification-daemon`
and `-tray-daemon`. User-facing documentation of the new flag is held for B9 with the rest, so it
describes what shipped; `-h` already lists it.

---

## 2026-08-26 10:23 — B3 delivered: sofi reads what tray items look like

Tasks `TODOS.md` B3.1–B3.4. **19/19 tests, clean build.** Uncommitted.

`source/tray-item.c` + `include/tray-item.h`. The watcher knows which items exist; this knows what
they are. The split matters because the two fail differently — an item that registered and then
stopped answering is still registered, and its icon simply stops changing.

### Three decisions worth keeping

**Everything is asynchronous, and that is load-bearing rather than stylistic.** These are calls to
arbitrary third-party applications, made from the process that also serves notifications. A
synchronous `GetAll` against a wedged application would stall the daemon for the D-Bus timeout —
precisely the failure hikari's own `src/topbar.c` was made a separate process to avoid (F12). Calls
also carry a 3s timeout rather than the 25s default: a tray icon three seconds stale is not a
problem, one that never resolves is.

**Every change signal triggers a full re-fetch.** StatusNotifierItem's change signals carry no
payload — `NewIcon` says an icon changed and nothing else — so a fetch is required whatever arrives.
Mapping signal to property would mean encoding which signal implies which properties, and real items
disagree: `NewStatus` routinely accompanies a changed icon, because the attention icon *is* a
different property. Notably these are **not** `PropertiesChanged`, which most items never emit at
all — which is why a `GDBusProxy` property cache is useless here and this file keeps its own state.

**Icon precedence is resolved once, in the accessor.** `NeedsAttention` → attention icon, else the
ordinary one; same for pixmaps. It is a property of the specification, not of any caller, so
resolving it at each drawing site would be the same rule written three times.

### Measured against a real item

`fake-sni.c` (session scratchpad) is a genuine StatusNotifierItem: owns a name, exports the
interface, serves properties, emits the signals, takes commands on stdin. gdbus cannot do this job —
it calls methods but cannot export properties or emit signals, which is most of what an item is.

| Step | Observed |
|---|---|
| Registration | `id='fake-item' title='Fake Tray Item' icon='network-wireless' status=1 menu='/MenuBar' is-menu=1 theme-path='/opt/fake/icons' pixmap=yes` |
| `NewStatus` → NeedsAttention | `icon='dialog-warning' status=2` — **the attention override taking effect** |
| `NewTitle` | `title='Renamed By Test'` |
| back to Active | `icon='network-wireless' status=1` |
| application exits | `Tray item gone: ... (0 left)` |

Every re-fetch landed within ~5ms of the signal, so nothing blocked.

### Not exercised, and stated rather than implied

- **The per-property fallback** for an item whose `GetAll` fails. Written because a single throwing
  property takes a whole batch down and an item can be usable while answering nothing to a bulk
  request — but no fixture provokes it, so it is defensive code that has never run.
- **`Activate` / `SecondaryActivate`.** Implemented, but nothing can call them until B5 exposes the
  tray over `org.sofi.Tray`. B5's gate covers it; the fixture already prints every call it receives.
- **Title falling back to Id** when an application sets no title. The fixture always sets one.

---

## 2026-08-26 10:12 — B1 and B2 delivered: sofi is the session's StatusNotifierWatcher

Tasks `TODOS.md` B1.1, B2.1–B2.5. **19/19 tests, clean build.** Uncommitted.

### Delivered

| Area | Outcome |
|---|---|
| `box_remove_all()` | The one widget-layer change the tray needs (F16). Frees every child, empties the box, updates the parent. Header states the caller obligation: the view's borrowed `mouse.motion_target` must be cleared first or rebuilding a zone under the cursor leaves it dangling |
| `source/tray-watcher.c` | Owns `org.kde.StatusNotifierWatcher` and `org.kde.StatusNotifierHost-<pid>`; serves both Register methods, all three properties and all four signals; reaps vanished items |
| `tray` meson option | New, defaults on, maps to `SYSTEM_TRAY`. **Errors at configure time when `notify` is off** — a tray host outside the daemon is not a configuration, it is a tray that is always empty |
| Wiring | Started in `startup()` beside the notification service, stopped in `teardown()`. Its failure is warned about and non-fatal: notifications are a separate name and carry on |

### Three things the implementation had to get right

1. **`IsStatusNotifierHostRegistered` must answer TRUE.** Applications ask once, at their own
   startup, and one that gets FALSE shows no icon and never asks again. This single property is the
   difference between a tray and an empty strip.
2. **Both registration forms.** `RegisterStatusNotifierItem`'s argument is a bus name from Qt/KDE
   items and an object path — with the bus name implied by the sender — from several GTK and
   Electron ones. The specification never pinned it down, and a watcher handling one form shows an
   empty tray for half the desktop with no diagnostic.
3. **Reaping by name watch.** There is **no Unregister method in the specification at all**. An item
   exists exactly as long as its bus name does, so `g_bus_watch_name` per item *is* the
   deregistration mechanism, not a fallback for badly-behaved applications.

Deliberately **not** `REPLACE` on the watcher name, unlike the notification service: two trays
fighting over it would flap every item on the desktop between them. Losing is warned about once and
costs the tray for the session, nothing else.

### Measured on a private bus

```
ProtocolVersion                 0
IsStatusNotifierHostRegistered  true
RegisterStatusNotifierItem("org.freedesktop.Notifications")
    -> 'org.freedesktop.Notifications/StatusNotifierItem'
RegisterStatusNotifierItem("/org/ayatana/NotificationItem/test")
    -> ':1.373/org/ayatana/NotificationItem/test'      (sender substituted)
caller exits
    -> StatusNotifierItemUnregistered(':1.373/org/ayatana/NotificationItem/test')
       and the other item survives
```

**B2.3 is partial and stated as such.** Both forms were driven by synthetic callers, which proves
the code path and not the toolkits' real behaviour — which is exactly where SNI interop goes wrong.
Closing it needs a desktop with a real tray application running and costs no code.

---

## 2026-08-26 10:05 — A2 and A3 delivered: the history panel can act on notifications

Tasks `TODOS.md` A2.1–A2.3, A3.1–A3.2, A5.1. **19/19 tests, clean build.** Uncommitted.

### What was broken

Three of the five defects had one cause: `save()` writes no `live` and no `actions`, and `load()`
forces `live = FALSE`. In a standalone `sofi -show notification-history` every guard of the form
`if (n->live)` was therefore **dead code** — Enter did nothing, Shift+Delete did nothing, and the
"still on screen" stripe could never render. Underneath that, the per-entry verbs had no route to
the daemon at all: `sofi_notify_service_dismiss()` mutated a copy the daemon overwrites within
seconds, and `sofi_notify_service_invoke_action()` reached `emit_signal()`, found
`service.connection == NULL` outside the daemon, and dropped `ActionInvoked` on the floor.

### The design, and why it is not "persist the missing fields"

`live` and `actions` describe a notification **being on screen right now**. They belong to the
process that received it, and a file asserting either is wrong the instant the daemon acts. So the
file keeps carrying the record — what arrived, from whom, when — and the panel asks the daemon for
the rest. Same principle as R39, applied one layer down.

| Added | Purpose |
|---|---|
| `Dismiss(u)` | Retire one entry where the ring lives |
| `InvokeAction(u,u)` | Emit `ActionInvoked` from the process that owns the connection |
| `GetLive() → a(uus)` | id, action-pair count, `desktop-entry` — the two facts the file must not hold, plus A5's correlation key |
| `SofiNotification.action_count` | Held rather than derived; `actions` is NULL in any process that read the ring from disk |
| `SofiNotification.desktop_entry` | The `desktop-entry` hint, previously parsed and discarded. Persisted (A5.1) |
| `sofi_notify_store_apply_live()` | Overlays the daemon's answer. Clears before setting, so a dismissed entry is demoted rather than left stale |

Enter now mirrors the banner: run the action if there is one and get out of the way, otherwise
acknowledge and **stay open**, because going through a history list means going through it.

### Measured, on a private bus and a private cache

```
ActionInvoked      (uint32 42, 'default')
NotificationClosed (uint32 42, uint32 2)
```

Spec order — action first, then close with reason 2. `GetLive` returned
`(42, 2, 'org.example.App')`: right id, right action-pair count, right hint. `Dismiss` and
`InvokeAction` each emptied the live set while leaving all three entries in the history file, which
is the distinction between the two cleanup verbs holding.

**A3.2 was proven visually** with `-take-screenshot-quit`, and then re-run with `-selected-row 2` as
a control: with the cursor moved off it, the live row still rendered bright with an `@accent-strong`
stripe and no selection fill, so what is being seen is liveness and not the selection treatment.
Screenshots in the session scratchpad.

### A4 is no longer the defect it was written as

A4 targeted `dismiss_all_locally()`: with no daemon, `close_all()` finds nothing live and never
calls `notify_changed()`. That was a defect while the panel could not tell live from retired. It is
now **correct** — with no daemon nothing can be on screen, so there is nothing to dismiss. What is
left is a feedback question, not a mechanism one, and it is tabled rather than assumed.

---

## 2026-08-26 09:32 — Phase 11 opened. A1 delivered; task strip counter removed

Decisions `DECISIONS_LOG.md` R36–R40. Plan `PLANS.md` Phase 11. Tasks `TODOS.md` A1–A5, B1–B9.
Branch `tray`, uncommitted. **19/19 tests, clean build.**

### Delivered

| Area | Outcome |
|---|---|
| **A1 — the daemon loads its own history** | One call added to `sofi_notify_service_start()`. Proven by an isolated gate *and* a baseline run against the reverted binary |
| Task strip counter | Removed on USER's explicit instruction (R38 amendment). `mainbox` is now `[ "inputbar", "listview" ]`; `footer` and the three count widgets deleted. README updated. `-sasi-validate` clean |

### A1: the defect, measured rather than asserted

`sofi_notify_service_start()` called `sofi_notify_store_init()` and never
`sofi_notify_store_load()` — whose only caller in the tree was the history mode. The daemon
therefore booted with an empty ring, and the first `Notify` ran `save()` over the persisted file.

Measured on an isolated bus and cache, seeding two entries (ids 41, 42) then starting the daemon and
sending one notification:

| Binary | Entries after | New id |
|---|---|---|
| **Without the fix** | **1** — both seeded entries destroyed | **1** |
| With the fix | **3** — both seeded entries intact | **43** |

The id column is the half that was not in the original diagnosis and matters as much: without the
load, `next_id` restarts at 1 after every daemon restart, so a restarted daemon hands out ids that
senders still believe they hold, and a stale `CloseNotification` can retire somebody else's
notification. `sofi_notify_store_load()` advancing `next_id` past the highest stored id
(`notify-store.c:515-517`) is what prevents that, and it had never executed in the daemon.

Corroborated in the wild: `~/.cache/sofi/notifications.history` was **0 bytes** on this host.

### Disclosure — I wrote to the real history file

The first attempt at the gate isolated `XDG_CACHE_HOME` *inside* the D-Bus session rather than
before it. A system-wide service file made `org.freedesktop.Notifications` bus-activatable, so
`notify-send` activated a **second** daemon that inherited the real environment and wrote one test
entry (`A1 gate`) into `~/.cache/sofi/notifications.history`. The file was already 0 bytes, so no
history was lost. Restoring it to empty was blocked by the sandbox, so **the stray entry is still
there**; the running daemon overwrites that file on its next change in any case.

The method that works, for the next time this is needed: export `XDG_CACHE_HOME` *before*
`dbus-run-session` so anything the bus activates inherits it, and wait on `NameHasOwner` before
sending so `notify-send` never triggers activation at all. No XDG variable removes a system-wide
service file from dbus's search path. Script kept at `a1-gate.sh` in the session scratchpad.

---

## 2026-08-25 11:47 — Phase 10 delivered: theming and layout modernisation

Decisions `DECISIONS_LOG.md` R25–R35. Plan `PLANS.md` Phase 10. Tasks `TODOS.md` T1–T6.
Uncommitted on branch `theme`. **19/19 tests, clean build at `warning_level=3`.**

### Delivered

| Area | Outcome |
|---|---|
| Palette | `doc/palette.sasi` — 16 positional slots + 14 semantic aliases, one GResource parsed before every layout. **Seven inlined, drifted copies removed.** No `.sasi` outside it holds a hex value |
| Contrast | Every text-on-fill pair computed against WCAG AA and recorded in `BLUEPRINT.md`. Three tones constrained: `muted` non-text only, `critical` fills only, `on-accent` dark not white |
| Application menu | south centre, 560 × 62%, icons, two-tier rows, match count |
| Task strip | zoned filter \| tasks \| count, title-first, inset from the edges |
| Sheet switcher | north centre, fixed ten-cell grid under the top bar |
| Notification banner | bottom-right, cards with urgency stripes, clear button |
| Notification history | east, 420 × 76%, two cleanup verbs as keys and buttons |
| Cleanup | `sofi_notify_store_clear_history()`, `org.sofi.Notifications` interface, two CLI flags |
| Documentation | new `sofi-customisation(5)`; README theming section; task-first `CONFIG.md`; `sofi-theme(5)` loading section rewritten; two flags in `sofi.1` |

### Four pre-existing defects fixed rather than worked around

1. The banner's `kb-custom-1` "clear all" had **never been reachable** — implemented, but the
   surface is forced to take no keyboard. A button dispatches it by pointer.
2. `panel-window.sasi`'s `y-offset: -12px` put the task strip's bottom 12px outside the usable
   area. The two positioning paths negate offsets relative to each other and nothing said so.
3. `-sasi-validate` ran before the palette and reported a resolution failure for every `@name`,
   including in sofi's own layouts. It now seeds the palette first.
4. `panel-notify.sasi` claimed its 48px offset cleared hikari's top bar. It never did — the
   compositor subtracts the bar before sofi sees a size.

### One defect introduced and fixed in-session

The application-menu footer was missing `expand: false`, so it split the panel's height with the
listview — half-populated list, halved row count. Found by USER. Recorded as R35 because the trap
is general: `sofi_view_add_widget()` packs unrecognised widget names with expand=TRUE.

### Two of my own scoping claims corrected

The old white-on-`#916778` pair was **4.76:1** and passed AA, not the 3.2:1 asserted when R26 was
written. The justification for a dark `on-accent` stands on white-on-`color12` being **2.40:1**.
And `color8` is 2.29:1, so it cannot carry text at all — three "dimmed" roles moved off it.

### Not finished

The four surfaces have not been seen up at once and nothing is visually confirmed. Both need the
running `2.0.0-dev` notification daemon replaced on a live session — USER's step.

---

## 2026-08-25 08:30 — Clean separation from upstream. Version set to 1.0.0.

USER ruling: *"we don't want their branding, their community, their version, we don't want
anything from them"* — sofi forked and separated cleanly, and is purpose-built for one
compositor. Tagging and release are USER's own; the target is **v1, not v2**.

### Version

`meson.build` `2.0.0-dev` → **`1.0.0`**. The `2.0.0` line was rofi's `next`-branch numbering
inherited at the fork point and never ours. Issue-template placeholders follow. `PACKAGE_VERSION`
and `VERSION` confirmed `1.0.0` in the configured header. `sofi -v` reports the git description
in a checkout, so it will read `1.0.0` once the tag exists.

### Upstream CI dependencies removed

- **`.github/workflows/main.yml` deleted.** Its only job was `davatorium/auto-close-issues@v1.0.4`
  — a third-party action from the upstream author that auto-closed issues failing a template
  check. Both the dependency and the practice are upstream's.
- **`davatorium/doxy-coverage` clone removed** from `.github/actions/setup`, and its
  `doxy-coverage.py` invocation removed from `.github/actions/doxycheck`. The doxygen
  *warning* check is self-contained and was kept; only the third-party coverage script went.

### Dead CI dependencies removed at the same time

`fluxbox`, `xdotool`, `xterm`, `xfonts-base`, `xutils-dev`, `gdb`, `lcov`, `jq`, `discount`,
`texi2html` and `texinfo` were being installed on every CI run. They are rofi's old X11
integration-test harness; nothing in this repository references any of them. `graphviz` (used by
doxygen) and `pandoc` (used for manpages) were verified in use and kept.

### Upstream's community voice replaced

`CONTRIBUTING.md` and all three issue templates were rofi's text verbatim, carrying its
issue-management culture — "please consider you're wrong", "will be closed and locked as spam",
mandatory gist URLs, "do NOT ask for an update". Rewritten in the project's own voice for a small
purpose-built fork.

**One inherited rule was actively wrong for this project.** `documentation_report.yml` carried a
**required** checkbox reading *"No, my documentation issue is not about running sofi using the
wayland display server protocol"*, plus "Please do not submit reports related to wayland". That is
rofi's position, where Wayland was an unsupported fork feature. **sofi is Wayland-first**, so the
template blocked every legitimate report. Removed.

The templates now ask for the compositor and whether Wayland or X11 — which actually matters
here, since layer-shell versus `xdg-shell` fallback changes behaviour.

### Author and README attribution corrected

The manpages listed Qball, Rasmus Steinke and Morgane Glidic under `AUTHOR`, implying they author
sofi. They do not. All seven manpage AUTHOR sections now name the sofi maintainer and point at
`AUTHORS` for the inherited history. The README's paragraph of lineage prose was cut to a factual
fork statement, and now tells users to file sofi issues here and rofi issues upstream.

`script/sofi-sensible-terminal` was traced to its actual origin: public domain by Han Boetes, via
i3. Recorded in `AUTHORS` and its manpage.

### What cannot be removed, and why

**The MIT licence requires the copyright notice be retained in all copies.** That fixes three
things in place permanently:

- the `Copyright © 2013-2023 Qball Cow` headers in ~90 files under `source/`, `include/`,
  `lexer/`, `config/` and `test/`
- the upstream holders in `COPYING`
- `AUTHORS`, which exists to record exactly this

Stripping them would make every distributed copy of sofi a licence violation. Everything else
upstream — branding, community, infrastructure, version line — is gone.

### Verified

`ninja -C build` clean, **19/19 tests**, all ten manpages regenerate through pandoc,
`.github` sweeps clean of `davatorium`, `doxy-coverage`, `auto-close`, `qtools`, `freenode`,
`libera` and `blob/next`. No `2.0.0` remains anywhere.

---

## 2026-08-25 08:18 — v1 branding and user-facing documentation audit

A full sweep of every user-facing surface for residual rofi identity and for docs that no longer
describe the project. The rename itself was found sound — source, headers, man page filenames,
pkg-config, desktop files, `SOFI_*` variables and the `.sasi` extension are all consistent. The
defects were in what the rename **over-reached into** and what it **never covered**.

### Collateral damage from the blind substitution — fixed

The `rasi`→`sasi` and `rofi`→`sofi` passes were applied as raw substring replacements and
corrupted three things that were never branding:

| Was | Restored to | Files |
|---|---|---|
| `sasi@xssn.at` | `rasi@xssn.at` | 5 man pages |
| `sardemff7+sofi@sardemff7.net` | `sardemff7+rofi@sardemff7.net` | 4 man pages |
| `psofile` | `profile` | `sofi-thumbnails.5` |

The first two are contributor contact details carried under the MIT attribution obligation.
Rewriting them was the one thing the rename must not have done.

### The application icon spelled "RofI" — fixed

`data/sofi.svg` — the icon installed to `hicolor/scalable/apps` and referenced by both desktop
files — rendered the literal letters R-o-f-I as `<tspan>` text. One character changed to `S`;
`data/sofi.png` regenerated from the corrected source with `rsvg-convert`. A purpose-designed
logo is still worth doing, but the icon no longer ships the upstream name.

### Documentation that contradicted itself or pointed nowhere — fixed

- `INSTALL.md` instructed `apt/dnf/pacman install sofi` and a Gentoo ebuild, none of which
  exist, four lines above a correct statement that sofi is not packaged anywhere. Fabricated
  section removed; the release-tarball section now says no tag has been cut.
- `sofi.1` referenced an `AUTHORS` file that did not exist. **`AUTHORS` created**, listing sofi,
  rofi and simpleswitcher holders separately.
- `COPYING` carried no sofi copyright line at all. Added, above the retained upstream notices,
  with a note that the notices are cumulative.
- Three issue templates and `CONTRIBUTING.md` linked `blob/next/…` and told contributors to check
  a `next` branch. **This repository has only `master`.** All repointed.
- `documentation_report.yml` linked `https://davatorium.github.io/sofi/`, a hybrid of both
  project names that has never existed.
- Version placeholders read `1.7.5` / `1.6.0` — rofi releases. Now `2.0.0-dev`.

### Dead communities removed

`reddit.com/r/qtools` (rofi's community) and `webchat.freenode.net` (dead since 2021) were being
advertised as sofi support channels, and `#sofi @ libera.chat` — a channel not registered to this
project — was printed by `sofi -h` at every invocation. USER ruled **GitHub only**. Removed from
`CONTRIBUTING.md`, `config.yml`, the `sofi.1` SUPPORT section and `source/sofi.c`.

### CI was dead on the main branch — fixed

`.github/workflows/build.yml` triggered on push to `next`. That branch does not exist, so **no
push to `master` has ever run CI**; only pull requests did. Same leftover family as the doc
links. Repointed to `master`.

### The rofi website deleted

`mkdocs/` was the upstream documentation site, entirely untouched by the rename: `site_name: Rofi
Documentation`, `repo_url: davatorium/rofi`, the rofi logo, rofi 1.7.x download links, ~30 theme
pages for themes sofi does not ship, and a nav pointing at `current/rofi.1.markdown` files absent
from this tree — meaning **the site could not build**. Its workflow only fired on branches
`sphinx`/`next`, which is the sole reason it had not already published. USER ruled delete.
`mkdocs/` and `.github/workflows/mkdocs.yml` removed (83 files, ~2,200 lines). The man pages in
`doc/` remain the documentation of record.

### The docs described the old project — rewritten

The largest gap. Per R16–R24 sofi is hikari-sakura's shell, and no user-facing document said so.

- **`README.md`** rewritten to lead with the four surfaces, each with its invocation and where it
  renders, plus the per-surface instance locks and compiled-in layouts. The inherited
  general-purpose modes are retained and documented, per USER ruling and AGENTS.md rule 3.
- **`sofi.1` NAME/DESCRIPTION** no longer opens "an X11 pop-up window switcher" for a project
  whose primary target is a Wayland compositor on FreeBSD.
- **Five modes were entirely undocumented** in *Available Modes*: `filebrowser`,
  `recursivebrowser`, `sheets`, `notifications`, `notification-history`. All added.
- **`-notification-daemon` was documented nowhere** — the flag that enables all of Phase 9.
- **`-wayland-keyboard-interactivity`** (R18) was undocumented while its sibling `-wayland-layer`
  was; **`-x11`/`-xcb`** appeared only in the README.
- **The task-manager verbs were invisible.** `sofi-keys.5` documented `kb-custom-1`/`-2`
  generically without saying they minimise/maximise in Wayland window mode, or that
  `kb-custom-1` sends to sheet in `sheets` mode.
- A **hikari-sakura integration section** added to `sofi.1` beside the existing i3 and Hyprland
  ones, covering the four invocations and the `hikari.sock` requirement.
- `CONFIG.md` now opens by stating no configuration file is required, and its rofi-era "migrate
  from older configuration format" phrasing is gone.

### Verified

`ninja -C build` clean, **19/19 tests pass**, `ninja generate-manpage` regenerates all ten man
pages through pandoc with the new sections present in the roff output, and `sofi -h` no longer
prints the IRC line.

### Left deliberately

Two upstream CI dependencies remain and are functional rather than branding:
`davatorium/auto-close-issues@v1.0.4` in `main.yml` and a clone of `davatorium/doxy-coverage` in
`actions/setup`. Replacing them changes CI behaviour and was out of scope for a docs pass.

---

## 2026-08-24 10:20 — Phase 8 delivered: the four native surfaces

sofi stopped being a rebranded rofi and became hikari-sakura's shell. Everything below builds
clean and passes **19/19 tests**, in both the before and after state. **Nothing is committed.**

### Delivered

| Item | Evidence |
|---|---|
| Four compiled-in panel layouts selected by mode (R16) | Zero-config renders: drun 280×816 west, window 1920×52 south, sheets 190×472 east, `-e` 380×55 north-east |
| Per-surface instance locks (R17) | Shared pidfile → second panel dies with a lock warning; per-surface → launcher and strip render simultaneously with no flags |
| Task-manager verbs on window mode (R15 / Phase 7c) | `kb-custom-1` minimise, `kb-custom-2` maximise, both toggles; minimised bit surfaced as `URGENT` |
| Layer-shell v1 → v4 + `-wayland-keyboard-interactivity` (R18) | `set_keyboard_interactivity(2)` accepted by the compositor; no fallback warning |
| hikari control socket (R19) | `state` / `sheet <n>` / `pin <n>` verified against a nested hikari |
| Native `sheets` mode | Renders live compositor state; occupied sheets show counts, empty dim, current takes the accent |
| `-e` self-dismisses (Phase 9 groundwork) | delay 1 → 1073ms floor; delay 2 with `-no-click-to-exit` → 2096–2466ms over four runs |

### Verified end-to-end on hardware

- **`wl_registry` dump** against the running compositor — 32 globals, settling capability
  questions that had previously been answered by inference.
- **Send-to-sheet works.** One window on sheet 5 → `counts` index 5 = 1; `pin 8` moved it to
  index 8; `sheet 8` switched to it. This closes **Q17**, which had been recorded as
  inexpressible by any standards-track protocol.
- **The sheets mode does not mutate compositor state on open** — A/B tested after an earlier
  observation turned out to be a client exiting, not sofi.
- **Full-screen `grim` capture** with the task strip live, confirming it overlays rather than
  displacing, and that hikari's own top bar is untouched.

### Two claims retracted

Both are recorded in full in `DECISIONS_LOG.md`; noted here because they were asserted to the
USER before being checked.

1. **`sofi -show window` was reported dead.** It was not. `strings` on the compositor binary
   cannot see a dynamically linked protocol's name, and a binary predating its own commit is the
   normal build-then-commit order.
2. **The sheet switcher was reported fully blocked.** Half of it — current sheet versus
   elsewhere — was already arriving over foreign-toplevel's minimised bit and being discarded.

### Superseded

- **Phase 7b** as written (a `~300`-line `ext_workspace_v1` client mode) is superseded by R19.
  The socket is smaller on both sides and expresses send-to-sheet, which the protocol cannot.
- **`sofi-config/`** as the deployment mechanism is superseded by R16. The layouts are compiled
  in; `~/.config/sofi/` is now optional rather than required.

### Not done

Installation and the compositor restart. `/usr/local/bin/hikari` is still the Aug 22 build with
no socket, so the sheets mode correctly reports `ENOENT` until it is rebuilt and the session
restarted.

---

## 2026-08-22 20:58 — LIVE VERIFICATION on a wlroots compositor

### Correction first

Throughout this session I asserted "no compositor is available to test against" and repeated
it in every Wayland-phase entry. **That was never checked and it was false.** The USER is
running a custom wlroots compositor: `WAYLAND_DISPLAY=wayland-0`,
`XDG_RUNTIME_DIR=/var/run/xdg/orpheus497`, `xdg-desktop-portal-wlr` running, Xwayland on
`DISPLAY=:1`. A `sofi.pid` timestamped 20:25 shows the USER had already been running the
renamed binary while I was claiming it could not be run.

Prior entries have been amended in place. Everything below is what actually running it showed.

### Compositor capabilities observed

```
wayland registry: interface xdg_wm_base
wayland registry: interface zwlr_layer_shell_v1
wayland registry: interface wp_fractional_scale_manager_v1
wayland registry: interface xwayland_shell_v1
Output eDP-1: 1920x1200 (300x190mm) position 0x0 scale 1 transform 0
```

### Results

| Test | Result |
|---|---|
| Launch under the live compositor | **Maps and runs.** Surface created, continuous redraw, clean exit on timeout |
| Shell selection | **Layer shell chosen** — and `xdg_wm_base` is *also* advertised, so the Phase 2b preference logic is confirmed: xdg is present but correctly not used |
| Default theme (Phase 6) | **Renders live at the right geometry**: `menu=280x816 pos=(15,175)` — the 280px width and 15px x-offset from `sofi-config` |
| ASAN build vs live compositor | **0 AddressSanitizer reports** over a 4s session |
| **3 concurrent instances** (separate `-pid` files) | **All 3 created surfaces, 0 shm errors** |
| Warnings in the live log | none functional |

### The concurrent-instance test is the important one

It directly exercises the two Phase 2a buffer-pool fixes. Under the original code —
a fixed name `/rofi-wayland-surface` with `O_CREAT|O_EXCL` — instances 2 and 3 would have
failed `shm_open` with `EEXIST`, `display_buffer_pool_new()` would have returned NULL, and
`display_buffer_pool_get_next_buffer(NULL)` would have dereferenced it. Both fixes confirmed
in one test.

Note a first attempt at this test was invalid: without `-pid` the second instance is refused
by the pidfile lock (correct single-instance behaviour) and never reaches the shm pool.

### A finding investigated and dismissed

`sofi -log <file>` emits `Failed to parse theme: configuration { log: <path>;}` into the log.
This is **not a bug**. `config_parse_cmd_options` (`source/xrmoptions.c:834-853`) deliberately
tries any unrecognised `-foo bar` as an unquoted rasi property, and on failure clears the
errors and retries quoted. The retry succeeds and `-log` works — 934 lines were captured.
It is log noise from a designed fallback. Recorded so it is not "found" again and misfiled.

### Still unverified

- **The xdg-shell fallback (Phase 2b).** This compositor has layer shell, so the fallback is
  correctly bypassed. Exercising it needs Mutter or KWin, or a nested compositor without
  `zwlr_layer_shell_v1`.
- **Interactive input paths** — key repeat (the UAF fix), the paste path, mouse handling.
  These need real keystrokes, not piped stdin.
- **The XCB backend**, though Xwayland on `:1` now makes that testable.

### Gap this exposes

`wp_fractional_scale_manager_v1` is advertised by this compositor and sofi does not implement
it (`AUDIT_REGISTER.md` notes integer `buffer_scale` only). On a fractional-scale output the
menu will be scaled by the compositor rather than rendered crisply. Worth a backlog item.

---

## 2026-08-22 20:44 — Phase 5: the four design-level findings

Build clean, **19/19 tests**, 12/12 sofi tests ASAN-clean, zero warnings. Three of the four
fixed; the fourth deliberately not attempted, see below.

### 1. `levenshtein()` VLA — FIXED

`unsigned int column[needlelen + 1]` was a VLA sized directly by the user's search string,
guarded only against `G_MAXLONG`. A long paste allocated megabytes on the stack — and this
runs on matcher worker threads, whose stacks are smaller than the main one. Now uses a
512-entry stack row for the common case and heap-allocates above that. Also rejects a
negative `needlelen`.

### 2. Icon-fetcher destroy race — FIXED, and it was worse than reported

The register said `sofi_icon_fetcher_destroy()` frees state in-flight workers still use.
Tracing the teardown showed the actual mechanism: `sofi_view_workers_finalize()` called
`g_thread_pool_free(tpool, TRUE, FALSE)` — the final `FALSE` means **do not wait for jobs
already running** — and the original code said so in a comment. `cleanup()` then called
`sofi_icon_fetcher_destroy()` twenty-odd lines later, freeing the caches those still-running
workers were reading.

**The fix could not simply be "wait".** `page_changed_callback()`
(`source/view.c:764`) also calls `workers_finalize()`, on *every page change*, purely to
drop queued work for the old page. Waiting there would block the UI thread on an in-flight
icon decode — or a thumbnailer spawn — every time the user pages through a list.

So the function now takes a `wait_for_running` parameter:
- `cleanup()` passes **TRUE** — correctness at teardown; only actively-running jobs are
  joined, since the queue is discarded regardless.
- `page_changed_callback()` passes **FALSE** — responsiveness; nothing it touches is freed
  on that path.

Signature changed in the installed header `include/view.h:346`. Acceptable: `ABI_VERSION`
was already bumped and R1 makes this a hard fork.

Residual risk, recorded rather than hidden: a pathological thumbnailer (`g_spawn_sync` with
no timeout, `source/sofi-icon-fetcher.c`) can still delay exit. Bounding that means
restructuring to `g_spawn_async` plus a waitpid loop — a separate change.

### 3. dmenu partial-line emission — FIXED and behaviourally verified

On a `select()` timeout the reader flushed whatever was buffered through
`read_add_block()`, so a line straddling a stall became two bogus entries. A timeout means
the producer is *slow*, not finished; the EOF path already flushes the remainder correctly.
The timeout branch now publishes only completed entries and leaves the partial line buffered.

Verified end-to-end:

```
$ ( printf 'x\ny'; sleep 0.4; printf 'z\n' ) | sofi -dmenu -no-config -dump
x
yz          <- one entry, previously "y" and "z"
```

### 4. `source/view.c` raw XCB calls — NOT DONE, deliberately

The finding is real: `source/view.c` makes raw xcb calls at `:364`, `:1012-1055` and
`:1917-1925`. They are correctly guarded (`#ifdef ENABLE_XCB` plus a runtime
`config.backend == DISPLAY_XCB` test), so this is a **layering** problem, not a bug — the
code builds and behaves correctly on both backends.

Doing it properly means implementing `get_clipboard_data` in the xcb proxy (the slot exists
at `include/display-internal.h:52` and only Wayland fills it), rerouting the
`XCB_SELECTION_NOTIFY` → `sofi_view_paste()` path through it, and removing
`sofi_view_paste(SofiViewState *, xcb_selection_notify_event_t *)` — which takes an **xcb
type** — from the public `include/view.h`.

The `:364` site is not a simple substitution either: `xcb_clear_area` + flush forces a
server-side *expose* event, whereas `sofi_view_queue_redraw()` schedules an idle repaint.
Different mechanisms.

**Note (corrected 20:58):** `DISPLAY=:1` is set and Xwayland is running, so an X11 test
target does exist. The refactor is still not attempted — the reasoning below about it being
a paste-path rewrite touching the public header stands on its own — but the stated reason
("no X display") was wrong. Rewriting the paste path blind is exactly how a cleanup introduces bugs while
claiming to remove debt. Left as recorded debt requiring an X11 test environment.

### Behavioural verification added

The dmenu paths are the first sofi code exercised at runtime rather than only compiled:

| Test | Result |
|---|---|
| basic stdin | 3 entries |
| `-sep '\0'` NUL separator | 3 entries |
| line straddling a 400 ms stall | one entry (the fix) |
| **3000 lines under ASAN** — crosses the `BLOCK_LINES_SIZE` 2048 boundary where the one-past-the-end write was | **0 ASAN reports** |

That last one directly exercises the earlier `read_add_block` fix.

---

## 2026-08-22 20:31 — Phase 4 complete, Phase 5 substantially complete

Build clean, **19/19 tests**, all 12 sofi tests ASAN-clean, zero project-code warnings.

### Phase 4 — FreeBSD

- `meson.build:63` — `find_library('rt', required: false)` added to `deps`. `shm_open()` is
  in libc on FreeBSD and glibc >= 2.34, but older glibc puts it in librt; verified by a
  compile test that it links without `-lrt` here, so this only matters for the Linux CI.
- `INSTALL.md`, `.build.yml` and the `.github` templates were already corrected in the
  previous session's doc scrub.
- The Linux CI workflows were **kept**. R14 ruled out OpenBSD/NetBSD *BSD* targets; it did
  not drop Linux, which `README.md` still lists as supported.

### Phase 5 — 17 medium findings fixed

**Icon fetcher threading** (the highest blast radius — worker threads touching UI state):
- `sofi_view_reload()` was called directly from a `GThreadPool` worker, straight into the
  backend proxy. Now marshalled through a `g_idle_add` helper onto the main loop.
- All six `query_done` publishes inside the worker (lines 555-764) are now
  `g_atomic_int_set`, ordered *after* the surface store; the UI-side reader in
  `sofi_icon_fetcher_get_ex` reads the flag with `g_atomic_int_get` *before* the surface.
  The original code carried the comment "is a pointer write atomic?" — that uncertainty
  was the bug.
- `cairo_image_surface_create()` result unchecked: cairo returns a surface in an error
  state rather than NULL, and `get_data()` then returns NULL, which the pixel loop wrote
  through. Both now checked.

**Unchecked xcb replies:**
- `source/xcb/display.c:466` `xcb_randr_get_output_info_reply()` dereferenced on the next
  line, while `crtc_reply` immediately below it *was* checked.
- `source/xcb/display.c:577` Xinerama reply passed to the iterator unchecked.
- `source/xcb/display.c:1469` XKB MapNotify handler did not check
  `xkb_x11_keymap_new_from_device()` / `xkb_x11_state_new_from_device()`, though the setup
  path at `:1744` does.

**dmenu:**
- `read_add_block` wrote its NULL sentinel to `values[length + 1]` while the caller only
  flushes *after* length reaches `BLOCK_LINES_SIZE` (2048) — so a full block wrote
  `values[2048]`, one past the end. Guarded; consumers use `->length`.
- `read_add` never initialised `permanent`, which `dmenu_token_match` reads, on
  `g_realloc`'d (non-zeroed) memory.

**Unbounded recursion:**
- `parse_ssh_config_file` followed `Include` with no depth limit or cycle detection —
  a config including itself recursed until the stack was exhausted. Capped at 16 levels,
  matching OpenSSH.
- `walk_dir` (drun) recursed with no depth bound. Capped at 32.

**Durability:**
- `source/history.c` rewrote history by `fopen(filename, "w")` — truncating the live file
  in place, so a crash, full disk or kill between truncate and write destroyed it. Now
  writes a sibling `.tmp` and `g_rename()`s over the original, which is atomic within a
  directory.

**Pidfile — three bugs in one function:**
- A corrupt or truncated pidfile parses to 0, and `kill(0, SIGTERM)` signals **the entire
  process group** — sofi itself and the shell that launched it. Now rejects `pid <= 0`.
- The wait loop was `while(1)` polling at 100us, spinning forever if the running instance
  ignored SIGTERM. Now bounded (2s) with a diagnostic.
- The pid write loop did `l += write(...)` unchecked; a `-1` return drives `l` negative,
  which both loops forever and indexes before the start of the buffer. Now handles errors
  and `EINTR`.

**Correctness:**
- `source/modes/window.c:901` appended window titles into a Pango-markup row **unescaped**
  while both sibling branches escape — a window titled "Tom & Jerry" rendered blank on X11.
  The Wayland twin was already correct.
- `combi_mode_result` dereferenced `*input` unconditionally, and indexed `switchers[0]`
  without checking `num_switchers`.
- `source/view.c` indexed `line_map[]` at two sites guarded only by `list_view != NULL`,
  not by `filtered_lines`; the other call sites in the same file do gate on it.
- `source/modes/recursivebrowser.c:188` built the visited-directory set with
  `g_str_hash` + **`g_int_equal`** — comparing the first four bytes of a path as an int,
  so two distinct paths sharing a bucket and a 4-byte prefix were conflated and a
  directory silently skipped.
- `drun_read_string` trusted the on-disk cache to be NUL-terminated. It is not a trusted
  input; now verified.

**Scripts:**
- `script/sofi-theme-selector:40` did `TMP_CONFIG_FILE=$(mktemp).sasi` — which names a
  *different* file than the one mktemp safely created, leaving the real target predictable
  and unprotected while leaking the secure one. Replaced with `mktemp -d` plus a `trap`.
- `script/get_git_rev.sh:8` tested `.git` with `-d`; in a worktree or submodule `.git` is
  a *file* holding a `gitdir:` pointer, so version info was silently dropped there. Now `-e`.

### One audit claim corrected

The register said `walk_dir` "follows symlinked directories recursively". It does not —
the switch recurses only on `DT_DIR`, and `DT_LNK` falls through to `default: break`.
The unbounded *depth* was real (a bind-mount loop reports `DT_DIR`), and that is what was
fixed; the symlink-loop framing was wrong.

### Remaining in Phase 5

~42 lower-severity findings from the register are not yet addressed, including
`sofi_icon_fetcher_destroy()` freeing state in-flight workers still use (needs a
cancellation design, not a one-line guard), the `levenshtein()` attacker-sized VLA on a
worker stack, `dmenu` async partial-line emission, and the `source/view.c:364` raw-XCB
layering violation.

---

## 2026-08-22 20:14 — Phase 6 (early): bundled themes removed, README scrubbed

USER directive: drop the inherited theme collection in favour of the supplied config, and
scrub the README of external links and inherited references. Build clean, **19/19 tests**,
zero project-code warnings.

### Themes: 37 files removed, 2 shipped

`themes/` is gone entirely (556 KB, 35 themes + `iggy.jpg` + `breaking-themes/`). Those were
upstream rofi's theme collection, none of it sofi's.

`sofi-config/` is now the single shipped theme **and** the compiled-in default:

- `doc/default_theme.sasi` regenerated from `sofi-config/config.sasi` with the colour
  variables **inlined** — an `@import` cannot resolve from inside a GResource, so the
  compiled-in copy has to be self-contained.
- `doc/default_configuration.sasi` regenerated from the file's `configuration {}` block.
- `meson.build:380-384` installs `config.sasi` + `colors-default.sasinc` to
  `$datadir/sofi/themes/`, replacing the 36-entry install list.

Verified: `sofi -no-config -dump-theme` succeeds with **zero stderr**, so the theme is
genuinely live with no config file present.

### `colors-default.sasi` → `.sasinc`

`script/sofi-theme-selector:92` globs `${TD}/*.sasi`, so the colour file would have been
offered as a selectable theme and produced a broken selection. It is an *include*, which is
exactly what `.sasinc` means. Renamed, and the import changed to extension-less
`@import "colors-default"` so it resolves through the extension array and survives any
future extension change — the failure mode recorded against R3.

### README scrubbed

External links reduced from 24 to **4**, all of them MIT attribution
(rofi, simpleswitcher, superswitcher, lbonn).

Removed: shields.io badges, upstream's demo video, the `## Screenshots` section (pointed at
`releasenotes/`, deleted under R12), `## Wiki` and its seven sub-links, `## Discussion
places`, and the starchart.cc graph — none of which exist for this fork. Config/theme links
now point at local files (`CONFIG.md`, `doc/sofi-theme.5.markdown`) rather than a web tree.
Table of contents trimmed to sections that still exist. A new Themes section documents
copying the shipped theme.

### Corrections to inherited docs

- **`INSTALL.md` claimed `pkg install sofi`** on FreeBSD, plus openSUSE and MacPorts
  packages. No distribution packages sofi. Replaced with an honest "not yet packaged"
  note and the actual FreeBSD build-dependency list, calling out `bison` explicitly since
  base `byacc` cannot build the GLR grammar. *(This lands part of Phase 4 early.)*
- **`INSTALL.md:28`** documented an autotools `--disable-check` flag; meson uses
  `-Dcheck=disabled`.
- **`.github/` templates** pointed at `DaveDavenport/*/wiki` pages that do not exist;
  repointed or replaced with manpage references.
- **`.gitattributes`** still had a `releasenotes export-ignore` rule for a deleted directory.

Legitimate external links were left alone: `freedesktop.org` spec references in
`doc/sofi.1.markdown`, and the Wikipedia/BibTeX links inside the generated
`doc/sofi.doxy.in`.

### Install layout now

24 files, down from 57 — the difference is the 35 removed themes.

---

## 2026-08-22 20:02 — Phase 3 complete: the rename

**sofi is now sofi.** 174 files changed, +3837/−3841, 64 renamed. Clean rebuild,
**19/19 tests**, all 12 sofi tests ASAN-clean, **zero project-code warnings**.

### All ten invariants pass

| Invariant | Result |
|---|---|
| 3a binary is `sofi` | PASS |
| 3b no rofi filenames | PASS |
| 3c no `rofi_`/`ROFI_`/`Rofi` in any C code | PASS |
| **3c2 `COPYING` md5 unchanged** | **PASS** (`166cdc06…`) |
| **3c3 copyright notices still exactly 90** | **PASS** |
| 3d no `"rofi` string literals in `source/`/`config/` | PASS |
| 3e no `.rasi`/`.rasinc` files remain | PASS |
| 3f pkg-config module is `sofi` | PASS |
| 3g no upstream URLs outside frozen docs | PASS |
| build + 19/19 tests | PASS |

### RR4 — the attribution risk — did not materialise

The bulk rename ran as a scripted transform with a protection regex
(`Copyright|sean\.pringle|qball@|gmpclient|Sean Pringle|Qball`) that skipped **91 lines**
across the tree. Copyright lines were snapshotted before the rename and diffed after:
byte-identical. `COPYING` md5 unchanged. 86 files still carry the full permission notice.

The project-title line `* rofi` → `* sofi` in 68 file headers *was* changed — correctly, as
those files are now part of sofi — while the `Copyright ©` lines beneath were not touched.

### Sub-phases

- **3a** `meson.build:1` `project('sofi')`. The three things that do not follow a project
  rename were edited by hand as predicted: `executable()`, `pkg.generate(filebase/name)`,
  and every literal filename in the install lists. `PACKAGE_BUGREPORT`/`PACKAGE_URL`
  repointed to `orpheus497/sofi`.
- **3b** 30 files `git mv`'d.
- **3c** 335 identifiers across 94 files, then a **second pass** for 9 more files — see
  "Two passes were needed" below. Plus `ABI_VERSION` already bumped in Phase 1.
- **3d** Config/theme/script paths → `sofi/`; cache files → `sofi3.druncache`,
  `sofi-4.runcache`, `sofi-2.sshcache`, `sofi3.filebrowsercache`,
  `sofi-drun-desktop.cache`, `sofi-entry-history.txt`; pidfile → `sofi.pid`;
  env vars → `SOFI_RETV`/`SOFI_OUTSIDE`/`SOFI_INFO`/`SOFI_DATA`/`SOFI_INPUT`/
  `SOFI_PLUGIN_PATH`/`SOFI_PNG_OUTPUT`. Hard break per R2/R4, no fallbacks.
- **3e** 39 theme/config files → `.sasi`/`.sasinc`; extension array, gresource aliases,
  `config.sasi`, `sofi.sasi`, `-sasi-validate`.
- **3f** WM_CLASS `"sofi\0Sofi"`, layer-shell namespace and xdg title/app_id `"sofi"`,
  desktop files with `StartupWMClass=Sofi` added to match R5, helper scripts,
  `sofi.pc` / `$libdir/sofi`.
- **3g** Docs, examples, issue templates. `README.md` hand-rewritten (see below).
  `.build.yml` replaced with a real FreeBSD job per R14.

### Two passes were needed for 3c

The first pass used `\b`-anchored patterns and missed 48 identifiers where `rofi_` is
preceded by an underscore — `wayland_rofi_view_*`, `xcb_rofi_view_*`,
`__rofi_view_state_create`, `int_rofi_theme_print_property`, `INCLUDE_ROFI_TYPES_H`.
`\b` does not match between `_` and `r` because both are word characters. A second
unanchored pass caught them.

**This is why the invariant is a grep over the whole tree and not a trust in the script.**
An initial invariant check also false-passed because `git grep` rejected the `\b` regex and
the `||` branch fired; re-run with `-E` it correctly reported 7 remaining files.

### Deliberate exceptions — things NOT renamed

- **`themes/gruvbox-*` `Source: https://github.com/bardisty/gruvbox-rofi`** (7 files).
  Third-party source attribution for where those themes came from. Rewriting it would
  falsify their provenance.
- **`README.md` mentions of rofi** (6). All intentional: the fork-provenance paragraph
  required by R12, and two references that genuinely mean upstream rofi.
- **`mkdocs/`** — frozen historical docs, untouched per R7 (the per-version trees were
  already deleted).
- **`.devdocs/`** — this workspace is process history; rewriting it would falsify the record.

### README rewritten by hand

The bulk substitution produced `davatorium/sofi` — correct name, wrong org — and left an
upstream release-history list (1.7.0–2.0.0) for releases sofi never made. Both fixed.
Added, per R12 and the R2/R4 "make it loud" requirement:

- Explicit fork provenance crediting Dave Davenport (Qball), Sean Pringle (simpleswitcher)
  and lbonn (Wayland), pointing at `COPYING`.
- A blockquote stating plainly that sofi is **not** a drop-in replacement: it does not read
  rofi's config, themes, cache or env vars, and rofi plugins will not load.

### Incidental fixes found during the rename

- **Two gresource prefix mismatches.** Renaming `/org/qtools/rofi` → `/org/sofi` in
  `resources/resources.xml` desynced from `source/sofi.c` (which the script had made
  `/org/qtools/sofi`) and from `lexer/theme-lexer.l:411`, which my first `sed` did not
  cover. Both caught and aligned; a mismatch would have made the built-in default theme
  fail to load at runtime with no build error.
- **Stale `extern` declaration.** `source/sofi.c:1143` declared
  `extern const char *rasi_theme_file_extensions[]` locally, so 3e's rename produced a link
  error — `undefined symbol: rasi_theme_file_extensions`. Caught by the build.
- **Test expectations updated.** `test/theme-parser-test.c:1274,1279,1328,1330` hardcoded
  `.rasi` paths. With `.rasi` no longer a recognised extension the resolver appends
  `.sasinc`, producing `/not-existing-file.rasi.sasinc`. The new behaviour is correct; the
  expectation was stale. Updated to `.sasi`.

### Install layout verified

A `DESTDIR` staging install was run: **57 files, zero `rofi`-named**.

```
/usr/local/bin/sofi, sofi-sensible-terminal, sofi-theme-selector
/usr/local/include/sofi/{mode,mode-private,helper,sofi-types,sofi-icon-fetcher}.h
/usr/local/libdata/pkgconfig/sofi.pc   (Name: sofi, pluginsdir=/usr/local/lib/sofi/)
/usr/local/share/applications/sofi.desktop, sofi-theme-selector.desktop
/usr/local/share/icons/hicolor/scalable/apps/sofi.svg
/usr/local/share/man/man1/sofi{,-sensible-terminal,-theme-selector}.1
/usr/local/share/man/man5/sofi-{actions,debugging,dmenu,keys,script,theme,thumbnails}.5
/usr/local/share/sofi/themes/*.sasi (+ gruvbox-common.sasinc)
```

---

## 2026-08-22 19:52 — Phase 2b complete: xdg-shell fallback

sofi now runs on compositors without `zwlr_layer_shell_v1` — Mutter (GNOME) and KWin
(Plasma). Clean rebuild, **19/19 tests**, all 12 sofi tests ASAN-clean, zero project-code
warnings, 20 xdg symbols linked.

Before this, layer shell was mandatory: Phase 2a made its absence a clean error instead of a
`SIGABRT`, and this phase makes it not an error at all.

### Design

Layer shell stays **preferred** — it can position and size itself, which xdg-shell cannot.
xdg-shell is a fallback selected only when layer shell is absent. A new
`wayland_shell_kind` enum (`include/wayland-internal.h:23-33`) records the choice once at
setup, and the four functions that drive the shell branch on it.

| Component | Detail |
|---|---|
| `wayland_shell_kind` | `NONE` / `LAYER` / `XDG`, plus `xdg_wm_base`, `xdg_surface`, `xdg_toplevel` in `wayland_stuff` |
| Registry bind | `xdg_wm_base` at version ≤2, `WAYLAND_GLOBAL_XDG_WM_BASE` added to the globals enum and to the `global_remove` switch |
| **ping/pong** | `xdg_wm_base.ping` → `xdg_wm_base_pong`. Mandatory — an unanswered ping makes the compositor kill the client as unresponsive |
| `xdg_surface.configure` | acked immediately, as the protocol requires |
| `xdg_toplevel.configure` | adopts width/height only when positive (0 means "choose your own") and feeds the same `layer_width`/`layer_height` the layer-shell configure does, so `display_get_surface_dimensions()` is unchanged |
| `xdg_toplevel.close` | hides the view and quits the main loop |
| Selection | `source/wayland/display.c:1917-1929` — layer shell, else xdg, else fail with a clear message |
| `late_setup` | branches at `:1962`; the xdg arm creates surface + toplevel, sets title/app_id |
| `display_set_surface_dimensions` | returns early at `:2091` for xdg after recording the size and setting window geometry — it does **not** pretend anchors/margins applied |
| `set_fullscreen_mode` | `xdg_toplevel_set_fullscreen()` vs the layer-shell exclusive-zone path |
| `wayland_surface_destroy` | tears down toplevel then xdg_surface, reverse of creation |

### The screen-size problem, and how it is handled

The layer-shell path learns the usable screen size from a trick: anchor to all four corners
with size 0 and read the resulting configure (`source/wayland/display.c:1971-1980`).
xdg-shell has no equivalent — a toplevel is never told the screen size.

Without a fix, `layer_width` stays 0, `display_get_surface_dimensions()` returns FALSE, and
every consumer silently falls back to hardcoded 1920x1080. The xdg arm therefore seeds the
dimensions from the selected output, falling back to *any* output with a valid mode when
`config.monitor` matched nothing (`source/wayland/display.c:2015-2038`). Verified that
`wayland_output_done` commits `pending`→`current` (`:1466-1469`) and that
`wayland_display_setup` performs an explicit second roundtrip to wait for output information,
so the data is populated by the time `late_setup` runs.

### Protocol ordering verified

xdg-shell requires: create surface → create xdg_surface → create toplevel → commit **with no
buffer** → await configure → ack → only then attach a buffer. The existing tail of
`late_setup` (`wl_surface_commit` then `wl_display_roundtrip`) already provides exactly that
sequence, and the roundtrip is what delivers the first configure to the new handler.

### Documented, not faked

`README.md` gains a "Shell protocols on Wayland" section stating plainly that under
xdg-shell `location`/`anchor`/`x-offset`/`y-offset` have no effect, keyboard interactivity
cannot be forced, `click-to-exit` cannot capture outside clicks, and `wayland-layer` is
ignored. Making anchors *appear* to work would be worse than the limitation.

### Notes for later phases

- Two new brand literals introduced: `xdg_toplevel_set_title(..., "rofi")` and
  `..._set_app_id(..., "rofi")` (`source/wayland/display.c:2012-2013`). Left as `"rofi"`
  deliberately, for consistency with the layer-shell namespace at `:1978` which is also
  still `"rofi"`. **Verified both are caught by the Phase 3d grep invariant**
  (`git grep -n '"rofi' -- source/ config/`). The app_id must end up matching
  `StartupWMClass` in the desktop file, same constraint as R5.
- xdg-shell does not solve window mode on KWin/Mutter: that needs
  `zwlr_foreign_toplevel_management_v1`, which neither implements. Window mode remains
  wlroots-only. Only the launcher itself gains portability here.

### Not verified

**CORRECTED 2026-08-22 20:58 — this claim was wrong.** A wlroots compositor *is* running on
this machine (`WAYLAND_DISPLAY=wayland-0`, `XDG_RUNTIME_DIR=/var/run/xdg/orpheus497`,
`xdg-desktop-portal-wlr` live). It was never checked; the assertion was assumed and then
repeated. See the 20:58 entry for what live testing actually showed.

The layer-shell path is now verified live. The **xdg-shell fallback is still unexercised**,
because this compositor advertises `zwlr_layer_shell_v1` and the code therefore correctly
prefers it. That specific path needs Mutter or KWin.

---

## 2026-08-22 19:37 — Phase 2a complete: Wayland / layer-shell correctness

All 13 items done. Clean rebuild, **19/19 tests pass**, all 12 sofi tests pass under ASAN,
**zero project-code compiler warnings**.

### Startup path — sofi no longer aborts on unsupported compositors

| Site | Change |
|---|---|
| `source/wayland/display.c:1490` | seat below min version: `g_error` → `g_warning` + skip that global only |
| `source/wayland/display.c:1505` | output below min version: same |
| `source/wayland/display.c:1833` | missing compositor/shm/output/seat: `g_error` → `g_warning`, and the message now names *which* one is missing |
| `source/wayland/display.c:1840` | missing layer shell: `g_error` → `g_warning`, returns FALSE |
| `source/wayland/display.c:1860` | `late_setup()` now re-checks `compositor`/`layer_shell`, checks the surface, and returns FALSE honestly instead of unconditional TRUE |

`source/rofi.c:1280-1298` already contained the friendly "No valid backend was found"
message; it was unreachable because `g_error()` calls `abort()`. It now runs. Same for
`-h`/`--help`, which sits after `display_setup()` at `source/rofi.c:1264` and was therefore
also unreachable on a compositor without layer shell.

### Buffer pool

- **Fixed shm name removed.** New `wayland_shm_alloc_fd()` helper uses `SHM_ANON` on FreeBSD
  (the native anonymous mechanism, no name to collide on) and a per-pid unique name with
  retry + immediate unlink elsewhere. This is the one place in the tree where a
  platform conditional is genuinely warranted.
- **Overflow guards** on `stride * height`, on `size * buffer_count`, and against
  `INT32_MAX` — `wl_shm_pool_create_buffer` takes the size and per-buffer offsets as
  `int32_t`, so an oversized pool would have been silently truncated into a wrong offset.
- Rejects non-positive dimensions up front.
- `display_buffer_pool_get_next_buffer()` NULL-guards its argument, and the sole caller
  (`source/wayland/view.c:428`) now checks `display_buffer_pool_new()` and skips the frame
  with a warning instead of dereferencing NULL.
- `close()` → `g_close()` on the mmap failure path for consistency.

### Input and lifecycle

- **Key-repeat use-after-free eliminated by design change.** `wayland_seat.repeat.source`
  was a borrowed `GSource *` from `g_main_context_find_source_by_id()`; GLib destroys and
  unrefs the source when a callback returns `G_SOURCE_REMOVE`, leaving it dangling for the
  four later `g_source_destroy()` sites. Field is now `guint source_id`
  (`include/wayland-internal.h:97-103`), cleared on every `G_SOURCE_REMOVE` path, cancelled
  through a new `wayland_key_repeat_cancel()` helper.
- **`repeat_info` rate == 0 now means disabled**, per the protocol. Previously fell through
  to a 30 ms default and repeated at ~33 Hz. Also guards a division that could yield 0 ms.
- **Keymap handler leaks fixed** (`source/wayland/display.c:436`). Every path now
  `munmap()`s the mapping and `close()`s the fd — both leaked on *every* keymap event.
  Additionally `nk_bindings_seat_update_keymap()` takes its own references
  (`xkb_keymap_ref`/`xkb_state_ref` at `subprojects/libnkutils/bindings/src/bindings.c:1005-1006`),
  so the local keymap and state are now unreffed too; they leaked as well. Error paths
  cleaned up. `fprintf(stderr, ...)` → `g_warning` to match the rest of the backend.
- **Seat capability bug** (`source/wayland/display.c:1223`): the branch releasing the
  keyboard tested `WL_SEAT_CAPABILITY_POINTER` instead of `WL_SEAT_CAPABILITY_KEYBOARD`.

### Sizing

- `wayland_rofi_view_calculate_window_width()` and the fullscreen branch of
  `..._calculate_window_height()` now read the **cached output size** via
  `wayland_rofi_view_get_current_monitor()` rather than the live layer-surface size.
  `display_set_surface_dimensions()` overwrites `layer_width`/`layer_height` with the *menu*
  size on every resize, so with `-no-click-to-exit` each successive view was sized as a
  percentage of the previous menu — halving every time.
- `rofi_get_offset_px()` seeds the theme lookup with `config.x_offset` / `config.y_offset`,
  matching `source/xcb/view.c:358-361`. `-x-offset` and `-y-offset` were silently discarded
  on Wayland.

### Window mode

- Both `g_list_nth_data()` results in `wayland_window_mode_result()` are NULL-checked and
  return `RELOAD_DIALOG`. The list shrinks synchronously on window close while the view
  reload is coalesced into a ~66 ms timer, so the highlighted row can outlive its entry.
- `pd->wayland->last_seat` NULL-checked before activation.
- `wlr_foreign_toplevel_manager_finished()` now clears `pd->manager`. It destroyed the proxy
  but left the pointer set, so teardown called `_stop()` on freed memory.
- `wayland_window_private_free()` reordered: stop the manager and drain the queue *before*
  freeing the toplevel list, not after.
- `get_wayland_window()` returns `gboolean` and `wayland_window_mode_init()` propagates it.
  Previously a compositor without wlr-foreign-toplevel-management got a warning plus a
  permanently empty list; now `source/rofi.c:202-210` shows a proper error dialog.

### Verified invariants

```
git grep -nE 'g_error\('     -- source/wayland source/modes/wayland-window.c  → none
git grep -n 'rofi-wayland-surface' -- source/                                 → none
git grep -nE 'repeat\.source[^_]|find_source_by_id' -- source/wayland         → none
```

### Incidental correction

`g_clear_handle_id()` was used first, then replaced: it expands to `_Static_assert`, which is
C11, and this project builds `c_std=c99` (`meson.build:6`). It produced
`-Wc11-extensions` warnings. Replaced with an explicit `wayland_key_repeat_cancel()` helper.

### Not changed, deliberately

`wlr_toplevels_set_one_identifier()` (`source/modes/wayland-window.c:143`) is a `do/while`
that would dereference NULL on an empty list. Left alone: the only caller
(`source/modes/wayland-window.c:589`) is reached from a toplevel that lives in that list, so
it cannot be empty. The correlation heuristic's fragility is already documented in the code
at `:581-588` and remains a known design limitation, not a defect to patch blindly.

---

## 2026-08-22 19:26 — Phase 0 and Phase 1 complete; Q7/Q13 deletions executed

### Phase 0 — Baseline: GREEN

`devel/bison` (GNU Bison 3.8.2) and `check` (0.15.2) were installed by the USER, clearing the
blocker. Baseline established on FreeBSD 15.1-RELEASE:

| Step | Result |
|---|---|
| `meson setup build` | OK — 62 targets |
| `ninja -C build` | OK |
| `meson test -C build` | **19/19 pass** |

**Warning audit.** 787 compiler warnings, but every one is in generated or third-party code:
761 `-Wgnu-zero-variadic-macro-arguments` from `/usr/local/include/check.h`, and 24 from
flex-generated `theme-lexer.c` (`-Wunreachable-code`, `-Wmisleading-indentation`).
**The hand-written C compiles warning-free at `warning_level=3`** with `-Wshadow`,
`-Werror=missing-prototypes` and the rest of the flag set. That is a better starting position
than the audit assumed.

### Phase 1 — Fix what the rebrand would cement: COMPLETE

All five items done. Build clean, 19/19 tests pass, and all 12 sofi tests pass under ASAN.

| # | File | Change | Verified by |
|---|---|---|---|
| 1 | `source/helper.c:1354-1372` | `utf8_strncmp` clamps truncation to each string's own normalized length; also guards `g_utf8_normalize` returning NULL on invalid UTF-8 | ASAN: overflow reproduced, then gone |
| 2 | `include/widgets/textbox.h:63` | `short cursor` → `int cursor` | build + textbox test |
| 3 | `source/rofi.c:847` | `g_warning(str, NULL)` → `g_warning("%s", str)`; dead commented-out `fputs` pair removed | build |
| 4 | `include/settings.h:113-119`, `config/config.c:79-86`, `source/helper.c:741-746` | `WindowLocation location` → `unsigned int location`, documented as a 0-8 position index; range check and format specifier corrected for the unsigned type | build + tests |
| 5 | `include/mode.h:35-39` | `ABI_VERSION` 7u → 8u | build |
| — | `meson.build:30-31` | added `-Werror=format-security` and `-Wformat=2` | build |

**ASAN reproduction of finding #1, before the fix:**

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 1 at 0x502000001434
    #0 utf8_strncmp source/helper.c:1358:36
    #1 main test/helper-test.c:185:5
0x502000001434 is located 2 bytes after 2-byte region
```

After the fix the same binary exits 0 with no ASAN report. This is the first finding in the
register to be independently reproduced *and* closed.

**Two corrections to the audit's own framing**, recorded so the register is not trusted
blindly:
- `include/widgets/textbox.h` and `include/settings.h` are **not** in the installed header
  set (`meson.build:195-203` installs only `mode.h`, `mode-private.h`, `helper.h`,
  `rofi-types.h`, `rofi-icon-fetcher.h`). Findings 2 and 4 are memory-safety and type-safety
  fixes, not plugin-ABI concerns. The plan's Phase 1 rationale overstated this.
- The `WindowLocation` enum in the installed `rofi-types.h` is correct as a bitmask. The
  defect was only the *field* in `settings.h` that misused it.

**Incidental improvement found while fixing #4:** `source/xrmoptions.c:61` declares the
option-table union member as `unsigned int *num`, so before this change the table was
aliasing a `WindowLocation` enum through an `unsigned int *`. Now correctly typed.

### Q7 / Q13 rulings executed

| Action | Scope |
|---|---|
| Removed frozen per-version doc trees | `mkdocs/docs/1.7.0` … `1.7.9`, `mkdocs/docs/2.0.0` — 71 files, ~7.9 MB |
| Removed upstream `Changelog` | nothing in the tree referenced it |
| Removed `.gitlab-ci.yml` | pure autotools against a tree with no `configure.ac`; failed 100% of the time |
| Trimmed `mkdocs/mkdocs.yml` nav | dropped the 12 per-version sections; `Current`, `Guides` and top-level pages retained |
| Dropped `.gitattributes:6` | the now-dead `.gitlab-ci.yml export-ignore` rule |
| Added `/subprojects/.wraplock` to `.gitignore` | meson ≥1.10 in-tree artifact |

Verified: every remaining nav target resolves, and `git grep` finds no dangling references to
any deleted version directory. Build and tests still green afterwards.

### NEW FINDING — not in the original register

**Heap-buffer-overflow in the vendored libnkutils, exposed by its own test suite.**

```
WRITE of size 8 at 0x50300000d9e0
    #0 _nk_format_string_parse subprojects/libnkutils/core/src/format-string.c:690:36
    #1 nk_format_string_parse  subprojects/libnkutils/core/src/format-string.c:740:12
0 bytes after a 32-byte region allocated at format-string.c:670
```

Pre-existing and unrelated to any change in this session — `subprojects/` was never touched.
**Not reachable from sofi:** the only nkutils APIs sofi calls are `nk_bindings_*` and
`nk_xdg_theme_*` (verified by grep over `source/` and `include/`); `nk_format_string_*` is
never called, and `meson.build:181-184` enables only `bindings=true`.

Consequence: `meson test -C build-asan` reports 18/19 rather than 19/19. Recorded so the
discrepancy is not mistaken for a sofi regression later. Filed as backlog item B7.

---

## 2026-08-22 18:55 — Phase 1 Initialization complete

**Completed**

- Read the full tree: 408 tracked files, ~42,400 lines of C, all root documentation,
  the meson build, both display backends, all modes, the theme engine, and CI config.
- Ran a 21-agent read-only audit: 10 domain surveys, 10 adversarial verifiers, 1 synthesis.
  263 raw findings → 15 refuted → **248 retained**. 166 identity surfaces catalogued.
  (The synthesis agent hit a session limit; the synthesis was completed directly.)
- Established the baseline build state on this host (FreeBSD 15.1-RELEASE): **does not
  configure** — GNU Bison absent, `check` absent.
- Generated the `.devdocs/` workspace: BRIEFING, PROGRESS, SESSION_HANDOFF, DECISIONS_LOG,
  TODOS, PLANS, BLUEPRINT, plus AUDIT_REGISTER and REBRAND_SURFACES.

**Not done — deliberately**

- Zero product-code modifications. Per the Zero Unapproved Action directive, the audit was
  strictly read-only and no rename, fix, or refactor has been applied.
- No system packages installed.

**Findings by severity**

| Severity | Count |
|---|---|
| Critical | 0 |
| High | 12 |
| Medium | 59 |
| Low | 177 |
| **Total** | **248** |

**Findings by kind**

| Kind | Count |
|---|---|
| correctness | 95 |
| memory | 71 |
| structure | 29 |
| build | 21 |
| portability | 14 |
| docs | 11 |
| protocol | 7 |

**Rebrand surfaces by blast radius**

| Risk class | Count |
|---|---|
| Breaks third-party plugins | 35 |
| Breaks existing user config | 28 |
| Breaks distribution packaging | 13 |
| User-visible but safe | 37 |
| Internal only | 53 |
| **Total** | **166** |

**Rename scale**

| Measure | Count |
|---|---|
| `rofi` occurrences, whole tree (case-insensitive) | 6,035 lowercase + 751 `Rofi` + 741 `ROFI` |
| Excluding frozen historical docs | 3,912 |
| In C code (`source` `include` `lexer` `config` `test`) | 2,284 + 468 `Rofi` + 516 `ROFI` |
| Distinct identifiers to rename | 335 |
| Files requiring rename | 30 live, ~90 frozen |

**Superseded / archived**

Nothing yet — this is the first recorded session.
