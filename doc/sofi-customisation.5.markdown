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
2. **One** palette, compiled in, chosen by `-palette` and defaulting to
   `sakura`, then `/org/sofi/roles.sasi` — also compiled in. The sixteen slots
   and the names mapped onto them, described below. If hikari-sakura's
   configuration is readable, its palette is applied between the two.
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

Colours come from two compiled-in files, because they are two different things.

A **palette** holds the sixteen positional slots, `color0` to `color15`, in the
conventional terminal arrangement — 0-7 normal, 8-15 bright — plus the five
tones that have to be derived from them. The slots carry no meaning. They exist
so that one sixteen-value scheme can dress the compositor, its top bar and this
shell at once, and so that a terminal colorscheme can be dropped in whole.

`/org/sofi/roles.sasi` holds the **semantic names**. These are what the layouts
actually use, and every one of them is a reference, so they all re-derive from
whichever palette is in force.

Three palettes ship, matching hikari-sakura's three:

| `-palette` | Scheme |
|---|---|
| `sakura` | Violet and dusty rose. The default |
| `sakura-dark` | Ash black, muted berry and plum |
| `sakura-light` | Pearl white, emerald and slate blue. Inverts the text pair |

```bash
sofi -show drun -palette sakura-light
```

Use `-palette`, not `-theme`: `-theme` discards everything parsed before it, so
a file named there would have to carry every layout rule as well as the colours.
To pin a palette for every invocation instead of passing the flag, see
**PIN A PALETTE** below.

**A readable hikari-sakura palette overrides this flag.** The compositor's slots
are imported after the palette the flag selected, and the cascade is last-wins
— so on a machine whose `hikari.lua` carries a complete palette, `-palette`
chooses a floor that is then replaced, and the only trace is one informational
log line. See **FOLLOWING THE COMPOSITOR** below; `-dump-theme` shows which
values actually won.

### The names

| Name | Resolves to | Used for |
|---|---|---|
| `background` | color0 at 90% | Panel grounds |
| `background-solid` | color0 | Notification cards and the toast, which must be opaque |
| `surface` | derived | Inset fields — the filter box, the scrollbar trough |
| `surface-alt` | derived | A field sitting on `surface`; the current sheet |
| `border-soft` | color8 at 60% | Hairline separators |
| `hint` | derived | Placeholder text |
| `foreground` | color7 | Body text |
| `foreground-bright` | color15 | Emphasis |
| `foreground-dim` | derived | Secondary text — app names, timestamps, counts |
| `muted` | color8 | **Non-text only**: separators, troughs, disabled fills |
| `accent` | **measured**: color12 or color4 | Selection |
| `accent-soft` | color4 | Prompts, leading stripes |
| `accent-strong` | color13 | The current sheet, a live notification |
| `on-accent` | **measured**: color0 or color15 | Text drawn **on** an accent fill |
| `urgent` | **literal red** | Critical, as text |
| `critical` | the same literal | Critical, as a fill or stripe |
| `warning` | color11 | Counts and truncation markers |

Five of these are not a fixed slot, and the reason is the same in every case: a
slot index that reads well on one palette can be invisible on the next.

- **`accent`** is whichever of `color12` and `color4` measures better against
  that palette's own ground — `color12` on both dark palettes, `color4` on
  Sakura Light, where `color12` reads at only 3.32:1.
- **`on-accent`** is whichever of `color0` and `color15` measures better against
  the accent that resolved. This is the most important pair in the set, and it
  is the one a fixed slot gets wrong the moment a palette inverts its text pair:
  naming `color0` is right on Sakura and on Sakura Light for opposite reasons,
  and wrong on Sakura Dark.
- **`urgent` and `critical`** are a literal red held outside every palette and
  chosen by the ground's luminance: `#df8787` on a dark ground, `#9b2226` on a
  light one. No single red clears AA against both the darkest and the lightest
  ground — the two requirements do not overlap — so there are two. `#df8787` is
  the default palette's own `color9`, so nothing about Sakura changes.
- **`foreground-dim`** is a tone each palette derives against its own ground.
  `color6` carries this role legibly on Sakura and collapses to 2.33:1 and
  1.75:1 on the other two.

### Contrast

Some of these are constrained by measurement and not by taste.

`muted` is 2.29:1 against the ground on Sakura and lower on the others; it must
never carry text — use `foreground-dim`. `accent-soft` is `color4`, and its
answer differs on all three palettes: 4.31:1 on Sakura, which is AA for large or
bold text but short of body copy; 4.97:1 on Sakura Light, which clears body
copy; and 1.67:1 on Sakura Dark, where it has to be a fill or a mark and never
text.

`roles.sasi` records the measured ratio of every role on every palette, and
records which ones fall short. The known shortfalls, rather than hidden:

- **The accent family on Sakura Dark.** Every accent candidate in that palette
  measures 2.5-2.8:1 against its ground, because its 8-15 band is mid-tone
  rather than bright. There is no rescue to pick. Use the accent for fills and
  marks there, and `foreground` for anything that must be read.
- **`accent-strong` and `warning` on both new palettes**, which land in the
  2.1-2.8:1 band.
- **`hint` on both new palettes**, where the derivation rule meets a `color6`
  sitting close to its own ground.
- **`accent` and `accent-soft` are the same colour on Sakura Light**, because
  `color4` is both the better accent and the soft accent's own slot.

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

The one thing to know is which slot a role resolved to on the palette you are
using, because five of them are measured rather than fixed. On Sakura Light,
redefining `color12` does not move the selection — `color4` does. Redefining
`accent` works on every palette, and so does redefining `urgent`, which no slot
determines at all.

## PIN A PALETTE

`-palette` is per invocation. To choose one for every invocation, the installed
theme ships an optional import for exactly this:

```css
/* ~/.config/sofi/config.sasi */
?import "palette"
```

`?import` does not fail when the file is absent, which is what makes shipping
that line safe — though it does log one line to stderr saying it looked. Create
`~/.config/sofi/palette.sasi` and it takes over; `~/.config/sofi/themes/` works
too.

Put a whole palette in it rather than only the slots you want moved: the five
derived tones and the two measured picks belong to the palette they were
computed for, so a file that redefines the sixteen slots and leaves them alone
gets another scheme's text over Sakura's surfaces. Redefining individual slots in
a plain `* { }` block, as above, is the right tool for a small change; a pinned
palette is for a whole scheme.

## FOLLOWING THE COMPOSITOR

If hikari-sakura's configuration is readable, sofi reads its palette from it and
applies it over the compiled-in one, so changing the compositor's theme retints
these panels too instead of leaving them behind. It looks for
`$XDG_CONFIG_HOME/hikari/hikari.lua` and then `~/.config/hikari/hikari.lua`,
expects the file to return a table, and takes the palette from `ui.palette` or
from a top-level `palette`, as `color0` to `color15` string keys.

All sixteen or none: a palette half taken from the compositor would have its
derived tones computed across two schemes. A file that is missing, unreadable,
malformed or carries no complete palette is the ordinary case and is silent —
the compiled-in palette stands.

**This overrides `-palette`**, because it is applied over whichever palette the
invocation selected and the compositor is meant to be the one source of colour
for the whole desktop. A complete palette here replaces all sixteen slots and
the tones derived from them, so `-palette sakura-light` on such a machine is
inert. To override in the other direction, write the slots in your own config,
which is parsed last.

The file is evaluated with `io`, `os`, `package`, `require` and `load` out of
reach. A menu program has no business running processes out of a configuration
file.

Your own `~/.config/sofi/config.sasi` is parsed after all of this, so it still
wins.

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

**Dismiss is hidden when no notification daemon is running.** With nothing on
screen there is nothing for it to retire, and a button that silently does
nothing is worse than one that is not there. Clear is unaffected: discarding the
stored history works with or without a daemon.

Mechanically the mode sets `button-dismiss-all { enabled: false; }`, and a
disabled widget is not drawn at all rather than greyed out — so what you see is
the button's absence. It is applied by the mode rather than written in the
layout, which means a `button-dismiss-all { enabled: true; }` in your own config
will not bring it back: it is hidden because it cannot work, not as a matter of
taste.

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

## THE SYSTEM TRAY ZONE

The task strip's right-hand corner is fed by `sofi -tray-daemon`. Unlike every
other widget in a layout its contents are not known when the theme is parsed:
they arrive over D-Bus and change while the strip is on screen.

| Widget | What it is |
|---|---|
| `tray` | The horizontal box holding the icons |
| `tray-icon` | Every icon. One rule styles them all |

```css
tray {
    spacing: 12px;
    padding: 0px 6px;
}

tray-icon {
    size:    24px;
    padding: 3px;
}
```

Each icon has the same widget name on purpose, so a single rule styles the zone;
which item an icon *is* lives in sofi rather than in the widget's name. That also
means a theme cannot target one application's icon.

**The shipped layout gives `tray` no border, and that is deliberate.** A box with
no children still has its padding, so it still has width, so a border on it still
draws — and an empty tray is the ordinary case whenever no tray daemon is
running. A rule floating in an empty corner reads as a defect. Add one only if
you always run the daemon.

Two properties do nothing here. `filename` is overwritten, because the icon is
supplied by the application rather than by the theme; and a selection colour has
nothing to select, because the zone is not a list.

## STARTING FROM THE SHIPPED THEME

If you would rather have a whole file to edit than a set of overrides, the
application-menu layout is installed as an ordinary theme:

```bash
mkdir -p ~/.config/sofi
cp /usr/local/share/sofi/themes/config.sasi ~/.config/sofi/config.sasi
cp /usr/local/share/sofi/themes/colors-default.sasinc ~/.config/sofi/
```

(`/usr/local/share` is the default prefix; substitute your own if you configured
one.) `colors-default.sasinc` is the whole colour layer of the default palette —
slots, derived tones, measured picks and semantic names in one file; `config.sasi`
is the layout and imports it. It is one file there because a theme reached
through `-theme` has had the compiled-in resources freed out from under it, so
the installed copy has to stand on its own.

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
- **`muted` as a text colour.** It is a 2.29:1 tone on the default palette, and
  lower on the other two, kept for separators. It will look fine to you on the
  monitor you tuned it on and be unreadable elsewhere.
- **`-theme` to switch palette.** It resets the whole theme first, so the file it
  names would have to carry every layout rule too. Use `-palette`.

## SEE ALSO

**sofi(1)**, **sofi-theme(5)**, **sofi-keys(5)**, **sofi-debugging(5)**

## AUTHOR

orpheus497

See **AUTHORS** for the full list of contributors, including those of the
project sofi was forked from.
