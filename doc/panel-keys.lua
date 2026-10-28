-- sofi built-in panel: the keyboard shortcut helper
--
-- Selected automatically for `sofi -show keys`. A near-square pane in the
-- middle of the screen, two columns wide.
--
-- ---------------------------------------------------------------------------
-- WHY THIS ONE IS SQUARE AND CENTRED
--
-- Every other sofi surface is a list you are picking *from* -- you arrive
-- knowing roughly what you want, you filter, you choose, it closes. This one is
-- a reference you are reading. Nothing here is actionable: `keys` has no verb
-- on Enter beyond dismissing.
--
-- That changes the right shape. A tall narrow column suits a list you scan for
-- one item; a reference you read wants as much of itself visible at once as
-- possible, which means width as well as height, which means two columns and a
-- roughly square block. Centre rather than an edge, because it is not summoned
-- from anywhere in particular and is not adjacent to anything.
--
-- ---------------------------------------------------------------------------
-- WHY TWO COLUMNS AND NOT MORE
--
-- The rows are `<b>binding</b> name: comment` from
-- config_parser_return_display_help(), and the comment is a sentence. Three
-- columns at this width ellipsize the comments, which loses the half of each
-- row that explains what the binding is for. Two is what fits without cutting.
--
-- `flow = horizontal` with `columns = 2` fills left-to-right then wraps, so the
-- bindings stay in their declared order reading normally. The grid renderer is
-- used rather than BARVIEW because equal-width cells are what put the two
-- columns in a straight line.
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
    location         = center,
    anchor           = center,

    -- Wide enough for two columns of `binding — description` without either
    -- ellipsizing, and a height close enough to that to read as a block rather
    -- than a list.
    width            = px(980),
    height           = percent(70),

    border           = px(0),
    border_radius    = px(14),
    background_color = "@background",
    padding          = px(16),
  },

  { select = "mainbox",
    enabled          = true,
    spacing          = px(12),
    background_color = transparent,

    -- The filter earns its place here more than on most surfaces: the list is
    -- long, fixed, and searched by the name of the thing you half-remember.
    children         = { "inputbar", "listview" },
  },

  { select = "inputbar",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(10), px(14) },
    border_radius    = px(10),
    background_color = "@surface",
    children         = { "prompt", "entry" },
  },

  { select = "prompt",
    enabled          = true,
    background_color = transparent,
    text_color       = "@accent-soft",
    padding          = { px(0), px(10), px(0), px(0) },
    vertical_align   = 0.5,
  },

  { select = "entry",
    enabled           = true,
    background_color  = transparent,
    text_color        = "@foreground",
    placeholder       = "Search bindings",
    placeholder_color = "@hint",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
  },

  { select = "listview",
    enabled          = true,
    columns          = 2,
    flow             = horizontal,

    -- Fills the pane's height; `dynamic` off so a filtered list keeps the block
    -- the same size instead of collapsing and jumping under the pointer.
    lines            = 18,
    dynamic          = false,
    fixed_height     = true,
    fixed_columns    = true,

    cycle            = true,
    scrollbar        = true,
    spacing          = px(4),
    background_color = transparent,
  },

  { select = "scrollbar",
    enabled          = true,
    handle_width     = px(4),
    handle_color     = "@accent-soft",
    background_color = "@muted",
    border_radius    = px(2),
  },

  -- No leading border here, unlike the list surfaces: nothing on this pane is
  -- selectable in a way that matters, so a selection stripe would suggest an
  -- action that does not exist. The row still highlights, to keep the keyboard
  -- cursor findable while scrolling.
  { select = "element",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(6), px(12) },
    border_radius    = px(6),
    background_color = transparent,
    text_color       = "@foreground",
    cursor           = default,
  },

  { select = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    text_color       = "@foreground",
  },

  { select = { "element selected.normal", "element selected.active",
               "element selected.urgent" },
    background_color = "@surface-alt",
    text_color       = "@accent-strong",
  },

  { select = "element-text",
    background_color = transparent,
    text_color       = inherit,
    vertical_align   = 0.5,
    horizontal_align = 0.0,
    cursor           = inherit,
  },
}
