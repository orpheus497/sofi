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
static double dim_fraction(const Slot *ground, const Slot *text) {
  for (int step = DIM_STEP; step <= 100; step += DIM_STEP) {
    Slot candidate = mix_towards(ground, text, step / 100.0);
    if (contrast_ratio(&candidate, ground) >= AA_BODY) {
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
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 0);
  lua_setfield(L, -2, "string");
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 0);
  lua_setfield(L, -2, "table");
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
 * read. Both halves are here because both have to agree: the five tones that
 * cannot be written as a `@colorN` reference, and the four roles whose right
 * answer is a measurement of this palette rather than a slot index.
 */
static char *render_sasi(const Slot palette[SLOTS]) {
  const Slot *ground = &palette[0];
  gboolean light = relative_luminance(ground) >= LIGHT_GROUND;

  Slot surface = mix_towards(ground, &palette[8], 0.25);
  Slot surface_alt = mix_towards(ground, &palette[8], 0.50);
  Slot hint = mix_towards(ground, &palette[6], 0.70);
  Slot dim = mix_towards(ground, &palette[7], dim_fraction(ground, &palette[7]));

  int accent = better_against(ground, palette, 12, 4);
  int on_accent = better_against(&palette[accent], palette, 0, 15);

  GString *out = g_string_new(NULL);

  g_string_append(out, "* {\n");
  for (int i = 0; i < SLOTS; i++) {
    if (palette[i].a >= 0) {
      g_string_append_printf(out, "  color%d: #%02x%02x%02x%02x;\n", i,
                             palette[i].r, palette[i].g, palette[i].b,
                             palette[i].a);
    } else {
      g_string_append_printf(out, "  color%d: #%02x%02x%02x;\n", i,
                             palette[i].r, palette[i].g, palette[i].b);
    }
  }

  /* Action purpose: color0 at 90%, which is byte-for-byte what hikari puts in
   * its own `bar` slot -- that is what makes a sofi panel under the top bar read
   * as the same surface rather than as a window laid over it. */
  g_string_append_printf(out, "  background: #%02x%02x%02xe6;\n", ground->r,
                         ground->g, ground->b);
  g_string_append_printf(out, "  surface: #%02x%02x%02x;\n", surface.r,
                         surface.g, surface.b);
  g_string_append_printf(out, "  surface-alt: #%02x%02x%02x;\n", surface_alt.r,
                         surface_alt.g, surface_alt.b);
  g_string_append_printf(out, "  border-soft: #%02x%02x%02x99;\n", palette[8].r,
                         palette[8].g, palette[8].b);
  g_string_append_printf(out, "  hint: #%02x%02x%02x;\n", hint.r, hint.g,
                         hint.b);

  /* Action purpose: the accent and its text are emitted as references and not
   * as literals, so that redefining the slot in ~/.config/sofi/config.sasi
   * still reaches the role through the chain. A link resolves at property
   * lookup, after every parse has finished. */
  g_string_append_printf(out, "  palette-accent: @color%d;\n", accent);
  g_string_append_printf(out, "  palette-on-accent: @color%d;\n", on_accent);
  g_string_append_printf(out, "  palette-urgent: %s;\n",
                         light ? URGENT_ON_LIGHT : URGENT_ON_DARK);
  g_string_append_printf(out, "  palette-dim: #%02x%02x%02x;\n", dim.r, dim.g,
                         dim.b);
  g_string_append(out, "}\n");

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
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    return NULL;
  }

  push_sandbox(L);

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
  lua_setupvalue(L, -2, 1);

  lua_sethook(L, step_hook, LUA_MASKCOUNT, STEP_LIMIT);

  if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
    g_debug("Cannot evaluate %s: %s", path, lua_tostring(L, -1));
    lua_close(L);
    return NULL;
  }

  lua_sethook(L, NULL, 0, 0);

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

char *sofi_hikari_palette_sasi(void) {
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
