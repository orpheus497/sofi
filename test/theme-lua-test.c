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

#include "css-colors.h"
#include "display.h"
#include "settings.h"
#include "sofi-icon-fetcher.h"
#include "sofi.h"
#include "theme-lua.h"
#include "theme.h"
#include "widgets/textbox.h"
#include "widgets/widget-internal.h"
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <helper.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

#include <check.h>

#define REAL_COMPARE_DELTA 0.001

int sofi_is_in_dmenu_mode = 0;

uint32_t sofi_icon_fetcher_query(G_GNUC_UNUSED const char *name,
                                 G_GNUC_UNUSED const int size) {
  return 0;
}
void sofi_clear_error_messages(void) {}
void sofi_clear_warning_messages(void) {}
uint32_t
sofi_icon_fetcher_query_advanced(G_GNUC_UNUSED const char *name,
                                 G_GNUC_UNUSED G_GNUC_UNUSED const int wsize,
                                 G_GNUC_UNUSED const int hsize) {
  return 0;
}

cairo_surface_t *sofi_icon_fetcher_get(G_GNUC_UNUSED const uint32_t uid) {
  return NULL;
}

int sofi_view_error_dialog(const char *msg, G_GNUC_UNUSED int markup) {
  fputs(msg, stderr);
  return TRUE;
}

void sofi_view_get_current_monitor(int *width, int *height) {
  if (width) {
    *width = 1920;
  }
  if (height) {
    *height = 1080;
  }
}
double textbox_get_estimated_char_height(void) { return 16.0; }

double textbox_get_estimated_ch(void) { return 8.0; }

int monitor_active(G_GNUC_UNUSED workarea *mon) { return 0; }

void display_startup_notification(
    G_GNUC_UNUSED SofiHelperExecuteContext *context,
    G_GNUC_UNUSED GSpawnChildSetupFunc *child_setup,
    G_GNUC_UNUSED gpointer *user_data) {}

#ifndef _ck_assert_ptr_null
/* Pointer against NULL comparison macros with improved output
 * compared to ck_assert(). */
/* OP may only be == or !=  */
#define _ck_assert_ptr_null(X, OP)                                             \
  do {                                                                         \
    const void *_ck_x = (X);                                                   \
    ck_assert_msg(_ck_x OP NULL, "Assertion '%s' failed: %s == %#x",           \
                  #X " " #OP " NULL", #X, _ck_x);                              \
  } while (0)

#define ck_assert_ptr_null(X) _ck_assert_ptr_null(X, ==)
#define ck_assert_ptr_nonnull(X) _ck_assert_ptr_null(X, !=)
#endif

gboolean error = FALSE;
GString *error_msg = NULL;
gboolean warning = FALSE;
GString *warning_msg = NULL;
void sofi_add_error_message(GString *msg) {
  ck_assert_ptr_null(error_msg);
  error_msg = msg;
  error = TRUE;
}
void sofi_add_warning_message(GString *msg) {
  ck_assert_ptr_null(warning_msg);
  warning_msg = msg;
  warning = TRUE;
}

/* The reader is loaded before a surface exists, so it reports a refusal with
 * g_warning() and a return value rather than through sofi_add_error_message().
 * Counting them here is what lets a case assert that something was complained
 * about, and keeps the expected complaints out of the test output. */
unsigned int logged = 0;
static void theme_lua_log(G_GNUC_UNUSED const gchar *domain,
                          GLogLevelFlags level,
                          G_GNUC_UNUSED const gchar *message,
                          G_GNUC_UNUSED gpointer data) {
  if (level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL)) {
    logged++;
  }
}

/* The import cases need files on disk. They are written into a directory of
 * their own and taken away again, so the repository carries no fixtures. */
static char *fixture_dir = NULL;

static char *theme_lua_fixture(const char *name, const char *content) {
  if (fixture_dir == NULL) {
    fixture_dir = g_dir_make_tmp("sofi-theme-lua-XXXXXX", NULL);
    ck_assert_ptr_nonnull(fixture_dir);
  }

  char *path = g_build_filename(fixture_dir, name, NULL);
  ck_assert_int_eq(g_file_set_contents(path, content, -1, NULL), TRUE);

  return path;
}

static void theme_lua_fixture_clean(void) {
  if (fixture_dir == NULL) {
    return;
  }

  GDir *dir = g_dir_open(fixture_dir, 0, NULL);

  if (dir != NULL) {
    const char *entry;

    while ((entry = g_dir_read_name(dir)) != NULL) {
      char *path = g_build_filename(fixture_dir, entry, NULL);
      g_unlink(path);
      g_free(path);
    }

    g_dir_close(dir);
  }

  g_rmdir(fixture_dir);
  g_free(fixture_dir);
  fixture_dir = NULL;
}

static void theme_lua_setup(void) {
  error = 0;
  logged = 0;
}
static void theme_lua_teardown(void) {
  ck_assert_ptr_null(error_msg);
  ck_assert_int_eq(error, 0);
  theme_lua_fixture_clean();
  sofi_theme_free(sofi_theme);
  sofi_theme = NULL;
}

/** @returns the root's own property table entry for @p name. */
static Property *root_property(const char *name) {
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_nonnull(sofi_theme->properties);

  return g_hash_table_lookup(sofi_theme->properties, name);
}

START_TEST(test_core_empty_theme) {
  ck_assert_int_eq(sofi_theme_parse_string("return {}"), FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_int_eq(sofi_theme->num_widgets, 0);
  ck_assert_str_eq(sofi_theme->name, "Root");
}
END_TEST
START_TEST(test_core_empty_global_section) {
  ck_assert_int_eq(sofi_theme_parse_string("return { { select = '*' } }"),
                   FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_int_eq(sofi_theme->num_widgets, 0);
  ck_assert_str_eq(sofi_theme->name, "Root");
}
END_TEST
START_TEST(test_core_empty_section) {
  ck_assert_int_eq(sofi_theme_parse_string("return { { select = 'test' } }"),
                   FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_nonnull(sofi_theme->widgets);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  ck_assert_int_eq(sofi_theme->num_widgets, 1);
  ck_assert_str_eq(sofi_theme->widgets[0]->name, "test");
  ck_assert_ptr_null(sofi_theme->widgets[0]->properties);
  ck_assert_ptr_eq(sofi_theme->widgets[0]->parent, sofi_theme);
}
END_TEST
START_TEST(test_core_error_root) {
  ck_assert_int_eq(sofi_theme_parse_string("Blaat"), TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_core_error_not_a_table) {
  ck_assert_int_eq(sofi_theme_parse_string("return 42"), TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);

  /* A chunk that returns nothing returns nil, which is not a table either. */
  ck_assert_int_eq(sofi_theme_parse_string(""), TRUE);
  ck_assert_ptr_null(sofi_theme);
}
END_TEST
START_TEST(test_core_error_no_select) {
  /* A directive that names neither a selector nor a block names nothing. It is
   * complained about and the rest of the theme still applies. */
  ck_assert_int_eq(
      sofi_theme_parse_string("return { { background_color = '#fff' },"
                              "         { select = '*', test = 4 } }"),
      FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_gt(logged, 0);
  ck_assert_ptr_null(root_property("background-color"));
  ck_assert_ptr_nonnull(root_property("test"));
}
END_TEST

START_TEST(test_core_comments) {
  ck_assert_int_eq(
      sofi_theme_parse_string("-- Random comments -- /*test */\nreturn {}"),
      FALSE);
  ck_assert_int_eq(sofi_theme_parse_string("--[[ test \n*\n* ]]\nreturn {}"),
                   FALSE);
  ck_assert_int_eq(
      sofi_theme_parse_string("--[==[ nested --[[ aap ]] ]==]\nreturn {}"),
      FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");

  /* A comment on the last line, with no newline after it. */
  ck_assert_int_eq(sofi_theme_parse_string("return {} -- trailing"), FALSE);
}
END_TEST
START_TEST(test_core_newline) {
  ck_assert_int_eq(
      sofi_theme_parse_string("\r\n\n\r\n\n--[[\r\n]]\r\nreturn {}"), FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
}
END_TEST
START_TEST(test_core_value_unknown_type) {
  /* A Lua value of a type no property has: dropped with a complaint, and the
   * properties beside it are kept. */
  ck_assert_int_eq(
      sofi_theme_parse_string(
          "return { { select = '*', test = tostring, keep = 4 } }"),
      FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_gt(logged, 0);
  ck_assert_ptr_null(root_property("test"));
  ck_assert_ptr_nonnull(root_property("keep"));

  /* An array of a size padding has no meaning for is the same case. */
  ck_assert_int_eq(
      sofi_theme_parse_string("return { { select = '*', five ="
                              " { px(1), px(2), px(3), px(4), px(5) } } }"),
      FALSE);
  ck_assert_ptr_null(root_property("five"));
}
END_TEST
START_TEST(test_core_properties_error) {
  /* An undefined name is a runtime error with a line number, which fails the
   * whole theme and leaves the previous tree standing. */
  ck_assert_int_eq(sofi_theme_parse_string(
                       "return { { select = '*', test = cmky(1,2,3) } }"),
                   TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);

  /* So is a constructor given something it cannot read. */
  ck_assert_int_eq(
      sofi_theme_parse_string(
          "return { { select = '*', test = color('notacolour') } }"),
      TRUE);
  ck_assert_ptr_null(sofi_theme);
}
END_TEST

START_TEST(test_properties_boolean) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  /** Boolean property */
  sofi_theme_parse_string("return { { select = '*', test = true,"
                          " test2 = false } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_nonnull(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_int_eq(root_property("test")->type, P_BOOLEAN);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "test", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "test2", TRUE), FALSE);
}
END_TEST

START_TEST(test_properties_boolean_reference) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = true,"
                          " test2 = false },"
                          " { select = '*', a = '@test', b = '@test2' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  /* `@name` is a reference, which is the one thing the `@` prefix means. */
  Property *p = root_property("a");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LINK);
  ck_assert_str_eq(p->value.link.name, "test");
  ck_assert_ptr_null(p->value.link.def_value);

  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "test", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "b", TRUE), FALSE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "test2", TRUE), FALSE);
}
END_TEST

START_TEST(test_properties_reference) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = '*', width = percent(30) },"
                          " { select = 'window', a = ref('width'),"
                          "   b = ref('nowhere', px(128)) } }");
  ThemeWidget *twid = sofi_theme_find_widget("window", NULL, TRUE);
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_nonnull(twid->properties);

  Property *p = g_hash_table_lookup(twid->properties, "a");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LINK);
  ck_assert_str_eq(p->value.link.name, "width");
  ck_assert_ptr_null(p->value.link.def_value);
  ck_assert_ptr_null(p->value.link.ref);

  p = g_hash_table_lookup(twid->properties, "b");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LINK);
  ck_assert_str_eq(p->value.link.name, "nowhere");
  ck_assert_ptr_nonnull(p->value.link.def_value);
  ck_assert_int_eq(p->value.link.def_value->type, P_PADDING);
  ck_assert_int_eq(p->value.link.def_value->value.padding.left.base.distance,
                   128);

  /* The reference resolves against the root; the fallback stands in for the
   * one that is not there. */
  SofiDistance d = sofi_theme_get_distance(&wid, "a", 0);
  ck_assert_int_eq(distance_get_pixel(d, SOFI_ORIENTATION_HORIZONTAL), 576);
  d = sofi_theme_get_distance(&wid, "b", 0);
  ck_assert_int_eq(distance_get_pixel(d, SOFI_ORIENTATION_HORIZONTAL), 128);
}
END_TEST

START_TEST(test_properties_distance_em) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = em(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_int_eq(p.left.base.distance, 10);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_EM);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
}
END_TEST
START_TEST(test_properties_distance_em_linestyle) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', sol = em(1.3, solid),"
                          " dash = em(1.5, dash) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "sol", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 1.3, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_EM);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);

  p = sofi_theme_get_padding(&wid, "dash", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 1.5, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_EM);
  ck_assert_int_eq(p.left.style, SOFI_HL_DASH);
}
END_TEST
START_TEST(test_properties_distance_ch) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = ch(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_int_eq(p.left.base.distance, 10);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_CH);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
}
END_TEST
START_TEST(test_properties_distance_ch_linestyle) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', sol = ch(1.3, solid),"
                          " dash = ch(1.5, dash) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "sol", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 1.3, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_CH);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);

  p = sofi_theme_get_padding(&wid, "dash", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 1.5, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_CH);
  ck_assert_int_eq(p.left.style, SOFI_HL_DASH);
}
END_TEST
START_TEST(test_properties_distance_px) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = px(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
}
END_TEST
START_TEST(test_properties_distance_px_linestyle) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', sol = px(10, solid),"
                          " dash = px(14, dash) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "sol", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
  p = sofi_theme_get_padding(&wid, "dash", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 14.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_int_eq(p.left.style, SOFI_HL_DASH);
}
END_TEST
START_TEST(test_properties_distance_percent) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = percent(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PERCENT);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
}
END_TEST
START_TEST(test_properties_distance_percent_linestyle) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', sol = percent(10, solid),"
                          " dash = percent(10, dash) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "sol", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PERCENT);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
  p = sofi_theme_get_padding(&wid, "dash", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PERCENT);
  ck_assert_int_eq(p.left.style, SOFI_HL_DASH);
}
END_TEST

START_TEST(test_properties_distance_mm) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = mm(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_MM);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
}
END_TEST

START_TEST(test_properties_distance_mm_linestyle) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', sol = mm(10, solid),"
                          " dash = mm(10, dash) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_EM, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_DASH};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "sol", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10.0, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_MM);
  ck_assert_int_eq(p.left.style, SOFI_HL_SOLID);
  p = sofi_theme_get_padding(&wid, "dash", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_MM);
  ck_assert_int_eq(p.left.style, SOFI_HL_DASH);
}
END_TEST
START_TEST(test_properties_distance_bare_number) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  /* A bare number used where a length belongs is pixels, which is what an
   * unsuffixed number was before. */
  sofi_theme_parse_string("return { { select = '*', test = 10 } }");
  ck_assert_int_eq(root_property("test")->type, P_INTEGER);
  SofiDistance l = sofi_theme_get_distance(&wid, "test", 0);
  ck_assert_int_eq(l.base.type, SOFI_PU_PX);
  ck_assert_int_eq(distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL), 10);
}
END_TEST
START_TEST(test_properties_position) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', center = center,"
                          " east = east, west = west, south = south,"
                          " north = north } }");
  ck_assert_int_eq(root_property("center")->type, P_POSITION);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "center", WL_SOUTH),
                   WL_CENTER);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "south", WL_EAST), WL_SOUTH);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "east", WL_WEST), WL_EAST);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "west", WL_NORTH), WL_WEST);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "north", WL_CENTER), WL_NORTH);

  sofi_theme_parse_string("return { { select = '*', southwest = south_west,"
                          " southeast = south_east, northwest = north_west,"
                          " northeast = north_east } }");
  ck_assert_int_eq(sofi_theme_get_position(&wid, "southwest", WL_EAST),
                   WL_SOUTH_WEST);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "southeast", WL_WEST),
                   WL_SOUTH_EAST);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "northwest", WL_NORTH),
                   WL_NORTH_WEST);
  ck_assert_int_eq(sofi_theme_get_position(&wid, "northeast", WL_CENTER),
                   WL_NORTH_EAST);
}
END_TEST

START_TEST(test_properties_style) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', none = none, bold = bold,"
                          " underline = underline, italic = italic,"
                          " strike = strikethrough, upper = uppercase,"
                          " lower = lowercase, cap = capitalize,"
                          " st = highlight(italic, strikethrough) } }");
  SofiHighlightColorStyle th = {SOFI_HL_BOLD, {0.0, 0.0, 0.0, 0.0}};
  ck_assert_int_eq(root_property("bold")->type, P_HIGHLIGHT);
  th = sofi_theme_get_highlight(&wid, "none", th);
  ck_assert_int_eq(th.style, SOFI_HL_NONE);
  th = sofi_theme_get_highlight(&wid, "underline", th);
  ck_assert_int_eq(th.style, SOFI_HL_UNDERLINE);
  th = sofi_theme_get_highlight(&wid, "italic", th);
  ck_assert_int_eq(th.style, SOFI_HL_ITALIC);
  th = sofi_theme_get_highlight(&wid, "bold", th);
  ck_assert_int_eq(th.style, SOFI_HL_BOLD);
  th = sofi_theme_get_highlight(&wid, "strike", th);
  ck_assert_int_eq(th.style, SOFI_HL_STRIKETHROUGH);
  th = sofi_theme_get_highlight(&wid, "upper", th);
  ck_assert_int_eq(th.style, SOFI_HL_UPPERCASE);
  th = sofi_theme_get_highlight(&wid, "lower", th);
  ck_assert_int_eq(th.style, SOFI_HL_LOWERCASE);
  th = sofi_theme_get_highlight(&wid, "cap", th);
  ck_assert_int_eq(th.style, SOFI_HL_CAPITALIZE);
  th = sofi_theme_get_highlight(&wid, "st", th);
  ck_assert_int_eq(th.style, SOFI_HL_ITALIC | SOFI_HL_STRIKETHROUGH);
}
END_TEST
START_TEST(test_properties_style2) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;

  sofi_theme_parse_string(
      "return { { select = '*', boldu = highlight(bold, underline),"
      " boldi = highlight(bold, italic),"
      " underlinei = highlight(underline, italic),"
      " italicu = highlight(italic, underline) } }");
  SofiHighlightColorStyle th = {SOFI_HL_BOLD, {0.0, 0.0, 0.0, 0.0}};
  th = sofi_theme_get_highlight(&wid, "boldu", th);
  ck_assert_int_eq(th.style, (SOFI_HL_UNDERLINE | SOFI_HL_BOLD));
  th = sofi_theme_get_highlight(&wid, "boldi", th);
  ck_assert_int_eq(th.style, (SOFI_HL_ITALIC | SOFI_HL_BOLD));
  th = sofi_theme_get_highlight(&wid, "underlinei", th);
  ck_assert_int_eq(th.style, (SOFI_HL_ITALIC | SOFI_HL_UNDERLINE));
  th = sofi_theme_get_highlight(&wid, "italicu", th);
  ck_assert_int_eq(th.style, (SOFI_HL_ITALIC | SOFI_HL_UNDERLINE));
}
END_TEST
START_TEST(test_properties_style_color) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " comb = highlight(bold, '#123'),"
                          " named = highlight(bold, color('red')) } }");
  SofiHighlightColorStyle th = {SOFI_HL_BOLD, {0.0, 0.0, 0.0, 0.0}};
  th = sofi_theme_get_highlight(&wid, "comb", th);
  ck_assert_int_eq(th.style, (SOFI_HL_BOLD | SOFI_HL_COLOR));
  ck_assert_double_eq_tol(th.color.red, (1 / 15.0), REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(th.color.green, (2 / 15.0), REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(th.color.blue, (3 / 15.0), REAL_COMPARE_DELTA);

  th = sofi_theme_get_highlight(&wid, "named", th);
  ck_assert_int_eq(th.style, (SOFI_HL_BOLD | SOFI_HL_COLOR));
  ck_assert_double_eq_tol(th.color.red, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(th.color.green, 0.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(th.color.blue, 0.0, REAL_COMPARE_DELTA);
}
END_TEST

START_TEST(test_properties_color_h3) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', red = '#F00',"
                          " green = '#0F0', blue = '#00F' } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
  Property *p = sofi_theme_find_property(twid, P_COLOR, "red", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "green", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "blue", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 1, REAL_COMPARE_DELTA);
}
END_TEST

START_TEST(test_properties_color_h6) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', red = '#FF0000',"
                          " green = '#00FF00', blue = '#0000FF' } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
  Property *p = sofi_theme_find_property(twid, P_COLOR, "red", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "green", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "blue", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 1, REAL_COMPARE_DELTA);
}
END_TEST

START_TEST(test_properties_color_h8) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', red = '#FF000033',"
                          " green = '#00FF0022', blue = '#0000FF11' } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
  Property *p = sofi_theme_find_property(twid, P_COLOR, "red", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.2, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "green", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1 / 7.5, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "blue", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1 / 15.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 1, REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_properties_color_hex_digits) {
  /* Three, four, six or eight digits after the `#`. Any other count is not a
   * colour and the property is dropped. Four is the short form WITH alpha,
   * which the format before this one accepted. */
  sofi_theme_parse_string("return { { select = '*', ok3 = '#F00',"
                          " ok4 = '#F003', ok6 = '#FF0000',"
                          " ok8 = '#FF000033', bad5 = '#FF000',"
                          " bad7 = '#FF00003', badx = '#GGGGGG' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(root_property("ok3")->type, P_COLOR);
  ck_assert_int_eq(root_property("ok4")->type, P_COLOR);
  ck_assert_int_eq(root_property("ok6")->type, P_COLOR);
  ck_assert_int_eq(root_property("ok8")->type, P_COLOR);
  ck_assert_ptr_null(root_property("bad5"));
  ck_assert_ptr_null(root_property("bad7"));
  ck_assert_ptr_null(root_property("badx"));

  /* Each nibble is doubled rather than shifted, so `#F003` is the same red as
   * `#FF0000` at the alpha `3` stands for -- which is what the previous
   * format's own suite pinned. */
  Property *p = root_property("ok4");
  ck_assert_double_eq_tol(p->value.color.red, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.alpha, (3 * 17) / 255.0,
                          REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_properties_color_rgb) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', red = rgb(255, 0, 0),"
                          " green = rgb(0, 255, 0), blue = rgb(0, 0, 255) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
  Property *p = sofi_theme_find_property(twid, P_COLOR, "red", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "green", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "blue", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 1, REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_properties_color_rgba) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  /* `rgb` and `rgba` are one function; the alpha is optional on both. */
  sofi_theme_parse_string("return { { select = '*',"
                          " red = rgba(255, 0, 0, 0.3),"
                          " green = rgba(0, 255, 0, 0.2),"
                          " blue = rgb(0, 0, 255, 0.7) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
  ck_assert_ptr_nonnull(twid);
  Property *p = sofi_theme_find_property(twid, P_COLOR, "red", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.3, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "green", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.2, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 1, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 0, REAL_COMPARE_DELTA);
  p = sofi_theme_find_property(twid, P_COLOR, "blue", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.7, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.green, 0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.blue, 1, REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_properties_color_hsl) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test1 = hsl(127, 0.40, 0.6666666),"
                          " test2 = hsl(0, 1.0, 0.5),"
                          " testa = hsl(127, 0.40, 0.6666666, 0.3) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);

  Property *p = sofi_theme_find_property(twid, P_COLOR, "test1", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x88 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0xcd / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0x90 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "test2", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "testa", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.3, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x88 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0xcd / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0x90 / 255.0, 0.004);
}
END_TEST
START_TEST(test_properties_color_hsla) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test1 = hsla(127, 0.40, 0.6666666, 0.40),"
                          " test2 = hsla(0, 1.0, 0.5, 0.55) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);

  Property *p = sofi_theme_find_property(twid, P_COLOR, "test1", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.4, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x88 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0xcd / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0x90 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "test2", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.55, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0, 0.004);
}
END_TEST
START_TEST(test_properties_color_hwb) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test1 = hwb(190, 0.65, 0.0),"
                          " test2 = hwb(265, 0.31, 0.29),"
                          " testa = hwb(265, 0.31, 0.29, 0.40) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);

  Property *p = sofi_theme_find_property(twid, P_COLOR, "test2", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x7a / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0x4f / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0xb5 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "test1", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 166 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 240 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 255 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "testa", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.4, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x7a / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0x4f / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0xb5 / 255.0, 0.004);
}
END_TEST
START_TEST(test_properties_color_hwb_angles) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  /* The four angle units are arithmetic now: gradians are n * 0.9, turns
   * n * 360 and radians n * 180 / math.pi. */
  sofi_theme_parse_string("return { { select = '*',"
                          " test1 = hwb(190, 0.65, 0.0),"
                          " test2 = hwb(295 * 0.9, 0.31, 0.29),"
                          " testa = hwb(0.736 * 360, 0.31, 0.29, 0.40),"
                          " rada = hwb(0.2 * 180 / math.pi,"
                          "            0.30, 0.30, 0.40) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);

  Property *p = sofi_theme_find_property(twid, P_COLOR, "test2", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x7a / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0x4f / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0xb5 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "test1", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 166 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 240 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 255 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "testa", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.4, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x7a / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0x4f / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0xb5 / 255.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "rada", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.4, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0.7, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0.376, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0.3, 0.004);
}
END_TEST
START_TEST(test_properties_color_cmyk) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test1 = cmyk(0.41, 0.0, 1.0, 0.0),"
                          " test2 = cmyk(0, 1.0, 1.0, 0),"
                          " testa = cmyk(0, 1.0, 1.0, 0, 0.4) } }");
  ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);

  Property *p = sofi_theme_find_property(twid, P_COLOR, "test1", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 0x96 / 255.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 1.0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0.0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "test2", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, 0.004);
  ck_assert_double_eq_tol(p->value.color.green, 0, 0.004);
  ck_assert_double_eq_tol(p->value.color.blue, 0, 0.004);
  p = sofi_theme_find_property(twid, P_COLOR, "testa", FALSE);
  ck_assert_ptr_nonnull(p);
  ck_assert_double_eq_tol(p->value.color.alpha, 0.4, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p->value.color.red, 1, 0.004);
}
END_TEST
START_TEST(test_properties_color_names) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  for (unsigned int iter = 0; iter < num_CSSColors; iter++) {
    char *str = g_strdup_printf("return { { select = '*',"
                                " ['text-color'] = color('%s') } }",
                                CSSColors[iter].name);
    sofi_theme_parse_string(str);
    ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
    Property *p = sofi_theme_find_property(twid, P_COLOR, "text-color", FALSE);
    ck_assert_ptr_nonnull(p);
    ck_assert_double_eq_tol(p->value.color.alpha, 1.0, REAL_COMPARE_DELTA);
    ck_assert_double_eq_tol(p->value.color.red, CSSColors[iter].r / 255.0,
                            0.004);
    ck_assert_double_eq_tol(p->value.color.green, CSSColors[iter].g / 255.0,
                            0.004);
    ck_assert_double_eq_tol(p->value.color.blue, CSSColors[iter].b / 255.0,
                            0.004);

    g_free(str);
  }
  {
    /* The name is matched without regard to case, as the old lexer matched
     * it. */
    sofi_theme_parse_string("return { { select = '*',"
                            " ['text-color'] = color('CornFlowerBlue') } }");
    ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
    Property *p = sofi_theme_find_property(twid, P_COLOR, "text-color", FALSE);
    ck_assert_ptr_nonnull(p);
    ck_assert_double_eq_tol(p->value.color.red, 100 / 255.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.green, 149 / 255.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.blue, 237 / 255.0, 0.004);
  }
  {
    /* `color()` takes hex too, so there is one way to write a literal colour
     * whichever notation the author prefers. */
    sofi_theme_parse_string("return { { select = '*',"
                            " ['text-color'] = color('#2b1e3a') } }");
    ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
    Property *p = sofi_theme_find_property(twid, P_COLOR, "text-color", FALSE);
    ck_assert_ptr_nonnull(p);
    ck_assert_double_eq_tol(p->value.color.red, 0x2b / 255.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.green, 0x1e / 255.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.blue, 0x3a / 255.0, 0.004);
  }
  {
    sofi_theme_parse_string("return { { select = '*',"
                            " ['text-color'] = transparent } }");
    ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
    Property *p = sofi_theme_find_property(twid, P_COLOR, "text-color", FALSE);
    ck_assert_ptr_nonnull(p);
    ck_assert_double_eq_tol(p->value.color.alpha, 0.0, REAL_COMPARE_DELTA);
    ck_assert_double_eq_tol(p->value.color.red, 0.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.green, 0.0, 0.004);
    ck_assert_double_eq_tol(p->value.color.blue, 0.0, 0.004);
  }
}
END_TEST
START_TEST(test_properties_color_names_alpha) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  for (unsigned int iter = 0; iter < num_CSSColors; iter++) {
    char *str = g_strdup_printf("return { { select = '*',"
                                " ['text-color'] = color('%s', %u / 100.0) } }",
                                CSSColors[iter].name, iter % 101);
    sofi_theme_parse_string(str);
    ThemeWidget *twid = sofi_theme_find_widget(wid.name, wid.state, FALSE);
    Property *p = sofi_theme_find_property(twid, P_COLOR, "text-color", FALSE);
    ck_assert_ptr_nonnull(p);
    ck_assert_double_eq_tol(p->value.color.alpha, (iter % 101) / 100.0,
                            REAL_COMPARE_DELTA);
    ck_assert_double_eq_tol(p->value.color.red, CSSColors[iter].r / 255.0,
                            0.004);
    ck_assert_double_eq_tol(p->value.color.green, CSSColors[iter].g / 255.0,
                            0.004);
    ck_assert_double_eq_tol(p->value.color.blue, CSSColors[iter].b / 255.0,
                            0.004);

    g_free(str);
  }
}
END_TEST
START_TEST(test_properties_padding_1) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = px(6) } }");
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.top.base.distance, 6, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p.right.base.distance, 6, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p.bottom.base.distance, 6, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(p.left.base.distance, 6, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
}
END_TEST
START_TEST(test_properties_padding_2) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test = { px(10), px(20) } } }");
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 20, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.right.base.distance, 20, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.right.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.top.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.top.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.bottom.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.bottom.base.type, SOFI_PU_PX);
}
END_TEST
START_TEST(test_properties_padding_3) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test = { px(10), px(30), px(20) } } }");
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 30, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.right.base.distance, 30, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.right.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.top.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.top.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.bottom.base.distance, 20, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.bottom.base.type, SOFI_PU_PX);
}
END_TEST
START_TEST(test_properties_padding_4) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " test = { px(10), px(30), px(20), px(40) } } }");
  SofiDistance d = (SofiDistance){
      {1, SOFI_PU_PX, SOFI_DISTANCE_MODIFIER_NONE, NULL, NULL}, SOFI_HL_SOLID};
  SofiPadding pi = (SofiPadding){d, d, d, d};
  SofiPadding p = sofi_theme_get_padding(&wid, "test", pi);
  ck_assert_double_eq_tol(p.left.base.distance, 40, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.left.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.right.base.distance, 30, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.right.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.top.base.distance, 10, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.top.base.type, SOFI_PU_PX);
  ck_assert_double_eq_tol(p.bottom.base.distance, 20, REAL_COMPARE_DELTA);
  ck_assert_int_eq(p.bottom.base.type, SOFI_PU_PX);
}
END_TEST

START_TEST(test_properties_string_escape) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " font = 'aap\" noot\" mies ',"
                          " test = [['123.432']] } }");

  const char *str = sofi_theme_get_string(&wid, "font", NULL);
  ck_assert_ptr_nonnull(str);
  ck_assert_int_eq(g_utf8_collate(str, "aap\" noot\" mies "), 0);
  const char *str2 = sofi_theme_get_string(&wid, "test", NULL);
  ck_assert_ptr_nonnull(str2);
  ck_assert_int_eq(g_utf8_collate(str2, "'123.432'"), 0);
}
END_TEST
START_TEST(test_properties_string) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', font = 'blaat€',"
                          " test = 123.432 } }");

  ck_assert_int_eq(root_property("font")->type, P_STRING);
  const char *str = sofi_theme_get_string(&wid, "font", NULL);
  ck_assert_ptr_nonnull(str);
  ck_assert_int_eq(g_utf8_collate(str, "blaat€"), 0);
}
END_TEST
START_TEST(test_properties_string_literal) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  /* `str()` is the escape hatch for a string that must keep a `#` or an `@`,
   * which are the two prefixes that otherwise make a colour and a
   * reference. */
  sofi_theme_parse_string("return { { select = '*',"
                          " chan = str('#channel'),"
                          " at = str('@notalink') } }");
  ck_assert_int_eq(root_property("chan")->type, P_STRING);
  ck_assert_int_eq(root_property("at")->type, P_STRING);
  ck_assert_str_eq(sofi_theme_get_string(&wid, "chan", NULL), "#channel");
  ck_assert_str_eq(sofi_theme_get_string(&wid, "at", NULL), "@notalink");
}
END_TEST
START_TEST(test_properties_double) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', test = 123.432 } }");
  ck_assert_int_eq(root_property("test")->type, P_DOUBLE);
  ck_assert_double_eq_tol(sofi_theme_get_double(&wid, "test", 0.0), 123.432,
                          REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_properties_integer) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', yoffset = 4,"
                          " neg = -17 } }");
  ck_assert_int_eq(root_property("yoffset")->type, P_INTEGER);
  ck_assert_int_eq(root_property("neg")->type, P_INTEGER);
  ck_assert_int_eq(sofi_theme_get_integer(&wid, "yoffset", 0), 4);
  ck_assert_int_eq(sofi_theme_get_integer(&wid, "neg", 0), -17);
}
END_TEST

START_TEST(test_properties_inherit) {
  sofi_theme_parse_string("return { { select = '*', test = inherit } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(root_property("test")->type, P_INHERIT);
}
END_TEST
START_TEST(test_properties_name_underscore) {
  /* `text_color` and `["text-color"]` are the same property: the underscore is
   * translated towards the spelling the engine knows. */
  sofi_theme_parse_string("return { { select = '*', text_color = '#f00' },"
                          " { select = '*', ['border-color'] = '#0f0' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(root_property("text_color"));
  ck_assert_int_eq(root_property("text-color")->type, P_COLOR);
  ck_assert_int_eq(root_property("border-color")->type, P_COLOR);
}
END_TEST

START_TEST(test_properties_orientation) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', vert = vertical,"
                          " hori = horizontal } }");
  ck_assert_int_eq(root_property("vert")->type, P_ORIENTATION);
  ck_assert_int_eq(
      sofi_theme_get_orientation(&wid, "vert", SOFI_ORIENTATION_HORIZONTAL),
      SOFI_ORIENTATION_VERTICAL);
  ck_assert_int_eq(
      sofi_theme_get_orientation(&wid, "hori", SOFI_ORIENTATION_VERTICAL),
      SOFI_ORIENTATION_HORIZONTAL);
  // default propagation
  ck_assert_int_eq(
      sofi_theme_get_orientation(&wid, "notfo", SOFI_ORIENTATION_HORIZONTAL),
      SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(
      sofi_theme_get_orientation(&wid, "notfo", SOFI_ORIENTATION_VERTICAL),
      SOFI_ORIENTATION_VERTICAL);
}
END_TEST
START_TEST(test_properties_cursor) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*', def = default,"
                          " ptr = pointer, txt = text } }");
  ck_assert_int_eq(root_property("def")->type, P_CURSOR);
  ck_assert_int_eq(sofi_theme_get_cursor_type(&wid, "def", SOFI_CURSOR_TEXT),
                   SOFI_CURSOR_DEFAULT);
  ck_assert_int_eq(sofi_theme_get_cursor_type(&wid, "ptr", SOFI_CURSOR_DEFAULT),
                   SOFI_CURSOR_POINTER);
  ck_assert_int_eq(sofi_theme_get_cursor_type(&wid, "txt", SOFI_CURSOR_DEFAULT),
                   SOFI_CURSOR_TEXT);
}
END_TEST
START_TEST(test_configuration) {
  sofi_theme_parse_string("return { { configuration = { font = 'blaat€',"
                          " yoffset = 4, notanoption = 1,"
                          " timeout = { delay = 15 } } } }");
  ck_assert_int_eq(g_utf8_collate(config.menu_font, "blaat€"), 0);
  ck_assert_int_eq(config.y_offset, 4);

  /* A plain table with no array part is a nested block, and goes into the
   * separate configuration tree rather than through the setter. */
  ck_assert_ptr_nonnull(sofi_configuration);
  ck_assert_int_eq(sofi_configuration->num_widgets, 1);
  ck_assert_str_eq(sofi_configuration->widgets[0]->name, "timeout");
  Property *p =
      g_hash_table_lookup(sofi_configuration->widgets[0]->properties, "delay");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_INTEGER);
  ck_assert_int_eq(p->value.i, 15);

  /* A configuration block is not a theme widget. */
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(sofi_theme->num_widgets, 0);
  ck_assert_ptr_null(sofi_theme->properties);
}
END_TEST

START_TEST(test_properties_list) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = 'blaat', liste = {},"
                          " list1 = { 'one' }, list2 = { 'one', 'two' } } }");
  GList *list = sofi_theme_get_list_strings(&wid, "liste");
  ck_assert_ptr_null(list);
  list = sofi_theme_get_list_strings(&wid, "list1");
  ck_assert_ptr_nonnull(list);
  ck_assert_str_eq((char *)list->data, "one");
  g_list_free_full(list, (GDestroyNotify)g_free);

  list = sofi_theme_get_list_strings(&wid, "list2");
  ck_assert_ptr_nonnull(list);
  ck_assert_int_eq(g_list_length(list), 2);
  list = g_list_first(list);
  ck_assert_str_eq((char *)list->data, "one");
  ck_assert_str_eq((char *)list->next->data, "two");
  g_list_free_full(list, (GDestroyNotify)g_free);

  /* The engine's list holds Properties, not strings. */
  ThemeWidget *twid = sofi_theme_find_widget("blaat", NULL, TRUE);
  ck_assert_ptr_nonnull(twid);
  Property *p = g_hash_table_lookup(twid->properties, "list2");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LIST);
  ck_assert_int_eq(g_list_length(p->value.list), 2);
  ck_assert_int_eq(((Property *)g_list_nth_data(p->value.list, 0))->type,
                   P_STRING);
  ck_assert_int_eq(((Property *)g_list_nth_data(p->value.list, 1))->type,
                   P_STRING);

  p = g_hash_table_lookup(twid->properties, "liste");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LIST);
  ck_assert_ptr_null(p->value.list);
}
END_TEST

START_TEST(test_parse_file_empty) {
  char *path = theme_lua_fixture("empty.lua", "return {}");
  ck_assert_int_eq(sofi_theme_parse_file(path), FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  g_free(path);
}
END_TEST
START_TEST(test_parse_file_not_existing) {
  ck_assert_int_eq(sofi_theme_parse_file("/not-existing-file.lua"), TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_import_required) {
  char *inc = theme_lua_fixture(
      "palette.lua",
      "return { { select = '*', background_color = '#2b1e3a' } }");
  char *path = theme_lua_fixture("main.lua",
                                 "return { import('palette'),"
                                 " { select = 'window', width = px(560) } }");
  /* An import returns the imported chunk's array, and an element with no
   * `select` is applied in place -- so an import applies exactly where it is
   * written. */
  ck_assert_int_eq(sofi_theme_parse_file(path), FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(root_property("background-color")->type, P_COLOR);
  ThemeWidget *twid = sofi_theme_find_widget("window", NULL, TRUE);
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_nonnull(g_hash_table_lookup(twid->properties, "width"));
  g_free(inc);
  g_free(path);
}
END_TEST
START_TEST(test_import_error) {
  char *path =
      theme_lua_fixture("main.lua", "return { import('nope-not-here') }");
  ck_assert_int_eq(sofi_theme_parse_file(path), TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
  g_free(path);
}
END_TEST
START_TEST(test_import_optional_missing) {
  char *path =
      theme_lua_fixture("main.lua", "return { import('nope-not-here', true),"
                                    " { select = '*', test = 4 } }");
  /* An optional import of a file that is not there is an empty table, and an
   * entirely empty directive is a silent no-op. */
  ck_assert_int_eq(sofi_theme_parse_file(path), FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(logged, 0);
  ck_assert_int_eq(root_property("test")->value.i, 4);
  g_free(path);
}
END_TEST

START_TEST(test_core_properties_types_names) {
  ck_assert_str_eq(PropertyTypeName[P_INTEGER], "Integer");
  ck_assert_str_eq(PropertyTypeName[P_DOUBLE], "Double");
  ck_assert_str_eq(PropertyTypeName[P_STRING], "String");
  ck_assert_str_eq(PropertyTypeName[P_BOOLEAN], "Boolean");
  ck_assert_str_eq(PropertyTypeName[P_COLOR], "Color");
  ck_assert_str_eq(PropertyTypeName[P_PADDING], "Padding");
  ck_assert_str_eq(PropertyTypeName[P_LINK], "Reference");
  ck_assert_str_eq(PropertyTypeName[P_POSITION], "Position");
  ck_assert_str_eq(PropertyTypeName[P_HIGHLIGHT], "Highlight");
  ck_assert_str_eq(PropertyTypeName[P_LIST], "List");
  ck_assert_str_eq(PropertyTypeName[P_ORIENTATION], "Orientation");
}
END_TEST

/** @returns the direct child of @p base called @p name, or NULL. */
static ThemeWidget *child_named(ThemeWidget *base, const char *name) {
  for (unsigned int i = 0; base != NULL && i < base->num_widgets; i++) {
    if (g_strcmp0(base->widgets[i]->name, name) == 0) {
      return base->widgets[i];
    }
  }

  return NULL;
}

START_TEST(test_selector_root) {
  /* `*` at the top level goes on the ROOT's own property table and not into a
   * child widget called `*`. A reference is looked up there and widget
   * defaults are inherited from there, so a `*` filed as a child would leave
   * every reference unresolved while still printing as `* { }` in a dump. */
  sofi_theme_parse_string("return { { select = '*',"
                          " background_color = '#2b1e3a' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_nonnull(sofi_theme->properties);
  ck_assert_int_eq(g_hash_table_size(sofi_theme->properties), 1);
  ck_assert_int_eq(root_property("background-color")->type, P_COLOR);
  ck_assert_int_eq(sofi_theme->num_widgets, 0);
  ck_assert_ptr_null(child_named(sofi_theme, "*"));
}
END_TEST
START_TEST(test_selector_name) {
  sofi_theme_parse_string("return { { select = 'window', width = px(560) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_int_eq(sofi_theme->num_widgets, 1);
  ThemeWidget *twid = child_named(sofi_theme, "window");
  ck_assert_ptr_nonnull(twid);
  ck_assert_int_eq(twid->set, TRUE);
  ck_assert_ptr_nonnull(g_hash_table_lookup(twid->properties, "width"));
}
END_TEST
START_TEST(test_selector_state_path) {
  widget wid;
  wid.name = "element";
  wid.state = "normal.urgent";
  /* `.` and whitespace are both separators, so this is the three-step path
   * element -> normal -> urgent. */
  sofi_theme_parse_string("return { { select = 'element normal.urgent',"
                          " text_color = '#ff0000' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ThemeWidget *twid = child_named(sofi_theme, "element");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_null(twid->properties);
  twid = child_named(twid, "normal");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_null(twid->properties);
  twid = child_named(twid, "urgent");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_nonnull(g_hash_table_lookup(twid->properties, "text-color"));

  ThemeWidget *found = sofi_theme_find_widget(wid.name, wid.state, TRUE);
  ck_assert_ptr_eq(found, twid);
}
END_TEST
START_TEST(test_selector_space_path) {
  sofi_theme_parse_string("return { { select = 'mainbox listview element',"
                          " padding = px(4) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ThemeWidget *twid = child_named(sofi_theme, "mainbox");
  ck_assert_ptr_nonnull(twid);
  twid = child_named(twid, "listview");
  ck_assert_ptr_nonnull(twid);
  twid = child_named(twid, "element");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_nonnull(g_hash_table_lookup(twid->properties, "padding"));
}
END_TEST
START_TEST(test_selector_array) {
  /* An array of selectors applies the same properties to each, which is what
   * the comma-separated selector list was. */
  sofi_theme_parse_string("return { { select = { 'element normal.urgent',"
                          " 'element alternate.urgent' },"
                          " text_color = '#ff0000' } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ThemeWidget *twid = child_named(sofi_theme, "element");
  ck_assert_ptr_nonnull(twid);
  ck_assert_int_eq(twid->num_widgets, 2);
  ThemeWidget *normal = child_named(child_named(twid, "normal"), "urgent");
  ck_assert_ptr_nonnull(normal);
  ck_assert_ptr_nonnull(g_hash_table_lookup(normal->properties, "text-color"));
  ThemeWidget *alt = child_named(child_named(twid, "alternate"), "urgent");
  ck_assert_ptr_nonnull(alt);
  ck_assert_ptr_nonnull(g_hash_table_lookup(alt->properties, "text-color"));
}
END_TEST

START_TEST(test_blocks_media) {
  sofi_theme_parse_string(
      "return { { media = { type = 'min-width', value = 1200 },"
      "   rules = { { select = 'window', width = px(800) },"
      "             { select = '*', test = 1 } } } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_int_eq(sofi_theme->num_widgets, 1);

  /* An integer value names the widget as the previous reader named it, so a
   * dump of the same theme reads the same. */
  ThemeWidget *med = child_named(sofi_theme, "@media ( min-width: 1200 px )");
  ck_assert_ptr_nonnull(med);
  ck_assert_ptr_nonnull(med->media);
  ck_assert_int_eq(med->media->type, THEME_MEDIA_TYPE_MIN_WIDTH);
  ck_assert_double_eq_tol(med->media->value, 1200, REAL_COMPARE_DELTA);
  ck_assert_int_eq(med->set, TRUE);

  /* Inside a media block a `*` is an ordinary child, which is what the old
   * reader did too. */
  ck_assert_int_eq(med->num_widgets, 2);
  ck_assert_ptr_nonnull(child_named(med, "window"));
  ThemeWidget *star = child_named(med, "*");
  ck_assert_ptr_nonnull(star);
  ck_assert_ptr_nonnull(g_hash_table_lookup(star->properties, "test"));
  ck_assert_ptr_null(med->properties);
}
END_TEST
START_TEST(test_blocks_media_float) {
  sofi_theme_parse_string(
      "return { { media = { type = 'max-aspect-ratio', value = 1.5 },"
      "   rules = { { select = 'window', width = px(800) } } } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ThemeWidget *med =
      child_named(sofi_theme, "@media ( max-aspect-ratio: 1.500000 )");
  ck_assert_ptr_nonnull(med);
  ck_assert_int_eq(med->media->type, THEME_MEDIA_TYPE_MAX_ASPECT_RATIO);
  ck_assert_double_eq_tol(med->media->value, 1.5, REAL_COMPARE_DELTA);
}
END_TEST
START_TEST(test_blocks_media_unknown) {
  sofi_theme_parse_string(
      "return { { media = { type = 'min-fontsize', value = 12 },"
      "   rules = { { select = 'window', width = px(800) } } } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_gt(logged, 0);
  ck_assert_int_eq(sofi_theme->num_widgets, 0);
}
END_TEST
START_TEST(test_blocks_reset) {
  /* `reset` throws the tree away and starts again, which is what `@theme` did
   * before importing. */
  sofi_theme_parse_string("return { { select = 'window', width = px(10) },"
                          " { reset = true },"
                          " { select = 'listview', lines = 5 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(sofi_theme->num_widgets, 1);
  ck_assert_ptr_null(child_named(sofi_theme, "window"));
  ck_assert_ptr_nonnull(child_named(sofi_theme, "listview"));
}
END_TEST
START_TEST(test_blocks_nested_array) {
  /* A directive with no `select` and an array part of its own is a nested list
   * of directives, applied in order. */
  sofi_theme_parse_string("return { { { select = 'window', width = px(10) },"
                          "           { select = 'listview', lines = 5 } } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(logged, 0);
  ck_assert_int_eq(sofi_theme->num_widgets, 2);
  ck_assert_ptr_nonnull(child_named(sofi_theme, "window"));
  ck_assert_ptr_nonnull(child_named(sofi_theme, "listview"));
}
END_TEST

START_TEST(test_properties_image_url) {
  sofi_theme_parse_string("return { { select = '*',"
                          " plain = url('/tmp/a.png'),"
                          " n = url('/tmp/a.png', 'none'),"
                          " b = url('/tmp/a.png', 'both'),"
                          " w = url('/tmp/a.png', 'width'),"
                          " h = url('/tmp/a.png', 'height') } }");
  ck_assert_ptr_nonnull(sofi_theme);
  Property *p = root_property("plain");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_IMAGE);
  ck_assert_int_eq(p->value.image.type, SOFI_IMAGE_URL);
  ck_assert_str_eq(p->value.image.url, "/tmp/a.png");
  ck_assert_int_eq(p->value.image.scaling, SOFI_SCALE_NONE);
  ck_assert_int_eq(root_property("n")->value.image.scaling, SOFI_SCALE_NONE);
  ck_assert_int_eq(root_property("b")->value.image.scaling, SOFI_SCALE_BOTH);
  ck_assert_int_eq(root_property("w")->value.image.scaling, SOFI_SCALE_WIDTH);
  ck_assert_int_eq(root_property("h")->value.image.scaling, SOFI_SCALE_HEIGHT);
}
END_TEST
START_TEST(test_properties_image_url_bad_mode) {
  ck_assert_int_eq(
      sofi_theme_parse_string("return { { select = '*',"
                              " i = url('/tmp/a.png', 'sideways') } }"),
      TRUE);
  ck_assert_ptr_null(sofi_theme);
}
END_TEST
START_TEST(test_properties_image_gradient) {
  sofi_theme_parse_string(
      "return { { select = '*',"
      " plain = linear_gradient('#000000', '#ffffff'),"
      " l = linear_gradient('left', '#000', '#fff'),"
      " r = linear_gradient('right', '#000', '#fff'),"
      " t = linear_gradient('top', '#000', '#fff'),"
      " b = linear_gradient('bottom', '#000', '#fff'),"
      " a = linear_gradient(45, '#000', '#fff'),"
      " named = linear_gradient('red', 'blue', 'green') } }");
  ck_assert_ptr_nonnull(sofi_theme);
  Property *p = root_property("plain");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_IMAGE);
  ck_assert_int_eq(p->value.image.type, SOFI_IMAGE_LINEAR_GRADIENT);
  /* Left to right when no direction is given. */
  ck_assert_int_eq(p->value.image.dir, SOFI_DIRECTION_RIGHT);
  ck_assert_int_eq(g_list_length(p->value.image.colors), 2);
  ThemeColor *c = (ThemeColor *)g_list_nth_data(p->value.image.colors, 1);
  ck_assert_double_eq_tol(c->red, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(c->blue, 1.0, REAL_COMPARE_DELTA);

  ck_assert_int_eq(root_property("l")->value.image.dir, SOFI_DIRECTION_LEFT);
  ck_assert_int_eq(root_property("r")->value.image.dir, SOFI_DIRECTION_RIGHT);
  ck_assert_int_eq(root_property("t")->value.image.dir, SOFI_DIRECTION_TOP);
  ck_assert_int_eq(root_property("b")->value.image.dir, SOFI_DIRECTION_BOTTOM);
  ck_assert_int_eq(g_list_length(root_property("l")->value.image.colors), 2);

  /* The angle is degrees; the engine keeps it in turns. */
  p = root_property("a");
  ck_assert_int_eq(p->value.image.dir, SOFI_DIRECTION_ANGLE);
  ck_assert_double_eq_tol(p->value.image.angle, 45.0 / 360.0,
                          REAL_COMPARE_DELTA);
  ck_assert_int_eq(g_list_length(p->value.image.colors), 2);

  /* A leading string that is not an edge is a CSS colour name, so it is a stop
   * and not a direction. */
  p = root_property("named");
  ck_assert_int_eq(p->value.image.dir, SOFI_DIRECTION_RIGHT);
  ck_assert_int_eq(g_list_length(p->value.image.colors), 3);
  c = (ThemeColor *)g_list_nth_data(p->value.image.colors, 0);
  ck_assert_double_eq_tol(c->red, 1.0, REAL_COMPARE_DELTA);
  ck_assert_double_eq_tol(c->green, 0.0, REAL_COMPARE_DELTA);
}
END_TEST

START_TEST(test_prepare_path) {
  char *current_dir = g_get_current_dir();
  ck_assert_ptr_nonnull(current_dir);
  char *f = sofi_theme_parse_prepare_file("../");
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(*f, '/');
  ck_assert_str_ne(f, current_dir);
  ck_assert(g_str_has_prefix(current_dir, f) == TRUE);
  g_free(f);

  g_free(current_dir);
}
END_TEST

START_TEST(test_prepare_array) {
  widget wid;
  wid.name = "element-text";
  wid.state = "normal.normal";
  /* An array of lengths is PADDING, so a numeric list has to say it is one. */
  sofi_theme_parse_string("return { { select = 'element-text',"
                          " tabs = list(10, px(20), px(30), px(40)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  GList *l = sofi_theme_get_list_distance(&wid, "tabs");

  ck_assert_int_eq(g_list_length(l), 4);

  int i = 10;
  for (GList *iter = g_list_first(l); iter != NULL; iter = g_list_next(iter)) {
    SofiDistance *d = (SofiDistance *)iter->data;
    ck_assert_int_eq(d->base.distance, i);
    i += 10;
  }

  g_list_free_full(l, g_free);

  /* Every element went through the same reader the property itself did, so a
   * bare number is an integer and a length is padding. */
  ThemeWidget *twid = child_named(sofi_theme, "element-text");
  ck_assert_ptr_nonnull(twid);
  Property *p = g_hash_table_lookup(twid->properties, "tabs");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(p->type, P_LIST);
  ck_assert_int_eq(g_list_length(p->value.list), 4);
  ck_assert_int_eq(((Property *)g_list_nth_data(p->value.list, 0))->type,
                   P_INTEGER);
  ck_assert_int_eq(((Property *)g_list_nth_data(p->value.list, 1))->type,
                   P_PADDING);
  ck_assert_int_eq(((Property *)g_list_nth_data(p->value.list, 3))->type,
                   P_PADDING);
}
END_TEST
START_TEST(test_prepare_array_list_of_one) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  /* `list()` is always a list, even of one length, where a bare array would be
   * padding. */
  sofi_theme_parse_string("return { { select = 'window',"
                          " tabs = list(px(20)) } }");
  GList *l = sofi_theme_get_list_distance(&wid, "tabs");
  ck_assert_int_eq(g_list_length(l), 1);
  ck_assert_int_eq(((SofiDistance *)g_list_nth_data(l, 0))->base.distance, 20);
  g_list_free_full(l, g_free);
}
END_TEST

START_TEST(test_prepare_math_floor) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = calc.floor(px(1024), px(30)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_FLOOR);
  ck_assert_ptr_nonnull(l.base.left);
  ck_assert_ptr_nonnull(l.base.right);
  ck_assert_int_eq(l.base.left->distance, 1024);
  ck_assert_int_eq(l.base.left->type, SOFI_PU_PX);
  ck_assert_int_eq(l.base.right->distance, 30);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1020);
}
END_TEST
START_TEST(test_prepare_math_ceil) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = calc.ceil(px(1024), px(30)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_CEIL);
  ck_assert_int_eq(l.base.left->distance, 1024);
  ck_assert_int_eq(l.base.right->distance, 30);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1050);
}
END_TEST
START_TEST(test_prepare_math_round) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = calc.round(px(1036), px(30)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_ROUND);
  ck_assert_int_eq(l.base.left->distance, 1036);
  ck_assert_int_eq(l.base.right->distance, 30);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1050);
}
END_TEST
START_TEST(test_prepare_math_add) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(1036) + px(30) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_ADD);
  ck_assert_int_eq(l.base.left->distance, 1036);
  ck_assert_int_eq(l.base.left->modtype, SOFI_DISTANCE_MODIFIER_NONE);
  ck_assert_ptr_null(l.base.left->left);
  ck_assert_int_eq(l.base.right->distance, 30);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1066);
}
END_TEST
START_TEST(test_prepare_math_subtract) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(1036) - px(30) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_SUBTRACT);
  ck_assert_int_eq(l.base.left->distance, 1036);
  ck_assert_int_eq(l.base.right->distance, 30);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1006);
}
END_TEST
START_TEST(test_prepare_math_multiply) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(256) * 4 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_MULTIPLY);
  ck_assert_int_eq(l.base.left->distance, 256);
  /* A bare number used where a length belongs is pixels. */
  ck_assert_int_eq(l.base.right->distance, 4);
  ck_assert_int_eq(l.base.right->type, SOFI_PU_PX);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1024);
}
END_TEST
START_TEST(test_prepare_math_divide) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(500, dash) / 2 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_DIVIDE);
  ck_assert_int_eq(l.base.left->distance, 500);
  ck_assert_int_eq(l.base.right->distance, 2);
  /* The style of the left operand carries. */
  ck_assert_int_eq(l.style, SOFI_HL_DASH);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 250);
}
END_TEST
START_TEST(test_prepare_math_modulo) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(255) % 4 % 5 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_MODULO);
  ck_assert_int_eq(l.base.left->modtype, SOFI_DISTANCE_MODIFIER_MODULO);
  ck_assert_int_eq(l.base.left->left->distance, 255);
  ck_assert_int_eq(l.base.left->right->distance, 4);
  ck_assert_int_eq(l.base.right->distance, 5);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 3);
}
END_TEST

START_TEST(test_prepare_math_min) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = calc.min(px(256), px(4)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_MIN);
  ck_assert_int_eq(l.base.left->distance, 256);
  ck_assert_int_eq(l.base.right->distance, 4);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 4);
}
END_TEST

START_TEST(test_prepare_math_max) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = calc.max(px(256), px(4)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_MAX);
  ck_assert_int_eq(l.base.left->distance, 256);
  ck_assert_int_eq(l.base.right->distance, 4);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 256);
}
END_TEST

START_TEST(test_prepare_math_percent) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  /* A length is a TREE because `%`, `em` and `ch` cannot be resolved until the
   * widget they are measured against exists. */
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = percent(100) - px(10) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_SUBTRACT);
  ck_assert_int_eq(l.base.left->distance, 100);
  ck_assert_int_eq(l.base.left->type, SOFI_PU_PERCENT);
  ck_assert_int_eq(l.base.right->distance, 10);
  ck_assert_int_eq(l.base.right->type, SOFI_PU_PX);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 1910);
  dist = distance_get_pixel(l, SOFI_ORIENTATION_VERTICAL);
  ck_assert_int_eq(dist, 1070);
}
END_TEST

START_TEST(test_prepare_math_failure) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(1) / 2 * 500 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 250);
}
END_TEST

START_TEST(test_prepare_math_failure2) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = -px(16) / 2 * 1.5 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, -12);
}
END_TEST
START_TEST(test_prepare_math_failure3) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(10) + 3 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 13);
}
END_TEST
START_TEST(test_prepare_math_failure4) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(10.0) + 3.2 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 13);
}
END_TEST
START_TEST(test_prepare_math_failure5) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(10) - 3 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 7);
}
END_TEST
START_TEST(test_prepare_math_failure6) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(10.0) - 3.2 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 6);
}
END_TEST
START_TEST(test_prepare_math_failure7) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = -px(10) - (-px(3)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, -7);
}
END_TEST
START_TEST(test_prepare_math_failure8) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = -px(10.0) - (-px(3.2)) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, -6);
}
END_TEST
START_TEST(test_prepare_math_failure9) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window', width = -128 } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, -128);
}
END_TEST
START_TEST(test_prepare_math_negate) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  /* Negation is subtraction from zero rather than a modifier of its own,
   * because a -1 multiplier would change the unit of a percentage. */
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = -px(128) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  ck_assert_int_eq(l.base.modtype, SOFI_DISTANCE_MODIFIER_SUBTRACT);
  ck_assert_int_eq(l.base.left->distance, 0);
  ck_assert_int_eq(l.base.right->distance, 128);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, -128);
}
END_TEST
START_TEST(test_prepare_math_bad_operand) {
  ck_assert_int_eq(sofi_theme_parse_string("return { { select = 'window',"
                                           " width = px(10) + 'x' } }"),
                   TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_prepare_math_cycle) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  /* A theme can build a cycle -- `rawset` reaches past the constructors -- and
   * the walk that reads a length is a single C call, so the instruction budget
   * cannot interrupt it. Stopping at a depth would SILENTLY PRODUCE A WRONG
   * NUMBER, so the length is refused outright. */
  ck_assert_int_eq(
      sofi_theme_parse_string("local v = px(10) + px(5)\n"
                              "rawset(v, 'left', v)\n"
                              "return { { select = 'window', width = v } }"),
      FALSE);
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_gt(logged, 0);
  ThemeWidget *twid = child_named(sofi_theme, "window");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_null(twid->properties);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 42);
  ck_assert_int_eq(distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL), 42);
}
END_TEST

START_TEST(test_prepare_environment_nf) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(tonumber(env.QER_TEST) or 128) } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 128);
}
END_TEST
START_TEST(test_prepare_environment_f) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  g_setenv("QER_TEST", "64", TRUE);
  sofi_theme_parse_string("return { { select = 'window',"
                          " width = px(tonumber(env.QER_TEST) or 128) } }");
  g_unsetenv("QER_TEST");
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 64);
}
END_TEST
START_TEST(test_prepare_environment_view) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  /* `env` is a read-only view of the process environment, which is what
   * `env(NAME)` and `${NAME}` were. */
  g_setenv("QER_TEST", "64", TRUE);
  sofi_theme_parse_string("return { { select = 'window',"
                          " test = env.QER_TEST,"
                          " gone = env.QER_NOT_SET == nil } }");
  g_unsetenv("QER_TEST");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_str_eq(sofi_theme_get_string(&wid, "test", NULL), "64");
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "gone", FALSE), TRUE);
}
END_TEST
START_TEST(test_prepare_environment_readonly) {
  ck_assert_int_eq(sofi_theme_parse_string("env.QER_TEST = 'no'\nreturn {}"),
                   TRUE);
  ck_assert_ptr_null(sofi_theme);
}
END_TEST
START_TEST(test_prepare_environment_media_f) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  g_setenv("QER_TEST", "true", TRUE);
  sofi_theme_parse_string(
      "return { { select = 'window', width = px(32) },"
      " { media = { type = 'enabled', value = env.QER_TEST == 'true' },"
      "   rules = { { select = 'window', width = px(64) } } } }");
  sofi_theme_parse_process_conditionals();
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_ptr_null(sofi_theme->properties);
  ck_assert_ptr_null(sofi_theme->parent);
  ck_assert_str_eq(sofi_theme->name, "Root");
  g_unsetenv("QER_TEST");
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 64);
}
END_TEST

START_TEST(test_prepare_environment_media_nf) {
  widget wid;
  wid.name = "window";
  wid.state = "";
  sofi_theme_parse_string(
      "return { { select = 'window', width = px(32) },"
      " { media = { type = 'enabled', value = env.QER_TEST == 'true' },"
      "   rules = { { select = 'window', width = px(64) } } } }");
  sofi_theme_parse_process_conditionals();
  ck_assert_ptr_nonnull(sofi_theme);
  SofiDistance l = sofi_theme_get_distance(&wid, "width", 0);
  int dist = distance_get_pixel(l, SOFI_ORIENTATION_HORIZONTAL);
  ck_assert_int_eq(dist, 32);
}
END_TEST

START_TEST(test_sandbox_closed) {
  /* A theme computes colours and lengths; nothing it legitimately does needs a
   * file or a process. None of these is merely nil: reading a name the sandbox
   * does not define RAISES, so the chunk that touches one does not run at all.
   * That is a stronger statement than `io == nil` -- it is what makes a
   * misspelled constant an error instead of a silent no-op -- but it does mean
   * absence can only be observed as a failure, never as a value. */
  static const char *const closed[] = {"io",      "os",   "package",
                                       "require", "load", "dofile",
                                       NULL};

  for (int i = 0; closed[i] != NULL; i++) {
    char *chunk =
        g_strdup_printf("return { { select = '*', probe = %s } }", closed[i]);

    sofi_theme_reset();
    ck_assert_int_eq(sofi_theme_parse_string(chunk), TRUE);

    /* The root is not merely without `probe`: the chunk raised before it ever
     * returned a table, so nothing was applied and the root has no property
     * table at all. root_property() is not used here because it asserts one
     * exists, which is the right assertion everywhere except after a failure. */
    ck_assert_ptr_nonnull(sofi_theme);
    ck_assert_ptr_null(sofi_theme->properties);
    g_free(chunk);
  }
}
END_TEST
START_TEST(test_sandbox_given) {
  widget wid;
  wid.name = "blaat";
  wid.state = NULL;
  sofi_theme_parse_string("return { { select = '*',"
                          " a_math = math ~= nil,"
                          " a_string = string ~= nil,"
                          " a_table = table ~= nil,"
                          " a_env = env ~= nil,"
                          " a_rawset = rawset ~= nil,"
                          " a_select = select ~= nil } }");
  ck_assert_ptr_nonnull(sofi_theme);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-math", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-string", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-table", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-env", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-rawset", FALSE), TRUE);
  ck_assert_int_eq(sofi_theme_get_boolean(&wid, "a-select", FALSE), TRUE);
}
END_TEST

START_TEST(test_hardening_string_rep) {
  /* `string.rep("", math.maxinteger)` has a zero-length result, so it passes
   * Lua's own guard and then runs its copy loop nearly maxinteger times,
   * allocating nothing. Refused by the count ceiling. */
  ck_assert_int_eq(sofi_theme_parse_string(
                       "local s = string.rep('', math.maxinteger)\nreturn {}"),
                   TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_hardening_string_rep_method) {
  /* The shared string metatable is repointed as well, so the method-call form
   * cannot walk past the bound. */
  ck_assert_int_eq(
      sofi_theme_parse_string("local s = (''):rep(math.maxinteger)\n"
                              "return {}"),
      TRUE);
  ck_assert_ptr_null(sofi_theme);
}
END_TEST
START_TEST(test_hardening_table_move) {
  /* The same shape of defect: Lua refuses a span whose endpoints overflow and
   * passes one that is merely enormous. */
  ck_assert_int_eq(
      sofi_theme_parse_string("table.move({}, 1, math.maxinteger - 1, 2)\n"
                              "return {}"),
      TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_hardening_infinite_loop) {
  /* A theme that loops is stopped by the instruction budget. */
  ck_assert_int_eq(sofi_theme_parse_string("while true do end\nreturn {}"),
                   TRUE);
  ck_assert_ptr_null(sofi_theme);
  ck_assert_int_gt(logged, 0);
}
END_TEST
START_TEST(test_hardening_previous_theme_stands) {
  /* Nothing is applied until the chunk has run and returned an array, because
   * half a theme is worse than the one it was replacing. */
  ck_assert_int_eq(sofi_theme_parse_string("return { { select = 'window',"
                                           " width = px(560) } }"),
                   FALSE);
  ck_assert_int_eq(sofi_theme_parse_string("while true do end\nreturn {}"),
                   TRUE);
  ck_assert_ptr_nonnull(sofi_theme);
  ThemeWidget *twid = child_named(sofi_theme, "window");
  ck_assert_ptr_nonnull(twid);
  ck_assert_ptr_nonnull(g_hash_table_lookup(twid->properties, "width"));
}
END_TEST

static Suite *theme_lua_suite(void) {
  Suite *s;

  s = suite_create("ThemeLua");

  /* Core test case */
  {
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, theme_lua_setup, theme_lua_teardown);
    tcase_add_test(tc_core, test_core_properties_types_names);
    tcase_add_test(tc_core, test_core_empty_theme);
    tcase_add_test(tc_core, test_core_empty_global_section);
    tcase_add_test(tc_core, test_core_empty_section);
    tcase_add_test(tc_core, test_core_error_root);
    tcase_add_test(tc_core, test_core_error_not_a_table);
    tcase_add_test(tc_core, test_core_error_no_select);
    tcase_add_test(tc_core, test_core_comments);
    tcase_add_test(tc_core, test_core_newline);
    tcase_add_test(tc_core, test_core_value_unknown_type);
    tcase_add_test(tc_core, test_core_properties_error);
    suite_add_tcase(s, tc_core);
  }
  {
    TCase *tc_prop_bool = tcase_create("PropertiesBoolean");
    tcase_add_checked_fixture(tc_prop_bool, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_bool, test_properties_boolean);
    tcase_add_test(tc_prop_bool, test_properties_boolean_reference);
    tcase_add_test(tc_prop_bool, test_properties_reference);
    suite_add_tcase(s, tc_prop_bool);
  }
  {
    TCase *tc_prop_distance = tcase_create("PropertiesDistance");
    tcase_add_checked_fixture(tc_prop_distance, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_distance, test_properties_distance_em);
    tcase_add_test(tc_prop_distance, test_properties_distance_ch);
    tcase_add_test(tc_prop_distance, test_properties_distance_px);
    tcase_add_test(tc_prop_distance, test_properties_distance_percent);
    tcase_add_test(tc_prop_distance, test_properties_distance_em_linestyle);
    tcase_add_test(tc_prop_distance, test_properties_distance_ch_linestyle);
    tcase_add_test(tc_prop_distance, test_properties_distance_px_linestyle);

    tcase_add_test(tc_prop_distance, test_properties_distance_mm);
    tcase_add_test(tc_prop_distance, test_properties_distance_mm_linestyle);
    tcase_add_test(tc_prop_distance,
                   test_properties_distance_percent_linestyle);
    tcase_add_test(tc_prop_distance, test_properties_distance_bare_number);
    suite_add_tcase(s, tc_prop_distance);
  }
  {
    TCase *tc_prop_position = tcase_create("PropertiesPosition");
    tcase_add_checked_fixture(tc_prop_position, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_position, test_properties_position);
    suite_add_tcase(s, tc_prop_position);
  }
  {
    TCase *tc_prop_style = tcase_create("PropertiesStyle");
    tcase_add_checked_fixture(tc_prop_style, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_style, test_properties_style);
    tcase_add_test(tc_prop_style, test_properties_style2);
    tcase_add_test(tc_prop_style, test_properties_style_color);
    suite_add_tcase(s, tc_prop_style);
  }
  {
    TCase *tc_prop_color = tcase_create("PropertiesColor");
    tcase_add_checked_fixture(tc_prop_color, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_color, test_properties_color_h3);
    tcase_add_test(tc_prop_color, test_properties_color_h6);
    tcase_add_test(tc_prop_color, test_properties_color_h8);
    tcase_add_test(tc_prop_color, test_properties_color_hex_digits);
    tcase_add_test(tc_prop_color, test_properties_color_rgb);
    tcase_add_test(tc_prop_color, test_properties_color_rgba);
    tcase_add_test(tc_prop_color, test_properties_color_hsl);
    tcase_add_test(tc_prop_color, test_properties_color_hsla);
    tcase_add_test(tc_prop_color, test_properties_color_hwb);
    tcase_add_test(tc_prop_color, test_properties_color_hwb_angles);
    tcase_add_test(tc_prop_color, test_properties_color_cmyk);
    tcase_add_test(tc_prop_color, test_properties_color_names);
    tcase_add_test(tc_prop_color, test_properties_color_names_alpha);
    suite_add_tcase(s, tc_prop_color);
  }
  {
    TCase *tc_prop_padding = tcase_create("PropertiesPadding");
    tcase_add_checked_fixture(tc_prop_padding, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_padding, test_properties_padding_1);
    tcase_add_test(tc_prop_padding, test_properties_padding_2);
    tcase_add_test(tc_prop_padding, test_properties_padding_3);
    tcase_add_test(tc_prop_padding, test_properties_padding_4);
    suite_add_tcase(s, tc_prop_padding);
  }
  {
    TCase *tc_prop_string = tcase_create("PropertiesString");
    tcase_add_checked_fixture(tc_prop_string, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_string, test_properties_string);
    tcase_add_test(tc_prop_string, test_properties_string_escape);
    tcase_add_test(tc_prop_string, test_properties_string_literal);
    suite_add_tcase(s, tc_prop_string);
  }

  {
    TCase *tc_prop_double = tcase_create("PropertiesDouble");
    tcase_add_checked_fixture(tc_prop_double, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_double, test_properties_double);
    suite_add_tcase(s, tc_prop_double);
  }
  {
    TCase *tc_prop_integer = tcase_create("PropertiesInteger");
    tcase_add_checked_fixture(tc_prop_integer, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_integer, test_properties_integer);
    suite_add_tcase(s, tc_prop_integer);
  }
  {
    TCase *tc_prop_orientation = tcase_create("Propertiesorientation");
    tcase_add_checked_fixture(tc_prop_orientation, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_orientation, test_properties_orientation);
    suite_add_tcase(s, tc_prop_orientation);
  }
  {
    TCase *tc_prop_cursor = tcase_create("Propertiescursor");
    tcase_add_checked_fixture(tc_prop_cursor, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_cursor, test_properties_cursor);
    suite_add_tcase(s, tc_prop_cursor);
  }
  {
    TCase *tc_prop_misc = tcase_create("PropertiesMisc");
    tcase_add_checked_fixture(tc_prop_misc, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_misc, test_properties_inherit);
    tcase_add_test(tc_prop_misc, test_properties_name_underscore);
    suite_add_tcase(s, tc_prop_misc);
  }
  {
    TCase *tc_prop_image = tcase_create("PropertiesImage");
    tcase_add_checked_fixture(tc_prop_image, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_image, test_properties_image_url);
    tcase_add_test(tc_prop_image, test_properties_image_url_bad_mode);
    tcase_add_test(tc_prop_image, test_properties_image_gradient);
    suite_add_tcase(s, tc_prop_image);
  }
  {
    TCase *tc_prop_configuration = tcase_create("Configuration");
    tcase_add_checked_fixture(tc_prop_configuration, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_configuration, test_configuration);
    suite_add_tcase(s, tc_prop_configuration);
  }
  {
    TCase *tc_prop_list = tcase_create("Propertieslist");
    tcase_add_checked_fixture(tc_prop_list, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_list, test_properties_list);
    suite_add_tcase(s, tc_prop_list);
  }
  {
    TCase *tc_selector = tcase_create("Selectors");
    tcase_add_checked_fixture(tc_selector, theme_lua_setup, theme_lua_teardown);
    tcase_add_test(tc_selector, test_selector_root);
    tcase_add_test(tc_selector, test_selector_name);
    tcase_add_test(tc_selector, test_selector_state_path);
    tcase_add_test(tc_selector, test_selector_space_path);
    tcase_add_test(tc_selector, test_selector_array);
    suite_add_tcase(s, tc_selector);
  }
  {
    TCase *tc_blocks = tcase_create("Blocks");
    tcase_add_checked_fixture(tc_blocks, theme_lua_setup, theme_lua_teardown);
    tcase_add_test(tc_blocks, test_blocks_media);
    tcase_add_test(tc_blocks, test_blocks_media_float);
    tcase_add_test(tc_blocks, test_blocks_media_unknown);
    tcase_add_test(tc_blocks, test_blocks_reset);
    tcase_add_test(tc_blocks, test_blocks_nested_array);
    suite_add_tcase(s, tc_blocks);
  }
  {
    TCase *tc_prop_parse_file = tcase_create("ParseFile");
    tcase_add_checked_fixture(tc_prop_parse_file, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_parse_file, test_parse_file_empty);
    tcase_add_test(tc_prop_parse_file, test_parse_file_not_existing);
    suite_add_tcase(s, tc_prop_parse_file);
  }
  {
    TCase *tc_prop_import = tcase_create("Import");
    tcase_add_checked_fixture(tc_prop_import, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prop_import, test_import_required);
    tcase_add_test(tc_prop_import, test_import_error);
    tcase_add_test(tc_prop_import, test_import_optional_missing);
    suite_add_tcase(s, tc_prop_import);
  }
  {
    TCase *tc_prepare_path = tcase_create("prepare_path");
    tcase_add_test(tc_prepare_path, test_prepare_path);
    suite_add_tcase(s, tc_prepare_path);
  }
  {
    TCase *tc_prepare_array = tcase_create("array");
    tcase_add_checked_fixture(tc_prepare_array, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prepare_array, test_prepare_array);
    tcase_add_test(tc_prepare_array, test_prepare_array_list_of_one);
    suite_add_tcase(s, tc_prepare_array);
  }
  {
    TCase *tc_prepare_math = tcase_create("math");
    tcase_add_checked_fixture(tc_prepare_math, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_prepare_math, test_prepare_math_add);
    tcase_add_test(tc_prepare_math, test_prepare_math_subtract);
    tcase_add_test(tc_prepare_math, test_prepare_math_multiply);
    tcase_add_test(tc_prepare_math, test_prepare_math_divide);
    tcase_add_test(tc_prepare_math, test_prepare_math_modulo);
    tcase_add_test(tc_prepare_math, test_prepare_math_floor);
    tcase_add_test(tc_prepare_math, test_prepare_math_ceil);
    tcase_add_test(tc_prepare_math, test_prepare_math_round);
    tcase_add_test(tc_prepare_math, test_prepare_math_min);
    tcase_add_test(tc_prepare_math, test_prepare_math_max);
    tcase_add_test(tc_prepare_math, test_prepare_math_percent);
    tcase_add_test(tc_prepare_math, test_prepare_math_negate);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure2);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure3);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure4);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure5);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure6);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure7);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure8);
    tcase_add_test(tc_prepare_math, test_prepare_math_failure9);
    tcase_add_test(tc_prepare_math, test_prepare_math_bad_operand);
    tcase_add_test(tc_prepare_math, test_prepare_math_cycle);
    suite_add_tcase(s, tc_prepare_math);
  }
  {
    TCase *tc_environment = tcase_create("environment");
    tcase_add_checked_fixture(tc_environment, theme_lua_setup,
                              theme_lua_teardown);
    tcase_add_test(tc_environment, test_prepare_environment_nf);
    tcase_add_test(tc_environment, test_prepare_environment_f);
    tcase_add_test(tc_environment, test_prepare_environment_view);
    tcase_add_test(tc_environment, test_prepare_environment_readonly);
    tcase_add_test(tc_environment, test_prepare_environment_media_f);
    tcase_add_test(tc_environment, test_prepare_environment_media_nf);
    suite_add_tcase(s, tc_environment);
  }
  {
    TCase *tc_sandbox = tcase_create("Sandbox");
    tcase_add_checked_fixture(tc_sandbox, theme_lua_setup, theme_lua_teardown);
    tcase_add_test(tc_sandbox, test_sandbox_closed);
    tcase_add_test(tc_sandbox, test_sandbox_given);
    suite_add_tcase(s, tc_sandbox);
  }
  {
    /* Action purpose: a longer timeout than the default four seconds, because
     * what these assert is that a refusal happens at all -- a case that hangs
     * must be reported as a failure of the bound and not as a test that is
     * merely slow. */
    TCase *tc_hardening = tcase_create("Hardening");
    tcase_add_checked_fixture(tc_hardening, theme_lua_setup,
                              theme_lua_teardown);
    tcase_set_timeout(tc_hardening, 30);
    tcase_add_test(tc_hardening, test_hardening_string_rep);
    tcase_add_test(tc_hardening, test_hardening_string_rep_method);
    tcase_add_test(tc_hardening, test_hardening_table_move);
    tcase_add_test(tc_hardening, test_hardening_infinite_loop);
    tcase_add_test(tc_hardening, test_hardening_previous_theme_stands);
    suite_add_tcase(s, tc_hardening);
  }
  return s;
}

int main(int argc, char **argv) {
  cmd_set_arguments(argc, argv);

  if (setlocale(LC_ALL, "C") == NULL) {
    fprintf(stderr, "Failed to set locale.\n");
    return EXIT_FAILURE;
  }

  g_log_set_default_handler(theme_lua_log, NULL);

  Suite *s;
  SRunner *sr;

  s = theme_lua_suite();
  sr = srunner_create(s);

  srunner_run_all(sr, CK_NORMAL);
  int number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
