# Installation guide

This guide explains how to install sofi using its build system and how you can
make debug builds.

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

- libpango >= 1.50

- libpangocairo

- libcairo

- libcairo-xcb

- libglib2.0 >= 2.72
  - gmodule-2.0
  - gio-unix-2.0

- libgdk-pixbuf-2.0

- libstartup-notification-1.0

- libxkbcommon >= 0.4.1

- libxkbcommon-x11

- libxcb (sometimes split, you need libxcb, libxcb-xkb and libxcb-randr
    libxcb-xinerama)

- xcb-util

- xcb-util-wm (sometimes split as libxcb-ewmh and libxcb-icccm)

- xcb-util-cursor

- xcb-imdkit  (optional, 1.0.3 or up preferred)

On debian based systems, the developer packages are in the form of:
`<package>-dev` on rpm based `<package>-devel`.

For wayland support:

- wayland
- wayland-protocols >= 1.17

## Install from a release

When downloading from the github release page, make sure to grab the archive
`sofi-{version}.tar.[g|x]z`. The auto-attached files `source code (zip|tar.gz)`
by github do not contain a valid release. It misses a setup build system and
includes irrelevant files.

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

The GitHub Pages version of these directions may be out of date.  Please use
[INSTALL.md from the online repo][master-install] or your local repository.

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

## Install distribution

### Debian or Ubuntu

```bash
    apt install sofi
```

### Fedora

```bash
    dnf install sofi
```

### ArchLinux

```bash
    pacman -S sofi
```

### Gentoo

An ebuild is available, `x11-misc/sofi`. It's up to date, but you may need to
enable ~arch to get the latest release:

```bash
    echo 'x11-misc/sofi ~amd64' >> /etc/portage/package.accept_keywords
```

for amd64 or:

```bash
    echo 'x11-misc/sofi ~x86' >> /etc/portage/package.accept_keywords
```

for i386.

To install it, simply issue `emerge sofi`.

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
