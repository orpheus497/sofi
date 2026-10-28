-- sofi built-in panel: notification / message toast
--
-- A small surface in the top-right corner, used for `sofi -e <message>`.
-- Selected automatically for message mode; no user config required.
--
-- The dismissal delay lives in the `configuration = { timeout = { … } }` block
-- below. sofi_view_error_dialog() arms that timer, so the toast removes itself
-- without input. Set `delay = 0` to make a message persist until dismissed.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

{ configuration = {
    timeout = {
        delay  = 5,
        action = "kb-cancel",
    },

    -- Load-bearing, not a preference. With click-to-exit enabled sofi grows
    -- its layer surface to cover the whole output so it can catch clicks
    -- outside the menu (wayland/view.c:262). On a menu that is what you want.
    -- On a toast it means an invisible full-screen input trap sits over the
    -- desktop for the whole delay -- every stray pointer event dismisses the
    -- toast, and nothing underneath can be clicked while it is up.
    click_to_exit = false,

    -- The other half of staying out of the way. Every other sofi menu takes
    -- the keyboard exclusively, which is correct for a menu you are typing
    -- into and wrong for a toast that appears while you are working. on-demand
    -- leaves the keyboard alone until the toast is actually clicked, so
    -- Escape still dismisses it early if you want that.
    wayland_keyboard_interactivity = "on-demand",
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
    location = north_east,
    anchor   = north_east,
    width    = px(380),

    -- Action purpose: these are LAYER-SHELL MARGINS, not software offsets, so
    -- the signs are inverted relative to the menus -- click-to-exit is off, so
    -- the surface is exactly its own size and the offsets go straight to
    -- zwlr_layer_surface_v1_set_margin as (y, -x, -y, x). x:-16 is 16px in from
    -- the right edge; y:16 is 16px down from the top of the usable area.
    --
    -- That is the top of the USABLE area, which the compositor already
    -- shrank by its own top bar before placing this surface. This offset is an
    -- inset to match the other panels, NOT bar clearance -- an earlier comment
    -- here claimed it was clearing the bar, and it never was.
    x_offset = px(-16),
    y_offset = px(16),

    -- A stripe down the leading edge, matching the notification cards, rather
    -- than a ring around the whole toast. A message from sofi and a
    -- notification from an application should read as the same kind of object,
    -- because to the user they are.
    border        = { px(0), px(0), px(0), px(4) },
    border_color  = "@accent-soft",
    border_radius = px(10),

    -- Opaque: a toast lands over arbitrary content, and the message is the
    -- whole point of it.
    background_color = "@background-solid",
    padding          = { px(14), px(16) },
},

{ select = "mainbox",
    enabled          = true,
    background_color = transparent,
    children         = { "message" },
},

{ select = "message",
    enabled          = true,
    background_color = transparent,
    padding          = 0,
},

{ select = "textbox",
    background_color = transparent,
    text_color       = "@foreground",
    vertical_align   = 0.5,
    horizontal_align = 0.0,
},

{ select = "error-message",
    background_color = transparent,
    padding          = 0,
},

}
