-- sofi built-in panel: sheet switcher
--
-- A horizontal row of ten chips, centred under hikari-sakura's top bar.
-- Selected automatically when sofi runs in sheets mode; no user config
-- required.
--
-- hikari-sakura divides each workspace into ten fixed sheets, 0-9, permanently
-- bound to their output. The row is sized for exactly that: ten chips, no
-- scrollbar, no dynamic resizing, no filter field -- there is nothing to search
-- in a fixed set of ten.
--
-- ---------------------------------------------------------------------------
-- WHY `north` NEEDS NO BAR COMPENSATION
--
-- sofi's idea of the screen on Wayland is whatever the layer-shell `configure`
-- event reported, and the compositor answers a `set_size(0,0)` with its USABLE
-- area -- which already has the top bar's rows subtracted
-- (hikari/src/layer_shell.c, arrange_layers calls hikari_bar_reserve first).
-- So `location: north` is flush against the underside of the bar on its own,
-- and the offset below is a deliberate gap rather than clearance. Do not add
-- the bar's height to it.
--
-- ---------------------------------------------------------------------------
-- WHY THIS IS A GRID AND THE TASK STRIP IS NOT
--
-- The task strip uses `layout: horizontal`, the BARVIEW renderer, because it
-- holds an unknown number of windows with titles of unknown length and has to
-- scroll. This row is the opposite case: exactly ten items, forever, each a
-- fixed five characters. So it uses the ordinary grid renderer with ten
-- columns and one row, which divides the width into ten EQUAL cells. That is
-- what actually pins the chip positions -- BARVIEW would size each chip to its
-- own content, and a two-digit window count would then shift every chip to its
-- right.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

  { select = "*",
    background_color = transparent,
    text_color       = "@foreground",
    margin           = 0,
    padding          = 0,
    spacing          = 0,
  },

  { select = "window",
    enabled  = true,
    location = "north",
    anchor   = "north",

    -- Ten equal cells are cut out of this, so it is the one number that decides
    -- how much room a chip's label gets. Sized for five monospace characters
    -- plus chrome at the default font with slack; raise it if you use a larger
    -- font and the counts start ellipsizing.
    width    = px(720),
    x_offset = px(0),

    -- The deliberate gap under the bar. Positive is inward on this path -- the
    -- surface covers the usable area to catch outside clicks and the panel is
    -- placed inside it in software.
    y_offset = px(8),

    border           = px(0),
    border_radius    = px(12),
    background_color = "@background",
    padding          = px(8),
  },

  { select = "mainbox",
    enabled          = true,
    background_color = transparent,
    children         = { "listview" },
  },

  { select = "listview",
    enabled = true,
    layout  = "vertical",
    flow    = "horizontal",
    columns = 10,
    lines   = 1,
    cycle   = true,

    -- Ten sheets always exist, so the row must not shrink to the number of
    -- inhabited ones -- that would make the chip positions move under the
    -- user's fingers between invocations. `fixed-columns` is the one that
    -- actually enforces it: without it the grid drops to as many columns as
    -- there are entries.
    fixed_columns    = true,
    dynamic          = false,
    fixed_height     = true,
    scrollbar        = false,
    spacing          = px(4),
    background_color = transparent,
  },

  -- See the note in doc/default_theme.lua: the leading border is on every chip,
  -- transparent until selected, so selecting one does not shift its label.
  { select = "element",
    enabled          = true,
    orientation      = "horizontal",
    padding          = { px(8), px(6) },
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

  -- Sheets with no windows on them. `foreground-dim`, not `muted`: this is text,
  -- and `muted` is 2.29:1 against the ground.
  { select = { "element normal.urgent", "element alternate.urgent" },
    background_color = transparent,
    text_color       = "@foreground-dim",
  },

  -- The sheet currently being displayed. Filled rather than merely tinted, so
  -- "where am I" is answerable without reading the digits.
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
    horizontal_align = 0.5,
    cursor           = inherit,
  },

}
