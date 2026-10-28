# sofi-theme(5)

## NAME

**sofi-theme** - Sofi theme format files

## Getting started with theming

The easiest way to get started theming sofi is by modifying your existing theme.

Themes can be modified/tweaked by adding theming elements to the end of the\
config file. The default location of this file is `~/.config/sofi/config.lua`,
if the file does not exists, you can create it.

A theme is a Lua chunk that `return`s one array. Each element of that array is
one directive, and the order of the array is the cascade: the last directive to
name a widget and a property wins.

A basic config:

```lua
return {

  { configuration = {
      modes       = { "combi" },
      combi_modes = { "window", "drun", "run" },
  } },

  { reset = true },
  import("gruvbox-light"),

  -- Insert theme modifications after this

}
```

`{ reset = true }` followed by an `import()` is how the old format's
`@theme "gruvbox-light"` is spelled: the reset throws away everything parsed
before it, and the import brings the named file in at that point in the array.

For example if we want to change the `Type to filter` text in the entry box we
append the following:

```lua
return {
  { select = "entry", placeholder = "Type here" },
}
```

In the above section, `select` names the widget, `placeholder` is the property
we want to modify and we set it to the string `"Type here"`. To find the
commonly available widgets in sofi, see the 'Basic structure' section.

To change the mouse over cursor to a pointer, add:

```lua
return {
  { select = "entry",
    placeholder = "Type here",
    cursor      = pointer,
  },
}
```

For the next modification, we want to add the icon after each text element and
increase the size. First we start by modifying the `element` widget:

```lua
return {
  { select = "element",
    orientation = horizontal,
    children    = { "element-text", "element-icon" },
    spacing     = px(5),
  },
}
```

Resulting in the following packing:

```text
┌─────────────────────────────────────────────────────────────────────┐ 
│ element                                                             │ 
│ ┌─────────────────────────────────────────────┐ ┌─────────────────┐ │ 
│ │element─text                                 │ │ element─icon    │ │ 
│ └─────────────────────────────────────────────┘ └─────────────────┘ │ 
└─────────────────────────────────────────────────────────────────────┘ 
```

The `element` (container) widget hold each entry in the `listview`, we add the
two pre-defined children in the order we want to show. We also specify the
packing direction (`orientation`) and the spacing between the children
(`spacing`). We specify the space between the two children in absolute pixels
(`px`).

To increase the icon-size, we need to modify the `element-icon` widget.

```lua
return {
  { select = "element-icon", size = em(2.5) },
}
```

```text
┌─────────────────────────────────────────────────────────────────────┐ 
│ element                                                             │ 
│ ┌─────────────────────────────────────────────┐ ┌─────────────────┐ │ 
│ │element─text                                 │ │    element      │ │ 
│ │                                             │ │       ─         │ │ 
│ │                                             │ │     icon        │ │ 
│ └─────────────────────────────────────────────┘ └─────────────────┘ │ 
└─────────────────────────────────────────────────────────────────────┘ 
```

In this example we specify the size in the [em](https://www.w3.org/Style/LieBos3e/em) unit.

Now lets change the text color of both the `entry` and the `element-text`
widget to red and background to blue.

```lua
return {
  { select = { "entry", "element-text" },
    text_color       = color("red"),
    background_color = rgb(0, 0, 255),
  },
}
```

Here we use two different methods of writing down the color, for `text-color`
we used a named color, for `background-color` we specify it in `rgb`.
We also specify the property for multiple widgets by passing an array of widget
names to `select`, which is what the old format's comma separated selector list
did.

If you want to center the text relative to the icon, we can set this:

```lua
return {
  { select = "element-text", vertical_align = 0.5 },
}
```

```text
┌─────────────────────────────────────────────────────────────────────┐ 
│ element                                                             │ 
│ ┌─────────────────────────────────────────────┐ ┌─────────────────┐ │ 
│ │                                             │ │    element      │ │ 
│ │element-text                                 │ │       ─         │ │ 
│ │                                             │ │     icon        │ │ 
│ └─────────────────────────────────────────────┘ └─────────────────┘ │ 
└─────────────────────────────────────────────────────────────────────┘ 
```

We can also customize the cursor's color, width, and choose to hide it when the input box is empty. You could, for example, create a crimson block cursor that only appears when text is entered, like this:

```lua
return {
  { select = "entry",
    cursor_color         = rgb(220, 20, 60),
    cursor_width         = px(8),
    hide_cursor_on_empty = true,
  },
}
```

By default, the `cursor-color` will be the same as the `text-color`. The
`cursor-width` will always default to 2 pixels and `hide-cursor-on-empty` is set to false.

If you want to see the complete theme, including the modification you can run:

```bash
sofi -dump-theme
```

## Default theme loading

sofi does **not** load a single default theme. It loads a palette and then one
of six built-in layouts, chosen from the surface you asked for. There is no
reset-and-import of a `"default"` theme in the default configuration; the choice
is made in C, by `sofi_builtin_panel_resource()`.

The full order is:

1. `/org/sofi/default_configuration.lua` — compiled in.
2. `/org/sofi/palette.lua` — compiled in. Defines `color0`-`color15` and the
   semantic names (`background`, `foreground`, `accent`, ...) that every layout
   refers to. It contains no layout.
3. One compiled-in layout: `panel-window.lua` for `-show window`,
   `panel-sheets.lua` for `-show sheets`,
   `panel-notification-history.lua` for `-show notification-history`,
   `panel-notifications.lua` for `-notification-daemon`, `panel-notify.lua`
   for `-e`, and `default.lua` for everything else.
4. A system `sofi.lua`: the first one found in `$XDG_CONFIG_DIRS`, otherwise
   the one in `SYSCONFDIR`. Only the first match is read; never several merged.
5. `~/.config/sofi/config.lua`.
6. `-theme`, if given.
7. `-theme-str` snippets, in the order given.

Sources 1-5 and 7 merge: later ones override earlier ones property by property,
so a config file edits the built-in layout rather than replacing it. `@`
references are resolved after every source has been parsed, which is what lets a
`{ select = "*" }` block in your own config redefine a palette name and have
every layout follow.

`-theme` is not one of them. It is handled like `{ reset = true }`: the theme
built so far is discarded, and the file it names becomes the whole theme --
palette and layout included. A file loaded that way therefore has to stand on
its own, and a `"@accent"` it does not define itself has nothing left to resolve
against. To override a few properties for one invocation, use `-theme-str`,
which merges.

`-no-default-config` skips steps 1-3 entirely. A theme that relied on the
palette's names will then fail to resolve them.

For the difference between `import()` and `{ reset = true }` see the `Multiple
file handling` section in this manpage.

To see what a given surface actually resolved to, dump it:

```bash
sofi -show drun -dump-theme
sofi -show window -dump-theme
```

For task-oriented instructions — recolouring, moving a panel, restyling one
surface — see **sofi-customisation(5)**.

### Overriding the palette

The palette is an ordinary `{ select = "*" }` block, so it is overridden like
any other property. Redefine a slot to change everything drawn from it:

```lua
return {
  { select = "*",
    color0  = "#1c1b22",
    color12 = "#89b4fa",
  },
}
```

Or redefine a semantic name to change one role only:

```lua
return {
  { select = "*", accent = "#f5c2e7" },
}
```

Two limitations are worth knowing. A `"@name"` reference cannot carry alpha —
the alpha argument of `color()` applies to a colour, not to a reference — so a
translucent value must be written as eight-digit hex. And the compiled-in
layouts cannot `import()` anything, because a GResource has no directory to
resolve a relative path against; that is why the palette is parsed for them at
startup rather than imported by them.

## Description

The need for a new theme format was motivated by the fact that the way sofi
handled widgets has changed. From a very static drawing of lines and text to a
nice structured form of packing widgets. This change made it possible to
provide a more flexible theme framework. The old theme format and config file
are not flexible enough to expose these options in a user-friendly way.
Therefore, a new file format has been created, replacing the old one.

That format is Lua. A theme is a chunk that returns the array of directives the
engine applies, so the arithmetic, the locals and the string handling a theme
needs are the language's own rather than a grammar of sofi's -- and so is the
diagnostic when a theme is wrong. What a theme may do with them is bounded; see
**The sandbox** below.

## Format specification

## Encoding

The encoding of the file is UTF-8. Both unix (`\n`) and windows (`\r\n`)
newlines format are supported. But unix is preferred.

## Comments

Lua comments are supported.

- Anything after  `--` and before a newline is considered a comment.

- Everything between `--[[` and `]]` is a comment, this comment can span
    multiple lines.

A long comment can be nested by adding equals signs to its brackets --
`--[==[ ... ]==]` holds a `]]` without ending early -- and a long comment can be
inline. A run of `--` lines is the form the shipped files use, because it cannot
be broken by anything written inside it.

The following is valid:

```lua
-- Magic comment.
return {
  { select = "entry", placeholder = --[[ comment ]] "Type here" },
}
```

However, this is not, because a comment cannot be put inside a name:

```text
prop--[[comment]]erty = value
```

## White space

White space and newlines, like comments, are ignored.

This:

```lua
return {
  { select = "entry", placeholder = "Type here" },
}
```

Is identical to:

```lua
return
{
  {
      select      =
    "entry"
    ,
            placeholder = "Type here"
  },
}
```

A Lua table constructor accepts `;` as a field separator as well as `,`, so
`{ select = "entry"; placeholder = "Type here" }` is the same directive again.

## File extension

The preferred file extension for the theme format is **lua**. If a theme file is
split over multiple files, include files can have the: **luainc** extension. The
distinction is a convention for the benefit of
**sofi-theme-selector(1)**, which globs `*.lua`: a colour fragment is not a
selectable theme. `import()` tries both extensions in turn and does not care
which it finds.

## The sandbox

A theme is a program, and a theme is the thing people download from each other,
so it is read under bounds rather than trusted. The chunk is evaluated in an
environment holding `assert`, `error`, `ipairs`, `next`, `pairs`, `rawequal`,
`rawget`, `rawlen`, `rawset`, `select`, `tonumber`, `tostring`, `type`, `math`,
a bounded `string`, a bounded `table`, `env`, and the value constructors
described under **Properties Format** below.

`io`, `os`, `package`, `require`, `load` and `dofile` are **not** reachable. A
theme computes colours and lengths; nothing it legitimately does needs a file or
a process.

Beyond the environment there are four limits, and every file an `import()` chain
pulls in shares the one allowance:

- a ceiling on how much memory the chunk may allocate;
- a ceiling on the cumulative work the bounded `string` and `table` libraries
  will do, so a single pattern match or table move cannot be made to stand in
  for a loop;
- an instruction budget, so a theme that does not finish is abandoned rather
  than waited on;
- a depth limit on the `import()` chain.

Precompiled chunks are refused -- only text is loaded. A theme that trips any of
these is declined with a diagnostic and the previously loaded theme stands,
which is the same thing that happens to a theme with a syntax error in it: a
half-applied theme is worse than the one it was replacing.

## Basic Structure

Each element of the returned array is one directive. A rule is a directive with
a `select` key naming the widget it applies to, and the rest of its keys are the
properties. Global properties are a rule whose `select` is `"*"`.

Five keys are reserved and are never read as properties: `select`, `reset`,
`configuration`, `media` and `rules`. No property is named any of them.

It is advised to define the *global properties section* on top of the file to
make inheritance of properties clearer.

```lua
return {

  -- Global properties section
  { select = "*",
    -- list of properties
  },

  -- Element theme section.
  { select = "{element path}",
    -- list of properties
  },
  { select = { "{element path}", "{another}" },
    -- list of properties
  },

}
```

If there are multiple rules with the same selector, they are merged. Duplicate
properties are overwritten and the last entry in the array kept.

A directive that is an entirely empty table is a silent no-op, and a directive
with no `select` but an array part of its own is a nested array of directives
applied in order. That is what makes `import()` usable as an array element: it
returns the imported chunk's array, which is then applied in place.

## Global properties section

A theme can have one or more global properties sections. If there is more than
one, they will be merged.

The global properties section denotes the defaults for each element.
Each property of this section can be referenced with the string
`"@{identifier}"` (See Properties section)

A global properties section is indicated with `select = "*"`. At the top level
that is the root of the tree, which is where a `"@name"` reference is looked up
and where widget defaults are inherited from. Inside a `media` block's `rules` a
`"*"` selector is an ordinary child instead, which is what the format before
this one did too.

## Element theme section

A theme can have multiple element theme sections.

A selector is a string. The element path in it can consist of multiple names
separated by whitespace or dots. Each element may contain any number of letters,
numbers and `-`'s. Multiple elements can be specified by giving `select` an array
of selector strings, which applies the same properties to each.

This is a valid element name:

```lua
return {
  { select = "element normal.normal", background_color = color("blue") },
  { select = "button",               background_color = color("blue") },
}
```

And is identical to:

```lua
return {
  { select = { "element normal normal", "button" },
    background_color = color("blue"),
  },
}
```

Each section inherits the global properties. Properties can be explicitly
inherited from their parent with the `inherit` keyword.
In the following example:

```lua
return {
  { select = "window",
    a        = 1,
    b        = 2,
    children = { "mainbox" },
  },
  { select = "mainbox",
    a = inherit,
    b = 4,
    c = 8,
  },
}
```

The element `mainbox` will have the following set of properties (if `mainbox`
is a child of `window`):

```lua
a = 1
b = 4
c = 8
```

If multiple rules are defined with the same selector, they are merged. If
multiple properties with the same name are defined in one rule, the last one
written is used -- that is Lua's own rule for a table constructor, not something
sofi adds.

## Properties Format

The properties in a rule consist of:

```text
{identifier} = {value},
```

Both fields are mandatory for a property.

The `identifier` names the specified property. **An underscore in an identifier
is read as a hyphen**, so `text-color` is written `text_color`, and
`["text-color"]` is the same property again. Prefer the underscore form: it is a
bare identifier, so a misspelling is caught where a quoted key would not be. The
property names listed under **Widget properties** below are given in their
hyphenated form, which is also the form `-dump-theme` prints; substitute an
underscore for each hyphen when you write one.

The type of the `value` comes from the Lua value, not from the property's name:
there is no name-to-type map, just as the format before this one took the type
from the syntax of the value. When used, values with the wrong type that cannot
be converted are ignored.

The current theme format supports different types:

- a string
- an integer number
- a fractional number
- a boolean value
- a color
- image
- text style
- line style
- a distance
- a padding
- a border
- a position
- a reference
- an orientation
- a cursor
- a list of keywords
- an array of values
- an environment variable
- Inherit

Some of these types are a combination of other types.

### String

- Format: a Lua string

Strings are always surrounded by double (`"`) or single (`'`, apostrophe)
quotes, or by Lua's long brackets (`[[ ... ]]`). Between the quotes there can be
any printable character.

For example:

```lua
return {
  { select = "window", font = "Awasome 12" },
}
```

The string must be valid UTF-8, special characters can be escaped:

```lua
return {
  { select = "text", content = "Line one\n\tIndented line two 'Quoted text'" },
  { select = "text", content = 'Line one\n\tIndented line two "Quoted text"' },
  { select = "text", content = "Line one\n\tIndented line two \"Quoted text\"" },
}
```

The following special characters can be escaped: `\b`, `\f`, `\n`, `\r`, `\t`, `\v`, `\`,
`"` and `'` (double quotes inside single-quotes or in reverse don't need escape).

A string that begins with `#` or `@` would otherwise be read as a colour or as a
reference, so one that must stay a string says so with `str()`:

```lua
return {
  { select = "textbox-channel", content = str("#general") },
}
```

### Integer

- Format: a Lua integer

An integer may contain any number.

For examples:

```lua
return {
  { select = "listview", lines = 12 },
}
```

### Real

- Format: a Lua float

A real is an integer with an optional fraction. Lua's own number syntax applies,
so `.3`, `3.`, scientific notation such as `3.4e-3` and hexadecimal floats are
all accepted where the format before this one took none of them.

For example:

```lua
return {
  { select = "element-text", vertical_align = 3.4 },
}
```

An integer and a float are different property types -- `12` is an integer and
`12.0` a fractional number -- so write the one the property wants.

### Boolean

- Format: `(true|false)`

Boolean value is either `true` or `false`. This is case-sensitive.

For example:

```lua
return {
  { select = "listview", dynamic = false },
}
```

### Image

**sofi** support a limited set of background-image formats.

- Format: `url("path to image")`

- Format: `url("path to image", scale)`
    where scale is: `"none"`, `"both"`, `"width"`, `"height"`

- Format: `linear_gradient(stop color, stop color, ...)`

- Format: `linear_gradient(direction, stop color, stop color, ...)`;
    where direction is: `"top"`, `"left"`, `"right"`, `"bottom"`.

- Format: `linear_gradient(angle, stop color, stop color, ...)`;
    the angle is a number of **degrees**.

Where the `path` is a string, and `stop` color is of type color.

```lua
return {
  { select = "window",
    background_image = linear_gradient(45, "#2b1e3a", "#8e7cc3"),
  },
}
```

The old format's four angle units are gone as keywords, because an angle is now
a number and the conversions are arithmetic: radians are `n * 180 / math.pi`
degrees, gradians `n * 0.9`, and turns `n * 360`.

### Color

**sofi** supports the color formats as specified in the CSS standard (1,2,3 and
some of CSS 4)

- Format: `"#{HEX}{3}"` (rgb)

- Format: `"#{HEX}{4}"` (rgba)

- Format: `"#{HEX}{6}"` (rrggbb)

- Format: `"#{HEX}{8}"` (rrggbbaa)

- Format: `rgb[a]({INTEGER}, {INTEGER}, {INTEGER} [, {ALPHA}])`

- Format: `hsl[a]({ANGLE}, {FRACTION}, {FRACTION} [, {ALPHA}])`

- Format: `hwb({ANGLE}, {FRACTION}, {FRACTION} [, {ALPHA}])`

- Format: `cmyk({FRACTION}, {FRACTION}, {FRACTION}, {FRACTION} [, {ALPHA}])`

- Format: `color("{named-color}" [, {ALPHA}])`

- Format: `color("#{HEX}{6}")`, for symmetry with the above

A hex colour is a plain string: the leading `#` is what makes it a colour rather
than a string, which is the rule the format before this one used as well. `#rgb`
doubles each nibble, so `"#fff"` is white.

`rgb` and `rgba` are one function, as are `hsl` and `hsla`; the alpha argument
is optional in both, so the second name of each pair is kept only so that a
theme that spells it out still reads.

The different values are:

- `{HEX}` is a hexadecimal number ('0-9a-f' case insensitive).

- `{INTEGER}` value can be between 0 and 255. The CSS percentage form of
    `rgb()` has no spelling here -- divide it yourself, as in
    `rgb(255 * 0.4, 0, 0)`.

- `{ANGLE}` is the angle on the color wheel, as a number of degrees. The `deg`,
    `rad`, `grad` and `turn` units are gone as keywords because the conversions
    are arithmetic now: `math.pi` radians is `180`, one gradian is `0.9`, and
    one turn is `360`.

- `{FRACTION}` and `{ALPHA}` are between 0 and 1.0. Values outside that range
    are clamped into it.

- `{named-color}` is one of the following colors, case-insensitively:

    AliceBlue, AntiqueWhite, Aqua, Aquamarine, Azure, Beige, Bisque, Black,
    BlanchedAlmond, Blue, BlueViolet, Brown, BurlyWood, CadetBlue, Chartreuse,
    Chocolate, Coral, CornflowerBlue, Cornsilk, Crimson, Cyan, DarkBlue,
    DarkCyan, DarkGoldenRod, DarkGray, DarkGrey, DarkGreen, DarkKhaki,
    DarkMagenta, DarkOliveGreen, DarkOrange, DarkOrchid, DarkRed, DarkSalmon,
    DarkSeaGreen, DarkSlateBlue, DarkSlateGray, DarkSlateGrey, DarkTurquoise,
    DarkViolet, DeepPink, DeepSkyBlue, DimGray, DimGrey, DodgerBlue, FireBrick,
    FloralWhite, ForestGreen, Fuchsia, Gainsboro, GhostWhite, Gold, GoldenRod,
    Gray, Grey, Green, GreenYellow, HoneyDew, HotPink, IndianRed, Indigo,
    Ivory, Khaki, Lavender, LavenderBlush, LawnGreen, LemonChiffon, LightBlue,
    LightCoral, LightCyan, LightGoldenRodYellow, LightGray, LightGrey,
    LightGreen, LightPink, LightSalmon, LightSeaGreen, LightSkyBlue,
    LightSlateGray, LightSlateGrey, LightSteelBlue, LightYellow, Lime,
    LimeGreen, Linen, Magenta, Maroon, MediumAquaMarine, MediumBlue,
    MediumOrchid, MediumPurple, MediumSeaGreen, MediumSlateBlue,
    MediumSpringGreen, MediumTurquoise, MediumVioletRed, MidnightBlue,
    MintCream, MistyRose, Moccasin, NavajoWhite, Navy, OldLace, Olive,
    OliveDrab, Orange, OrangeRed, Orchid, PaleGoldenRod, PaleGreen,
    PaleTurquoise, PaleVioletRed, PapayaWhip, PeachPuff, Peru, Pink, Plum,
    PowderBlue, Purple, RebeccaPurple, Red, RosyBrown, RoyalBlue, SaddleBrown,
    Salmon, SandyBrown, SeaGreen, SeaShell, Sienna, Silver, SkyBlue, SlateBlue,
    SlateGray, SlateGrey, Snow, SpringGreen, SteelBlue, Tan, Teal, Thistle,
    Tomato, Turquoise, Violet, Wheat, White, WhiteSmoke, Yellow,
    YellowGreen

`transparent` was in that list in the old format and is a bare keyword here, not
a name `color()` knows: write `background_color = transparent`.

For example:

```lua
return {
  { select = "window",
    background_color = "#FF0000",
    border_color     = rgba(0, 0, 1, 0.5),
    text_color       = color("SeaGreen"),
  },
}
```

or

```lua
return {
  { select = "window",
    background_color = transparent,
    text_color       = color("Black"),
  },
}
```

### Text style

- Format: `(bold|italic|underline|strikethrough|none)`

One style is that keyword on its own, as in `highlight = bold`. To combine
several, and optionally to give a colour, use `highlight()`:

```lua
return {
  { select = "element-text", highlight = highlight(bold, underline, "#ff0000") },
}
```

The arguments may be given in any order; a string among them is read as a
colour.

Text style indicates how the highlighted text is emphasized. `none` indicates
that no emphasis should be applied.

- `bold`: make the text thicker then the surrounding text.
- `italic`: put the highlighted text in script type (slanted).
- `underline`: put a line under the text.
- `strikethrough`: put a line through the text.

The following options are available on pango 1.50.0 and up:

- `uppercase`: Uppercase the text.
- `lowercase`: Lowercase the text.

The following option is disabled as pango crashes on this if there is eel
upsizing or wrapping. This will be re-enabled once fixed:

- `capitalize`: Capitalize the text.

### Line style

- Format: `(dash|solid)`

Indicates how a line should be drawn. It is the second argument of a distance:
`px(1, solid)`, `px(2, dash)`. `solid` is the default and need not be given.

It currently supports:

- `dash`:  a dashed line, where the gap is the same width as the dash
- `solid`: a solid line

### Distance

- Format: `px({Integer})`
- Format: `em({Real})`
- Format: `ch({Real})`
- Format: `percent({Real})`
- Format: `mm({Real})`

A distance can be specified in 5 different units:

- `px`: Screen pixels.
- `em`: Relative to text height.
- `ch`: Relative to width of a single number.
- `mm`: Actual size in millimeters (based on dpi).
- `percent`:  Percentage of the **monitor** size.

A bare number used where a distance belongs is pixels, so `padding = 0` and
`padding = px(0)` are the same thing.

Distances used in the horizontal direction use the monitor width. Distances in
the vertical direction use the monitor height.
For example:

```lua
return {
  { select = "window", padding = percent(10) },
}
```

On a full-HD (1920x1080) monitor, it defines a padding of 192 pixels on the left
and right side and 108 pixels on the top and bottom.

#### Calculating sizes

Sofi supports some maths in calculating sizes. Where the old format had a
`calc()` wrapper and CSS syntax inside it, this one has Lua's own arithmetic on
distances, building the same tree and resolving it at use time:

```lua
return {
  { select = "window", width = percent(100) - px(37) },
}
```

```lua
return {
  { select = "window", width = calc.min(percent(20), px(512)) },
}
```

The operators are `+`, `-`, `*`, `/`, `%` and unary minus, and `calc` holds the
five operations Lua has no operator for, each taking two operands:

- `+`           : Add
- `-`           : Subtract
- `/`           : Divide
- `*`           : Multiply
- `%`           : Modulo
- `calc.min`    : Minimum of the two operands;
- `calc.max`    : Maximum of the two operands;
- `calc.floor`  : Round the first down to the next multiple of the second
- `calc.ceil`   : Round the first up to the next multiple of the second
- `calc.round`  : Round the first to the nearest multiple of the second

It uses Lua's precedence ordering, which for these operators is the C one. There
is no `calc(` wrapper, so a dumped distance reads `100.0000% - 37px` where the
old format printed `calc( ( 100.0000% - 37px ))`; the wrapper was a print marker
with no effect on the value.

A length that a theme wants to compute once and use in several places goes in a
Lua local, which is the replacement for defining a global and referring to it
with `var()`:

```lua
local gutter = px(16)

return {
  { select = "mainbox", padding = gutter },
  { select = "footer",  padding = { px(4), gutter } },
}
```

### Padding

- Format: `{Integer}`
- Format: `{Distance}`
- Format: `{ {Distance} }`
- Format: `{ {Distance}, {Distance} }`
- Format: `{ {Distance}, {Distance}, {Distance} }`
- Format: `{ {Distance}, {Distance}, {Distance}, {Distance} }`

If no unit is specified, pixels are assumed.

The different number of fields in the formats are parsed like:

- 1 field: `all`
- 2 fields: `top&bottom` `left&right`
- 3 fields: `top`, `left&right`, `bottom`
- 4 fields: `top`, `right`, `bottom`, `left`

```lua
return {
  { select = "window",  padding = px(6) },                            -- all
  { select = "mainbox", padding = { px(6), px(10) } },                 -- 2
  { select = "element", padding = { px(1), px(2), px(3) } },           -- 3
  { select = "footer",  padding = { px(1), px(2), px(3), px(4) } },    -- 4
}
```

**An array of lengths or of bare numbers is padding**, and that is the one real
trap in the format: `{ 10, 20, 30, 40 }` is four edges, not a list of four
numbers. For a numeric list, say so with `list()` -- see **List of values**
below.

### Border

- Format: `{Integer}`

- Format: `{Distance}`

- Format: `{ {Distance} }`

- Format: `{ {Distance}, {Distance} }`

- Format: `{ {Distance}, {Distance}, {Distance} }`

- Format: `{ {Distance}, {Distance}, {Distance}, {Distance} }`

Borders are identical to padding, except that each distance field carries its own
line style, as the second argument of the unit:

```lua
return {
  { select = "window", border = { px(2, dash), px(1), px(1), px(1, solid) } },
}
```

> When no unit is specified, pixels are assumed.

### Position

Indicate a place on the window/monitor.

```text
┌─────────────┬─────────────┬─────────────┐
│ north west  │    north    │  north east │
├─────────────┼─────────────┼─────────────┤
│   west      │   center    │     east    │
├─────────────┼─────────────┼─────────────┤
│ south west  │    south    │  south east │
└─────────────┴─────────────┴─────────────┘
```

- Format: `(center|east|north|west|south|north_east|north_west|south_west|south_east)`

These are bare keywords rather than strings, so a misspelling is an
undefined-name error the moment it is wrong. Note the underscore: the two-word
positions are `north_east` and so on.

### Visibility

It is possible to hide widgets:

```lua
return {
  { select = "inputbar", enabled = false },
}
```

### Reference

- Format: `"@{PROPERTY NAME}"`

A reference is the string `"@"` followed by the name of a **theme global** -- a
property set in a `{ select = "*" }` block -- and it is resolved at lookup, not
where it is written. A reference can point to another reference. Currently, the
maximum number of redirects is 20. A property always refers to another property.
It cannot be used for a subpart of the property. For example, this is not valid,
because `highlight()` wants a colour and `"@pink"` is a reference:

```lua
return {
  { select = "window", highlight = highlight(bold, "@pink") },
}
```

But this is:

```lua
return {
  { select = "*",      myhigh    = highlight(bold, "#FAA") },
  { select = "window", highlight = "@myhigh" },
}
```

- Format: `ref("PROPERTY NAME" [, DEFAULT])`

`ref` is the long spelling of the same thing, and the form that can carry a
default, which a `"@name"` string cannot. A reference can point to another
reference. Currently, the maximum number of redirects is 20. A property always
refers to another property. It cannot be used for a subpart of the property.

Example:

```lua
return {
  { select = "window", width = ref("width", percent(30)) },
}
```

If the property `width` is set globally (`{ select = "*" }`) that value is used,
if the property `width` is not set, the default value is used.

A value the theme computes **for itself** does not want a reference at all: it
wants a Lua local, which is the replacement for defining a global only to read
it back.

```lua
local width = percent(30)

return {
  { select = "window", width = width },
}
```

The difference is where the value is settled. A local is settled as the chunk
runs, before anything is merged, so nothing a later source does can change it. A
`"@name"` reference is settled at lookup, after every source has been parsed, so
a `{ select = "*" }` block in a file read later still reaches it. Palette names
rely on exactly that, which is why they stay references.

### Orientation

- Format: `(horizontal|vertical)`

Specify the orientation of the widget.

### Cursor

- Format: `(default|pointer|text)`

Specify the type of mouse cursor that is set when the mouse pointer is over the
widget.

### List of keywords

- Format: `{ "keyword", "keyword" }`

A list is a Lua array of **strings**. The `keyword` in the list refers to a
widget name. An empty array is an empty list.

```lua
return {
  { select = "mainbox", children = { "inputbar", "listview" } },
  { select = "footer",  children = {} },
}
```

### List of values

- Format: `list(value, value, ...)`

An array of lengths or of bare numbers is **padding**, not a list, so a numeric
list has to say so. `list(...)` is always a list, whatever is in it:

```lua
return {
  { select = "entry", tab_stops = list(px(10), px(20), px(30), px(40)) },
}
```

This is the one place where the Lua form needs a word the old format spelled
with punctuation: `[ 10, 20px, 30px, 40px ]` was unambiguous because padding was
written without brackets.

### Environment variable

- Format: `env.NAME`

`env` is a read-only table holding a view of this process's environment, and it
replaces both of the old format's spellings, `${NAME}` and `env(NAME, default)`.
An undefined variable reads as `nil`.

```lua
return {
  { select = "*", background_color = env.BG },
}
```

A default is Lua's `or`:

```lua
return {
  { select = "*", background_color = env.BG or "#2b1e3a" },
}
```

**What `env` yields is always a string**, where the old `env()` parsed the
variable's text as a property value of any type. So a variable that has to
become something other than a string is converted:

```lua
return {
  { select = "window", width = percent(tonumber(env.WIDTH) or 40) },
}
```

If environment WIDTH is set to `40`, the width is 40% of the monitor; otherwise
the default (`40`) is used. Assigning to `env` raises an error -- it is a view
of the environment, not a place to keep things.

### Inherit

- Format: `inherit`

Inherits the property from its parent widget.

```lua
return {
  { select = "mainbox", border_color = inherit },
}
```

## Elements paths

Element paths exists of two parts, the first part refers to the actual widget
by name. Some widgets have an extra state.

For example:

```lua
return {
  { select = "element selected" },
}
```

Here `element selected` is the name of the widget, `selected` is the state of
the widget.

The difference between dots and spaces is purely cosmetic. These are all the
same:

```lua
return {
  { select = "element .selected" },
  { select = "element.selected" },
  { select = "element selected" },
}
```

### Supported element paths

### Base widgets

The default widgets available in **sofi** and the default hierarchic:

- `window`
  - `overlay`: the overlay widget.

  - `mainbox`: The mainbox box.

  - `inputbar`: The input bar box.
    - `box`: the horizontal @box packing the widgets

    - `case-indicator`: the case/sort indicator @textbox

    - `prompt`: the prompt @textbox

    - `entry`: the main entry @textbox

    - `num-rows`: Shows the total number of rows.

    - `num-filtered-rows`: Shows the total number of rows after
              filtering.

    - `textbox-current-entry`: Shows the text of the currently selected
              entry.

    - `icon-current-entry`: Shows the icon of the currently selected
              entry.

  - `listview`: The listview.

    - `scrollbar`: the listview scrollbar

    - `element`: a box in the listview holding the entries

      - `element-icon`: the widget in the listview's entry showing the
   (optional) icon

      - `element-index`: the widget in the listview's entry
   keybindable index (1,2,3..0)

      - `element-text`: the widget in the listview's entry showing the
   text.

  - `mode-switcher`: the main horizontal @box packing the buttons.
    - `button`: the buttons @textbox for each mode

  - `message`: The container holding the textbox.
    - `textbox`: the message textbox

Note that these path names match the default theme. Themes that provide a
custom layout will have different elements, and structure.

### State

State: State of widget

Optional flag(s) indicating state of the widget, used for theming.

These are appended after the name or class of the widget.

#### Example

```lua
return {
  { select = "button selected.normal" },

  { select = "element selected.urgent" },
}
```

Currently only the entrybox and scrollbar have states:

#### Entrybox

```
{visible modifier}.{state}
```

Where `visible modifier` can be:

- normal: no modification
- selected: the entry is selected/highlighted by user
- alternate: the entry is at an alternating row (uneven row)

Where `state` is:

- normal: no modification
- urgent: this entry is marked urgent
- active: this entry is marked active

These can be mixed.

Example:

```lua
return {
  { select = "nametotextbox selected.active",
    background_color = "#003642",
    text_color       = "#008ed4",
  },
}
```

Sets all selected textboxes marked active to the given text and background
color. Note that a state modifies the original element, it therefore contains
all the properties of that element.

#### Scrollbar

The scrollbar uses the `handle` state when drawing the small scrollbar handle.
This allows the colors used for drawing the handle to be set independently.

## Widget properties

The following properties are currently supported:

### all widgets

- **enabled**:           enable/disable rendering of the widget

- **padding**:           padding
    Padding on the inside of the widget

- **margin**:            padding
    Margin on the outside of the widget

- **border**:            border
    Border around the widget (between padding and margin)/

- **border-radius**:     padding
    Sets a radius on the corners of the borders.
- border-aa:  boolean
    Disable aliasing on the border line. Disabling fixes some drawing issues because of nvidia broken driver workaround.

- border-disable-nvidia-workaround: boolean
    Disable work-around for nvidia driver breaking.

- **background-color**:  color
    Background color

- **background-image**:  image
    Background image

- **border-color**:      color
    Color of the border

- **cursor**:            cursor
    Type of mouse cursor that is set when the mouse pointer is hovered over the
    widget.

### window

- **font**:            string
    The font used in the window

- **transparency**:    string
    Indicating if transparency should be used and what type:
  - **real** - True transparency. Only works with a compositor.
  - **background** - Take a screenshot of the background image and use that.
  - **screenshot** - Take a screenshot of the screen and use that.
  - **Path** to png file - Use an image.

- **location**:       position
      The place of the anchor on the monitor

- **anchor**:         anchor
      The anchor position on the window

- **fullscreen**:     boolean Window is fullscreen.

- **width**:          distance The width of the window

- **x-offset**:       distance

- **y-offset**:       distance The offset of the window to the anchor point,
    allowing you to push the window left/right/up/down

### scrollbar Properties

- **background-color**:    color
- **handle-width**:        distance
- **handle-color**:        color
- **border-color**:        color
- **handle-rounded-corners**:     boolean for rounded scrollbar

### box

- **orientation**:      orientation Set the direction the elements are packed.
- **spacing**:          distance Distance between the packed elements.

### textbox

- **background-color**:  color

- **border-color**:      the color used for the border around the widget.

- **font**:              the font used by this textbox (string).

- **str**/**content**:   the string to display by this textbox (string).

- **vertical-align**:    Vertical alignment of the text. A number between 0
    (top) and 1 (bottom).

- **horizontal-align**:  Horizontal alignment of the text. A number between 0
    (left) and 1 (right).

- **text-color**:        the text color to use.

- **text-transform**:    text style {color} for the whole text.

- **highlight**:         text style {color}. color is optional, multiple
    highlight styles can be added like: bold underline italic #000000; This
    option is only available on the `element-text` widget.

- **width**:             override the desired width for the textbox.

- **content**:           Set the displayed text (String).

- **placeholder**:       Set the displayed text (String) when nothing is
    entered.

- **placeholder-markup**:       If true, placeholder text supports pango
    markup for stylizing.

- **placeholder-color**: Color of the placeholder text.

- **blink**:             Enable/Disable blinking on an input textbox
    (Boolean).

- **markup**:            Force markup on, beware that only valid pango markup
    strings are shown.

- **tab-stops**:         array of distances. Set the location of tab stops by
    their distance from the beginning of the line. Each distance should be
    greater than the previous one. The text appears to the right of the tab
    stop position (other alignments are not supported yet).

- **cursor-width**:      The width of the cursor.

- **cursor-color**:      The color used to draw the cursor.

- **hide-cursor-on-empty**: Hides the cursor when the search field is empty.
    (Boolean)

- **cursor-outline**:      Enable a border (outline) around the cursor.
    (Boolean)

- **cursor-outline-width**: The width of the border around the cursor.
    (Double)

- **cursor-outline-color**: The color to use for the cursor outline.
    (Color)

- **text-outline**:      Enable a border (outline) around the text. (Boolean)

- **text-outline-width**: The width of the border around the text.  (Double)

- **text-outline-color**: The color to use for the text outline.    (Color)

### listview

- **columns**:         integer Number of columns to show (at least 1)

- **fixed-height**:    boolean Always show `lines` rows, even if fewer
    elements are available.

- **dynamic**:         boolean `True` if the size should change when filtering
    the list, `False` if it should keep the original height.

- **scrollbar**:       boolean If the scrollbar should be enabled/disabled.

- **scrollbar-width**: distance Width of the scrollbar

- **cycle**:           boolean When navigating, it should wrap around

- **spacing**:         distance Spacing between the elements (both vertical
    and horizontal)

- **lines**:           integer Number of rows to show in the list view.

- **layout**:           orientation Indicate how elements are stacked.
    Horizontal implements the dmenu style.

- **reverse**:         boolean Reverse the ordering (top down to bottom up).

- **flow**:           orientation The order the elements are laid out.
    Vertical is the original 'column' view.

- **fixed-columns**:    boolean Do not reduce the number of columns shown when
    number of visible elements is not enough to fill them all.

- **require-input**:    boolean Listview requires user input to be unhidden.
    The list is still present and hitting accept will activate the first entry.

### Overlay widget

- **timeout**: The time the widget is visible when showing a temporary message.

## Listview widget

The listview widget is special container widget.
It has the following fixed children widgets:

- 0 or more `element` widgets of the type box.

- An optional `scrollbar` widget. This can be enabled using the scrollbar
    property.

These cannot be changed using the `children` property.

Each Entry displayed by listview is captured by a `box` called `element`.
An `element` widget can contain the following special child widgets:

- `element-icon`: An icon widget showing the icon associated to the entry.
- `element-text`: A textbox widget showing the text associated to the entry.
- `element-index`: A textbox widget that shows the shortcut keybinding number.

By default the `element-icon` and `element-text` child widgets are added to the
`element`. This can be modified using the `children` property or the
`[no]-show-icons` option.

A child added with another name is treated the same as the special widget
described in the [advanced layout](#advanced-layout) section.

### listview text highlight

The `element-text` widget in the `listview` is the one used to show the text.
On this widget set the `highlight` property (only place this property is used)
to change the style of highlighting. The `highlight` property consist of the
`text-style` property and a color.

To disable highlighting:

```lua
return {
  { select = "element-text", highlight = none },
}
```

To set to red underlined:

```lua
return {
  { select = "element-text", highlight = highlight(underline, "red") },
}
```

## Layout

The new format allows the layout of the **sofi** window to be tweaked
extensively. For each widget, the themer can specify padding, margin, border,
font, and more. It even allows, as an advanced feature, to pack widgets in a
custom structure.

### Basic layout structure

The whole view is made out of boxes that pack other boxes or widgets.
The box can be vertical or horizontal. This is loosely inspired by [GTK](http://gtk.org/).

The current layout of **sofi** is structured as follows:

```text
┌────────────────────────────────────────────────────────────────────────────────────┐
│ window {BOX:vertical}                                                              │
│ ┌───────────────────────────────────────────────────────────────────────────────┐  │
│ │ mainbox  {BOX:vertical}                                                       │  │
│ │ ┌───────────────────────────────────────────────────────────────────────────┐ │  │
│ │ │ inputbar {BOX:horizontal}                                                 │ │  │
│ │ │ ┌─────────┐ ┌─┐ ┌───────────────────────────────┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ │ │  │
│ │ │ │ prompt  │ │:│ │ entry                         │ │#fr│ │ / │ │#ns│ │ci │ │ │  │
│ │ │ └─────────┘ └─┘ └───────────────────────────────┘ └───┘ └───┘ └───┘ └───┘ │ │  │
│ │ └───────────────────────────────────────────────────────────────────────────┘ │  │
│ │                                                                               │  │
│ │ ┌───────────────────────────────────────────────────────────────────────────┐ │  │
│ │ │ message                                                                   │ │  │
│ │ │ ┌───────────────────────────────────────────────────────────────────────┐ │ │  │
│ │ │ │ textbox                                                               │ │ │  │
│ │ │ └───────────────────────────────────────────────────────────────────────┘ │ │  │
│ │ └───────────────────────────────────────────────────────────────────────────┘ │  │
│ │                                                                               │  │
│ │ ┌───────────────────────────────────────────────────────────────────────────┐ │  │
│ │ │ listview                                                                  │ │  │
│ │ │ ┌─────────────────────────────────────────────────────────────────────┐   │ │  │
│ │ │ │ element                                                             │   │ │  │
│ │ │ │ ┌─────────────────┐ ┌─────────────────────────────────────────────┐ │   │ │  │
│ │ │ │ │element─icon     │ │element─text                                 │ │   │ │  │
│ │ │ │ └─────────────────┘ └─────────────────────────────────────────────┘ │   │ │  │
│ │ │ └─────────────────────────────────────────────────────────────────────┘   │ │  │
│ │ └───────────────────────────────────────────────────────────────────────────┘ │  │
│ │                                                                               │  │
│ │ ┌───────────────────────────────────────────────────────────────────────────┐ │  │
│ │ │  mode─switcher {BOX:horizontal}                                           │ │  │
│ │ │ ┌───────────────┐   ┌───────────────┐  ┌──────────────┐ ┌───────────────┐ │ │  │
│ │ │ │ Button        │   │ Button        │  │ Button       │ │ Button        │ │ │  │
│ │ │ └───────────────┘   └───────────────┘  └──────────────┘ └───────────────┘ │ │  │
│ │ └───────────────────────────────────────────────────────────────────────────┘ │  │
│ └───────────────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────────┘


```
>
> - ci is the case-indicator
> - fr is the num-filtered-rows
> - ns is the num-rows

### Error message structure

```text
┌──────────────────────────────────────────────────────────────────────────────────┐
│ window {BOX:vertical}                                                            │
│ ┌─────────────────────────────────────────────────────────────────────────────┐  │
│ │ error─message {BOX:vertical}                                                │  │
│ │ ┌────────────────────────────────────────────────────────────────────────┐  │  │
│ │ │ textbox                                                                │  │  │
│ │ └────────────────────────────────────────────────────────────────────────┘  │  │
│ └─────────────────────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────────────────┘

```

### Advanced layout

The layout of **sofi** can be tweaked by packing the 'fixed' widgets in a
custom structure.

The following widgets are fixed, as they provide core **sofi** functionality:

- prompt
- entry
- overlay
- case-indicator
- message
- listview
- mode-switcher
- num-rows
- num-filtered-rows

The following keywords are defined and can be used to automatically pack a
subset of the widgets. These are used in the default theme as depicted in the
figure above.

- mainbox Packs: `inputbar, message, listview, mode-switcher`
- inputbar Packs: `prompt,entry,case-indicator`

Any widget name starting with `textbox` is a textbox widget, others are box
widgets and can pack other widgets.

There are several special widgets that can be used by prefixing the name of the
widget:

#### Textbox widget

This is a read-only textbox widget. The displayed string can be set with `content`.

Example:

```lua
return {
  { select = "textbox-custom",
    expand  = false,
    content = "My Message",
  },
}
```

#### Icon

This is an icon widget. The displayed icon can be set with `filename` and size
with `size`. If the property `action` is set, it acts as a button. `action` can
be set to a keybinding name and completes that action. (see sofi -show keys for
a list).

If the `squared` property is set to **false** the widget height and width are
not forced to be equal.

If the `tint`  property is set with a white, the icons is greyscale. If
another color is set, the icon is tinted to that color.

Example:

```lua
return {
  { select = "icon-paste",
    expand         = false,
    filename       = "gtk-paste",
    size           = 24,
    vertical_align = 0.5,
    action         = "kb-primary-paste",
  },
}
```

#### button

This is a textbox widget that can have a 'clickable' action. The `action` can
be set to: `keybinding`: accepts a keybinding name and completes that action.
(see sofi -show keys for a list).

```lua
return {
  { select = "button-paste",
    expand         = false,
    content        = "My Clickable Message",
    vertical_align = 0.5,
    action         = "kb-primary-paste",
  },
}
```

#### Children

To specify children, set the `children`
property (this always happens on the `box` child, see example below):

```lua
return {
  { select = "inputbar",
    children = { "prompt", "entry", "overlay", "case-indicator" },
  },
}
```

The theme needs to be updated to match the hierarchy specified.

Below is an example of a theme emulating dmenu:

```lua
return {

  { select = "*",
    background_color = color("Black"),
    text_color       = color("White"),
    border_color     = color("White"),
    font             = "Times New Roman 12",
  },

  { select = "window",
    anchor   = north,
    location = north,
    width    = percent(100),
    padding  = px(4),
    children = { "horibox" },
  },

  { select = "horibox",
    orientation = horizontal,
    children    = { "prompt", "entry", "listview" },
  },

  { select = "listview",
    layout  = horizontal,
    spacing = px(5),
    lines   = 10,
  },

  { select = "entry",
    expand = false,
    width  = em(10),
  },

  { select = "element",
    padding = { px(0), px(2) },
  },
  { select = "element selected",
    background_color = color("SteelBlue"),
  },

}
```

### Padding and margin

Just like CSS, **sofi** uses the box model for each widget.

```text
┌──────────────────────────────────────────────────────────────────┐
│ margin                                                           │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ border                                                     │  │
│  │ ┌────────────────────────────────────────────────────────┐ │  │
│  │ │ padding                                                │ │  │
│  │ │ ┌────────────────────────────────────────────────────┐ │ │  │
│  │ │ │ content                                            │ │ │  │
│  │ │ └────────────────────────────────────────────────────┘ │ │  │
│  │ └────────────────────────────────────────────────────────┘ │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

Explanation of the different parts:

- Content - The content of the widget.

- Padding - Clears an area around the widget. The padding shows the
    background color of the widget.

- Border - A border that goes around the padding and content. The border use
    the border-color of the widget.

- Margin - Clears an area outside the border. The margin is transparent.

The box model allows us to add a border around elements, and to define space
between elements.

The size of each margin, border, and padding can be set.
For the border, a linestyle and radius can be set.

### Spacing

Widgets that can pack more then one child widget (currently box and listview)
have the `spacing` property. This property sets the distance between the packed
widgets (both horizontally and vertically).

```text
┌───────────────────────────────────────┐
│ ┌────────┐ s ┌────────┐ s ┌────────┐  │
│ │ child  │ p │ child  │ p │ child  │  │
│ │        │ a │        │ a │        │  │
│ │        │ c │        │ c │        │  │
│ │        │ i │        │ i │        │  │
│ │        │ n │        │ n │        │  │
│ └────────┘ g └────────┘ g └────────┘  │
└───────────────────────────────────────┘
```

### Advanced box packing

More dynamic spacing can be achieved by adding dummy widgets, for example to
make one widget centered:

```text
┌────────────────────────────────────────────────────┐
│  ┌───────────────┐  ┌────────┐  ┌───────────────┐  │
│  │ dummy         │  │ child  │  │ dummy         │  │
│  │ expand: true; │  │        │  │ expand: true; │  │
│  │               │  │        │  │               │  │
│  │               │  │        │  │               │  │
│  │               │  │        │  │               │  │
│  └───────────────┘  └────────┘  └───────────────┘  │
└────────────────────────────────────────────────────┘
```

If both dummy widgets are set to expand, `child` will be centered. Depending on
the `expand` flag of child the remaining space will be equally divided between
both dummy and child widget (expand enabled), or both dummy widgets (expand
disabled).

## Debugging

A theme that does not load says so on stderr, naming the file and the line Lua
stopped at, and the theme that was already loaded stands. There is no popup and
no separate parser debug domain: the whole front end is the Lua interpreter, so
a syntax error is reported the way Lua reports one.

To check a file without starting a surface, validate it:

```bash
sofi -theme-validate mytheme.lua
```

To see the elements queried during running, run:

```bash
G_MESSAGES_DEBUG=Theme sofi -show run
```

To test minor changes, part of the theme can be passed on the command line. A
`-theme-str` argument is a chunk in its own right, so it returns an array like
any other theme -- for example to set it to full-screen:

```bash
sofi -theme-str 'return { { select = "window", fullscreen = true } }' -show run
```

Another syntax to modify theme properties, which needs no Lua at all, is:

```bash
sofi -theme+window+fullscreen true -show run
```

To print the current theme, run:

```bash
sofi -dump-theme
```

What it prints is Lua, and it is a theme: feed it back in with `-theme` and you
get the same tree you dumped. That is the reliable way to settle which of the
sources listed under **Default theme loading** a value actually came from.

## Media support

Parts of the theme can be conditionally loaded, like the CSS `@media` option. A
media block is a directive with a `media` table naming the constraint and a
`rules` array holding the directives it guards:

```lua
return {
  { media = { type = "min-width", value = 120 },
    rules = {
      { select = "window", width = px(800) },
    },
  },
}
```

`type` is the constraint and `value` is what it is compared against. Inside
`rules`, a `"*"` selector is an ordinary child rather than the root, which is
what the old reader did too.

It supports the following keys as constraint:

- `min-width`:         load when width is bigger or equal then value.
- `max-width`:         load when width is smaller then value.
- `min-height`:        load when height is bigger or equal then value.
- `max-height`:        load when height is smaller then value.
- `min-aspect-ratio`   load when aspect ratio is over value.
- `max-aspect-ratio`:  load when aspect ratio is under value.
- `monitor-id`:        The monitor id, see sofi -help for id's. **X11 only** —
  see below.
- `enabled`:           Boolean option to enable. The old format's `DMENU`
  keyword, which stood for whether sofi was invoked in dmenu mode, has no
  spelling here: this key takes a boolean, and `env` is what a theme reads to
  compute one of its own.

**`monitor-id` is not evaluated under Wayland.** A Wayland client learns which
monitor it was placed on only after its window has been drawn, which is later
than the theme is resolved — so the id is not knowable at the point the query
must be answered. Rather than compare against a guess, **sofi** ignores such a
block and warns. Under layer-shell the size and aspect constraints above are
unaffected: the monitor's dimensions *are* known by then and those queries work
normally on both backends.

**Under `xdg-shell` — the fallback used by compositors without layer-shell,
notably Mutter (GNOME) and KWin (Plasma) — the size and aspect constraints are
unavailable too.** No configure event reports the output's dimensions there, and
the compositor, not **sofi**, decides which output the window is placed on, so
there is no monitor to measure. Those blocks are ignored with a warning, exactly
as an unknown monitor below.

If the whole monitor is unknown — which happens on `-dump-processed-theme`,
where no window has been created at all, and for every `xdg-shell` session —
then **every** constraint above except
`enabled` is ignored, each with a warning. This is deliberate: a monitor that
could not be measured would otherwise read as one of size zero, under which
`min-width` never matches and `max-width` always does.

A media block takes an integer number or a fraction, and Lua's own distinction
between the two is what decides which. An integer `value` names the block
`@media ( type: N px )` and a float names it `@media ( type: N.000000 )`, so a
dump reads as it always did -- and `120` and `120.0` are therefore two different
blocks.

```lua
return {
  { media = { type = "min-width", value = 120 },
    rules = {},
  },
}
```

```lua
return {
  { media = { type = "enabled", value = env.DO_LIGHT ~= nil },
    rules = {},
  },
}
```

`enabled` wants a boolean, and `env.DO_LIGHT` is a string or `nil`, so the
comparison is written out. For a variable whose *content* is the switch, compare
against it: `env.DO_LIGHT == "1"`.

## Conflicting constraints

It is possible to define conflicting constraints in the theme. These conflicts
are not explicitly reported. The most common example is forcing a specific
window size, for example by enabling full-screen mode, having number of lines
set in the listview and having the listview expand to available space. There is
clearly a conflict in these 3 constraints. In this case, listview will not
limit to the number of lines, but tries to fill the available space. It is up
to the theme designer to make sure the theme handles this correctly.

## Font Parsing

Sofi uses [pango](https://pango.gnome.org/) for font rendering. The font should
be specified in a format that pango understands. This normally is the font name
followed by the font size. For example:

```text
mono 18
```

Or

```text
FontAwesome 22
```

From the pango manpage:

The string must have the form

```text
\[FAMILY-LIST] \[STYLE-OPTIONS] \[SIZE] \[VARIATIONS]
```

where FAMILY-LIST is a comma-separated list of families optionally terminated
by a comma, STYLE\_OPTIONS is a whitespace-separated list of words where each
word describes one of style, variant, weight, stretch, or gravity, and SIZE is
a decimal number (size in points) or optionally followed by the unit modifier
“px” for absolute size. VARIATIONS is a comma-separated list of font variation
specifications of the form “`axis`=value” (the = sign is optional).

The following words are understood as styles: "Normal”, “Roman”, “Oblique”,
“Italic”.

The following words are understood as variants: “Small-Caps”, “All-Small-Caps”,
“Petite-Caps”, “All-Petite-Caps”, “Unicase”, “Title-Caps”.

The following words are understood as weights: “Thin”, “Ultra-Light”,
“Extra-Light”, “Light”, “Semi-Light”, “Demi-Light”, “Book”, “Regular”,
“Medium”, “Semi-Bold”, “Demi-Bold”, “Bold”, “Ultra-Bold”, “Extra-Bold”,
“Heavy”, “Black”, “Ultra-Black”, “Extra-Black”.

The following words are understood as stretch values: “Ultra-Condensed”,
“Extra-Condensed”, “Condensed”, “Semi-Condensed”, “Semi-Expanded”, “Expanded”,
“Extra-Expanded”, “Ultra-Expanded”.

The following words are understood as gravity values: “Not-Rotated”, “South”,
“Upside-Down”, “North”, “Rotated-Left”, “East”, “Rotated-Right”, “West”.

Any one of the options may be absent. If FAMILY-LIST is absent, then the
family\_name field of the resulting font description will be initialized to
NULL. If STYLE-OPTIONS is missing, then all style options will be set to the
default values. If SIZE is missing, the size in the resulting font description
will be set to 0.

A typical example:

"Cantarell Italic Light 15 \`wght`=200"

## Icon Handling

Sofi supports 3 ways of specifying an icon:

- Filename
- icon-name, this is looked up via the icon-theme.
- Markup String. It renders a string as an icon.

For the first two options, GdkPixbuf is used to open and render the icons.
This in general gives support for most required image formats.
For the string option it uses Pango to render the string. The string needs to
start with a `<span` tag, that allows you to set color and font.

Markup string:

```bash
echo -en "testing\0icon\x1f<span color='red'>⏻</span>" | ./sofi -dmenu
```

Getting supported icon formats:

```bash
G_MESSAGES_DEBUG=Helpers.IconFetcher sofi
```

This uses the debug framework and prints out a list of supported image  file
extensions.

## Multiple file handling

The theme format offers two methods of including other files. This can be
used to modify existing themes, or have multiple variations on a theme.

- import:  Import and parse a second file.
- reset:   Discard theme, and load file as a fresh theme.

Syntax:

```lua
return {
  import("myfile"),

  { reset = true },
  import("mytheme"),
}
```

`import()` returns the imported chunk's array, and a directive with no `select`
is applied in place, so writing `import(...)` as an array element is exactly
what `@import` did. The old format's `@theme "mytheme"` is the pair
`{ reset = true }, import("mytheme")`: the reset throws the tree away and the
import brings in what replaces it.

The commandline option `-theme` is handled similarly to `{ reset = true }`.

The specified file can either by *name*, *filename*,*full path*.

If a filename is provided, it will try to resolve it in the following order:

- If path is absolute and file exists, it will open the file. This includes expansion of '~' or '~user'
- On an `import()` it looks in the directory of the file that tried to include it.
- `${XDG_CONFIG_HOME}/sofi/themes/`
- `${XDG_CONFIG_HOME}/sofi/`
- `${XDG_DATA_HOME}/sofi/themes/`
- `${INSTALL PREFIX}/share/sofi/themes/`

A name is resolved (if it has no valid extension) as a filename by appending the `.lua` and the `.luainc` extension.
It will first look for files with `.lua`, then for files with `.luainc`.

An imported chunk shares the importing one's sandbox, and therefore its
allocation ceiling and its instruction budget: one theme gets one allowance
however many files it is written in. The chain may be sixteen files deep.

If you want to do an optional import, e.g. no error when the file does not exists, you can do:

```lua
return {
  import("myfile", true),
}
```

A second argument of `true` means absence is not an error. It still fails on a
syntax error, but won't abort parsing if the file does not exist -- a missing
optional import returns an empty array, which is a directive that does nothing.

## Examples

Several examples are installed together with **sofi**. These can be found in
`{datadir}/sofi/themes/`, where `{datadir}` is the install path of **sofi**
data. When installed using a package manager, this is usually: `/usr/share/`.

## SEE ALSO

sofi(1), sofi-customisation(5), sofi-script(5), sofi-theme-selector(1)
