-- sofi built-in panel: notification banner
--
-- The daemon's live stack, pinned bottom-right and growing upward from above
-- the task strip. Bottom-right rather than the usual top-right because on
-- hikari-sakura the compositor's own bar owns the top edge and the sheet row
-- now sits under it, which makes the top of the screen the busiest part.
--
-- It stays in the corner while the history menu moved to the bottom centre, so
-- an arriving toast never lands on the part of the task strip the pointer is
-- travelling toward.
--
-- ---------------------------------------------------------------------------
-- NOT A MENU, AND IT MUST NOT LOOK LIKE ONE
--
-- The menus are rows in a panel with a filled selection. These are separate
-- cards on a transparent ground, each with a leading urgency stripe and no
-- selection at all. The distinction is carried by shape rather than by colour,
-- because every surface draws from the same palette -- recolouring the theme
-- must not be able to make a notification look like a launcher row.
--
-- ---------------------------------------------------------------------------
-- Two settings here are load-bearing and are ALSO forced in code for daemon
-- mode -- see sofi_notify_daemon_force_safety() in source/sofi.c. They are
-- repeated here so the file reads honestly, but editing them has no effect:
-- either one wrong makes the desktop unusable for the whole session, and a
-- theme must not be able to cause that.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

{ configuration = {
    show_icons = true,

    -- Action purpose: rows are two lines tall -- summary above, body below.
    -- The listview measures ONE element height at creation from a probe row
    -- (widgets/listview.c), and `eh` is what tells it how many lines that probe
    -- should contain. Without it a notification body is simply clipped.
    eh = 2,

    -- The daemon decides when a notification goes away, per notification.
    -- A view-level timeout would fight it.
    timeout = {
        delay = 0,
    },

    -- Forced in code. With click-to-exit on, sofi grows its layer surface to
    -- cover the whole output to catch outside clicks (wayland/view.c) -- on a
    -- resident daemon that is an invisible full-screen input trap.
    click_to_exit = false,

    -- Forced in code. A banner nobody asked to open takes no keyboard at
    -- all -- not even on-demand, which still lets the compositor hand it
    -- focus. Interact with notifications in the history mode instead.
    wayland_keyboard_interactivity = "none",
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
    location = "south_east",
    anchor   = "south_east",
    width    = px(400),

    -- Action purpose: NEGATIVE is inward here, and that is not a typo. With
    -- click-to-exit off this surface is exactly its own size and the offsets
    -- are handed to zwlr_layer_surface_v1_set_margin as (y, -x, -y, x)
    -- (wayland/display.c), which inverts them. The menus take the other path
    -- and write the same intent as positive numbers.
    x_offset = px(-16),
    -- Clears the task strip: 52px tall, sitting 12px off the bottom edge.
    y_offset = px(-80),

    border           = px(0),
    border_radius    = px(0),
    background_color = transparent,
    padding          = 0,
},

{ select = "mainbox",
    enabled          = true,
    spacing          = px(8),
    background_color = transparent,
    children         = { "listview", "footer" },
},

{ select = "listview",
    enabled = true,
    layout  = "vertical",
    columns = 1,
    -- Ceiling, not a reservation -- see fixed-height below.
    lines   = 5,
    cycle   = false,
    dynamic = true,

    -- Action purpose: load-bearing for input, not just for looks. The default
    -- is TRUE (config.fixed_num_lines), which makes the listview report its
    -- full `lines` height whatever it is actually showing -- so a single
    -- notification produced a 400x382 surface with ~290px of invisible,
    -- transparent, pointer-eating area above it. With this off the surface
    -- hugs the rows and the rest of the screen stays clickable.
    fixed_height = false,
    scrollbar    = false,
    -- Newest at the bottom, nearest the cursor, so the stack grows away from
    -- the task strip rather than into it.
    reverse          = true,
    spacing          = px(8),
    background_color = transparent,
},

{ select = "element",
    enabled     = true,
    orientation = "horizontal",
    padding     = { px(14), px(16) },
    spacing     = px(12),

    -- Action purpose: a stripe down the leading edge rather than a full
    -- border. It is the cheapest way to make a notification unmistakably not a
    -- menu row at a glance -- the menus use a filled selection, these use an
    -- edge marker -- and it doubles as the urgency channel.
    border        = { px(0), px(0), px(0), px(4) },
    border_radius = px(10),
    border_color  = "@accent-soft",

    -- The one surface that uses the opaque ground. A banner lands over
    -- arbitrary content, and body text with a video showing through it is not
    -- readable.
    background_color = "@background-solid",
    text_color       = "@foreground",
    cursor           = "pointer",
},

{ select = { "element normal.normal", "element alternate.normal" },
    border_color = "@accent-soft",
    text_color   = "@foreground",
},

-- Critical urgency. These never expire, so the stripe is the loud one.
{ select = { "element normal.urgent", "element alternate.urgent" },
    border_color = "@critical",
    text_color   = "@foreground",
},

-- Low urgency: no shout at all, and muted text. `foreground-dim` rather than
-- `muted`, which is 2.29:1 against the ground and not a text colour.
{ select = { "element normal.active", "element alternate.active" },
    border_color = "@muted",
    text_color   = "@foreground-dim",
},

-- Action purpose: selection is deliberately invisible here. The banner takes
-- no keyboard (see the configuration block), so there is nothing to move a
-- selection with -- drawing one would promise an interaction that does not
-- exist. Each state simply keeps its own stripe.
{ select = "element selected.normal",
    background_color = "@background-solid",
    border_color     = "@accent-soft",
    text_color       = "@foreground",
},

{ select = "element selected.urgent",
    background_color = "@background-solid",
    border_color     = "@critical",
    text_color       = "@foreground",
},

{ select = "element selected.active",
    background_color = "@background-solid",
    border_color     = "@muted",
    text_color       = "@foreground-dim",
},

{ select = "element-icon",
    background_color = transparent,
    size             = px(32),
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

-- ===================================================================
-- The clear affordance.
--
-- Action purpose: kb-custom-1 has always meant "dismiss every live
-- notification" in source/modes/notifications.c, and until now nothing could
-- ever reach it -- the surface takes no keyboard, so no key arrives. A button
-- widget dispatches the same binding from a pointer click
-- (textbox_button_trigger_action in source/view.c), and pointer input is
-- unaffected by keyboard interactivity. This is what makes the existing
-- feature usable rather than a new feature.
--
-- It sits below the stack because the stack is reversed and grows upward: the
-- button has to be the one thing that does not move as notifications arrive.
-- ===================================================================
{ select = "footer",
    enabled     = true,
    expand      = false,
    orientation = "horizontal",
    padding     = { px(0), px(4) },
    spacing     = px(8),
    children    = { "textbox-spacer", "button-clear-all" },
},

{ select = "textbox-spacer",
    expand           = true,
    background_color = transparent,
    content          = "",
},

{ select = "button-clear-all",
    expand           = false,
    action           = "kb-custom-1",
    content          = "Clear",
    padding          = { px(6), px(12) },
    border_radius    = px(8),
    background_color = "@background-solid",
    text_color       = "@foreground-dim",
    horizontal_align = 0.5,
    vertical_align   = 0.5,
    cursor           = "pointer",
},

}
