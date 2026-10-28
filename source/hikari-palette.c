/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2026 orpheus497
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/**
 * @file
 * @brief Imports the compositor's palette from hikari-sakura's Lua
 * configuration.
 *
 * Script function and purpose: reads hikari's sixteen palette slots and renders
 * them as a sasi fragment, so that editing the compositor's theme retints
 * sofi's panels instead of leaving them on the compiled-in default. sofi already
 * speaks to hikari's control socket for the sheet list; this is the other half,
 * and it is the half saber has had all along.
 *
 * The four contrast helpers -- relative luminance, the ratio, the better-of pick
 * and the blend -- are ported from saber's `src/theme.c`, same author and the
 * same MIT licence, and are kept deliberately identical: the two programs have
 * to resolve the same role to the same colour from the same sixteen slots, or a
 * panel and the column beside it disagree about what "selected" looks like.
 * saber's `src/sni.c` records the reciprocal arrangement in the other
 * direction.
 */

/** The log domain of this file. */
#define G_LOG_DOMAIN "HikariPalette"

#include "config.h"

#include <math.h>
#include <string.h>

#include <glib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "hikari-lua.h"
#include "hikari-palette.h"

/** Slots in a palette. The conventional terminal arrangement, 0-7 normal and
 * 8-15 bright. */
#define SLOTS 16

/* Action purpose: urgency is a literal red held outside the palette, and there
 * are two of them because there cannot be one: per R-103 the band of reds that
 * stays legible against the darkest ground this desktop ships does not overlap
 * the band that stays legible against the lightest, so a single value is a
 * choice of which theme to fail on. The dark pick is byte-identical to the
 * shipped palette's bright red, which is what urgency already resolved to
 * there. Both stay overridable by name in ~/.config/sofi/config.sasi. */
#define URGENT_ON_DARK "#df8787"
#define URGENT_ON_LIGHT "#9b2226"

/* A ground at or above this relative luminance is light. It sits far from every
 * palette the desktop ships -- the two dark grounds measure 0.008 and 0.018 and
 * the light one 0.694 -- so an unfamiliar palette is not being judged on a
 * knife edge either. */
#define LIGHT_GROUND 0.18

/** AA for body copy. */
#define AA_BODY 4.5

/** AA for a graphical object -- a stripe, a fill, a mark carrying no text. */
#define AA_MARK 3.0

/* Action purpose: what each derived role aims at, above its floor rather than
 * on it, so rounding and a later palette edit have somewhere to go.
 *
 * `hint` is placeholder text and is deliberately NOT held to body AA: it has to
 * read as a hint rather than as a label, and the shipped Sakura palette sets
 * that recession at 3.66:1. What the other palettes hold is that figure, not
 * the arithmetic that happens to produce it there -- their grey slot sits close
 * to their own ground, so the blend Sakura uses lands at 1.75:1 and 1.45:1.
 *
 * `soft` is held to body AA and not to the mark floor, because the shipped
 * layouts paint `prompt { text-color }` in it -- normal weight at the body
 * size, so WCAG 1.4.3 wants 4.5:1 and the 3:1 of 1.4.11 does not apply. It
 * aims lower than the accent family because on a light ground both it and
 * `strong` darken out of the same blue, and at 5.0:1 the two land on the same
 * literal: a prompt and the current sheet in one colour, which is the
 * collision this role exists to avoid one role over. 4.6:1 keeps them apart
 * and clears the floor; the margin only has to absorb rounding, since unlike
 * the accent family this role starts within a step of its floor on the palette
 * that sets the reference. */
#define HINT_TARGET 3.6
#define ACCENT_TARGET 5.0
#define SOFT_TARGET 4.6

/* How far from pure red a hue may sit and still be read as one, in degrees
 * either side. Sakura's red slot is at 0, Sakura Dark's also at 0, and Sakura
 * Light's "red" slot is a muted emerald at 139 -- which is the case this
 * exists to catch. */
#define RED_ARC 40.0

/** The step the dim tone is searched on, as a percentage of the distance from
 * the ground to the text slot. */
#define DIM_STEP 5

/** Ceiling on the work a hikari configuration may do before it is abandoned.
 * The sandbox bounds what the file can reach, not how long it may run, and sofi
 * is the launcher: a configuration that never finishes would cost the user
 * every menu rather than one colour. Kept equal to the limit source/anim.c
 * refuses the same file with, so the two readers of it agree about whether it is
 * usable. */
#define STEP_LIMIT 200000

/** A slot. Channels are 0-255 so that an exact half lands on round-half-to-even
 * rather than on a floating point artefact, which is what reproduces the
 * literals the shipped palettes carry. Alpha is -1 when the configuration gave
 * none. */
typedef struct {
  int r, g, b, a;
} Slot;

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/**
 * Function purpose: accepts the three hex spellings hikari's own parser does, so
 * a configuration that works there is not rejected here over a shorthand.
 *
 * @returns FALSE when the spec is not a colour, which is a reason to decline the
 * whole import rather than to guess.
 */
static gboolean slot_parse(const char *spec, Slot *out) {
  if (spec == NULL || spec[0] != '#') {
    return FALSE;
  }

  const char *digits = spec + 1;
  size_t len = strlen(digits);

  if (len != 3 && len != 6 && len != 8) {
    return FALSE;
  }

  int nibble[8];
  for (size_t i = 0; i < len; i++) {
    nibble[i] = hex_value(digits[i]);
    if (nibble[i] < 0) {
      return FALSE;
    }
  }

  /* Action purpose: "#rgb" is the same colour as "#rrggbb", so each nibble is
   * doubled (n * 17) rather than merely shifted, which would darken every value
   * by up to one part in sixteen. */
  if (len == 3) {
    out->r = nibble[0] * 17;
    out->g = nibble[1] * 17;
    out->b = nibble[2] * 17;
    out->a = -1;
    return TRUE;
  }

  out->r = nibble[0] * 16 + nibble[1];
  out->g = nibble[2] * 16 + nibble[3];
  out->b = nibble[4] * 16 + nibble[5];
  out->a = len == 8 ? nibble[6] * 16 + nibble[7] : -1;
  return TRUE;
}

static double channel_linear(int v) {
  double c = v / 255.0;
  return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

/**
 * Function purpose: WCAG 2.1 relative luminance. The role map turns on it three
 * times over -- the light/dark discriminator, the accent pick and the on-accent
 * pick are all settled by measuring the palette in hand rather than by naming a
 * slot that happened to suit one theme.
 */
static double relative_luminance(const Slot *s) {
  return 0.2126 * channel_linear(s->r) + 0.7152 * channel_linear(s->g) +
         0.0722 * channel_linear(s->b);
}

/**
 * Function purpose: WCAG contrast ratio, 1.0 to 21.0. Alpha is deliberately
 * ignored: every pair the role map compares is opaque at the point it compares
 * them, and a panel's own translucency is applied afterwards.
 */
static double contrast_ratio(const Slot *a, const Slot *b) {
  double la = relative_luminance(a);
  double lb = relative_luminance(b);
  return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

/**
 * Function purpose: whichever candidate reads better against `ground`. Three
 * lines here remove a whole class of defect permanently -- a role picked by
 * measurement cannot be legible on the palette it was chosen for and invisible
 * on the next one. `first` wins a tie, so the pick is stable.
 *
 * @returns the slot index of the winner.
 */
static int better_against(const Slot *ground, const Slot palette[SLOTS],
                          int first, int second) {
  return contrast_ratio(ground, &palette[second]) >
                 contrast_ratio(ground, &palette[first])
             ? second
             : first;
}

/**
 * Function purpose: `base` moved `mix` of the way towards `towards`. A derived
 * tone is a tint of the surface it sits on, so the result is opaque and the
 * alpha the tone wants is appended by the caller that wants one.
 */
static Slot mix_towards(const Slot *base, const Slot *towards, double mix) {
  Slot out;
  out.r = (int)nearbyint(base->r + (towards->r - base->r) * mix);
  out.g = (int)nearbyint(base->g + (towards->g - base->g) * mix);
  out.b = (int)nearbyint(base->b + (towards->b - base->b) * mix);
  out.a = -1;
  return out;
}

/**
 * Function purpose: `slot` with its lightness moved far enough to clear
 * `target` against `ground`, towards white on a dark ground and towards black
 * on a light one. Returns `slot` untouched when it already clears, so a palette
 * whose own slot carries the role keeps it.
 *
 * Towards the extreme rather than towards the text slot, which is the obvious
 * alternative and the wrong one: a blend towards a near-grey text colour
 * reaches the same ratio by draining the colour out of the tone, so a warning
 * arrives as slate and a sheet marker as putty. Moving lightness holds the hue
 * to within a degree or two and costs saturation only on a dark ground, where
 * lightening anything must.
 */
/**
 * Function purpose: the lower of a tone's two ratios -- against the window
 * ground and against `surface`. Which of the two a role is painted on is a
 * property of the layouts and not of the role: list rows sit on the ground
 * (`window` is the only opaque ancestor in the chain, every box between is
 * transparent), while everything inside the input bar sits on `surface`,
 * because `inputbar` paints it and `prompt` and `entry` are transparent. Three
 * roles are painted on both. So the figure that decides whether a tone is
 * legible is the worse of the pair, never either alone.
 *
 * `surface` is the ground a quarter-step towards color8. On a dark palette that
 * makes it the lighter of the two, so a light tone reads LOWER against it and
 * the ground is the flattering measurement -- the error that measuring only the
 * ground produces is in the unsafe direction, which is why this is the worse of
 * the two rather than an average or a choice.
 */
static double worst_ratio(const Slot *ground, const Slot *surface,
                          const Slot *slot) {
  double on_ground = contrast_ratio(ground, slot);
  double on_surface = contrast_ratio(surface, slot);

  return on_ground < on_surface ? on_ground : on_surface;
}

static Slot lift_to(const Slot *ground, const Slot *surface, const Slot *slot,
                    double target) {
  if (worst_ratio(ground, surface, slot) >= target) {
    return *slot;
  }

  static const Slot white = {255, 255, 255, -1};
  static const Slot black = {0, 0, 0, -1};
  const Slot *towards =
      relative_luminance(ground) >= LIGHT_GROUND ? &black : &white;

  for (int step = 1; step <= 100; step++) {
    Slot out = mix_towards(slot, towards, step / 100.0);

    if (worst_ratio(ground, surface, &out) >= target) {
      return out;
    }
  }

  return *towards;
}

/**
 * Function purpose: a tone that sits AT `target` against `ground` rather than
 * merely above it, walking out from the ground towards `slot` and stopping at
 * the first step that reaches it.
 *
 * This is what a recession needs and what lift_to() cannot give it. lift_to()
 * only ever raises contrast, so a grey slot that already clears the target by a
 * wide margin comes back untouched -- and on the shipped Sakura palette that is
 * exactly the case: color6 measures 5.97:1, brighter than `foreground-dim` at
 * 5.26:1, which would put the placeholder in front of the secondary text it is
 * supposed to sit behind. Walking out from the ground instead caps it at the
 * target from below.
 *
 * Falls back to lift_to() when even the slot itself cannot reach the target,
 * which is the opposite failure and the one the other two palettes have: their
 * grey sits two steps off their own ground, so the whole walk lands under the
 * target and the tone has to be carried past the slot instead. One rule, both
 * directions; see HINT_TARGET for why the target is what it is.
 */
static Slot settle_to(const Slot *ground, const Slot *surface,
                      const Slot *slot, double target) {
  for (int step = 1; step <= 100; step++) {
    Slot out = mix_towards(ground, slot, step / 100.0);

    if (worst_ratio(ground, surface, &out) >= target) {
      return out;
    }
  }

  return lift_to(ground, surface, slot, target);
}

/**
 * Function purpose: emit a role as `@colorN` when the derivation left the slot
 * untouched, and as a literal only when it did not.
 *
 * Action purpose: a reference keeps the override chain intact. Redefining
 * `color13` in ~/.config/sofi/config.sasi reaches `accent-strong` through
 * `@color13` -> `palette-strong` -> `accent-strong`, resolved at property
 * lookup after every parse has finished; a literal ends that chain and freezes
 * the role at whatever the imported palette measured. So the rule is the one
 * the accent pair and `critical` already follow: name the slot wherever naming
 * it is still correct, and spend a literal only where the slot genuinely cannot
 * carry the role.
 */
static void append_role(GString *out, const char *name, int slot,
                        const Slot *source, const Slot *derived) {
  if (source->r == derived->r && source->g == derived->g &&
      source->b == derived->b) {
    g_string_append_printf(out, "    [\"%s\"] = \"@color%d\",\n", name, slot);
    return;
  }

  g_string_append_printf(out, "    [\"%s\"] = \"#%02x%02x%02x\",\n", name,
                         derived->r, derived->g, derived->b);
}

/**
 * Function purpose: whether a slot reads as red, which decides whether a
 * palette can carry critical urgency in its own colours or has to borrow the
 * literal. Contrast alone is not enough to answer it: Sakura Light's red slot
 * clears the mark floor comfortably and is green, so a stripe drawn in it would
 * announce a failure in the colour of success.
 */
static gboolean slot_is_red(const Slot *s) {
  int max = MAX(s->r, MAX(s->g, s->b));
  int min = MIN(s->r, MIN(s->g, s->b));
  double chroma = max - min;

  if (chroma <= 0.0) {
    return FALSE; /* grey has no hue to judge */
  }

  double hue;

  if (max == s->r) {
    hue = fmod(60.0 * ((s->g - s->b) / chroma) + 360.0, 360.0);
  } else if (max == s->g) {
    hue = fmod(60.0 * ((s->b - s->r) / chroma) + 120.0, 360.0);
  } else {
    hue = fmod(60.0 * ((s->r - s->g) / chroma) + 240.0, 360.0);
  }

  return hue <= RED_ARC || hue >= 360.0 - RED_ARC;
}

/**
 * Function purpose: the dimmest tone on the line from the ground to the text
 * slot that still clears AA, plus one step of margin. Secondary metadata has to
 * recede without becoming unreadable, and which tone does that is a property of
 * the palette rather than of a slot index -- hikari's own grey slot measures
 * 5.97:1 on one shipped palette and 1.75:1 on another.
 *
 * @returns the fraction of the way from the ground to the text slot, or 1.0 if
 * even the text slot itself does not clear AA, which leaves `foreground-dim`
 * equal to `foreground` rather than illegible.
 */
static double dim_fraction(const Slot *ground, const Slot *surface,
                           const Slot *text) {
  for (int step = DIM_STEP; step <= 100; step += DIM_STEP) {
    Slot candidate = mix_towards(ground, text, step / 100.0);
    if (worst_ratio(ground, surface, &candidate) >= AA_BODY) {
      int with_margin = step + DIM_STEP;
      return (with_margin > 100 ? 100 : with_margin) / 100.0;
    }
  }
  return 1.0;
}

/**
 * Function purpose: the environment the configuration is evaluated in. A menu
 * program has no business running processes out of a config file, so `io`, `os`,
 * `package`, `require` and `load` are simply not reachable: the chunk's `_ENV`
 * is this table and nothing else, and the real globals table is not in it.
 * What is left is enough to compute a colour and nothing more.
 *
 * Leaves the sandbox on the top of the stack.
 */
static void push_sandbox(lua_State *L) {
  static const char *const base[] = {"pairs",    "ipairs", "type",  "tonumber",
                                     "tostring", "assert", "error", NULL};

  /* Action purpose: the base library has to be opened to be borrowed from, and
   * it is opened into the real globals table on purpose -- that table is never
   * handed to the chunk, so everything it installs that is not copied across
   * below is out of reach. */
  luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
  lua_pop(L, 1);

  lua_newtable(L);

  for (int i = 0; base[i] != NULL; i++) {
    lua_getglobal(L, base[i]);
    lua_setfield(L, -2, base[i]);
  }

  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 0);
  lua_setfield(L, -2, "math");

  /* Action purpose: `string` is the bounded table rather than the real library,
   * because two of the real library's functions loop inside a single C call for
   * longer than the step hook below can interrupt -- the hook counts VM
   * instructions and a C call is one of them. The call also repoints the string
   * metatable, which is what closes the method-call form; see
   * source/hikari-lua.c, which the other reader of this same file shares. */
  sofi_hikari_lua_push_bounded_string(L);
  lua_setfield(L, -2, "string");

  /* Action purpose: the bounded table library, for the reason the bounded
   * string library is here -- table.move walks a span of bare indices in a
   * C-side loop the instruction budget cannot interrupt. */
  sofi_hikari_lua_push_bounded_table(L);
  lua_setfield(L, -2, "table");

  /* Action purpose: hikari's own reader provides `env`, and the hikari.lua it
   * ships reads env.TERMINAL. Leaving it out does not cost that one value, it
   * costs the file: indexing an absent global raises, the chunk ends there, and
   * the palette is never reached -- so sofi would fall back to its built-in
   * colours against the very file it exists to follow. */
  sofi_hikari_lua_push_env(L);
  lua_setfield(L, -2, "env");
}

/* Function purpose: push_sandbox() as something lua_pcall() can run. Every step
 * of building the sandbox allocates, an allocation the ceiling refuses raises
 * LUA_ERRMEM, and a raise with no protected call under it reaches Lua's panic
 * function and abort()s -- which would turn a configuration this file is meant
 * to decline into SIGABRT during sofi's startup. The sandbox is this call's one
 * result, so it lands exactly where the direct call left it. */
static int push_sandbox_protected(lua_State *L) {
  push_sandbox(L);

  return 1;
}

/* Action purpose: the other half of the restriction. Taking `io` and `os` away
 * stops the file reaching anything; only a step hook stops it looping forever,
 * and luaL_error from inside the hook unwinds into the pcall running it, so a
 * runaway file is reported by the same branch as any other evaluation failure
 * and the built-in palette stands. */
static void step_hook(lua_State *L, G_GNUC_UNUSED lua_Debug *activation) {
  luaL_error(L, "configuration did not finish");
}

/**
 * Function purpose: hikari's Lua shape is still being designed, so the palette
 * is looked for where it is most likely to be and then where it might also be,
 * rather than demanded at one path. Leaves the palette table on the stack when
 * it finds one.
 *
 * @returns TRUE when a table was pushed.
 */
static gboolean push_palette_table(lua_State *L) {
  if (!lua_istable(L, -1)) {
    g_debug("hikari configuration did not return a table; ignoring it.");
    return FALSE;
  }

  if (lua_getfield(L, -1, "ui") == LUA_TTABLE) {
    if (lua_getfield(L, -1, "palette") == LUA_TTABLE) {
      return TRUE;
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  if (lua_getfield(L, -1, "palette") == LUA_TTABLE) {
    return TRUE;
  }
  lua_pop(L, 1);

  g_debug("hikari configuration carries no ui.palette and no palette.");
  return FALSE;
}

/**
 * Function purpose: all sixteen or none. A palette half taken from the
 * compositor and half from the built-in one would have its five derived tones
 * computed across two schemes, which is the exact failure the palettes were
 * split into separate files to prevent.
 *
 * @returns TRUE when every slot was present and parsed.
 */
static gboolean read_slots(lua_State *L, Slot out[SLOTS]) {
  for (int i = 0; i < SLOTS; i++) {
    char key[12];
    g_snprintf(key, sizeof(key), "color%d", i);

    if (lua_getfield(L, -1, key) != LUA_TSTRING) {
      lua_pop(L, 1);
      g_debug("hikari palette has no usable %s; keeping the built-in palette.",
              key);
      return FALSE;
    }

    const char *spec = lua_tostring(L, -1);
    gboolean ok = slot_parse(spec, &out[i]);
    lua_pop(L, 1);

    /* Action purpose: a palette slot is a colour and carries no alpha, so one
     * written as `#rrggbbaa` keeps its three channels and loses the fourth.
     * This is hikari's own model and not a simplification of it: its sixteen
     * slots are terminal colours, and the one place it writes alpha -- `bar`,
     * "#2b1e3ae6" -- is a separate setting and not a slot. Taking an alpha here
     * would be taking it somewhere hikari never puts one.
     *
     * Keeping it broke exactly one role. `background-solid` is what the
     * notification cards paint on, its whole contract is the name -- a banner
     * lands over arbitrary content and that content must not show through its
     * text -- and the role map defines it as `@color0`. With alpha on the slot,
     * `color0 = "#00000000"` made that card fully transparent. The role map is
     * parsed AFTER this import and the cascade is last-wins, so no amount of
     * emitting here can override it; and the format has no operator for
     * applying alpha to a reference, so the role map cannot strip it either.
     * The slot is the only place the fix can live.
     *
     * Every role that wants opacity appends its own and always did:
     * `background` is this ground at 90%, `border-soft` at 60%. And every
     * derived tone is measured with relative_luminance(), which reads three
     * channels and ignores the fourth -- so dropping it here is also what makes
     * those measurements describe what is actually painted. */
    out[i].a = -1;

    if (!ok) {
      g_debug("hikari palette's %s is not a colour; keeping the built-in "
              "palette.",
              key);
      return FALSE;
    }
  }
  return TRUE;
}

/**
 * Function purpose: renders the palette in hand into the names sofi's layouts
 * read. Both halves are here because both have to agree: the tones that cannot
 * be written as a `@colorN` reference, and the roles whose right answer is a
 * measurement of this palette rather than a slot index.
 *
 * Every `palette-*` name roles.sasi reads must appear below. The fragment is
 * overlaid on a built-in palette rather than replacing it, so an omission is
 * not a default -- it is the built-in's definition resolving through somebody
 * else's slots.
 */
static char *render_sasi(const Slot palette[SLOTS]) {
  const Slot *ground = &palette[0];
  gboolean light = relative_luminance(ground) >= LIGHT_GROUND;

  Slot surface = mix_towards(ground, &palette[8], 0.25);
  Slot surface_alt = mix_towards(ground, &palette[8], 0.50);
  Slot dim = mix_towards(ground, &palette[7],
                         dim_fraction(ground, &surface, &palette[7]));

  /* Action purpose: `hint` is a recession and is therefore settled AT its
   * target rather than lifted above it -- the ground walked out towards the
   * grey slot, which is the blend the shipped Sakura palette states, and the
   * slot carried further only where that walk cannot reach. See HINT_TARGET and
   * settle_to(). */
  Slot hint = settle_to(ground, &surface, &palette[6], HINT_TARGET);

  int accent = better_against(ground, palette, 12, 4);
  int on_accent = better_against(&palette[accent], palette, 0, 15);

  /* Action purpose: the ink on the selected row, carried up until it clears
   * body AA against the fill it sits on. Picking the better of color0 and
   * color15 is the right first move and is not always enough on its own: on the
   * shipped Sakura Dark the accent fill is #873b7d and the better of the pair,
   * color15 #cacace, reads 4.35:1 -- the better choice and still under the
   * floor, because the shortfall is the fill being as light as it is rather
   * than the ink being too dark. Seven per cent toward white answers it,
   * #ceced1 at 4.53:1, which is four steps per channel and not a colour anyone
   * would notice had moved.
   *
   * The accent is passed as both backgrounds because this role has only one:
   * it is drawn on the fill and nowhere else, so there is no second pair to be
   * the worse of. */
  Slot on_accent_ink =
      lift_to(&palette[accent], &palette[accent], &palette[on_accent], AA_BODY);

  /* Action purpose: the prompt stripe is the accent's neighbour, except where
   * the accent measured better as color4 and taking color4 again would paint a
   * selection and a stripe in one colour and lose the difference between
   * them. */
  int soft_slot = accent == 4 ? 5 : 4;

  /* Action purpose: measured against `surface` and not against the ground,
   * because `surface` is what this role is painted on. Every shipped layout
   * draws the prompt as `text-color: @accent-soft` on a `prompt` block that is
   * itself `background-color: transparent`, inside an `inputbar` that is
   * `background-color: @surface` -- so the ground never reaches the glass
   * behind this text. `surface` is a quarter-step from the ground towards
   * color8, which is lighter on a dark palette, so measuring against the ground
   * reads HIGHER than the pair a user actually sees and the error is in the
   * unsafe direction. On the Sakura slots that difference is 4.68:1 claimed
   * against 3.88:1 real, which is under the 4.5:1 floor for text.
   *
   * `surface` is computed above for this reason, ahead of the roles that need
   * it. The scrollbar handle takes the same role against `surface` or `@muted`
   * and is a graphical object rather than text, so WCAG 1.4.11's 3:1 governs
   * it; clearing 4.5:1 here clears that with room to spare. */
  Slot soft = lift_to(ground, &surface, &palette[soft_slot], SOFT_TARGET);
  Slot strong = lift_to(ground, &surface, &palette[13], ACCENT_TARGET);
  Slot warning = lift_to(ground, &surface, &palette[11], ACCENT_TARGET);

  /* Action purpose: critical urgency keeps the palette's own red where it has
   * one that reads, because `critical` is a fill and `urgent` is the text over
   * it and the two are meant to differ. Where the slot is too dark to see or is
   * not red at all, it borrows urgency's literal -- contrast is the problem on
   * one shipped palette and hue on the other. */
  gboolean own_red = slot_is_red(&palette[1]) &&
                     contrast_ratio(ground, &palette[1]) >= AA_MARK;

  GString *out = g_string_new(NULL);

  /* Action purpose: one rule naming `*`, which is what puts these on the
   * theme's ROOT rather than into a widget called `*` -- and only the root is
   * searched when a `@colorN` reference resolves. The property names are
   * written in the bracketed form because several of them carry a hyphen, and
   * the bracketed form needs no translation either way. */
  g_string_append(out, "return {\n  { select = \"*\",\n");

  for (int i = 0; i < SLOTS; i++) {
    if (palette[i].a >= 0) {
      g_string_append_printf(out, "    color%d = \"#%02x%02x%02x%02x\",\n", i,
                             palette[i].r, palette[i].g, palette[i].b,
                             palette[i].a);
    } else {
      g_string_append_printf(out, "    color%d = \"#%02x%02x%02x\",\n", i,
                             palette[i].r, palette[i].g, palette[i].b);
    }
  }

  /* Action purpose: color0 at 90%, which is byte-for-byte what hikari puts in
   * its own `bar` slot -- that is what makes a sofi panel under the top bar read
   * as the same surface rather than as a window laid over it. */
  g_string_append_printf(out, "    background = \"#%02x%02x%02xe6\",\n",
                         ground->r, ground->g, ground->b);

  g_string_append_printf(out, "    surface = \"#%02x%02x%02x\",\n", surface.r,
                         surface.g, surface.b);
  g_string_append_printf(out, "    [\"surface-alt\"] = \"#%02x%02x%02x\",\n",
                         surface_alt.r, surface_alt.g, surface_alt.b);
  g_string_append_printf(out, "    [\"border-soft\"] = \"#%02x%02x%02x99\",\n",
                         palette[8].r, palette[8].g, palette[8].b);
  g_string_append_printf(out, "    hint = \"#%02x%02x%02x\",\n", hint.r, hint.g,
                         hint.b);

  /* Action purpose: the accent and its text are emitted as references and not
   * as literals, so that redefining the slot in ~/.config/sofi/config.sasi
   * still reaches the role through the chain. A link resolves at property
   * lookup, after every parse has finished. */
  g_string_append_printf(out, "    [\"palette-accent\"] = \"@color%d\",\n",
                         accent);
  append_role(out, "palette-on-accent", on_accent, &palette[on_accent],
              &on_accent_ink);
  g_string_append_printf(out, "    [\"palette-urgent\"] = \"%s\",\n",
                         light ? URGENT_ON_LIGHT : URGENT_ON_DARK);
  g_string_append_printf(out, "    [\"palette-dim\"] = \"#%02x%02x%02x\",\n",
                         dim.r, dim.g, dim.b);

  /* Action purpose: every name roles.sasi reads has to be emitted here, not
   * merely the ones a slot index happens to get right. This fragment is parsed
   * OVER the selected built-in palette, so a name left out keeps that
   * palette's definition while the slots underneath it have all been replaced
   * -- and a definition like `@color1` then resolves through the imported
   * slots. Importing Sakura Light over the built-in Sakura that way left
   * `critical` pointing at an imported color1 that is green. */
  if (own_red) {
    g_string_append(out, "    [\"palette-critical\"] = \"@color1\",\n");
  } else {
    g_string_append_printf(out, "    [\"palette-critical\"] = \"%s\",\n",
                           light ? URGENT_ON_LIGHT : URGENT_ON_DARK);
  }

  /* These three are references wherever the slot came back untouched, for the
   * reason append_role() gives: a palette whose own slot carries the role has
   * no business losing the override chain to a literal that happens to equal
   * it. On the shipped Sakura palette all three resolve that way. */
  append_role(out, "palette-soft", soft_slot, &palette[soft_slot], &soft);
  append_role(out, "palette-strong", 13, &palette[13], &strong);
  append_role(out, "palette-warning", 11, &palette[11], &warning);
  g_string_append(out, "  },\n}\n");

  g_info("Imported hikari's palette: %s ground, accent color%d, on-accent "
         "color%d.",
         light ? "light" : "dark", accent, on_accent);

  return g_string_free(out, FALSE);
}

/**
 * Function purpose: one candidate file, evaluated and rendered. Split out from
 * the search below so that where the configuration is looked for and what is
 * done with it when found are separate -- the same division source/anim.c makes
 * reading the same file, and the reason a path added to the list costs nothing
 * here.
 *
 * @returns a newly allocated sasi fragment, or NULL for any reason at all.
 */
static char *palette_from_file(const char *path) {
  SofiHikariLuaBudget budget;
  lua_State *L = sofi_hikari_lua_newstate(&budget);
  if (L == NULL) {
    return NULL;
  }

  lua_pushcfunction(L, push_sandbox_protected);
  if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
    g_debug("Cannot sandbox %s: %s", path, lua_tostring(L, -1));
    lua_close(L);
    return NULL;
  }

  /* Action purpose: text mode only. A precompiled chunk is not something a
   * configuration directory should be able to hand an interpreter, and refusing
   * it here costs nothing because nobody writes one by hand. */
  if (luaL_loadfilex(L, path, "t") != LUA_OK) {
    g_debug("Cannot load %s: %s", path, lua_tostring(L, -1));
    lua_close(L);
    return NULL;
  }

  /* Action purpose: the sandbox becomes the chunk's `_ENV`, which is upvalue 1
   * of every main chunk. This is the whole of the restriction -- a global read
   * inside the file is a lookup in that table, so anything not put there is
   * simply absent rather than merely discouraged. */
  lua_pushvalue(L, -2);
  if (lua_setupvalue(L, -2, 1) == NULL) {
    /* Action purpose: a sandbox that failed to attach must fail closed. Upvalue
     * 1 of a main chunk is always `_ENV`, so this cannot happen today; without
     * the check a future change to chunk layout would leave the configuration
     * reading the real globals instead, which is the one failure mode a sandbox
     * must not have. source/anim.c tests the same thing for the same reason. */
    g_debug("Cannot sandbox %s: the chunk has no _ENV upvalue.", path);
    lua_close(L);
    return NULL;
  }

  lua_sethook(L, step_hook, LUA_MASKCOUNT, STEP_LIMIT);

  int status = lua_pcall(L, 0, 1, 0);

  lua_sethook(L, NULL, 0, 0);

  /* Action purpose: the allocation ceiling comes off here and not at the close,
   * because from this line on the memory being asked for is this file's own and
   * not the configuration's -- the sixteen key lookups below intern their
   * names. See sofi_hikari_lua_release(). */
  sofi_hikari_lua_release(&budget);

  if (status != LUA_OK) {
    g_debug("Cannot evaluate %s: %s", path, lua_tostring(L, -1));
    lua_close(L);
    return NULL;
  }

  char *sasi = NULL;
  if (push_palette_table(L)) {
    Slot palette[SLOTS];
    if (read_slots(L, palette)) {
      sasi = render_sasi(palette);
    }
  }

  lua_close(L);
  return sasi;
}

char *sofi_hikari_palette_lua(void) {
  /* Action purpose: the four paths sofi_anim_duration() reads this same file at,
   * in the same order, because the two readers disagreeing about where the
   * compositor's configuration lives is worse than either of them being wrong
   * about it -- hikari-sakura's own `make install` puts the file under
   * SYSCONFDIR and only copies it into ~/.config on request, so a packaged
   * desktop would otherwise take the compositor's animation speed from it while
   * leaving these panels on the compiled-in colours. XDG_CONFIG_HOME is read
   * directly rather than through GLib's accessor because the fallback has to be
   * the literal ~/.config even when that variable points somewhere else. */
  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  const char *home = g_get_home_dir();
  char *candidates[4];
  int count = 0;
  char *sasi = NULL;

  if (xdg != NULL && *xdg != '\0') {
    candidates[count++] = g_build_filename(xdg, "hikari", "hikari.lua", NULL);
  }
  if (home != NULL && *home != '\0') {
    candidates[count++] =
        g_build_filename(home, ".config", "hikari", "hikari.lua", NULL);
  }
  candidates[count++] =
      g_build_filename(SYSCONFDIR, "hikari", "hikari.lua", NULL);
  candidates[count++] = g_strdup("/etc/hikari/hikari.lua");

  /* Action purpose: the first file that yields a complete palette wins, and the
   * loop runs to the end regardless so that every path is freed on one route
   * out. Existence is tested before the file is opened to keep the "Cannot
   * load" diagnostic about a configuration that is there and broken, rather
   * than about the three that were never meant to be. */
  for (int i = 0; i < count; i++) {
    if (sasi == NULL && g_file_test(candidates[i], G_FILE_TEST_IS_REGULAR)) {
      sasi = palette_from_file(candidates[i]);
    }
    g_free(candidates[i]);
  }

  return sasi;
}
