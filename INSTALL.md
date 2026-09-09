# Installation guide

How to build and install **Sofi** — the Sakura Official Full Indexer, the UI
display and layer-shell layer of the
[hikari-sakura](https://github.com/orpheus497/hikari-sakura) compositor — and how
to make debug builds.

> Sofi is a separate program from rofi, not a drop-in. Installing it alongside
> rofi is fine: the binaries, configuration directories, cache files, manpages,
> pkg-config module and plugin directories are all named differently and neither
> reads the other's files.

Sofi uses [Meson](https://mesonbuild.com/) as build system.
Be default sofi builds with both backends (x11 and wayland) if available on the
system. If no backend is found, it will give an error.
You can force the build system to disable the [wayland](#disable-wayland-support)
or [x11](#disable-x11-support) backend.

## DEPENDENCY

### For building

- C compiler that supports the c99 standard. (gcc or clang)

- meson

- ninja

- pkg-config

- flex 2.5.39 or higher

- bison

- check (Can be disabled with `meson setup -Dcheck=disabled`)
    check is used for build-time tests and does not affect functionality.

- Developer packages of the external libraries

- glib-compile-resources

### External libraries

Split by backend. **A Wayland-only build needs the core list plus the Wayland
list, and none of the X11 entries** — those are required only when the xcb
backend is enabled, which it is by default. Disable it with `-Dxcb=disabled` if
you do not want them.

#### Core — always required

- libpango >= 1.50

- libpangocairo

- libcairo

- libglib2.0 >= 2.72
  - gmodule-2.0
  - gio-unix-2.0

- libgdk-pixbuf-2.0

- libxkbcommon >= 0.4.1

#### For X11/xcb support

- libcairo-xcb

- libstartup-notification-1.0

- libxkbcommon-x11

- libxcb (sometimes split, you need libxcb, libxcb-xkb and libxcb-randr
    libxcb-xinerama)

- xcb-util

- xcb-util-wm (sometimes split as libxcb-ewmh and libxcb-icccm)

- xcb-util-cursor

- xcb-imdkit  (optional, 1.0.3 or up preferred)

#### For Wayland support

- wayland-client

- wayland-cursor

- wayland-protocols >= 1.17

On most distributions a single `wayland` package provides both `wayland-client`
and `wayland-cursor`; they are named separately here because those are the
pkg-config modules the build actually looks for.

On debian based systems, the developer packages are in the form of:
`<package>-dev` on rpm based `<package>-devel`.

### Runtime tools — not linked, not required to build

Some surfaces drive an external command as a subprocess. **None of these is a
build dependency**, nothing here is linked into sofi, and a missing tool costs
only the surface that uses it — the build does not check for them and the binary
does not fail without them.

| Surface | Needs one of | Notes |
|---|---|---|
| `sofi -show volume` | `wpctl` (WirePlumber), `pactl` (PulseAudio), `mixer` (FreeBSD base) | Tried in that order; a tool that is installed but reports no sink is skipped. `mixer` needs nothing installed on FreeBSD, so it is the last backend tried rather than one you have to set up — but it is chosen only if it reports a usable control, and where none of the three reports a sink the mode says so and exits |
| `sofi -show network` | `nmcli` (NetworkManager), or the base system's `ifconfig` + `wpa_cli` + `dhclient` + `service` | `nmcli` is used only when NetworkManager is actually running. The base-system path needs nothing installed on FreeBSD. **Most verbs need privilege** — see `network-privilege-command` in [CONFIG.md](CONFIG.md); sofi installs nothing setuid |
| `sofi -show bluetooth` | `hccontrol`, `bthidcontrol`, `service` — all FreeBSD base | **FreeBSD netgraph only**; sofi does not speak BlueZ, so this mode has nothing to drive on Linux. Needs `bluetooth_enable="YES"` and `hcsecd_enable="YES"` in `rc.conf`, and `bthidd_enable="YES"` for keyboards, mice and gamepads. **Pairing, forgetting and starting/stopping the stack need privilege** — see `network-privilege-command` in [CONFIG.md](CONFIG.md). The paired list is read unprivileged first and falls back to that option only when it has to, which on a stock system it does, because `/etc/bluetooth/hcsecd.conf` is `0600 root`; every other read is unprivileged, and connect, disconnect and reset are tried unprivileged before escalating. Some adapters need firmware: Intel parts want `comms/iwmbt-firmware`. **Audio needs `virtual_oss(8)` and a bluetooth backend, and which package supplies them depends on your release.** On **FreeBSD 15.0 and newer**, `virtual_oss` is in the base system and the backend is **`audio/virtual_oss_bluetooth`**, which installs `voss_bt.so`; that port takes its version from `OSVERSION` and builds from `${SRC_BASE}/lib/virtual_oss/bt`, so `pkg install virtual_oss_bluetooth` works only where a package exists for that exact release, and otherwise needs `/usr/src` for the running release — it refuses with `requires FreeBSD source files` without it. On **FreeBSD 14 and earlier**, `virtual_oss` is not in base: install **`audio/virtual_oss`**, whose `BLUETOOTH` option is on by default and builds the support in; `audio/virtual_oss_bluetooth` is not the mechanism there. That port is `IGNORE`d on 15 and 16 precisely because base supersedes it. Either way, `cuse_load="YES"` in `loader.conf` is required. PipeWire and PulseAudio do **not** work here; their bluetooth backend needs BlueZ |
| `sofi -show sheets` | *(no binary)* | Needs hikari-sakura's control socket at `$XDG_RUNTIME_DIR/hikari.sock` |
| `sofi -show display` | `wlr-randr`, plus `backlight` (FreeBSD base) for brightness | Needs a compositor that advertises `wlr-output-management-unstable-v1` — hikari-sakura does as of its commit `60075bd`. Being wlroots-based is not sufficient: the compositor has to create `wlr_output_manager_v1` itself, which hikari did not before that commit. Without `wlr-randr` the pane says so rather than showing an empty list. **Brightness is the internal panel only**: `backlight(8)` writes `/dev/backlight/backlight0`, which is `root:video`, so add your user to the `video` group and no privilege is needed. External monitors need DDC/CI (`ddcutil`), which sofi does not drive |
| `sofi -show windowlist` | *(no binary)* | Needs `wlr-foreign-toplevel-management` on Wayland, or an EWMH window manager on X11. **`-show window` is the control panel and needs neither** — it lists sofi's own surfaces |
| `sofi -tray-daemon` | *(no binary)* | Needs a session bus. **Conflicts with `saber`'s tray host — run one, not both** |

Every tool above is run as a separate process. **None of them is a dependency of
this build, and none is linked into, loaded by, or vendored with sofi.** That is
a statement about how sofi is built and what it does at runtime, and it is why
this project treats them as runtime tools rather than dependencies.

It is not a statement about your obligations, and nothing here should be read as
one. **If you install or redistribute any of these components — `pactl`,
NetworkManager, `mixer`, `hccontrol`, `bthidcontrol` or `virtual_oss` — review
that component's own licence terms.** They differ from each other, they differ
by version and by how the component reached your system, and they are not
sofi's to summarise on your behalf.

One component is worth naming separately, because it is dynamically loaded and
the paragraph above would otherwise be read as covering it: **`voss_bt.so`**,
from `audio/virtual_oss_bluetooth`, is loaded by `virtual_oss(8)` — **not by
sofi**, which spawns `virtual_oss` as a subprocess and never opens the plugin
itself. The port declares it as BSD-2-Clause; consult the port for the terms
that actually apply, as with everything else above.

### The rest of the desktop

Sofi is one of four programs and none of the other three is required to build or
run it:

| Program | Relationship |
|---|---|
| [hikari-sakura](https://github.com/orpheus497/hikari-sakura) | The compositor. Provides `zwlr_layer_shell_v1` and the sheet socket. Sofi runs on any layer-shell compositor; only `sofi -show sheets` needs this one |
| [saber](https://github.com/orpheus497/saber) | The persistent panel. **Sofi does not provide a persistent taskbar or tray-in-a-panel, and is not intended to** — that is saber's job. Sofi's own tray host is for sessions that do not run saber |
| [sakura](https://github.com/orpheus497/sakura) | The display manager. No interaction with sofi at all |

## Install from a release

Sofi has not cut a tagged release yet, so build from a git checkout as described
below. Once releases exist, grab the `sofi-{version}.tar.[g|x]z` archive from the
GitHub releases page rather than the auto-attached `source code (zip|tar.gz)`
files, which do not include a set-up build system.

### Meson

Check dependencies and configure build system:

```bash
    meson setup build
```

Build Sofi:

```bash
    ninja -C build
```

The actual install, execute as root (if needed):

```bash
    ninja -C build install
```

The default installation prefix is: `/usr/local/` use `meson setup build
--prefix={prefix}` to install into another location.

## Install a checkout from git

These directions are also kept in [INSTALL.md in the repository][master-install].

If you don't have a checkout:

```bash
    git clone --recursive https://github.com/orpheus497/sofi
    cd sofi/
```

If you already have a checkout:

```bash
    cd sofi/
    git pull
    git submodule update --init
```

From this point, use the same steps you use for a release.

## Options for building

When you run the configure step there are several options you can configure.

For Meson, before the initial setup, you can see sofi options in
`meson_options.txt` and Meson options with `meson setup --help`. Meson's
built-in options can be set using regular command line arguments, like so:
`meson setup build --option=value`. Sofi-specific options can be set using the
`-D` argument, like so: `meson setup build -Doption=value`. After the build dir
is set up by `meson setup build`, the `meson configure build` command can be
used to configure options, by the same means.

The most useful one to set is the installation prefix:

```bash
    # Meson
    meson setup build --prefix <installation path>
```

f.e.

```bash
    # Meson
    meson setup build --prefix /usr
```

### Disable x11 support

```bash
meson setup build -Dxcb=disabled
```

### Disable wayland support

```bash
meson setup build -Dwayland=disabled
```

### Install locally

or to install locally:

```bash
    # Meson
    meson setup build --prefix ${HOME}/.local
```

### Verbose build output

Show the commands called (when using ninja):

```bash
    # Meson
    ninja -C build -v
```

### Debug build

Compile with debug symbols and no optimization, this is useful for making
backtraces:

```bash
    # Meson
    meson configure build --debug
    ninja -C build
```

### Get a backtrace

Getting a backtrace using GDB is not very handy. Because if sofi get stuck, it
grabs keyboard and mouse. So if it crashes in GDB you are stuck. The best way
to go is to enable core file. (ulimit -c unlimited in bash) then make sofi
crash. You can then load the core in GDB.

```bash
    # Meson (because it uses a separate build directory)
    gdb build/sofi core
```

> Where the core file is located and what its exact name is different on each
> distributions. Please consult the relevant documentation.

For more information see the sofi-debugging(5) manpage.

## Distribution packages

Sofi is a young fork and is **not yet packaged by any distribution**. Build from
source using the instructions above.

On FreeBSD, which is sofi's primary development target, the build dependencies are:

```sh
pkg install meson ninja pkgconf bison flex check glib gtk-update-icon-cache \
            cairo pango gdk-pixbuf2 libxkbcommon wayland wayland-protocols \
            libxcb xcb-util xcb-util-wm xcb-util-cursor xcb-util-keysyms \
            startup-notification
```

`bison` is required explicitly: the theme grammar in `lexer/theme-parser.y` uses
GNU Bison features (`%glr-parser`, `%define api.pure`) that the base system
`byacc` cannot build.

[master-install]: https://github.com/orpheus497/sofi/blob/master/INSTALL.md#install-a-checkout-from-git
