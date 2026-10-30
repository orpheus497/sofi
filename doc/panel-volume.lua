-- sofi built-in panel: volume
--
-- A compact pane in the top-right corner, under hikari-sakura's bar. Selected
-- automatically when sofi runs in volume mode; no user config required.
--
-- ---------------------------------------------------------------------------
-- WHY THE TOP-RIGHT CORNER
--
-- hikari-sakura's own top bar carries the volume readout, on the right. This
-- pane drops from underneath it, so the number you were looking at and the
-- control you summoned are in the same place. The other corners are taken:
-- notifications stack bottom-right, the application menu rises bottom-centre,
-- the sheet row sits top-centre, and saber occupies the left edge permanently.
--
-- `north east` needs no bar compensation. sofi's idea of the screen on Wayland
-- is whatever the layer-shell `configure` reported, and the compositor answers
-- `set_size(0,0)` with its USABLE area, which already has the bar's rows
-- subtracted (hikari/src/layer_shell.c). The offsets below are a deliberate
-- gap, not clearance -- do not add the bar's height to them.
--
-- ---------------------------------------------------------------------------
-- WHY THERE IS NO FILTER FIELD
--
-- A machine has two or three audio sinks. There is nothing to search, and a
-- text field would take the arrow keys this pane needs for the level. The same
-- reasoning as the sheet switcher, which is also a fixed short list.
--
-- Colours come from /org/sofi/palette.lua, parsed immediately before this
-- file. There are deliberately no colour values here.

return {

  { configuration = {
      show_icons = false,

      -- -----------------------------------------------------------------------
      -- THE BINDINGS ARE THE POINT OF THIS BLOCK
      --
      -- `kb-custom-N` defaults to `Alt+N`. For a volume pane that means the only
      -- discoverable action is Enter, and the two verbs a volume pane exists for
      -- -- louder and quieter -- sit behind a chord nobody guesses. So they are
      -- moved to the arrow keys, which is where every other volume control in
      -- existence puts them.
      --
      -- Left and Right are bound by default to `kb-move-char-back` and
      -- `kb-move-char-forward`, the text cursor of the filter field. A binding
      -- claimed twice is refused, so those two have to give the keys up first --
      -- which costs nothing here, because this pane has no filter field for a
      -- cursor to move in. Their Control+b / Control+f bindings are kept, so
      -- nothing is actually lost.
      --
      -- The Alt+N originals are kept alongside the new keys rather than replaced:
      -- a user who learned them elsewhere in sofi keeps them here.
      kb_move_char_back    = "Control+b",
      kb_move_char_forward = "Control+f",

      kb_custom_2 = "Left,Alt+2,KP_Subtract",
      kb_custom_3 = "Right,Alt+3,KP_Add",
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
    location = "north_east",
    anchor   = "north_east",

    -- Sized for the widest row this pane can draw: twenty bar cells, a
    -- three-digit percentage, and a sink description. Raise it if your sinks
    -- have long descriptions and they ellipsize.
    width    = px(460),

    -- The deliberate gap from the corner. Positive is inward on this path --
    -- the surface covers the usable area to catch outside clicks and the panel
    -- is placed inside it in software.
    x_offset = px(-12),
    y_offset = px(8),

    border           = px(0),
    border_radius    = px(12),
    background_color = "@background",
    padding          = px(10),
  },

  { select = "mainbox",
    enabled          = true,
    background_color = transparent,
    spacing          = px(8),

    -- The message carries the key hints and the backend name, and it is the
    -- only thing telling the user the arrow keys do anything -- so it is not
    -- optional decoration here the way it is in a launcher. Below the list, so
    -- the sinks are what the eye meets first.
    children         = { "listview", "message" },
  },

  { select = "listview",
    enabled = true,
    columns = 1,

    -- Enough for a laptop's built-in sink plus HDMI, a dock and a headset,
    -- without reserving a tall empty pane on a machine with one card.
    -- `dynamic` is what makes it shrink to the number actually present.
    lines        = 5,
    dynamic      = true,
    fixed_height = false,

    cycle            = true,
    scrollbar        = false,
    spacing          = px(2),
    background_color = transparent,
  },

  -- See the note in doc/default_theme.lua: the leading border is on every row,
  -- transparent until selected, so selecting one does not shift its text.
  { select = "element",
    enabled          = true,
    orientation      = "horizontal",
    padding          = { px(8), px(10) },
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

  -- A muted sink. `foreground-dim`, not `muted`: this is text, and the `muted`
  -- palette role is 2.29:1 against the ground. The row's own markup already
  -- dims the bar further, so the two compose rather than repeat.
  { select = { "element normal.urgent", "element alternate.urgent" },
    background_color = transparent,
    text_color       = "@foreground-dim",
  },

  -- The sink the session is actually using. Filled rather than merely tinted, so
  -- "where is the sound going" is answerable without reading the labels.
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
