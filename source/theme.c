/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2013-2023 Qball Cow <qball@gmpclient.org>
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

/** Log domain used by the theme engine.*/
#define G_LOG_DOMAIN "Theme"

#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// GFile stuff.
#include "display.h"
#include "css-colors.h"
#include "helper.h"
#include "sofi-icon-fetcher.h"
#include "sofi-types.h"
#include "sofi.h"
#include "settings.h"
#include "theme.h"
#include "view.h"
#include "widgets/textbox.h"
#include <gio/gio.h>

/* Action purpose: the theme tree itself. It used to live in the generated
 * parser, which was the one translation unit guaranteed to be linked anywhere a
 * theme could be read. With that gone it belongs to the engine that creates,
 * walks and frees it -- which is also the only place a reader-independent home
 * for it exists. */
ThemeWidget *sofi_theme = NULL;

/**
 * list of config files we parsed.
 */
GList *parsed_config_files = NULL;
static disp_scale_func disp_scale = NULL;

/** cleanup (free) the list of parsed config files. */
void sofi_theme_free_parsed_files(void) {
  g_list_free_full(parsed_config_files, g_free);
  parsed_config_files = NULL;
}

/**
 * @param is_term if print to terminal
 *
 * print the list of parsed config files.
 */
void sofi_theme_print_parsed_files(gboolean is_term) {
  printf("\nParsed files:\n");
  for (GList *iter = g_list_first(parsed_config_files); iter != NULL;
       iter = g_list_next(iter)) {
    printf("\t\u2023 %s%s%s\n", is_term ? color_bold : "",
           (const char *)(iter->data), is_term ? color_reset : "");
  }
  printf("\n");
}

/**
 * Function purpose: whether two nodes of a length's arithmetic are the same
 * node, operands and all.
 *
 * Action purpose: the whole TREE is compared and not just its root, because a
 * modifier node carries distance 0 and type px whatever it computes -- so
 * comparing the root alone made `percent(100) - px(10)` and `px(5) + px(2)`
 * equal. The padding printer uses this to decide how many of the four edges it
 * has to print, and it was printing one edge for four different ones.
 */
static gboolean distance_unit_compare(const SofiDistanceUnit *d,
                                      const SofiDistanceUnit *e) {
  if (d == NULL || e == NULL) {
    return d == e;
  }
  return d->modtype == e->modtype && d->type == e->type &&
         d->distance == e->distance &&
         distance_unit_compare(d->left, e->left) &&
         distance_unit_compare(d->right, e->right);
}

static gboolean distance_compare(SofiDistance d, SofiDistance e) {
  return d.style == e.style && distance_unit_compare(&(d.base), &(e.base));
}

ThemeWidget *sofi_theme_find_or_create_name(ThemeWidget *base,
                                            const char *name) {
  for (unsigned int i = 0; i < base->num_widgets; i++) {
    if (g_strcmp0(base->widgets[i]->name, name) == 0) {
      return base->widgets[i];
    }
  }

  base->widgets =
      g_realloc(base->widgets, sizeof(ThemeWidget *) * (base->num_widgets + 1));
  base->widgets[base->num_widgets] = g_slice_new0(ThemeWidget);
  ThemeWidget *retv = base->widgets[base->num_widgets];
  retv->parent = base;
  retv->name = g_strdup(name);
  base->num_widgets++;
  return retv;
}
/**
 *  Properties
 */
Property *sofi_theme_property_create(PropertyType type) {
  Property *retv = g_slice_new0(Property);
  retv->type = type;
  return retv;
}

static SofiDistanceUnit *
sofi_theme_property_copy_distance_unit(SofiDistanceUnit *unit) {
  SofiDistanceUnit *retv = g_slice_new0(SofiDistanceUnit);
  *retv = *unit;
  if (unit->left) {
    retv->left = sofi_theme_property_copy_distance_unit(unit->left);
  }
  if (unit->right) {
    retv->right = sofi_theme_property_copy_distance_unit(unit->right);
  }
  return retv;
}
SofiDistance sofi_theme_property_copy_distance(SofiDistance const distance) {
  SofiDistance retv = distance;
  if (distance.base.left) {
    retv.base.left = sofi_theme_property_copy_distance_unit(distance.base.left);
  }
  if (distance.base.right) {
    retv.base.right =
        sofi_theme_property_copy_distance_unit(distance.base.right);
  }
  return retv;
}

Property *sofi_theme_property_copy(const Property *p,
                                   G_GNUC_UNUSED void *data) {
  Property *retv = sofi_theme_property_create(p->type);
  retv->name = g_strdup(p->name);

  switch (p->type) {
  case P_STRING:
    retv->value.s = g_strdup(p->value.s);
    break;
  case P_LIST:
    retv->value.list = g_list_copy_deep(
        p->value.list, (GCopyFunc)sofi_theme_property_copy, NULL);
    break;
  case P_LINK:
    retv->value.link.name = g_strdup(p->value.link.name);
    retv->value.link.ref = NULL;
    if (p->value.link.def_value) {
      retv->value.link.def_value =
          sofi_theme_property_copy(p->value.link.def_value, NULL);
    }
    break;
  case P_PADDING: {
    retv->value = p->value;
    retv->value.padding.top =
        sofi_theme_property_copy_distance(p->value.padding.top);
    retv->value.padding.left =
        sofi_theme_property_copy_distance(p->value.padding.left);
    retv->value.padding.bottom =
        sofi_theme_property_copy_distance(p->value.padding.bottom);
    retv->value.padding.right =
        sofi_theme_property_copy_distance(p->value.padding.right);
    break;
  }
  case P_IMAGE: {
    retv->value = p->value;
    retv->value.image.url = g_strdup(p->value.image.url);
    retv->value.image.colors = NULL;
    for (GList *l = g_list_first(p->value.image.colors); l;
         l = g_list_next(l)) {
      retv->value.image.colors = g_list_append(
          retv->value.image.colors, g_memdup2(l->data, sizeof(ThemeColor)));
    }
    break;
  }
  default:
    retv->value = p->value;
  }
  return retv;
}

static void sofi_theme_distance_unit_property_free(SofiDistanceUnit *unit) {
  if (unit->left) {
    sofi_theme_distance_unit_property_free(unit->left);
    unit->left = NULL;
  }
  if (unit->right) {
    sofi_theme_distance_unit_property_free(unit->right);
    unit->right = NULL;
  }
  g_slice_free(SofiDistanceUnit, unit);
}
static void sofi_theme_distance_property_free(SofiDistance *distance) {
  if (distance->base.left) {
    sofi_theme_distance_unit_property_free(distance->base.left);
    distance->base.left = NULL;
  }
  if (distance->base.right) {
    sofi_theme_distance_unit_property_free(distance->base.right);
    distance->base.right = NULL;
  }
}

void sofi_theme_property_free(Property *p) {
  if (p == NULL) {
    return;
  }
  g_free(p->name);
  if (p->type == P_STRING) {
    g_free(p->value.s);
  } else if (p->type == P_LIST) {
    g_list_free_full(p->value.list, (GDestroyNotify)sofi_theme_property_free);
    p->value.list = 0;
  } else if (p->type == P_LINK) {
    g_free(p->value.link.name);
    if (p->value.link.def_value) {
      sofi_theme_property_free(p->value.link.def_value);
    }
  } else if (p->type == P_PADDING) {
    sofi_theme_distance_property_free(&(p->value.padding.top));
    sofi_theme_distance_property_free(&(p->value.padding.right));
    sofi_theme_distance_property_free(&(p->value.padding.bottom));
    sofi_theme_distance_property_free(&(p->value.padding.left));
  } else if (p->type == P_IMAGE) {
    if (p->value.image.url) {
      g_free(p->value.image.url);
    }
    if (p->value.image.colors) {
      g_list_free_full(p->value.image.colors, g_free);
    }
  }
  g_slice_free(Property, p);
}

void sofi_theme_reset(void) {
  sofi_theme_free(sofi_theme);
  sofi_theme = g_slice_new0(ThemeWidget);
  sofi_theme->name = g_strdup("Root");
}

void sofi_theme_free(ThemeWidget *wid) {
  if (wid == NULL) {
    return;
  }
  if (wid->properties) {
    g_hash_table_destroy(wid->properties);
    wid->properties = NULL;
  }
  if (wid->media) {
    g_slice_free(ThemeMedia, wid->media);
  }
  for (unsigned int i = 0; i < wid->num_widgets; i++) {
    sofi_theme_free(wid->widgets[i]);
  }
  g_free(wid->widgets);
  g_free(wid->name);
  g_slice_free(ThemeWidget, wid);
}

/**
 * print
 *
 * Action purpose: the dump is LUA, because Lua is the only format this binary
 * can still read. A `-dump-theme` that prints a syntax no reader accepts makes
 * the flag useless for the one job it has -- taking the theme a running
 * invocation actually resolved and handing it back as a file you can edit -- so
 * every construct printed below is one source/theme-lua.c reads. The test is
 * the round trip: a dump, read and dumped again, is byte-identical to itself.
 */

/** How far one level of nesting indents. */
#define THEME_PRINT_INDENT 2

/**
 * Function purpose: print a number so that Lua reads back the double that was
 * printed.
 *
 * Action purpose: g_ascii_formatd() rather than printf(), because the C
 * library's decimal separator is a comma across half of Europe and Lua reads
 * `0,5` as two values in an argument list. The shortest form that reads back
 * equal is chosen rather than a fixed precision, so a dump neither drops
 * digits nor prints seventeen of them for 0.5.
 *
 * @param force_float when TRUE a whole number is given a `.0`, which is what
 * keeps a P_DOUBLE from reading back as a P_INTEGER.
 */
static void sofi_theme_print_lua_number(double d, gboolean force_float) {
  char buf[G_ASCII_DTOSTR_BUF_SIZE];

  /* Neither has a literal in Lua, and a division in a theme can produce
   * both. */
  if (isnan(d)) {
    fputs("(0/0)", stdout);
    return;
  }
  if (isinf(d)) {
    fputs(d < 0 ? "-math.huge" : "math.huge", stdout);
    return;
  }

  /* Action purpose: a whole number is printed as one. `%g` reaches for an
   * exponent as soon as that is shorter, so 560 came out as `5.6e+02` -- which
   * reads back as the right value but as a FLOAT, and is not what anyone
   * writing `px(560)` would recognise. */
  if (d == trunc(d) && fabs(d) < 1e15) {
    printf("%.0f", d);
    if (force_float) {
      fputs(".0", stdout);
    }
    return;
  }

  for (int precision = 1; precision <= 17; precision++) {
    char format[8];

    g_snprintf(format, sizeof(format), "%%.%dg", precision);
    g_ascii_formatd(buf, sizeof(buf), format, d);

    if (g_ascii_strtod(buf, NULL) == d) {
      break;
    }
  }

  fputs(buf, stdout);
}

/**
 * Function purpose: @p str as a Lua string literal, quoted and escaped.
 *
 * Action purpose: a control character is escaped as three decimal digits
 * rather than as few as it needs, because Lua reads up to three after a
 * backslash -- so `\0` followed by a digit of the string itself would be read
 * as one escape and swallow it.
 *
 * @returns text the caller frees.
 */
char *sofi_theme_lua_quote(const char *str) {
  GString *out = g_string_new("\"");

  for (const char *c = str != NULL ? str : ""; *c != '\0'; c++) {
    switch (*c) {
    case '"':
      g_string_append(out, "\\\"");
      break;
    case '\\':
      g_string_append(out, "\\\\");
      break;
    case '\n':
      g_string_append(out, "\\n");
      break;
    case '\r':
      g_string_append(out, "\\r");
      break;
    case '\t':
      g_string_append(out, "\\t");
      break;
    default:
      if ((unsigned char)*c < 0x20 || (unsigned char)*c == 0x7f) {
        g_string_append_printf(out, "\\%03u", (unsigned char)*c);
      } else {
        g_string_append_c(out, *c);
      }
      break;
    }
  }

  g_string_append_c(out, '"');

  return g_string_free(out, FALSE);
}

/** @returns whether @p name can be written as a bare Lua table key. */
static gboolean sofi_theme_lua_is_identifier(const char *name) {
  static const char *const keyword[] = {
      "and",    "break", "do",   "else",     "elseif", "end",
      "false",  "for",   "function", "goto", "if",     "in",
      "local",  "nil",   "not",  "or",       "repeat", "return",
      "then",   "true",  "until", "while",   NULL};

  if (name == NULL || name[0] == '\0') {
    return FALSE;
  }
  if (!g_ascii_isalpha(name[0]) && name[0] != '_') {
    return FALSE;
  }
  for (const char *c = name; *c != '\0'; c++) {
    if (!g_ascii_isalnum(*c) && *c != '_') {
      return FALSE;
    }
  }
  for (int i = 0; keyword[i] != NULL; i++) {
    if (strcmp(name, keyword[i]) == 0) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * Function purpose: @p name as a Lua table key.
 *
 * Action purpose: a hyphen is not an identifier character, and the engine's own
 * names are hyphenated -- so `text-color` is written `["text-color"]`. The
 * reader also accepts `text_color` and translates the underscore, but printing
 * that spelling would turn a name that genuinely holds an underscore into one
 * that holds a hyphen, so the bracketed form is used for everything that is not
 * already an identifier and nothing is translated on the way out.
 *
 * @returns text the caller frees.
 */
char *sofi_theme_lua_key(const char *name) {
  if (sofi_theme_lua_is_identifier(name)) {
    return g_strdup(name);
  }

  char *quoted = sofi_theme_lua_quote(name);
  char *key = g_strdup_printf("[%s]", quoted);
  g_free(quoted);

  return key;
}

/** Print @p str as a Lua string literal. */
static void sofi_theme_print_lua_string(const char *str) {
  char *quoted = sofi_theme_lua_quote(str);

  fputs(quoted, stdout);
  g_free(quoted);
}

/** @returns one colour channel of @p value as the 0-255 the hex notation
 * carries. */
static unsigned int sofi_theme_color_channel(double value) {
  return (unsigned int)(CLAMP(value, 0.0, 1.0) * 255.0 + 0.5);
}

/**
 * Function purpose: print a colour as a hex string.
 *
 * Action purpose: hex and not a CSS name, which is what the format before this
 * one printed. Two reasons, and either is enough: a bare `cornflowerblue` is an
 * undefined name to the Lua reader -- it needs `color("cornflowerblue")` -- and
 * hex is EXACT, while a name is only printed when the three channels happen to
 * land on it and says nothing about the alpha it was carrying.
 */
static void sofi_theme_print_lua_color_string(ThemeColor color) {
  unsigned int r = sofi_theme_color_channel(color.red);
  unsigned int g = sofi_theme_color_channel(color.green);
  unsigned int b = sofi_theme_color_channel(color.blue);
  unsigned int a = sofi_theme_color_channel(color.alpha);

  if (a == 255) {
    printf("\"#%02x%02x%02x\"", r, g, b);
  } else {
    printf("\"#%02x%02x%02x%02x\"", r, g, b, a);
  }
}

/**
 * Function purpose: print a colour where a value of any kind is accepted.
 *
 * Action purpose: fully transparent black prints as `transparent`, which is
 * what every shipped layout writes and so what a dump of one should read as. A
 * colour that is transparent but not black keeps its channels instead, because
 * `transparent` would throw them away -- and they are still there to be
 * recovered by raising the alpha.
 */
static void sofi_theme_print_color(ThemeColor color) {
  if (sofi_theme_color_channel(color.alpha) == 0 &&
      sofi_theme_color_channel(color.red) == 0 &&
      sofi_theme_color_channel(color.green) == 0 &&
      sofi_theme_color_channel(color.blue) == 0) {
    fputs("transparent", stdout);
    return;
  }

  sofi_theme_print_lua_color_string(color);
}

/** The five length constructors the reader installs, by unit. */
static const char *sofi_theme_lua_unit(SofiPixelUnit type) {
  switch (type) {
  case SOFI_PU_MM:
    return "mm";
  case SOFI_PU_EM:
    return "em";
  case SOFI_PU_PERCENT:
    return "percent";
  case SOFI_PU_CH:
    return "ch";
  default:
    return "px";
  }
}

/* Action purpose: how each modifier is spelled and how tightly it binds. Five
 * of them are Lua operators and five are `calc` calls, because Lua has no
 * operator for min, max, round, floor or ceil. A call binds as tightly as a
 * leaf, which is what precedence 0 means here; `+` and `-` bind at 10 and `*`,
 * `/` and `%` at 11, which is Lua's own table. */
static const struct {
  const char *infix;
  const char *call;
  int precedence;
} sofi_theme_lua_modifier[] = {
    [SOFI_DISTANCE_MODIFIER_NONE] = {NULL, NULL, 0},
    [SOFI_DISTANCE_MODIFIER_ADD] = {" + ", NULL, 10},
    [SOFI_DISTANCE_MODIFIER_SUBTRACT] = {" - ", NULL, 10},
    [SOFI_DISTANCE_MODIFIER_DIVIDE] = {" / ", NULL, 11},
    [SOFI_DISTANCE_MODIFIER_MULTIPLY] = {" * ", NULL, 11},
    [SOFI_DISTANCE_MODIFIER_MODULO] = {" % ", NULL, 11},
    [SOFI_DISTANCE_MODIFIER_GROUP] = {NULL, NULL, 0},
    [SOFI_DISTANCE_MODIFIER_MIN] = {NULL, "calc.min", 0},
    [SOFI_DISTANCE_MODIFIER_MAX] = {NULL, "calc.max", 0},
    [SOFI_DISTANCE_MODIFIER_ROUND] = {NULL, "calc.round", 0},
    [SOFI_DISTANCE_MODIFIER_FLOOR] = {NULL, "calc.floor", 0},
    [SOFI_DISTANCE_MODIFIER_CEIL] = {NULL, "calc.ceil", 0},
};

/**
 * Function purpose: print one node of a length's arithmetic.
 *
 * @param unit the node.
 * @param style the line style of the whole distance, printed on the LEFTMOST
 *        leaf because that is where the reader takes it from: length_math()
 *        copies its left operand's style into the node it builds, so the style
 *        of the leftmost leaf is the style of the whole expression.
 * @param leftmost TRUE while this node is still on the left spine.
 * @param outer the lowest precedence this position accepts without brackets.
 */
static void sofi_theme_print_distance_unit(SofiDistanceUnit *unit,
                                           SofiLineStyle style,
                                           gboolean leftmost, int outer) {
  SofiDistanceModifier mod = unit->modtype;

  /* Action purpose: SOFI_DISTANCE_MODIFIER_GROUP has no spelling in this
   * format and needs none. It was the `calc( )` wrapper, and
   * distance_unit_get_pixel() evaluates such a node as its left operand alone
   * -- a print marker with no effect on the value -- so the operand is the
   * whole of what there is to print. */
  if (mod == SOFI_DISTANCE_MODIFIER_GROUP) {
    if (unit->left != NULL) {
      sofi_theme_print_distance_unit(unit->left, style, leftmost, outer);
      return;
    }
    mod = SOFI_DISTANCE_MODIFIER_NONE;
  }

  /* A node with an operand missing is not an operation; the engine reads its
   * own value in that case and so does this. */
  if (mod == SOFI_DISTANCE_MODIFIER_NONE || unit->left == NULL ||
      unit->right == NULL) {
    printf("%s(", sofi_theme_lua_unit(unit->type));
    sofi_theme_print_lua_number(unit->distance, FALSE);
    if (leftmost && style == SOFI_HL_DASH) {
      fputs(", dash", stdout);
    }
    putchar(')');

    return;
  }

  if (sofi_theme_lua_modifier[mod].call != NULL) {
    printf("%s(", sofi_theme_lua_modifier[mod].call);
    sofi_theme_print_distance_unit(unit->left, style, leftmost, 0);
    fputs(", ", stdout);
    sofi_theme_print_distance_unit(unit->right, style, FALSE, 0);
    putchar(')');

    return;
  }

  /* Action purpose: brackets only where they change the tree. All five
   * operators are left-associative, so an operand of equal precedence needs
   * brackets on the right and does not on the left -- which is what prints
   * `percent(100) - px(10)` rather than burying every expression in
   * parentheses, while still keeping `px(2) * (px(3) + px(4))` the shape it
   * is. */
  int precedence = sofi_theme_lua_modifier[mod].precedence;
  gboolean brackets = precedence < outer;

  if (brackets) {
    putchar('(');
  }
  sofi_theme_print_distance_unit(unit->left, style, leftmost, precedence);
  fputs(sofi_theme_lua_modifier[mod].infix, stdout);
  sofi_theme_print_distance_unit(unit->right, style, FALSE, precedence + 1);
  if (brackets) {
    putchar(')');
  }
}

static void sofi_theme_print_distance(SofiDistance d) {
  sofi_theme_print_distance_unit(&(d.base), d.style, TRUE, 0);
}

/** Textual representation of SofiCursorType, as the reader's own constants. */
const char *const SofiCursorTypeStr[3] = {
    "default",
    "pointer",
    "text",
};

/* Action purpose: the position constants as the reader installs them --
 * `north_east` and not the `northeast` the format before this one printed,
 * which Lua reads as an undefined name. */
static const char *sofi_theme_lua_position(int position) {
  switch (position) {
  case WL_NORTH:
    return "north";
  case WL_SOUTH:
    return "south";
  case WL_WEST:
    return "west";
  case WL_EAST:
    return "east";
  case WL_NORTH | WL_EAST:
    return "north_east";
  case WL_SOUTH | WL_EAST:
    return "south_east";
  case WL_NORTH | WL_WEST:
    return "north_west";
  case WL_SOUTH | WL_WEST:
    return "south_west";
  default:
    return "center";
  }
}

/** The text styles, in the order highlight() takes them. */
static const struct {
  SofiHighlightStyle bit;
  const char *name;
} sofi_theme_lua_highlight[] = {
    {SOFI_HL_BOLD, "bold"},
    {SOFI_HL_ITALIC, "italic"},
    {SOFI_HL_UNDERLINE, "underline"},
    {SOFI_HL_STRIKETHROUGH, "strikethrough"},
    {SOFI_HL_UPPERCASE, "uppercase"},
    {SOFI_HL_LOWERCASE, "lowercase"},
    {SOFI_HL_CAPITALIZE, "capitalize"},
    {0, NULL},
};

/**
 * Function purpose: print a highlight.
 *
 * Action purpose: one style and no colour prints as a bare constant, because
 * the reader reads a lone style constant as a highlight of one bit and that is
 * what a theme writes. Everything else goes through highlight(), which is the
 * only way to combine them.
 *
 * The colour is printed as hex and never as `transparent`: highlight() takes a
 * colour or a string, and the transparent constant is neither.
 */
static void sofi_theme_print_highlight(SofiHighlightColorStyle highlight) {
  gboolean coloured = (highlight.style & SOFI_HL_COLOR) != 0;
  int styles = 0;

  for (int i = 0; sofi_theme_lua_highlight[i].name != NULL; i++) {
    if (highlight.style & sofi_theme_lua_highlight[i].bit) {
      styles++;
    }
  }

  if (!coloured && styles <= 1) {
    for (int i = 0; sofi_theme_lua_highlight[i].name != NULL; i++) {
      if (highlight.style & sofi_theme_lua_highlight[i].bit) {
        fputs(sofi_theme_lua_highlight[i].name, stdout);

        return;
      }
    }
    fputs("none", stdout);

    return;
  }

  fputs("highlight(", stdout);

  gboolean first = TRUE;

  for (int i = 0; sofi_theme_lua_highlight[i].name != NULL; i++) {
    if (highlight.style & sofi_theme_lua_highlight[i].bit) {
      fputs(first ? "" : ", ", stdout);
      fputs(sofi_theme_lua_highlight[i].name, stdout);
      first = FALSE;
    }
  }

  if (coloured) {
    fputs(first ? "" : ", ", stdout);
    sofi_theme_print_lua_color_string(highlight.color);
  }

  putchar(')');
}

/**
 * Function purpose: an angle back in degrees, which is what linear_gradient()
 * takes -- the reader divides by 360 to store it in turns.
 *
 * Action purpose: the multiplication that undoes that division is correctly
 * rounded but is not its exact inverse for every value, so the two neighbours
 * of the obvious answer are tried as well. The figure printed is one the reader
 * turns back into exactly the angle that was stored, so the dump is stable
 * rather than drifting by one ulp per round trip.
 */
static double sofi_theme_gradient_degrees(double angle) {
  double degrees = angle * 360.0;

  if (!isfinite(degrees) || degrees / 360.0 == angle) {
    return degrees;
  }
  if (nextafter(degrees, INFINITY) / 360.0 == angle) {
    return nextafter(degrees, INFINITY);
  }
  if (nextafter(degrees, -INFINITY) / 360.0 == angle) {
    return nextafter(degrees, -INFINITY);
  }

  return degrees;
}

/**
 * Function purpose: print an image -- a file, or a gradient.
 *
 * Action purpose: the DIRECTION and the ANGLE are printed. The printer before
 * this one dropped both and printed the colour stops alone, so a gradient that
 * ran top to bottom or at 45 degrees came back running left to right.
 */
static void sofi_theme_print_image(SofiImage image) {
  /* The scaling modes url() takes. SOFI_SCALE_NONE is its default and is left
   * out, which is the one-argument call every theme writes. */
  static const char *const scaling[] = {
      [SOFI_SCALE_NONE] = NULL,
      [SOFI_SCALE_BOTH] = "both",
      [SOFI_SCALE_HEIGHT] = "height",
      [SOFI_SCALE_WIDTH] = "width",
  };
  static const char *const edge[] = {
      [SOFI_DIRECTION_LEFT] = "left",
      [SOFI_DIRECTION_RIGHT] = "right",
      [SOFI_DIRECTION_TOP] = "top",
      [SOFI_DIRECTION_BOTTOM] = "bottom",
  };

  if (image.type == SOFI_IMAGE_URL) {
    /* Action purpose: image.url, which is the member that holds it. The
     * printer before this one read p->value.s -- a different arm of the same
     * union, which overlays the `type` field of SofiImage rather than its
     * path, so every URL image in every dump printed `url ("(null)")`. */
    fputs("url(", stdout);
    sofi_theme_print_lua_string(image.url);

    if ((unsigned int)image.scaling < G_N_ELEMENTS(scaling) &&
        scaling[image.scaling] != NULL) {
      fputs(", ", stdout);
      sofi_theme_print_lua_string(scaling[image.scaling]);
    }
    putchar(')');

    return;
  }

  fputs("linear_gradient(", stdout);

  if (image.dir == SOFI_DIRECTION_ANGLE) {
    sofi_theme_print_lua_number(sofi_theme_gradient_degrees(image.angle),
                                FALSE);
    fputs(", ", stdout);
  } else if ((unsigned int)image.dir < G_N_ELEMENTS(edge) &&
             edge[image.dir] != NULL) {
    sofi_theme_print_lua_string(edge[image.dir]);
    fputs(", ", stdout);
  }

  unsigned int stops = 0;

  for (GList *l = g_list_first(image.colors); l != NULL; l = g_list_next(l)) {
    fputs(stops > 0 ? ", " : "", stdout);
    sofi_theme_print_lua_color_string(*(ThemeColor *)l->data);
    stops++;
  }

  /* A gradient with no stops is not one the reader can build and not one that
   * paints anything, but it still has to print as a call that reads. */
  if (stops == 0) {
    fputs("\"#00000000\"", stdout);
  }

  putchar(')');
}

/**
 * Function purpose: whether a list can print as a plain array.
 *
 * Action purpose: an array of STRINGS is a list to the reader and an array of
 * numbers or lengths is PADDING, so anything else has to go through list().
 * A string beginning with `#` or `@` is a colour or a reference and so is not
 * one of the strings that qualifies.
 */
static gboolean sofi_theme_list_is_strings(GList *list) {
  for (GList *iter = list; iter != NULL; iter = g_list_next(iter)) {
    Property *p = (Property *)iter->data;

    if (p == NULL || p->type != P_STRING) {
      return FALSE;
    }
    if (p->value.s != NULL &&
        (p->value.s[0] == '#' || p->value.s[0] == '@')) {
      return FALSE;
    }
  }

  return TRUE;
}

static void int_sofi_theme_print_property(Property *p) {
  switch (p->type) {
  case P_LIST: {
    gboolean plain = sofi_theme_list_is_strings(p->value.list);

    if (p->value.list == NULL) {
      /* An empty array is an empty list; there is nothing for it to be
       * ambiguous with. */
      fputs("{}", stdout);

      break;
    }

    fputs(plain ? "{ " : "list(", stdout);
    for (GList *iter = p->value.list; iter != NULL; iter = g_list_next(iter)) {
      int_sofi_theme_print_property((Property *)iter->data);
      if (iter->next != NULL) {
        fputs(", ", stdout);
      }
    }
    fputs(plain ? " }" : ")", stdout);
    break;
  }
  case P_ORIENTATION:
    fputs((p->value.i == SOFI_ORIENTATION_HORIZONTAL) ? "horizontal"
                                                      : "vertical",
          stdout);
    break;
  case P_CURSOR:
    fputs(SofiCursorTypeStr[CLAMP(p->value.i, 0, 2)], stdout);
    break;
  case P_HIGHLIGHT:
    sofi_theme_print_highlight(p->value.highlight);
    break;
  case P_POSITION:
    fputs(sofi_theme_lua_position(p->value.i), stdout);
    break;
  case P_STRING:
    /* Action purpose: `#` and `@` are what make a colour and a reference, so a
     * string that begins with either has to say that it is a string or it
     * comes back as one of those two. */
    if (p->value.s != NULL &&
        (p->value.s[0] == '#' || p->value.s[0] == '@')) {
      fputs("str(", stdout);
      sofi_theme_print_lua_string(p->value.s);
      putchar(')');
    } else {
      sofi_theme_print_lua_string(p->value.s);
    }
    break;
  case P_INTEGER:
    printf("%d", p->value.i);
    break;
  case P_DOUBLE:
    /* A double has to read back as a double, which in Lua means it needs a
     * decimal point or an exponent. */
    sofi_theme_print_lua_number(p->value.f, TRUE);
    break;
  case P_BOOLEAN:
    fputs(p->value.b ? "true" : "false", stdout);
    break;
  case P_COLOR:
    sofi_theme_print_color(p->value.color);
    break;
  case P_IMAGE:
    sofi_theme_print_image(p->value.image);
    break;
  case P_PADDING:
    /* The CSS counts, as the reader reads them: one length for all four edges,
     * two for vertical and horizontal, three for top, horizontal and bottom,
     * four clockwise from the top. One length needs no array -- a bare length
     * IS all four edges to the reader -- and the rest do. */
    if (distance_compare(p->value.padding.top, p->value.padding.bottom) &&
        distance_compare(p->value.padding.left, p->value.padding.right) &&
        distance_compare(p->value.padding.left, p->value.padding.top)) {
      sofi_theme_print_distance(p->value.padding.left);
    } else if (distance_compare(p->value.padding.top,
                                p->value.padding.bottom) &&
               distance_compare(p->value.padding.left,
                                p->value.padding.right)) {
      fputs("{ ", stdout);
      sofi_theme_print_distance(p->value.padding.top);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.left);
      fputs(" }", stdout);
    } else if (!distance_compare(p->value.padding.top,
                                 p->value.padding.bottom) &&
               distance_compare(p->value.padding.left,
                                p->value.padding.right)) {
      fputs("{ ", stdout);
      sofi_theme_print_distance(p->value.padding.top);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.left);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.bottom);
      fputs(" }", stdout);
    } else {
      fputs("{ ", stdout);
      sofi_theme_print_distance(p->value.padding.top);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.right);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.bottom);
      fputs(", ", stdout);
      sofi_theme_print_distance(p->value.padding.left);
      fputs(" }", stdout);
    }
    break;
  case P_LINK:
    if (p->value.link.def_value != NULL) {
      /* A fallback is what `"@name"` cannot carry, which is the whole reason
       * ref() exists. */
      fputs("ref(", stdout);
      sofi_theme_print_lua_string(p->value.link.name);
      fputs(", ", stdout);
      int_sofi_theme_print_property(p->value.link.def_value);
      putchar(')');
    } else {
      char *reference = g_strconcat("@", p->value.link.name, NULL);

      sofi_theme_print_lua_string(reference);
      g_free(reference);
    }
    break;
  case P_INHERIT:
    fputs("inherit", stdout);
    break;
  default:
    /* No type of this engine's reaches here. `nil` is what Lua says about a
     * key a table does not have, so an unprintable value prints as a
     * well-formed nothing rather than as a syntax error. */
    fputs("nil", stdout);
    break;
  }
}

static void sofi_theme_print_property_index(int depth, Property *p) {
  char *key = sofi_theme_lua_key(p->name);

  printf("%*s%s = ", depth, "", key);
  g_free(key);
  int_sofi_theme_print_property(p);
  fputs(",\n", stdout);
}

static int sofi_theme_property_name_cmp(gconstpointer a, gconstpointer b) {
  return g_strcmp0(((const Property *)a)->name, ((const Property *)b)->name);
}

/**
 * Function purpose: a block's declarations, in a reproducible order.
 *
 * Action purpose: the order a GLib hash table iterates in follows from the key
 * set, the insertion order and the table's capacity, so printing in it gave
 * `-dump-theme` an output that could not be diffed against a second dump of
 * the same theme -- which is exactly what a round trip is. Declarations inside
 * one block are unordered by construction, one per name in a hash table, so
 * sorting them by name changes nothing about what the block means and is what
 * makes the dump reproducible.
 *
 * @returns a list the caller frees with g_list_free().
 */
static GList *sofi_theme_sorted_properties(GHashTable *properties) {
  return g_list_sort(g_hash_table_get_values(properties),
                     sofi_theme_property_name_cmp);
}

/**
 * Function purpose: the selector a widget is reached by, spelled as the reader
 * splits one: the first separator is a space and the rest are dots, so
 * `element normal.urgent` is the three-step path it was written as.
 *
 * The walk stops at the root and at a media block, because inside a media
 * block the path starts again from that block.
 *
 * @returns text the caller frees.
 */
static char *sofi_theme_widget_selector(ThemeWidget *wid) {
  GList *path = NULL;

  for (ThemeWidget *w = wid; w != NULL; w = w->parent) {
    if (g_strcmp0(w->name, "Root") == 0 || w->media != NULL) {
      break;
    }
    path = g_list_prepend(path, w->name);
  }

  /* No path at all is the root's own property table, which is what `*` names
   * and is where a `@name` reference is looked up. */
  if (path == NULL) {
    return g_strdup("*");
  }

  GString *out = g_string_new(NULL);

  for (GList *iter = path; iter != NULL; iter = g_list_next(iter)) {
    if (iter->prev != NULL) {
      g_string_append_c(out, iter->prev->prev == NULL ? ' ' : '.');
    }
    g_string_append(out, (const char *)iter->data);
  }

  g_list_free(path);

  return g_string_free(out, FALSE);
}

/**
 * Function purpose: the name of a media type -- the inverse of
 * sofi_theme_parse_media_type(), which is what a dump needs: the tree holds the
 * enumerated type and the reader is given the word.
 *
 * The two have to stay in step, and they are kept together for that reason: a
 * name this returns that that one does not accept is a dump the reader refuses.
 *
 * @returns a static name, or NULL when the type is not one of them.
 */
static const char *sofi_theme_media_type_name(ThemeMediaType type) {
  switch (type) {
  case THEME_MEDIA_TYPE_MON_ID:
    return "monitor-id";
  case THEME_MEDIA_TYPE_MIN_WIDTH:
    return "min-width";
  case THEME_MEDIA_TYPE_MAX_WIDTH:
    return "max-width";
  case THEME_MEDIA_TYPE_MIN_HEIGHT:
    return "min-height";
  case THEME_MEDIA_TYPE_MAX_HEIGHT:
    return "max-height";
  case THEME_MEDIA_TYPE_MIN_ASPECT_RATIO:
    return "min-aspect-ratio";
  case THEME_MEDIA_TYPE_MAX_ASPECT_RATIO:
    return "max-aspect-ratio";
  case THEME_MEDIA_TYPE_BOOLEAN:
    return "enabled";
  default:
    return NULL;
  }
}

/**
 * Function purpose: print one `@media` block, with its rules inside it.
 *
 * Action purpose: a media block is a DIRECTIVE of its own and not a rule --
 * `{ media = { ... }, rules = { ... } }` -- because its query has to be
 * evaluated before its rules apply and because a `*` inside one is an ordinary
 * child rather than the root. Printing it as a rule named `@media ( ... )`
 * would file the whole block under a widget of that name, which nothing ever
 * looks up.
 */
static void sofi_theme_print_media(ThemeWidget *wid, int depth) {
  const char *type = sofi_theme_media_type_name(wid->media->type);

  /* A block the reader cannot name is one it would refuse on the way back in,
   * and its rules are inside it, so there is nothing to print. */
  if (type == NULL) {
    return;
  }

  char *quoted = sofi_theme_lua_quote(type);

  printf("%*s{ media = { type = %s, value = ", depth, "", quoted);
  g_free(quoted);

  if (wid->media->type == THEME_MEDIA_TYPE_BOOLEAN) {
    fputs(wid->media->boolv ? "true" : "false", stdout);
  } else {
    sofi_theme_print_lua_number(wid->media->value, FALSE);
  }

  fputs(" },\n", stdout);
  printf("%*srules = {\n", depth + THEME_PRINT_INDENT, "");

  for (unsigned int i = 0; i < wid->num_widgets; i++) {
    sofi_theme_print_index(wid->widgets[i], depth + 2 * THEME_PRINT_INDENT);
  }

  printf("%*s} },\n", depth + THEME_PRINT_INDENT, "");
}

void sofi_theme_print_index(ThemeWidget *wid, int index) {
  if (wid->media != NULL) {
    sofi_theme_print_media(wid, index);

    return;
  }

  /* Action purpose: a widget with no declarations prints no block. It is
   * reachable anyway -- every selector that names a child creates the widgets
   * on the way to it -- and an empty `{ select = "x" }` would come back as a
   * widget with a property table that has nothing in it, which prints no block
   * on the next dump and so would make the round trip disagree with itself. */
  if (wid->properties != NULL && g_hash_table_size(wid->properties) > 0) {
    char *selector = sofi_theme_widget_selector(wid);
    char *quoted = sofi_theme_lua_quote(selector);
    GList *properties = sofi_theme_sorted_properties(wid->properties);

    printf("%*s{ select = %s,\n", index, "", quoted);
    g_free(quoted);
    g_free(selector);

    for (GList *iter = properties; iter != NULL; iter = g_list_next(iter)) {
      sofi_theme_print_property_index(index + THEME_PRINT_INDENT,
                                      (Property *)iter->data);
    }

    g_list_free(properties);
    printf("%*s},\n", index, "");
  }

  for (unsigned int i = 0; i < wid->num_widgets; i++) {
    sofi_theme_print_index(wid->widgets[i], index);
  }
}

void sofi_theme_print(ThemeWidget *wid) {
  if (wid != NULL) {
    printf("-- sofi -dump-theme output.\n-- Sofi version: %s\n\nreturn {\n",
           PACKAGE_VERSION);
    sofi_theme_print_index(wid, THEME_PRINT_INDENT);
    printf("}\n");
  }
}

/**
 * Function purpose: print one nested configuration block, as the reader reads
 * one: a plain table with no array part, under the block's own name.
 *
 * Action purpose: this is NOT a rule and must not print as one. A configuration
 * block's children go into the separate sofi_configuration tree through
 * apply_configuration(), which is reached by the `configuration` key and by
 * nothing else, so `{ select = "timeout", delay = 15 }` would file it under a
 * theme widget nothing ever looks up -- a silent no-op, and the worst of the
 * available outcomes.
 */
void sofi_theme_print_lua_config_block(ThemeWidget *wid, int depth) {
  if (wid == NULL) {
    return;
  }

  char *key = sofi_theme_lua_key(wid->name);

  printf("%*s%s = {\n", depth, "", key);
  g_free(key);

  if (wid->properties != NULL) {
    GList *properties = sofi_theme_sorted_properties(wid->properties);

    for (GList *iter = properties; iter != NULL; iter = g_list_next(iter)) {
      sofi_theme_print_property_index(depth + THEME_PRINT_INDENT,
                                      (Property *)iter->data);
    }

    g_list_free(properties);
  }

  for (unsigned int i = 0; i < wid->num_widgets; i++) {
    sofi_theme_print_lua_config_block(wid->widgets[i],
                                      depth + THEME_PRINT_INDENT);
  }

  printf("%*s},\n", depth, "");
}

static void sofi_theme_copy_property_int(G_GNUC_UNUSED gpointer key,
                                         gpointer value, gpointer user_data) {
  GHashTable *table = (GHashTable *)user_data;
  Property *p = sofi_theme_property_copy((Property *)value, NULL);
  g_hash_table_replace(table, p->name, p);
}
void sofi_theme_widget_add_properties(ThemeWidget *wid, GHashTable *table) {
  if (table == NULL) {
    return;
  }
  if (wid->properties == NULL) {
    wid->properties =
        g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                              (GDestroyNotify)sofi_theme_property_free);
  }
  g_hash_table_foreach(table, sofi_theme_copy_property_int, wid->properties);
}

/**
 * Public API
 */

static inline ThemeWidget *sofi_theme_find_single(ThemeWidget *wid,
                                                  const char *name) {
  for (unsigned int j = 0; wid && j < wid->num_widgets; j++) {
    if (g_strcmp0(wid->widgets[j]->name, name) == 0) {
      return wid->widgets[j];
    }
  }
  return wid;
}

static ThemeWidget *sofi_theme_find(ThemeWidget *wid, const char *name,
                                    const gboolean exact) {
  if (wid == NULL || name == NULL) {
    return wid;
  }
  char *tname = g_strdup(name);
  char *saveptr = NULL;
  int found = TRUE;
  for (const char *iter = strtok_r(tname, ".", &saveptr); iter != NULL;
       iter = strtok_r(NULL, ".", &saveptr)) {
    found = FALSE;
    ThemeWidget *f = sofi_theme_find_single(wid, iter);
    if (f != wid) {
      wid = f;
      found = TRUE;
    } else if (exact) {
      break;
    }
  }
  g_free(tname);
  if (!exact || found) {
    return wid;
  }
  return NULL;
}

static void sofi_theme_resolve_link_property(Property *p, int cur_depth) {
  // Set name, remove '@' prefix.
  const char *name = p->value.link.name; // + (*(p->value.link.name)== '@'?1:0;
  g_info("Resolving link to %s", p->value.link.name);
  if (cur_depth > 20) {
    g_warning("Found more then 20 redirects for property. Stopping.");
    p->value.link.ref = p;
    return;
  }

  if (sofi_theme->properties &&
      g_hash_table_contains(sofi_theme->properties, name)) {
    Property *pr = g_hash_table_lookup(sofi_theme->properties, name);
    g_info("Resolving link %s found: %s", p->value.link.name, pr->name);
    if (pr->type == P_LINK) {
      if (pr->value.link.ref == NULL) {
        sofi_theme_resolve_link_property(pr, cur_depth + 1);
      }
      if (pr->value.link.ref != pr) {
        p->value.link.ref = pr->value.link.ref;
        return;
      }
    } else {
      p->value.link.ref = pr;
      return;
    }
  }
  // No found and we have default value.
  if (p->value.link.def_value) {
    p->value.link.ref = p->value.link.def_value;
    return;
  }

  // No found, set ref to self.
  p->value.link.ref = p;
}

Property *sofi_theme_find_property(ThemeWidget *wid, PropertyType type,
                                   const char *property, gboolean exact) {
  while (wid) {
    if (wid->properties && g_hash_table_contains(wid->properties, property)) {
      Property *p = g_hash_table_lookup(wid->properties, property);
      if (p->type == P_INHERIT) {
        return p;
      }
      if (p->type == P_LINK) {
        if (p->value.link.ref == NULL) {
          // Resolve link.
          sofi_theme_resolve_link_property(p, 0);
        }
        if (p->value.link.ref != NULL && p->value.link.ref->type == type) {
          return p->value.link.ref;
        }
      }
      if (p->type == type) {
        return p;
      }
      // SofiPadding and integer can be converted.
      if (p->type == P_INTEGER && type == P_PADDING) {
        return p;
      }
      g_debug("Found property: '%s' on '%s', but type %s does not match "
              "expected type %s.",
              property, wid->name, PropertyTypeName[p->type],
              PropertyTypeName[type]);
    }
    if (exact) {
      return NULL;
    }
    // Fall back to defaults.
    wid = wid->parent;
  }
  return NULL;
}
ThemeWidget *sofi_config_find_widget(const char *name, const char *state,
                                     gboolean exact) {
  // First find exact match based on name.
  ThemeWidget *wid = sofi_theme_find_single(sofi_configuration, name);
  wid = sofi_theme_find(wid, state, exact);

  return wid;
}
ThemeWidget *sofi_theme_find_widget(const char *name, const char *state,
                                    gboolean exact) {
  // First find exact match based on name.
  ThemeWidget *wid = sofi_theme_find_single(sofi_theme, name);
  wid = sofi_theme_find(wid, state, exact);

  return wid;
}

static int sofi_theme_get_position_inside(Property *p, const widget *wid,
                                          const char *property, int def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_POSITION, property, FALSE);
        return sofi_theme_get_position_inside(pv, wid->parent, property, def);
      }
      return def;
    }
    return p->value.i;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
int sofi_theme_get_position(const widget *wid, const char *property, int def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_POSITION, property, FALSE);
  return sofi_theme_get_position_inside(p, wid, property, def);
}
static int sofi_theme_get_integer_inside(Property *p, const widget *wid,
                                         const char *property, int def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_INTEGER, property, FALSE);
        return sofi_theme_get_integer_inside(pv, wid->parent, property, def);
      }
      return def;
    }
    return p->value.i;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
int sofi_theme_get_integer(const widget *wid, const char *property, int def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_INTEGER, property, FALSE);
  return (int)sofi_theme_get_integer_inside(p, wid, property, (double)def);
}
static SofiDistance sofi_theme_get_distance_inside(Property *p,
                                                   const widget *wid,
                                                   const char *property,
                                                   int def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_PADDING, property, FALSE);
        return sofi_theme_get_distance_inside(pv, wid->parent, property, def);
      }
      return (SofiDistance){
          .base = {def, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL},
          .style = SOFI_HL_SOLID};
    }
    if (p->type == P_INTEGER) {
      return (SofiDistance){.base = {p->value.i, SOFI_PU_PX,
                                     SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL},
                            .style = SOFI_HL_SOLID};
    }
    return p->value.padding.left;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return (SofiDistance){
      .base = {def, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL},
      .style = SOFI_HL_SOLID};
}
SofiDistance sofi_theme_get_distance(const widget *wid, const char *property,
                                     int def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_PADDING, property, FALSE);
  return sofi_theme_get_distance_inside(p, wid, property, def);
}

static int sofi_theme_get_boolean_inside(Property *p, const widget *wid,
                                         const char *property, int def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_BOOLEAN, property, FALSE);
        return sofi_theme_get_boolean_inside(pv, wid->parent, property, def);
      }
      return def;
    }
    return p->value.b;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
int sofi_theme_get_boolean(const widget *wid, const char *property, int def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_BOOLEAN, property, FALSE);
  return sofi_theme_get_boolean_inside(p, wid, property, def);
}

static SofiOrientation sofi_theme_get_orientation_inside(Property *p,
                                                         const widget *wid,
                                                         const char *property,
                                                         SofiOrientation def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_ORIENTATION, property, FALSE);
        return sofi_theme_get_orientation_inside(pv, wid->parent, property,
                                                 def);
      }
      return def;
    }
    return p->value.b;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
SofiOrientation sofi_theme_get_orientation(const widget *wid,
                                           const char *property,
                                           SofiOrientation def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p =
      sofi_theme_find_property(wid_find, P_ORIENTATION, property, FALSE);
  return sofi_theme_get_orientation_inside(p, wid, property, def);
}

static SofiCursorType sofi_theme_get_cursor_type_inside(Property *p,
                                                        const widget *wid,
                                                        const char *property,
                                                        SofiCursorType def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_CURSOR, property, FALSE);
        return sofi_theme_get_cursor_type_inside(pv, wid->parent, property,
                                                 def);
      }
      return def;
    }
    return p->value.i;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
SofiCursorType sofi_theme_get_cursor_type(const widget *wid,
                                          const char *property,
                                          SofiCursorType def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_CURSOR, property, FALSE);
  return sofi_theme_get_cursor_type_inside(p, wid, property, def);
}
static const char *sofi_theme_get_string_inside(Property *p, const widget *wid,
                                                const char *property,
                                                const char *def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_STRING, property, FALSE);
        return sofi_theme_get_string_inside(pv, wid->parent, property, def);
      }
      return def;
    }
    return p->value.s;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
const char *sofi_theme_get_string(const widget *wid, const char *property,
                                  const char *def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_STRING, property, FALSE);
  return sofi_theme_get_string_inside(p, wid, property, def);
}

static double sofi_theme_get_double_integer_fb_inside(Property *p,
                                                      const widget *wid,
                                                      const char *property,
                                                      double def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_INTEGER, property, FALSE);
        return sofi_theme_get_double_integer_fb_inside(pv, wid->parent,
                                                       property, def);
      }
      return def;
    }
    return p->value.i;
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return def;
}
static double sofi_theme_get_double_inside(const widget *orig, Property *p,
                                           const widget *wid,
                                           const char *property, double def) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_DOUBLE, property, FALSE);
        return sofi_theme_get_double_inside(orig, pv, wid->parent, property,
                                            def);
      }
      return def;
    }
    return p->value.f;
  }
  ThemeWidget *wid_find = sofi_theme_find_widget(orig->name, wid->state, FALSE);
  // Fallback to integer if double is not found.
  p = sofi_theme_find_property(wid_find, P_INTEGER, property, FALSE);
  return sofi_theme_get_double_integer_fb_inside(p, wid, property, def);
}
double sofi_theme_get_double(const widget *wid, const char *property,
                             double def) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_DOUBLE, property, FALSE);
  return sofi_theme_get_double_inside(wid, p, wid, property, def);
}
static void sofi_theme_get_color_inside(const widget *wid, Property *p,
                                        const char *property, cairo_t *d) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_COLOR, property, FALSE);
        sofi_theme_get_color_inside(wid->parent, pv, property, d);
      }
      return;
    }
    cairo_set_source_rgba(d, p->value.color.red, p->value.color.green,
                          p->value.color.blue, p->value.color.alpha);
  } else {
    g_debug("Theme entry: #%s %s property %s unset.", wid->name,
            wid->state ? wid->state : "", property);
  }
}

void sofi_theme_get_color(const widget *wid, const char *property, cairo_t *d) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_COLOR, property, FALSE);
  sofi_theme_get_color_inside(wid, p, property, d);
}

static gboolean sofi_theme_get_image_inside(Property *p, const widget *wid,
                                            const char *property, cairo_t *d) {
  const guint scale = disp_scale ? disp_scale() : 1;
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_IMAGE, property, FALSE);
        return sofi_theme_get_image_inside(pv, wid->parent, property, d);
      }
      return FALSE;
    }
    if (p->value.image.type == SOFI_IMAGE_URL) {
      int wsize = -1;
      int hsize = -1;
      switch (p->value.image.scaling) {
      case SOFI_SCALE_BOTH:
        wsize = wid->w;
        hsize = wid->h;
        break;
      case SOFI_SCALE_WIDTH:
        wsize = wid->w;
        break;
      case SOFI_SCALE_HEIGHT:
        hsize = wid->h;
        break;
      case SOFI_SCALE_NONE:
      default:
        break;
      }
      // FIXME: cache when hsize, wsize and scale do not change without
      // modifying SofiImage (for ABI compatibility)
      p->value.image.surface_id =
          sofi_icon_fetcher_query_advanced(p->value.image.url, wsize, hsize);
      cairo_surface_t *img = sofi_icon_fetcher_get(p->value.image.surface_id);

      if (img != NULL) {
        cairo_pattern_t *pat = cairo_pattern_create_for_surface(img);
        cairo_surface_set_device_scale(img, scale, scale);
        cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
        cairo_set_source(d, pat);
        cairo_pattern_destroy(pat);
        return TRUE;
      }
    } else if (p->value.image.type == SOFI_IMAGE_LINEAR_GRADIENT) {
      cairo_pattern_t *pat = NULL;
      switch (p->value.image.dir) {
      case SOFI_DIRECTION_RIGHT:
        pat = cairo_pattern_create_linear(0.0, 0.0, wid->w, 0.0);
        break;
      case SOFI_DIRECTION_LEFT:
        pat = cairo_pattern_create_linear(wid->w, 0.0, 0.0, 0.0);
        break;
      case SOFI_DIRECTION_BOTTOM:
        pat = cairo_pattern_create_linear(0.0, 0.0, 0.0, wid->h);
        break;
      case SOFI_DIRECTION_TOP:
        pat = cairo_pattern_create_linear(0.0, wid->h, 0.0, 0.0);
        break;
      case SOFI_DIRECTION_ANGLE: {
        double offsety1 = sin(G_PI * 2 * p->value.image.angle) * (wid->h / 2.0);
        double offsetx1 = cos(G_PI * 2 * p->value.image.angle) * (wid->w / 2.0);
        pat = cairo_pattern_create_linear(
            wid->w / 2.0 - offsetx1, wid->h / 2.0 - offsety1,
            wid->w / 2.0 + offsetx1, wid->h / 2.0 + offsety1);
        break;
      }
      };
      guint length = g_list_length(p->value.image.colors);
      if (length > 1) {
        length--;
        guint color_index = 0;
        for (GList *l = g_list_first(p->value.image.colors); l != NULL;
             l = g_list_next(l)) {
          ThemeColor *c = (ThemeColor *)(l->data);
          cairo_pattern_add_color_stop_rgba(pat, (color_index) / (double)length,
                                            c->red, c->green, c->blue,
                                            c->alpha);
          color_index++;
        }
        cairo_set_source(d, pat);
        cairo_pattern_destroy(pat);
        return TRUE;
      }
      if (length == 1) {
        ThemeColor *c = (ThemeColor *)(p->value.image.colors->data);
        cairo_pattern_add_color_stop_rgba(pat, 0, c->red, c->green, c->blue,
                                          c->alpha);
        cairo_set_source(d, pat);
        cairo_pattern_destroy(pat);
        return TRUE;
      }
    }
  } else {
    g_debug("Theme entry: #%s %s property %s unset.", wid->name,
            wid->state ? wid->state : "", property);
  }
  return FALSE;
}
gboolean sofi_theme_get_image(const widget *wid, const char *property,
                              cairo_t *d) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_IMAGE, property, FALSE);
  return sofi_theme_get_image_inside(p, wid, property, d);
}
static SofiPadding sofi_theme_get_padding_inside(Property *p, const widget *wid,
                                                 const char *property,
                                                 SofiPadding pad) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_PADDING, property, FALSE);
        return sofi_theme_get_padding_inside(pv, wid->parent, property, pad);
      }
      return pad;
    }
    if (p->type == P_PADDING) {
      pad = p->value.padding;
    } else {
      SofiDistance d =
          (SofiDistance){.base = {p->value.i, SOFI_PU_PX,
                                  SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL},
                         .style = SOFI_HL_SOLID};
      return (SofiPadding){d, d, d, d};
    }
  }
  g_debug("Theme entry: #%s %s property %s unset.", wid->name,
          wid->state ? wid->state : "", property);
  return pad;
}
SofiPadding sofi_theme_get_padding(const widget *wid, const char *property,
                                   SofiPadding pad) {
  ThemeWidget *wid_find = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid_find, P_PADDING, property, FALSE);
  return sofi_theme_get_padding_inside(p, wid, property, pad);
}

static GList *sofi_theme_get_list_inside(Property *p, const widget *wid,
                                         const char *property,
                                         PropertyType child_type) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_LIST, property, FALSE);
        return sofi_theme_get_list_inside(pv, wid->parent, property,
                                          child_type);
      }
    } else if (p->type == P_LIST) {
      return p->value.list;
    }
  }
  return NULL;
}
GList *sofi_theme_get_list_distance(const widget *wid, const char *property) {
  ThemeWidget *wid2 = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid2, P_LIST, property, FALSE);
  GList *list = sofi_theme_get_list_inside(p, wid, property, P_PADDING);
  GList *retv = NULL;
  for (GList *iter = g_list_first(list); iter != NULL;
       iter = g_list_next(iter)) {
    Property *prop = (Property *)(iter->data);
    if (prop->type == P_PADDING) {
      SofiDistance *pnew = g_new0(SofiDistance, 1);
      *pnew = prop->value.padding.left;
      retv = g_list_append(retv, pnew);
    } else if (prop->type == P_INTEGER) {
      SofiDistance *pnew = g_new0(SofiDistance, 1);
      SofiDistance d =
          (SofiDistance){.base = {prop->value.i, SOFI_PU_PX,
                                  SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL},
                         .style = SOFI_HL_SOLID};
      *pnew = d;
      retv = g_list_append(retv, pnew);
    } else {
      g_warning("Invalid type detected in list.");
    }
  }
  return retv;
}
GList *sofi_theme_get_list_strings(const widget *wid, const char *property) {
  ThemeWidget *wid2 = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p = sofi_theme_find_property(wid2, P_LIST, property, FALSE);
  GList *list = sofi_theme_get_list_inside(p, wid, property, P_STRING);
  GList *retv = NULL;
  for (GList *iter = g_list_first(list); iter != NULL;
       iter = g_list_next(iter)) {
    Property *prop = (Property *)(iter->data);
    if (prop->type == P_STRING) {
      retv = g_list_append(retv, g_strdup(prop->value.s));
    } else {
      g_warning("Invalid type detected in list.");
    }
  }
  return retv;
}

static SofiHighlightColorStyle
sofi_theme_get_highlight_inside(Property *p, widget *wid, const char *property,
                                SofiHighlightColorStyle th) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid->parent->name, wid->state, FALSE);
        Property *pv =
            sofi_theme_find_property(parent, P_HIGHLIGHT, property, FALSE);
        return sofi_theme_get_highlight_inside(pv, wid->parent, property, th);
      }
      return th;
    } else if (p->type == P_COLOR) {
      th.style = SOFI_HL_NONE | SOFI_HL_COLOR;
      th.color = p->value.color;
      return th;
    }

    return p->value.highlight;
  } else {
    ThemeWidget *find_wid =
        sofi_theme_find_widget(wid->name, wid->state, FALSE);
    Property *p2 = sofi_theme_find_property(find_wid, P_COLOR, property, FALSE);
    if (p2 != NULL) {
      return sofi_theme_get_highlight_inside(p2, wid, property, th);
    }
    return th;
  }
  // UNREACHABLE
}
SofiHighlightColorStyle sofi_theme_get_highlight(widget *wid,
                                                 const char *property,
                                                 SofiHighlightColorStyle th) {
  ThemeWidget *found_wid = sofi_theme_find_widget(wid->name, wid->state, FALSE);
  Property *p =
      sofi_theme_find_property(found_wid, P_HIGHLIGHT, property, FALSE);
  if (p == NULL) {
    p = sofi_theme_find_property(found_wid, P_COLOR, property, FALSE);
  }
  return sofi_theme_get_highlight_inside(p, wid, property, th);
}
static double get_pixels(SofiDistanceUnit *unit, SofiOrientation ori) {
  double val = unit->distance;

  if (unit->type == SOFI_PU_EM) {
    val = unit->distance * textbox_get_estimated_char_height();
  } else if (unit->type == SOFI_PU_CH) {
    val = unit->distance * textbox_get_estimated_ch();
  } else if (unit->type == SOFI_PU_PERCENT) {
    if (ori == SOFI_ORIENTATION_VERTICAL) {
      int height = 0;
      sofi_view_get_current_monitor(NULL, &height);
      val = (unit->distance * height) / (100.0);
    } else {
      int width = 0;
      sofi_view_get_current_monitor(&width, NULL);
      val = (unit->distance * width) / (100.0);
    }
  } else if (unit->type == SOFI_PU_MM) {
    val = unit->distance * config.dpi / 25.4;
  }
  return val;
}

static double distance_unit_get_pixel(SofiDistanceUnit *unit,
                                      SofiOrientation ori) {
  switch (unit->modtype) {
  case SOFI_DISTANCE_MODIFIER_GROUP:
    return distance_unit_get_pixel(unit->left, ori);
    break;
  case SOFI_DISTANCE_MODIFIER_ADD:
    return distance_unit_get_pixel(unit->left, ori) +
           distance_unit_get_pixel(unit->right, ori);
  case SOFI_DISTANCE_MODIFIER_SUBTRACT:
    return distance_unit_get_pixel(unit->left, ori) -
           distance_unit_get_pixel(unit->right, ori);
  case SOFI_DISTANCE_MODIFIER_MULTIPLY:
    return distance_unit_get_pixel(unit->left, ori) *
           distance_unit_get_pixel(unit->right, ori);
  case SOFI_DISTANCE_MODIFIER_DIVIDE: {
    double a = distance_unit_get_pixel(unit->left, ori);
    double b = distance_unit_get_pixel(unit->right, ori);
    if (b != 0) {
      return a / b;
    }
    return a;
  }
  case SOFI_DISTANCE_MODIFIER_MODULO: {
    double a = distance_unit_get_pixel(unit->left, ori);
    double b = distance_unit_get_pixel(unit->right, ori);
    if (b != 0) {
      return fmod(a, b);
    }
    return 0;
  }
  case SOFI_DISTANCE_MODIFIER_MIN: {
    double a = distance_unit_get_pixel(unit->left, ori);
    double b = distance_unit_get_pixel(unit->right, ori);
    return MIN(a, b);
  }
  case SOFI_DISTANCE_MODIFIER_MAX: {
    double a = distance_unit_get_pixel(unit->left, ori);
    double b = distance_unit_get_pixel(unit->right, ori);
    return MAX(a, b);
  }
  case SOFI_DISTANCE_MODIFIER_ROUND: {
    double a = (double)distance_unit_get_pixel(unit->left, ori);
    double b = (double)distance_unit_get_pixel(unit->right, ori);
    return (double)(round(a / b) * b);
  }
  case SOFI_DISTANCE_MODIFIER_CEIL: {
    double a = (double)distance_unit_get_pixel(unit->left, ori);
    double b = (double)distance_unit_get_pixel(unit->right, ori);
    return (double)(ceil(a / b) * b);
  }
  case SOFI_DISTANCE_MODIFIER_FLOOR: {
    double a = (double)distance_unit_get_pixel(unit->left, ori);
    double b = (double)distance_unit_get_pixel(unit->right, ori);
    return (double)(floor(a / b) * b);
  }
  default:
    break;
  }
  return get_pixels(unit, ori);
}

int distance_get_pixel(SofiDistance d, SofiOrientation ori) {
  return distance_unit_get_pixel(&(d.base), ori);
}

void distance_get_linestyle(SofiDistance d, cairo_t *draw) {
  if (d.style == SOFI_HL_DASH) {
    const double dashes[1] = {4};
    cairo_set_dash(draw, dashes, 1, 0.0);
  } else {
    cairo_set_dash(draw, NULL, 0, 0.0);
  }
}

char *sofi_theme_parse_prepare_file(const char *file) {
  char *filename = g_strdup(file);
  // TODO: Why did I write this code? I think it was to get full path.
  GFile *gf = g_file_new_for_path(filename);
  parsed_config_files = g_list_append(parsed_config_files, filename);
  filename = g_file_get_path(gf);
  g_object_unref(gf);

  return filename;
}

void sofi_theme_parse_merge_widgets(ThemeWidget *parent, ThemeWidget *child) {
  g_assert(parent != NULL);
  g_assert(child != NULL);

  if (parent == sofi_theme && g_strcmp0(child->name, "*") == 0) {
    sofi_theme_widget_add_properties(parent, child->properties);
    return;
  }

  ThemeWidget *w = sofi_theme_find_or_create_name(parent, child->name);
  if (child->media) {
    w->media = g_slice_new0(ThemeMedia);
    *(w->media) = *(child->media);
  }
  sofi_theme_widget_add_properties(w, child->properties);
  for (unsigned int i = 0; i < child->num_widgets; i++) {
    sofi_theme_parse_merge_widgets(w, child->widgets[i]);
  }
}

static void sofi_theme_parse_process_conditionals_int(workarea mon,
                                                      gboolean mon_known,
                                                      ThemeWidget *rwidget) {
  if (rwidget == NULL) {
    return;
  }
  unsigned int i = 0;
  //  for (unsigned int i = 0; i < rwidget->num_widgets; i++) {
  while (i < rwidget->num_widgets) {
    ThemeWidget *child_widget = rwidget->widgets[i];
    if (child_widget->media != NULL) {
      rwidget->num_widgets--;
      for (unsigned x = i; x < rwidget->num_widgets; x++) {
        rwidget->widgets[x] = rwidget->widgets[x + 1];
      }
      rwidget->widgets[rwidget->num_widgets] = NULL;

      /* Action purpose: every media type below except the boolean one tests a
       * property of the monitor, and `mon` is meaningful only when
       * monitor_active() filled it. Refusing them as a group is what stops a
       * failed lookup being read as a monitor of size zero -- under which
       * min-width never matches and, worse, max-width ALWAYS does, so an
       * unanswerable query would silently apply its block. The widget has
       * already been unlinked above, so the tree is still stripped correctly;
       * `continue` re-tests the same index, exactly as the paths below do. */
      if (!mon_known && child_widget->media->type != THEME_MEDIA_TYPE_BOOLEAN) {
        g_warning("A theme @media query needs the monitor's dimensions, which "
                  "this display backend could not supply. The block is "
                  "ignored.");
        sofi_theme_free(child_widget);
        continue;
      }

      switch (child_widget->media->type) {
      case THEME_MEDIA_TYPE_MIN_WIDTH: {
        int w = child_widget->media->value;
        if (mon.w >= w) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_MAX_WIDTH: {
        int w = child_widget->media->value;
        if (mon.w < w) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_MIN_HEIGHT: {
        int h = child_widget->media->value;
        if (mon.h >= h) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        } else {
        }
        break;
      }
      case THEME_MEDIA_TYPE_MAX_HEIGHT: {
        int h = child_widget->media->value;
        if (mon.h < h) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_MON_ID: {
        /* Action purpose: a negative id means the backend could not identify
         * the monitor, only measure it -- the wayland case, where the output a
         * surface landed on is not known until after the theme is resolved.
         * Refused with a warning rather than compared, because every real id
         * is a non-negative index and testing against -1 would silently drop
         * the block while looking like an evaluated rule. */
        if (mon.monitor_id < 0) {
          g_warning("@media (monitor-id: %d) cannot be evaluated on this "
                    "display backend, which cannot identify the monitor it is "
                    "displayed on. The block is ignored.",
                    (int)child_widget->media->value);
          break;
        }
        if (mon.monitor_id == child_widget->media->value) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_MIN_ASPECT_RATIO: {
        double r = child_widget->media->value;
        if ((mon.w / (double)mon.h) >= r) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_MAX_ASPECT_RATIO: {
        double r = child_widget->media->value;
        if ((mon.w / (double)mon.h) < r) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }
      case THEME_MEDIA_TYPE_BOOLEAN: {
        if (child_widget->media->boolv) {
          for (unsigned int x = 0; x < child_widget->num_widgets; x++) {
            sofi_theme_parse_merge_widgets(rwidget, child_widget->widgets[x]);
          }
        }
        break;
      }

      default: {
        break;
      }
      }
      sofi_theme_free(child_widget);
      // endif
    } else {
      sofi_theme_parse_process_conditionals_int(mon, mon_known, child_widget);
      i++;
    }
  }
}

static char *sofi_theme_widget_get_name(ThemeWidget *wid) {
  GString *str = g_string_new(wid->name);
  for (ThemeWidget *i = wid->parent; i->parent != NULL; i = i->parent) {
    g_string_prepend_c(str, ' ');
    g_string_prepend(str, i->name);
  }
  char *retv = str->str;
  g_string_free(str, FALSE);
  return retv;
}

static void sofi_theme_parse_process_links_int(ThemeWidget *wid) {
  if (wid == NULL) {
    return;
  }

  for (unsigned int i = 0; i < wid->num_widgets; i++) {
    ThemeWidget *child_widget = wid->widgets[i];
    sofi_theme_parse_process_links_int(child_widget);
    if (child_widget->properties == NULL) {
      continue;
    }
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, child_widget->properties);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
      Property *pv = (Property *)value;
      if (pv->type == P_LINK) {
        if (pv->value.link.ref == NULL) {
          sofi_theme_resolve_link_property(pv, 0);
          if (pv->value.link.ref == pv) {
            /* Action purpose: the rule is quoted in the syntax the theme was
             * written in, because the point of the message is that the reader
             * can go and find the line. It named `widget { prop: var(x); }` --
             * three constructs this format does not have -- which sent anyone
             * following it looking for a spelling that cannot occur. */
            char *n = sofi_theme_widget_get_name(child_widget);
            char *spelled = sofi_theme_lua_key(pv->name);
            GString *str = g_string_new(NULL);
            g_string_printf(str,
                            "Validating the theme failed: the reference `@%s` "
                            "in `{ select = \"%s\", %s = \"@%s\" }` failed to "
                            "resolve.",
                            pv->value.link.name, n, spelled,
                            pv->value.link.name);

            sofi_add_warning_message(str);
            g_free(spelled);
            g_free(n);
          }
        }
      }
    }
  }
}

void sofi_theme_parse_process_links(void) {
  sofi_theme_parse_process_links_int(sofi_theme);
}

void sofi_theme_parse_process_conditionals(void) {
  /* Action purpose: monitor_active() leaves this struct untouched on every
   * path that returns FALSE, so it is zeroed first -- the traversal below runs
   * either way, because it also strips the @media blocks out of the widget
   * tree, and that must happen whether or not a query can be evaluated.
   * Its return value is carried separately rather than inferred from the
   * contents: there is no value of `mon` that means "unknown" to a max-width
   * test, which matches anything larger than what it is given. */
  workarea mon = {0};
  mon.monitor_id = -1;
  gboolean mon_known = monitor_active(&mon) ? TRUE : FALSE;
  sofi_theme_parse_process_conditionals_int(mon, mon_known, sofi_theme);
}

ThemeMediaType sofi_theme_parse_media_type(const char *type) {
  if (g_strcmp0(type, "monitor-id") == 0) {
    return THEME_MEDIA_TYPE_MON_ID;
  }
  if (g_strcmp0(type, "min-width") == 0) {
    return THEME_MEDIA_TYPE_MIN_WIDTH;
  }
  if (g_strcmp0(type, "min-height") == 0) {
    return THEME_MEDIA_TYPE_MIN_HEIGHT;
  }
  if (g_strcmp0(type, "max-width") == 0) {
    return THEME_MEDIA_TYPE_MAX_WIDTH;
  }
  if (g_strcmp0(type, "max-height") == 0) {
    return THEME_MEDIA_TYPE_MAX_HEIGHT;
  }
  if (g_strcmp0(type, "min-aspect-ratio") == 0) {
    return THEME_MEDIA_TYPE_MIN_ASPECT_RATIO;
  }
  if (g_strcmp0(type, "max-aspect-ratio") == 0) {
    return THEME_MEDIA_TYPE_MAX_ASPECT_RATIO;
  }
  if (g_strcmp0(type, "enabled") == 0) {
    return THEME_MEDIA_TYPE_BOOLEAN;
  }
  return THEME_MEDIA_TYPE_INVALID;
}

static gboolean sofi_theme_has_property_inside(Property *p,
                                               const widget *wid_in,
                                               const char *property) {
  if (p) {
    if (p->type == P_INHERIT) {
      if (wid_in->parent) {
        ThemeWidget *parent =
            sofi_theme_find_widget(wid_in->parent->name, wid_in->state, FALSE);
        Property *pp =
            sofi_theme_find_property(parent, P_STRING, property, FALSE);
        return sofi_theme_has_property_inside(pp, wid_in->parent, property);
      }
      return FALSE;
    }
    return TRUE;
  }
  return FALSE;
}
gboolean sofi_theme_has_property(const widget *wid_in, const PropertyType type,
                                 const char *property) {
  ThemeWidget *wid = sofi_theme_find_widget(wid_in->name, wid_in->state, FALSE);
  Property *p = sofi_theme_find_property(wid, type, property, FALSE);
  return sofi_theme_has_property_inside(p, wid_in, property);
}

void sofi_theme_set_disp_scale_func(disp_scale_func func) { disp_scale = func; }
