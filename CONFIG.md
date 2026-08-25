> This page covers the common cases. For the full option list see the manpages —
> **sofi(1)** for behaviour, **sofi-theme(5)** for the format, and
> **sofi-customisation(5)** for theming in depth.

Sofi works with no configuration file at all. Every surface has a layout
compiled into the binary and a palette compiled in beside it. Everything below
is optional, and *overrides* the built-in defaults rather than replacing them.

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
4. `~/.config/sofi/config.sasi`.
5. `-theme <file>`, if given.

**Later wins, property by property.** Your config file edits the built-in
layout; it does not replace it. Colour references resolve after all five have
been read, so redefining a colour name in step 4 reaches the layout from step 3.

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
everywhere. To reach exactly one surface, put the rules in their own file and
load it only for that invocation:

```bash
sofi -show window -theme ~/.config/sofi/taskbar.sasi
```

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
  windows       = "sofi -show window"
  sheets        = "sofi -show sheets"
  notifications = "sofi -show notification-history"
  notify-clear  = "sofi -notification-clear"
}

bindings {
  keyboard {
    "L+Space" = action-menu
    "L+w"     = action-windows
    "L+e"     = action-sheets
    "L+n"     = action-notifications
    "L+S+n"   = action-notify-clear
  }
}
```

The notification daemon is long-running — start it from your autostart, not from
a key:

```sh
sofi -notification-daemon &
```

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
combi-modes: [window,drun];
```

A comma-separated string is also accepted:

```css
combi-modes: "window,drun";
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
