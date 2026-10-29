-- sofi built-in panel: network
--
-- A tall pane in the top-right corner, under hikari-sakura's bar. Selected
-- automatically when sofi runs in network mode; no user config required.
--
-- ---------------------------------------------------------------------------
-- WHY IT SITS WHERE THE VOLUME PANE SITS
--
-- Both are summoned from the same place -- the status end of the compositor's
-- top bar -- and only one of them is ever on screen at a time, because each
-- dismisses on selection. Sharing the corner means the system menus appear
-- where the eye already is, rather than each claiming a different edge and
-- making the user learn four positions.
--
-- It is taller than the volume pane because the list is longer and genuinely
-- variable: interfaces, then every network in range, then the maintenance
-- verbs. `dynamic` keeps it short on a machine with one interface and nothing
-- in range.
--
-- `north east` needs no bar compensation -- the compositor answers
-- `set_size(0,0)` with its usable area, which already has the bar subtracted.
-- The offsets are a deliberate gap, not clearance.
--
-- ---------------------------------------------------------------------------
-- WHY THIS ONE KEEPS ITS FILTER FIELD
--
-- Unlike the volume and sheet panes, this list can be long -- a flat in a city
-- sees thirty access points -- and typing part of an SSID is the fastest way to
-- reach one. So the input bar stays, and the arrow keys stay with the list
-- where they belong. The mode's extra verbs are on `kb-custom-1` (rescan),
-- `kb-custom-2` (disconnect) and `kb-custom-3` (forget a saved network), and
-- all three are named in the message bar.
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
    location         = "north_east",
    anchor           = "north_east",

    width            = px(460),
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

    -- The message carries the verbs, the backend name and the result of the
    -- last action -- "DHCP lease renewed", "Could not join X". On a surface
    -- where every verb changes system state and most of them take a moment,
    -- that line is the only feedback there is, so it is not optional.
    children         = { "inputbar", "listview", "message" },
  },

  { select = "inputbar",
    enabled          = true,
    orientation      = "horizontal",
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

    -- Room for a couple of interfaces, a screenful of networks and the three
    -- maintenance verbs, without reserving that height on a wired-only
    -- machine. `dynamic` is what makes it shrink.
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
    orientation      = "horizontal",
    padding          = { px(7), px(10) },
    border           = { px(0), px(0), px(0), px(3) },
    border_color     = transparent,
    border_radius    = px(8),
    background_color = transparent,
    text_color       = "@foreground",
    cursor           = "pointer",
  },

  { select = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    text_color       = "@foreground",
  },

  -- An interface that is down. `foreground-dim`, not `muted`: this is text, and
  -- the `muted` palette role is 2.29:1 against the ground. The same state the
  -- task strip uses for a minimised window and the volume pane for a muted
  -- sink -- present, but not doing its job.
  { select = { "element normal.urgent", "element alternate.urgent" },
    background_color = transparent,
    text_color       = "@foreground-dim",
  },

  -- Connected, up, or on. Filled rather than tinted, so "what am I actually on"
  -- is answerable without reading the labels.
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
