-- sofi default theme -- the installed copy.
--
-- The same layout sofi compiles in as /org/sofi/default.lua. The one
-- difference is how it reaches the palette: through import(), because a file on
-- disk can do that and a GResource cannot -- which is why the built-in copy has
-- its palette parsed for it at startup instead.
--
-- This is a starting point for your own theme, not a file sofi reads by
-- default. Nothing needs to be installed for sofi to work -- copy it to
-- ~/.config/sofi/sofi.lua to take it over. See sofi-customisation(5).

return {

{ configuration = {
    -- Icons are what make a launcher scannable rather than readable. drun,
    -- run and filebrowser all resolve them through the icon fetcher; ssh has
    -- none and shows an empty gutter, which is noted in the manpage.
    show_icons                = true,

    -- Action purpose: two-tier rows. The bracketed section is dropped entirely
    -- when {generic} is empty, so an application with no GenericName gets a
    -- clean single-line row rather than a dangling separator. `alpha` rather
    -- than a colour so the secondary tier follows whatever the row's text
    -- colour currently is -- including the inverted colour it takes when
    -- selected, which a hardcoded foreground would get wrong.
    drun_display_format       = "{name} [<span size='small' alpha='55%'>{generic}</span>]",
} },

import("colors-default"),

-- WHERE COLOUR COMES FROM
--
-- The import above is this file's stand-in for a cascade the binary runs for
-- itself, and it is worth knowing in which order, because the whole of
-- recolouring sofi rests on it being last-wins:
--
--   1. one of three compiled-in palettes -- the sixteen positional slots, the
--      five tones derived from them and the eight values the role map settles
--      by measuring the palette. Chosen by `-palette sakura`, `sakura-dark` or
--      `sakura-light`, defaulting to `sakura`; an unrecognised name warns and
--      takes the default.
--   2. hikari-sakura's own palette, if its configuration is readable, parsed
--      OVER the one just selected so the compositor overrides sofi rather than
--      the other way round. All sixteen slots or none, and no configuration at
--      all is the ordinary case and is silent.
--   3. /org/sofi/roles.lua -- the semantic names, which are what the layouts
--      below refer to. Every one is a reference, so all of them re-derive from
--      whichever of the two above ended up in force.
--   4. a system sofi/settings.lua, then ~/.config/sofi/sofi.lua -- this file,
--      when you have copied it there. Parsed last, so it wins over all of
--      the above.
--
-- `import("colors-default")` collapses steps 1 and 3 into one file, because a
-- theme reached through `-theme` has had those resources freed out from under
-- it and has to carry its own colours. Step 2 has no equivalent here: the
-- import is something the binary does, not something a theme can ask for.
--
-- `-dump-theme` prints the result for the invocation you give it, which is the
-- only reliable way to see which of the four a value came from.
--
-- Action purpose: pin a palette without passing -palette on every invocation.
-- To turn it on, uncomment the `import` line at the end of this comment and
-- put a palette in ~/.config/sofi/palette.lua next to this file.
--
-- It ships commented out because a missing optional import is reported at
-- warning level rather than debug: left live with no palette.lua beside it,
-- this one line prints a GLib WARNING to stderr on every invocation -- an entry
-- in the session journal per keypress for a launcher bound to a hotkey, and a
-- warning interleaved with the output of -dump-config and -dump-theme. The
-- second argument still means absence is not an error, so the parse carries on
-- untouched either way; it is only the report that is noisy. Once the file
-- exists the line is silent.
--
-- Put a whole palette in it, not just the slots you want moved: the five
-- derived tones and the eight measured picks above are this palette's, written
-- as literals or as the slot that measured best against THIS ground, so a file
-- that redefines the sixteen slots and leaves them alone will have Sakura's
-- surfaces and Sakura's placeholder under another scheme's text. Sofi installs
-- its own three palettes where it looks for themes, so
-- `import("palette-sakura-light", true)` pins one of those by name with nothing
-- to copy; otherwise write the sixteen slots plus the tones the comments above
-- describe.
--
-- Imported after the default colours and before every layout rule below, so a
-- `{ select = "*" }` block further down still wins over it -- the same ordering
-- the compiled-in cascade uses.
-- import("palette", true),

{ select = "*",
    background_color             = transparent,
    text_color                   = "@foreground",
    margin                       = 0,
    padding                      = 0,
    spacing                      = 0,
},

{ select = "window",
    enabled                      = true,
    location                     = "south",
    anchor                       = "south",
    width                        = px(560),
    height                       = percent(62),

    x_offset                     = px(0),

    -- Positive is inward from the anchored edge on this path: the surface
    -- covers the whole usable area to catch outside clicks, and the panel is
    -- placed inside it in software (wayland/view.c). The daemon's panels use
    -- the layer-shell margin path instead, where the sign is the other way
    -- round.
    --
    -- Sized to clear the task strip, which is 52px tall and sits 12px off the
    -- bottom edge, plus a 16px gap.
    y_offset                     = px(80),

    border                       = px(0),
    border_radius                = px(14),
    background_color             = "@background",
    padding                      = px(0),

    -- MOTION
    --
    -- `animation-duration` is milliseconds, read off this widget and this
    -- widget only, and it is the one knob: every piece of motion sofi draws
    -- runs for that long. A panel fades up into place, the notification banner
    -- travels in from the edge it is anchored to, the listview's outgoing and
    -- incoming highlights cross-fade as the selection moves, a hovered row
    -- fades in while the one the pointer just left fades out, and a live
    -- notification's cue settles once. 0 means instant and is the way to turn
    -- all of it off; anything above 1000 is clamped to 1000, because a menu
    -- that takes longer than a second to appear is a menu that is in the way.
    --
    -- Action purpose: shipped commented out rather than set, because the
    -- resolution order is deliberate and setting it here would pre-empt the
    -- middle step:
    --
    --   1. this property, whenever a theme sets it;
    --   2. otherwise hikari-sakura's own `animation.duration`, read from
    --      `ui.animation` or a top-level `animation` in its configuration --
    --      `enabled = false` there counts as 0, so a compositor that does not
    --      animate stops these panels animating too;
    --   3. otherwise 180ms, which is the number saber settles on as well, so a
    --      desktop with no animation block anywhere still moves at one speed.
    --
    -- Uncomment it to take the decision away from the compositor. The whole
    -- subsystem is Wayland-only: the XCB backend has no frame-callback
    -- equivalent, so on X11 every one of those animations is instant whatever
    -- this says.
    -- animation_duration           = 180,
},

{ select = "mainbox",
    enabled                      = true,
    padding                      = px(16),
    spacing                      = px(12),
    background_color             = transparent,
    children                     = { "inputbar", "message", "listview", "footer" },
},

{ select = "inputbar",
    enabled                      = true,
    orientation                  = "horizontal",
    padding                      = { px(10), px(12) },
    spacing                      = px(10),
    background_color             = "@surface",
    border_radius                = px(10),
    children                     = { "prompt", "entry" },
},

{ select = "prompt",
    enabled                      = true,
    background_color             = transparent,
    text_color                   = "@accent-soft",
    vertical_align               = 0.5,
},

{ select = "entry",
    enabled                      = true,
    background_color             = transparent,
    text_color                   = "@foreground",
    cursor                       = "text",
    placeholder                  = "Search",
    placeholder_color            = "@hint",
    cursor_color                 = "@accent",
    cursor_width                 = px(2),
    vertical_align               = 0.5,
},

-- Mode errors and `-mesg` text. Kept in the layout so a message never has
-- nowhere to render; it collapses to nothing when there is none.
{ select = "message",
    enabled                      = true,
    background_color             = "@surface",
    border_radius                = px(10),
    padding                      = { px(10), px(12) },
},

{ select = "textbox",
    background_color             = transparent,
    text_color                   = "@foreground",
    vertical_align               = 0.5,
    horizontal_align             = 0.0,
},

{ select = "listview",
    enabled                      = true,
    columns                      = 1,
    lines                        = 12,
    cycle                        = true,
    dynamic                      = true,
    scrollbar                    = true,
    layout                       = "vertical",
    reverse                      = false,
    fixed_height                 = true,
    spacing                      = px(4),
    background_color             = transparent,
},

{ select = "scrollbar",
    enabled                      = true,
    handle_width                 = px(4),
    handle_color                 = "@accent-soft",
    handle_rounded_corners       = true,
    background_color             = "@surface",
    border_radius                = px(4),
},

-- Action purpose: the 3px leading border is on EVERY element, transparent
-- until the row is selected. Adding it only to the selected rule would change
-- that row's content box and shift its text sideways by 3px each time the
-- cursor moved.
{ select = "element",
    enabled                      = true,
    orientation                  = "horizontal",
    padding                      = { px(9), px(12) },
    spacing                      = px(10),
    border                       = { px(0), px(0), px(0), px(3) },
    border_color                 = transparent,
    border_radius                = px(8),
    background_color             = transparent,
    text_color                   = "@foreground",
    cursor                       = "pointer",
},

{ select = { "element normal.normal", "element alternate.normal" },
    background_color             = transparent,
    text_color                   = "@foreground",
},

{ select = { "element normal.active", "element alternate.active" },
    text_color                   = "@accent-strong",
},

{ select = { "element normal.urgent", "element alternate.urgent" },
    text_color                   = "@urgent",
},

-- Fill plus marker, not fill alone. The marker is what stays legible while a
-- repaint is in flight and on a row whose own colour is already close to the
-- accent.
{ select = { "element selected.normal", "element selected.active", "element selected.urgent" },
    background_color             = "@accent",
    border_color                 = "@accent-strong",
    text_color                   = "@on-accent",
},

{ select = "element-icon",
    background_color             = transparent,
    size                         = px(22),
    vertical_align               = 0.5,
    cursor                       = inherit,
},

{ select = "element-text",
    background_color             = transparent,
    text_color                   = inherit,
    vertical_align               = 0.5,
    horizontal_align             = 0.0,
    cursor                       = inherit,
},

-- Shown matches over total. `num-filtered-rows` and `num-rows` are fixed
-- widgets that carry a bare number each, so the separator between them is an
-- ordinary textbox with static content.
{ select = "footer",
    enabled                      = true,

    -- Action purpose: load-bearing. An unrecognised widget name becomes a box
    -- packed with expand=TRUE (sofi_view_add_widget), and the listview expands
    -- too -- so without this the two split the panel's height between them and
    -- the list renders at half the window's size with the rest left empty.
    -- Every packed box that is not meant to grow needs this.
    expand                       = false,

    orientation                  = "horizontal",
    spacing                      = px(4),
    padding                      = { px(4), px(4), px(0), px(4) },
    children                     = { "num-filtered-rows", "textbox-of", "num-rows" },
},

{ select = { "num-filtered-rows", "num-rows", "textbox-of" },
    expand                       = false,
    background_color             = transparent,
    text_color                   = "@foreground-dim",
    vertical_align               = 0.5,
},

{ select = "textbox-of",
    content                      = "/",
},

}
