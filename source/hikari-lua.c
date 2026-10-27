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
 * @brief The bounds sofi puts on a hikari configuration that the sandbox
 * cannot express.
 *
 * Script function and purpose: holds the two ceilings and the one allocation
 * ceiling that stop a hikari.lua from never finishing, and is a file of its own
 * because sofi has two readers of that same file -- source/hikari-palette.c for
 * the palette and source/anim.c for the animation duration -- opening it from
 * the same four paths in the same order. A bound only one of them applies is
 * not a bound: either reader alone is enough to hang sofi at startup, so there
 * is exactly one copy of this and both use it.
 *
 * The file being read belongs to ANOTHER PROGRAM, which is what makes this
 * worth the code. A user who has never heard of this code path can pick up a
 * hikari.lua from a dotfiles repository, a theme or a distribution package --
 * /etc/hikari/hikari.lua is one of the four paths both readers try -- and sofi
 * opens it on its own initiative. It is treated as hostile input rather than as
 * something the user wrote and can be expected to have read.
 *
 * WHAT THE STEP HOOK CANNOT DO, which is the whole reason this file exists.
 * Both readers arm lua_sethook() with LUA_MASKCOUNT, and that counts Lua VM
 * instructions. One C function is one instruction to it however long the loop
 * inside that C function runs, so a count hook cannot interrupt one, and two
 * standard library functions have loops long enough to matter. Measured before
 * this file existed: a hikari.lua whose second line was
 * `local s = string.rep("", math.maxinteger)` held `sofi -dump-config` past a
 * 20 second timeout with the hook armed at 200000 and not one firing of it.
 */

#include "config.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "hikari-lua.h"

/* Action purpose: the two ceilings on the string functions whose work happens
 * inside a single C call, where the step hook cannot reach.
 *
 * REP_COUNT_LIMIT bounds string.rep's repeat COUNT and not the size of its
 * result, and that distinction is the entire defect. Lua's own guard refuses a
 * rep whose result would exceed MAXSIZE, which sounds sufficient and is not:
 * `string.rep("", math.maxinteger)` has a zero-length result, so it passes that
 * guard and then runs its copy loop nearly maxinteger times. It allocates
 * nothing, so MEMORY_LIMIT below does not see it either.
 *
 * PATTERN_SUBJECT_LIMIT bounds the subject handed to find, match, gmatch and
 * gsub. Lua's matcher backtracks and the work is superlinear in the length of
 * the subject, so an unbounded subject is the same hazard with a less obvious
 * trigger. This ceiling is the weaker of the two and is stated as such rather
 * than left to be discovered: it does not make pathological backtracking
 * impossible -- a pattern carrying several non-greedy quantifiers is still slow
 * on a short string -- it converts unbounded work into bounded work on bounded
 * input, where the rep case was unbounded outright.
 *
 * Both sit far above anything a configuration does. The longest rep in
 * hikari-sakura's own shipped hikari.lua is a separator at 80, and its longest
 * string is a path. */
#define REP_COUNT_LIMIT (1 << 20)
#define PATTERN_SUBJECT_LIMIT (1 << 16)

/* Action purpose: the ceiling on what a configuration may allocate, which is
 * the other half of the rep problem rather than a second opinion about it. The
 * count ceiling above stops the zero-length rep, which is unbounded time and no
 * memory at all; this stops the opposite case, which is memory the step hook
 * never sees because it is asked for in a handful of instructions.
 *
 * Lua is not silent here and the limit of its help is the point. lstrlib's own
 * MAXSIZE is INT_MAX, so one rep result cannot exceed 2 GiB whatever this file
 * does -- `('x'):rep(1048576):rep(1048576)` is refused by Lua itself with
 * "resulting string too large". What Lua bounds is each result and not their
 * sum. Measured on this machine: `local t = {} for i = 1, 300 do t[i] =
 * ('x'):rep(1048576) end` reached 303 MiB of peak RSS with this ceiling lifted
 * and was refused at 65 MiB with it on, and that loop costs about five
 * instructions an iteration, so what the step hook alone permits is tens of
 * thousands of iterations and tens of gibibytes.
 *
 * Three orders of magnitude above either reader's needs -- the interpreter, the
 * libraries and the sandbox measure 9.2 KiB in the palette reader and 16.6 KiB
 * in the animation reader, which opens the whole standard library -- and equal
 * to the figure hikari-sakura's own reader of this file uses, because the two
 * programs disagreeing about what the file may do is worse than either of them
 * being wrong about it. */
#define MEMORY_LIMIT ((size_t)64 * 1024 * 1024)

/* Function purpose: hand the call on to the real string function, which the
 * guard that checked it carries as upvalue 1. Shared by both guards because the
 * delegation is the fiddly half: the original has to be moved BELOW the
 * arguments it is being given, which is what lua_insert() does here. */
static int delegate_to_real(lua_State *lua) {
  int nargs = lua_gettop(lua);

  lua_pushvalue(lua, lua_upvalueindex(1));
  lua_insert(lua, 1);
  lua_call(lua, nargs, LUA_MULTRET);

  return lua_gettop(lua);
}

/* Function purpose: string.rep with its repeat count bounded.
 *
 * Action purpose: the count is read with luaL_optinteger() and not
 * luaL_checkinteger() so that a call which omits it is refused by the real
 * function, in the real function's words, rather than by this guard inventing
 * a complaint of its own about an argument it was not written to validate. */
static int bounded_rep(lua_State *lua) {
  lua_Integer count = luaL_optinteger(lua, 2, 0);

  if (count > REP_COUNT_LIMIT) {
    return luaL_error(lua, "string.rep count %I is above sofi's limit of %d",
                      count, REP_COUNT_LIMIT);
  }

  return delegate_to_real(lua);
}

/* Function purpose: one of find, match, gmatch and gsub, with the length of its
 * subject bounded. Installed over all four because each takes the subject as
 * its first argument.
 *
 * Action purpose: the length is taken with lua_rawlen() rather than
 * lua_tolstring(), which would be the obvious call and is the wrong one here:
 * lua_tolstring() REWRITES the slot it is given when that slot is not already a
 * string, and the slot in question is argument 1 of the call about to be passed
 * on. lua_rawlen() cannot rewrite anything. A non-string subject measures zero
 * and is left for the real function to coerce as it always has. */
static int bounded_pattern(lua_State *lua) {
  size_t len = lua_type(lua, 1) == LUA_TSTRING ? lua_rawlen(lua, 1) : 0;

  if (len > PATTERN_SUBJECT_LIMIT) {
    return luaL_error(lua,
                      "a %d byte subject is above sofi's %d byte limit for "
                      "pattern matching",
                      (int)len, PATTERN_SUBJECT_LIMIT);
  }

  return delegate_to_real(lua);
}

void sofi_hikari_lua_push_bounded_string(lua_State *lua) {
  static const char *const by_subject[] = {"find", "match", "gmatch", "gsub",
                                           NULL};

  /* Action purpose: luaL_requiref() caches in package.loaded and hands back the
   * cached table on a second call, so this reaches the real library whether or
   * not the caller has already opened it -- which is exactly the difference
   * between the two readers, one of which opens the whole standard library
   * first and one of which opens four libraries by name. */
  luaL_requiref(lua, LUA_STRLIBNAME, luaopen_string, 0);
  lua_newtable(lua);

  /* Action purpose: every entry is copied across and only then are the bounded
   * ones written over the copies. Copying first means a function some future
   * Lua adds still reaches the configuration, rather than silently vanishing
   * from the sandbox the release it arrives in. */
  lua_pushnil(lua);
  while (lua_next(lua, -3) != 0) {
    lua_pushvalue(lua, -2);
    lua_insert(lua, -2);
    lua_settable(lua, -4);
  }

  lua_getfield(lua, -2, "rep");
  lua_pushcclosure(lua, bounded_rep, 1);
  lua_setfield(lua, -2, "rep");

  for (int i = 0; by_subject[i] != NULL; i++) {
    lua_getfield(lua, -2, by_subject[i]);
    lua_pushcclosure(lua, bounded_pattern, 1);
    lua_setfield(lua, -2, by_subject[i]);
  }

  lua_remove(lua, -2);

  /* Action purpose: the bounded table has to become the string METATABLE's
   * `__index` as well, and leaving this out is the way to write a fix that
   * passes its own tests and closes nothing. Replacing only the sandbox's
   * `string` leaves the method-call form going straight past it: `("x"):rep(n)`
   * never reads that field at all, it goes through the metatable Lua installs
   * for every string in the interpreter, whose `__index` is the real library.
   * Mutating that shared metatable is contained because both readers build a
   * fresh interpreter for each candidate file and lua_close() it before
   * returning, so nothing outside one read of one file can observe it. The
   * metatable is reached through an empty string because Lua exposes it no
   * other way. */
  lua_pushliteral(lua, "");
  if (lua_getmetatable(lua, -1)) {
    lua_pushvalue(lua, -3);
    lua_setfield(lua, -2, "__index");
    lua_pop(lua, 1);
  }
  lua_pop(lua, 1);
}

/* Function purpose: answer a read of env.NAME with that environment variable,
 * or with nil when it is unset. Reached through the table's __index, so the
 * table itself stays empty and no copy of the environment is made.
 *
 * Action purpose: a Lua string may carry an embedded NUL and getenv(3) stops at
 * the first one, so `env["PATH\0anything"]` would otherwise answer with PATH.
 * A name whose C length is not its Lua length is not the name of a variable,
 * and is answered with nil rather than with the value of its prefix. */
static int env_index(lua_State *lua) {
  size_t len;
  const char *name;
  const char *value;

  if (lua_type(lua, 2) != LUA_TSTRING) {
    lua_pushnil(lua);

    return 1;
  }

  name = lua_tolstring(lua, 2, &len);
  value = (strlen(name) == len) ? getenv(name) : NULL;

  if (value == NULL) {
    lua_pushnil(lua);
  } else {
    lua_pushstring(lua, value);
  }

  return 1;
}

/* Function purpose: refuse an assignment to env. The table is a view of this
 * process's environment, not a place to keep things, and a configuration that
 * believes it has set a variable would be wrong in a way nothing reports. */
static int env_newindex(lua_State *lua) {
  return luaL_error(lua, "env is read-only");
}

void sofi_hikari_lua_push_env(lua_State *lua) {
  lua_newtable(lua);

  /* Action purpose: __metatable = false is the half that makes the other two
   * hold. Without it a configuration reads the metatable with getmetatable(env)
   * and replaces __newindex, and read-only lasts exactly as long as nobody
   * tries. With it, getmetatable answers false and setmetatable raises. */
  lua_createtable(lua, 0, 3);
  lua_pushcfunction(lua, env_index);
  lua_setfield(lua, -2, "__index");
  lua_pushcfunction(lua, env_newindex);
  lua_setfield(lua, -2, "__newindex");
  lua_pushboolean(lua, 0);
  lua_setfield(lua, -2, "__metatable");

  lua_setmetatable(lua, -2);
}

/* Function purpose: Lua's allocator with a ceiling on it. Refusing a growing
 * request makes the core raise LUA_ERRMEM, which the protected call each reader
 * runs the configuration inside turns into an ordinary diagnostic and a kept
 * default.
 *
 * Action purpose: `used` is signed and starts at zero, which is the price of
 * installing this over an interpreter luaL_newstate() has already built -- see
 * sofi_hikari_lua_newstate(). The interpreter's own first few kilobytes were
 * allocated before this function was reachable and are not in the total, so
 * releasing one of those blocks drives the total NEGATIVE. That is correct and
 * not merely tolerated: the ceiling bounds what the configuration adds, and a
 * negative total is baseline the interpreter has handed back. An unsigned total
 * would wrap on the first such free and then refuse every allocation for the
 * rest of the read, which is a configuration declined for a reason that has
 * nothing to do with it.
 *
 * When ptr is NULL the osize argument is a type tag and not a size, so the old
 * size has to be taken as zero. That is the one part of the lua_Alloc contract
 * which is easy to get wrong and it corrupts the total silently. */
static void *bounded_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  SofiHikariLuaBudget *budget = ud;
  size_t old = ptr == NULL ? 0 : osize;
  void *grown;

  if (nsize == 0) {
    free(ptr);
    budget->used -= (ptrdiff_t)old;

    return NULL;
  }

  /* Action purpose: the headroom is the difference of the two signed totals and
   * the cast of it is always meaningful, because nothing is let through that
   * would take `used` past `cap` and so the difference is never negative. The
   * growth it is compared against is computed in size_t and never as
   * `used + nsize`, which is the subtraction that overflows. */
  if (nsize > old && nsize - old > (size_t)(budget->cap - budget->used)) {
    return NULL;
  }

  grown = realloc(ptr, nsize);
  if (grown == NULL) {
    return NULL;
  }

  budget->used = budget->used - (ptrdiff_t)old + (ptrdiff_t)nsize;

  return grown;
}

lua_State *sofi_hikari_lua_newstate(SofiHikariLuaBudget *budget) {
  lua_State *lua = luaL_newstate();

  budget->used = 0;
  budget->cap = (ptrdiff_t)MEMORY_LIMIT;

  if (lua == NULL) {
    return NULL;
  }

  /* Action purpose: the interpreter is built by luaL_newstate() and the
   * allocator swapped in afterwards, rather than passed to lua_newstate(), for
   * two reasons that both point the same way. luaL_newstate() installs the
   * panic function that prints a line before abort() where lua_newstate()
   * leaves none, and lua_newstate() grew a third argument in Lua 5.4's
   * successor while this tree builds against anything from 5.4 up -- naming it
   * here would make a toolchain that compiles today stop compiling. The cost is
   * that the interpreter's own baseline sits outside the total, which
   * bounded_alloc() explains is harmless. */
  lua_setallocf(lua, bounded_alloc, budget);

  return lua;
}

void sofi_hikari_lua_release(SofiHikariLuaBudget *budget) {
  /* Action purpose: lifting the ceiling once the chunk has returned is what
   * keeps this change from trading one abort for another. The ceiling exists to
   * bound the configuration, and the configuration is finished; what runs after
   * it is each reader's own walk of the table it left behind, whose
   * lua_getfield calls allocate a few hundred bytes interning key names.
   * Leaving the ceiling armed would let a file that parks 64 MiB of live data
   * and then returns a perfectly good palette fail one of those internings
   * instead -- and that LUA_ERRMEM is raised with no protected call under it,
   * which is abort(). */
  budget->cap = PTRDIFF_MAX;
}
