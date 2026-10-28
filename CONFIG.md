# Configuring Sofi

> This page covers the common cases, task first. For everything else:
> [FEATURES.md](FEATURES.md) is the reference by capability, **sofi(1)** the
> reference by flag, **sofi-theme(5)** the format, and **sofi-customisation(5)**
> theming in depth.

Sofi works with no configuration file at all. Every surface has a layout
compiled into the binary and a palette compiled in beside it. Everything below
is optional, and *overrides* the built-in defaults rather than replacing them.

> **Sofi does not read rofi's configuration.** The paths are `~/.config/sofi/`
> and the extension is `.lua`, with no fallback to the rofi equivalents.
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
file is `sofi.lua`.

**The file named after the program is yours; `settings.lua` is the one that
shipped.** That rule holds across the desktop — `sofi.lua`, `saber.lua` and
`hikari.lua` are all files you write, and each component reads its
distributor's defaults from a `settings.lua` beside them. It is worth stating
because sofi had it the other way round: `sofi.lua` was the *system* file and
your own was `config.lua`, so the program's own name meant opposite things in
two components of one desktop, and anyone following saber's convention wrote
`~/.config/sofi/sofi.lua` and was ignored without a word. A `config.lua` from
before the rename is still read when `sofi.lua` is absent, and sofi says so
once.

Create it only if you want to change something:

```bash
mkdir -p ~/.config/sofi
$EDITOR ~/.config/sofi/sofi.lua
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
4. The distributor's `sofi/settings.lua` — the first found in
   `$XDG_CONFIG_DIRS`, otherwise the one under `SYSCONFDIR` (usually
   `/etc/sofi/settings.lua`). Only the first match is read; several are never
   merged. A loose `sofi.lua` directly in either directory is the name this
   file had before and is still read when no `settings.lua` is found.
5. `~/.config/sofi/sofi.lua`.
6. `-theme <file>`, if given.
7. Any `-theme-str` snippets, in the order given.

**Later wins, property by property** — with one exception. Steps 1–5 and 7
merge, so your config file edits the built-in layout rather than replacing it,
and the distributor's file shows up in a `-dump-theme` the same way your own
does. Colour references resolve after every source has been read, so redefining
a colour name in step 4 or 5 reaches the layout from step 3.

`-theme` is the exception: like `{ reset = true }`, it *discards* everything
parsed before it and the named file becomes the whole theme. Use `-theme-str`
when you want to override a handful of properties for one invocation.

To see what a surface actually resolved to:

```bash
sofi -show drun -dump-theme
sofi -show window -dump-theme
```

## Recipes

### Recolour everything

Redefine the positional slots. Every surface follows, because every semantic
name is a reference to one:

```lua
-- ~/.config/sofi/sofi.lua
return {
  { select = "*",
    color0  = "#1c1b22",
    color12 = "#89b4fa",
    color9  = "#f38ba8",
  },
}
```

Supply as many or as few as you like; anything you leave alone keeps its
built-in value. The full sixteen are listed in the README.

These are the same sixteen values hikari-sakura takes in its `ui.palette`
block, so a terminal colorscheme pasted into both makes the compositor and the
shell agree.

**Five names are literals, not references**, because the format can neither
blend two colours nor apply alpha to a `"@reference"`. They keep their old values
until you recompute them:

| Name | Derived from |
|---|---|
| `background` | `color0` at 90% |
| `surface` | `color0` 25% toward `color8` |
| `surface-alt` | `color0` 50% toward `color8` |
| `border-soft` | `color8` at 60% |
| `hint` | `color0` 70% toward `color6` |

So the example above, which moves `color0`, also needs:

```lua
return {
  { select = "*",
    background  = "#1c1b22e6",   -- color0 at 90%
    surface     = "#2d2b33",     -- color0 25% toward color8
    surface_alt = "#3d3a44",     -- color0 50% toward color8
    hint        = "#78787e",     -- color0 70% toward color6
  },
}
```

`border-soft` comes from `color8`, which that example leaves alone. Everything
else — `foreground`, `accent`, `on-accent` and the rest — is a plain reference
and follows its slot on its own.

### Recolour one role

Redefine the semantic name instead, when you want to change what something is
*for* without disturbing everything else drawn from that slot:

```lua
return {
  -- selections only; color12 keeps its other jobs
  { select = "*", accent = "#f5c2e7" },
}
```

Three names are contrast-constrained: `muted` must never carry text, `critical`
is for fills rather than text, and `on-accent` is what goes on top of an accent
fill. See **sofi-customisation(5)**.

### Move a panel

```lua
return {
  { select = "window",
    location = west,
    anchor   = west,
  },
}
```

Valid positions: `north`, `north_east`, `east`, `south_east`, `south`,
`south_west`, `west`, `north_west`, `center`. These are bare keywords, not
strings, so a misspelling is an error rather than a value nothing understands.

**Offset signs differ between two kinds of surface.** Menus cover the screen
with an invisible click-catching surface and position the panel inside it, so
positive offsets move *inward* from the anchored edge. The notification banner
and the `-e` toast use layer-shell margins instead, where the sign is inverted.
If a panel moves the wrong way, that is why.

On the top edge, `location = north` is already flush beneath hikari-sakura's bar
— the compositor subtracts it before sofi sees a size. An offset there is a gap
you chose, not clearance you owe.

### Resize a panel

```lua
return {
  { select = "window",
    width  = px(360),
    height = percent(80),
  },

  { select = "listview", lines = 16 },
}
```

`height` is ignored by surfaces that size themselves to their content — the
notification banner and the toast. For lists, `lines` is usually what you
actually want.

The sheet switcher is a fixed ten-cell grid, so the `width` on its `window` is
what decides how much room each chip's label gets.

### Restyle one surface only

`sofi.lua` is read for every invocation, so anything in it applies
everywhere — a `{ select = "window" }` rule there moves *every* panel.

That holds for every invocation that does not pass `-theme`. One that does
starts the theme over from the named file, so `sofi.lua`'s *styling* is
discarded unless that file defines or imports it. Its `configuration` block is
untouched either way: it is not part of the theme.

To reach exactly one surface, override on that invocation only. `-theme-str`
takes a chunk of its own, so it returns an array like any other theme:

```bash
sofi -show window -theme-str 'return { { select = "window",
    location = west,
    anchor   = west,
} }'
```

`-theme-str` merges over the theme currently loaded, so only the properties you
name change. `-theme` does not: it discards every earlier source, palette
included, so a file loaded that way has to be a complete theme.

### Change what a mode shows

```lua
return {
  { configuration = {
      drun_display_format = "{name}",
      window_format       = "{c}  {t}",
      show_icons          = true,
  } },
}
```

### Bind the surfaces in hikari-sakura

In `~/.config/hikari/hikari.lua`. An action is a name for a shell command; a
binding points a key at `action-` plus that name. Both are entries in the table
that file returns, which is why each closes with `},`:

```lua
  actions = {
    menu = "sofi -show drun",
    windows = "sofi -show window",          -- the control panel
    sheets = "sofi -show sheets",
    volume = "sofi -show volume",
    network = "sofi -show network",
    bluetooth = "sofi -show bluetooth",
    notifications = "sofi -show notification-history",
    ["notify-clear"] = "sofi -notification-clear",
  },

  bindings = {
    keyboard = {
      ["L+Space"] = "action-menu",
      ["L+w"]     = "action-windows",
      ["L+e"]     = "action-sheets",
      ["L+v"]     = "action-volume",
      ["L+i"]     = "action-network",
      ["L+b"]     = "action-bluetooth",
      ["L+n"]     = "action-notifications",
      ["L+S+n"]   = "action-notify-clear",
    },
  },
```

Add these entries to the `actions` and `bindings.keyboard` tables **the shipped
file already has**. A second `actions = { ... }` beside the first does not
merge: Lua keeps the last assignment to a key and discards the earlier table
whole, without a word.

Keys and action names are bracketed and quoted wherever they are not plain Lua
names -- every key is, because of the `+`, and `notify-clear` is because of the
hyphen. hikari-sakura ships `menu`, `windows`, `sheets` and `notifications`
bound already; the other four are surfaces it leaves to the control panel, so
bind them only if you want keys of their own.

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
| Control panel | `sofi -show window` |
| Window switcher | `sofi -show windowlist` |
| Displays | `sofi -show display` |
| Sheet switcher | `sofi -show sheets` |
| Volume | `sofi -show volume` |
| Network | `sofi -show network` |
| Bluetooth | `sofi -show bluetooth` |
| Notification history | `sofi -show notification-history` |
| Message toast | `sofi -e "text"` |

Each exits non-zero when it cannot do its job, so it composes in a script.

### Let the network and bluetooth panes change things

Both panes read fine as your own user. Changing things generally needs root —
joining a network, pairing a device, starting or stopping the bluetooth stack —
and **sofi installs nothing setuid**.

The bluetooth pane's own HCI verbs are the exception: connect, disconnect,
discoverability and controller reset are attempted unprivileged first and
escalate only where the kernel refuses them, because which HCI commands the raw
socket gates varies. One option covers both modes:

```lua
return {
  { configuration = { network_privilege_command = "doas" } },
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

```lua
-- ~/.config/sofi/sofi.lua
return {
  { select = "tray",
    spacing = px(12),              -- between icons
    padding = { px(0), px(6) },
  },

  { select = "tray-icon",
    size    = px(24),
    padding = px(3),
  },
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

```lua
return {
  { configuration = {
      mt_activate           = "MousePrimary",     -- menu, or Activate
      mt_context_menu       = "MouseSecondary",   -- menu, or ContextMenu
      mt_secondary_activate = "MouseMiddle",      -- SecondaryActivate
  } },
}
```

They sit in their own binding scope, which is what stops a right click over a
tray icon reaching `kb-cancel` and closing the panel. Right click still cancels
everywhere else.

## The configuration block

Behavioural options live in a `configuration` block:

```lua
return {
  { configuration = {
      modes      = "drun,run,ssh",
      font       = "Hurmit Nerd Font Mono 10",
      show_icons = true,
  } },
}
```

An option whose name carries a hyphen is written with an underscore, and an
option sofi does not recognise is ignored without a warning.

To start from a file listing every option with its current value, commented
where it is still the default:

```bash
sofi -dump-config > ~/.config/sofi/sofi.lua
```

For example:

```lua
-- sofi -dump-config output.
--
-- An option that is commented out is one this invocation left at
-- its default. Uncomment it to set it.

return {

  { configuration = {
      -- modes = "drun,run",
      font = "Hurmit Nerd Font Mono 10",
      -- location = 0,
      -- ["fixed-num-lines"] = true,
      -- ... cut ...
      -- ["me-accept-custom"] = "Control+MouseDPrimary",
  } },
}
```

Note the two spellings. `font` is a Lua name, so it is written plainly; an
option whose name has a hyphen in it is not, so the dump brackets and quotes
it. Writing `fixed_num_lines` by hand works just as well -- the reader
substitutes a hyphen per underscore -- but the dump has no way to know which of
the two you would have preferred, so it prints the option's real name.

And to dump the fully resolved theme for a surface:

```bash
sofi -show drun -dump-theme > ~/.config/sofi/current.lua
```

Both dumps are Lua, and the theme dump is itself a theme: pass that file back
with `-theme` and you get the tree it was printed from. That is what makes it
usable as a starting point rather than only as something to read.

## File format

A configuration file is a theme, and a theme is a Lua chunk that returns an
array of directives. The behavioural options live in a `configuration` directive
inside that array; everything else in it styles widgets.

### Encoding

UTF-8. Both Unix (`\n`) and Windows (`\r\n`) newlines are accepted; Unix is
preferred.

### Comments

Lua comments: `--` to the end of the line, or `--[[ ... ]]` across several. A
long comment nests by adding equals signs to its brackets, and can appear
inline.

```lua
-- Magic comment.
return {
  { configuration = { dpi = --[[ comment ]] 72 } },
}
```

This, however, is not valid, because a comment cannot be put inside a name:

```text
prop--[[comment]]erty = value
```

### White space

White space and newlines are ignored, so this:

```lua
return {
  { configuration = { dpi = 72 } },
}
```

is identical to:

```lua
return
{
  { configuration =
      {
    dpi =
      72
  }
  }
  ,
}
```

A Lua table constructor takes `;` as a field separator as well as `,`.

### Data types

The type of a value comes from the Lua value, not from the option's name.

**String** — in double or single quotes, or in long brackets:

```lua
return {
  { configuration = { ml_row_down = "ScrollDown" } },
}
```

A string that must begin with `#` or `@` would otherwise read as a colour or a
reference, so it says so with `str("#general")`.

**Number** — an integer or a float, and the two are different types, so write
the one the option wants:

```lua
return {
  { configuration = { eh = 2 } },
}
```

**Boolean** — `true` or `false`, case-sensitive. `show_icons = true` is the same
as the `-show-icons` flag, and `false` the same as `-no-show-icons`:

```lua
return {
  { configuration = { show_icons = true } },
}
```

**List** — an array of strings:

```lua
return {
  { configuration = { combi_modes = { "ssh", "drun" } } },
}
```

A comma-separated string is also accepted:

```lua
return {
  { configuration = { combi_modes = "ssh,drun" } },
}
```

An array of lengths or of bare numbers is **padding**, not a list, so a numeric
list has to say so with `list(10, 20, 30)`. That is the one real trap in the
format.

**Length** — `px(n)`, `em(n)`, `ch(n)`, `mm(n)` or `percent(n)`, and a bare
number where a length belongs is pixels. Arithmetic replaces the old `calc()`:
`percent(100) - px(10)`, with `calc.min`, `calc.max`, `calc.round`, `calc.floor`
and `calc.ceil` for the five operations Lua has no operator for.

**Colour** — a hex string `"#RGB"`, `"#RGBA"`, `"#RRGGBB"` or `"#RRGGBBAA"`, or
`rgb()`, `rgba()`, `hsl()`, `hsla()`, `hwb()`, `cmyk()`, or `color("name")` for
a CSS colour name. A reference to another name is the string `"@name"`, or
`ref("name", default)` when it needs a fallback.

Note that a reference cannot carry alpha: there is no spelling for it, because
the alpha argument of `color()` applies to a colour and `"@color0"` is a
reference rather than one. Write the eight-digit hex instead.

**Environment** — `env` is a read-only table of this process's environment, so
`env.HOME` replaces the old `${HOME}` and `env(HOME)`. What it yields is always
a string, so a value that has to be something else is converted:
`percent(tonumber(env.WIDTH) or 40)`.

### What a configuration file may do

It is a program, so it is read under bounds. `io`, `os`, `package`, `require`,
`load` and `dofile` are not reachable from it, `string` and `table` are bounded
versions of themselves, and there are ceilings on how much it may allocate, on
the cumulative work those two libraries will do, and on how many instructions it
may run. Every file an `import()` chain pulls in shares the one allowance. A
file that trips any of it is declined with a diagnostic and whatever was already
loaded stands -- a downloaded theme is a downloaded program, which is the whole
reason those bounds are there.

## Finding every option

Three ways:

1. `sofi -dump-config` — every option and its current value.
2. `sofi -h` — the command-line surface.
3. The manpages, which describe what the values mean.

Not every option appears in the manpages, since plugins can add options at
runtime.

## Splitting configuration over multiple files

```lua
-- ~/.config/sofi/sofi.lua
return {
  { configuration = {
  } },

  import("myConfig"),

  { reset = true },
  import("MyTheme"),
}
```

Sofi carries out the directives in array order, so it applies the `configuration`
block first, then `~/.config/sofi/myConfig.lua`, then throws the theme away and
loads `MyTheme`. `import()` returns the imported file's own array and a directive
with no `select` is applied in place, which is what makes it usable as an element
like this; imports can nest, sixteen deep. `{ reset = true }` followed by an
import is how the old format's `@theme` is spelled -- for the difference between
the two, see **sofi-theme(5)**.

One limitation worth knowing: sofi's own compiled-in layouts cannot `import()`
anything, because a resource compiled into the binary has no directory to
resolve a relative path against. That is why the palette is parsed for them at
startup, and why the installed copy of the theme uses `import()` while the
built-in one does not.
