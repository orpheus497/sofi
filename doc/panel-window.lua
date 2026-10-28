-- sofi built-in panel: the sofi control panel
--
-- A strip along the bottom of the screen, inset from the edges so it reads as a
-- floating bar. Selected automatically for `sofi -show window`: a row of
-- buttons, one per sofi indexer, each with an icon.
--
-- Two zones: a filter field and the button row. **There is no tray zone and no
-- window list** -- saber owns the persistent tray and the taskbar, and removing
-- both from here is the overlap this project set out to close. See the note at
-- the foot of this file for restoring the tray on a session without saber.
--
-- ---------------------------------------------------------------------------
-- WHY `layout: horizontal`
--
-- `layout: horizontal` selects the listview's BARVIEW renderer, which sizes
-- each element to its own content and scrolls when they overflow -- rather than
-- the ordinary grid, where `flow: horizontal` would divide the width into equal
-- cells instead. A button reading "Applications" and one reading "SSH" should
-- not be given the same width.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

  { configuration = {
      -- The buttons are icon-and-label; this is what draws the icon.
      show_icons = true,
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
    location = south,
    anchor   = south,

    -- Not 100%: an inset strip reads as an object floating over the desktop,
    -- which is what the rounded corners are for. At 100% the side radii would
    -- be off-screen and the bar would read as a slab welded to the edge.
    width    = percent(98),
    x_offset = px(0),

    -- Action purpose: POSITIVE is inward here. This surface covers the whole
    -- usable area to catch outside clicks, so the panel is placed inside it in
    -- software and the offset moves it away from the anchored edge
    -- (wayland/view.c, window_update_size_with_outside_click). The daemon's
    -- banner takes the other path -- layer-shell margins -- where the same
    -- intent is written as a NEGATIVE number. This was -12px and put the
    -- bottom 12px of the strip outside the usable area.
    y_offset = px(12),

    border           = px(0),
    border_radius    = px(14),
    background_color = "@background",
    padding          = { px(8), px(12) },
  },

  -- Horizontal so type-to-filter stays available: dropping the inputbar
  -- entirely would leave state->text NULL and silently disable filtering.
  { select = "mainbox",
    enabled          = true,
    orientation      = horizontal,
    spacing          = px(12),
    background_color = transparent,
    -- No "tray" child. The system tray belongs to saber, and this strip is the
    -- sofi control panel -- see the note at the foot of this file.
    children         = { "inputbar", "listview" },
  },

  { select = "inputbar",
    enabled          = true,
    expand           = false,
    orientation      = horizontal,
    padding          = { px(6), px(10) },
    spacing          = px(8),
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
    placeholder       = "Filter",
    placeholder_color = "@hint",
    cursor_color      = "@accent",
    cursor_width      = px(2),
    vertical_align    = 0.5,
    width             = px(150),
  },

  { select = "listview",
    enabled          = true,
    layout           = horizontal,
    cycle            = true,
    dynamic          = true,
    scrollbar        = false,
    spacing          = px(6),
    background_color = transparent,
  },

  -- See the note in doc/default_theme.lua: the leading border is on every
  -- element, transparent until selected, so selecting a chip does not shift its
  -- contents sideways.
  { select = "element",
    enabled          = true,
    orientation      = horizontal,
    padding          = { px(7), px(12) },
    spacing          = px(9),
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

  -- Kept so a theme has the state available; the control panel's buttons do not
  -- set it. `foreground-dim` rather than `muted`: this is text, and `muted` is
  -- 2.29:1 against the ground.
  { select = { "element normal.urgent", "element alternate.urgent" },
    text_color = "@foreground-dim",
  },

  { select = { "element normal.active", "element alternate.active" },
    text_color = "@accent-strong",
  },

  -- Every state takes the same selection treatment: a row grouped with the muted
  -- normal.urgent rule above would leave the cursor invisible when it landed
  -- there.
  { select = { "element selected.normal", "element selected.active",
               "element selected.urgent" },
    background_color = "@accent",
    border_color     = "@accent-strong",
    text_color       = "@on-accent",
  },

  { select = "element-icon",
    background_color = transparent,
    size             = px(20),
    vertical_align   = 0.5,
    cursor           = inherit,
  },

  { select = "element-text",
    background_color = transparent,
    text_color       = inherit,
    vertical_align   = 0.5,
    horizontal_align = 0.0,
    cursor           = inherit,
  },

}

-- ===================================================================
-- THE TRAY ZONE IS GONE, AND THAT IS DELIBERATE
--
-- This strip carried the system tray in its right-hand corner until 2026-09-09.
-- It no longer does: saber owns the persistent tray, and duplicating it here
-- was the overlap this project set out to remove.
--
-- `sofi -tray-daemon` still exists and still hosts StatusNotifierItem -- it was
-- kept deliberately, for sessions that do not run saber -- but it now has no
-- built-in surface. On such a session, add the widget back:
--
--   { select = "mainbox", children = { "inputbar", "listview", "tray" } },
--
--   { select = "tray",
--       enabled          = true,
--       expand           = false,
--       orientation      = horizontal,
--       spacing          = px(8),
--       padding          = { px(0), px(2) },
--       background_color = transparent,
--   },
--
--   { select = "tray-icon",
--       size             = px(20),
--       padding          = px(2),
--       background_color = transparent,
--       cursor           = pointer,
--   },
--
-- The widget code is untouched; only this layout stopped referring to it. Do
-- not add it back on a session running saber -- exactly one process per bus can
-- own org.kde.StatusNotifierWatcher.
-- ===================================================================
