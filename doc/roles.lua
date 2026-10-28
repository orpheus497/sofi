-- sofi semantic roles -- what each colour is FOR.
--
-- Parsed from the GResource immediately after whichever palette the invocation
-- selected, and immediately before the built-in panel layout. This is the file
-- the layouts refer to; they contain no hex and no `@colorN` at all.
--
-- One file for three palettes. Everything here is a reference, so everything
-- here re-derives from whichever palette is in force -- which is the whole
-- reason the slots are in a separate file. Where a role's right answer differs
-- between palettes it points at a `palette-` name that the palette file sets by
-- measuring itself: `accent`, `on-accent`, `urgent`, `critical` and
-- `foreground-dim`. A link resolves at property lookup, after every parse has
-- finished, so those chains cost nothing and stay overridable.
--
-- See sofi-customisation(5) for the cascade and how to override any of it, or
-- doc/palette.lua in the source tree for why it is built this way. This file
-- and the palettes are installed under `$datadir/sofi/themes/`;
-- doc/palette.lua is not, because it holds no colours -- it is an explainer,
-- and the manual is where an installed system carries that explanation.
--
-- ---------------------------------------------------------------------------
-- MEASURED CONTRAST, PER PALETTE
--
-- WCAG 2.1 relative luminance, ratio = (L_hi + 0.05) / (L_lo + 0.05).
--
-- EACH ROLE AGAINST THE BACKGROUND THE LAYOUTS ACTUALLY PAINT IT ON, which is
-- not always the ground and is the correction these figures carry. Two
-- backgrounds exist under text here. A list row sits on the window ground:
-- `window` is the only opaque box in that chain and `mainbox`, `listview` and
-- `element` are all transparent. Everything inside the input bar sits on
-- `surface`, because `inputbar` paints it while `prompt` and `entry` are
-- transparent. A role painted in both places is recorded at the WORSE of its
-- two figures, because that is the one that decides whether it is legible.
--
-- This matters in one direction only, and it is the unsafe one: `surface` is
-- the ground a quarter-step towards color8, so on a dark palette it is the
-- lighter of the two and light ink reads LOWER against it. Measuring every
-- role against the ground therefore flattered the inputbar roles. `accent-soft`
-- read 4.68:1 that way and 3.88:1 where it is drawn, under the floor.
--
-- 4.5:1 is AA for body copy, 3.0:1 for large or bold. `hint` is held to 3.6:1
-- for the reason its own note below gives.
--
--   role                   bg        Sakura   S.Dark   S.Light
--   foreground             both       8.73     6.38      5.65
--   foreground-bright      ground    13.41    11.06     11.44
--   foreground-dim         both       5.40     4.95      5.07
--   accent                 surface    5.38     2.39 *    5.17
--   on-accent              accent     6.49     4.53      4.97
--   accent-soft            both       4.61     4.65      4.64
--   accent-strong          ground     7.96     5.34      5.08
--   urgent                 ground     5.89     6.84      5.61
--   critical (fill)        ground     5.89     6.84      5.61
--   warning                ground    10.60     5.32      5.02
--   hint (placeholder)     surface    3.64     3.60      3.82
--   muted (non-text)       ground     2.29     1.34      1.16
--   surface (non-text)     ground     1.21     1.06      1.04
--   surface-alt (non-text) ground     1.49     1.14      1.08
--
-- `*` marks a text-bearing role below its floor on that palette. Two things
-- emptied that column, and they are worth keeping apart.
--
-- MEASURING A ROLE RATHER THAN NAMING A SLOT did most of it. Before the split a
-- positional map put `on-accent` at 2.54:1 and 3.32:1, `urgent` at 2.94:1 and
-- 2.43:1, and `foreground-dim` at 2.33:1 and 1.75:1 on the two new palettes.
--
-- DERIVING THE TONE did the rest, and the figures above are the derived tones
-- and not the slots they come from. The raw slot measurements, which is what an
-- earlier version of this table recorded, are PRE-DERIVATION context and
-- nothing more: on Sakura Dark `accent-soft`'s color4 reads 1.67:1, and
-- `accent-strong`'s color13 and `warning`'s color11 read 2.73:1 and 2.75:1. The
-- shipped roles are 4.65:1, 5.34:1 and 5.32:1, because each is carried up until
-- it clears. A table that printed the slot figure as the role's contrast was
-- reporting a shortfall that the palette files had already resolved.
--
-- `critical` moved as well, and that move is not free: it named color1 and now
-- resolves to the urgency literal, so its ratio reads 4.07:1 -> 5.89:1 on
-- Sakura, 1.98:1 -> 6.84:1 on Sakura Dark and 3.58:1 -> 5.61:1 on Sakura
-- Light. The gain on the default is a changed colour, #c96464 -> #df8787, so
-- the default's critical stripe is lighter than it was. The role's own note
-- below records why.
--
-- What remains, recorded rather than worked around:
--
--   - `accent` on Sakura Dark, R-104, and it is now the ONLY text-bearing
--     shortfall left on any palette. Every candidate in that palette measures
--     2.4-2.8:1 against its own ground; there is no rescue to pick and
--     inventing one was ruled against. So `accent` is for fills and marks
--     there, never for text. No shipped layout paints text in it any more:
--     doc/panel-display.lua did, and that pair read 2.39:1 against the surface
--     it was drawn on, so it now takes `accent-soft` like the other four panels
--     -- 4.65:1 on the same palette, and a neighbouring tone, so the styling is
--     unchanged to look at.
--   - On Sakura Light `accent` and `accent-soft` resolve to the same slot,
--     because color4 is both the better accent and the soft accent's own slot.
--     A selection and a prompt stripe are the same colour on that palette.
--   - `urgent` and `critical` are one value on every palette now, so a stripe
--     and the text it frames cannot differ in tone. The notification history's
--     urgent row drew the two in two reds, color1 around color9, and draws one
--     from here on. Nothing becomes unreadable -- the stripe gains contrast on
--     all three palettes -- but that row's two-tone reading is gone, and
--     getting it back needs a second colour in that layout, not a change here.

return {

  { select = "*",
    -- Body text. Not color15 -- holding the brightest tone back is what gives
    -- a list somewhere to escalate to.
    foreground        = "@color7",

    -- Emphasis: a selected row's label, a live notification's summary.
    foreground_bright = "@color15",

    -- Secondary metadata -- app names, timestamps, window classes, counts. It
    -- recedes by being cooler and smaller, not by being too faint to read,
    -- which is why it is a tone each palette derives against its own ground
    -- rather than a slot.
    foreground_dim    = "@palette-dim",

    -- NON-TEXT ONLY -- separators, scrollbar troughs, disabled fills, the empty
    -- half of a meter. Putting a label in this colour is the one way to make
    -- any of these palettes unreadable; use `hint` or `foreground-dim`.
    muted             = "@color8",

    -- The same ground as `background` with no transparency, for the
    -- notification cards: a banner lands over arbitrary content and must not
    -- have that content showing through the text.
    background_solid  = "@color0",

    -- Selection. Whichever of color12 and color4 the palette measured better
    -- against its own ground, so a highlighted row stays readable when the
    -- bright half of the scheme is the half nearest the paper.
    accent            = "@palette-accent",

    -- Prompts, leading stripes, hover. AA for large or bold text and for the
    -- non-text marks it is mostly used as, but not for body copy on any
    -- palette -- Sakura's own 4.31:1 has always been that compromise, and it
    -- is the reference the other two aim at. Derived per palette: Sakura Dark's
    -- color4 reads 1.67:1, the worst figure in that palette, and on Sakura
    -- Light color4 is what `accent` measured best, so keeping both on the slot
    -- would paint a selection and a prompt stripe the same colour.
    accent_soft       = "@palette-soft",

    -- The current sheet, a still-live notification. Derived per palette:
    -- color13 carries it at 7.96:1 on Sakura and collapses to 2.73:1 and
    -- 2.12:1 on the other two, so those move the slot's lightness toward white
    -- or black -- holding its hue -- rather than keeping a sheet marker nobody
    -- can see.
    accent_strong     = "@palette-strong",

    -- Text drawn ON an accent fill. Whichever of color0 and color15 the palette
    -- measured better against the accent it resolved -- the single most
    -- important pair in the file, and the one a slot index gets wrong the
    -- moment the text pair inverts.
    on_accent         = "@palette-on-accent",

    -- Critical urgency as text. A literal red held outside the palette and
    -- chosen by ground luminance, R-103: no one red clears AA against both the
    -- darkest and the lightest ground this desktop ships.
    urgent            = "@palette-urgent",

    -- Critical urgency as a fill or a stripe. Derived per palette rather than
    -- sharing `urgent`'s literal outright, because the two roles are meant to
    -- differ -- the fill darker than the text beside it -- and only two of the
    -- three palettes need rescuing. Sakura keeps its own color1 at 4.07:1, so
    -- that distinction survives where it always worked. Sakura Dark's color1
    -- is red but reads 1.98:1, and Sakura Light has no red at all -- its
    -- color1 is a muted emerald, which would draw a critical stripe in the
    -- colour of success -- so both take the urgency literal for their ground.
    -- Contrast is the problem on one and hue on the other.
    critical          = "@palette-critical",

    -- Unread counts, truncation markers, anything that wants attention without
    -- claiming to be an error. Derived per palette for the same reason as
    -- `accent-strong`: color11 reads 10.60:1 on Sakura and 2.75:1 and 2.74:1
    -- on the other two.
    warning           = "@palette-warning",
  },

}
