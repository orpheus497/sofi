-- sofi palette: Sakura Dark -- ash black, muted berry and plum.
--
-- Sixteen positional slots, this palette's own five derived tones, and the four
-- values the role map cannot derive from a slot index. The semantic names live
-- in /org/sofi/roles.lua, parsed immediately after this file. See
-- sofi-customisation(5) for the cascade, or doc/palette.lua in the source
-- tree for why it is built this way.
--
-- Selected by `-palette sakura-dark`.
--
-- The slots are byte-identical to hikari-sakura's Sakura Dark block. This is
-- not a darker Sakura -- it is a different palette that happens to occupy the
-- same sixteen positions. Its 8-15 band is mid-tone rather than bright, which
-- is why the accent family cannot be rescued here; see the known limit below.

return {

  -- ===================================================================
  -- POSITIONAL SLOTS
  -- ===================================================================
  { select = "*",
    color0  = "#161618",  -- base (ash black)
    color1  = "#802b2b",  -- red (muted dark red)
    color2  = "#8c5338",  -- orange (muted)
    color3  = "#782639",  -- was yellow -> berry red
    color4  = "#63255a",  -- was violet -> plum
    color5  = "#70264b",  -- was mauve -> dark rose
    color6  = "#525257",  -- grey
    color7  = "#9e9ea3",  -- text

    color8  = "#2e2e32",  -- bright base (charcoal)
    color9  = "#a34242",  -- bright red
    color10 = "#ab6c4e",  -- bright orange
    color11 = "#9e3b52",  -- bright berry red
    color12 = "#873b7d",  -- bright plum
    color13 = "#963d6b",  -- bright dark rose
    color14 = "#6f6f75",  -- bright grey
    color15 = "#cacace",  -- bright text
  },

  -- ===================================================================
  -- DERIVED TONES
  --
  -- Recomputed for this palette by the same five rules Sakura's file states. The
  -- format has no blend and no alpha operator for a `@colorN` reference, so each
  -- is a literal.
  --
  -- Ground relative luminance 0.0081. A dark palette, and the darkest of the
  -- three.
  -- ===================================================================
  { select = "*",
    -- color0 at 90%. Identical to hikari's `bar` for this palette.
    background  = "#161618e6",

    -- color0 blended 25% toward color8. 1.06:1 -- a recess. Slighter than
    -- Sakura's because this palette's bright base is nearly its base.
    surface     = "#1c1c1e",

    -- The same blend at 50%. 1.14:1.
    surface_alt = "#222225",

    -- color8 at 60%. Hairline separators.
    border_soft = "#2e2e3299",

    -- color6's lightness moved 19% toward white. 3.60:1 against the
    -- `surface` a placeholder is drawn on, 3.83:1 against the ground.
    --
    -- NOT the ground-blended-toward-color6 rule the Sakura file states, and the
    -- reason is that the rule does not survive this palette: Sakura's color6 is
    -- a light grey, this one's is a dark grey two steps off its own ground, and
    -- the same blend lands on #47474b, 1.84:1 against that same surface -- a
    -- placeholder nobody can read. What the
    -- rule is actually for is a fixed *recession*, 3.64:1 on Sakura, so this
    -- targets that figure instead of the arithmetic that happens to produce it
    -- there, and reaches it within 0.05. Moved toward white rather than toward
    -- color7 so the tone stays neutral at hue 240 instead of being greyed out
    -- by a blend toward the text colour.
    hint        = "#737377",
  },

  -- ===================================================================
  -- WHAT THE ROLE MAP CANNOT DERIVE
  --
  -- Measured against this palette, not inherited from Sakura. See the Sakura
  -- file for why these four exist and how the cascade keeps them overridable.
  -- ===================================================================
  { select = "*",
    -- Selection. The better of color12 and color4 -- 2.54:1 against 1.67:1.
    --
    -- KNOWN LIMIT, R-104. 2.54:1 is below AA and no candidate in this palette
    -- clears it: every one measures 2.5-2.8:1 against this ground, because the
    -- 8-15 band is mid-tone. The shortfall is accepted and recorded rather
    -- than worked around, so the accent stays the colour the compositor
    -- marks a focused window with. Use `accent` for fills and marks here, and
    -- `foreground` or `foreground-bright` for anything that must be read.
    palette_accent    = "@color12",

    -- Text drawn ON that accent fill, and the one role here that both picks a
    -- slot and then moves it. The pick comes first: the better of color0 and
    -- color15 against the fill is 4.35:1 against 2.54:1, so the bright text
    -- slot rather than the ground, and that alone is 1.7x better than the slot
    -- a positional map would have named.
    --
    -- 4.35:1 is still under the 4.5:1 a selected row's label needs, and the
    -- shortfall is the fill being as light as it is rather than the ink being
    -- too dark -- which is why the answer is a small move and not a different
    -- slot. Lifted 7% toward white for 4.53:1: #cacace to #ceced1, four steps
    -- per channel, a change nobody would see as a change. The accent itself
    -- cannot be rescued that way and is not (R-104); its ink can be, and is.
    palette_on_accent = "#ceced1",

    -- Urgency, as text and as a fill. The dark-ground literal from R-103, the
    -- same value Sakura uses: 6.84:1 here, the best reading of the three. A
    -- positional map would have resolved this to color9 at 2.94:1.
    palette_urgent    = "#df8787",

    -- Secondary metadata. color0 blended 85% toward color7, at 4.95:1
    -- against `surface` and 5.25:1 against the ground -- the
    -- dimmest 5% step that clears AA with a step of margin. color6 measures
    -- 2.33:1 here and cannot carry text.
    palette_dim       = "#8a8a8e",

    -- Critical urgency as a fill. This palette's color1 is red but reads at
    -- only 1.98:1 against its own ground, so it takes the dark-ground urgency
    -- literal at 6.84:1 rather than a stripe nobody can see.
    palette_critical  = "#df8787",

    -- Prompts, leading stripes, hover. MEASURED AGAINST `surface`, not against
    -- the ground: the shipped layouts give `prompt` a transparent background
    -- inside an `inputbar` painted `@surface`, so the ground is never behind
    -- this text. Here the two are close -- #1c1c1e against #161618 -- but the
    -- surface is still the lighter and so the less forgiving of the two.
    --
    -- color4 is #63255a, 1.57:1 against that surface and the worst figure in
    -- this palette, so its lightness is moved 39% toward white for 4.65:1. Hue
    -- held at 309 degrees; saturation falls 63% to 24%, which is what carrying
    -- a plum that far up a near-black ground costs.
    --
    -- Body AA and not the mark floor, because that `prompt { text-color }` is
    -- normal weight at the body size, so 4.5:1 applies and 3:1 does not. The
    -- scrollbar handle takes the same role over a `@muted` track and IS a
    -- graphical object at 3:1; here that pair reads 3.70:1, so this value
    -- carries both uses.
    palette_soft      = "#a07a9a",

    -- The current sheet, a still-live notification. color13 is #963d6b at
    -- 2.73:1 here, so its lightness is moved 31% toward white: 5.03:1
    -- against `surface` and 5.34:1 against the ground, with
    -- the hue held at 330 degrees and saturation falling 59% to 35%, which is
    -- what lightening a mid-tone on a near-black ground costs. Moved toward
    -- white rather than toward color7, which would have reached the same ratio
    -- by greying the rose out of it entirely.
    palette_strong    = "#b77999",

    -- Unread counts and truncation markers. color11 is #9e3b52 at 2.75:1, so
    -- the same treatment: 31% toward white for 5.01:1 against `surface` and
    -- 5.32:1 against the ground, hue held at 346.
    palette_warning   = "#bc7888",

  },

}
