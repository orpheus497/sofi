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

#ifndef SOFI_THEME_LUA_H
#define SOFI_THEME_LUA_H

#include <glib.h>

/**
 * Read a theme from a Lua file and merge it into the current theme.
 *
 * The chunk must return an ARRAY of rules; see source/theme-lua.c for why the
 * order of that array is load-bearing and a table of selectors would not do.
 *
 * @param file the path to read.
 * @returns TRUE on FAILURE, which is the convention the theme entry points
 * have always used and what all of their call sites test.
 */
gboolean sofi_theme_lua_parse_file(const char *file);

/**
 * Read a theme from a Lua string. Same contract as
 * sofi_theme_lua_parse_file().
 */
gboolean sofi_theme_lua_parse_string(const char *string);

/**
 * The extensions a theme name without one is tried with, in order: `.lua` for
 * a theme and `.luainc` for a fragment that is not selectable on its own.
 */
extern const char *lua_theme_file_extensions[];

#endif
