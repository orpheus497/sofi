<h1 align="center"> Sofi </h1>
<p align="center"><i>The shell for hikari-sakura — application menu, task strip, sheet switcher, notification daemon and system tray in one binary</i>.</p>

<p align="center">
  <img src=".github/sofi_screenshot.png" alt="Four sofi surfaces on one hikari-sakura desktop: the sheet switcher as a row of ten chips under the top bar, the application menu rising from the bottom centre, the notification history down the right edge, and the task strip along the bottom." width="100%">
</p>
<p align="center"><sub>Four surfaces at once — sheet switcher under the top bar, application menu bottom centre, notification history on the right edge, task strip along the bottom. One binary, one invocation each, no configuration file. (Taken before the system tray landed in the task strip's right-hand corner.)</sub></p>

**Sofi** is the shell for the
[hikari-sakura](https://github.com/orpheus497/hikari-sakura) Wayland compositor.
It began as a hard fork of rofi, is developed independently, and does not track
upstream. It is MIT licensed — see [COPYING](COPYING) and [AUTHORS](AUTHORS).

> **Sofi is a separate program, not a rofi drop-in.** It does not read rofi's
> configuration, themes, cache or script-mode environment variables, and rofi
> plugins will not load. Configuration lives in `~/.config/sofi/config.sasi`,
> themes use the `.sasi` extension, and script modes receive `SOFI_*` variables.
> File sofi issues here and rofi issues upstream; neither project supports the
> other.

## What sofi is

Sofi provides the system surfaces of the
[hikari-sakura](https://github.com/orpheus497/hikari-sakura) compositor, each a
separate invocation of the same binary:

| Surface | Invocation | Where it renders |
|---|---|---|
| Application menu | `sofi -show drun` | Bottom centre, 560px wide, above the task strip |
| Task and window manager | `sofi -show window` | Strip along the bottom, inset from the edges |
| Sheet switcher | `sofi -show sheets` | Top centre, a row of ten chips under the compositor's bar |
| Notification daemon | `sofi -notification-daemon` | Stack in the bottom-right corner |
| Notification history | `sofi -show notification-history` | Right edge, 420px wide |
| System tray host | `sofi -tray-daemon` | No surface of its own — feeds the task strip's right corner |
| Message toast | `sofi -e <message>` | Top-right corner |

Every placement above is a property in a compiled-in layout, changeable in four
lines of `~/.config/sofi/config.sasi` — see [Theming](#theming).

Each surface has its **own layout compiled into the binary** and its **own
instance lock**, so they coexist rather than replacing one another. No
configuration file is required for any of them — `~/.config/sofi/` is optional,
and anything you put there still overrides the built-in layout.

Sofi is developed primarily on FreeBSD and targets Wayland via
`zwlr_layer_shell_v1` (bound at version 4). It retains a working X11/xcb backend
and every general-purpose mode it inherited, so it is still usable as a
standalone launcher on other compositors and window managers.

### What sofi is not

Sofi is not:

- A UI toolkit.

- A library to be used in other applications.

- An application that can support every possible use-case. It tries to be
    generic enough to be usable by everybody.
  - Specific functionality can be added using scripts or plugins.

- Just a dmenu replacement. The dmenu functionality is a nice 'extra' to
    **sofi**, not its main purpose.

## Table of Contents

- [The surfaces](#the-surfaces)
- [Theming](#theming)
- [Features](#features)
- [Modes](#modes)
- [Wayland support](#wayland-support)
- [Manpages](#manpage)
- [Installation](#installation)
- [Quickstart](#quickstart)

## The surfaces

### Application menu

```bash
sofi -show drun
```

Rises from the bottom centre, clearing the task strip, with icons and a two-tier
row — application name, then generic name beside it in a lighter weight. This is
the default surface: any mode that is not one of those below gets the same shape,
so `run`, `ssh`, `combi`, `filebrowser` and user script modes all look
consistent.

### Task and window manager

```bash
sofi -show window
```

A strip anchored near the south edge, inset from the screen edges so it reads as
a floating bar. A filter field, the task strip itself, and the system tray in the
right-hand corner. Rows lead with the window title and demote the application
class, because the title is what distinguishes two windows of the same
application.

Beyond switching, it carries task-manager verbs on the Wayland backend:

- `kb-custom-1` — toggle minimise
- `kb-custom-2` — toggle maximise

Minimised windows are surfaced through the `URGENT` display state, so a theme
can style them distinctly. On hikari-sakura, maximise maps onto full-maximize;
there is no separate fullscreen state.

### Sheet switcher

```bash
sofi -show sheets
```

Renders as a horizontal row of ten chips centred under the compositor's top bar.
Each chip is a sheet number and its window count; empty sheets are dimmed and the
current sheet is filled. `kb-custom-1` sends the focused window to the
highlighted chip.

The row is a fixed ten-cell grid rather than a content-sized strip, so the chips
stay in the same place from one invocation to the next.

This mode speaks to hikari's control socket at
`$XDG_RUNTIME_DIR/hikari.sock` rather than to a Wayland protocol — no
standards-track protocol can express send-to-sheet. On any other compositor the
mode reports that the socket is absent and exits cleanly; it does not abort.

### Notification daemon

```bash
sofi -notification-daemon
```

Owns `org.freedesktop.Notifications` on the session bus and renders the
notification stack in the bottom-right corner. Notifications are kept in a ring
buffer; `urgency=2` (critical) notifications never expire on their own. Browse
what has arrived with:

```bash
sofi -show notification-history
```

The daemon idles with no surface mapped and brings one back when a notification
arrives, so it costs nothing while the desktop is quiet.

Notifications look deliberately unlike the menus — separate cards with a leading
urgency stripe, rather than rows with a filled selection — so a banner is never
mistaken for something you are about to launch.

#### Clearing notifications

Two separate actions, because clearing banners off your screen should not also
lose the list of what you missed:

```bash
sofi -notification-clear           # dismiss what is on screen, keep history
sofi -notification-clear-history   # discard everything
```

Both are available inside the history panel as `kb-custom-1` / `kb-custom-2` and
as buttons. The live banner carries the dismiss button too — it takes no
keyboard, so a button is the only way to reach it.

Inside the history panel, **Dismiss is hidden when no daemon is running**: with
nothing on screen it has nothing to retire. Clear still works, because
discarding the stored history does not need a daemon.

Per entry, Enter tries three things in order:

1. **Run the notification's default action**, if it is still on screen and
   offered one. The application said what Enter should mean.
2. **Raise the window of the application that sent it.** This is what a history
   list is for that a banner is not — seeing something from an hour ago and
   wanting to go and deal with it — so it works on retired entries too. It needs
   the sender to have set the `desktop-entry` hint, and matching is strict: when
   nothing matches, nothing is raised, rather than the wrong window.
3. **Acknowledge it**, if it is still on screen, and leave the panel open —
   going through a list of missed notifications means going through it.

An entry that is already retired and has no window left simply closes the panel.
Shift+Delete retires one entry while keeping it in history.

### System tray

```bash
sofi -tray-daemon
```

Owns `org.kde.StatusNotifierWatcher` and collects the tray items applications
publish. It has no surface of its own — the icons appear in the **task strip's
right-hand corner**, and follow along while the strip is open, so an application
starting or changing its icon shows up without reopening anything.

Three things worth knowing:

- **Start it before the applications whose icons you want.** A tray application
  asks once, at its own startup, whether a host exists. One that finds none shows
  no icon and never asks again — so a host started later means restarting those
  applications.
- **Restart it after upgrading sofi.** `org.sofi.Tray` is private between two
  sofi processes and changes with the code; an older daemon serves a shape the
  new strip cannot read, and you get an empty tray zone plus a warning saying
  so. The applications themselves do not need restarting — they watch for the
  watcher and re-register.
- **It needs no display.** The protocol is D-Bus only, so it runs with no Wayland
  or X11 session at all.
- **It is a separate process from the notification daemon**, deliberately. The
  two share no state, and a fault in one should not take the other with it.

**Clicking an icon opens that application's menu, in the strip.** The window
list is replaced by the menu while it is up; Escape or choosing an entry closes
it. Submenus open in place with a `..` row to go back, the way the file browser
descends into directories.

| Button | What it does |
|---|---|
| Left | The item's menu, or `Activate` when it published none |
| Right | The same menu, or the item's own `ContextMenu` when it published none |
| Middle | `SecondaryActivate` |

All three are rebindable — `mt-activate`, `mt-context-menu`,
`mt-secondary-activate`.

**Why sofi draws the menu rather than the application.** Under
StatusNotifierItem an application publishes a *description* of its menu over
`com.canonical.dbusmenu` — labels, separators, toggles, which rows open
submenus — and there is no method in that protocol that asks it to display
anything. Rendering is the host's job. That is the deliberate break from the old
X11 tray, where an application embedded a window and drew its own menu; moving
the menu out of the application's process is what lets the panel theme it. So
there is no "native menu" to show: for most tray applications the menu exists
only as data until something draws it.

### Autostart

Two long-running services, neither of which belongs on a key:

```sh
sofi -notification-daemon &
sofi -tray-daemon &
```

## Theming

Sofi has no theme file to install and no theme to pick. It compiles in a palette
and six layouts, and your config file *edits* them rather than replacing them.

### The palette

One file defines every colour, in two layers. Sixteen positional slots in the
conventional terminal arrangement:

```css
color0  #2b1e3a   color8  #5e5966
color1  #c96464   color9  #df8787
color2  #df9f87   color10 #f2bda8
color3  #e4b382   color11 #f5cf9e
color4  #8e7cc3   color12 #aba0d9
color5  #b18fc7   color13 #cfaedc
color6  #9fa0a6   color14 #b8b9be
color7  #d4d4d9   color15 #f0edf2
```

…and semantic names that reference them, which is what the layouts actually use:

| Name | Slot | Used for |
|---|---|---|
| `background` | color0 @ 90% | Panel grounds |
| `surface` | derived | Inset fields, scrollbar troughs |
| `foreground` | color7 | Body text |
| `foreground-dim` | color6 | App names, timestamps, counts |
| `accent` | color12 | Selection |
| `accent-soft` | color4 | Prompts and leading stripes |
| `accent-strong` | color13 | Current sheet, live notification |
| `on-accent` | color0 | Text on an accent fill |
| `urgent` / `critical` | color9 / color1 | Critical, as text / as a fill |
| `muted` | color8 | Separators and troughs — **never text** |

These sixteen values are the same ones hikari-sakura takes in its `ui { palette }`
block, and the semantic names map onto its `ui { colorscheme }` slot for slot —
`accent` is the compositor's `selected`, `background` is its `bar` exactly. One
scheme dresses the compositor, its bar and the shell together, which also means
a terminal colorscheme can be dropped into both.

Every text-on-fill pair is checked against WCAG AA. Three tones are constrained
by that and not by taste: `muted` cannot carry text at 2.29:1, `critical` is for
fills rather than text at 4.07:1, and `on-accent` is dark rather than white
because white on `color12` is 2.40:1.

### Recolour everything

```css
/* ~/.config/sofi/config.sasi */
* {
    color0:  #1c1b22;
    color12: #89b4fa;
}
```

Every surface follows, because every semantic name is a reference to a slot.
Supply as many or as few as you like.

### Recolour one role

```css
* {
    accent: #f5c2e7;   /* selections only */
}
```

### Move or resize a panel

```css
window {
    location: west;
    anchor:   west;
    width:    340px;
}
```

### Why this works

Sources are parsed in order — default configuration, palette, the one layout for
the surface you asked for, your `config.sasi`, then `-theme` — and later sources
override earlier ones property by property. Colour references are resolved after
all of them have been read, so redefining a name in your own file reaches every
layout that uses it.

Full instructions, including per-surface theming and the offset-sign trap, are in
[sofi-customisation(5)](doc/sofi-customisation.5.markdown).

## Features

Its main features are:

- Fully configurable keyboard navigation

- Type to filter
  - Tokenized: type any word in any order to filter
  - Case insensitive (togglable) or SmartCase
  - Support for fuzzy-, regex-, prefix-, and glob-matching

- UTF-8 enabled
  - UTF-8-aware string collating
  - International keyboard support (\`e -> è)

- RTL language support

- Cairo drawing and Pango font rendering

- Built-in modes:
  - Window switcher and task manager
    - EWMH compatible WM
    - Workarounds for i3,bspwm
    - Wayland based WMs that follow the wlr family

  - Application launcher

  - Desktop file application launcher

  - SSH launcher mode

  - File browser

  - Sheet switcher (hikari-sakura)

  - Notification daemon and history

  - Combi mode, allowing several modes to be merged into one list

- History-based ordering — last 25 choices are ordered on top based on use
    (optional)

- Levenshtein distance or fzf like sorting of matches (optional)

- Drop-in dmenu replacement
  - Many added improvements

- Easily extensible using scripts and plugins

- Advanced Theming

## Modes

**Sofi** has several built-in modes implementing common use cases and can be
extended by scripts (either called from
**Sofi** or calling **Sofi**) or plugins.

Below is a list of the different modes:

- **run**: launch applications from $PATH, with option to launch in terminal.

- **drun**: launch applications based on desktop files. It tries to be
    compliant to the XDG standard.

- **window**: Switch between windows, and minimise or maximise them.

- **windowcd**: Switch between windows on the current desktop (X11 only).

- **sheets**: Switch hikari-sakura sheets and send windows to them.

- **notifications**: The notification stack rendered by the daemon.

- **notification-history**: Browse notifications that have already been shown.

- **ssh**: Connect to a remote host via ssh.

- **filebrowser**: A basic file-browser for opening files.

- **recursivebrowser**: A file-browser that descends into directories.

- **keys**: list internal keybindings.

- **script**: Write (limited) custom mode using simple scripts.

- **combi**: Combine multiple modes into one.

Which modes are available depends on the backend and on build options: the
window modes differ between X11 and Wayland, and `sheets`, `notifications` and
`notification-history` are only present when their features are compiled in.
Run `sofi -h` to see what the binary you have actually offers.

**Sofi** is known to work on FreeBSD and Linux.

## Wayland support

### Build

Please follow the [build instructions](INSTALL.md) to build sofi.

Wayland support is enabled by default, along with X11/xcb.

sofi can also be built *without* X11/xcb or wayland, but at least one backend
should be enabled:

    meson build -Dxcb=disabled
    meson build -Dwayland=disabled

### Usage

**Sofi** should automatically select the xcb or wayland backend depending on
the environment it is run on.

To force the use of the xcb backend (if enabled during build), the `-x11`
option can be used:

    sofi -x11 ...

### Missing features in Wayland mode

A few options are difficult or impossible to implement under Wayland's
architecture and available APIs:

- `-normal-window`. Not impossible, but it would require real work, and it is a
  toy feature for a program that renders as a layer surface.
- `-monitor -n` for fine-grained selection of monitor to display sofi on
- some window locations parameters work partially, `x-offset` and `y-offset` are only working from screen edges
- fake transparency
- window mode on KWin which implements different protocols than the wlr family

### Shell protocols on Wayland

The Wayland backend prefers `zwlr_layer_shell_v1`, which lets it position and size
itself precisely. It binds version 4, because the `on-demand` keyboard
interactivity the notification daemon needs is unreachable below it and
wlroots silently degrades the request rather than erroring. Compositors that do
not implement layer-shell — notably Mutter (GNOME) and KWin (Plasma) — fall back
to `xdg-shell`, where the surface is an ordinary toplevel window and
**placement is the compositor's decision**. In that mode:

- `location`, `anchor`, `x-offset` and `y-offset` have no effect; the compositor
  places the window
- keyboard interactivity cannot be forced, so focus follows normal window rules
  rather than being grabbed
- `click-to-exit` cannot capture clicks outside the window
- the `wayland-layer` option (`overlay` / `top` / `bottom` / `background`) is ignored

The four panel surfaces depend on layer-shell for their placement, so under
`xdg-shell` they degrade to ordinary windows.

The backend logs which shell it selected at debug level. Run with `-log-level debug`
to confirm. If neither protocol is available, the backend reports the failure and
exits rather than aborting.

### Wayland DPI

On wayland, the output is only known after the first surface is shown. This makes sizing
sofi windows in absolute size (mm) very difficult, a problem unique for sofi,
as the actual DPI is unknown beforehand. This can be worked around by manually
passing the right DPI via configuration system. If the `dpi` config option is
set to `0` and only one monitor is connected sofi will use the DPI of the only
connected monitor or if you have multiple monitors and you specify a monitor
name, it will use the DPI of that monitor.

## Manpage

For more up to date information, please see the manpages. The other sections
and links might have outdated information as they have relatively less
maintenance than the manpages. So, if you come across any issues please
consult the manpages before filing a new issue.

- Manpages:
  - [sofi](doc/sofi.1.markdown)
  - [sofi-theme](doc/sofi-theme.5.markdown)
  - [sofi-debugging](doc/sofi-debugging.5.markdown)
  - [sofi-script](doc/sofi-script.5.markdown)
  - [sofi-theme-selector](doc/sofi-theme-selector.1.markdown)
  - [sofi-thumbnails](doc/sofi-thumbnails.5.markdown)
  - [sofi-keys](doc/sofi-keys.5.markdown)
  - [sofi-dmenu](doc/sofi-dmenu.5.markdown)
  - [sofi-actions](doc/sofi-actions.5.markdown)
  - [sofi-customisation](doc/sofi-customisation.5.markdown)

## Installation

Please see the [installation guide](INSTALL.md) for instructions on how to
install **Sofi**. Sofi is not yet packaged by any distribution; build from
source.

## Quickstart

### Usage

> **This section just gives a brief overview of the various options. To get the
> full set of options see the [manpages](#manpage) section above**

#### Running sofi

To launch **sofi** directly in a certain mode, specify a mode with `sofi -show <mode>`.
To show the `run` dialog:

```bash
    sofi -show run
```

Or get the options from a script:

```bash
    ~/my_script.sh | sofi -dmenu
```

Specify an ordered, comma-separated list of modes to enable. Enabled modes can
be changed at runtime. Default key is `Ctrl+Tab`. If no modes are specified,
all configured modes will be enabled. To only show the `run` and `ssh`
launcher:

```bash
    sofi -modes "run,ssh" -show run
```

The modes to combine in combi mode.
For syntax to `-combi-modes`, see `-modes`.
To get one merge view, of `window`,`run`, and `ssh`:

```bash
 sofi -show combi -combi-modes "window,run,ssh" -modes combi
```

### Configuration

Every surface works with no configuration file at all. If you want to change
something, generate a configuration file:

```bash
mkdir -p ~/.config/sofi
sofi -dump-config > ~/.config/sofi/config.sasi
```

This creates a file called `config.sasi` in the `~/.config/sofi/` folder. You
can modify this file to set configuration settings and modify themes.
`config.sasi` is the file sofi looks to by default.

Please see [CONFIG.md](CONFIG.md) for a summary of configuration options.
More detailed options are provided in the manpages.

### Themes

See [Theming](#theming) above, and
[sofi-customisation(5)](doc/sofi-customisation.5.markdown) for the full guide.

Sofi needs no theme installed to work. For anyone who would rather edit a whole
file than write overrides, the application-menu layout is installed as an
ordinary theme:

```bash
mkdir -p ~/.config/sofi
cp /usr/local/share/sofi/themes/config.sasi ~/.config/sofi/config.sasi
cp /usr/local/share/sofi/themes/colors-default.sasinc ~/.config/sofi/
```

`colors-default.sasinc` is the palette and `config.sasi` is the layout that
imports it. Note what copying it actually does: a config file cannot know which
surface it was loaded for, and `~/.config/sofi/config.sasi` is parsed *after*
whichever panel layout the invocation selected. Its `window`, `mainbox`,
`listview` and `element` rules therefore land on every surface — the task strip
and the sheet row pick up the menu's geometry too. Treat it as a starting point
to edit, not as a drop-in that leaves the other surfaces alone.

To change one surface and no other, override on that invocation instead:

```bash
sofi -show drun -theme-str 'window { width: 640px; }'
```

`-theme-str` merges over the layout that was loaded; `-theme` replaces the whole
theme, palette included, so a file passed that way must stand on its own.
