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

#ifndef SOFI_HIKARI_LUA_H
#define SOFI_HIKARI_LUA_H

#include <stddef.h>

#include <lua.h>

/**
 * The running allocation total of one configuration read, which the caller
 * owns and must keep alive for as long as the interpreter it was handed to:
 * Lua stores the pointer, not the struct.
 *
 * `used` is signed on purpose and may go negative. See bounded_alloc() in
 * source/hikari-lua.c, which is the only code that touches either field.
 */
typedef struct {
  ptrdiff_t used;
  ptrdiff_t cap;
} SofiHikariLuaBudget;

/**
 * An interpreter for one hikari configuration, with the allocation ceiling
 * already installed on it.
 *
 * @param budget storage for the running total, zeroed here, which must outlive
 * the returned interpreter.
 * @returns an interpreter the caller closes with lua_close(), or NULL when one
 * could not be created.
 */
lua_State *sofi_hikari_lua_newstate(SofiHikariLuaBudget *budget);

/**
 * Lift the allocation ceiling, because the configuration has finished running
 * and what remains is sofi's own reading of the table it left behind.
 *
 * @param budget the total handed to sofi_hikari_lua_newstate().
 */
void sofi_hikari_lua_release(SofiHikariLuaBudget *budget);

/**
 * Build the bounded replacement for the string library, leave it on the top of
 * the stack, and point the shared string metatable at it on the way out so
 * that the method-call form reaches it too.
 *
 * The caller installs the returned table as `string` in its sandbox.
 */
void sofi_hikari_lua_push_bounded_string(lua_State *lua);

/**
 * Build hikari's `env` table and leave it on the top of the stack.
 *
 * hikari-sakura's own reader hands a configuration an `env` through which it
 * reads the environment, and its shipped hikari.lua uses it. A sandbox without
 * one does not merely lose that value: indexing an absent global raises, which
 * ends the whole chunk before anything in it has been read. Reading the
 * compositor's file means providing what the compositor provides.
 *
 * The caller installs the returned table as `env` in its sandbox.
 */
void sofi_hikari_lua_push_env(lua_State *lua);

/**
 * Build the bounded replacement for the table library and leave it on the top
 * of the stack.
 *
 * Only `table.move` is bounded, and unlike the string library there is no
 * metatable to repoint: a table has no default metatable and no `t:move()`
 * form, so the sandbox's `table` is the only route to it. See the note in
 * source/hikari-lua.c for why the other range-taking functions need nothing.
 *
 * The caller installs the returned table as `table` in its sandbox.
 */
void sofi_hikari_lua_push_bounded_table(lua_State *lua);

#endif
