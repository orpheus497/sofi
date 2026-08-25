# sofi-customisation(5)

## NAME

**sofi-customisation** - recolouring, moving and restyling sofi's surfaces

## DESCRIPTION

**sofi** works with no configuration file at all. Every surface has a layout
compiled into the binary, and a palette compiled in beside it. This page is
about changing them.

It is organised by what you want to do rather than by what the format can
express. For the complete theme format see **sofi-theme(5)**; for the
`configuration {}` block see **sofi(1)**.

Nothing here requires copying a theme. Every example is a small file that
*overrides* the built-in one.

## WHAT IS LOADED, AND IN WHAT ORDER

This order is the whole basis of customising sofi, so it is worth reading once:

1. `/org/sofi/default_configuration.sasi` — compiled in. Global defaults.
2. `/org/sofi/palette.sasi` — compiled in. The colour names, described below.
3. **One** panel layout, compiled in, chosen from the mode you asked for:

    | Invocation | Layout |
    |---|---|
    | `sofi -show window` | `panel-window.sasi` |
    | `sofi -show sheets` | `panel-sheets.sasi` |
    | `sofi -show notification-history` | `panel-notification-history.sasi` |
    | `sofi -notification-daemon` | `panel-notifications.sasi` |
    | `sofi -e <message>` | `panel-notify.sasi` |
    | anything else | `default.sasi` |

4. A system `sofi.sasi`: the first one found in `$XDG_CONFIG_DIRS`, otherwise
   the one in `SYSCONFDIR`. Only the first match is read, never several merged.
5. `~/.config/sofi/config.sasi`, if it exists.
6. Whatever `-theme` names, if given.
7. Any `-theme-str` snippets, in the order given.

**Later wins.** A property set at step 5 beats the same property at step 2. So
your config file does not replace sofi's layout, it edits it — which is why the
examples below are three or four lines rather than three hundred.

Step 6 is the exception. `-theme` behaves like `@theme`: it discards everything
parsed before it, palette and layout alike, and the named file becomes the whole
theme. `-theme-str` is the one that merges, which is why the per-invocation
examples below use it.

To see the result for a given surface:

```bash
sofi -show drun -dump-theme
```

## THE PALETTE

Colours are named in one place, `/org/sofi/palette.sasi`, in two layers.

**Sixteen positional slots**, `color0` to `color15`, in the conventional
terminal arrangement — 0-7 normal, 8-15 bright. These carry no meaning. They
exist so that one sixteen-value scheme can dress the compositor, its top bar and
this shell at once, and so that a terminal colorscheme can be dropped in whole.

**Semantic names**, each a reference to a slot. These are what the layouts
actually use:

| Name | Slot | Used for |
|---|---|---|
| `background` | color0 at 90% | Panel grounds |
| `background-solid` | color0 | Notification cards and the toast, which must be opaque |
| `surface` | derived | Inset fields — the filter box, the scrollbar trough |
| `surface-alt` | derived | A field sitting on `surface`; the current sheet |
| `border-soft` | color8 at 60% | Hairline separators |
| `hint` | derived | Placeholder text |
| `foreground` | color7 | Body text |
| `foreground-bright` | color15 | Emphasis |
| `foreground-dim` | color6 | Secondary text — app names, timestamps, counts |
| `muted` | color8 | **Non-text only**: separators, troughs, disabled fills |
| `accent` | color12 | Selection |
| `accent-soft` | color4 | Prompts, leading stripes |
| `accent-strong` | color13 | The current sheet, a live notification |
| `on-accent` | color0 | Text drawn **on** an accent fill |
| `urgent` | color9 | Critical, as text |
| `critical` | color1 | Critical, as a fill or stripe |
| `warning` | color11 | Counts and truncation markers |

Three of these are constrained by contrast and not by taste. `muted` is 2.29:1
against the ground and must never carry text — use `foreground-dim`. `critical`
is 4.07:1, so it is for fills rather than text — use `urgent`. `on-accent` is
`color0` rather than white because white on `color12` is 2.40:1 and unreadable.

## RECOLOUR EVERYTHING

Redefine the slots. Every surface follows, because every semantic name is a
reference to one:

```css
/* ~/.config/sofi/config.sasi */
* {
    color0:   #1c1b22;
    color12:  #89b4fa;
    color9:   #f38ba8;
}
```

You do not have to supply all sixteen — anything you leave alone keeps its
built-in value.

If you are porting a terminal colorscheme, paste all sixteen and you are done;
the same sixteen values also go into hikari-sakura's `ui { palette }` block, and
the two programs will then agree.

## RECOLOUR ONE ROLE

Redefine the semantic name instead of the slot, when you want to change what
something is *for* without disturbing everything else drawn from that slot:

```css
* {
    accent: #f5c2e7;   /* selections only; color12 keeps its other jobs */
}
```

## MOVE A PANEL

Each surface is placed by `location`, `anchor` and the two offsets. Every layout
calls its panel `window`, and `~/.config/sofi/config.sasi` is read for every
invocation — so a `window { }` block written there moves *all six* surfaces at
once, on every invocation that does not pass `-theme`. (One that does starts the
theme over from the file it names and never sees that block at all.) Scope the
rules to one invocation to move one panel.

To make the application menu a left-edge side panel instead of a bottom-centre
one, leaving the task strip, the sheet row, the notification stack and the toast
where they are:

```bash
sofi -show drun -theme-str 'window {
    location: west;
    anchor:   west;
    width:    300px;
    height:   76%;
    y-offset: 0px;
}'
```

`-theme-str` merges over the currently loaded theme — the built-in layout, or
whatever `-theme` replaced it with — so only the properties named here change
and the rest of that theme survives. `-theme` is not an alternative: it throws
away every earlier source, the palette included, and the file it names has to be
a complete theme.

`y-offset` is reset because the shipped layout uses it to clear the task strip,
which a side panel does not need to do.

**Offset signs differ between two kinds of surface**, and this trips people up:

- Menus (`drun`, `window`, `sheets`, `notification-history`) cover the whole
  screen with an invisible surface so a click anywhere outside dismisses them,
  and the panel is positioned inside it. There, **positive moves inward** from
  the anchored edge.
- The notification banner and the `-e` toast do not do that, and their offsets
  become layer-shell margins, where **the sign is inverted**.

If a panel moves the wrong way when you change an offset, that is why.

### On the top edge

`location: north` is already flush beneath hikari-sakura's top bar. The
compositor subtracts the bar from the usable area before sofi is given a size,
so an offset there is a gap you chose, not clearance you owe. Do not add the
bar's height.

## RESIZE A PANEL

```css
window {
    width:  360px;
    height: 80%;
}
```

`height` is ignored for surfaces that size themselves to their content — the
notification banner and the toast.

For a list, the number of visible rows is usually what you actually want:

```css
listview {
    lines: 16;
}
```

The sheet switcher is the exception. It is a fixed ten-cell grid, so its `width`
is what decides how much room each chip's label gets — raise it if you use a
large font and the window counts start ellipsizing.

## RESTYLE ONE SURFACE AND NOT THE OTHERS

A plain `~/.config/sofi/config.sasi` is parsed for every invocation that does
not pass `-theme`, so anything in it applies everywhere. To reach exactly one
surface, override on that invocation:

```bash
sofi -show window -theme-str 'window { width: 90%; } element { padding: 4px; }'
```

For more than a couple of properties, pass several `-theme-str` snippets: each
merges over the currently loaded theme in turn. Reserve `-theme` for a surface
that should look genuinely different rather than merely adjusted — it discards
the built-in layout *and* the palette, so the file it names has to define
everything it uses, `@import`ing your own colours if it wants any.

## THE NOTIFICATION CLEANUP ACTIONS

The history menu carries two deliberately separate verbs, as both keybindings
and buttons:

| Action | Binding | Effect |
|---|---|---|
| Dismiss | `kb-custom-1` | Retire every notification still on screen. History is kept |
| Clear | `kb-custom-2` | Discard everything, on screen and in history |

They are separate on purpose: clearing banners off your screen should not also
lose the list of what you missed.

The live banner carries the Dismiss action only, as a button. It takes no
keyboard at all — a banner you did not ask to open must not steal focus — so a
button is the only way to reach it.

Both verbs are also one-shot command-line flags, which is how you bind them in a
compositor:

```
sofi -notification-clear
sofi -notification-clear-history
```

These exit non-zero and change nothing if no sofi notification daemon is
running. That is deliberate: the daemon owns the notification ring, and a
separate process writing the history file would simply be overwritten the next
time the daemon saved.

### Binding them in hikari-sakura

```
actions {
  notifications = "sofi -show notification-history"
  notify-clear  = "sofi -notification-clear"
}

bindings {
  keyboard {
    "L+n"  = action-notifications
    "L+S+n" = action-notify-clear
  }
}
```

## STARTING FROM THE SHIPPED THEME

If you would rather have a whole file to edit than a set of overrides, the
application-menu layout is installed as an ordinary theme:

```bash
mkdir -p ~/.config/sofi
cp /usr/local/share/sofi/themes/config.sasi ~/.config/sofi/config.sasi
cp /usr/local/share/sofi/themes/colors-default.sasinc ~/.config/sofi/
```

(`/usr/local/share` is the default prefix; substitute your own if you configured
one.) `colors-default.sasinc` is the palette; `config.sasi` is the layout and
imports it.

What it does **not** do is stay confined to the application menu. A config file
cannot know which surface it was loaded for, and it is parsed after whichever
layout the invocation selected — so its `window`, `mainbox`, `listview` and
`element` rules reach the task strip, the sheet row, the notification stack, the
toast and the history panel as well, all five of which then lose the geometry
their own layouts gave them. Copy it as a starting point for a theme you intend
to edit, not as a drop-in that leaves the other surfaces alone.

The exception is an invocation that passes `-theme`. That discards `config.sasi`
along with the built-in layout, and the named file becomes the whole theme --
which is the reliable way to keep one surface out of the copied layout's reach.

## THINGS THAT WILL NOT WORK

- **`@import` inside a built-in layout.** The compiled-in layouts cannot import
  anything, because a GResource has no directory to resolve a path against. That
  is why the palette is parsed for them at startup instead, and why the
  installed copy of the theme uses `@import` while the built-in one does not.
- **Alpha on a palette reference.** `@color0 / 50%` is not valid; the `/
  percentage` form applies to named CSS colours only. Write the eight-digit hex,
  as `background` does.
- **A live reload.** sofi is one process per invocation. A palette edit is
  picked up the next time you summon a panel, which for a menu is immediately
  and for the notification daemon means restarting it.
- **`muted` as a text colour.** It is a 2.29:1 tone kept for separators. It will
  look fine to you on the monitor you tuned it on and be unreadable elsewhere.

## SEE ALSO

**sofi(1)**, **sofi-theme(5)**, **sofi-keys(5)**, **sofi-debugging(5)**

## AUTHOR

orpheus497

See **AUTHORS** for the full list of contributors, including those of the
project sofi was forked from.
