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

#ifndef SOFI_HIKARI_PALETTE_H
#define SOFI_HIKARI_PALETTE_H

#include <glib.h>

/**
 * Read hikari-sakura's palette out of its Lua configuration and render it as a
 * theme fragment: the sixteen slots, the five tones that have to be derived
 * from them, and the four roles that are settled by measuring the palette
 * rather than by naming a slot.
 *
 * The fragment is itself Lua -- one rule naming `*`, in the format
 * source/theme-lua.c reads -- so the same interpreter that read the
 * compositor's configuration reads it back. Parsed after the compiled-in
 * palette, so it overrides it, and before roles.lua. Finding no usable
 * configuration is the ordinary case.
 *
 * @returns a newly allocated Lua chunk the caller frees, or NULL when there is
 * no configuration to read or it carries no complete palette.
 */
char *sofi_hikari_palette_lua(void);

#endif
