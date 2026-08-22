<h1 align="center"> Sofi </h1>
<p align="center"><i>A window switcher, Application launcher and dmenu replacement</i>.</p>

**Sofi** is a hard fork of [rofi](https://github.com/davatorium/rofi) by Dave
Davenport (Qball), which itself began as a clone of simpleswitcher by [Sean
Pringle](http://github.com/seanpringle/simpleswitcher) — a popup window switcher
roughly based on [superswitcher](http://code.google.com/p/superswitcher/).
Simpleswitcher laid the foundations, and Sean Pringle deserves much of the credit
for this tool. Wayland support originates in the fork maintained for years by
[lbonn](https://github.com/lbonn). Sofi is MIT licensed, as rofi is; see
[COPYING](COPYING) for the full notice and the retained copyright holders.

> **Sofi is not a drop-in replacement for rofi.** It does not read rofi's
> configuration, themes, cache or script-mode environment variables, and rofi
> plugins will not load. Configuration lives in `~/.config/sofi/config.sasi`,
> themes use the `.sasi` extension, and script modes receive `SOFI_*` variables.

**Sofi**, like dmenu, will provide the user with a textual list of options
where one or more can be selected.
This can either be running an application, selecting a window, or options
provided by an external script.

### What is sofi not?

Sofi is not:

- A UI toolkit.

- A library to be used in other applications.

- An application that can support every possible use-case. It tries to be
    generic enough to be usable by everybody.
  - Specific functionality can be added using scripts or plugins, many exist.

- Just a dmenu replacement. The dmenu functionality is a nice 'extra' to
    **sofi**, not its main purpose.

## Table of Contents

- [Features](#features)
- [Modes](#modes)
- [Wayland support](#wayland-support)
- [Manpages](#manpage)
- [Installation](#installation)
- [Quickstart](#quickstart)

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
  - Window switcher mode
    - EWMH compatible WM
    - Workarounds for i3,bspwm
    - Wayland based WMs that follow the wlr family

  - Application launcher

  - Desktop file application launcher

  - SSH launcher mode

  - File browser

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

- **window**: Switch between windows on an EWMH compatible window manager.

- **ssh**: Connect to a remote host via ssh.

- **filebrowser**: A basic file-browser for opening files.

- **keys**: list internal keybindings.

- **script**: Write (limited) custom mode using simple scripts.

- **combi**: Combine multiple modes into one.

**Sofi** is known to work on Linux and BSD.

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

Due to the different architecture and available APIs in Wayland mode, some features inherited from rofi are difficult or impossible to replicate

- `-normal-window` flag. Though it is also considered as a toy/deprecated feature in upstream rofi. Not impossible but would require some work.
- `-monitor -n` for fine-grained selection of monitor to display sofi on
- some window locations parameters work partially, `x-offset` and `y-offset` are only working from screen edges
- fake transparency
- window mode on KWin which implements different protocols than the wlr family

### Shell protocols on Wayland

The Wayland backend prefers `zwlr_layer_shell_v1`, which lets it position and size
itself precisely. Compositors that do not implement it — notably Mutter (GNOME) and
KWin (Plasma) — fall back to `xdg-shell`, where the surface is an ordinary toplevel
window and **placement is the compositor's decision**. In that mode:

- `location`, `anchor`, `x-offset` and `y-offset` have no effect; the compositor
  places the window
- keyboard interactivity cannot be forced, so focus follows normal window rules
  rather than being grabbed
- `click-to-exit` cannot capture clicks outside the window
- the `wayland-layer` option (`overlay` / `top` / `bottom` / `background`) is ignored

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

## Installation

Please see the [installation guide](INSTALL.md) for instructions on how to
install **Sofi**.

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

Generate a default configuration file

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
