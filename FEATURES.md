# Sofi — features, functions and usage

Reference for **Sofi**, the Sakura Official Full Indexer: the UI display and
layer-shell layer of the [hikari-sakura](https://github.com/orpheus497/hikari-sakura)
Wayland compositor.

## How to read this, and what else to read

Sofi's documentation has two indexes into the same material. Use whichever
matches the question you have:

| You want | Read |
|---|---|
| **"What can this thing do, and how do I use it?"** | **This document** — reference by capability |
| "What does flag `-x` do?" | [sofi(1)](doc/sofi.1.markdown) — reference by flag, exhaustive |
| "How do I change the colours / move a panel?" | [CONFIG.md](CONFIG.md), then [sofi-customisation(5)](doc/sofi-customisation.5.markdown) |
| "How do I build it?" | [INSTALL.md](INSTALL.md) |
| An overview and the project's identity | [README.md](README.md) |

Where this document and a manpage disagree, **the manpage is right** — it is the
more closely maintained reference. Please report the disagreement either way.

---

## Contents

- [1. The model](#1-the-model)
- [2. The system surfaces](#2-the-system-surfaces)
- [3. The general-purpose modes](#3-the-general-purpose-modes)
- [4. Matching, sorting and selection](#4-matching-sorting-and-selection)
- [5. Keyboard and mouse bindings](#5-keyboard-and-mouse-bindings)
- [6. The daemons and their interfaces](#6-the-daemons-and-their-interfaces)
- [7. Theming and layout](#7-theming-and-layout)
- [8. Scripting and extension](#8-scripting-and-extension)
- [9. Backends and build options](#9-backends-and-build-options)
- [10. Files, paths and environment](#10-files-paths-and-environment)
- [11. Diagnostics](#11-diagnostics)
- [12. Known limits](#12-known-limits)

---

## 1. The model

Three concepts, and everything else follows from them.

**An index** is a list sofi builds from somewhere in the system: desktop files,
`$PATH`, the compositor's toplevels, the session bus, the filesystem.

**A mode** is the code that builds one index, decides how a row is displayed, and
decides what happens when you pick one. `drun` is a mode; so is `window`.

**A surface** is where a mode is drawn — position, size, colours, which widgets
are present. Sofi compiles in six layouts and picks one from the invocation, so
the same `drun` mode can render as the application menu without a configuration
file existing.

```
   an index          a mode                a surface
   ────────          ──────                ─────────
   desktop files  →  drun               →  application menu, bottom centre
   $PATH          →  run                →  (falls through to the same layout)
   toplevels      →  window             →  task strip, along the bottom
   hikari socket  →  sheets             →  chip row, under the top bar
   session bus    →  notifications      →  banner stack, bottom right
                     notification-history  history panel, right edge
   SNI watcher    →  (the tray zone)    →  inside the task strip
                     tray-menu          →  replaces the strip's list, in place
```

**Two invariants worth knowing up front:**

1. **Each surface holds its own instance lock**, a pidfile named
   `sofi-<surface>.pid` under `$XDG_RUNTIME_DIR`. That is what lets the
   application menu and the task strip be on screen simultaneously. A session-wide
   lock would make them mutually exclusive.
2. **Your configuration file beats the compiled-in layout.** The layout is parsed
   *before* `~/.config/sofi/config.sasi`, and later sources override earlier ones
   property by property. Only the command line outranks your config file:
   `-theme-str` merges over it, and `-theme` replaces the whole theme, palette
   included. Full order in [7.2](#72-overriding).

---

## 2. The system surfaces

Five system surfaces across seven invocations. Every one works with no
configuration file.

| Surface | Invocation | Layout resource | Lock | Default placement |
|---|---|---|---|---|
| Application menu | `sofi -show drun` | `/org/sofi/default.sasi` | `sofi-menu.pid` | south centre, 560 wide × 62% |
| Task / window manager | `sofi -show window` | `/org/sofi/panel-window.sasi` | `sofi-window.pid` | south, 98% wide, 12px inset |
| Sheet switcher | `sofi -show sheets` | `/org/sofi/panel-sheets.sasi` | `sofi-sheets.pid` | north centre, 720 wide |
| Notification banner | `sofi -notification-daemon` | `/org/sofi/panel-notifications.sasi` | `sofi-notifyd.pid` | south east, 400 wide |
| Notification history | `sofi -show notification-history` | `/org/sofi/panel-notification-history.sasi` | `sofi-notification-history.pid` | east, 420 wide × 76% |
| System tray host | `sofi -tray-daemon` | *none — no surface* | bus name | inside the task strip |
| Message toast | `sofi -e <msg>` | `/org/sofi/panel-notify.sasi` | `sofi-notify.pid` | north east, 380 × 55 |

### 2.1 Application menu

```bash
sofi -show drun
```

Indexes XDG desktop files. Two-tier rows — application name, then generic name
beside it in a lighter weight — with icons on.

**This is the fallthrough layout.** Any mode that is not one of the four with a
layout of its own gets this one, so `run`, `ssh`, `combi`, `filebrowser`,
`recursivebrowser` and your own script modes all look consistent without
configuring anything.

| Key | Does |
|---|---|
| `Enter` | Launch |
| `Shift`+`Enter` | Launch in a terminal |
| `Ctrl`+`Enter` | Run the typed text as a command instead of the selection |
| `Ctrl`+`Tab` / `Ctrl`+`Shift`+`Tab` | Next / previous mode |
| `Escape` | Dismiss |

Useful options: `-drun-match-fields`, `-drun-display-format`,
`-drun-categories`, `-drun-exclude-categories`, `-drun-show-actions`,
`-drun-url-launcher`. See [sofi(1)](doc/sofi.1.markdown).

### 2.2 Task and window manager

```bash
sofi -show window
```

A strip anchored near the south edge, inset from the screen edges so it reads as
a floating bar. Three zones: a filter field, the task list, and the **system tray**
in the right-hand corner.

Rows **lead with the window title** and demote the application class to dim small
text, because the title is what distinguishes two windows of the same
application.

| Key | Does | Backend |
|---|---|---|
| `Enter` | Focus the window | both |
| `kb-custom-1` (`Alt`+`1`) | Toggle **minimise** | Wayland |
| `kb-custom-2` (`Alt`+`2`) | Toggle **maximise** | Wayland |
| `kb-accept-alt` (`Shift`+`Return`) | Run `-window-command` on the selection | both |
| `kb-delete-entry` (`Shift`+`Delete`) | **Close the window** | both |

Closing a window dismisses the strip by default. Set `close-on-delete: false;` on
the mode's widget to close several in a row without the strip going away.

**Minimised windows are surfaced through the `URGENT` display state**, so a theme
can style them distinctly. On hikari-sakura this is doing double duty and it is
worth knowing why: hiding a view when you switch sheets is published as
foreign-toplevel's minimised bit, so *"minimised"* there means **"not on the sheet
you are looking at"**. Sheet 0's views are never hidden — that asymmetry is the
compositor's semantics, not a bug.

On hikari-sakura, maximise maps onto full-maximize; there is no separate
fullscreen state.

**On X11** the mode is `window`, plus `windowcd` for windows on the current
desktop only. It needs an EWMH-compatible window manager, with workarounds
present for i3 and bspwm. **On Wayland** it needs
`zwlr_foreign_toplevel_management_v1`; KWin and Mutter implement neither wlr
protocol, so the window mode does not work there.

Useful options: `-window-format`, `-window-match-fields`, `-window-command`,
`-window-thumbnail`.

### 2.3 Sheet switcher

```bash
sofi -show sheets
```

A horizontal row of **ten chips** centred under the compositor's top bar. Each
chip is a sheet number and its window count; empty sheets are dimmed and the
current sheet is filled.

| Key | Does |
|---|---|
| `Enter` | Switch to the highlighted sheet |
| `kb-custom-1` (`Alt`+`1`) | **Send the focused window** to the highlighted sheet |

The row is a fixed ten-cell grid rather than a content-sized strip, deliberately:
a content-sized row would shift every chip to the right of any sheet whose window
count reached two digits, so the chips would not be in the same place twice.

**This mode does not use a Wayland protocol.** It speaks to hikari's control
socket at `$XDG_RUNTIME_DIR/hikari.sock`, because **no standards-track protocol
can express send-to-sheet** — see [6.3](#63-the-compositor-control-socket). On any
other compositor the mode reports the socket is absent and **exits cleanly rather
than aborting**.

### 2.4 Notifications

Two processes and two surfaces over one ring buffer.

#### The daemon

```bash
sofi -notification-daemon
```

Owns `org.freedesktop.Notifications` on the session bus. Runs until killed, and
**idles with no surface mapped** — it maps one when a notification arrives and
unmaps when the last one goes, so it costs nothing while the desktop is quiet.

- Notifications are kept in a **ring buffer**, persisted to
  `$XDG_CACHE_HOME/sofi/notifications.history`.
- **`urgency=2` (critical) never expires on its own.** Everything else honours
  the sender's timeout, with `-1` meaning the server default and `0` meaning
  never.
- `replaces_id` updates in place rather than stacking a second banner.
- Body markup is validated with `pango_parse_markup` and escaped if it does not
  parse, so a malformed payload degrades to literal text rather than breaking the
  layout.
- Icons arrive as `app_icon`, `image-path` or raw `image-data`; the raw path is
  **validated before a pixel is read** — `rowstride × height` against the array
  length, with dimensions capped and oversized images refused rather than scaled.

Notifications look deliberately unlike the menus — **cards with a leading urgency
stripe**, rather than rows with a filled selection — so a banner is never mistaken
for something you are about to launch.

| Key on the banner | Does |
|---|---|
| `Enter` | Run the default action if there is one, otherwise acknowledge |
| `kb-custom-1` | **Dismiss all** — also on a clickable button |
| `kb-custom-2` … | Invoke the 2nd, 3rd … action of the highlighted entry |

> The banner surface is forced to take **no keyboard** (it would otherwise steal
> focus from whatever you are typing into). Pointer input is unaffected by
> keyboard interactivity, which is why the dismiss-all button exists: it is the
> only way to reach `kb-custom-1` on this surface.

#### The history panel

```bash
sofi -show notification-history
```

An ordinary one-shot invocation over the same ring, on the east edge.

**`Enter` tries three things in order:**

1. **Run the notification's default action** — if it is still on screen and
   offered one. The application said what `Enter` should mean, and that beats
   anything sofi could infer.
2. **Raise the window of the application that sent it.** This is the thing a
   history list is for that a banner is not: seeing something from an hour ago
   and wanting to go and deal with it. It works on **retired** entries too — that
   is the case that needs it most. It requires the sender to have set the
   `desktop-entry` hint, and **matching is exact or reversed-DNS-tail only**:
   when nothing matches, nothing is raised. Raising the wrong window is worse
   than raising none, because the user asked to be taken somewhere and would be
   taken somewhere else.
3. **Acknowledge it**, if it is still on screen, and **leave the panel open** —
   going through a list of missed notifications means going through it.

An entry that is already retired and has no window left simply closes the panel.

| Key | Does |
|---|---|
| `Enter` | The three-step sequence above |
| `Shift`+`Delete` | Retire one entry, keeping it in history |
| `kb-custom-1` | **Dismiss all** — retire what is on screen, keep the record |
| `kb-custom-2` | **Clear history** — destroy the record |

Both cleanup verbs are also clickable buttons.

**Dismiss is hidden when no daemon is reachable**: with nothing on screen it has
nothing to retire. The button is not greyed out — a disabled widget is neither
drawn nor given space, so it is simply absent. **Clear is always available**,
because discarding stored history genuinely does not need a daemon.

#### Clearing from a script

```bash
sofi -notification-clear           # dismiss what is on screen, keep history
sofi -notification-clear-history   # discard everything
```

Two separate verbs on purpose: clearing banners off your screen should not also
lose the list of what you missed. Both exit non-zero and change nothing when no
daemon answers, and neither will ever auto-start a daemon — clearing a list must
not leave behind a service you did not ask for.

### 2.5 System tray

```bash
sofi -tray-daemon
```

Owns `org.kde.StatusNotifierWatcher` and collects the tray items applications
publish. **It has no surface of its own** — icons render in the task strip's
right-hand corner, and follow along while the strip is open, so an application
starting or changing its icon appears without reopening anything.

**Four operational facts:**

- **Start it before the applications whose icons you want.** A tray application
  asks **once**, at its own startup, whether a host exists. One that finds none
  shows no icon and never asks again — so a host started later means restarting
  those applications. This single property is the difference between a tray and
  an empty strip.
- **Restart it after upgrading sofi.** `org.sofi.Tray` is private between two
  sofi processes and changes with the code. An older daemon serves a shape the
  new strip cannot read; you get an empty tray zone and a warning saying exactly
  that. The applications do not need restarting — they watch for the watcher and
  re-register.
- **It needs no display.** StatusNotifierItem is D-Bus and nothing else: no
  protocol, no surface, no input routing. It runs with no Wayland or X11 session
  at all, and is dispatched before display selection for that reason.
- **It is a separate process from the notification daemon**, deliberately. Tray
  code parses hostile input from arbitrary applications; notifications matter
  more; one main loop would mean one crash takes both.

#### Tray menus

Clicking an icon opens **that application's menu, inside the strip**. The window
list is replaced while the menu is up; `Escape` or choosing an entry closes it.
Submenus open in place with a `..` row to go back, the way the file browser
descends into directories.

| Button | Does | Binding |
|---|---|---|
| Left | The item's menu, or `Activate` when it published none | `mt-activate` |
| Right | The same menu, or the item's own `ContextMenu` when it published none | `mt-context-menu` |
| Middle | `SecondaryActivate` | `mt-secondary-activate` |

All three are rebindable, in a scope of the tray's own.

**Why sofi draws the menu rather than the application.** Under
StatusNotifierItem an application publishes a *description* of its menu over
`com.canonical.dbusmenu` — labels, separators, toggles, which rows open submenus
— and **there is no method in that protocol that asks it to display anything**.
Rendering is the host's job. That is the deliberate break from the old X11 tray,
where an application embedded a window and drew its own menu; moving the menu out
of the application's process is what lets the panel theme it. For most tray
applications the menu exists only as data until something draws it.

Sofi reads `com.canonical.dbusmenu` over GDBus and **does not link
`libdbusmenu`** — it is a wire format here, not a dependency.

### 2.6 Message toast

```bash
sofi -e "some message"
```

A one-shot dialog in the top-right corner for scripts. Unrelated to the
notification daemon: no bus, no history, no ring. It dismisses itself on a timer
as well as on a key.

---

## 3. The general-purpose modes

Sofi keeps every mode it inherited, so it is usable as a standalone launcher on
any compositor or window manager. Run `sofi -h` to see what your binary offers.

| Mode | Indexes | Notes |
|---|---|---|
| `drun` | Applications, from XDG desktop files | The application menu. `-Ddrun` |
| `run` | Executables on `$PATH` | `Shift`+`Enter` runs in a terminal |
| `window` | Windows | X11/EWMH or Wayland/wlr. `-Dwindow` |
| `windowcd` | Windows on the current desktop | **X11 only** |
| `sheets` | hikari-sakura sheets 0–9 | `-Dsheets`; needs the socket |
| `notifications` | The live notification stack | `-Dnotify` |
| `notification-history` | Notifications already shown | `-Dnotify` |
| `tray-menu` | One tray item's dbusmenu tree | `-Dtray`; switched into, not summoned |
| `ssh` | Hosts from SSH config and known-hosts | Parses `Include` directives |
| `filebrowser` | Files in one directory | |
| `recursivebrowser` | Files, descending into directories | |
| `combi` | Several modes merged into one list | `-combi-modes` |
| `keys` | Sofi's own keybindings | Handy when you have rebound things |
| `script` | Whatever your script prints | See [8.1](#81-script-modes) |
| `dmenu` | Whatever you pipe in | `-dmenu`, not `-show dmenu` |

**Combining modes.** `-modes` sets which are enabled and reachable with
`Ctrl`+`Tab`; `-combi-modes` sets which are merged into a single list by the
`combi` mode:

```bash
sofi -modes "run,ssh" -show run
sofi -show combi -combi-modes "window,run,ssh" -modes combi
```

---

## 4. Matching, sorting and selection

### 4.1 Matching methods

Set with `-matching <method>`:

| Method | Behaviour |
|---|---|
| `normal` | Substring — **the default** |
| `regex` | Each token is a regular expression |
| `glob` | Each token is a glob pattern |
| `fuzzy` | Characters in order, gaps allowed |
| `prefix` | Each token must match the start of a word |

Switch method **at runtime** with `kb-matcher-up` / `kb-matcher-down`
(`Super+equal` / `Super+minus`) — useful when a `normal` search is returning too
much and `prefix` would cut it down.

**Tokenized** (`-tokenize`, on by default) means what it says: type any words in
any order, and every one must match somewhere. `term fire` finds
*Firefox — Terminal* as readily as `fire term`.

| Option | Effect |
|---|---|
| `-case-sensitive` / `-no-case-sensitive` | Case sensitivity, off by default |
| `-case-smart` | SmartCase — a lowercase pattern is insensitive; any uppercase character makes it sensitive |
| `-tokenize` / `-no-tokenize` | Treat the input as separate tokens, or as one string |
| `-matching-negate-char <c>` | Prefix a token with this character to **exclude** matches. `'\0'` disables it |
| `-normalize-match` | Match with accents normalised away — note this **disables match highlighting** |
| `-threads <n>` | Threads used for matching |

Case sensitivity is also togglable at runtime with `kb-toggle-case-sensitivity`
(`` ` ``).

### 4.2 Sorting

`-sort` enables sorting of matches; `-sorting-method` picks how:

| Method | Behaviour |
|---|---|
| `normal` | Levenshtein distance — **the default**. `levenshtein` is accepted as a synonym |
| `fzf` | A fuzzy scorer loosely inspired by fzf |
| `fzf-v2` | A port of fzf's own FuzzyMatchV2, aiming to match fzf's ranking. Prefers contiguous matches and word-boundary/camelCase starts, using fzf's weights; whitespace-separated terms are scored independently and summed |

Toggle at runtime with `kb-toggle-sort`.

### 4.3 History

`run`, `ssh`, `drun` and `filebrowser` each keep a use-ordered history, and the
**last 25 choices float to the top**. Disable with `-disable-history`; exclude
programs with `-ignored-prefixes`.

Separately, the *input field* keeps an entry history, navigable with
`kb-entry-history-up` / `-down`.

### 4.4 Selection

- `-auto-select` — accept immediately when exactly one row matches.
- `-select <string>` — preselect the first row matching a string.
- `-selected-row <n>` — preselect by index.
- Multi-select in dmenu mode, with configurable ballot strings.

---

## 5. Keyboard and mouse bindings

Full list with defaults: [sofi-keys(5)](doc/sofi-keys.5.markdown), or at runtime:

```bash
sofi -list-keybindings
sofi -show keys
```

Rebind by name, in configuration or on the command line:

```css
configuration {
    kb-row-down: "Down,Control+j";
    kb-cancel:   "Escape,Control+bracketleft";
}
```

```bash
sofi -kb-row-down "Down,Control+j" -show drun
```

### 5.1 The bindings that matter most

Defaults below are as `sofi -no-config -list-keybindings` reports them. Several
are not what a reader familiar with other launchers would guess — `kb-row-left`
is not `Left`, and row selection is on `Super`, not `Alt`.

| Binding | Default | Does |
|---|---|---|
| `kb-accept-entry` | `Return`, `KP_Enter`, `Control+j`, `Control+m` | Accept the selection |
| `kb-accept-alt` | `Shift+Return` | Accept, alternate action (e.g. run in terminal) |
| `kb-accept-custom` | `Control+Return` | Accept the *typed text*, not the selection |
| `kb-accept-custom-alt` | `Control+Shift+Return` | Typed text, alternate action |
| `kb-cancel` | `Escape`, `Control+g`, `Control+bracketleft`, `MouseSecondary` | Dismiss |
| `kb-row-up` / `kb-row-down` | `Up` / `Down`, `Control+p` / `Control+n` | Move the selection |
| `kb-row-left` / `kb-row-right` | `Control+Page_Up` / `Control+Page_Down` | Move a column in a grid layout |
| `kb-page-prev` / `kb-page-next` | `Page_Up` / `Page_Down` | Page the list |
| `kb-row-first` / `kb-row-last` | `Home` / `End` | Jump to the ends |
| `kb-element-next` / `kb-element-prev` | `Tab` / `ISO_Left_Tab` | Next / previous element |
| `kb-row-select` | `Control+space` | Select without accepting |
| `kb-mode-next` / `kb-mode-previous` | `Shift+Right`, `Control+Tab` / `Shift+Left`, `Control+ISO_Left_Tab` | Switch mode |
| `kb-mode-complete` | `Control+l` | Complete to the current mode |
| `kb-delete-entry` | `Shift+Delete` | Delete the selection — history entry, window, notification |
| `kb-toggle-case-sensitivity` | `grave`, `dead_grave` | Toggle case sensitivity |
| `kb-toggle-sort` | `Alt+grave` | Toggle sorting |
| `kb-matcher-up` / `kb-matcher-down` | `Super+equal` / `Super+minus` | **Change matching method at runtime** |
| `kb-entry-history-up` / `-down` | `Control+Up` / `Control+Down` | Recall previously typed input |
| `kb-screenshot` | `Alt+S` | Write a PNG of the surface |
| `kb-ellipsize` | `Alt+period` | Toggle ellipsizing |
| `kb-select-1` … `kb-select-10` | `Super+1` … `Super+0` | Jump to row *n* |
| `kb-custom-1` … `kb-custom-19` | `Alt+1` … `Alt+9`, `Alt+0`, then shifted digits | Mode-specific; otherwise exit with code `10+N` |

Plus a full set of readline-style editing bindings on the input field —
`kb-move-word-back` (`Alt+b`), `kb-remove-to-eol` (`Control+k`),
`kb-transpose-chars` (`Control+t`), `kb-clear-line` (`Control+w`), and so on.

### 5.2 `kb-custom-N`, per mode

`kb-custom-N` means different things in different modes. Everywhere it is not
listed below, it keeps its general meaning: **exit with code `10+N`**, which is
how a script driving sofi reads a custom action.

| Mode | `kb-custom-1` | `kb-custom-2` | Higher |
|---|---|---|---|
| `window` (Wayland) | Toggle minimise | Toggle maximise | exit `10+N` |
| `sheets` | Send focused window to the sheet | — | exit `10+N` |
| `notifications` | Dismiss all | Invoke 2nd action | Invoke *N*th action |
| `notification-history` | Dismiss all | Clear history | exit `10+N` |

### 5.3 Mouse

| Binding | Default | Does |
|---|---|---|
| `me-select-entry` | `MousePrimary` | Select the row under the pointer |
| `me-accept-entry` | `MouseDPrimary` | Accept it (double click) |
| `me-accept-custom` | `Control+MouseDPrimary` | Accept typed text |
| `ml-row-up` / `ml-row-down` | `ScrollUp` / `ScrollDown` | Scroll the list |
| `ml-row-left` / `ml-row-right` | `ScrollLeft` / `ScrollRight` | Scroll horizontally |
| `mt-activate` | `MousePrimary` | **Tray:** menu, or `Activate` |
| `mt-context-menu` | `MouseSecondary` | **Tray:** menu, or `ContextMenu` |
| `mt-secondary-activate` | `MouseMiddle` | **Tray:** `SecondaryActivate` |

Note that `kb-cancel`'s default list includes `MouseSecondary` — a right-click
anywhere dismisses a panel. The tray is the one exception, below.

The `mt-*` bindings live in a scope of the tray's own, which is consulted
**before** the global scope. That is what stops a right-click over a tray icon
reaching `kb-cancel` and tearing the strip down — and right-click still cancels
everywhere else, because `kb-cancel`'s own default is untouched.

---

## 6. The daemons and their interfaces

### 6.1 `org.freedesktop.Notifications`

The standard interface, implemented by `sofi -notification-daemon`. Taken with
`REPLACE | DO_NOT_QUEUE`, and the daemon exits cleanly if it loses the name.
`GetCapabilities` declares **only** what is actually implemented.

### 6.2 `org.sofi.Notifications`

A second interface on the same object, for verbs the standard one has no place
for. It exists because **`sofi -show notification-history` is a separate process
from the daemon**: it reads the file the daemon persists, so a clear performed by
writing that file would be overwritten within seconds. The mutation has to happen
where the ring lives.

| Member | Purpose |
|---|---|
| `DismissAll()` | Retire the live set, keep the record |
| `ClearHistory()` | Destroy the record |
| `Dismiss(u id)` | Retire one entry |
| `InvokeAction(u id, u index)` | Emit `ActionInvoked` from the process owning the connection |
| `GetLive() → a(uus)` | id, action-pair count, `desktop-entry` |

**`live` and `actions` are never persisted, deliberately.** Both describe a
notification being on screen *right now*. They belong to the process that
received it, and a file asserting either is wrong the instant the daemon acts. The
history panel reads the record from disk and asks the daemon for the rest.

### 6.3 The compositor control socket

`$XDG_RUNTIME_DIR/hikari.sock`, mode 0600, `AF_UNIX`/`SOCK_STREAM`. Owned by
hikari-sakura, consumed by sofi's `sheets` mode. Bounded by design: 512-byte
requests, 8 concurrent clients, one exchange per connection.

| Request | Response |
|---|---|
| `state` | `sheet <n>` / `output <name>` / `counts <c0>…<c9>` / `END` |
| `sheet <0-9>` | `ok` or `error …` |
| `pin <0-9>` | `ok` or `error …` — move the focused view |

Unrecognised lines are ignored by the client, so the compositor can add fields
without breaking older sofi builds.

`pin` is **the operation no Wayland protocol expresses**, and is most of the
reason the socket exists at all.

### 6.4 `org.kde.StatusNotifierWatcher` and `org.sofi.Tray`

`sofi -tray-daemon` owns the watcher plus a `StatusNotifierHost-<pid>` name, and
serves `org.sofi.Tray` for the task strip to read.

| `org.sofi.Tray` member | Purpose |
|---|---|
| `ListItems() → a(sssssubuuay)` | service, title, icon name, icon theme path, menu path, status, is-menu, width, height, premultiplied ARGB32 pixels |
| `Activate(s,i,i)` / `SecondaryActivate` / `ContextMenu` | Forwarded to the application |
| `Changed` | Registry or property change, coalesced |

**`org.sofi.Tray` is private between two sofi processes and is not a public
API.** It changes with the code; that is why the daemon and the strip must be the
same build.

Three interoperability facts, each of which was a defect before it was a rule:

- **Both registration forms are accepted.** `RegisterStatusNotifierItem`'s
  argument is a bus name from Qt/KDE items and an object path from several GTK
  and Electron ones. The specification never pinned it down, and handling one
  form shows an empty tray for half the desktop with no diagnostic.
- **Items are reaped by `NameOwnerChanged`.** There is **no Unregister method in
  the specification at all** — an item exists exactly as long as its bus name
  does.
- **`ItemIsMenu` is advisory and is not the test for "has a menu".** An item whose
  entire interface *is* its menu may omit the property; whether the menu path is
  non-empty is the fact.

---

## 7. Theming and layout

**No theme has to be installed and none has to be chosen.** Sofi compiles in one
palette and six layouts, and your configuration *edits* them. Theme files are
installed — `config.sasi` and `colors-default.sasinc`, under
`$datadir/sofi/themes/` — but purely as an optional starting point to copy and
edit; nothing loads them unless you ask. See [README.md](README.md#themes) for
what copying them actually does.

### 7.1 The palette

Sixteen positional slots, then semantic aliases referencing them. The layouts use
only the aliases: **`doc/palette.sasi` is the single source of colour for every
surface, and none of the six layouts contains a colour value of its own.**

```css
color0  #2b1e3a   color8  #5e5966      /* base   / bright base   */
color1  #c96464   color9  #df8787      /* red    / bright red    */
color2  #df9f87   color10 #f2bda8      /* orange / bright orange */
color3  #e4b382   color11 #f5cf9e      /* yellow / bright yellow */
color4  #8e7cc3   color12 #aba0d9      /* violet / bright violet */
color5  #b18fc7   color13 #cfaedc      /* mauve  / bright mauve  */
color6  #9fa0a6   color14 #b8b9be      /* grey   / bright grey   */
color7  #d4d4d9   color15 #f0edf2      /* text   / bright text   */
```

| Alias | Slot | Contrast | Allowed use |
|---|---|---|---|
| `foreground-bright` | color15 | 13.41 | Emphasis |
| `warning` | color11 | 10.60 | Text or fill |
| `foreground` | color7 | 10.54 | Body text |
| `accent-strong` | color13 | 7.96 | Current sheet, live notification |
| `accent` | color12 | 6.49 | Selection |
| `foreground-dim` | color6 | 5.97 | Secondary text |
| `urgent` | color9 | 5.89 | Text or fill |
| `accent-soft` | color4 | 4.31 | **Large or bold text and non-text marks only** |
| `critical` | color1 | 4.07 | **Fills and stripes only, never text** |
| `hint` | derived | 3.66 | Placeholder only |
| `muted` | color8 | 2.29 | **Non-text only** — separators, troughs |

Contrast is computed against the `color0` ground, not eyeballed. Three tones are
constrained by measurement rather than taste, including `on-accent` being dark
because white on `color12` is 2.40:1.

**The same sixteen values are hikari-sakura's `ui { palette }`, byte for byte**,
and the aliases map onto its `ui { colorscheme }` slot for slot. The application
icon is drawn from them too.

### 7.2 Overriding

```css
/* ~/.config/sofi/config.sasi */

* {
    color0:  #1c1b22;     /* recolour everything */
    color12: #89b4fa;
    accent:  #f5c2e7;     /* or just one role */
}

window {
    location: west;       /* move or resize a panel */
    anchor:   west;
    width:    340px;
}
```

**Why redefining a name reaches every layout:** a `@name` reference is resolved
on first *lookup*, not at parse, and lookup happens at widget construction after
every source has been read. So the palette does not have to be parsed last to
win, and a `* { }` block in your config — parsed after both — overrides it for
every surface at once.

**Parse order**, later overriding earlier: default configuration → palette → the
one layout for the surface you invoked → `~/.config/sofi/config.sasi` → `-theme`
/ `-theme-str`.

> **The offset-sign trap.** The two positioning paths negate offsets relative to
> each other. A negative `y-offset` on the software-positioned path pushes a
> panel *outside* the usable area. See
> [sofi-customisation(5)](doc/sofi-customisation.5.markdown).

> **`location: north` is already flush under hikari's top bar.** Sofi's "screen"
> on Wayland is the compositor's *usable area* — the bar is subtracted before
> sofi sees a size. An offset there is a deliberate gap, never bar clearance.

### 7.3 Changing one surface only

A configuration file cannot know which surface it was loaded for, and it is
parsed *after* the layout — so its `window` and `listview` rules land on **every**
surface. To change one and no other, override on that invocation:

```bash
sofi -show drun -theme-str 'window { width: 640px; }'
```

`-theme-str` merges over the loaded layout. `-theme` **replaces the whole theme,
palette included**, so a file passed that way must stand on its own.

### 7.4 Icons

| Option | Effect |
|---|---|
| `-show-icons` / `-no-show-icons` | Load and show icons at all. On by default |
| `-icon-theme <name>` | Which icon theme to look in. Unset means the system default |
| `-window-thumbnail` | In the window switcher, use a live thumbnail as the row icon where the compositor offers one |
| `-preview-cmd <cmd>` | Generate preview icons with your own command — see [sofi-thumbnails(5)](doc/sofi-thumbnails.5.markdown) |

#### Fallback icons

Not every row has an icon to show. A `.desktop` file may have no `Icon=` key, or
name one absent from your icon theme; and **`run` mode indexes bare executables
on `$PATH`**, which have no desktop entry and therefore usually no icon at all.
Those rows otherwise draw a blank where every other row has a picture, leaving
the text column ragged.

Set a stand-in **per mode**, as a theme property:

```css
configuration {
    run,drun {
      fallback-icon: "application-x-addon";
    }
}
```

The value is any icon name your theme provides. It applies to whichever modes you
name, so `run` can fall back to something different from `filebrowser`.

This is the only fallback-icon mechanism. A `-application-fallback-icon`
command-line option existed up to and including 1.0.0, was never wired to
anything, and has been removed; leaving it in a configuration file is harmless,
since an unrecognised key is ignored silently.

Full guide: [sofi-customisation(5)](doc/sofi-customisation.5.markdown). Format
reference: [sofi-theme(5)](doc/sofi-theme.5.markdown).

---

## 8. Scripting and extension

### 8.1 Script modes

A mode can be a shell script. Sofi calls it, reads lines from stdout, and calls
it again with the selection.

```bash
sofi -show mymode -modes "mymode:~/bin/mymode.sh"
```

The protocol is environment variables — **`SOFI_*`, not `ROFI_*`**:

| Variable | Meaning |
|---|---|
| `SOFI_RETV` | 0 initial call, 1 selection, 2 custom entry, 10+ custom binding |
| `SOFI_INFO` | The hidden `info` field of the selected row |
| `SOFI_DATA` | Opaque state your script set on a previous call |
| `SOFI_INPUT` | The text currently typed |
| `SOFI_OUTSIDE` | Set inside a sofi-launched process; used to refuse recursion |

> **This rename fails silently.** A script written for rofi reads unset variables
> rather than erroring. If a ported script behaves as though nothing is selected,
> this is why.

Rows carry options after an ASCII `\x1f` separator — `icon`, `display`, `info`,
`meta`, `nonselectable`, `urgent`, `active`, `permanent`. Full protocol:
[sofi-script(5)](doc/sofi-script.5.markdown), with worked examples in
[Examples/](Examples/).

### 8.2 dmenu mode

```bash
printf 'one\ntwo\nthree\n' | sofi -dmenu -p "pick one"
```

A dmenu-compatible filter: reads stdin, writes the choice to stdout. Beyond
dmenu it adds `-format`, multi-select with ballots, `-display-columns`,
`-markup-rows`, `-password`, `-only-match`, `-no-custom`, `-sync`, `-select`,
`-mesg` and per-row urgent/active marking.

Exit codes: `0` accepted, `1` cancelled, `10+N` for `kb-custom-N`.

Full reference: [sofi-dmenu(5)](doc/sofi-dmenu.5.markdown).

### 8.3 Plugins

Out-of-tree modes are shared objects exporting a `mode` symbol, loaded from
`$libdir/sofi` or `$SOFI_PLUGIN_PATH`, discoverable with
`pkg-config --variable=pluginsdir sofi`.

**Rofi plugins will not load**, and are not intended to. The `ABI_VERSION` was
bumped at the fork so a stale plugin fails loudly rather than crashing.

### 8.4 Custom commands on events

```bash
-on-selection-changed   -on-mode-changed   -on-entry-accepted
-on-menu-canceled       -on-menu-error     -on-screenshot-taken
```

See [sofi-actions(5)](doc/sofi-actions.5.markdown).

---

## 9. Backends and build options

### 9.1 Backend selection

xcb is chosen when compiled in; Wayland overrides it when `$WAYLAND_DISPLAY` is
set and neither `-x11` nor `-xcb` was passed. Force xcb with `sofi -x11`.

### 9.2 Wayland shell protocols

The backend prefers **`zwlr_layer_shell_v1`, bound at version 4**. Version 4
matters: `on-demand` keyboard interactivity — which the notification daemon needs
in order not to steal your focus — arrived in v4, and below it wlroots coerces
the request to `EXCLUSIVE` **silently** rather than erroring.

Compositors without layer-shell — notably **Mutter (GNOME) and KWin (Plasma)** —
fall back to `xdg-shell`, where the surface is an ordinary toplevel and
**placement is the compositor's decision**:

- `location`, `anchor`, `x-offset`, `y-offset` have no effect
- keyboard interactivity cannot be forced
- `click-to-exit` cannot capture clicks outside the window
- `wayland-layer` is ignored
- the monitor's size and aspect ratio are unknowable, so theme `@media`
  size/aspect queries are ignored with a warning (see `sofi-theme(5)`)

The panel surfaces depend on layer-shell for placement, so under `xdg-shell` they
degrade to ordinary windows. If neither protocol is available the backend reports
the failure and exits rather than aborting.

### 9.3 Build options

| Option | Default | Enables |
|---|---|---|
| `-Ddrun` | true | Desktop-file application menu |
| `-Dwindow` | true | Window switcher and task manager |
| `-Dsheets` | true | hikari-sakura sheet switcher |
| `-Dnotify` | true | Notification daemon and history |
| `-Dtray` | true | System tray host and tray menus |
| `-Dwayland` | auto | Wayland backend |
| `-Dxcb` | auto | X11 backend |
| `-Dimdkit` | true | X11 input-method support |
| `-Dcheck` | auto | Build and run the test suite |

At least one backend must be enabled. See [INSTALL.md](INSTALL.md).

---

## 10. Files, paths and environment

### 10.1 Configuration

| Path | Contents |
|---|---|
| `$XDG_CONFIG_HOME/sofi/config.sasi` | Your configuration — **the main file** |
| `$XDG_CONFIG_HOME/sofi/themes/` | Your themes |
| `$XDG_CONFIG_HOME/sofi/scripts/` | Your script modes |
| `$XDG_CONFIG_DIRS/*/sofi.sasi`, `$SYSCONFDIR/sofi.sasi` | System-wide |
| `$datadir/sofi/themes/` | Installed themes |

> **These are a hard break from rofi's paths, with no fallback.** Sofi does not
> read `~/.config/rofi/`. Nothing errors; things simply are not found.

### 10.2 Cache and runtime

| Path | Contents |
|---|---|
| `$XDG_CACHE_HOME/sofi/notifications.history` | The persisted notification ring |
| `$XDG_CACHE_HOME/sofi3.druncache` | drun use-ordering |
| `$XDG_CACHE_HOME/sofi-drun-desktop.cache` | Parsed desktop files |
| `$XDG_CACHE_HOME/sofi-4.runcache` | run history |
| `$XDG_CACHE_HOME/sofi-2.sshcache` | ssh history |
| `$XDG_CACHE_HOME/sofi3.filebrowsercache` | filebrowser history |
| `$XDG_CACHE_HOME/sofi-entry-history.txt` | Input-field history |
| `$XDG_RUNTIME_DIR/sofi-<surface>.pid` | Per-surface instance lock |
| `$XDG_RUNTIME_DIR/hikari.sock` | The compositor socket (owned by hikari) |

### 10.3 Environment variables

| Variable | Read by | Purpose |
|---|---|---|
| `SOFI_RETV`, `SOFI_INFO`, `SOFI_DATA`, `SOFI_INPUT` | script modes | The script protocol |
| `SOFI_OUTSIDE` | sofi and script modes | Refuse launching sofi from inside sofi |
| `SOFI_PLUGIN_PATH` | sofi | Extra plugin search directory |
| `SOFI_PNG_OUTPUT` | sofi | Override the screenshot output path |
| `WAYLAND_DISPLAY`, `DISPLAY` | sofi | Backend selection |
| `SOFI_TRAY_EAGER_DECODE` | tray daemon | **Testing only** — force icon decode in the debug path instead of lazily |

---

## 11. Diagnostics

```bash
sofi -h                     # what this binary actually offers
sofi -v                     # version
sofi -list-keybindings      # every binding and its current value
sofi -dump-config           # current configuration, as .sasi
sofi -dump-theme            # current theme, as .sasi
sofi -sasi-validate FILE    # check a theme file resolves
sofi -log-level debug       # includes which Wayland shell was selected
```

`-log-level debug` is the first thing to reach for on Wayland: the backend logs
which shell protocol it bound, which settles most placement questions immediately.

More: [sofi-debugging(5)](doc/sofi-debugging.5.markdown).

### Common situations

| Symptom | Cause |
|---|---|
| Tray zone empty, warning about a shape it cannot read | Daemon and strip are different builds. Restart `sofi -tray-daemon` |
| An application shows no tray icon, others do | It started before the host. Restart that application |
| Sheet switcher reports the socket is absent | hikari is not running, or predates the control socket |
| A panel is positioned strangely on GNOME/KDE | No layer-shell; `xdg-shell` fallback, where the compositor places it |
| A ported rofi script acts as if nothing is selected | It reads `ROFI_*`; sofi sets `SOFI_*` |
| Panels render soft on a scaled output | Fractional scaling is not implemented; integer `buffer_scale` only |
| Your rofi config seems ignored | It is. Sofi reads `~/.config/sofi/`, with no fallback |

---

## 12. Known limits

Stated rather than left to be discovered:

- **Fractional scaling is not implemented.** `wp_fractional_scale_v1` is
  advertised by hikari but sofi uses integer `buffer_scale`, so panels render
  soft on a fractionally scaled output.
- **Window mode does not work on KWin or Mutter**, which implement neither wlr
  foreign-toplevel protocol.
- **`-monitor -n`, `-normal-window` and fake transparency** are X11-only or
  unimplemented on Wayland.
- **`-global-kb`, IME and the `cursor-shape` path are inert on hikari-sakura**,
  which does not advertise those protocols. They fail quietly rather than loudly.
- **Power controls — lock, logout, shutdown, reboot, suspend — are not
  implemented.** Lock and logout need a compositor control verb that
  hikari-sakura deliberately does not expose; the rest need a privilege decision,
  since FreeBSD has no `logind`. The system menu reserves the space so adding
  them later is additive.
- **X11 has no tray.** The tray host is StatusNotifierItem only; XEmbed is not
  implemented.
- **There is no test coverage of the display backends, the modes, or anything
  Wayland.** The suite covers the theme parser, helpers and widgets.
