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

1. `/org/sofi/default_configuration.lua` — compiled in. Global defaults.
2. **One** palette, compiled in, chosen by `-palette` and defaulting to
   `sakura`, then `/org/sofi/roles.lua` — also compiled in. The sixteen slots
   and the names mapped onto them, described below. If hikari-sakura's
   configuration is readable, its palette is applied between the two.
3. **One** panel layout, compiled in, chosen from the mode you asked for:

    | Invocation | Layout |
    |---|---|
    | `sofi -show window` | `panel-window.lua` |
    | `sofi -show sheets` | `panel-sheets.lua` |
    | `sofi -show volume` | `panel-volume.lua` |
    | `sofi -show network` | `panel-network.lua` |
    | `sofi -show bluetooth` | `panel-bluetooth.lua` |
    | `sofi -show display` | `panel-display.lua` |
    | `sofi -show keys` | `panel-keys.lua` |
    | `sofi -show notification-history` | `panel-notification-history.lua` |
    | `sofi -notification-daemon` | `panel-notifications.lua` |
    | `sofi -e <message>` | `panel-notify.lua` |
    | anything else | `default.lua` |

    Only those exact names are matched. `-show windowlist`, a combi mode and a
    script mode all take `default.lua`, which is the safe general shape.

4. A system `sofi.lua`: the first one found in `$XDG_CONFIG_DIRS`, otherwise
   the one in `SYSCONFDIR`. Only the first match is read, never several merged.
5. `~/.config/sofi/config.lua`, if it exists.
6. Whatever `-theme` names, if given.
7. Any `-theme-str` snippets, in the order given.

**Later wins.** A property set at step 5 beats the same property at step 2. So
your config file does not replace sofi's layout, it edits it — which is why the
examples below are three or four lines rather than three hundred.

Step 6 is the exception. `-theme` behaves like `{ reset = true }`: it discards
everything parsed before it, palette and layout alike, and the named file becomes
the whole theme. `-theme-str` is the one that merges, which is why the
per-invocation examples below use it.

To see the result for a given surface:

```bash
sofi -show drun -dump-theme
```

## THE PALETTE

Colours come from two compiled-in files, because they are two different things.

A **palette** holds the sixteen positional slots, `color0` to `color15`, in the
conventional terminal arrangement — 0-7 normal, 8-15 bright — plus the five
tones that have to be derived from them and the eight values a role map cannot
get from a slot index at all. The slots carry no meaning. They exist so that one
sixteen-value scheme can dress the compositor, its top bar and this shell at
once, and so that a terminal colorscheme can be dropped in whole.

`/org/sofi/roles.lua` holds the **semantic names**. These are what the layouts
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
| `accent-soft` | **derived** from color4 or color5 | Prompts, leading stripes |
| `accent-strong` | **derived**: color13, or a tone lifted out of it | The current sheet, a live notification |
| `on-accent` | **measured**: color0 or color15 | Text drawn **on** an accent fill |
| `urgent` | **literal red** | Critical, as text |
| `critical` | **measured**: color1, or that same literal | Critical, as a fill or stripe |
| `warning` | **derived**: color11, or a tone lifted out of it | Counts and truncation markers |

Eight of these are not a fixed slot, and the reason is the same in every case: a
slot index that reads well on one palette can be invisible on the next. On the
default palette five of the eight still land on the slot a positional map would
have named; the other three are literals.

- **`accent`** is whichever of `color12` and `color4` measures better against
  that palette's own ground — `color12` on both dark palettes, `color4` on
  Sakura Light, where `color12` reads at only 3.32:1.
- **`on-accent`** is whichever of `color0` and `color15` measures better against
  the accent that resolved. This is the most important pair in the set, and it
  is the one a fixed slot gets wrong the moment a palette inverts its text pair:
  naming `color0` is right on Sakura and on Sakura Light for opposite reasons,
  and wrong on Sakura Dark.
- **`urgent`** is a literal red held outside every palette and chosen by the
  ground's luminance: `#df8787` on a dark ground, `#9b2226` on a light one. No
  single red clears AA against both the darkest and the lightest ground — the
  two requirements do not overlap — so there are two. `#df8787` is the default
  palette's own `color9`, so nothing about Sakura changes.
- **`critical`** is deliberately *not* that same literal wherever the palette
  can do better. It is a fill behind the text `urgent` draws, so the two are
  meant to differ, and Sakura's own `color1` carries it at 4.07:1 — which is
  what it still resolves to there. The other two cannot: Sakura Dark's `color1`
  is red but reads 1.98:1, and Sakura Light's is a muted emerald, so both take
  the urgency literal for their ground. Contrast is the problem on one and hue
  on the other.
- **`foreground-dim`** is a tone each palette derives. `color6` carries this
  role legibly on Sakura (5.97:1) and collapses to 2.33:1 and 1.75:1 on the
  other two, so all three derive instead: `#aaa6b1` at 5.40:1 on the default.
  This role is painted both on list rows, which sit on the ground, and as
  placeholder text inside the input bar, which sits on `surface` — so it is
  derived against whichever of the two is worse, and that is the figure here.
- **`accent-soft`** is derived on all three, and it is the one role the default
  palette moves off its own slot. The shipped layouts paint
  `prompt { text-color }` in it at normal weight and the body size, which makes
  it body copy and puts its floor at 4.5:1 rather than the 3:1 a mark would get.
  It is also painted on `surface` rather than on the ground, because `inputbar`
  carries the background and `prompt` is transparent, and on a dark palette
  `surface` is the lighter and so the less forgiving of the two. `color4` is
  3.57:1 against it on Sakura — under that floor — so the role is that slot
  lifted 17% toward white, `#a192cd` at 4.61:1. Sakura Dark lifts its `color4`
  from 1.57:1 to 4.65:1; Sakura Light changes slot as well as tone, because
  there `color4` is already what `accent` measured best, and painting a
  selection and a prompt stripe the same colour is the collision this role
  exists to avoid.
- **`accent-strong` and `warning`** are `color13` and `color11` on Sakura, at
  7.96:1 and 10.60:1. On the other two palettes those slots collapse — 2.73:1
  and 2.12:1 for `color13`, 2.75:1 and 2.74:1 for `color11` — so each moves the
  slot's lightness toward white or black, holding its hue, and lands near 5:1
  rather than leaving a sheet marker nobody can see. Those collapsed figures are
  the SLOTS and not the roles: the shipped roles measure 5.34:1 and 5.08:1, and
  5.32:1 and 5.02:1, on Sakura Dark and Sakura Light respectively.

### Contrast

Some of these are constrained by measurement and not by taste. What each role
resolves to on the default palette, and what it measures against the background
the layouts paint it on — WCAG 2.1 relative luminance,
`(L_hi + 0.05) / (L_lo + 0.05)`.

Two backgrounds carry text. A list row sits on the window ground, because
`window` is the only opaque box in that chain. Everything inside the input bar
sits on `surface`, because `inputbar` paints it and `prompt` and `entry` are
transparent. A role painted in both places is given the worse of its two
figures. The `bg` column says which applies:

| Name | Resolves to | bg | Ratio |
|---|---|---|---|
| `foreground` | `#d4d4d9` | both | 8.73:1 |
| `foreground-bright` | `#f0edf2` | ground | 13.41:1 |
| `foreground-dim` | `#aaa6b1` | both | 5.40:1 |
| `accent` | `#aba0d9` | surface | 5.38:1 |
| `accent-soft` | `#a192cd` | both | 4.61:1 |
| `accent-strong` | `#cfaedc` | ground | 7.96:1 |
| `on-accent` | `#2b1e3a` | *the accent fill* | 6.49:1 |
| `urgent` | `#df8787` | ground | 5.89:1 |
| `critical` | `#c96464` | ground | 4.07:1 |
| `warning` | `#f5cf9e` | ground | 10.60:1 |
| `hint` | `#898791` | surface | 3.64:1 |
| `muted` | `#5e5966` | ground | 2.29:1 |

4.5:1 is AA for body copy and 3:1 for large or bold text and for a graphical
mark. Three rows are below the body floor on purpose:

- **`muted`**, at 2.29:1 on this palette and lower on the other two, is for
  separators, scrollbar troughs, disabled fills and the empty half of a meter.
  It must never carry text — use `foreground-dim`.
- **`hint`** is placeholder text and is held to a *recession* rather than to AA.
  3.6:1 keeps it legible without outreading what the user types over it, and
  deliberately below `foreground-dim`: a placeholder that outreads the secondary
  text is no longer a placeholder. All three palettes hold near that figure —
  3.64:1, 3.60:1, 3.82:1 against the `surface` the input bar paints — which is
  the point of it being a target rather than an arithmetic rule.
- **`critical`** is the fill or stripe behind the text `urgent` draws, so it is
  measured as a graphical object. At 4.07:1 it clears that floor comfortably and
  misses body copy; use `urgent` for anything to be read.

`roles.lua` records the ratio of every role on every palette. One shortfall
remains and is recorded rather than hidden:

- **The accent itself on Sakura Dark**, 2.54:1 against its ground and 2.39:1
  against the `surface` one shipped layout paints it on. Every accent candidate
  in that palette
  measures 2.4-2.8:1, because its 8-15 band is mid-tone rather than bright, and
  inventing a colour that is not in the compositor's own scheme was ruled
  against. Use `accent` for fills and marks there, and `foreground` or
  `foreground-bright` for anything that must be read. The rest of that family —
  `accent-soft` at 4.65:1, `accent-strong` at 5.34:1, `warning` at 5.32:1 — is
  derived rather than positional and does clear AA, and `accent-soft` is the
  role a prompt label on that palette wants. Its `on-accent` clears too, at
  4.53:1, by the same means: `color15` is the better of the two candidates at
  4.35:1 and is then carried the last 7% to the floor.

## RECOLOUR EVERYTHING

Redefine the slots. Every role that resolves *through* a slot follows — but the
derived tones do not, for the reason **The names** above gives: they are
literals, and redefining `color4` leaves `accent-soft` where it was. Override
those by name as well, or let **FOLLOWING THE COMPOSITOR** re-derive them from
your own sixteen slots. `sofi -dump-theme` prints a `"@colorN"` reference for a
role that resolves through a slot and a colour for one that does not, so the
split is visible rather than a thing to remember.

```lua
-- ~/.config/sofi/config.lua
return {
  { select = "*",
    color0  = "#1c1b22",
    color12 = "#89b4fa",
    color9  = "#f38ba8",
  },
}
```

You do not have to supply all sixteen — anything you leave alone keeps its
built-in value.

What a slot does **not** reach is the eight values the default palette holds as
literals. Five are the derived tones — `background`, `surface`, `surface-alt`,
`border-soft` and `hint` — which are blends of two slots, and the format has
neither a blend nor an alpha operator for a `"@colorN"` reference. The other three
are the measured values that did not land on a bare slot there: `urgent`,
`foreground-dim` and `accent-soft`. Paste another scheme's sixteen over the
block above and all eight stay Sakura's — which on a scheme of a different
lightness means another scheme's text on Sakura's surfaces, and Sakura's
placeholder tone measured against a ground that has moved.

So: redefining a few slots is the right tool for a small change, and porting a
whole colorscheme is the case for **PIN A PALETTE** below, which replaces the
derived tones too. The same sixteen values also go into hikari-sakura's
`ui.palette` table — and if you put them there instead, sofi derives the
tones itself from what it reads, which is the one route that needs no arithmetic
from you at all. See **FOLLOWING THE COMPOSITOR**.

## RECOLOUR ONE ROLE

Redefine the semantic name instead of the slot, when you want to change what
something is *for* without disturbing everything else drawn from that slot:

```lua
return {
  -- selections only; color12 keeps its other jobs
  { select = "*", accent = "#f5c2e7" },
}
```

The one thing to know is which slot a role resolved to on the palette you are
using, because eight of them are measured rather than fixed. On Sakura Light,
redefining `color12` does not move the selection — `color4` does. Redefining
`color4` on the default palette does not move the prompt either, because
`accent-soft` is a tone lifted out of that slot and not the slot. Redefining the
semantic name works on every palette, every time, which is the reason to prefer
it: `accent`, `accent-soft` and `urgent` are all reached this way, and the last
of those is determined by no slot at all.

## PIN A PALETTE

`-palette` is per invocation. To choose one for every invocation, the installed
theme ships an optional import for exactly this:

```lua
-- ~/.config/sofi/config.lua
return {
  import("palette", true),
}
```

A second argument of `true` makes the import optional: it does not fail when the
file is absent, which is what makes shipping that line safe — though it does log
one line to stderr saying it looked. A missing optional import returns an empty
array, which is a directive that does nothing. Create
`~/.config/sofi/palette.lua` and it takes over; `~/.config/sofi/themes/` works
too.

Put a whole palette in it rather than only the slots you want moved: the five
derived tones and the eight measured picks belong to the palette they were
computed for, so a file that redefines the sixteen slots and leaves them alone
gets another scheme's text over Sakura's surfaces. Redefining individual slots in
a plain `{ select = "*" }` block, as above, is the right tool for a small
change; a pinned palette is for a whole scheme.

The three shipped palettes are installed beside the theme for exactly this, as
`palette-sakura.luainc`, `palette-sakura-dark.luainc` and
`palette-sakura-light.luainc`, so `import("palette-sakura-light", true)` pins
one by name with nothing to copy. The `.luainc` extension is deliberate: a colour
fragment is not a selectable theme, and **sofi-theme-selector(1)** globs `*.lua`
in that directory. The import side does not care — it tries both extensions in
turn.

## FOLLOWING THE COMPOSITOR

If hikari-sakura's configuration is readable, sofi reads its palette from it and
applies it over the compiled-in one, so changing the compositor's theme retints
these panels too instead of leaving them behind. Four paths are tried, and the
first that yields a complete palette wins:

1. `$XDG_CONFIG_HOME/hikari/hikari.lua`, when that variable is set and not empty
2. `~/.config/hikari/hikari.lua` — the literal `.config`, even when
   `XDG_CONFIG_HOME` points somewhere else
3. `$SYSCONFDIR/hikari/hikari.lua`
4. `/etc/hikari/hikari.lua`

The last two matter because hikari-sakura's own `make install` puts the file
under `SYSCONFDIR` and only copies it into `~/.config` on request, so a packaged
desktop would otherwise have nothing to read. The animation duration below is
read from the same four paths in the same order, so the two never disagree about
where the compositor's configuration lives.

The file is expected to return a table. The palette is taken from `ui.palette`,
or from a top-level `palette` if that is absent, as `color0` to `color15` string
keys — `#rgb`, `#rrggbb` and `#rrggbbaa` are all accepted, as hikari's own
parser accepts them.

All sixteen or none: a palette half taken from the compositor would have its
derived tones computed across two schemes, which is the exact failure the
palettes were split into separate files to prevent. A file that is missing,
unreadable, malformed or carries no complete palette is the ordinary case and is
silent — the compiled-in palette stands.

**This overrides `-palette`**, because it is applied over whichever palette the
invocation selected and the compositor is meant to be the one source of colour
for the whole desktop. What it replaces is not only the sixteen slots: sofi
recomputes the five derived tones and all eight measured values from what it
read, by the same rules the shipped palette files state, and emits every one of
them. So `-palette sakura-light` on such a machine is inert. To override in the
other direction, write the slots or the names in your own config, which is
parsed last.

The file is evaluated with `io`, `os`, `package`, `require` and `load` out of
reach — the chunk's environment is a table holding `pairs`, `ipairs`, `type`,
`tonumber`, `tostring`, `assert`, `error`, `math`, `string` and `table`, and
nothing else. A menu program has no business running processes out of a
configuration file. Precompiled chunks are refused, and a file that does not
finish is abandoned rather than waited on: this is a launcher, and a
configuration that loops would otherwise cost you every menu rather than one
colour.

**A theme is read under the same bounds**, and from the same code rather than a
second copy of it: a theme is a Lua chunk now, and a theme is the thing people
download from each other, so it gets the bounded string and table libraries, the
allocation ceiling, the cumulative work ceiling and the instruction budget that
the compositor's configuration gets. `io`, `os`, `package`, `require`, `load`
and `dofile` are out of reach there too, and every file an `import()` chain pulls
in shares the one allowance. A theme that trips any of it is declined with a
diagnostic and the theme already loaded stands. See **sofi-theme(5)** for the
format itself.

Your own `~/.config/sofi/config.lua` is parsed after all of this, so it still
wins.

## MOTION

Every piece of motion sofi draws runs for one duration, and one theme property
sets it:

```lua
-- ~/.config/sofi/config.lua
return {
  { select = "window", animation_duration = 240 },
}
```

Milliseconds, read off the `window` widget and no other. `0` means instant and
is how to turn all of it off; a value above `1000` is clamped to `1000`, because
a menu that takes longer than a second to appear is a menu that is in the way.
For one invocation rather than all of them, use `-theme-str`, which takes a
chunk of its own and so returns an array like any other theme:

```bash
sofi -show drun -theme-str 'return { { select = "window", animation_duration = 0 } }'
```

What moves, all on that one duration: a panel fades up into place; the
notification banner travels in from the screen edge it is anchored to instead;
the listview's outgoing and incoming highlights cross-fade as the selection
moves; a hovered row fades in while the row the pointer has just left fades out;
and a live notification's cue settles once.

The duration is resolved in three steps, and the shipped theme deliberately sets
nothing so that the middle one is reached:

1. this property, whenever a theme sets it;
2. otherwise hikari-sakura's own `animation.duration`, read from
   `ui.animation` or from a top-level `animation` in the same four files the
   palette import reads, in the same order. `enabled = false` there counts as
   `0`, so a compositor that does not animate stops these panels animating too.
   A duration outside 0-1000 is not a usable answer and that file is passed
   over;
3. otherwise 180ms, which is the number saber settles on as well, so a desktop
   with no animation block anywhere still moves at one speed.

Step 2 reads the compositor's own configuration, so it answers only on a machine
that has one; otherwise the resolution is step 1 or step 3.

**The whole subsystem is Wayland-only.** Motion is paced from
`wl_callback.done`, which is the compositor telling sofi a frame is about to be
presented, rather than from a timer — so it is the presentation that sets the
pace and not how often the main loop happens to wake. The XCB backend has no
equivalent, so under X11 every one of those animations is instant whatever this
property says.

## MOVE A PANEL

Each surface is placed by `location`, `anchor` and the two offsets. Every layout
calls its panel `window`, and `~/.config/sofi/config.lua` is read for every
invocation — so a `{ select = "window" }` rule written there moves *every*
surface at once, all eleven of them, on every invocation that does not pass
`-theme`. (One that does starts the theme over from the file it names and never
sees that rule at all.) Scope the rules to one invocation to move one panel.

To make the application menu a left-edge side panel instead of a bottom-centre
one, leaving the task strip, the sheet row, the notification stack and the toast
where they are:

```bash
sofi -show drun -theme-str 'return { { select = "window",
    location = west,
    anchor   = west,
    width    = px(300),
    height   = percent(76),
    y_offset = px(0),
} }'
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

```lua
return {
  { select = "window",
    width  = px(360),
    height = percent(80),
  },
}
```

`height` is ignored for surfaces that size themselves to their content — the
notification banner and the toast.

For a list, the number of visible rows is usually what you actually want:

```lua
return {
  { select = "listview", lines = 16 },
}
```

The sheet switcher is the exception. It is a fixed ten-cell grid, so its `width`
is what decides how much room each chip's label gets — raise it if you use a
large font and the window counts start ellipsizing.

## RESTYLE ONE SURFACE AND NOT THE OTHERS

A plain `~/.config/sofi/config.lua` is parsed for every invocation that does
not pass `-theme`, so anything in it applies everywhere. To reach exactly one
surface, override on that invocation:

```bash
sofi -show window -theme-str 'return {
  { select = "window",  width   = percent(90) },
  { select = "element", padding = px(4) },
}'
```

For more than a couple of properties, pass several `-theme-str` snippets: each
merges over the currently loaded theme in turn. Reserve `-theme` for a surface
that should look genuinely different rather than merely adjusted — it discards
the built-in layout *and* the palette, so the file it names has to define
everything it uses, `import()`ing your own colours if it wants any.

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

Mechanically the mode sets `{ select = "button-dismiss-all", enabled = false }`,
and a disabled widget is not drawn at all rather than greyed out — so what you
see is the button's absence. It is applied by the mode rather than written in the
layout, which means a `{ select = "button-dismiss-all", enabled = true }` in your
own config will not bring it back: it is hidden because it cannot work, not as a
matter of taste.

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

Entries for the `actions` and `bindings.keyboard` tables in
`~/.config/hikari/hikari.lua` -- added to the ones already there, not appended
as a second block, since Lua would keep only the last assignment to a key:

```lua
  actions = {
    notifications = "sofi -show notification-history",
    ["notify-clear"] = "sofi -notification-clear",
  },

  bindings = {
    keyboard = {
      ["L+n"]   = "action-notifications",
      ["L+S+n"] = "action-notify-clear",
    },
  },
```

`notify-clear` is bracketed because a hyphen is not legal in a plain Lua name,
and every key is bracketed because of the `+`. The binding names an action as
`action-` followed by the action's own name.

## THE SYSTEM TRAY ZONE

The task strip's right-hand corner is fed by `sofi -tray-daemon`. Unlike every
other widget in a layout its contents are not known when the theme is parsed:
they arrive over D-Bus and change while the strip is on screen.

| Widget | What it is |
|---|---|
| `tray` | The horizontal box holding the icons |
| `tray-icon` | Every icon. One rule styles them all |

```lua
return {
  { select = "tray",
    spacing = px(12),
    padding = { px(0), px(6) },
  },

  { select = "tray-icon",
    size    = px(24),
    padding = px(3),
  },
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
cp /usr/local/share/sofi/themes/config.lua ~/.config/sofi/config.lua
cp /usr/local/share/sofi/themes/colors-default.luainc ~/.config/sofi/
```

(`/usr/local/share` is the default prefix; substitute your own if you configured
one.) `colors-default.luainc` is the whole colour layer of the default palette —
slots, derived tones, measured picks and semantic names in one file; `config.lua`
is the layout and imports it. It is one file there because a theme reached
through `-theme` has had the compiled-in resources freed out from under it, so
the installed copy has to stand on its own.

Standing on its own is also what it costs you. That copy redefines the slots,
the derived tones, the measured picks and the semantic names, and it is parsed
after all of them — so `-palette` goes inert the moment it is in place, and so
does the compositor's palette. Verify with `-dump-theme`: `color0` reads
`#2b1e3a` under `-palette sakura-dark` once the copy is imported, where without
it the flag gives `#161618`. If you want the flag to keep working, delete the
`import()` line and override the handful of names you actually meant to change,
as **RECOLOUR ONE ROLE** does; if you want a whole scheme, pin a palette instead
and leave the role map alone.

What it does **not** do is stay confined to the application menu. A config file
cannot know which surface it was loaded for, and it is parsed after whichever
layout the invocation selected — so its `window`, `mainbox`, `listview` and
`element` rules reach the task strip, the sheet row, the volume, network,
Bluetooth, display and keys panels, the notification stack, the toast and the
history panel as well, every one of which then loses the geometry its own layout
gave it. Copy it as a starting point for a theme you intend to edit, not as a
drop-in that leaves the other surfaces alone.

The exception is an invocation that passes `-theme`. That discards `config.lua`
along with the built-in layout, and the named file becomes the whole theme --
which is the reliable way to keep one surface out of the copied layout's reach.

## THINGS THAT WILL NOT WORK

- **`import()` inside a built-in layout.** The compiled-in layouts cannot import
  anything, because a GResource has no directory to resolve a path against. That
  is why the palette is parsed for them at startup instead, and why the
  installed copy of the theme uses `import()` while the built-in one does not.
- **Alpha on a palette reference.** There is no spelling for it; the alpha
  argument of `color()` applies to a colour, and `"@color0"` is a reference
  rather than one. Write the eight-digit hex, as `background` does.
- **A file or a process from a theme.** `io`, `os`, `package`, `require`, `load`
  and `dofile` are not reachable from a theme's Lua, and a theme that does not
  finish is abandoned rather than waited on. A downloaded theme is a downloaded
  program, which is the whole reason those bounds are there.
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
