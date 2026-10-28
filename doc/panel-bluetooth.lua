-- sofi built-in panel: bluetooth
--
-- A tall pane in the top-right corner, under hikari-sakura's bar. Selected
-- automatically when sofi runs in bluetooth mode; no user config required.
--
-- ---------------------------------------------------------------------------
-- WHY IT SITS WHERE THE VOLUME AND NETWORK PANES SIT
--
-- All three are summoned from the same place -- the status end of the
-- compositor's top bar -- and only one of them is ever on screen at a time,
-- because each dismisses on selection. Sharing the corner means the system
-- menus appear where the eye already is, rather than each claiming a different
-- edge and making the user learn four positions.
--
-- `north east` needs no bar compensation: the compositor answers
-- `set_size(0,0)` with its usable area, which already has the bar subtracted.
-- The offsets are a deliberate gap, not clearance.
--
-- ---------------------------------------------------------------------------
-- WHY IT IS WIDER THAN THE NETWORK PANE
--
-- 520px against the network pane's 460px, and the extra sixty is not
-- decoration. Every device row carries a second line's worth of information on
-- one line -- the address, the class, and then whichever of "connected",
-- "paired", "in range", "needs input setup (Alt+4)" and "no audio path" apply.
-- Those last two are the whole reason a person can tell why a paired gamepad is
-- silent, and at 460px they are the first thing the ellipsis eats.
--
-- ---------------------------------------------------------------------------
-- WHY THIS ONE KEEPS ITS FILTER FIELD
--
-- Like the network pane and unlike the volume one, this list can be long: an
-- inquiry in a flat picks up every phone, television and pair of earbuds in the
-- building, and typing part of a name is the fastest way past them. So the
-- input bar stays and the arrow keys stay with the list.
--
-- The mode's four verbs are on the stock `kb-custom-1` to `kb-custom-4`
-- bindings -- `Alt+1` scan, `Alt+2` disconnect, `Alt+3` forget, `Alt+4` set up
-- -- so there is deliberately no `configuration = { }` rebinding block here. The
-- volume pane needed one because it had claimed `Left` and `Right`; this pane
-- claims nothing, and a rebind that changes nothing is a line that will
-- eventually be believed. All four are named in the message bar.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

  { configuration = {
      show_icons = false,
  } },

  { select = "*",
    background_color = transparent,
    text_color       = "@foreground",
    margin           = 0,
    padding          = 0,
    spacing          = 0,
  },

  { select = "window",
    enabled          = true,
    location         = north_east,
    anchor           = north_east,

    width            = px(520),
    x_offset         = px(-12),
    y_offset         = px(8),

    border           = px(0),
    border_radius    = px(12),
    background_color = "@background",
    padding          = px(10),
  },

  { select = "mainbox",
    enabled          = true,
    background_color = transparent,
    spacing          = px(8),

    -- The message carries the verbs, the netgraph node it is driving, and the
    -- result of the last action -- "Paired and connected X", "Pairing X failed;
    -- nothing was saved". On a surface where a verb can rewrite a root-owned
    -- configuration file, that line is the only confirmation there is, so it is
    -- not optional. It also states when the paired list could not be read,
    -- which is the difference between "nothing is paired" and "sofi cannot
    -- see what is paired".
    children         = { "inputbar", "listview", "message" },
  },

  { select = "inputbar",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(8), px(10) },
    border_radius    = px(8),
    background_color = "@surface",
    children         = { "prompt", "entry" },
  },

  { select = "prompt",
    enabled          = true,
    background_color = transparent,
    text_color       = "@accent-soft",
    padding          = { px(0), px(8), px(0), px(0) },
    vertical_align   = 0.5,
  },

  { select = "entry",
    enabled           = true,
    background_color  = transparent,
    text_color        = "@foreground",
    placeholder       = "Filter",
    placeholder_color = "@foreground-dim",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
  },

  { select = "listview",
    enabled          = true,
    columns          = 1,

    -- The adapter, a household's worth of devices, and the three maintenance
    -- verbs, without reserving that height on a machine with one headset.
    -- `dynamic` is what makes it shrink.
    lines            = 14,
    dynamic          = true,
    fixed_height     = false,

    cycle            = true,
    scrollbar        = true,
    spacing          = px(2),
    background_color = transparent,
  },

  { select = "scrollbar",
    enabled          = true,
    handle_width     = px(4),
    handle_color     = "@accent-soft",
    background_color = "@muted",
    border_radius    = px(2),
  },

  -- See the note in doc/default_theme.lua: the leading border is on every row,
  -- transparent until selected, so selecting one does not shift its text.
  { select = "element",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(7), px(10) },
    border           = { px(0), px(0), px(0), px(3) },
    border_color     = transparent,
    border_radius    = px(8),
    background_color = transparent,
    text_color       = "@foreground",
    cursor           = pointer,
  },

  { select = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    text_color       = "@foreground",
  },

  -- The adapter with the stack down, and a paired device that is out of range.
  -- `foreground-dim`, not `muted`: this is text, and the `muted` palette role is
  -- 2.29:1 against the ground. The same state the volume pane gives a muted sink
  -- and the network pane an interface that is down -- present, but not doing its
  -- job.
  { select = { "element normal.urgent", "element alternate.urgent" },
    background_color = transparent,
    text_color       = "@foreground-dim",
  },

  -- Connected, or on. Filled rather than tinted, so "what am I actually
  -- connected to" is answerable without reading the labels.
  { select = { "element normal.active", "element alternate.active" },
    background_color = "@surface-alt",
    text_color       = "@accent-strong",
  },

  { select = { "element selected.normal", "element selected.active",
               "element selected.urgent" },
    background_color = "@accent",
    border_color     = "@accent-strong",
    text_color       = "@on-accent",
  },

  { select = "element-text",
    background_color = transparent,
    text_color       = inherit,
    vertical_align   = 0.5,
    horizontal_align = 0.0,
    cursor           = inherit,
  },

  { select = "message",
    enabled          = true,
    background_color = "@surface",
    border_radius    = px(8),
    padding          = { px(6), px(10) },
  },

  { select = "textbox",
    background_color = transparent,
    text_color       = "@foreground-dim",
    vertical_align   = 0.5,
    horizontal_align = 0.0,
  },
}
