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

/*
 * The two public entry points -- sofi_theme_parse_file() and
 * sofi_theme_parse_string() -- are declared in theme.h, where they have always
 * been, and defined in source/theme-lua.c. A theme is a Lua chunk returning an
 * ARRAY of directives; see the head of that file for why the order of that
 * array is load-bearing and a table of selectors would not do.
 *
 * Both return TRUE on FAILURE, which is the convention they have always used
 * and what every one of their call sites tests.
 */

/**
 * The extensions a theme name without one is tried with, in order: `.lua` for
 * a theme and `.luainc` for a fragment that is not selectable on its own.
 */
extern const char *lua_theme_file_extensions[];

#endif
