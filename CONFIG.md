# Configuring Sofi

> This page covers the common cases, task first. For everything else:
> [FEATURES.md](FEATURES.md) is the reference by capability, **sofi(1)** the
> reference by flag, **sofi-theme(5)** the format, and **sofi-customisation(5)**
> theming in depth.

Sofi works with no configuration file at all. Every surface has a layout
compiled into the binary and a palette compiled in beside it. Everything below
is optional, and *overrides* the built-in defaults rather than replacing them.

> **Sofi does not read rofi's configuration.** The paths are `~/.config/sofi/`
> and the extension is `.sasi`, with no fallback to the rofi equivalents.
> Nothing errors — files are simply not found.

## Contents

- [Where configuration lives](#where-configuration-lives)
- [What gets loaded, and in what order](#what-gets-loaded-and-in-what-order)
- [Recipes](#recipes)
  - [Recolour everything](#recolour-everything)
  - [Recolour one role](#recolour-one-role)
  - [Move a panel](#move-a-panel)
  - [Resize a panel](#resize-a-panel)
  - [Restyle one surface only](#restyle-one-surface-only)
  - [Change what a mode shows](#change-what-a-mode-shows)
  - [Bind the surfaces in hikari-sakura](#bind-the-surfaces-in-hikari-sakura)
  - [Trigger a sofi surface from saber, or from any script](#trigger-a-sofi-surface-from-saber-or-from-any-script)
  - [Let the network and bluetooth panes change things](#let-the-network-and-bluetooth-panes-change-things)
- [The configuration block](#the-configuration-block)
- [File format](#file-format)
- [Finding every option](#finding-every-option)
- [Splitting configuration over multiple files](#splitting-configuration-over-multiple-files)

## Where configuration lives

`${XDG_CONFIG_HOME}/sofi/`, which on most systems is `~/.config/sofi/`. The main
file is `config.sasi`.

Create it only if you want to change something:

```bash
mkdir -p ~/.config/sofi
$EDITOR ~/.config/sofi/config.sasi
```

## What gets loaded, and in what order

Understanding this is what makes the recipes below three lines instead of three
hundred:

1. The compiled-in default configuration.
2. The compiled-in **palette** — `color0`–`color15` plus the semantic names
   (`background`, `foreground`, `accent`, …) that every layout refers to.
3. **One** compiled-in layout, chosen from the surface you asked for:
   `-show window`, `-show sheets`, `-show notification-history`,
   `-notification-daemon`, `-e`, or the general one for everything else.
4. A system `sofi.sasi` — the first one found in `$XDG_CONFIG_DIRS`, otherwise
   the one in `SYSCONFDIR` (usually `/etc/sofi.sasi`). Only the first match is
   read; several are never merged.
5. `~/.config/sofi/config.sasi`.
6. `-theme <file>`, if given.
7. Any `-theme-str` snippets, in the order given.

**Later wins, property by property** — with one exception. Steps 1–5 and 7
merge, so your config file edits the built-in layout rather than replacing it,
and a system `sofi.sasi` shows up in a `-dump-theme` the same way your own file
does. Colour references resolve after every source has been read, so redefining
a colour name in step 4 or 5 reaches the layout from step 3.

`-theme` is the exception: like `@theme`, it *discards* everything parsed before
it and the named file becomes the whole theme. Use `-theme-str` when you want to
override a handful of properties for one invocation.

To see what a surface actually resolved to:

```bash
sofi -show drun -dump-theme
sofi -show window -dump-theme
```

## Recipes

### Recolour everything

Redefine the positional slots. Every surface follows, because every semantic
name is a reference to one:

```css
/* ~/.config/sofi/config.sasi */
* {
    color0:  #1c1b22;
    color12: #89b4fa;
    color9:  #f38ba8;
}
```

Supply as many or as few as you like; anything you leave alone keeps its
built-in value. The full sixteen are listed in the README.

These are the same sixteen values hikari-sakura takes in its `ui { palette }`
block, so a terminal colorscheme pasted into both makes the compositor and the
shell agree.

**Five names are literals, not references**, because the format can neither
blend two colours nor apply alpha to a `@reference`. They keep their old values
until you recompute them:

| Name | Derived from |
|---|---|
| `background` | `color0` at 90% |
| `surface` | `color0` 25% toward `color8` |
| `surface-alt` | `color0` 50% toward `color8` |
| `border-soft` | `color8` at 60% |
| `hint` | `color0` 70% toward `color6` |

So the example above, which moves `color0`, also needs:

```css
* {
    background:  #1c1b22e6;   /* color0 at 90%            */
    surface:     #2d2b33;     /* color0 25% toward color8 */
    surface-alt: #3d3a44;     /* color0 50% toward color8 */
    hint:        #78787e;     /* color0 70% toward color6 */
}
```

`border-soft` comes from `color8`, which that example leaves alone. Everything
else — `foreground`, `accent`, `on-accent` and the rest — is a plain reference
and follows its slot on its own.

### Recolour one role

Redefine the semantic name instead, when you want to change what something is
*for* without disturbing everything else drawn from that slot:

```css
* {
    accent: #f5c2e7;   /* selections only; color12 keeps its other jobs */
}
```

Three names are contrast-constrained: `muted` must never carry text, `critical`
is for fills rather than text, and `on-accent` is what goes on top of an accent
fill. See **sofi-customisation(5)**.

### Move a panel

```css
window {
    location: west;
    anchor:   west;
}
```

Valid positions: `north`, `north east`, `east`, `south east`, `south`,
`south west`, `west`, `north west`, `center`.

**Offset signs differ between two kinds of surface.** Menus cover the screen
with an invisible click-catching surface and position the panel inside it, so
positive offsets move *inward* from the anchored edge. The notification banner
and the `-e` toast use layer-shell margins instead, where the sign is inverted.
If a panel moves the wrong way, that is why.

On the top edge, `location: north` is already flush beneath hikari-sakura's bar
— the compositor subtracts it before sofi sees a size. An offset there is a gap
you chose, not clearance you owe.

### Resize a panel

```css
window {
    width:  360px;
    height: 80%;
}

listview {
    lines: 16;
}
```

`height` is ignored by surfaces that size themselves to their content — the
notification banner and the toast. For lists, `lines` is usually what you
actually want.

The sheet switcher is a fixed ten-cell grid, so its `window { width }` is what
decides how much room each chip's label gets.

### Restyle one surface only

`config.sasi` is read for every invocation, so anything in it applies
everywhere — a `window { }` block there moves *every* panel.

That holds for every invocation that does not pass `-theme`. One that does
starts the theme over from the named file, so `config.sasi`'s *styling* is
discarded unless that file defines or imports it. Its `configuration { }`
settings are untouched either way: they are not part of the theme.

To reach exactly one surface, override on that invocation only:

```bash
sofi -show window -theme-str 'window { location: west; anchor: west; }'
```

`-theme-str` merges over the theme currently loaded, so only the properties you
name change. `-theme` does not: it discards every earlier source, palette
included, so a file loaded that way has to be a complete theme.

### Change what a mode shows

```css
configuration {
    drun-display-format: "{name}";
    window-format:       "{c}  {t}";
    show-icons:          true;
}
```

### Bind the surfaces in hikari-sakura

```
actions {
  menu          = "sofi -show drun"
  windows       = "sofi -show window"          # the control panel
  sheets        = "sofi -show sheets"
  volume        = "sofi -show volume"
  network       = "sofi -show network"
  bluetooth     = "sofi -show bluetooth"
  notifications = "sofi -show notification-history"
  notify-clear  = "sofi -notification-clear"
}

bindings {
  keyboard {
    "L+Space" = action-menu
    "L+w"     = action-windows
    "L+e"     = action-sheets
    "L+v"     = action-volume
    "L+i"     = action-network
    "L+b"     = action-bluetooth
    "L+n"     = action-notifications
    "L+S+n"   = action-notify-clear
  }
}
```

Long-running services go in your autostart, not on a key. **Which ones depends
on whether you run [saber](https://github.com/orpheus497/saber)**, because
exactly one process per session bus can host the system tray:

```sh
# With saber: the tray comes from saber.
sofi -notification-daemon &
saber &
```

```sh
# Without saber: sofi hosts the tray as well.
sofi -notification-daemon &
sofi -tray-daemon &
```

The tray host must be running **before** the applications whose icons you want:
a StatusNotifierItem application asks once at its own startup whether a host
exists, and one that finds none never asks again. That is also why changing
which host runs means restarting those applications afterwards.

### Trigger a sofi surface from saber, or from any script

There is no sofi IPC socket and none is needed. Each surface is one invocation
holding its own instance lock, so running it twice does not stack two copies —
a panel button, a keybinding and a shell script all reach sofi identically:

| Surface | Command |
|---|---|
| Application menu | `sofi -show drun` |
| Task and window manager | `sofi -show window` |
| Sheet switcher | `sofi -show sheets` |
| Volume | `sofi -show volume` |
| Network | `sofi -show network` |
| Bluetooth | `sofi -show bluetooth` |
| Notification history | `sofi -show notification-history` |
| Message toast | `sofi -e "text"` |

Each exits non-zero when it cannot do its job, so it composes in a script.

### Let the network and bluetooth panes change things

Both panes read fine as your own user. Changing anything — joining a network,
pairing a device, resetting a controller — needs root, and **sofi installs
nothing setuid**. One option covers both modes:

```css
configuration {
    network-privilege-command: "doas";
}
```

It is empty by default, because choosing how a machine escalates privilege is
an administrator's decision and guessing at `sudo` would be sofi making it for
you. The value is parsed as a command with its own arguments, so `"sudo -n"`
works as well as `"doas"`.

**It must not need a password on a terminal.** There is no terminal behind a
summoned menu, so an interactive `sudo` will simply hang. Use `doas` with a
`nopass` rule, or `sudo -n` with `NOPASSWD`, for the specific commands you want
to allow.

**The name says `network` and it serves bluetooth too.** That is deliberate
rather than an oversight: one machine escalates one way, and a second option
would only mean two places to get it wrong.

#### What each mode does without it

| Mode | Still works | Needs it |
|---|---|---|
| `network` | Listing interfaces and scanning | Joining, radio on/off, DHCP renewal, reconnect, reset |
| `bluetooth` | Every read: the adapter, connections, the neighbour cache, discovery | The paired-device list, pairing, forgetting, and the stack start/stop/restart. Connect, disconnect, discoverability and controller reset are tried unprivileged first and escalate only if the kernel refuses them |

The bluetooth pane's paired list is the case worth knowing about: on FreeBSD
`/etc/bluetooth/hcsecd.conf` is `0600 root`, so without a privilege command sofi
cannot see which devices are paired. It says so in the message bar rather than
showing an empty list, because "nothing is paired" and "sofi cannot see what is
paired" are different situations.

When a privileged verb fails, the warning names this option and says what to set
it to — so the difference between "the button does nothing" and "the button
needs a line of configuration" does not have to be found by reading the source.

**Do not drive `org.sofi.Tray` from outside sofi.** It is private between two
sofi processes and its signature changes with the build. `org.sofi.Notifications`
**is** public and stable, for acting on notifications without opening a panel:
`DismissAll`, `ClearHistory`, `Dismiss(u)`, `InvokeAction(u,u)` and
`GetLive() → a(uus)`.

### Restyle the system tray

The tray lives in the task strip's right-hand corner. Two widgets:

```css
/* ~/.config/sofi/config.sasi */
tray {
    spacing: 12px;      /* between icons */
    padding: 0px 6px;
}

tray-icon {
    size:    24px;
    padding: 3px;
}
```

Both rules reach every icon, because they all share the `tray-icon` name — which
item each one is lives in sofi rather than in the widget's name.

Deliberately no border by default: a box with no children still has padding, so
it still has width, so a border on it still draws — and an empty tray is the
normal case when no tray daemon is running. Add one only if you always run one.

### Tray mouse buttons

Clicking an icon opens that application's menu inside the strip. The three
buttons are ordinary bindings, so you can swap them:

```css
configuration {
    mt-activate:           "MousePrimary";     /* menu, or Activate */
    mt-context-menu:       "MouseSecondary";   /* menu, or ContextMenu */
    mt-secondary-activate: "MouseMiddle";      /* SecondaryActivate */
}
```

They sit in their own binding scope, which is what stops a right click over a
tray icon reaching `kb-cancel` and closing the panel. Right click still cancels
everywhere else.

## The configuration block

Behavioural options live in a `configuration` block:

```css
configuration {
    modes:       "drun,run,ssh";
    font:        "Hurmit Nerd Font Mono 10";
    show-icons:  true;
}
```

To start from a file listing every option with its current value, commented
where it is still the default:

```bash
sofi -dump-config > ~/.config/sofi/config.sasi
```

For example:

```css
configuration {
/*  modes: "window,run,ssh,drun";*/
/*  font: "mono 12";*/
/*  location: 0;*/
/*  fixed-num-lines: true;*/
... cut ...
/*  me-accept-custom: "Control+MouseDPrimary";*/
}
```

And to dump the fully resolved theme for a surface:

```bash
sofi -show drun -dump-theme > ~/.config/sofi/current.sasi
```

## File format

### Encoding

UTF-8. Both Unix (`\n`) and Windows (`\r\n`) newlines are accepted; Unix is
preferred.

### Comments

C and C++ comments are supported, and C comments can nest and appear inline.

```css
// Magic comment.
property: /* comment */ value;
```

This, however, is not valid:

```css
prop/*comment*/erty: value;
```

### White space

White space and newlines are ignored by the parser, so this:

```css
property: name;
```

is identical to:

```css
     property             :
name

;
```

### Data types

**String** — always in double quotes:

```css
ml-row-down: "ScrollDown";
```

**Number** — any whole number:

```css
eh: 2;
```

**Boolean** — `true` or `false`, case-sensitive. `show-icons: true;` is the same
as the `-show-icons` flag, and `false` the same as `-no-show-icons`:

```css
show-icons: true;
```

**List** — comma-separated, in brackets:

```css
combi-modes: [ssh,drun];
```

A comma-separated string is also accepted:

```css
combi-modes: "ssh,drun";
```

**Colour** — CSS syntax: `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, `rgb()`,
`rgba()`, `hsl()`, or a CSS colour name. A reference to another name is written
`@name`.

Note that a reference cannot carry alpha: `@color0 / 50%` is not valid, because
the `/ percentage` form applies to CSS colour names only. Write the eight-digit
hex instead.

## Finding every option

Three ways:

1. `sofi -dump-config` — every option and its current value.
2. `sofi -h` — the command-line surface.
3. The manpages, which describe what the values mean.

Not every option appears in the manpages, since plugins can add options at
runtime.

## Splitting configuration over multiple files

```css
/* ~/.config/sofi/config.sasi */
configuration {
}

@import "myConfig"
@theme "MyTheme"
```

Sofi parses the `configuration` block first, then
`~/.config/sofi/myConfig.sasi`, then loads the theme `MyTheme`. Imports can
nest. For the difference between `@import` and `@theme`, see
**sofi-theme(5)**.

One limitation worth knowing: sofi's own compiled-in layouts cannot `@import`
anything, because a resource compiled into the binary has no directory to
resolve a relative path against. That is why the palette is parsed for them at
startup, and why the installed copy of the theme uses `@import` while the
built-in one does not.
