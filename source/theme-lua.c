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
 * Script function and purpose: the theme reader. A theme is a Lua chunk that
 * returns an ARRAY of rules, and this turns that array into the same
 * ThemeWidget tree the format before it produced -- through the same ten engine
 * functions, so nothing downstream of here knows which reader built the tree.
 *
 * WHY AN ARRAY AND NOT A TABLE OF SELECTORS. The cascade is ordered: properties
 * merge with g_hash_table_replace(), so the last rule to name a widget and a
 * property wins. Lua does not define the iteration order of a table's string
 * keys, so a theme written as `{ ["*"] = ..., ["element selected"] = ... }`
 * would apply its own rules in an arbitrary order and could resolve
 * differently between two runs of the same file. The array part of a Lua
 * table HAS a defined order, so rules live there and the order you read is
 * the order that applies. This is the one thing about the shape of a theme
 * that is not a matter of taste.
 *
 * WHAT A VALUE'S TYPE COMES FROM. The format before this one took a
 * property's type from the SYNTAX of its value and not from the property's
 * name -- `#2b1e3a` was a colour because of the `#`, `@urgent` a reference
 * because of the `@` -- and the engine still has no property-name-to-type map
 * to consult. So the type is taken from the Lua value, by the same rules:
 *
 *   a number        -> integer when it is integral, otherwise double
 *   a boolean       -> boolean
 *   "#..."          -> a colour, the same prefix rule the old lexer used
 *   "@..."          -> a reference, likewise
 *   any other string-> a string
 *   px(12) and kin  -> a distance
 *   { px(6), px(10) } -> padding, by one, two or four parts
 *   { "a", "b" }    -> a list
 *   a named constant-> whatever that constant is: inherit, transparent,
 *                      horizontal, pointer, north_east and the rest
 *
 * The named constants are values and not strings on purpose. `orientation =
 * "horizontal"` would be indistinguishable from a string property, and a
 * misspelling would be a silent string rather than an error; `orientation =
 * horizontal` is an undefined-name error the moment it is wrong, which is what
 * the sandbox's own diagnostic is for.
 */

#include "config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "css-colors.h"
#include "helper.h"
#include "hikari-lua.h"
#include "sofi-types.h"
#include "theme-lua.h"
#include "theme.h"
#include "xrmoptions.h"

/** The key every constructed value carries, naming what it is. */
#define TAG_FIELD "__sofi"

/** How many VM instructions a theme gets before it is taken to have hung. */
#define THEME_LUA_STEPS 20000000

/* How deep a length's arithmetic may nest. The walk that reads one is a single
 * C call, so this is a guard on the C STACK and not on hostile input -- a cycle
 * is caught by identity, not by depth. Reaching it is refused rather than
 * truncated. Themes in this repository nest three deep at the most. */
#define DISTANCE_TREE_DEPTH 256

/** How many files an import chain may be deep, for the same reason. */
#define IMPORT_DEPTH 16

/** The registry key the length metatable is kept under. */
#define LENGTH_META "sofi.length"

/* The registry keys import() reads: the sandbox every chunk of one theme
 * shares, the path of the file currently being read -- which is what a relative
 * import resolves against -- and how deep the chain already is. They live in
 * the registry rather than as upvalues because `import` is one C function
 * installed once, while the path changes per file. */
#define SANDBOX_KEY "sofi.theme.env"
#define IMPORT_PATH_KEY "sofi.theme.path"
#define IMPORT_DEPTH_KEY "sofi.theme.depth"

/* The extensions a theme name without one is tried with, in order. `.lua` is
 * the theme; `.luainc` is a fragment -- a palette or a role map -- which is not
 * a selectable theme and so is deliberately not matched by the selector
 * script's glob. The import side does not care which it is. */
const char *lua_theme_file_extensions[] = {".lua", ".luainc", NULL};

/* Action purpose: set while a caller is making a deliberate attempt that may
 * fail. The command-line option path tries a value as Lua and then as text --
 * `-window-orientation horizontal` wants the constant, `-show drun` wants the
 * string, and which of the two a bare word is cannot be told from the word --
 * so the first of those two failures is expected rather than worth reporting.
 * A plain file or string read leaves this clear and complains as it always
 * did. */
static gboolean theme_lua_quiet = FALSE;

/* Action purpose: a theme is read before a surface exists, so a complaint has
 * nowhere to go but the log and the return value. Every failure path here
 * leaves the tree as it found it, because a half-applied theme is worse than
 * the previous one. */
static void theme_lua_warn(const char *path, const char *message) {
  if (theme_lua_quiet) {
    return;
  }

  if (path != NULL) {
    g_warning("%s: %s", path, message);
  } else {
    g_warning("theme: %s", message);
  }
}

/**
 * Function purpose: push a tagged table, which is how every constructed value
 * travels from the configuration back into C. One field names the kind so a
 * wrong value is reported as what it is rather than as "table".
 *
 * Leaves the table on the stack.
 */
static void tagged_new(lua_State *L, const char *kind) {
  lua_createtable(L, 0, 4);
  lua_pushstring(L, kind);
  lua_setfield(L, -2, TAG_FIELD);
}

/**
 * @returns the tag of the table at @p idx, or NULL when it carries none.
 *
 * Action purpose: the string is popped before the pointer to it is returned,
 * which would ordinarily be handing out a reference the collector is free to
 * reclaim. It is safe here for one specific reason: the string is also the
 * value of the table's own TAG_FIELD, and that table is on the stack, so the
 * string stays reachable for exactly as long as the table the caller is
 * reading. A caller that keeps the pointer past the table's lifetime would be
 * wrong; none does.
 */
static const char *tagged_kind(lua_State *L, int idx) {
  if (lua_type(L, idx) != LUA_TTABLE) {
    return NULL;
  }

  lua_getfield(L, idx, TAG_FIELD);
  const char *kind = lua_tostring(L, -1);
  lua_pop(L, 1);

  return kind;
}

static void length_meta_build(lua_State *L);
static int sandbox_undefined(lua_State *L);
static void colour_from_table(lua_State *L, int idx, ThemeColor *out);
static int tagged_int(lua_State *L, int idx);

/**
 * Function purpose: give the table on top of the stack the length metatable,
 * which is what makes `px(100) - px(10)` an expression rather than an error.
 *
 * Action purpose: the metatable is built once and kept in the registry, so
 * every length shares one and an `__index` lookup costs nothing extra. It is
 * created on first use rather than at sandbox-build time because the arithmetic
 * functions it holds are defined below it.
 */
static void length_setmeta(lua_State *L) {
  if (luaL_getmetatable(L, LENGTH_META) == LUA_TNIL) {
    lua_pop(L, 1);
    length_meta_build(L);
    luaL_getmetatable(L, LENGTH_META);
  }

  lua_setmetatable(L, -2);
}

/* Action purpose: the five length units, built by one function because they
 * differ only in the unit they carry. The unit rides as an upvalue so there is
 * one closure per unit and one body for all of them.
 *
 * The optional second argument is the line style, which is where `1px solid`
 * went: a border's style belongs to its width in the engine's own struct, so it
 * is given at the same place rather than as a property of its own. */
static int length_new(lua_State *L) {
  double value = luaL_checknumber(L, 1);
  int unit = (int)lua_tointeger(L, lua_upvalueindex(1));
  int style = SOFI_HL_SOLID;

  if (!lua_isnoneornil(L, 2)) {
    const char *kind = tagged_kind(L, 2);

    if (kind == NULL || strcmp(kind, "linestyle") != 0) {
      return luaL_error(L, "a length's second argument is a line style, "
                           "either solid or dash");
    }

    style = tagged_int(L, 2);
  }

  tagged_new(L, "length");
  lua_pushnumber(L, value);
  lua_setfield(L, -2, "value");
  lua_pushinteger(L, unit);
  lua_setfield(L, -2, "unit");
  lua_pushinteger(L, style);
  lua_setfield(L, -2, "style");
  lua_pushinteger(L, SOFI_DISTANCE_MODIFIER_NONE);
  lua_setfield(L, -2, "modtype");
  length_setmeta(L);

  return 1;
}

/**
 * Function purpose: push a length for one operand of an arithmetic expression,
 * accepting a bare number as pixels.
 *
 * Action purpose: a bare number IS a length in this format -- an unsuffixed
 * number was pixels in the format before it, and property_from_lua() still
 * reads one that way -- so `px(100) / 2` has to mean what it looks like rather
 * than refusing because 2 carries no unit.
 *
 * @returns FALSE when the value is neither a length nor a number, which the
 * caller turns into an error naming both operands.
 */
static gboolean length_push_operand(lua_State *L, int idx) {
  const char *kind = tagged_kind(L, idx);

  if (kind != NULL && strcmp(kind, "length") == 0) {
    lua_pushvalue(L, idx);

    return TRUE;
  }

  if (lua_type(L, idx) == LUA_TNUMBER) {
    lua_pushinteger(L, SOFI_PU_PX);
    lua_pushcclosure(L, length_new, 1);
    lua_pushvalue(L, idx);
    lua_call(L, 1, 1);

    return TRUE;
  }

  return FALSE;
}

/**
 * Function purpose: build one node of a length expression. The modifier rides
 * as an upvalue, so the eleven operations share this one body.
 *
 * Action purpose: the node carries its own `value` and `unit` as well as its
 * two operands, because the engine's SofiDistanceUnit does -- it reads
 * `distance` and `type` when `modtype` is NONE and the operands when it is not,
 * and a node that left them unset would read as a zero-pixel length if the tree
 * were ever walked as a leaf.
 */
static int length_math(lua_State *L) {
  int modifier = (int)lua_tointeger(L, lua_upvalueindex(1));

  if (!length_push_operand(L, 1) || !length_push_operand(L, 2)) {
    return luaL_error(L, "a length can only be combined with another length "
                         "or with a plain number");
  }

  int right = lua_gettop(L);
  int left = right - 1;

  tagged_new(L, "length");

  lua_pushnumber(L, 0.0);
  lua_setfield(L, -2, "value");
  lua_pushinteger(L, SOFI_PU_PX);
  lua_setfield(L, -2, "unit");
  lua_pushinteger(L, modifier);
  lua_setfield(L, -2, "modtype");
  lua_pushvalue(L, left);
  lua_setfield(L, -2, "left");
  lua_pushvalue(L, right);
  lua_setfield(L, -2, "right");

  /* The style of the left operand carries, so `px(1, dash) / 2` is still a
   * dashed line. */
  lua_getfield(L, left, "style");
  lua_setfield(L, -2, "style");

  length_setmeta(L);

  return 1;
}

/* Action purpose: negation is subtraction from zero rather than a modifier of
 * its own, because the engine has no unary node and a -1 multiplier would
 * change the unit of a percentage. */
static int length_neg(lua_State *L) {
  lua_pushinteger(L, SOFI_PU_PX);
  lua_pushcclosure(L, length_new, 1);
  lua_pushnumber(L, 0.0);
  lua_call(L, 1, 1);
  lua_pushvalue(L, 1);

  lua_pushinteger(L, SOFI_DISTANCE_MODIFIER_SUBTRACT);
  lua_pushcclosure(L, length_math, 1);
  lua_insert(L, -3);

  lua_call(L, 2, 1);

  return 1;
}

/**
 * Function purpose: build the length metatable -- the arithmetic operators as
 * metamethods, and `min`, `max`, `round`, `floor` and `ceil` as named
 * functions, since Lua has no operator for those.
 *
 * Action purpose: the five named ones are reached through the `calc` table
 * rather than as metamethods because they are two-operand operations with no
 * syntax: `calc.floor(px(1024), px(30))` is what `calc( 1024 floor 30 )` was,
 * and the name is kept so a theme being translated reads the same.
 */
static void length_meta_build(lua_State *L) {
  static const struct {
    const char *event;
    int modifier;
  } binary[] = {
      {"__add", SOFI_DISTANCE_MODIFIER_ADD},
      {"__sub", SOFI_DISTANCE_MODIFIER_SUBTRACT},
      {"__mul", SOFI_DISTANCE_MODIFIER_MULTIPLY},
      {"__div", SOFI_DISTANCE_MODIFIER_DIVIDE},
      {"__mod", SOFI_DISTANCE_MODIFIER_MODULO},
      {NULL, 0},
  };

  luaL_newmetatable(L, LENGTH_META);

  for (int i = 0; binary[i].event != NULL; i++) {
    lua_pushinteger(L, binary[i].modifier);
    lua_pushcclosure(L, length_math, 1);
    lua_setfield(L, -2, binary[i].event);
  }

  lua_pushcfunction(L, length_neg);
  lua_setfield(L, -2, "__unm");

  /* A length is not a table a theme should be reaching into, and hiding the
   * metatable keeps `getmetatable` from handing out the one every length
   * shares. */
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");

  lua_pop(L, 1);
}

/* Action purpose: the `calc` table, holding the five operations Lua has no
 * operator for. They are the previous format's five modifier keywords, under
 * the name that format used for the whole construct. */
static void push_calc(lua_State *L) {
  static const struct {
    const char *name;
    int modifier;
  } named[] = {
      {"min", SOFI_DISTANCE_MODIFIER_MIN},
      {"max", SOFI_DISTANCE_MODIFIER_MAX},
      {"round", SOFI_DISTANCE_MODIFIER_ROUND},
      {"floor", SOFI_DISTANCE_MODIFIER_FLOOR},
      {"ceil", SOFI_DISTANCE_MODIFIER_CEIL},
      {NULL, 0},
  };

  lua_createtable(L, 0, 5);

  for (int i = 0; named[i].name != NULL; i++) {
    lua_pushinteger(L, named[i].modifier);
    lua_pushcclosure(L, length_math, 1);
    lua_setfield(L, -2, named[i].name);
  }
}

/* Action purpose: a colour from four channels, red green blue in 0-255 and
 * alpha in 0-1, which is the order and the ranges the old format's `rgba()`
 * took. Alpha is optional and opaque when left out. */
static int colour_rgba(lua_State *L) {
  double r = luaL_checknumber(L, 1);
  double g = luaL_checknumber(L, 2);
  double b = luaL_checknumber(L, 3);
  double a = luaL_optnumber(L, 4, 1.0);

  tagged_new(L, "colour");
  lua_pushnumber(L, CLAMP(r, 0.0, 255.0) / 255.0);
  lua_setfield(L, -2, "red");
  lua_pushnumber(L, CLAMP(g, 0.0, 255.0) / 255.0);
  lua_setfield(L, -2, "green");
  lua_pushnumber(L, CLAMP(b, 0.0, 255.0) / 255.0);
  lua_setfield(L, -2, "blue");
  lua_pushnumber(L, CLAMP(a, 0.0, 1.0));
  lua_setfield(L, -2, "alpha");

  return 1;
}

/* Action purpose: hue in degrees, saturation and lightness in 0-1, converted
 * here rather than carried as HSL because the engine's ThemeColor is RGB and a
 * conversion done once at read time cannot disagree with one done later. */
static int colour_hsl(lua_State *L) {
  double h = luaL_checknumber(L, 1);
  double s = CLAMP(luaL_checknumber(L, 2), 0.0, 1.0);
  double l = CLAMP(luaL_checknumber(L, 3), 0.0, 1.0);
  double a = CLAMP(luaL_optnumber(L, 4, 1.0), 0.0, 1.0);

  h = fmod(fmod(h, 360.0) + 360.0, 360.0) / 60.0;

  double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
  double x = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
  double m = l - c / 2.0;
  double rgb[3] = {m, m, m};

  switch ((int)h) {
  case 0: rgb[0] += c; rgb[1] += x; break;
  case 1: rgb[0] += x; rgb[1] += c; break;
  case 2: rgb[1] += c; rgb[2] += x; break;
  case 3: rgb[1] += x; rgb[2] += c; break;
  case 4: rgb[0] += x; rgb[2] += c; break;
  default: rgb[0] += c; rgb[2] += x; break;
  }

  tagged_new(L, "colour");
  lua_pushnumber(L, rgb[0]);
  lua_setfield(L, -2, "red");
  lua_pushnumber(L, rgb[1]);
  lua_setfield(L, -2, "green");
  lua_pushnumber(L, rgb[2]);
  lua_setfield(L, -2, "blue");
  lua_pushnumber(L, a);
  lua_setfield(L, -2, "alpha");

  return 1;
}

/** Push a tagged colour from four already-normalised 0-1 channels. */
static void colour_push(lua_State *L, double r, double g, double b, double a) {
  static const char *const channel[] = {"red", "green", "blue", "alpha", NULL};
  double value[] = {r, g, b, a};

  tagged_new(L, "colour");

  for (int i = 0; channel[i] != NULL; i++) {
    lua_pushnumber(L, CLAMP(value[i], 0.0, 1.0));
    lua_setfield(L, -2, channel[i]);
  }
}

/* Action purpose: hue, whiteness and blackness, the last two in 0-1. Computed
 * the way the previous format computed it -- a fully saturated hue scaled by
 * what is left after the white and the black, then the white added back --
 * rather than by a formula of my own, so a theme being translated keeps the
 * colour it had. */
static int colour_hwb(lua_State *L) {
  double h = luaL_checknumber(L, 1);
  double w = CLAMP(luaL_checknumber(L, 2), 0.0, 1.0);
  double b = CLAMP(luaL_checknumber(L, 3), 0.0, 1.0);
  double a = CLAMP(luaL_optnumber(L, 4, 1.0), 0.0, 1.0);

  lua_pushcfunction(L, colour_hsl);
  lua_pushnumber(L, h);
  lua_pushnumber(L, 1.0);
  lua_pushnumber(L, 0.5);
  lua_call(L, 3, 1);

  ThemeColor pure;
  colour_from_table(L, lua_gettop(L), &pure);
  lua_pop(L, 1);

  double scale = 1.0 - w - b;

  colour_push(L, pure.red * scale + w, pure.green * scale + w,
              pure.blue * scale + w, a);

  return 1;
}

/* Action purpose: cyan, magenta, yellow and key, all in 0-1. The subtractive
 * conversion, which is one multiplication per channel and has no edge case. */
static int colour_cmyk(lua_State *L) {
  double c = CLAMP(luaL_checknumber(L, 1), 0.0, 1.0);
  double m = CLAMP(luaL_checknumber(L, 2), 0.0, 1.0);
  double y = CLAMP(luaL_checknumber(L, 3), 0.0, 1.0);
  double k = CLAMP(luaL_checknumber(L, 4), 0.0, 1.0);
  double a = CLAMP(luaL_optnumber(L, 5, 1.0), 0.0, 1.0);

  colour_push(L, (1.0 - c) * (1.0 - k), (1.0 - m) * (1.0 - k),
              (1.0 - y) * (1.0 - k), a);

  return 1;
}

/* Action purpose: the named constants, each a tagged table with its engine
 * value already in it, so the walk below reads a number rather than matching a
 * word. They are installed as names and not as strings so that a misspelling is
 * an undefined name -- an error with a line number -- instead of a string
 * property that silently does nothing. */
struct named_constant {
  const char *name;
  const char *kind;
  int value;
};

static const struct named_constant constants[] = {
    {"inherit", "inherit", 0},
    {"transparent", "transparent", 0},

    {"horizontal", "orientation", SOFI_ORIENTATION_HORIZONTAL},
    {"vertical", "orientation", SOFI_ORIENTATION_VERTICAL},

    {"default", "cursor", SOFI_CURSOR_DEFAULT},
    {"pointer", "cursor", SOFI_CURSOR_POINTER},
    {"text", "cursor", SOFI_CURSOR_TEXT},

    {"solid", "linestyle", SOFI_HL_SOLID},
    {"dash", "linestyle", SOFI_HL_DASH},

    {"none", "style", SOFI_HL_NONE},
    {"bold", "style", SOFI_HL_BOLD},
    {"italic", "style", SOFI_HL_ITALIC},
    {"underline", "style", SOFI_HL_UNDERLINE},
    {"strikethrough", "style", SOFI_HL_STRIKETHROUGH},
    {"uppercase", "style", SOFI_HL_UPPERCASE},
    {"lowercase", "style", SOFI_HL_LOWERCASE},
    {"capitalize", "style", SOFI_HL_CAPITALIZE},

    {"center", "position", WL_CENTER},
    {"north", "position", WL_NORTH},
    {"south", "position", WL_SOUTH},
    {"east", "position", WL_EAST},
    {"west", "position", WL_WEST},
    {"north_east", "position", WL_NORTH_EAST},
    {"north_west", "position", WL_NORTH_WEST},
    {"south_east", "position", WL_SOUTH_EAST},
    {"south_west", "position", WL_SOUTH_WEST},

    {NULL, NULL, 0},
};

/**
 * Function purpose: read a colour out of `#rgb`, `#rrggbb` or `#rrggbbaa`.
 *
 * Action purpose: the `#` prefix is what makes this a colour rather than a
 * string, which is the rule the old lexer used and is therefore the rule a
 * theme already written is expecting. A string that must literally begin with
 * `#` and stay a string is the one case this costs, and `tostring` on a table
 * is not it -- that case wants `str()`, which is why that constructor exists.
 *
 * `#rgb` doubles each nibble rather than shifting it, so `#fff` is white and
 * not one part in sixteen short of it.
 *
 * @returns FALSE when the text after `#` is not 3, 6 or 8 hex digits.
 */
static gboolean colour_from_hex(const char *spec, ThemeColor *out) {
  size_t len = strlen(spec + 1);
  const char *d = spec + 1;
  int v[8];

  if (len != 3 && len != 4 && len != 6 && len != 8) {
    return FALSE;
  }

  for (size_t i = 0; i < len; i++) {
    if (!g_ascii_isxdigit(d[i])) {
      return FALSE;
    }
    v[i] = g_ascii_xdigit_value(d[i]);
  }

  /* Four digits is the short form WITH alpha, which the format before this one
   * accepted and its test suite pinned: `#f003` is red at a fifth opacity. It
   * is here because dropping it would have been a notation lost rather than a
   * syntax replaced, and nothing about moving to Lua requires losing it. */
  if (len == 3 || len == 4) {
    out->red = (v[0] * 17) / 255.0;
    out->green = (v[1] * 17) / 255.0;
    out->blue = (v[2] * 17) / 255.0;
    out->alpha = len == 4 ? (v[3] * 17) / 255.0 : 1.0;

    return TRUE;
  }

  out->red = (v[0] * 16 + v[1]) / 255.0;
  out->green = (v[2] * 16 + v[3]) / 255.0;
  out->blue = (v[4] * 16 + v[5]) / 255.0;
  out->alpha = len == 8 ? (v[6] * 16 + v[7]) / 255.0 : 1.0;

  return TRUE;
}

/** @returns the `value` field of the tagged table at @p idx, as an integer. */
static int tagged_int(lua_State *L, int idx) {
  lua_getfield(L, idx, "value");
  int v = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);

  return v;
}

/** Fill @p out from a tagged colour table at @p idx. */
static void colour_from_table(lua_State *L, int idx, ThemeColor *out) {
  static const char *const channel[] = {"red", "green", "blue", "alpha", NULL};
  double *slot[] = {&out->red, &out->green, &out->blue, &out->alpha};

  for (int i = 0; channel[i] != NULL; i++) {
    lua_getfield(L, idx, channel[i]);
    *slot[i] = lua_tonumber(L, -1);
    lua_pop(L, 1);
  }
}

/* Action purpose: free a half-built distance tree. The engine has the same walk
 * but keeps it private to source/theme.c, and only an abandoned tree needs it
 * here -- a tree that reached a Property is freed by
 * sofi_theme_property_free() -- so a local four lines is less to carry than a
 * widened interface. */
static void distance_unit_drop(SofiDistanceUnit *unit) {
  if (unit == NULL) {
    return;
  }

  distance_unit_drop(unit->left);
  distance_unit_drop(unit->right);
  g_slice_free(SofiDistanceUnit, unit);
}

/**
 * Function purpose: fill one node of a distance tree from a tagged length
 * table, following `left` and `right` for as long as they are there.
 *
 * Action purpose: a length is a TREE and not a number because `%`, `em` and
 * `ch` cannot be resolved until the widget they are measured against exists.
 * `percent(100) - px(10)` therefore has to survive being read as a subtraction
 * of two unresolved operands, which is exactly what the previous format's
 * `calc( 100% - 10px )` built and what the engine still evaluates at use time.
 *
 * Action purpose: @p path holds the tables already open on the way down, and a
 * table that appears on it twice is a cycle. A theme can build one --
 * `rawset(v, "left", v)` reaches past the constructors -- and this walk is a
 * single C call, so the instruction budget cannot interrupt it. The reason to
 * detect the cycle rather than simply stop at a depth is that stopping
 * SILENTLY PRODUCES A WRONG NUMBER: a cut tree still evaluates, to whatever
 * the truncated expression happens to come to. A cycle is refused outright,
 * and a legitimate expression is limited only by the C stack, which is what
 * DISTANCE_TREE_DEPTH is now for -- and reaching that is refused too, for the
 * same reason.
 *
 * @returns FALSE when the tree cycles or is deeper than the stack allows, with
 * @p out left holding nothing the caller has to free.
 */
static gboolean distance_unit_from_table(lua_State *L, int idx,
                                         SofiDistanceUnit *out,
                                         const void **path, int depth) {
  const void *self = lua_topointer(L, idx);

  for (int i = 0; i < depth; i++) {
    if (path[i] == self) {
      return FALSE;
    }
  }

  if (depth >= DISTANCE_TREE_DEPTH) {
    return FALSE;
  }

  /* Action purpose: each level of this walk leaves the operand it is following
   * on the Lua stack while it recurses, so the stack grows with the
   * expression. lua_getfield() does NOT grow it -- the C API makes reserving
   * space the caller's job -- and pushing into a full stack writes past the
   * end of the array Lua allocated for it. That is heap corruption, and it
   * surfaces much later as a free() of a pointer whose header has been
   * overwritten, which is exactly what a deep expression did here before this
   * check existed. */
  if (!lua_checkstack(L, 4)) {
    return FALSE;
  }

  path[depth] = self;

  lua_getfield(L, idx, "value");
  out->distance = lua_tonumber(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "unit");
  out->type = (SofiPixelUnit)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "modtype");
  out->modtype = (SofiDistanceModifier)lua_tointeger(L, -1);
  lua_pop(L, 1);

  out->left = NULL;
  out->right = NULL;

  if (out->modtype == SOFI_DISTANCE_MODIFIER_NONE) {
    return TRUE;
  }

  static const char *const side[] = {"left", "right", NULL};
  SofiDistanceUnit **slot[] = {&out->left, &out->right};
  gboolean ok = TRUE;

  for (int i = 0; ok && side[i] != NULL; i++) {
    lua_getfield(L, idx, side[i]);

    if (lua_type(L, -1) == LUA_TTABLE) {
      *slot[i] = g_slice_new0(SofiDistanceUnit);
      ok = distance_unit_from_table(L, lua_gettop(L), *slot[i], path,
                                    depth + 1);
    }

    lua_pop(L, 1);
  }

  if (!ok) {
    distance_unit_drop(out->left);
    distance_unit_drop(out->right);
    out->left = NULL;
    out->right = NULL;
    out->modtype = SOFI_DISTANCE_MODIFIER_NONE;

    return FALSE;
  }

  /* A modifier with an operand missing is not a modifier; the node keeps its
   * own value rather than being evaluated against nothing. */
  if (out->left == NULL || out->right == NULL) {
    distance_unit_drop(out->left);
    distance_unit_drop(out->right);
    out->left = NULL;
    out->right = NULL;
    out->modtype = SOFI_DISTANCE_MODIFIER_NONE;
  }

  return TRUE;
}

/**
 * Fill @p out from a tagged length table at @p idx.
 *
 * @returns FALSE when the length's arithmetic cycles, which the caller reports
 * as the property being unreadable rather than quietly using a wrong number.
 */
static gboolean distance_from_table(lua_State *L, int idx, SofiDistance *out) {
  const void *path[DISTANCE_TREE_DEPTH];

  /* Action purpose: a relative index is resolved once, here. The walk below
   * pushes and pops around every field it reads, so a relative index happens
   * to survive it -- but only by that balance, and one unpaired push anywhere
   * in it would silently start reading a different table. */
  idx = lua_absindex(L, idx);

  out->base.distance = 0.0;
  out->base.type = SOFI_PU_PX;
  out->base.modtype = SOFI_DISTANCE_MODIFIER_NONE;
  out->base.left = NULL;
  out->base.right = NULL;
  out->style = SOFI_HL_SOLID;

  if (!distance_unit_from_table(L, idx, &out->base, path, 0)) {
    return FALSE;
  }

  lua_getfield(L, idx, "style");
  out->style = lua_isnumber(L, -1) ? (SofiLineStyle)lua_tointeger(L, -1)
                                   : SOFI_HL_SOLID;
  lua_pop(L, 1);

  return TRUE;
}

/* A bare number used where a length belongs is pixels, which is what the old
 * format did with an unsuffixed integer. */
static void distance_from_number(double value, SofiDistance *out) {
  out->base.distance = value;
  out->base.type = SOFI_PU_PX;
  out->base.modtype = SOFI_DISTANCE_MODIFIER_NONE;
  out->base.left = NULL;
  out->base.right = NULL;
  out->style = SOFI_HL_SOLID;
}

/**
 * Function purpose: a colour by CSS name, or by hex, with an optional alpha.
 *
 * Action purpose: the format before this one made a bare word a colour when it
 * matched a CSS name, which Lua cannot do -- a bare word is either a variable
 * or a string, and a string without a `#` is a string property. So the name is
 * given to a function instead, and the same function takes hex so there is one
 * way to write a literal colour whichever notation the author prefers.
 *
 * The CSS table is the engine's own (source/css-colors.c), matched without
 * regard to case exactly as the old lexer matched it, so every name that worked
 * before works here.
 */
static int colour_named(lua_State *L) {
  const char *spec = luaL_checkstring(L, 1);
  double alpha = CLAMP(luaL_optnumber(L, 2, 1.0), 0.0, 1.0);

  if (spec[0] == '#') {
    ThemeColor c;

    if (!colour_from_hex(spec, &c)) {
      return luaL_error(L, "`%s` is not 3, 6 or 8 hex digits after the #",
                        spec);
    }

    colour_push(L, c.red, c.green, c.blue,
                lua_isnoneornil(L, 2) ? c.alpha : alpha);

    return 1;
  }

  for (unsigned int i = 0; i < num_CSSColors; i++) {
    if (g_ascii_strcasecmp(spec, CSSColors[i].name) == 0) {
      colour_push(L, CSSColors[i].r / 255.0, CSSColors[i].g / 255.0,
                  CSSColors[i].b / 255.0, alpha);

      return 1;
    }
  }

  return luaL_error(L, "`%s` is not a CSS colour name and does not begin "
                       "with #",
                    spec);
}

/**
 * Function purpose: a text style, optionally several of them and optionally
 * with a colour.
 *
 * Action purpose: the engine stores a highlight as a bit set plus one colour,
 * so the styles OR together and the colour is separate. A single style needs no
 * call at all -- `highlight = bold` works, because property_from_lua() reads a
 * bare style constant as a highlight of one bit -- and this exists for the
 * combinations, which is what `bold underline #f00` was.
 */
static int value_highlight(lua_State *L) {
  int top = lua_gettop(L);
  int style = SOFI_HL_NONE;
  ThemeColor colour = {0.0, 0.0, 0.0, 1.0};
  gboolean coloured = FALSE;

  for (int i = 1; i <= top; i++) {
    const char *kind = tagged_kind(L, i);

    if (kind != NULL && strcmp(kind, "style") == 0) {
      style |= tagged_int(L, i);

      continue;
    }

    if (kind != NULL && strcmp(kind, "colour") == 0) {
      colour_from_table(L, i, &colour);
      coloured = TRUE;

      continue;
    }

    if (lua_type(L, i) == LUA_TSTRING) {
      lua_pushcfunction(L, colour_named);
      lua_pushvalue(L, i);
      lua_call(L, 1, 1);
      colour_from_table(L, lua_gettop(L), &colour);
      lua_pop(L, 1);
      coloured = TRUE;

      continue;
    }

    return luaL_error(L, "argument %d of highlight is neither a style nor a "
                         "colour",
                      i);
  }

  if (coloured) {
    style |= SOFI_HL_COLOR;
  }

  tagged_new(L, "highlight");
  lua_pushinteger(L, style);
  lua_setfield(L, -2, "style");
  colour_push(L, colour.red, colour.green, colour.blue, colour.alpha);
  lua_setfield(L, -2, "colour");

  return 1;
}

/**
 * Function purpose: an image from a path, with an optional scaling mode.
 *
 * Action purpose: the mode is a string from a closed set rather than four more
 * names in the global scope, because three of the four -- `width`, `height`,
 * `none` -- are words this format already uses for other things, and a global
 * that shadows a property name is a trap. An unknown mode is an error, so the
 * closed set is still checked.
 */
static int image_url(lua_State *L) {
  static const char *const mode[] = {"none", "both", "width", "height", NULL};
  static const SofiScaleType scaling[] = {SOFI_SCALE_NONE, SOFI_SCALE_BOTH,
                                          SOFI_SCALE_WIDTH, SOFI_SCALE_HEIGHT};
  const char *path = luaL_checkstring(L, 1);
  int which = lua_isnoneornil(L, 2) ? 0 : luaL_checkoption(L, 2, NULL, mode);

  tagged_new(L, "image");
  lua_pushinteger(L, SOFI_IMAGE_URL);
  lua_setfield(L, -2, "image");
  lua_pushstring(L, path);
  lua_setfield(L, -2, "url");
  lua_pushinteger(L, scaling[which]);
  lua_setfield(L, -2, "scaling");

  return 1;
}

/**
 * Function purpose: a linear gradient: an optional direction or angle, then two
 * or more colour stops.
 *
 * Action purpose: the direction is a string and the angle a plain number of
 * degrees, which is where the previous format's four angle units went. They are
 * not lost -- `deg` was the default, and radians, gradians and turns are
 * `n * 180 / math.pi`, `n * 0.9` and `n * 360`, which is arithmetic this
 * format can now do. Four names in the global scope to spell three
 * multiplications is exactly the bloat moving to Lua was meant to remove.
 *
 * The engine stores the angle in turns, so degrees are divided by 360 here --
 * the same division the previous reader did.
 */
static int image_linear_gradient(lua_State *L) {
  static const char *const edge[] = {"left", "right", "top", "bottom", NULL};
  static const SofiDirection toward[] = {
      SOFI_DIRECTION_LEFT, SOFI_DIRECTION_RIGHT, SOFI_DIRECTION_TOP,
      SOFI_DIRECTION_BOTTOM};
  int top = lua_gettop(L);
  int first = 1;
  SofiDirection dir = SOFI_DIRECTION_RIGHT;
  double angle = 0.0;

  if (top >= 1 && lua_type(L, 1) == LUA_TNUMBER) {
    dir = SOFI_DIRECTION_ANGLE;
    angle = lua_tonumber(L, 1) / 360.0;
    first = 2;
  } else if (top >= 1 && lua_type(L, 1) == LUA_TSTRING &&
             lua_tostring(L, 1)[0] != '#') {
    const char *word = lua_tostring(L, 1);
    gboolean named = FALSE;

    for (int i = 0; edge[i] != NULL; i++) {
      if (strcmp(word, edge[i]) == 0) {
        dir = toward[i];
        named = TRUE;
        first = 2;

        break;
      }
    }

    /* A leading string that is not an edge is a CSS colour name, which is a
     * stop and not a direction. */
    if (!named) {
      first = 1;
    }
  }

  if (top - first + 1 < 1) {
    return luaL_error(L, "a gradient needs at least one colour stop");
  }

  tagged_new(L, "image");
  lua_pushinteger(L, SOFI_IMAGE_LINEAR_GRADIENT);
  lua_setfield(L, -2, "image");
  lua_pushinteger(L, dir);
  lua_setfield(L, -2, "direction");
  lua_pushnumber(L, angle);
  lua_setfield(L, -2, "angle");

  lua_createtable(L, top - first + 1, 0);

  for (int i = first; i <= top; i++) {
    const char *kind = tagged_kind(L, i);

    if (kind != NULL && strcmp(kind, "colour") == 0) {
      lua_pushvalue(L, i);
    } else if (lua_type(L, i) == LUA_TSTRING) {
      lua_pushcfunction(L, colour_named);
      lua_pushvalue(L, i);
      lua_call(L, 1, 1);
    } else {
      return luaL_error(L, "stop %d of the gradient is not a colour",
                        i - first + 1);
    }

    lua_rawseti(L, -2, i - first + 1);
  }

  lua_setfield(L, -2, "stops");

  return 1;
}

/** Fill @p out from a tagged image table at @p idx. */
static void image_from_table(lua_State *L, int idx, SofiImage *out) {
  lua_getfield(L, idx, "image");
  out->type = (SofiImageType)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "url");
  out->url = lua_isstring(L, -1) ? g_strdup(lua_tostring(L, -1)) : NULL;
  lua_pop(L, 1);

  lua_getfield(L, idx, "scaling");
  out->scaling = (SofiScaleType)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "direction");
  out->dir = (SofiDirection)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "angle");
  out->angle = lua_tonumber(L, -1);
  lua_pop(L, 1);

  out->colors = NULL;
  out->wsize = 0;
  out->hsize = 0;
  out->surface_id = 0;

  lua_getfield(L, idx, "stops");

  if (lua_type(L, -1) == LUA_TTABLE) {
    lua_Unsigned n = lua_rawlen(L, -1);

    for (lua_Unsigned i = 1; i <= n; i++) {
      lua_rawgeti(L, -1, (lua_Integer)i);

      if (lua_type(L, -1) == LUA_TTABLE) {
        ThemeColor c;
        colour_from_table(L, lua_gettop(L), &c);
        out->colors = g_list_append(
            out->colors, g_memdup2((gconstpointer)&c, sizeof(ThemeColor)));
      }

      lua_pop(L, 1);
    }
  }

  lua_pop(L, 1);
}

static Property *property_from_array(lua_State *L, const char *name, int idx);
static Property *property_from_lua(lua_State *L, const char *name, int idx);

/**
 * Function purpose: build a P_LIST from the array part of the table at @p idx.
 *
 * Action purpose: the engine's list holds PROPERTIES and not strings -- its
 * copy walks the list with sofi_theme_property_copy() and its printer with
 * int_sofi_theme_print_property() -- so every element goes through the same
 * reader the property itself did. That is also what makes a list of lengths or
 * of colours work, which the previous format allowed and `tabs: [ 10, 20px ]`
 * in the manual uses.
 *
 * Elements are unnamed, as the previous reader left them: the name belongs to
 * the list, not to its members.
 */
static Property *property_list_from_array(lua_State *L, const char *name,
                                          int idx) {
  Property *p = sofi_theme_property_create(P_LIST);
  lua_Unsigned n = lua_rawlen(L, idx);

  p->value.list = NULL;
  p->name = g_strdup(name);

  for (lua_Unsigned i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, (lua_Integer)i);

    Property *item = property_from_lua(L, NULL, lua_gettop(L));

    if (item == NULL) {
      g_warning("`%s`: item %lu of the list is not a value this understands; "
                "dropping it.",
                name, (unsigned long)i);
    } else {
      p->value.list = g_list_append(p->value.list, item);
    }

    lua_pop(L, 1);
  }

  return p;
}

/**
 * Function purpose: a reference with a fallback, which `"@name"` cannot carry.
 *
 * Action purpose: a reference resolves at property lookup, after every source
 * has been read, so whether it resolves is not knowable while the theme runs
 * and the fallback cannot be an `or` in Lua. The engine models it as a second
 * Property hanging off the link, which is what the previous format's
 * `var( width, 30% )` and `env( NAME, 128 )` both built, so this is the one
 * construct that needs a name of its own rather than becoming arithmetic.
 *
 * `ref("name")` with no fallback is the long spelling of `"@name"`, kept so
 * that a dump can print every reference the same way.
 */
static int value_ref(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);

  /* Action purpose: whether there IS a second argument is settled before
   * anything is pushed. tagged_new() puts a table on the stack, and asking
   * afterwards whether index 2 is empty answers about that table rather than
   * about the call -- so every reference written without a fallback got itself
   * as its fallback, and reading one recursed until the Lua stack was
   * overrun. */
  gboolean fallback = !lua_isnoneornil(L, 2);

  tagged_new(L, "ref");
  lua_pushstring(L, name);
  lua_setfield(L, -2, "name");

  if (fallback) {
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "default");
  }

  return 1;
}

/* Action purpose: a list, written out, for the cases an array on its own is
 * ambiguous. `{ px(1), px(2), px(3), px(4) }` is padding -- four edges is what
 * four lengths nearly always mean -- so a list of four lengths has to say so,
 * which is what the previous format's square brackets did. */
static int value_list(lua_State *L) {
  int top = lua_gettop(L);

  tagged_new(L, "list");
  lua_createtable(L, top, 0);

  for (int i = 1; i <= top; i++) {
    lua_pushvalue(L, i);
    lua_rawseti(L, -2, i);
  }

  lua_setfield(L, -2, "value");

  return 1;
}

/**
 * Function purpose: turn one Lua value into one Property, which is where the
 * type rules at the top of this file are actually applied. The property is
 * named here because the engine keys its hash table on the name the Property
 * carries, not on the key it is filed under.
 *
 * @param L the interpreter.
 * @param name the property's name, already normalised.
 * @param idx the stack index of its value.
 * @returns a Property the caller owns, or NULL when the value is of no type
 * this understands -- which is an error the caller reports with the name.
 */
static Property *property_from_lua(lua_State *L, const char *name, int idx) {
  Property *p = NULL;
  const char *kind = tagged_kind(L, idx);

  if (kind != NULL) {
    if (!strcmp(kind, "length")) {
      /* Action purpose: each edge gets its OWN copy of the distance. A
       * SofiDistance can own a tree of SofiDistanceUnit, and the free walks all
       * four edges, so sharing one tree between them frees it four times. */
      SofiDistance d;

      if (!distance_from_table(L, idx, &d)) {
        return NULL;
      }

      p = sofi_theme_property_create(P_PADDING);
      p->value.padding.top = d;
      p->value.padding.right = sofi_theme_property_copy_distance(d);
      p->value.padding.bottom = sofi_theme_property_copy_distance(d);
      p->value.padding.left = sofi_theme_property_copy_distance(d);
    } else if (!strcmp(kind, "colour")) {
      p = sofi_theme_property_create(P_COLOR);
      colour_from_table(L, idx, &p->value.color);
    } else if (!strcmp(kind, "transparent")) {
      p = sofi_theme_property_create(P_COLOR);
      p->value.color.red = 0.0;
      p->value.color.green = 0.0;
      p->value.color.blue = 0.0;
      p->value.color.alpha = 0.0;
    } else if (!strcmp(kind, "inherit")) {
      p = sofi_theme_property_create(P_INHERIT);
    } else if (!strcmp(kind, "orientation")) {
      p = sofi_theme_property_create(P_ORIENTATION);
      p->value.i = tagged_int(L, idx);
    } else if (!strcmp(kind, "cursor")) {
      p = sofi_theme_property_create(P_CURSOR);
      p->value.i = tagged_int(L, idx);
    } else if (!strcmp(kind, "position")) {
      p = sofi_theme_property_create(P_POSITION);
      p->value.i = tagged_int(L, idx);
    } else if (!strcmp(kind, "string")) {
      p = sofi_theme_property_create(P_STRING);
      lua_getfield(L, idx, "value");
      p->value.s = g_strdup(lua_tostring(L, -1));
      lua_pop(L, 1);
    } else if (!strcmp(kind, "style")) {
      /* A lone style constant is a highlight of one bit, so `highlight = bold`
       * needs no call. */
      p = sofi_theme_property_create(P_HIGHLIGHT);
      p->value.highlight.style = (SofiHighlightStyle)tagged_int(L, idx);
      p->value.highlight.color.red = 0.0;
      p->value.highlight.color.green = 0.0;
      p->value.highlight.color.blue = 0.0;
      p->value.highlight.color.alpha = 1.0;
    } else if (!strcmp(kind, "highlight")) {
      p = sofi_theme_property_create(P_HIGHLIGHT);
      lua_getfield(L, idx, "style");
      p->value.highlight.style = (SofiHighlightStyle)lua_tointeger(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, idx, "colour");
      colour_from_table(L, lua_gettop(L), &p->value.highlight.color);
      lua_pop(L, 1);
    } else if (!strcmp(kind, "image")) {
      p = sofi_theme_property_create(P_IMAGE);
      image_from_table(L, idx, &p->value.image);
    } else if (!strcmp(kind, "list")) {
      lua_getfield(L, idx, "value");
      p = property_list_from_array(L, name, lua_gettop(L));
      lua_pop(L, 1);

      return p;
    } else if (!strcmp(kind, "ref")) {
      p = sofi_theme_property_create(P_LINK);
      lua_getfield(L, idx, "name");
      p->value.link.name = g_strdup(lua_tostring(L, -1));
      lua_pop(L, 1);
      p->value.link.ref = NULL;
      p->value.link.def_value = NULL;

      lua_getfield(L, idx, "default");

      if (!lua_isnil(L, -1)) {
        /* The fallback is read as an ordinary value and kept unnamed, the way
         * the previous reader left it: the name belongs to the reference. */
        p->value.link.def_value =
            property_from_lua(L, NULL, lua_gettop(L));
      }

      lua_pop(L, 1);
    }

    if (p != NULL) {
      p->name = g_strdup(name);
    }

    return p;
  }

  switch (lua_type(L, idx)) {
  case LUA_TBOOLEAN:
    p = sofi_theme_property_create(P_BOOLEAN);
    p->value.b = lua_toboolean(L, idx);
    break;

  case LUA_TNUMBER:
    /* Action purpose: an integral number is an integer, which matters because
     * the engine reads `lines` and `columns` as P_INTEGER and an unexpected
     * P_DOUBLE there is a type error at use rather than at read. */
    if (lua_isinteger(L, idx)) {
      p = sofi_theme_property_create(P_INTEGER);
      p->value.i = (int)lua_tointeger(L, idx);
    } else {
      p = sofi_theme_property_create(P_DOUBLE);
      p->value.f = lua_tonumber(L, idx);
    }
    break;

  case LUA_TSTRING: {
    const char *s = lua_tostring(L, idx);

    if (s[0] == '#') {
      ThemeColor c;

      if (!colour_from_hex(s, &c)) {
        return NULL;
      }

      p = sofi_theme_property_create(P_COLOR);
      p->value.color = c;
    } else if (s[0] == '@') {
      p = sofi_theme_property_create(P_LINK);
      p->value.link.name = g_strdup(s + 1);
      p->value.link.ref = NULL;
      p->value.link.def_value = NULL;
    } else {
      p = sofi_theme_property_create(P_STRING);
      p->value.s = g_strdup(s);
    }
    break;
  }

  case LUA_TTABLE:
    /* An untagged table is padding when it holds lengths or numbers and a list
     * when it holds strings, which is how the old format's `6px 10px` and
     * `[ "a", "b" ]` arrive here without needing two spellings. */
    p = property_from_array(L, name, idx);

    return p;

  default:
    return NULL;
  }

  p->name = g_strdup(name);

  return p;
}

/**
 * Function purpose: an untagged array becomes padding or a list, decided by
 * what is in it rather than by the property it is for -- the engine has no
 * property-name-to-type map, so what the value holds is all there is to go on.
 *
 * Lengths or numbers give padding, by the CSS count: one part for all four
 * edges, two for vertical and horizontal, three for top, horizontal and
 * bottom, four clockwise from the top. Strings give a list. An empty array is
 * an empty list, because `children = {}` is a real statement -- a widget with
 * nothing in it -- and guessing padding from it would be wrong.
 *
 * @returns a Property the caller owns, or NULL when the array mixes kinds or
 * holds a count padding has no meaning for.
 */
static Property *property_from_array(lua_State *L, const char *name, int idx) {
  lua_Unsigned n = lua_rawlen(L, idx);
  Property *p = NULL;
  gboolean lengths = TRUE;
  gboolean strings = TRUE;

  for (lua_Unsigned i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, (lua_Integer)i);

    const char *kind = tagged_kind(L, -1);
    int type = lua_type(L, -1);

    if (!(type == LUA_TNUMBER || (kind != NULL && !strcmp(kind, "length")))) {
      lengths = FALSE;
    }
    if (type != LUA_TSTRING) {
      strings = FALSE;
    }

    lua_pop(L, 1);
  }

  if (n == 0 || (strings && !lengths)) {
    return property_list_from_array(L, name, idx);
  }

  if (!lengths || (n != 1 && n != 2 && n != 3 && n != 4)) {
    return NULL;
  }

  SofiDistance part[4];
  lua_Unsigned read = 0;
  gboolean ok = TRUE;

  while (ok && read < n) {
    lua_rawgeti(L, idx, (lua_Integer)(read + 1));

    if (lua_type(L, -1) == LUA_TNUMBER) {
      distance_from_number(lua_tonumber(L, -1), &part[read]);
    } else {
      ok = distance_from_table(L, -1, &part[read]);
    }

    lua_pop(L, 1);
    read++;
  }

  /* The parts read before the failure own trees of their own, and nothing has
   * taken them over yet. */
  if (!ok) {
    for (lua_Unsigned i = 0; i < read; i++) {
      distance_unit_drop(part[i].base.left);
      distance_unit_drop(part[i].base.right);
    }

    return NULL;
  }

  p = sofi_theme_property_create(P_PADDING);

  /* Action purpose: an edge that repeats a part gets a COPY of it, not the same
   * tree again. The free walks all four edges, so a shared tree is freed once
   * per edge that points at it -- the same reason the previous reader called
   * sofi_theme_property_copy_distance() in exactly these three cases. */
  switch (n) {
  case 1:
    p->value.padding.top = part[0];
    p->value.padding.right = sofi_theme_property_copy_distance(part[0]);
    p->value.padding.bottom = sofi_theme_property_copy_distance(part[0]);
    p->value.padding.left = sofi_theme_property_copy_distance(part[0]);
    break;
  case 2:
    p->value.padding.top = part[0];
    p->value.padding.right = part[1];
    p->value.padding.bottom = sofi_theme_property_copy_distance(part[0]);
    p->value.padding.left = sofi_theme_property_copy_distance(part[1]);
    break;
  case 3:
    p->value.padding.top = part[0];
    p->value.padding.right = part[1];
    p->value.padding.bottom = part[2];
    p->value.padding.left = sofi_theme_property_copy_distance(part[1]);
    break;
  default:
    p->value.padding.top = part[0];
    p->value.padding.right = part[1];
    p->value.padding.bottom = part[2];
    p->value.padding.left = part[3];
    break;
  }

  p->name = g_strdup(name);

  return p;
}

/**
 * Function purpose: normalise a property name. The manual writes `text-color`,
 * which Lua can only spell as `["text-color"]`, while `text_color` is a bare
 * identifier and is what a Lua table wants to be written with. Both reach the
 * same property -- the hyphenated spelling is the one the engine knows, so the
 * underscore is translated towards it and not the other way round.
 *
 * @returns a name the caller frees.
 */
static char *property_name(const char *key) {
  char *name = g_strdup(key);

  for (char *c = name; *c != '\0'; c++) {
    if (*c == '_') {
      *c = '-';
    }
  }

  return name;
}

/**
 * Function purpose: split a selector into the name path the engine walks.
 *
 * Action purpose: `.` and WHITESPACE are both separators, which is what the old
 * lexer's T_NSEP matched, so `element normal.urgent` is the three-step path
 * element -> normal -> urgent and not one name with punctuation in it. Getting
 * this wrong would put every stateful rule under a widget nothing looks for.
 *
 * @returns a NULL-terminated vector the caller frees with g_strfreev().
 */
static char **selector_path(const char *selector) {
  char **raw = g_strsplit_set(selector, " \t\n.", -1);
  GPtrArray *parts = g_ptr_array_new();

  for (int i = 0; raw[i] != NULL; i++) {
    if (raw[i][0] != '\0') {
      g_ptr_array_add(parts, g_strdup(raw[i]));
    }
  }

  g_strfreev(raw);
  g_ptr_array_add(parts, NULL);

  return (char **)g_ptr_array_free(parts, FALSE);
}

/**
 * Function purpose: apply one rule's properties to one selector.
 *
 * Action purpose: a `*` at the TOP LEVEL goes on the root's own property table
 * and not into a child widget called `*`, and this is not cosmetic. Two things
 * read that table and only that table: sofi_theme_resolve_link_property()
 * looks a `@name` reference up in `sofi_theme->properties` (source/theme.c),
 * and sofi_theme_find_property() walks a widget's PARENT chain, which a
 * sibling named `*` is not on. A `*` block filed as a child would therefore
 * leave every reference unresolved and every default unreachable, while still
 * printing as `* { }` in a dump -- a break that is invisible in the one place
 * you would look for it.
 *
 * The previous reader arrived at the same place by a different route: it built
 * a temporary tree in which `*` WAS a child, then
 * sofi_theme_parse_merge_widgets() hoisted it onto the root as a special case
 * when the parent was sofi_theme. That special case is conditional on the
 * parent, so inside a `@media` block a `*` stays a child -- and so it does
 * here, which is why this tests the root rather than the selector alone.
 */
static void rule_apply(ThemeWidget *root, const char *selector,
                       GHashTable *props) {
  char **path = selector_path(selector);

  if (root == sofi_theme && path[0] != NULL && path[1] == NULL &&
      strcmp(path[0], "*") == 0) {
    g_strfreev(path);
    sofi_theme_widget_add_properties(root, props);

    return;
  }

  ThemeWidget *wid = root;

  for (int i = 0; path[i] != NULL && wid != NULL; i++) {
    wid = sofi_theme_find_or_create_name(wid, path[i]);
  }

  g_strfreev(path);

  if (wid == NULL) {
    return;
  }

  wid->set = TRUE;
  sofi_theme_widget_add_properties(wid, props);
}

/**
 * Function purpose: collect a rule's property keys into the table the engine
 * merges.
 *
 * Action purpose: the five reserved keys are skipped because they name what the
 * directive IS rather than a property of it. None of them is the name of a
 * property: checked against every name the shipped layouts, the manual and
 * config/config.c use.
 *
 * @returns a table the caller destroys, or NULL when the rule carried no
 * properties, which the engine reads as "nothing to merge".
 */
static GHashTable *rule_properties(lua_State *L, int idx, const char *where) {
  static const char *const reserved[] = {"select",        "reset", "media",
                                         "configuration", "rules", TAG_FIELD,
                                         NULL};
  GHashTable *props = NULL;

  lua_pushnil(L);

  while (lua_next(L, idx) != 0) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      lua_pop(L, 1);
      continue;
    }

    const char *key = lua_tostring(L, -2);
    gboolean skip = FALSE;

    for (int i = 0; reserved[i] != NULL; i++) {
      if (!strcmp(key, reserved[i])) {
        skip = TRUE;

        break;
      }
    }

    if (skip) {
      lua_pop(L, 1);
      continue;
    }

    char *name = property_name(key);
    Property *p = property_from_lua(L, name, lua_gettop(L));

    if (p == NULL) {
      g_warning("%s: `%s` is not a value this understands; ignoring it.", where,
                name);
    } else {
      if (props == NULL) {
        props = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                      (GDestroyNotify)sofi_theme_property_free);
      }
      g_hash_table_replace(props, p->name, p);
    }

    g_free(name);
    lua_pop(L, 1);
  }

  return props;
}

/* Action purpose: a string that has to stay a string even though it begins with
 * `#` or `@`. Those two prefixes are what make a colour and a reference, so
 * without this escape hatch a literal "#channel" would be an unparseable
 * colour rather than text. */
static int value_str(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);

  tagged_new(L, "string");
  lua_pushstring(L, s);
  lua_setfield(L, -2, "value");

  return 1;
}

/**
 * Function purpose: read another theme file and return its directives, so a
 * theme can be assembled out of pieces.
 *
 * Action purpose: this is the one reader in the project that is allowed to open
 * a file named by the configuration, and the reason is that the file is the
 * theme author's own. The compositor's reader refuses the equivalent because
 * there the naming file is a FOREIGN one -- hikari.lua belongs to another
 * program -- and a foreign file naming paths for sofi to read is a different
 * proposition from a theme naming the palette it is built on. The previous
 * format allowed it too, and sofi-config/config.lua depends on it.
 *
 * The chunk is run in a FRESH sandbox with its own budget, so an imported file
 * cannot exhaust the importer's allowance or see its locals. It returns the
 * imported chunk's table, which the caller puts in its own array and
 * apply_directives() then walks in place -- so an import applies exactly where
 * it is written, which is what the cascade needs.
 *
 * The second argument makes the import optional: a missing file is then an
 * empty table rather than an error, which is what `?import` did.
 */
static int theme_import(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  gboolean optional = lua_toboolean(L, 2);

  lua_getfield(L, LUA_REGISTRYINDEX, IMPORT_DEPTH_KEY);
  int depth = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);

  if (depth >= IMPORT_DEPTH) {
    return luaL_error(L, "imports are more than %d deep; `%s` was not read",
                      IMPORT_DEPTH, name);
  }

  lua_getfield(L, LUA_REGISTRYINDEX, IMPORT_PATH_KEY);
  char *path = helper_get_theme_path(name, lua_theme_file_extensions,
                                     lua_tostring(L, -1));
  lua_pop(L, 1);

  if (path == NULL || !g_file_test(path, G_FILE_TEST_EXISTS)) {
    g_free(path);

    if (optional) {
      lua_createtable(L, 0, 0);

      return 1;
    }

    return luaL_error(L, "`%s` could not be found", name);
  }

  int status = luaL_loadfilex(L, path, "t");

  if (status != LUA_OK) {
    g_free(path);

    if (optional) {
      lua_pop(L, 1);
      lua_createtable(L, 0, 0);

      return 1;
    }

    return lua_error(L);
  }

  /* The imported chunk shares this one's environment, and therefore its
   * allocation ceiling and its instruction budget: one theme gets one
   * allowance however many files it is written in. */
  lua_getfield(L, LUA_REGISTRYINDEX, SANDBOX_KEY);

  if (lua_setupvalue(L, -2, 1) == NULL) {
    lua_pop(L, 1);
  }

  /* The importer's own path is tucked under the chunk so a relative import
   * inside the imported file resolves against ITS directory, and the
   * importer's resolves against its own again once this returns. */
  int chunk = lua_gettop(L);

  lua_getfield(L, LUA_REGISTRYINDEX, IMPORT_PATH_KEY);
  lua_insert(L, chunk);

  lua_pushstring(L, path);
  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_PATH_KEY);
  lua_pushinteger(L, depth + 1);
  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_DEPTH_KEY);
  g_free(path);

  /* An unprotected call on purpose: import() is only reachable from inside the
   * chunk, which is itself running under lua_pcall(), so an error here unwinds
   * to that and is reported as the whole theme failing -- which is what a
   * broken import is. */
  lua_call(L, 0, 1);

  lua_pushvalue(L, chunk);
  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_PATH_KEY);
  lua_pushinteger(L, depth);
  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_DEPTH_KEY);
  lua_remove(L, chunk);

  if (lua_type(L, -1) != LUA_TTABLE) {
    return luaL_error(L, "`%s` did not return a table of directives", name);
  }

  return 1;
}

/**
 * Function purpose: the environment a theme is evaluated in.
 *
 * Action purpose: a theme is now a PROGRAM, and a theme is the thing users
 * download from each other -- so this is the reader that most needs the bounds
 * the compositor's reader already has. It gets the same bounded string library,
 * the same allocation ceiling and the same instruction budget, and it gets them
 * from the same place rather than from a second copy: a bound that is applied
 * to one reader and not the other is not a bound.
 *
 * `io`, `os`, `package`, `require`, `load` and `dofile` are not reachable. A
 * theme computes colours and lengths; nothing it legitimately does needs a file
 * or a process.
 *
 * Leaves the sandbox on the top of the stack.
 */
static void push_sandbox(lua_State *L) {
  static const char *const base[] = {"assert",   "error", "ipairs",
                                     "next",     "pairs", "rawequal",
                                     "rawget",   "rawlen", "rawset",
                                     "select",   "tonumber", "tostring",
                                     "type",     NULL};
  static const struct {
    const char *name;
    int unit;
  } units[] = {
      {"px", SOFI_PU_PX},   {"em", SOFI_PU_EM},
      {"ch", SOFI_PU_CH},   {"mm", SOFI_PU_MM},
      {"percent", SOFI_PU_PERCENT}, {NULL, 0},
  };

  luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
  lua_pop(L, 1);

  lua_newtable(L);

  for (int i = 0; base[i] != NULL; i++) {
    lua_getglobal(L, base[i]);
    lua_setfield(L, -2, base[i]);
  }

  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 0);
  lua_setfield(L, -2, "math");
  sofi_hikari_lua_push_bounded_table(L);
  lua_setfield(L, -2, "table");

  sofi_hikari_lua_push_bounded_string(L);
  lua_setfield(L, -2, LUA_STRLIBNAME);

  sofi_hikari_lua_push_env(L);
  lua_setfield(L, -2, "env");

  for (int i = 0; units[i].name != NULL; i++) {
    lua_pushinteger(L, units[i].unit);
    lua_pushcclosure(L, length_new, 1);
    lua_setfield(L, -2, units[i].name);
  }

  static const struct {
    const char *name;
    lua_CFunction fn;
  } ctor[] = {
      /* `rgb` and `rgba` are one function: the fourth argument is optional, so
       * the two names the previous format needed are one here. */
      {"rgb", colour_rgba},
      {"rgba", colour_rgba},
      {"hsl", colour_hsl},
      {"hsla", colour_hsl},
      {"hwb", colour_hwb},
      {"cmyk", colour_cmyk},
      {"color", colour_named},
      {"highlight", value_highlight},
      {"url", image_url},
      {"linear_gradient", image_linear_gradient},
      {"str", value_str},
      {"list", value_list},
      {"ref", value_ref},
      {"import", theme_import},
      {NULL, NULL},
  };

  for (int i = 0; ctor[i].name != NULL; i++) {
    lua_pushcfunction(L, ctor[i].fn);
    lua_setfield(L, -2, ctor[i].name);
  }

  push_calc(L);
  lua_setfield(L, -2, "calc");

  /* The metatable is hidden for the reason env's is: a theme that could replace
   * `__index` could switch this off, and then strictness lasts exactly as long
   * as nobody tries. */
  lua_createtable(L, 0, 2);
  lua_pushcfunction(L, sandbox_undefined);
  lua_setfield(L, -2, "__index");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);

  for (int i = 0; constants[i].name != NULL; i++) {
    tagged_new(L, constants[i].kind);
    lua_pushinteger(L, constants[i].value);
    lua_setfield(L, -2, "value");
    lua_setfield(L, -2, constants[i].name);
  }
}

/**
 * Function purpose: refuse a read of a name the sandbox does not define.
 *
 * Action purpose: without this, a misspelled constant is the quietest possible
 * failure. Reading an undefined global gives nil, and `{ highlight = nil }`
 * does not create the key at all, so `highlight = bolt` reaches the reader as
 * a rule with no properties -- no error, no warning, nothing. That is worse
 * than the silent string property the named constants were introduced to avoid,
 * and for a while the comment above them claimed the opposite was true.
 *
 * A theme has only the names this sandbox installs plus its own locals, so a
 * free name that is not one of them is a mistake every time.
 */
static int sandbox_undefined(lua_State *L) {
  const char *name = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : NULL;

  if (name == NULL) {
    return luaL_error(L, "a theme indexed its environment with a %s",
                      luaL_typename(L, 2));
  }

  return luaL_error(L, "`%s` is not a name a theme can use", name);
}

/* Action purpose: push_sandbox() as something lua_pcall() can run, for the
 * reason its siblings in source/hikari-palette.c and source/anim.c have one:
 * every step of building it allocates, an allocation the ceiling refuses raises
 * LUA_ERRMEM, and a raise with nothing protected beneath it reaches Lua's panic
 * function and abort()s. A theme sofi means to decline must not end sofi. */
static int push_sandbox_protected(lua_State *L) {
  push_sandbox(L);

  return 1;
}

/* Action purpose: a theme that loops is stopped the same way a compositor
 * configuration that loops is. luaL_error from inside the hook unwinds into the
 * pcall running the chunk, so it is reported by the same branch as a syntax
 * error and the previous theme stands. */
static void step_hook(lua_State *L, G_GNUC_UNUSED lua_Debug *activation) {
  luaL_error(L, "the theme did not finish");
}

/**
 * Function purpose: apply one rule to its target, which is either one selector
 * or an array of them.
 *
 * @returns the number of selectors the rule reached.
 */
static int rule_apply_target(lua_State *L, int target, ThemeWidget *root,
                             GHashTable *props) {
  int applied = 0;

  if (lua_type(L, target) == LUA_TSTRING) {
    rule_apply(root, lua_tostring(L, target), props);

    return 1;
  }

  if (lua_type(L, target) != LUA_TTABLE) {
    return 0;
  }

  lua_Unsigned n = lua_rawlen(L, target);

  for (lua_Unsigned i = 1; i <= n; i++) {
    lua_rawgeti(L, target, (lua_Integer)i);

    if (lua_type(L, -1) == LUA_TSTRING) {
      rule_apply(root, lua_tostring(L, -1), props);
      applied++;
    }

    lua_pop(L, 1);
  }

  return applied;
}

/**
 * Function purpose: apply a `configuration` block.
 *
 * Action purpose: a configuration block does NOT go into the theme tree. Its
 * scalar properties go through config_parse_set_property(), which is the same
 * setter the command line and Xresources reach, and its nested blocks go into
 * the separate sofi_configuration tree -- exactly the two destinations the
 * previous format's `configuration { }` had. Writing these as ordinary rules
 * would file them under a theme widget called "configuration" that nothing ever
 * looks up, which is a silent no-op and the worst possible outcome.
 *
 * A value is a nested block when it is a plain table with no array part: that
 * is the one shape no property value has, since padding and lists both have
 * one and every constructed value carries a tag.
 */
static void apply_configuration(lua_State *L, int idx, const char *where) {
  if (sofi_configuration == NULL) {
    sofi_configuration = g_slice_new0(ThemeWidget);
    sofi_configuration->name = g_strdup("Root");
  }

  lua_pushnil(L);

  while (lua_next(L, idx) != 0) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      lua_pop(L, 1);

      continue;
    }

    char *name = property_name(lua_tostring(L, -2));
    int value = lua_gettop(L);

    if (lua_type(L, value) == LUA_TTABLE && tagged_kind(L, value) == NULL &&
        lua_rawlen(L, value) == 0) {
      GHashTable *props = rule_properties(L, value, where);
      ThemeWidget *wid = sofi_theme_find_or_create_name(sofi_configuration,
                                                        name);

      if (wid != NULL) {
        wid->set = TRUE;
        sofi_theme_widget_add_properties(wid, props);
      }

      if (props != NULL) {
        g_hash_table_destroy(props);
      }
    } else {
      Property *p = property_from_lua(L, name, value);

      if (p == NULL) {
        g_warning("%s: configuration `%s` is not a value this understands.",
                  where, name);
      } else {
        char *error = NULL;

        if (config_parse_set_property(p, &error)) {
          g_warning("%s: configuration `%s`: %s", where, name,
                    error != NULL ? error : "rejected");
          g_free(error);
        }

        /* The setter copies what it keeps, which is why the parser freed its
         * property here too. */
        sofi_theme_property_free(p);
      }
    }

    g_free(name);
    lua_pop(L, 1);
  }
}

/**
 * Function purpose: build the widget a `@media` block became, named exactly as
 * the previous reader named it so a dump of the same theme reads the same.
 *
 * @returns the widget, or NULL when the block named no media type this
 * understands.
 */
static ThemeWidget *media_widget(lua_State *L, int idx, const char *where) {
  lua_getfield(L, idx, "type");
  const char *type = lua_tostring(L, -1);

  if (type == NULL) {
    g_warning("%s: a media block has no `type`.", where);
    lua_pop(L, 1);

    return NULL;
  }

  ThemeMediaType kind = sofi_theme_parse_media_type(type);

  if (kind == THEME_MEDIA_TYPE_INVALID) {
    g_warning("%s: `%s` is not a media type. Known: monitor-id, min-width, "
              "max-width, min-height, max-height, min-aspect-ratio, "
              "max-aspect-ratio, enabled.",
              where, type);
    lua_pop(L, 1);

    return NULL;
  }

  lua_getfield(L, idx, "value");

  char *name = NULL;
  double value = 0.0;
  gboolean boolv = FALSE;

  if (lua_isboolean(L, -1)) {
    boolv = lua_toboolean(L, -1);
    name = g_strdup_printf("@media ( %s: %s )", type, boolv ? "true" : "false");
  } else if (lua_isinteger(L, -1)) {
    value = (double)lua_tointeger(L, -1);
    name = g_strdup_printf("@media ( %s: %d px )", type,
                           (int)lua_tointeger(L, -1));
  } else {
    value = lua_tonumber(L, -1);
    name = g_strdup_printf("@media ( %s: %f )", type, value);
  }

  lua_pop(L, 2);

  ThemeWidget *wid = sofi_theme_find_or_create_name(sofi_theme, name);
  g_free(name);

  if (wid == NULL) {
    return NULL;
  }

  wid->set = TRUE;

  if (wid->media == NULL) {
    wid->media = g_slice_new0(ThemeMedia);
  }

  wid->media->type = kind;
  wid->media->value = value;
  wid->media->boolv = boolv;

  return wid;
}

/**
 * Function purpose: walk the returned array in order and carry out every
 * directive in it.
 *
 * Action purpose: ONE ordered array holds everything a theme says, because the
 * format before this one also put its media blocks, its configuration blocks
 * and its theme resets in amongst its rules, and the cascade is source order.
 * A separate top-level key per kind would have lost the order between kinds.
 *
 * An element is read as, in this order of precedence:
 *
 *   `reset`         -- throw the whole tree away and start again, which is
 *                      what `@theme` did before importing
 *   `configuration` -- a configuration block, as above
 *   `media`         -- a media block, its `rules` applied inside it
 *   `select`        -- a rule
 *   an array part   -- a nested list of directives, applied in order; this is
 *                      what import() returns and what lets a theme be built
 *                      out of pieces
 *
 * Action purpose: `lua_rawgeti` and `lua_rawlen` rather than their non-raw
 * forms, and for the reason saber's reader documents: this runs AFTER the
 * chunk's pcall has returned, so there is no protected frame around it, and a
 * metamethod that raised here would reach Lua's panic handler and abort(). The
 * chunk decides what table comes back, so it can hand over one whose metatable
 * errors on a missing key. A raw access cannot call anything.
 *
 * @returns the number of directives carried out.
 */
static int apply_directives(lua_State *L, int idx, ThemeWidget *root,
                            const char *where, int depth) {
  lua_Unsigned n = lua_rawlen(L, idx);
  int applied = 0;

  if (depth >= IMPORT_DEPTH) {
    g_warning("%s: directives nest more than %d deep; stopping here.", where,
              IMPORT_DEPTH);

    return 0;
  }

  /* This walk also holds each level's table on the Lua stack while it
   * recurses; see the note in distance_unit_from_table() for why reserving the
   * room is not optional. */
  if (!lua_checkstack(L, 8)) {
    g_warning("%s: no room left to read it.", where);

    return 0;
  }

  for (lua_Unsigned i = 1; i <= n; i++) {
    lua_rawgeti(L, idx, (lua_Integer)i);

    if (lua_type(L, -1) != LUA_TTABLE) {
      g_warning("%s: directive %lu is a %s and not a table; ignoring it.",
                where, (unsigned long)i, luaL_typename(L, -1));
      lua_pop(L, 1);

      continue;
    }

    int element = lua_gettop(L);

    lua_pushliteral(L, "reset");
    lua_rawget(L, element);
    gboolean reset = lua_toboolean(L, -1);
    lua_pop(L, 1);

    if (reset) {
      sofi_theme_reset();
      root = sofi_theme;
      applied++;
      lua_pop(L, 1);

      continue;
    }

    lua_pushliteral(L, "configuration");
    lua_rawget(L, element);

    if (lua_type(L, -1) == LUA_TTABLE) {
      apply_configuration(L, lua_gettop(L), where);
      applied++;
      lua_pop(L, 2);

      continue;
    }

    lua_pop(L, 1);

    lua_pushliteral(L, "media");
    lua_rawget(L, element);

    if (lua_type(L, -1) == LUA_TTABLE) {
      ThemeWidget *inner = media_widget(L, lua_gettop(L), where);
      lua_pop(L, 1);

      if (inner != NULL) {
        lua_pushliteral(L, "rules");
        lua_rawget(L, element);

        if (lua_type(L, -1) == LUA_TTABLE) {
          applied += apply_directives(L, lua_gettop(L), inner, where,
                                      depth + 1);
        } else {
          g_warning("%s: a media block has no `rules` array.", where);
        }

        lua_pop(L, 1);
      }

      lua_pop(L, 1);

      continue;
    }

    lua_pop(L, 1);

    lua_pushliteral(L, "select");
    lua_rawget(L, element);

    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);

      /* No `select` and an array part of its own: a nested list. */
      if (lua_rawlen(L, element) > 0) {
        applied += apply_directives(L, element, root, where, depth + 1);
      } else {
        /* An entirely empty table is a directive list with nothing in it,
         * which is what an optional import of a file that is not there
         * returns. That is the whole point of an optional import, so it is a
         * silent no-op rather than a complaint. */
        lua_pushnil(L);

        if (lua_next(L, element) != 0) {
          lua_pop(L, 2);
          g_warning("%s: directive %lu names nothing; ignoring it.", where,
                    (unsigned long)i);
        }
      }

      lua_pop(L, 1);

      continue;
    }

    GHashTable *props = rule_properties(L, element, where);

    applied += rule_apply_target(L, lua_gettop(L), root, props);

    if (props != NULL) {
      g_hash_table_destroy(props);
    }

    lua_pop(L, 2);
  }

  return applied;
}

/**
 * Function purpose: read a theme out of one Lua chunk, which is the single
 * place both public entry points meet.
 *
 * Action purpose: nothing is applied until the chunk has run and returned an
 * array. A theme that fails to load, fails to run or returns the wrong shape
 * leaves the tree exactly as it was, because half a theme is worse than the one
 * it was replacing -- and this is also what lets a reload of a file a user has
 * just broken keep the panel on screen.
 *
 * @returns TRUE on FAILURE, which is the convention the previous reader's
 * sofi_theme_parse_string() set and every one of its fifteen call sites tests.
 */
static gboolean theme_lua_read(const char *chunk, const char *path) {
  SofiHikariLuaBudget budget;
  lua_State *L = sofi_hikari_lua_newstate(&budget);

  if (L == NULL) {
    theme_lua_warn(path, "no interpreter could be built for it.");

    return TRUE;
  }

  int status = path != NULL ? luaL_loadfilex(L, path, "t")
                            : luaL_loadbufferx(L, chunk, strlen(chunk),
                                               "=theme", "t");

  if (status != LUA_OK) {
    theme_lua_warn(path, lua_tostring(L, -1));
    lua_close(L);

    return TRUE;
  }

  lua_pushcfunction(L, push_sandbox_protected);

  if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
    theme_lua_warn(path, lua_tostring(L, -1));
    lua_close(L);

    return TRUE;
  }

  /* The sandbox is kept where import() can find it, so an imported file shares
   * this one's environment and therefore its budget. */
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, SANDBOX_KEY);

  if (path != NULL) {
    lua_pushstring(L, path);
  } else {
    lua_pushnil(L);
  }

  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_PATH_KEY);
  lua_pushinteger(L, 0);
  lua_setfield(L, LUA_REGISTRYINDEX, IMPORT_DEPTH_KEY);

  /* _ENV is always upvalue 1 of a main chunk. */
  if (lua_setupvalue(L, -2, 1) == NULL) {
    theme_lua_warn(path, "it is not a chunk.");
    lua_close(L);

    return TRUE;
  }

  lua_sethook(L, step_hook, LUA_MASKCOUNT, THEME_LUA_STEPS);

  status = lua_pcall(L, 0, 1, 0);

  lua_sethook(L, NULL, 0, 0);

  /* Action purpose: the ceiling is lifted the moment the theme stops running,
   * because the walk below reads the returned table with no protected frame
   * under it -- an allocation refused there would abort rather than decline. */
  sofi_hikari_lua_release(&budget);

  if (status != LUA_OK) {
    theme_lua_warn(path, lua_tostring(L, -1));
    lua_close(L);

    return TRUE;
  }

  if (lua_type(L, -1) != LUA_TTABLE) {
    theme_lua_warn(path, "it did not return a table of rules.");
    lua_close(L);

    return TRUE;
  }

  /* Action purpose: the tree is created here if nothing has created it yet, for
   * the reason the previous reader's own top-level rule did it: the first
   * source parsed in a run may be this one, and every path below writes into
   * the root. */
  if (sofi_theme == NULL) {
    sofi_theme_reset();
  }

  apply_directives(L, lua_gettop(L), sofi_theme,
                   path != NULL ? path : "theme", 0);
  lua_close(L);

  return FALSE;
}

/* Action purpose: a name without an extension is resolved the way a theme name
 * has always been resolved -- `.lua` then `.luainc`, in the theme directories
 * -- so `-theme sakura` still finds an installed theme rather than needing a
 * path. The name is used as given when it already resolves. */
gboolean sofi_theme_parse_file(const char *file) {
  char *resolved =
      helper_get_theme_path(file, lua_theme_file_extensions, NULL);
  gboolean failed = theme_lua_read(NULL, resolved != NULL ? resolved : file);

  g_free(resolved);

  return failed;
}

gboolean sofi_theme_parse_string(const char *string) {
  return theme_lua_read(string, NULL);
}

gboolean sofi_theme_try_string(const char *string) {
  theme_lua_quiet = TRUE;

  gboolean failed = theme_lua_read(string, NULL);

  theme_lua_quiet = FALSE;

  return failed;
}
