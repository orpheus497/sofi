-- sofi built-in panel: notification history
--
-- The whole ring buffer, live and retired. Unlike the banner this is a menu the
-- user summoned deliberately, so it takes the keyboard, it filters, and
-- clicking outside dismisses it.
--
-- A tall panel down the east edge. The right edge is where a notification
-- centre conventionally lives, and it puts the history beside the corner the
-- live banner appears in rather than across the screen from it -- the two are
-- views of the same ring, and reaching for one after the other should not mean
-- crossing the display.
--
-- ---------------------------------------------------------------------------
-- IT IS STILL NOT A MENU, VISUALLY
--
-- It shares the banner's vocabulary rather than the launcher's: cards with a
-- leading stripe, and a selection that is a recess plus an accent stripe rather
-- than the solid accent fill every other menu uses. It takes the keyboard, so
-- unlike the banner it must draw a selection -- it just must not draw the
-- menus' one.
--
-- ---------------------------------------------------------------------------
-- THE TWO CLEANUP VERBS
--
--   kb-custom-1 / "Dismiss"  -- retire what is still on screen, keep the record
--   kb-custom-2 / "Clear"    -- destroy the record
--
-- Deliberately separate: clearing a banner off the screen should not also lose
-- the list of what you missed. Both are also exposed as buttons, because this
-- panel is as likely to be driven by pointer as by keyboard. See
-- source/modes/notification-history.c for how they reach the daemon's ring from
-- this separate process.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

{ configuration = {
    show_icons = true,
    -- Two-line rows: heading above, body below. See the note in
    -- panel-notifications.lua.
    eh = 2,
} },

{ select = "*",
    background_color = transparent,
    text_color       = "@foreground",
    margin           = 0,
    padding          = 0,
    spacing          = 0,
},

{ select = "window",
    enabled  = true,
    location = east,
    anchor   = east,

    -- Wider than the launcher, because a row here is two lines of arbitrary
    -- application text rather than one application name, and the footer carries
    -- a count and both cleanup verbs.
    width  = px(420),
    height = percent(76),

    -- Positive is inward on this path -- the surface covers the usable area to
    -- catch outside clicks and the panel is placed inside it in software.
    x_offset = px(16),
    y_offset = px(0),

    border           = px(0),
    border_radius    = px(14),
    background_color = "@background",
    padding          = px(16),
},

{ select = "mainbox",
    enabled          = true,
    spacing          = px(12),
    background_color = transparent,
    children         = { "inputbar", "listview", "footer" },
},

{ select = "inputbar",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(10), px(12) },
    spacing          = px(10),
    background_color = "@surface",
    border_radius    = px(10),
    children         = { "prompt", "entry" },
},

{ select = "prompt",
    enabled          = true,
    background_color = transparent,
    text_color       = "@accent-soft",
    vertical_align   = 0.5,
},

{ select = "entry",
    enabled           = true,
    background_color  = transparent,
    text_color        = "@foreground",
    cursor            = text,
    placeholder       = "Search notifications",
    placeholder_color = "@hint",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
},

{ select = "listview",
    enabled          = true,
    layout           = vertical,
    columns          = 1,
    lines            = 10,
    cycle            = true,
    dynamic          = true,
    scrollbar        = true,
    spacing          = px(6),
    background_color = transparent,
},

{ select = "scrollbar",
    enabled                = true,
    handle_width           = px(4),
    handle_color           = "@accent-soft",
    handle_rounded_corners = true,
    background_color       = "@surface",
    border_radius          = px(4),
},

-- Cards, like the banner -- the leading stripe is the shared cue that this is
-- a notification and not a list of things to launch.
{ select = "element",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(10), px(12) },
    spacing          = px(10),
    border           = { px(0), px(0), px(0), px(4) },
    border_color     = "@muted",
    border_radius    = px(10),
    background_color = transparent,
    text_color       = "@foreground-dim",
    cursor           = pointer,
},

-- Retired entries: the common case in a history list, so they are the muted
-- default rather than a special state. `foreground-dim` is still AA-legible;
-- the recession comes from the stripe going grey, not from unreadable text.
{ select = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    border_color     = "@muted",
    text_color       = "@foreground-dim",
},

-- Still on screen right now.
{ select = { "element normal.active", "element alternate.active" },
    border_color = "@accent-strong",
    text_color   = "@foreground",
},

-- Critical, and already retired.
{ select = { "element normal.urgent", "element alternate.urgent" },
    border_color = "@critical",
    text_color   = "@urgent",
},

-- Action purpose: a recess plus a stripe, NOT the solid accent fill the menus
-- use. This panel takes the keyboard so it has to show where the cursor is;
-- using the launcher's treatment to do it would undo the whole point of making
-- notifications look unlike menus.
{ select = { "element selected.normal", "element selected.active",
             "element selected.urgent" },
    background_color = "@surface-alt",
    border_color     = "@accent",
    text_color       = "@foreground",
},

{ select = "element-icon",
    background_color = transparent,
    size             = px(24),
    vertical_align   = 0.0,
    cursor           = inherit,
},

{ select = "element-text",
    background_color = transparent,
    text_color       = inherit,
    vertical_align   = 0.0,
    horizontal_align = 0.0,
    cursor           = inherit,
},

-- Count on the left, the two verbs on the right. `num-filtered-rows` carries a
-- bare number, so the label beside it is a static textbox.
{ select = "footer",
    enabled      = true,
    expand       = false,
    orientation  = horizontal,
    spacing      = px(8),
    padding      = { px(8), px(2), px(0), px(2) },
    border       = { px(1), px(0), px(0), px(0) },
    border_color = "@border-soft",
    children     = { "num-filtered-rows", "textbox-shown",
                     "textbox-spacer", "button-dismiss-all",
                     "button-clear-history" },
},

{ select = { "num-filtered-rows", "textbox-shown" },
    expand           = false,
    background_color = transparent,
    text_color       = "@foreground-dim",
    vertical_align   = 0.5,
},

{ select = "textbox-shown",
    content = "shown",
    margin  = { px(0), px(0), px(0), px(4) },
},

{ select = "textbox-spacer",
    expand           = true,
    background_color = transparent,
    content          = "",
},

{ select = { "button-dismiss-all", "button-clear-history" },
    expand           = false,
    padding          = { px(6), px(12) },
    border_radius    = px(8),
    background_color = "@surface",
    text_color       = "@foreground-dim",
    horizontal_align = 0.5,
    vertical_align   = 0.5,
    cursor           = pointer,
},

{ select = "button-dismiss-all",
    action  = "kb-custom-1",
    content = "Dismiss",
},

-- The destructive one, so it is the one that carries a warning colour rather
-- than sharing the neutral treatment.
{ select = "button-clear-history",
    action     = "kb-custom-2",
    content    = "Clear",
    text_color = "@urgent",
},

}
