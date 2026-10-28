-- sofi built-in panel: display
--
-- ---------------------------------------------------------------------------
-- WHY THIS ONE IS NOT SHAPED LIKE THE OTHER SYSTEM PANES
--
-- Volume, network and bluetooth share the top-right corner and a narrow column,
-- because all three are *pick and dismiss*: you arrive knowing roughly what you
-- want, choose it, and the surface closes. A narrow column suits that.
--
-- Display configuration is not that interaction. You are adjusting a spatial
-- layout, comparing outputs against each other, and stepping in and out of
-- per-output settings -- resolution, refresh, position, scale, rotation,
-- brightness -- often several in a row before it is right. **You sit with this
-- surface rather than passing through it.** That is the same argument that gave
-- `keys` its own shape, and it lands in the same place: wide, centred, and tall
-- enough that a mode list does not become a peephole.
--
-- It is centred and not in the corner for a second, practical reason: this pane
-- changes the geometry of the screen it is drawn on. A corner-anchored surface
-- jumps when the resolution under it changes. A centred one stays where the eye
-- already is.
--
-- ---------------------------------------------------------------------------
-- THE ROWS ARE TABULAR, AND THAT IS WHY THE PANE IS WIDE
--
-- Every row carries a label and a *current value*, and the values are what the
-- eye scans -- "which of these is at 60Hz", "which one is scaled". The mode
-- emits them in a fixed-width span so they line up into a column without a
-- table widget. 760px is what a two-column layout of
-- `name  1920x1200@60  1.00x  normal  17%` needs before the value column starts
-- ellipsizing, which is exactly the half worth reading.
--
-- ---------------------------------------------------------------------------
-- IT KEEPS THE FILTER FIELD, AND ONLY BECAUSE OF ONE LEVEL
--
-- At the top level there are two or three outputs and filtering is pointless.
-- One level down, a mode list can be twenty-five entries -- this machine's DP-3
-- has exactly that -- and typing `1920` is far and away the fastest way to the
-- one you want. The field stays for that level.
--
-- **The prompt is not the breadcrumb, and cannot be.** It comes from
-- `mode_get_display_name()`, a static theme property refreshed only when the
-- mode is switched -- not when its rows reload -- so a mode cannot rewrite it
-- per level. The level is shown in the message bar instead, which does refresh.
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

    width            = px(760),

    border           = px(0),
    border_radius    = px(14),
    background_color = "@background",
    padding          = px(14),
  },

  { select = "mainbox",
    enabled          = true,
    background_color = transparent,
    spacing          = px(10),

    -- The message carries the current level, the verbs and the result of the
    -- last apply -- "1920x1080@60 on DP-3", "the compositor refused that
    -- mode". On a surface where every action changes what the screen is doing,
    -- that line is the only confirmation there is.
    children         = { "inputbar", "listview", "message" },
  },

  { select = "inputbar",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(9), px(12) },
    border_radius    = px(9),
    background_color = "@surface",
    children         = { "prompt", "entry" },
  },

  -- Styled as an accent because it labels the filter field, not because it
  -- tracks the level -- see the note above; the level lives in the message.
  --
  -- `accent-soft` and not `accent`, which is what the other four panels use and
  -- what this one should always have used. `accent` is a FILL role: it is held to
  -- the 3:1 of a graphical object, and on Sakura Dark it is the one text pair in
  -- the set that cannot reach body AA -- 2.39:1 against the `surface` this prompt
  -- is drawn on, which roles.lua records as R-104. `accent-soft` is the role for
  -- a label at this weight and size and clears 4.5:1 on all three palettes. The
  -- two are neighbouring tones, so this reads as the same styling choice.
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
    placeholder       = "Filter",
    placeholder_color = "@foreground-dim",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
  },

  { select = "listview",
    enabled          = true,
    columns          = 1,

    -- Deep enough for a mode list without scrolling on the common case, and
    -- `dynamic` keeps it short at the top level where there are two outputs.
    lines            = 13,
    dynamic          = true,
    fixed_height     = false,

    cycle            = true,
    scrollbar        = true,
    spacing          = px(3),
    background_color = transparent,
  },

  { select = "scrollbar",
    enabled          = true,
    handle_width     = px(4),
    handle_color     = "@accent-soft",
    background_color = "@muted",
    border_radius    = px(2),
  },

  -- Roomier than the pick-and-dismiss panes: these rows are read and compared,
  -- not skimmed past. The leading border is on every row, transparent until
  -- selected, so selecting one does not shift its text.
  { select = "element",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(9), px(12) },
    border           = { px(0), px(0), px(0), px(3) },
    border_color     = transparent,
    border_radius    = px(9),
    background_color = transparent,
    text_color       = "@foreground",
    cursor           = pointer,
  },

  { select = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    text_color       = "@foreground",
  },

  -- A disabled output, and the `..` row. Present, but not the thing you came for.
  -- `foreground-dim` and not `muted`: this is text, and the `muted` palette role
  -- is 2.29:1 against the ground.
  { select = { "element normal.urgent", "element alternate.urgent" },
    background_color = transparent,
    text_color       = "@foreground-dim",
  },

  -- The value currently in effect -- the current mode, the current scale, the
  -- current rotation. Filled rather than tinted, so "what is this output doing
  -- right now" is answerable without reading a single label.
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
    border_radius    = px(9),
    padding          = { px(8), px(12) },
  },

  { select = "textbox",
    background_color = transparent,
    text_color       = "@foreground-dim",
    vertical_align   = 0.5,
    horizontal_align = 0.0,
  },
}
