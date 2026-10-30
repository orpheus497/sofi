-- sofi built-in panel: application menu, and the general surface
--
-- A panel rising from the bottom centre, clearing the task strip. Selected for
-- `drun`, and for every mode that is not one of the four purpose-built surfaces
-- -- `run`, `ssh`, `combi`, `filebrowser`, `recursivebrowser`, `keys` and user
-- script modes all land here, which is why it is written as a general list
-- surface rather than as a launcher specifically.
--
-- Centre rather than an edge: it is the surface summoned most often and the one
-- the user is reading rather than glancing at, so it sits in front of them. The
-- edges are left to the surfaces that belong there -- notifications east, tasks
-- along the bottom, sheets under the top bar. Placement is four properties; see
-- sofi-customisation(5) for the override that moves it to any edge.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

  { configuration = {
      -- Icons are what make a launcher scannable rather than readable. drun,
      -- run and filebrowser all resolve them through the icon fetcher; ssh has
      -- none and shows an empty gutter, which is noted in the manpage.
      show_icons          = true,

      -- Action purpose: two-tier rows. The bracketed section is dropped entirely
      -- when {generic} is empty, so an application with no GenericName gets a
      -- clean single-line row rather than a dangling separator. `alpha` rather
      -- than a colour so the secondary tier follows whatever the row's text
      -- colour currently is -- including the inverted colour it takes when
      -- selected, which a hardcoded foreground would get wrong.
      drun_display_format = "{name} [<span size='small' alpha='55%'>{generic}</span>]",
  } },

  { select           = "*",
    background_color = transparent,
    text_color       = "@foreground",
    margin           = 0,
    padding          = 0,
    spacing          = 0,
  },

  { select           = "window",
    enabled          = true,
    location         = "south",
    anchor           = "south",
    width            = px(560),
    height           = percent(62),

    x_offset         = px(0),

    -- Positive is inward from the anchored edge on this path: the surface
    -- covers the whole usable area to catch outside clicks, and the panel is
    -- placed inside it in software (wayland/view.c). The daemon's panels use
    -- the layer-shell margin path instead, where the sign is the other way
    -- round.
    --
    -- Sized to clear the task strip, which is 52px tall and sits 12px off the
    -- bottom edge, plus a 16px gap.
    y_offset         = px(80),

    border           = px(0),
    border_radius    = px(14),
    background_color = "@background",
    padding          = px(0),
  },

  { select           = "mainbox",
    enabled          = true,
    padding          = px(16),
    spacing          = px(12),
    background_color = transparent,
    children         = { "inputbar", "message", "listview", "footer" },
  },

  { select           = "inputbar",
    enabled          = true,
    orientation      = "horizontal",
    padding          = { px(10), px(12) },
    spacing          = px(10),
    background_color = "@surface",
    border_radius    = px(10),
    children         = { "prompt", "entry" },
  },

  { select           = "prompt",
    enabled          = true,
    background_color = transparent,
    text_color       = "@accent-soft",
    vertical_align   = 0.5,
  },

  { select            = "entry",
    enabled           = true,
    background_color  = transparent,
    text_color        = "@foreground",
    cursor            = "text",
    placeholder       = "Search",
    placeholder_color = "@hint",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
  },

  -- Mode errors and `-mesg` text. Kept in the layout so a message never has
  -- nowhere to render; it collapses to nothing when there is none.
  { select           = "message",
    enabled          = true,
    background_color = "@surface",
    border_radius    = px(10),
    padding          = { px(10), px(12) },
  },

  { select           = "textbox",
    background_color = transparent,
    text_color       = "@foreground",
    vertical_align   = 0.5,
    horizontal_align = 0.0,
  },

  { select           = "listview",
    enabled          = true,
    columns          = 1,
    lines            = 12,
    cycle            = true,
    dynamic          = true,
    scrollbar        = true,
    layout           = "vertical",
    reverse          = false,
    fixed_height     = true,
    spacing          = px(4),
    background_color = transparent,
  },

  { select                 = "scrollbar",
    enabled                = true,
    handle_width           = px(4),
    handle_color           = "@accent-soft",
    handle_rounded_corners = true,
    background_color       = "@surface",
    border_radius          = px(4),
  },

  -- Action purpose: the 3px leading border is on EVERY element, transparent
  -- until the row is selected. Adding it only to the selected rule would change
  -- that row's content box and shift its text sideways by 3px each time the
  -- cursor moved.
  { select           = "element",
    enabled          = true,
    orientation      = "horizontal",
    padding          = { px(9), px(12) },
    spacing          = px(10),
    border           = { px(0), px(0), px(0), px(3) },
    border_color     = transparent,
    border_radius    = px(8),
    background_color = transparent,
    text_color       = "@foreground",
    cursor           = "pointer",
  },

  { select           = { "element normal.normal", "element alternate.normal" },
    background_color = transparent,
    text_color       = "@foreground",
  },

  { select     = { "element normal.active", "element alternate.active" },
    text_color = "@accent-strong",
  },

  { select     = { "element normal.urgent", "element alternate.urgent" },
    text_color = "@urgent",
  },

  -- Fill plus marker, not fill alone. The marker is what stays legible while a
  -- repaint is in flight and on a row whose own colour is already close to the
  -- accent.
  { select           = { "element selected.normal", "element selected.active",
                         "element selected.urgent" },
    background_color = "@accent",
    border_color     = "@accent-strong",
    text_color       = "@on-accent",
  },

  { select           = "element-icon",
    background_color = transparent,
    size             = px(22),
    vertical_align   = 0.5,
    cursor           = inherit,
  },

  { select           = "element-text",
    background_color = transparent,
    text_color       = inherit,
    vertical_align   = 0.5,
    horizontal_align = 0.0,
    cursor           = inherit,
  },

  -- Shown matches over total. `num-filtered-rows` and `num-rows` are fixed
  -- widgets that carry a bare number each, so the separator between them is an
  -- ordinary textbox with static content.
  { select  = "footer",
    enabled = true,

    -- Action purpose: load-bearing. An unrecognised widget name becomes a box
    -- packed with expand=TRUE (sofi_view_add_widget), and the listview expands
    -- too -- so without this the two split the panel's height between them and
    -- the list renders at half the window's size with the rest left empty.
    -- Every packed box that is not meant to grow needs this.
    expand = false,

    orientation = "horizontal",
    spacing     = px(4),
    padding     = { px(4), px(4), px(0), px(4) },
    children    = { "num-filtered-rows", "textbox-of", "num-rows" },
  },

  { select           = { "num-filtered-rows", "num-rows", "textbox-of" },
    expand           = false,
    background_color = transparent,
    text_color       = "@foreground-dim",
    vertical_align   = 0.5,
  },

  { select  = "textbox-of",
    content = "/",
  },

}
