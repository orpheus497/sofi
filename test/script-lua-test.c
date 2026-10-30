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

/**
 * Script function and purpose: drives a Lua mode through the public Mode
 * vtable, the same way sofi's view layer drives it, so that every claim the
 * manual makes about the entries() contract is held by a test rather than by a
 * reading of the source. Each case writes a mode to a temporary file, builds
 * the Mode with script_mode_parse_setup(), and inspects what comes back.
 */

#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "display.h"
#include "sofi.h"
#include "theme.h"
#include "widgets/textbox.h"
#include <helper.h>
#include <keyb.h>
#include <mode-private.h>
#include <mode.h>
#include <modes/script.h>

#include "sofi-icon-fetcher.h"
#include <check.h>

/* The host sofi provides and a test does not. Each one is reached by the code
 * under test only through a pointer or a return value it does not inspect. */
uint32_t sofi_icon_fetcher_query(G_GNUC_UNUSED const char *name,
                                 G_GNUC_UNUSED const int size) {
  return 0;
}
uint32_t sofi_icon_fetcher_query_advanced(G_GNUC_UNUSED const char *name,
                                          G_GNUC_UNUSED const int wsize,
                                          G_GNUC_UNUSED const int hsize) {
  return 0;
}
gboolean sofi_icon_fetcher_get_ex(G_GNUC_UNUSED const uint32_t uid,
                                  G_GNUC_UNUSED cairo_surface_t **surface) {
  return FALSE;
}
cairo_surface_t *sofi_icon_fetcher_get(G_GNUC_UNUSED const uint32_t uid) {
  return NULL;
}
guint display_scale(void) { return 1; }
void sofi_clear_error_messages(void) {}
void sofi_clear_warning_messages(void) {}
gboolean sofi_theme_try_string(G_GNUC_UNUSED const char *string) { return 0; }

/** Records the last theme string a mode asked for, so the `theme` header
 * option can be shown to arrive rather than assumed to. */
static char *last_theme = NULL;
gboolean sofi_theme_parse_string(const char *string) {
  g_free(last_theme);
  last_theme = g_strdup(string);
  return FALSE;
}

double textbox_get_estimated_char_height(void) { return 16.0; }
double textbox_get_estimated_ch(void) { return 9.0; }
void sofi_add_error_message(G_GNUC_UNUSED GString *msg) {}
void sofi_add_warning_message(G_GNUC_UNUSED GString *msg) {}
int monitor_active(G_GNUC_UNUSED workarea *d) { return 0; }
int sofi_view_error_dialog(G_GNUC_UNUSED const char *msg,
                           G_GNUC_UNUSED int markup) {
  return TRUE;
}
void sofi_view_get_current_monitor(G_GNUC_UNUSED int *width,
                                   G_GNUC_UNUSED int *height) {}
SofiViewState *sofi_view_get_active(void) { return NULL; }
void sofi_view_set_selected_line(G_GNUC_UNUSED SofiViewState *state,
                                 G_GNUC_UNUSED unsigned int selected_line) {}
gboolean sofi_view_check_action(G_GNUC_UNUSED SofiViewState *state,
                                G_GNUC_UNUSED BindingsScope scope,
                                G_GNUC_UNUSED guint action) {
  return FALSE;
}
void sofi_view_trigger_action(G_GNUC_UNUSED SofiViewState *state,
                              G_GNUC_UNUSED BindingsScope scope,
                              G_GNUC_UNUSED guint action) {}
void display_startup_notification(
    G_GNUC_UNUSED SofiHelperExecuteContext *context,
    G_GNUC_UNUSED GSpawnChildSetupFunc *child_setup,
    G_GNUC_UNUSED gpointer *user_data) {}
int mode_lookup(G_GNUC_UNUSED const char *name) { return -1; }

#ifndef _ck_assert_ptr_null
#define _ck_assert_ptr_null(X, OP)                                             \
  do {                                                                         \
    const void *_ck_x = (X);                                                   \
    ck_assert_msg(_ck_x OP NULL, "Assertion '%s' failed: %s == %#x",           \
                  #X " " #OP " NULL", #X, _ck_x);                              \
  } while (0)
#define ck_assert_ptr_null(X) _ck_assert_ptr_null(X, ==)
#define ck_assert_ptr_nonnull(X) _ck_assert_ptr_null(X, !=)
#endif

/** The directory every mode file in this suite is written into. */
static char *tmpdir = NULL;
/** The mode under test. One per test case, torn down by the fixture. */
static Mode *mode = NULL;

static void script_lua_setup(void) {
  GError *error = NULL;
  tmpdir = g_dir_make_tmp("sofi-lua-test-XXXXXX", &error);
  ck_assert_ptr_null(error);
  ck_assert_ptr_nonnull(tmpdir);
}

static void script_lua_teardown(void) {
  if (mode != NULL) {
    mode_destroy(mode);
    mode_free(&mode);
    mode = NULL;
  }
  g_free(last_theme);
  last_theme = NULL;
  if (tmpdir != NULL) {
    /* Action purpose: g_rmdir refuses a non-empty directory, so the files this
     * case wrote go first. Named explicitly rather than globbed, because a
     * sweep that silently removed the wrong tree would be worse than a leak. */
    char *p = g_build_filename(tmpdir, "m.lua", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "lib.lua", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "gen.lua", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "notes.lua", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "bookmarks.lua", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "sofi", "bookmarks", NULL);
    g_unlink(p);
    g_free(p);
    p = g_build_filename(tmpdir, "sofi", NULL);
    g_rmdir(p);
    g_free(p);
    g_rmdir(tmpdir);
    g_free(tmpdir);
    tmpdir = NULL;
  }
}

/** Function purpose: write `body` as the mode file and build the Mode over it,
 * through the same `name:path` entry point `-modes` uses. `exec` asks for the
 * execute bit, which is what tells the two producers apart. */
static Mode *build_mode(const char *body, gboolean exec) {
  char *path = g_build_filename(tmpdir, "m.lua", NULL);
  GError *error = NULL;
  g_file_set_contents(path, body, -1, &error);
  ck_assert_ptr_null(error);
  if (exec) {
    ck_assert_int_eq(g_chmod(path, 0755), 0);
  }
  char *spec = g_strdup_printf("m:%s", path);
  Mode *sw = script_mode_parse_setup(spec);
  g_free(spec);
  g_free(path);
  ck_assert_ptr_nonnull(sw);
  ck_assert_int_eq(mode_init(sw), TRUE);
  return sw;
}

/** Function purpose: build the Mode from an arbitrary `-modes` spec, for the
 * cases whose whole point is that `ed` is a command line rather than a path. */
static Mode *build_mode_spec(const char *spec) {
  Mode *sw = script_mode_parse_setup(spec);
  ck_assert_ptr_nonnull(sw);
  ck_assert_int_eq(mode_init(sw), TRUE);
  return sw;
}

/** Function purpose: write `body` at `path` and make it executable. */
static void write_exec(const char *path, const char *body) {
  GError *error = NULL;
  g_file_set_contents(path, body, -1, &error);
  ck_assert_ptr_null(error);
  ck_assert_int_eq(g_chmod(path, 0755), 0);
}

static char *display_of(Mode *sw, unsigned int row) {
  int state = 0;
  GList *list = NULL;
  return mode_get_display_value(sw, row, &state, &list, TRUE);
}

static int state_of(Mode *sw, unsigned int row) {
  int state = 0;
  GList *list = NULL;
  char *v = mode_get_display_value(sw, row, &state, &list, TRUE);
  g_free(v);
  return state;
}

START_TEST(test_lua_rows_in_order) {
  mode = build_mode("return { entries = function(s)\n"
                    "  return { 'alpha', 'beta', 'gamma' }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 3);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "alpha");
  g_free(v);
  v = display_of(mode, 2);
  ck_assert_str_eq(v, "gamma");
  g_free(v);
}
END_TEST

START_TEST(test_lua_row_fields) {
  mode = build_mode(
      "return { entries = function(s)\n"
      "  return { { entry = 'e', display = 'D', meta = 'M', info = 'I',\n"
      "             icon = { 'one', 'two' }, urgent = true, active = true } }\n"
      "end }\n",
      FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  /* `display` is what is shown; `entry` is what is matched. */
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "D");
  g_free(v);
  ck_assert_int_eq(state_of(mode, 0) & URGENT, URGENT);
  ck_assert_int_eq(state_of(mode, 0) & ACTIVE, ACTIVE);
  /* `meta` is searched and `entry` is searched; `display` is not. */
  sofi_int_matcher **t = helper_tokenize("M", FALSE);
  ck_assert_int_eq(mode_token_match(mode, t, 0), TRUE);
  helper_tokenize_free(t);
  t = helper_tokenize("e", FALSE);
  ck_assert_int_eq(mode_token_match(mode, t, 0), TRUE);
  helper_tokenize_free(t);
  t = helper_tokenize("zzz", FALSE);
  ck_assert_int_eq(mode_token_match(mode, t, 0), FALSE);
  helper_tokenize_free(t);
}
END_TEST

START_TEST(test_lua_icon_comma_string) {
  mode = build_mode("return { entries = function(s)\n"
                    "  return { { entry = 'e', icon = 'a,b,c' } }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  /* A comma string and an array are the same strv, so the only observable is
   * that the icon query is driven at all; _get_icon returns NULL because the
   * fetcher is stubbed. */
  ck_assert_ptr_null(mode_get_icon(mode, 0, 16));
}
END_TEST

START_TEST(test_lua_header_options) {
  mode =
      build_mode("return { entries = function(s)\n"
                 "  return { 'x', prompt = 'P', message = 'hello',\n"
                 "           ['markup-rows'] = true, theme = '* { a: 1; }',\n"
                 "           data = 'carried' }\n"
                 "end }\n",
                 FALSE);
  char *m = mode_get_message(mode);
  ck_assert_str_eq(m, "hello");
  g_free(m);
  ck_assert_str_eq(mode->display_name, "P");
  ck_assert_int_eq(state_of(mode, 0) & MARKUP, MARKUP);
  ck_assert_str_eq(last_theme, "* { a: 1; }");
}
END_TEST

START_TEST(test_lua_empty_message_clears) {
  /* The guard inside parse_header_entry is `(length_key + 1) < length`, so a
   * length that omits the terminating NUL silently drops an empty value -- and
   * an empty `message` is how the protocol CLEARS a message. */
  mode = build_mode("local first = true\n"
                    "return { entries = function(s)\n"
                    "  if first then first = false\n"
                    "    return { 'x', message = 'shown' } end\n"
                    "  return { 'x', message = '' }\n"
                    "end }\n",
                    FALSE);
  char *m = mode_get_message(mode);
  ck_assert_str_eq(m, "shown");
  g_free(m);

  char *input = g_strdup("");
  mode_result(mode, MENU_OK, &input, 0);
  g_free(input);

  m = mode_get_message(mode);
  ck_assert_ptr_null(m);
}
END_TEST

START_TEST(test_lua_state_round_trip) {
  /* retv, entry, info, input and data are the five pieces of state the line
   * protocol hands a script. A mode echoes them back as its single row. */
  mode = build_mode(
      "return { entries = function(s)\n"
      "  if s.retv == 0 then\n"
      "    return { { entry = 'first', info = 'INFO' }, data = 'D1' }\n"
      "  end\n"
      "  return { string.format('%d|%s|%s|%s|%s', s.retv,\n"
      "    s.entry or '-', s.info or '-', s.input or '-', s.data or '-') }\n"
      "end }\n",
      FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);

  char *input = g_strdup("typed");
  mode_result(mode, MENU_OK, &input, 0);
  g_free(input);

  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "1|first|INFO|typed|D1");
  g_free(v);
}
END_TEST

START_TEST(test_lua_nil_closes) {
  mode = build_mode("local first = true\n"
                    "return { entries = function(s)\n"
                    "  if first then first = false; return { 'x' } end\n"
                    "  return nil\n"
                    "end }\n",
                    FALSE);
  char *input = g_strdup("");
  /* MODE_EXIT is what sofi acts on to close, and it is what a script that
   * prints nothing produces. */
  ck_assert_int_eq(mode_result(mode, MENU_OK, &input, 0), MODE_EXIT);
  g_free(input);
}
END_TEST

START_TEST(test_lua_empty_table_closes) {
  /* An empty list must mean what nil means, or an empty table would reload a
   * menu with no rows in it. */
  mode = build_mode("local first = true\n"
                    "return { entries = function(s)\n"
                    "  if first then first = false; return { 'x' } end\n"
                    "  return {}\n"
                    "end }\n",
                    FALSE);
  char *input = g_strdup("");
  ck_assert_int_eq(mode_result(mode, MENU_OK, &input, 0), MODE_EXIT);
  g_free(input);
}
END_TEST

START_TEST(test_lua_bad_row_skipped) {
  /* A typo in one row must not blank the menu. */
  mode = build_mode("return { entries = function(s)\n"
                    "  return { 'good', { display = 'no entry here' },\n"
                    "           42, { entry = 'also good' } }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 2);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "good");
  g_free(v);
  v = display_of(mode, 1);
  ck_assert_str_eq(v, "also good");
  g_free(v);
}
END_TEST

START_TEST(test_lua_syntax_error_quarantines) {
  mode = build_mode("this is not lua\n", FALSE);
  /* One row, naming the file and the line, rather than an empty menu or a
   * dialog stacked under the generic initialisation failure. */
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(v);
  ck_assert_ptr_nonnull(strstr(v, "would not load"));
  g_free(v);
}
END_TEST

START_TEST(test_lua_wrong_shape_quarantines) {
  mode = build_mode("return 42\n", FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "must return a table"));
  g_free(v);
}
END_TEST

START_TEST(test_lua_missing_entries_quarantines) {
  mode = build_mode("return { entries = 'not a function' }\n", FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "not a"));
  g_free(v);
}
END_TEST

START_TEST(test_lua_quarantine_survives_delete) {
  /* The delete and hot-key branches of script_mode_result do NOT test
   * nonselectable, so they reach the producer. A quarantine that answered with
   * NULL there would close the menu on Shift+Delete instead of redisplaying
   * the error. */
  mode = build_mode("return 42\n", FALSE);
  char *input = g_strdup("");
  ck_assert_int_eq(mode_result(mode, MENU_ENTRY_DELETE, &input, 0),
                   RELOAD_DIALOG);
  g_free(input);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
}
END_TEST

START_TEST(test_lua_raising_metatable_does_not_abort) {
  /* Every read of the mode's table happens AFTER lua_pcall has returned, where
   * no protected frame exists: an __index that raises would reach Lua's panic
   * function and abort the process. Raw reads are what make this survivable,
   * and this test is the only thing that holds them in place. */
  mode = build_mode(
      "local mt = { __index = function(t, k) error('boom ' .. k) end,\n"
      "             __len = function(t) error('boom len') end }\n"
      "return { entries = function(s)\n"
      "  return setmetatable({ 'row' }, mt)\n"
      "end }\n",
      FALSE);
  /* The row is read; the metamethods are ignored, which is the documented
   * behaviour -- the table sofi reads is the table the mode built. */
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "row");
  g_free(v);
}
END_TEST

START_TEST(test_lua_raising_mode_table_does_not_abort) {
  /* The same hazard on the table the CHUNK returns, which is read once in
   * _init and once per interaction. */
  mode = build_mode(
      "return setmetatable({}, { __index = function(t, k) error('no') end })\n",
      FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
}
END_TEST

START_TEST(test_lua_error_is_not_permanent) {
  /* A branch that raises is reported and answered with the error row, but the
   * mode is not quarantined: the next interaction calls Lua again. */
  mode = build_mode("return { entries = function(s)\n"
                    "  if s.retv == 3 then error('deliberate') end\n"
                    "  return { 'alpha', 'beta' }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 2);

  char *input = g_strdup("");
  mode_result(mode, MENU_ENTRY_DELETE, &input, 0);
  g_free(input);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deliberate"));
  g_free(v);

  input = g_strdup("");
  mode_result(mode, MENU_CUSTOM_INPUT, &input, 0);
  g_free(input);
  ck_assert_int_eq(mode_get_num_entries(mode), 2);
}
END_TEST

START_TEST(test_lua_keep_selection_is_not_sticky) {
  /* execute_executor resets new_selection, keep_selection and keep_filter
   * before anything else, and script_mode_result snapshots two of them BEFORE
   * calling the producer. A producer that skipped the resets would make a mode
   * that once set keep-selection keep it for the rest of the run. */
  mode = build_mode("return { entries = function(s)\n"
                    "  if s.retv == 0 then\n"
                    "    return { 'a', 'b', ['keep-filter'] = true }\n"
                    "  end\n"
                    "  return { 'a', 'b' }\n"
                    "end }\n",
                    FALSE);
  char *input = g_strdup("kept");
  mode_result(mode, MENU_OK, &input, 0);
  /* The first list asked to keep the filter, so this interaction keeps it. */
  ck_assert_ptr_nonnull(input);
  ck_assert_str_eq(input, "kept");

  mode_result(mode, MENU_OK, &input, 0);
  /* The second list did not ask, so the filter is cleared -- which only
   * happens if the producer reset the flag. */
  ck_assert_ptr_null(input);
}
END_TEST

START_TEST(test_lua_sandbox_subtractions) {
  mode = build_mode(
      "local missing = {}\n"
      "if os.exit ~= nil then missing[#missing+1] = 'os.exit' end\n"
      "if os.setlocale ~= nil then missing[#missing+1] = 'os.setlocale' end\n"
      "if load ~= nil then missing[#missing+1] = 'load' end\n"
      "if loadfile ~= nil then missing[#missing+1] = 'loadfile' end\n"
      "if dofile ~= nil then missing[#missing+1] = 'dofile' end\n"
      "if debug.sethook ~= nil then missing[#missing+1] = 'debug.sethook' end\n"
      "if debug.setupvalue ~= nil then\n"
      "  missing[#missing+1] = 'debug.setupvalue' end\n"
      "if package.loadlib ~= nil then\n"
      "  missing[#missing+1] = 'package.loadlib' end\n"
      "if #package.searchers ~= 2 then\n"
      "  missing[#missing+1] = 'searchers=' .. #package.searchers end\n"
      "-- kept, deliberately: a script mode can do all of these to itself\n"
      "if type(debug.traceback) ~= 'function' then\n"
      "  missing[#missing+1] = 'no traceback' end\n"
      "if io == nil then missing[#missing+1] = 'no io' end\n"
      "if os.execute == nil then missing[#missing+1] = 'no os.execute' end\n"
      "if io.popen == nil then missing[#missing+1] = 'no io.popen' end\n"
      "if setmetatable == nil then missing[#missing+1] = 'no setmetatable' "
      "end\n"
      "if pcall == nil then missing[#missing+1] = 'no pcall' end\n"
      "if coroutine == nil then missing[#missing+1] = 'no coroutine' end\n"
      "if utf8 == nil then missing[#missing+1] = 'no utf8' end\n"
      "if require == nil then missing[#missing+1] = 'no require' end\n"
      "return { entries = function(s)\n"
      "  if #missing == 0 then return { 'clean' } end\n"
      "  return { table.concat(missing, ',') }\n"
      "end }\n",
      FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "clean");
  g_free(v);
}
END_TEST

START_TEST(test_lua_sofi_table) {
  mode = build_mode(
      "local bad = {}\n"
      "if type(sofi.exec) ~= 'function' then bad[#bad+1] = 'exec' end\n"
      "if type(sofi.log) ~= 'function' then bad[#bad+1] = 'log' end\n"
      "if sofi.markup_escape('a<b&c') ~= 'a&lt;b&amp;c' then\n"
      "  bad[#bad+1] = 'escape:' .. sofi.markup_escape('a<b&c') end\n"
      "if sofi.shell_quote(\"a b\") ~= \"'a b'\" then\n"
      "  bad[#bad+1] = 'quote:' .. sofi.shell_quote('a b') end\n"
      "if sofi.name ~= 'm' then bad[#bad+1] = 'name:' .. sofi.name end\n"
      "if type(sofi.config_dir) ~= 'string' then bad[#bad+1] = 'dir' end\n"
      "return { entries = function(s)\n"
      "  if #bad == 0 then return { 'clean' } end\n"
      "  return { table.concat(bad, ',') }\n"
      "end }\n",
      FALSE);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "clean");
  g_free(v);
}
END_TEST

START_TEST(test_lua_require_finds_sibling) {
  /* A mode that outgrows one file can require its parts, because its own
   * directory is prepended to package.path. */
  char *lib = g_build_filename(tmpdir, "lib.lua", NULL);
  GError *error = NULL;
  g_file_set_contents(lib, "return { word = 'from-lib' }\n", -1, &error);
  ck_assert_ptr_null(error);
  g_free(lib);

  mode = build_mode("local l = require('lib')\n"
                    "return { entries = function(s) return { l.word } end }\n",
                    FALSE);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "from-lib");
  g_free(v);
}
END_TEST

START_TEST(test_lua_watchdog_stops_a_hang) {
  /* The deadline is the one thing a Lua mode has that a script mode does not:
   * sofi reads a script's stdout with getdelim in the calling thread and will
   * wait for a wedged child forever. */
  gint64 start = g_get_monotonic_time();
  mode = build_mode("return { entries = function(s)\n"
                    "  local n = 0\n"
                    "  while true do n = n + 1 end\n"
                    "end }\n",
                    FALSE);
  gint64 spent = g_get_monotonic_time() - start;
  /* Stopped, with the error reported as a row rather than as a hang. */
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  g_free(v);
  /* Bounded well inside twice the deadline, which is what distinguishes "the
   * watchdog fired" from "the loop happened to end". */
  ck_assert_int_lt(spent, 2 * 2000000 + 1000000);
}
END_TEST

START_TEST(test_lua_deadline_is_per_call) {
  /* A mode used many times must not die on the last one: lua_sethook resets
   * the instruction count and the deadline is recomputed per interaction. */
  mode = build_mode("return { entries = function(s)\n"
                    "  local t = {}\n"
                    "  for i = 1, 2000 do t[i] = 'row ' .. i end\n"
                    "  return t\n"
                    "end }\n",
                    FALSE);
  for (int i = 0; i < 20; i++) {
    ck_assert_int_eq(mode_get_num_entries(mode), 2000);
    char *input = g_strdup("");
    ModeMode r = mode_result(mode, MENU_OK, &input, 0);
    g_free(input);
    ck_assert_int_eq(r, RELOAD_DIALOG);
  }
}
END_TEST

START_TEST(test_executable_lua_still_spawns) {
  /* The retention proof. execute_executor reaches a script only through
   * g_spawn with G_SPAWN_SEARCH_PATH, which requires an executable file, so
   * every script mode that works today is executable. A .lua file WITH the
   * execute bit must therefore still be spawned -- this one is a shell script
   * that happens to be named .lua, and it works exactly as it does today. */
  mode = build_mode("#!/bin/sh\nprintf 'spawned\\n'\n", TRUE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "spawned");
  g_free(v);
}
END_TEST

START_TEST(test_shipped_example_runs) {
  /* The example in Examples/ is documentation that can rot, so it is driven
   * here rather than only read. Exercised through retv 0 and retv 3 only: its
   * retv 1 and retv 2 branches call sofi.exec, and a test must not launch a
   * browser. */
  char *cfg = g_build_filename(tmpdir, "sofi", NULL);
  ck_assert_int_eq(g_mkdir_with_parents(cfg, 0700), 0);
  char *marks = g_build_filename(cfg, "bookmarks", NULL);
  GError *error = NULL;
  g_file_set_contents(marks,
                      "# a comment line is ignored\n"
                      "news\thttps://example.org/news\n"
                      "docs\thttps://example.org/docs\n"
                      "https://example.org/bare\n",
                      -1, &error);
  ck_assert_ptr_null(error);
  g_free(marks);
  g_free(cfg);
  /* The example reads XDG_CONFIG_HOME, so it is pointed at this case's own
   * directory rather than the machine's real configuration. */
  g_setenv("XDG_CONFIG_HOME", tmpdir, TRUE);

  char *src =
      g_build_filename(SOFI_SOURCE_ROOT, "Examples", "bookmarks.lua", NULL);
  char *body = NULL;
  gsize len = 0;
  ck_assert_int_eq(g_file_get_contents(src, &body, &len, &error), TRUE);
  ck_assert_ptr_null(error);
  g_free(src);

  char *path = g_build_filename(tmpdir, "bookmarks.lua", NULL);
  g_file_set_contents(path, body, (gssize)len, &error);
  ck_assert_ptr_null(error);
  g_free(body);
  char *spec = g_strdup_printf("bookmarks:%s", path);
  mode = script_mode_parse_setup(spec);
  g_free(spec);
  g_free(path);
  ck_assert_ptr_nonnull(mode);
  ck_assert_int_eq(mode_init(mode), TRUE);

  /* Three bookmarks, in the file's own order, with the header options the
   * example sets. */
  ck_assert_int_eq(mode_get_num_entries(mode), 3);
  ck_assert_str_eq(mode->display_name, "bookmarks");
  ck_assert_int_eq(state_of(mode, 0) & MARKUP, MARKUP);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "news");
  g_free(v);
  v = display_of(mode, 2);
  ck_assert_str_eq(v, "https://example.org/bare");
  g_free(v);
  char *m = mode_get_message(mode);
  ck_assert_ptr_nonnull(strstr(m, "Shift+Delete"));
  g_free(m);

  /* The delete key hides the selected row and the list comes back one
   * shorter, which is the closure state surviving between interactions. */
  char *input = g_strdup("");
  ck_assert_int_eq(mode_result(mode, MENU_ENTRY_DELETE, &input, 0),
                   RELOAD_DIALOG);
  g_free(input);
  ck_assert_int_eq(mode_get_num_entries(mode), 2);
  v = display_of(mode, 0);
  ck_assert_str_eq(v, "docs");
  g_free(v);
  m = mode_get_message(mode);
  ck_assert_str_eq(m, "hidden for this session");
  g_free(m);

  g_unsetenv("XDG_CONFIG_HOME");
}
END_TEST

START_TEST(test_interpreter_command_line_still_spawns) {
  /* A command line with ARGUMENTS is a spawn whatever it ends in. This spec --
   * an interpreter followed by a .lua file -- is how someone already running a
   * Lua mode would have written it before Lua modes existed, and testing the
   * raw `ed` string as a path turned it into a permanent error row. */
  char *path = g_build_filename(tmpdir, "gen.lua", NULL);
  write_exec(path, "#!/bin/sh\nprintf 'via-interpreter\\n'\n");
  char *spec = g_strdup_printf("m:/bin/sh %s", path);
  mode = build_mode_spec(spec);
  g_free(spec);
  g_free(path);

  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "via-interpreter");
  g_free(v);
}
END_TEST

START_TEST(test_bare_name_on_path_still_spawns) {
  /* A bare name is resolved on $PATH and nowhere else -- it is the form the
   * manual documents first. Resolving it against the working directory would
   * also have made the answer depend on where sofi was launched from. */
  char *path = g_build_filename(tmpdir, "notes.lua", NULL);
  write_exec(path, "#!/bin/sh\nprintf 'found-on-path\\n'\n");
  g_free(path);

  char *oldpath = g_strdup(g_getenv("PATH"));
  char *newpath =
      g_strdup_printf("%s:%s", tmpdir, oldpath != NULL ? oldpath : "");
  g_setenv("PATH", newpath, TRUE);
  g_free(newpath);

  mode = build_mode_spec("m:notes.lua");
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "found-on-path");
  g_free(v);

  if (oldpath != NULL) {
    g_setenv("PATH", oldpath, TRUE);
  }
  g_free(oldpath);
}
END_TEST

START_TEST(test_shell_escaped_path_still_spawns) {
  /* g_shell_parse_argv unescapes before the spawn ever sees the path, so the
   * classification has to unescape too. */
  char *dir = g_build_filename(tmpdir, "my notes", NULL);
  ck_assert_int_eq(g_mkdir_with_parents(dir, 0700), 0);
  char *path = g_build_filename(dir, "x.lua", NULL);
  write_exec(path, "#!/bin/sh\nprintf 'escaped-path\\n'\n");

  char *spec = g_strdup_printf("m:%s/my\\ notes/x.lua", tmpdir);
  mode = build_mode_spec(spec);
  g_free(spec);

  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "escaped-path");
  g_free(v);

  g_unlink(path);
  g_free(path);
  g_rmdir(dir);
  g_free(dir);
}
END_TEST

START_TEST(test_lua_debug_not_recoverable_from_package_loaded) {
  /* The debug table is emptied IN PLACE. Replacing the global with a smaller
   * table left package.loaded.debug pointing at the original, so
   * require("debug") handed back sethook -- which clears the one hook slot the
   * deadline uses -- and setupvalue, which reaches into sofi's own closures. */
  mode = build_mode(
      "local bad = {}\n"
      "if package.loaded.debug.sethook ~= nil then\n"
      "  bad[#bad+1] = 'loaded.sethook' end\n"
      "if package.loaded.debug.setupvalue ~= nil then\n"
      "  bad[#bad+1] = 'loaded.setupvalue' end\n"
      "if package.loaded.debug.getinfo ~= nil then\n"
      "  bad[#bad+1] = 'loaded.getinfo' end\n"
      "if require('debug').sethook ~= nil then\n"
      "  bad[#bad+1] = 'require.sethook' end\n"
      "if debug.sethook ~= nil then bad[#bad+1] = 'global.sethook' end\n"
      "if type(debug.traceback) ~= 'function' then\n"
      "  bad[#bad+1] = 'lost traceback' end\n"
      "return { entries = function(s)\n"
      "  if #bad == 0 then return { 'clean' } end\n"
      "  return { table.concat(bad, ',') }\n"
      "end }\n",
      FALSE);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "clean");
  g_free(v);
}
END_TEST

START_TEST(test_lua_pcall_cannot_swallow_the_deadline) {
  /* THE ONE THAT MAKES THE DEADLINE MEAN ANYTHING. A watchdog that raises an
   * error is caught by any pcall the mode wrapped its work in, and raises
   * again at the next sample, and is caught again -- measured at 5.8 seconds
   * against a 2 second deadline, 73054 errors swallowed. The watchdog yields
   * instead, and a yield propagates through pcall rather than being trapped. */
  gint64 start = g_get_monotonic_time();
  mode = build_mode("return { entries = function(s)\n"
                    "  local swallowed = 0\n"
                    "  while true do\n"
                    "    local ok = pcall(function()\n"
                    "      local n = 0\n"
                    "      while true do n = n + 1 end\n"
                    "    end)\n"
                    "    if not ok then swallowed = swallowed + 1 end\n"
                    "  end\n"
                    "end }\n",
                    FALSE);
  gint64 spent = g_get_monotonic_time() - start;
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  g_free(v);
  ck_assert_int_lt(spent, 2 * 2000000 + 1000000);
}
END_TEST

START_TEST(test_lua_require_refuses_bytecode) {
  /* The stock searcher for a .lua file loads it in "text or binary" mode, so
   * require() accepted a precompiled chunk even though load, loadfile, dofile
   * and the mode's own file were all restricted to text. string.dump and io
   * are both deliberately kept, so a mode could produce the chunk itself with
   * nothing external -- and Lua does not verify bytecode, so a malformed one
   * crashed the process. The replacement searcher loads text only. */
  mode =
      build_mode("local dir = package.path:match('^(.-)/%?%.lua;')\n"
                 "local bc = string.dump(function() return { word = "
                 "'bytecode-ran' } end)\n"
                 "local out = assert(io.open(dir .. '/lib.lua', 'wb'))\n"
                 "out:write(bc)\n"
                 "out:close()\n"
                 "local ok, m = pcall(require, 'lib')\n"
                 "return { entries = function(s)\n"
                 "  if ok then return { 'LOADED:' .. tostring(m.word) } end\n"
                 "  return { 'refused' }\n"
                 "end }\n",
                 FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "refused");
  g_free(v);
}
END_TEST

START_TEST(test_lua_require_refuses_malformed_bytecode) {
  /* The same route fed a corrupted chunk, which is the case that segfaulted:
   * a crash skips cleanup, the surface teardown and the pid file exactly as
   * the removed os.exit would have. */
  mode = build_mode(
      "local dir = package.path:match('^(.-)/%?%.lua;')\n"
      "local bc = string.dump(function() return { word = 'x' } end)\n"
      "local body = bc:sub(1, 20) .. '\\255\\255\\255\\255' .. bc:sub(25)\n"
      "local out = assert(io.open(dir .. '/lib.lua', 'wb'))\n"
      "out:write(body)\n"
      "out:close()\n"
      "local ok = pcall(require, 'lib')\n"
      "return { entries = function(s)\n"
      "  if ok then return { 'LOADED' } end\n"
      "  return { 'refused' }\n"
      "end }\n",
      FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "refused");
  g_free(v);
}
END_TEST

START_TEST(test_lua_deadline_survives_a_c_boundary) {
  /* lua_yield refuses inside a C function that cannot be resumed -- a
   * table.sort comparator is one -- and the refusal is itself an ordinary
   * catchable error. Measured at six seconds of a two second deadline before
   * the watchdog was made sticky. */
  gint64 start = g_get_monotonic_time();
  mode = build_mode("local t = {}\n"
                    "for i = 1, 4096 do t[i] = (i * 7919) % 4096 end\n"
                    "return { entries = function(s)\n"
                    "  while true do\n"
                    "    pcall(table.sort, t, function(a, b)\n"
                    "      local x = 0\n"
                    "      for i = 1, 50 do x = x + i end\n"
                    "      return a < b\n"
                    "    end)\n"
                    "  end\n"
                    "end }\n",
                    FALSE);
  gint64 spent = g_get_monotonic_time() - start;
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  g_free(v);
  ck_assert_int_lt(spent, 2 * 2000000 + 1000000);
}
END_TEST

START_TEST(test_lua_deadline_survives_a_fresh_coroutine_each_cycle) {
  /* lua_newthread copies the hook AND its instruction budget from its creator,
   * so a loop that creates one coroutine per iteration used to get a whole
   * fresh sampling interval every cycle and ran forever. Listening for call
   * events as well as instruction counts is what reaches a brand new thread. */
  gint64 start = g_get_monotonic_time();
  mode = build_mode(
      "return { entries = function(s)\n"
      "  local function spin() local n = 0; while true do n = n + 1 end end\n"
      "  local l2 = function() while true do pcall(spin) end end\n"
      "  local l1 = coroutine.wrap(function()\n"
      "    while true do pcall(coroutine.wrap(l2)) end\n"
      "  end)\n"
      "  l1()\n"
      "  return { 'never' }\n"
      "end }\n",
      FALSE);
  gint64 spent = g_get_monotonic_time() - start;
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  g_free(v);
  ck_assert_int_lt(spent, 2 * 2000000 + 1000000);
}
END_TEST

START_TEST(test_lua_generator_is_not_truncated) {
  /* The other side of the watchdog reaching every thread: a mode's own
   * coroutine, used legitimately, must be left alone. A generator whose total
   * work fits inside the deadline has to deliver every row, with no warning --
   * an earlier watchdog suspended the mode's own generator, which read to the
   * mode as the generator finishing, and silently returned 11 rows of 40. */
  mode = build_mode("return { entries = function(s)\n"
                    "  local gen = coroutine.wrap(function()\n"
                    "    for i = 1, 40 do\n"
                    "      local x = 0\n"
                    "      for j = 1, 20000 do x = x + j end\n"
                    "      coroutine.yield('row ' .. i)\n"
                    "    end\n"
                    "  end)\n"
                    "  local out, i = {}, 1\n"
                    "  while true do\n"
                    "    local v = gen()\n"
                    "    if v == nil then break end\n"
                    "    out[i] = v; i = i + 1\n"
                    "  end\n"
                    "  return out\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 40);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "row 1");
  g_free(v);
  v = display_of(mode, 39);
  ck_assert_str_eq(v, "row 40");
  g_free(v);
}
END_TEST

START_TEST(test_lua_self_yield_is_its_own_error) {
  /* A mode runs as a coroutine body, which it has no way to know, so a
   * top-level coroutine.yield suspends it exactly as the deadline does. It is
   * the mode's mistake, not a hang, and must not be reported as one. */
  gint64 start = g_get_monotonic_time();
  mode = build_mode("return { entries = function(s)\n"
                    "  coroutine.yield()\n"
                    "  return { 'never' }\n"
                    "end }\n",
                    FALSE);
  gint64 spent = g_get_monotonic_time() - start;
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "suspended itself"));
  ck_assert_ptr_null(strstr(v, "deadline"));
  g_free(v);
  /* And immediately, which is what distinguishes it from the deadline. */
  ck_assert_int_lt(spent, 1000000);
}
END_TEST

START_TEST(test_lua_overrun_in_a_finalizer_is_reported) {
  /* Lua runs a __gc finalizer with hooks disabled, so no deadline can reach
   * one. That cannot be fixed, but the SILENT half of it can: a mode that
   * overran and then returned normally used to be believed, with no warning
   * anywhere. The clock is checked once after the call so it is not. */
  mode = build_mode("return { entries = function(s)\n"
                    "  setmetatable({}, { __gc = function()\n"
                    "    local t0 = os.clock()\n"
                    "    while os.clock() - t0 < 2.5 do end\n"
                    "  end })\n"
                    "  collectgarbage('collect')\n"
                    "  collectgarbage('collect')\n"
                    "  return { 'finished' }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  ck_assert_ptr_null(strstr(v, "finished"));
  g_free(v);
}
END_TEST

START_TEST(test_escaped_path_loads_as_a_lua_mode) {
  /* The classification unescapes the command line, so the reader has to open
   * the file that decision was made about. Deciding on the unescaped path and
   * then opening the raw one recognised this as a Lua mode and then
   * permanently quarantined it for not existing. */
  char *dir = g_build_filename(tmpdir, "my notes", NULL);
  ck_assert_int_eq(g_mkdir_with_parents(dir, 0700), 0);
  char *path = g_build_filename(dir, "x.lua", NULL);
  GError *error = NULL;
  g_file_set_contents(
      path, "return { entries = function(s) return { 'escaped-ok' } end }\n",
      -1, &error);
  ck_assert_ptr_null(error);

  char *spec = g_strdup_printf("m:%s/my\\ notes/x.lua", tmpdir);
  mode = build_mode_spec(spec);
  g_free(spec);

  ck_assert_int_eq(mode_get_num_entries(mode), 1);
  char *v = display_of(mode, 0);
  ck_assert_str_eq(v, "escaped-ok");
  g_free(v);

  g_unlink(path);
  g_free(path);
  g_rmdir(dir);
  g_free(dir);
}
END_TEST

START_TEST(test_lua_deadline_is_not_disarmed_by_a_kept_coroutine) {
  /* Lua permits a hook to yield only from a line or a count event. Yielding
   * from a call event throws out of a half-built C frame with `allowhook`
   * still zero, and a coroutine the MODE keeps across interactions then stays
   * hooked-off and runs with NO deadline for the rest of the session.
   * REPRODUCED before the fix: the second interaction below ran unbounded and
   * had to be killed. The watchdog now only suspends on a count event. */
  mode = build_mode("local co = nil\n"
                    "return { entries = function(s)\n"
                    "  if co == nil then\n"
                    "    co = coroutine.create(function()\n"
                    "      while true do local _ = select('#', 1) end\n"
                    "    end)\n"
                    "    coroutine.resume(co)\n"
                    "    return { 'unreachable' }\n"
                    "  end\n"
                    "  coroutine.resume(co)\n"
                    "  return { 'after' }\n"
                    "end }\n",
                    FALSE);
  ck_assert_int_eq(mode_get_num_entries(mode), 1);

  /* MENU_CUSTOM_INPUT rather than MENU_OK, because the error row is
   * nonselectable and Return over it reloads without reaching the mode. */
  gint64 start = g_get_monotonic_time();
  char *input = g_strdup("x");
  mode_result(mode, MENU_CUSTOM_INPUT, &input, 0);
  gint64 spent = g_get_monotonic_time() - start;
  g_free(input);

  char *v = display_of(mode, 0);
  ck_assert_ptr_nonnull(strstr(v, "deadline"));
  g_free(v);
  ck_assert_int_lt(spent, 2 * 2000000 + 1000000);
}
END_TEST

START_TEST(test_lua_error_message_clears_on_recovery) {
  /* `message` is a sticky header option, so sofi's own error text would sit in
   * the message bar for the rest of the run once a mode had failed and then
   * recovered without setting `message` itself. */
  mode = build_mode("return { entries = function(s)\n"
                    "  if s.retv == 3 then error('deliberate') end\n"
                    "  return { 'alpha' }\n"
                    "end }\n",
                    FALSE);
  ck_assert_ptr_null(mode_get_message(mode));

  char *input = g_strdup("");
  mode_result(mode, MENU_ENTRY_DELETE, &input, 0);
  g_free(input);
  char *m = mode_get_message(mode);
  ck_assert_ptr_nonnull(m);
  ck_assert_ptr_nonnull(strstr(m, "deliberate"));
  g_free(m);

  input = g_strdup("");
  mode_result(mode, MENU_CUSTOM_INPUT, &input, 0);
  g_free(input);
  /* Recovered, and the mode never set `message`, so the bar is empty again. */
  ck_assert_ptr_null(mode_get_message(mode));
}
END_TEST

START_TEST(test_lua_sticky_message_survives_recovery) {
  /* The other half: a message the MODE set is its own, and clearing sofi's
   * error text must not take it away. */
  mode = build_mode("return { entries = function(s)\n"
                    "  if s.retv == 3 then error('deliberate') end\n"
                    "  if s.retv == 0 then\n"
                    "    return { 'alpha', message = 'mine' }\n"
                    "  end\n"
                    "  return { 'alpha' }\n"
                    "end }\n",
                    FALSE);
  char *m = mode_get_message(mode);
  ck_assert_str_eq(m, "mine");
  g_free(m);

  char *input = g_strdup("");
  mode_result(mode, MENU_CUSTOM_INPUT, &input, 0);
  g_free(input);
  /* No error happened, so the mode's own sticky message is untouched. */
  m = mode_get_message(mode);
  ck_assert_str_eq(m, "mine");
  g_free(m);
}
END_TEST

START_TEST(test_lua_destroy_is_idempotent) {
  /* mode_destroy is reached from five places, and a mode that is both in
   * modes[] and a combi member is destroyed more than once. */
  mode = build_mode("return { entries = function(s) return { 'x' } end }\n",
                    FALSE);
  mode_destroy(mode);
  ck_assert_ptr_null(mode->private_data);
  mode_destroy(mode);
  ck_assert_ptr_null(mode->private_data);
  mode_free(&mode);
  mode = NULL;
}
END_TEST

static Suite *script_lua_suite(void) {
  Suite *s = suite_create("ScriptLua");
  TCase *tc = tcase_create("LuaModes");
  tcase_add_checked_fixture(tc, script_lua_setup, script_lua_teardown);
  /* The watchdog case deliberately burns the deadline, so the case timeout has
   * to exceed it or check would kill the test rather than let the hook do it.
   */
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, test_lua_rows_in_order);
  tcase_add_test(tc, test_lua_row_fields);
  tcase_add_test(tc, test_lua_icon_comma_string);
  tcase_add_test(tc, test_lua_header_options);
  tcase_add_test(tc, test_lua_empty_message_clears);
  tcase_add_test(tc, test_lua_state_round_trip);
  tcase_add_test(tc, test_lua_nil_closes);
  tcase_add_test(tc, test_lua_empty_table_closes);
  tcase_add_test(tc, test_lua_bad_row_skipped);
  tcase_add_test(tc, test_lua_syntax_error_quarantines);
  tcase_add_test(tc, test_lua_wrong_shape_quarantines);
  tcase_add_test(tc, test_lua_missing_entries_quarantines);
  tcase_add_test(tc, test_lua_quarantine_survives_delete);
  tcase_add_test(tc, test_lua_raising_metatable_does_not_abort);
  tcase_add_test(tc, test_lua_raising_mode_table_does_not_abort);
  tcase_add_test(tc, test_lua_error_is_not_permanent);
  tcase_add_test(tc, test_lua_keep_selection_is_not_sticky);
  tcase_add_test(tc, test_lua_sandbox_subtractions);
  tcase_add_test(tc, test_lua_sofi_table);
  tcase_add_test(tc, test_lua_require_finds_sibling);
  tcase_add_test(tc, test_lua_watchdog_stops_a_hang);
  tcase_add_test(tc, test_lua_deadline_is_per_call);
  tcase_add_test(tc, test_executable_lua_still_spawns);
  tcase_add_test(tc, test_shipped_example_runs);
  tcase_add_test(tc, test_interpreter_command_line_still_spawns);
  tcase_add_test(tc, test_bare_name_on_path_still_spawns);
  tcase_add_test(tc, test_shell_escaped_path_still_spawns);
  tcase_add_test(tc, test_lua_debug_not_recoverable_from_package_loaded);
  tcase_add_test(tc, test_lua_pcall_cannot_swallow_the_deadline);
  tcase_add_test(tc, test_lua_require_refuses_bytecode);
  tcase_add_test(tc, test_lua_require_refuses_malformed_bytecode);
  tcase_add_test(tc, test_lua_deadline_survives_a_c_boundary);
  tcase_add_test(tc, test_lua_deadline_survives_a_fresh_coroutine_each_cycle);
  tcase_add_test(tc, test_lua_generator_is_not_truncated);
  tcase_add_test(tc, test_lua_self_yield_is_its_own_error);
  tcase_add_test(tc, test_lua_overrun_in_a_finalizer_is_reported);
  tcase_add_test(tc, test_escaped_path_loads_as_a_lua_mode);
  tcase_add_test(tc, test_lua_deadline_is_not_disarmed_by_a_kept_coroutine);
  tcase_add_test(tc, test_lua_error_message_clears_on_recovery);
  tcase_add_test(tc, test_lua_sticky_message_survives_recovery);
  tcase_add_test(tc, test_lua_destroy_is_idempotent);

  suite_add_tcase(s, tc);
  return s;
}

int main(G_GNUC_UNUSED int argc, G_GNUC_UNUSED char **argv) {
  setup_abe();
  Suite *s = script_lua_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  int number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}