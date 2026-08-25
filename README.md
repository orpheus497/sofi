<h1 align="center"> Sofi </h1>
<p align="center"><i>The shell for hikari-sakura — application menu, task strip, sheet switcher and notification daemon in one binary</i>.</p>

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

Sofi provides the four system surfaces of the
[hikari-sakura](https://github.com/orpheus497/hikari-sakura) compositor, each a
separate invocation of the same binary:

| Surface | Invocation | Where it renders |
|---|---|---|
| Application menu | `sofi -show drun` | Left edge, 280px wide |
| Task and window manager | `sofi -show window` | Full-width strip along the bottom |
| Sheet switcher | `sofi -show sheets` | Right edge, 190px wide |
| Notification daemon | `sofi -notification-daemon` | Stack in the bottom-right corner |

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

- [The four surfaces](#the-four-surfaces)
- [Features](#features)
- [Modes](#modes)
- [Wayland support](#wayland-support)
- [Manpages](#manpage)
- [Installation](#installation)
- [Quickstart](#quickstart)

## The four surfaces

### Application menu

```bash
sofi -show drun
```

Renders on the west edge. This is the default surface: any mode that is not one
of the three below gets the same sidebar shape, so `run`, `ssh`, `combi` and
user script modes all look consistent.

### Task and window manager

```bash
sofi -show window
```

A full-width strip anchored to the south edge, listing open windows. Beyond
switching, it carries task-manager verbs on the Wayland backend:

- `kb-custom-1` — toggle minimise
- `kb-custom-2` — toggle maximise

Minimised windows are surfaced through the `URGENT` display state, so a theme
can style them distinctly. On hikari-sakura, maximise maps onto full-maximize;
there is no separate fullscreen state.

### Sheet switcher

```bash
sofi -show sheets
```

Renders on the east edge and shows hikari's sheets: occupied ones display a
window count, empty ones are dimmed, and the current sheet takes the accent
colour. `kb-custom-1` sends the focused window to the highlighted sheet.

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

Sofi ships a single theme, which is also compiled into the binary and used when no
user theme is present. It is installed to `$datadir/sofi/themes/` as
`config.sasi` and `colors-default.sasinc`.

To start from it, copy it and edit:

```bash
mkdir -p ~/.config/sofi
cp /usr/local/share/sofi/themes/config.sasi ~/.config/sofi/config.sasi
cp /usr/local/share/sofi/themes/colors-default.sasinc ~/.config/sofi/
```

Recolouring only needs `colors-default.sasinc`; the layout lives in `config.sasi`.
See the `sofi-theme(5)` manpage for the full format.

The per-surface panel layouts are separate from this theme and are compiled into
the binary. They are parsed after the default configuration and before anything
you supply, so `~/.config/sofi/config.sasi` and `-theme` still take precedence.
