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

#define G_LOG_DOMAIN "Modes.Script"

/** The log domain of this dialog. */
#include "glib.h"

#include "display.h"
#include "helper.h"
#include "modes/script.h"
#include "sofi.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "widgets/textbox.h"

#include "mode-private.h"

#include "sofi-icon-fetcher.h"

#include "modes/dmenuscriptshared.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

typedef struct {
  /** ID of the current script. */
  unsigned int id;
  /** List of visible items. */
  DmenuScriptEntry *cmd_list;
  /** length list of visible items. */
  unsigned int cmd_list_length;

  /** Urgent list */
  struct sofi_range_pair *urgent_list;
  unsigned int num_urgent_list;
  /** Active list */
  struct sofi_range_pair *active_list;
  unsigned int num_active_list;
  /** Configuration settings. */
  char *message;
  char *prompt;
  char *data;
  gboolean do_markup;
  gboolean keep_selection;
  int64_t new_selection;
  char delim;
  /** no custom */
  gboolean no_custom;
  /** keep filter */
  gboolean keep_filter;

  gboolean use_hot_keys;

  int switch_mode;

  /** This mode's interpreter, NULL for a spawned script mode. Also the
   * discriminator script_run_executor() reads to pick a producer. */
  lua_State *lua;
  /** Wall-clock instant past which the watchdog stops the mode. Set
   * immediately before each protected call and read by the debug hook, which
   * reaches it through the state's extra space. */
  gint64 lua_deadline;
  /** TRUE when the watchdog is what suspended the coroutine. `coroutine` is
   * deliberately available to a mode, so a mode calling coroutine.yield() on
   * its own also returns LUA_YIELD, and without this the two would be reported
   * with the same wrong message. Also the STICKY half of the deadline: once it
   * is set the watchdog stops consulting the clock and keeps trying to suspend
   * the mode at every opportunity. */
  gboolean lua_expired;
  /** TRUE while `message` holds a Lua error rather than something the mode
   * asked for. `message` is a STICKY header option, so a mode that reports an
   * error and then recovers without setting `message` itself would otherwise
   * leave the error text in the message bar for the rest of the run. */
  gboolean message_is_error;
  /** The coroutine this interaction runs on. The watchdog may only suspend
   * THIS thread: lua_newthread copies the hook from its creator, so every
   * coroutine a mode makes for itself inherits the watchdog, and suspending
   * one of those would look to the mode like its own generator finishing. */
  lua_State *lua_co;
  /** TRUE once the mode has failed in a way that must not be retried: the
   * chunk would not load, or it returned the wrong shape. The producer then
   * answers with a one-row error snapshot and never enters Lua again. Separate
   * from `lua == NULL`, which is also what an ordinary spawned script mode
   * looks like. */
  gboolean quarantined;
} ScriptModePrivateData;

/**
 * Shared function between DMENU and Script mode.
 */
void dmenuscript_parse_entry_extras(G_GNUC_UNUSED Mode *sw,
                                    DmenuScriptEntry *entry, char *buffer,
                                    G_GNUC_UNUSED size_t length) {
  gchar **extras = g_strsplit(buffer, "\x1f", -1);
  gchar **extra = extras;
  for (; *extra != NULL && *(extra + 1) != NULL; extra += 2) {
    gchar *key = *extra;
    gchar *value = *(extra + 1);
    // Mark NULL
    *(extra) = NULL;
    *(extra + 1) = NULL;
    if (strcasecmp(key, "icon") == 0) {
      entry->icon_name = g_strsplit(value, ",", -1);
      g_free(value);
    } else if (strcasecmp(key, "display") == 0) {
      entry->display = value;
    } else if (strcasecmp(key, "meta") == 0) {
      entry->meta = value;
    } else if (strcasecmp(key, "info") == 0) {
      entry->info = value;
    } else if (strcasecmp(key, "nonselectable") == 0) {
      entry->nonselectable = g_ascii_strcasecmp(value, "true") == 0;
      g_free(value);
    } else if (strcasecmp(key, "permanent") == 0) {
      entry->permanent = g_ascii_strcasecmp(value, "true") == 0;
      g_free(value);
    } else if (strcasecmp(key, "urgent") == 0) {
      entry->urgent = g_ascii_strcasecmp(value, "true") == 0;
      g_free(value);
    } else if (strcasecmp(key, "active") == 0) {
      entry->active = g_ascii_strcasecmp(value, "true") == 0;
      g_free(value);
    } else {
      g_free(value);
    }
    g_free(key);
  }
  // Got an extra entry.. lets free it.
  if (*extras != NULL) {
    g_free(*extras);
  }
  g_free(extras);
}

/**
 * End of shared functions.
 */

static void script_switch_mode(Mode *sw, char *value) {
  int index = mode_lookup(value);
  ScriptModePrivateData *pd = (ScriptModePrivateData *)sw->private_data;

  if (index >= 0) {
    pd->switch_mode = index;
  } else {
    g_warning("Mode \"%s\" not found, please make sure it's enabled.\n", value);
  }
}

static void parse_header_entry(Mode *sw, char *line, ssize_t length) {
  ScriptModePrivateData *pd = (ScriptModePrivateData *)sw->private_data;
  ssize_t length_key = 0; // strlen ( line );
  while (length_key < length && line[length_key] != '\x1f') {
    length_key++;
  }

  if ((length_key + 1) < length) {
    line[length_key] = '\0';
    char *value = line + length_key + 1;
    if (strcasecmp(line, "message") == 0) {
      g_free(pd->message);
      pd->message = strlen(value) ? g_strdup(value) : NULL;
    } else if (strcasecmp(line, "prompt") == 0) {
      g_free(pd->prompt);
      pd->prompt = g_strdup(value);
      sw->display_name = pd->prompt;
    } else if (strcasecmp(line, "markup-rows") == 0) {
      pd->do_markup = (strcasecmp(value, "true") == 0);
    } else if (strcasecmp(line, "urgent") == 0) {
      parse_ranges(value, &(pd->urgent_list), &(pd->num_urgent_list));
    } else if (strcasecmp(line, "active") == 0) {
      parse_ranges(value, &(pd->active_list), &(pd->num_active_list));
    } else if (strcasecmp(line, "delim") == 0) {
      pd->delim = helper_parse_char(value);
    } else if (strcasecmp(line, "no-custom") == 0) {
      pd->no_custom = (strcasecmp(value, "true") == 0);
    } else if (strcasecmp(line, "use-hot-keys") == 0) {
      pd->use_hot_keys = (strcasecmp(value, "true") == 0);
    } else if (strcasecmp(line, "keep-selection") == 0) {
      pd->keep_selection = (strcasecmp(value, "true") == 0);
    } else if (strcasecmp(line, "keep-filter") == 0) {
      pd->keep_filter = (strcasecmp(value, "true") == 0);
    } else if (strcasecmp(line, "new-selection") == 0) {
      pd->new_selection = (int64_t)g_ascii_strtoll(value, NULL, 0);
    } else if (strcasecmp(line, "data") == 0) {
      g_free(pd->data);
      pd->data = g_strdup(value);
    } else if (strcasecmp(line, "theme") == 0) {
      if (sofi_theme_parse_string((const char *)value)) {
        g_warning("Failed to parse: '%s'", value);
        sofi_clear_error_messages();
      }
    } else if (strcasecmp(line, "switch-mode") == 0) {
      script_switch_mode(sw, value);
    }
  }
}

static DmenuScriptEntry *execute_executor(Mode *sw, char *arg,
                                          unsigned int *length, int value,
                                          DmenuScriptEntry *entry,
                                          char *input) {
  ScriptModePrivateData *pd = (ScriptModePrivateData *)sw->private_data;
  int fd = -1;
  GError *error = NULL;
  DmenuScriptEntry *retv = NULL;
  char **argv = NULL;
  int argc = 0;
  *length = 0;
  // Reset these between runs.
  pd->new_selection = -1;
  pd->keep_selection = 0;
  pd->keep_filter = 0;
  // Environment
  char **env = g_get_environ();

  char *str_value = g_strdup_printf("%d", value);
  env = g_environ_setenv(env, "SOFI_RETV", str_value, TRUE);
  g_free(str_value);

  str_value = g_strdup_printf("%d", (int)getpid());
  env = g_environ_setenv(env, "SOFI_OUTSIDE", str_value, TRUE);
  g_free(str_value);

  if (entry && entry->info) {
    env = g_environ_setenv(env, "SOFI_INFO", entry->info, TRUE);
  }
  if (pd->data) {
    env = g_environ_setenv(env, "SOFI_DATA", pd->data, TRUE);
  }

  if (input != NULL) {
    env = g_environ_setenv(env, "SOFI_INPUT", input, TRUE);
  }

  if (g_shell_parse_argv(sw->ed, &argc, &argv, &error)) {
    argv = g_realloc(argv, (argc + 2) * sizeof(char *));
    argv[argc] = g_strdup(arg);
    argv[argc + 1] = NULL;
    g_spawn_async_with_pipes(NULL, argv, env, G_SPAWN_SEARCH_PATH, NULL, NULL,
                             NULL, NULL, &fd, NULL, &error);
  }
  g_strfreev(env);
  if (error != NULL) {
    char *msg = g_strdup_printf("Failed to execute: '%s'\nError: '%s'",
                                (char *)sw->ed, error->message);
    sofi_view_error_dialog(msg, FALSE);
    g_free(msg);
    // print error.
    g_error_free(error);
    fd = -1;
  }
  if (fd >= 0) {
    FILE *inp = fdopen(fd, "r");
    if (inp) {
      char *buffer = NULL;
      size_t buffer_length = 0;
      ssize_t read_length = 0;
      size_t actual_size = 0;
      while ((read_length = getdelim(&buffer, &buffer_length, pd->delim, inp)) >
             0) {
        // Filter out line-end.
        if (buffer[read_length - 1] == pd->delim) {
          buffer[read_length - 1] = '\0';
        }
        if (buffer[0] == '\0') {
          parse_header_entry(sw, &buffer[1], read_length - 1);
        } else {
          if (actual_size < ((*length) + 2)) {
            actual_size += 256;
            // Check if it returns NULL, if it does, break out and show what we
            // have so far.
            DmenuScriptEntry *retvn =
                g_realloc(retv, (actual_size) * sizeof(DmenuScriptEntry));
            if (retvn == NULL) {
              break;
            }
            retv = retvn;
          }
          if (retv) {
            size_t buf_length = strlen(buffer) + 1;
#if GLIB_CHECK_VERSION(2, 68, 0)
            retv[(*length)].entry = g_memdup2(buffer, buf_length);
#else
            retv[(*length)].entry = g_memdup(buffer, buf_length);
#endif
            retv[(*length)].icon_name = NULL;
            /* Action purpose: entry 0 lands in memory from g_realloc(NULL, ..),
             * which does not zero, and only entries 1..n are covered by the
             * previous iteration's memset below. Leaving this field
             * indeterminate lets the first row start its icon search at an
             * arbitrary index. */
            retv[(*length)].icon_fallback_index = 0;
            retv[(*length)].display = NULL;
            retv[(*length)].meta = NULL;
            retv[(*length)].info = NULL;
            retv[(*length)].active = FALSE;
            retv[(*length)].urgent = FALSE;
            retv[(*length)].icon_fetch_uid = 0;
            retv[(*length)].icon_fetch_size = 0;
            retv[(*length)].icon_fetch_scale = 0;
            retv[(*length)].nonselectable = FALSE;
            retv[(*length)].permanent = FALSE;
            if (buf_length > 0 && (read_length > (ssize_t)buf_length)) {
              dmenuscript_parse_entry_extras(sw, &(retv[(*length)]),
                                             buffer + buf_length,
                                             read_length - buf_length);
            }
            memset(&(retv[(*length) + 1]), 0, sizeof(DmenuScriptEntry));
            (*length)++;
          }
        }
      }
      if (buffer) {
        free(buffer);
      }
      if (fclose(inp) != 0) {
        g_warning("Failed to close stdout off executor script: '%s'",
                  g_strerror(errno));
      }
    }
  }
  g_strfreev(argv);
  return retv;
}

/**
 * The Lua producer.
 *
 * A Lua mode is a script mode whose process is a lua_State and whose standard
 * output is a Lua table. Everything below this comment replaces one thing and
 * one thing only: the step that turns an interaction into a DmenuScriptEntry
 * array. The eleven vtable functions above and below it do not care where that
 * array came from, so none of them is modified.
 */

/** Action purpose: the longest a mode may spend inside one interaction. A
 * 100,000-row build -- six fields per row, string.format, a two-name icon array
 * and a table.sort -- costs about 1.3e7 VM instructions and 0.75s of CPU, so
 * two seconds leaves roughly three times the headroom over the largest list
 * anyone plausibly builds while still being unmistakably a hang rather than a
 * stutter. An instruction budget was rejected: it is a false positive on an
 * honest large list and a zero-instruction false negative on a blocking C
 * call. */
#define SCRIPT_LUA_DEADLINE_US 2000000

/** Action purpose: how often the hook samples the clock. Not a budget. At the
 * ~1.2e8 instructions per second a release build runs, 10000 instructions is
 * well under a frame between samples, and the cost is one vDSO clock read per
 * sample, which measures below run-to-run variance. */
#define SCRIPT_LUA_SAMPLE 10000

/** Action purpose: the events the watchdog listens for. The instruction count
 * is the budget; the call and return events are what make that budget apply to
 * a coroutine the mode has only just created.
 *
 * lua_newthread copies hook, hookmask and basehookcount from its creator, so a
 * brand new thread starts with a WHOLE fresh sampling interval. A mode whose
 * loop body creates one coroutine per iteration therefore gets
 * SCRIPT_LUA_SAMPLE free instructions every cycle, for as long as it likes, and
 * no count-based deadline can see it -- reproduced as an unbounded hang from
 * `while true do pcall(coroutine.wrap(f)) end`. A call event fires on the new
 * thread's first call instead, so no thread in this interpreter can be entered
 * without the deadline being consulted. */
#define SCRIPT_LUA_EVENTS (LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET)

/** Function purpose: the private data behind a lua_State. Lua's extra space is
 * a pointer-sized area the library itself never touches, which is the only way
 * a debug hook -- whose single argument is the state -- can reach the deadline
 * it has to enforce. */
static ScriptModePrivateData *script_lua_pd(lua_State *L) {
  return *(ScriptModePrivateData **)lua_getextraspace(L);
}

/** Function purpose: stop a mode that will not come back.
 *
 * It YIELDS and does not raise, and that distinction is the whole of the
 * deadline's enforceability. An error raised from a hook is an ordinary Lua
 * error, so a mode whose work sits inside `pcall` catches it, discards it and
 * carries on -- and the hook raises again at the next sample, which it catches
 * again. MEASURED against a mode doing `while true do pcall(f) end`: 5.8
 * seconds of a 2 second deadline, with 73054 deadline errors swallowed.
 *
 * A yield cannot be caught. Lua 5.4 implements `pcall` with a continuation, so
 * it propagates a yield rather than trapping it, and the yield unwinds past
 * every protected call the mode built to the lua_resume that started it.
 * Yielding from a hook is explicitly supported as long as no continuation
 * function is supplied, which is what lua_yield passes.
 */
static void script_lua_watchdog(lua_State *L, lua_Debug *ar) {
  ScriptModePrivateData *pd = script_lua_pd(L);
  if (pd == NULL) {
    return;
  }
  if (!pd->lua_expired) {
    if (g_get_monotonic_time() <= pd->lua_deadline) {
      return;
    }
    /* Action purpose: the deadline becomes STICKY, and from here the clock is
     * never consulted again -- there is no way back from expired. */
    pd->lua_expired = TRUE;
  }

  /* Action purpose: sampling drops to every instruction, on whichever thread
   * this is, so the next count event arrives immediately. */
  lua_sethook(L, script_lua_watchdog, SCRIPT_LUA_EVENTS, 1);

  /* Action purpose: ONLY A COUNT EVENT MAY SUSPEND. Lua permits a hook to
   * yield only from a line or a count event; from a call or a return event the
   * current frame is a C frame that is only half set up, and lua_yieldk takes
   * the C path and throws from inside the hook with `allowhook` still zero.
   * The abandoned interaction coroutine never notices, but a coroutine the
   * MODE keeps across interactions does: it stays hooked-off and runs with no
   * deadline at all for the rest of the session. REPRODUCED -- a mode that
   * tripped the deadline inside a coroutine it stored, then resumed that same
   * coroutine on the next interaction, ran unbounded and had to be killed.
   *
   * So a call or return event only records the expiry and re-arms above. The
   * next count event does the suspending, which costs at most one instruction
   * because entering a Lua function always executes one. */
  if (ar->event != LUA_HOOKCOUNT) {
    return;
  }

  /* Action purpose: suspend wherever suspending is possible, on ANY thread,
   * because a yield is the only interruption a mode cannot catch. */
  if (lua_isyieldable(L)) {
    lua_yield(L, 0);
    /* THE RETURN IS LOAD-BEARING. lua_yield called from a hook does not throw:
     * it records LUA_YIELD on the thread and returns, and the VM performs the
     * suspension once the hook function has returned. Falling through to the
     * luaL_error below would longjmp first and the pending yield would never
     * become one. */
    return;
  }

  /* Action purpose: SUSPENDING IS NOT ALWAYS POSSIBLE. lua_yield refuses
   * across a non-yieldable C frame -- inside table.sort's comparator, inside
   * string.gsub's replacement function, inside any metamethod reached through
   * a C call. MEASURED against `while true do pcall(table.sort, t, cmp) end`:
   * six seconds of a two second deadline, with 57675 errors swallowed.
   *
   * So where the mode cannot be suspended it is unwound instead. The error IS
   * catchable, and that is survivable precisely because `lua_expired` is
   * sticky and sampling has dropped to one instruction: catching it returns
   * the mode to code where the next count event is a yield it cannot catch. */
  luaL_error(L, "mode exceeded its %d ms deadline and was stopped",
             SCRIPT_LUA_DEADLINE_US / 1000);
}

/** Action purpose: Lua runs hooked only inside a protected call, so the hook
 * can never fire where there is nothing to unwind into. lua_sethook resets the
 * instruction count, which is what makes the deadline per-interaction rather
 * than per-state: a mode used fifty times must not die on the fiftieth. */
static void script_lua_arm(lua_State *L, ScriptModePrivateData *pd) {
  pd->lua_deadline = g_get_monotonic_time() + SCRIPT_LUA_DEADLINE_US;
  pd->lua_expired = FALSE;
  pd->lua_co = L;
  lua_sethook(L, script_lua_watchdog, SCRIPT_LUA_EVENTS, SCRIPT_LUA_SAMPLE);
}

static void script_lua_disarm(lua_State *L, ScriptModePrivateData *pd) {
  lua_sethook(L, NULL, 0, 0);
  pd->lua_co = NULL;
}

/** Function purpose: call into a mode under the deadline, and return exactly
 * one value whatever happens.
 *
 * On entry the function and its `nargs` arguments are the top of L. On return
 * they are replaced by a single value: the function's first result on success,
 * or a message string describing the failure. The status is LUA_OK, LUA_YIELD
 * for the deadline, or the error status.
 *
 * The call runs on a coroutine rather than through lua_pcall because that is
 * what lets the watchdog yield, and a yield is the only interruption a mode
 * cannot catch -- see script_lua_watchdog. The coroutine is created per call
 * and abandoned afterwards: freeing a thread closes its upvalues, including any
 * to-be-closed variables a mode declared, so a thread left suspended by the
 * deadline still runs its cleanup when it is collected.
 *
 * There is no message handler. luaL_traceback can trace another thread, so the
 * file and line a mode author needs are read off the coroutine here, which is
 * one fewer long-lived value on the main stack than a handler installed for the
 * life of the state.
 */
static int script_lua_call(lua_State *L, ScriptModePrivateData *pd, int nargs) {
  lua_State *co = lua_newthread(L);
  /* Action purpose: the thread is pushed above the call, so it is moved below
   * it before the call is handed over -- which leaves the thread as the one
   * value this function has to remove at the end. */
  lua_insert(L, -(nargs + 2));
  lua_xmove(L, co, nargs + 1);

  script_lua_arm(co, pd);
  int nres = 0;
  int status = lua_resume(co, L, nargs, &nres);
  script_lua_disarm(co, pd);

  /* Action purpose: catch the time the hook cannot see. Lua runs a __gc
   * finalizer with hooks disabled, and no hook fires while a C call blocks, so
   * a mode can overrun the deadline and still return normally -- MEASURED at
   * 4185ms against a 2000ms deadline from a finalizer doing a bounded loop,
   * with no warning anywhere, which is the worse half of that hazard because
   * it is silent. Checking the clock once after the call cannot stop a
   * finalizer that never returns, but it does mean a mode that overran is
   * never quietly believed. */
  if (status == LUA_OK && g_get_monotonic_time() > pd->lua_deadline) {
    pd->lua_expired = TRUE;
    lua_settop(co, 0);
    status = LUA_YIELD;
  }

  if (status == LUA_OK) {
    if (nres == 0) {
      lua_pushnil(co);
    } else if (nres > 1) {
      lua_pop(co, nres - 1);
    }
    lua_xmove(co, L, 1);
  } else if (status == LUA_YIELD) {
    lua_settop(co, 0);
    /* Action purpose: a mode that calls coroutine.yield() itself suspends the
     * same way the deadline does, and `coroutine` is deliberately available to
     * modes, so the two have to be told apart or an ordinary mistake would be
     * reported as a hang. */
    if (pd->lua_expired) {
      lua_pushfstring(L, "mode exceeded its %d ms deadline and was stopped",
                      SCRIPT_LUA_DEADLINE_US / 1000);
    } else {
      lua_pushliteral(L, "mode suspended itself; entries must return a list "
                         "or nil, and must not yield");
    }
  } else {
    /* The traceback is read before the coroutine's stack is cleared, because
     * the message belongs to that stack. */
    const char *msg = lua_tostring(co, -1);
    luaL_traceback(L, co, msg != NULL ? msg : "(non-string error)", 0);
    lua_settop(co, 0);
  }
  lua_remove(L, -2);
  return status;
}

/** Function purpose: read one field of a mode's table WITHOUT running Lua.
 * Every such read happens after the call into the mode has returned, where no
 * protected frame exists: lua_getfield honours __index, and a metamethod that
 * raises there reaches Lua's panic function and aborts the process. A mode
 * gets setmetatable, so the hazard is reachable rather than theoretical.
 * `idx` must be absolute, because pushing the key shifts relative indices. */
static int script_lua_rawfield(lua_State *L, int idx, const char *key) {
  lua_pushstring(L, key);
  return lua_rawget(L, idx);
}

/** Function purpose: take one string out of Lua. The pointer lua_tostring
 * returns is owned by the Lua collector and dies with the stack slot, so the
 * copy happens here and no caller ever holds a Lua pointer. Returns NULL when
 * the value is not a string or a number -- the type check comes BEFORE the
 * conversion, because luaL_tolstring never fails: it calls __tostring, which a
 * mode can define and which would therefore raise outside any protected frame,
 * and it renders nil as the three-character string "nil". */
static char *script_lua_take(lua_State *L, int idx) {
  int t = lua_type(L, idx);
  if (t != LUA_TSTRING && t != LUA_TNUMBER) {
    return NULL;
  }
  return g_strdup(lua_tostring(L, idx));
}

/** Function purpose: render a header option's value the way the line protocol
 * writes it, so parse_header_entry sees exactly what a script mode's
 * "\0key\x1fvalue" line produces. A boolean becomes the word the protocol
 * compares against; a table, function or userdata is refused. */
static char *script_lua_option_value(lua_State *L, int idx) {
  switch (lua_type(L, idx)) {
  case LUA_TBOOLEAN:
    return g_strdup(lua_toboolean(L, idx) ? "true" : "false");
  case LUA_TSTRING:
  case LUA_TNUMBER:
    return g_strdup(lua_tostring(L, idx));
  default:
    return NULL;
  }
}

/** Action purpose: launch without blocking sofi. This is the one capability C
 * must provide, and the only reason the sofi table exists: Lua's own
 * os.execute waits for the child, and a wait inside a mode is a wait inside
 * sofi's main loop. The call is the same one run and drun make, so a mode
 * inherits their terminal and startup-notification handling. */
static int script_lua_exec(lua_State *L) {
  const char *cmd = luaL_checkstring(L, 1);
  gboolean in_term = lua_toboolean(L, 2);
  lua_pushboolean(L, helper_execute_command(NULL, cmd, in_term, NULL));
  return 1;
}

static int script_lua_log(lua_State *L) {
  const char *level = luaL_checkstring(L, 1);
  const char *msg = luaL_checkstring(L, 2);
  if (g_strcmp0(level, "debug") == 0) {
    g_debug("%s", msg);
  } else if (g_strcmp0(level, "warning") == 0) {
    g_warning("%s", msg);
  } else {
    g_message("%s", msg);
  }
  return 0;
}

/** Action purpose: escaping markup by hand is the first bug a mode author
 * writes, and it is a silent one: script_token_match strips markup with
 * pango_parse_markup and returns FALSE when the parse fails, so a row with
 * malformed markup stops matching rather than stops rendering. */
static int script_lua_markup_escape(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  char *esc = g_markup_escape_text(s, -1);
  lua_pushstring(L, esc);
  g_free(esc);
  return 1;
}

/** Action purpose: sofi.exec hands its argument to g_shell_parse_argv, so a
 * value carrying a space, a quote or an ampersand has to arrive quoted. */
static int script_lua_shell_quote(lua_State *L) {
  const char *s = luaL_checkstring(L, 1);
  char *q = g_shell_quote(s);
  lua_pushstring(L, q);
  g_free(q);
  return 1;
}

/** Action purpose: the five subtractions, and the rule behind them -- a Lua
 * mode gets every capability a script mode has, and does not get the
 * capabilities that exist only because it shares sofi's process. The test for
 * each is one question: could a spawned child do this to sofi? io entire,
 * os.execute, io.popen, setmetatable, pcall, coroutine, utf8 and require for
 * pure-Lua modules are all kept, because a script mode can do every one of
 * them to itself and to the system. */
/** Function purpose: find and load a pure-Lua module in TEXT MODE ONLY.
 *
 * The stock searcher calls luaL_loadfilex with no mode restriction, which means
 * "text or binary", so require() would accept a precompiled chunk -- and Lua
 * 5.4 does not verify bytecode, so a crafted one is arbitrary control of SOFI's
 * virtual machine from a data file. That is precisely the asymmetry the sandbox
 * rule exists to remove: a spawned script mode can load whatever bytecode it
 * likes into its OWN interpreter and none into ours.
 *
 * Upvalue 1 is package.searchpath and upvalue 2 is the package table, both
 * captured when this is installed, before any mode code has run. Capturing them
 * rather than reading globals at call time means a mode cannot redirect the
 * search by reassigning `package`.
 */
static int script_lua_searcher(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);

  lua_pushvalue(L, lua_upvalueindex(1));
  lua_pushstring(L, name);
  /* Action purpose: package.path is read at call time, not captured, because a
   * mode is allowed to extend it and this searcher must honour that. */
  lua_getfield(L, lua_upvalueindex(2), "path");
  lua_call(L, 2, 2);

  if (lua_type(L, -2) != LUA_TSTRING) {
    /* Action purpose: not found. A searcher returns a string explaining why,
     * which require() concatenates into its own error. Returning the top value
     * returns searchpath's message and leaves the nil behind. */
    return 1;
  }

  const char *filename = lua_tostring(L, -2);
  if (luaL_loadfilex(L, filename, "t") != LUA_OK) {
    return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s",
                      name, filename, lua_tostring(L, -1));
  }
  /* Action purpose: require() passes a searcher's second return value to the
   * loader as its second argument, and the stock searcher passes the filename
   * there, so a module reading `...` sees what it would normally see. */
  lua_pushstring(L, filename);
  return 2;
}

static void script_lua_sandbox(lua_State *L) {
  /* Action purpose: os.exit terminates SOFI, skipping cleanup(), the Wayland
   * teardown and the pid file. os.setlocale is process-global and would change
   * strtod and pango for every other part of sofi. */
  lua_getglobal(L, "os");
  lua_pushnil(L);
  lua_setfield(L, -2, "exit");
  lua_pushnil(L);
  lua_setfield(L, -2, "setlocale");
  lua_pop(L, 1);

  /* Action purpose: debug.sethook clears the watchdog -- both go through the
   * same hook slot, so a mode that uses it for its own tracing disarms the
   * deadline as a side effect, with no malice required. The rest of the library
   * reaches into sofi's own C closures, upvalues and stack slots. traceback is
   * kept because it only formats.
   *
   * The table is emptied IN PLACE rather than replaced with a smaller one.
   * Replacing the global leaves package.loaded.debug pointing at the original,
   * so require("debug") hands the whole library straight back -- which it did,
   * sethook and setupvalue included, when this was written the other way.
   * Setting an existing field to nil during a traversal is permitted; adding
   * one is not, and nothing here adds one. The key is only stringified once it
   * is known to be a string, because converting a number key in place during a
   * traversal changes its type and is undefined. */
  lua_getglobal(L, "debug");
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    lua_pop(L, 1);
    if (!(lua_type(L, -1) == LUA_TSTRING &&
          g_strcmp0(lua_tostring(L, -1), "traceback") == 0)) {
      lua_pushvalue(L, -1);
      lua_pushnil(L);
      lua_rawset(L, -4);
    }
  }
  lua_pop(L, 1);

  /* Action purpose: Lua 5.4 does not verify bytecode, so load() on a malformed
   * binary chunk crashes SOFI's virtual machine. A mode has no need to compile
   * strings at run time; its own file is already a chunk. */
  lua_pushnil(L);
  lua_setglobal(L, "load");
  lua_pushnil(L);
  lua_setglobal(L, "loadfile");
  lua_pushnil(L);
  lua_setglobal(L, "dofile");

  /* Action purpose: a shared library loaded through require() would be native
   * code in sofi's address space with no ABI check -- the one thing a spawned
   * script mode structurally cannot do to us. Nilling package.loadlib and
   * clearing package.cpath is NOT enough: require's C searcher calls liblua's
   * own loader rather than the Lua-visible package.loadlib, and package is a
   * plain mutable table, so a mode could put cpath back. Removing searchers 3
   * and 4 removes the only handles to that loader. */
  lua_getglobal(L, "package");
  lua_pushnil(L);
  lua_setfield(L, -2, "loadlib");
  lua_pushstring(L, "");
  lua_setfield(L, -2, "cpath");
  lua_getfield(L, -1, "searchers");
  lua_pushnil(L);
  lua_rawseti(L, -2, 3);
  lua_pushnil(L);
  lua_rawseti(L, -2, 4);

  /* Action purpose: searcher 2 is the one that finds a .lua file, and it loads
   * it in "text or binary" mode. Replace it with the text-only equivalent
   * above, closing the last route by which a data file could hand this process
   * unverified bytecode. Searcher 1 is package.preload, which only ever returns
   * a function a mode put there itself, so it needs no replacement. */
  lua_getfield(L, -2, "searchpath");
  lua_pushvalue(L, -3);
  lua_pushcclosure(L, script_lua_searcher, 2);
  lua_rawseti(L, -2, 2);

  lua_pop(L, 2);
}

/** Action purpose: the mode's own directory goes on package.path so a mode that
 * outgrows one file can require its parts. Prepended rather than replacing the
 * default, so a mode can still use a library the user installed system-wide. */
static void script_lua_add_path(lua_State *L, const char *path) {
  char *dir = g_path_get_dirname(path);
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");
  char *merged = g_strdup_printf("%s/?.lua;%s/?/init.lua;%s", dir, dir,
                                 lua_tostring(L, -1));
  lua_pop(L, 1);
  lua_pushstring(L, merged);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);
  g_free(merged);
  g_free(dir);
}

static void script_lua_install_table(lua_State *L, Mode *sw) {
  lua_newtable(L);
  lua_pushcfunction(L, script_lua_exec);
  lua_setfield(L, -2, "exec");
  lua_pushcfunction(L, script_lua_log);
  lua_setfield(L, -2, "log");
  lua_pushcfunction(L, script_lua_markup_escape);
  lua_setfield(L, -2, "markup_escape");
  lua_pushcfunction(L, script_lua_shell_quote);
  lua_setfield(L, -2, "shell_quote");
  lua_pushstring(L, sw->name != NULL ? sw->name : "");
  lua_setfield(L, -2, "name");
  char *dir = g_build_filename(g_get_user_config_dir(), "sofi", NULL);
  lua_pushstring(L, dir);
  lua_setfield(L, -2, "config_dir");
  g_free(dir);
  lua_setglobal(L, "sofi");
}

/** Function purpose: decide whether this mode is read by the interpreter or
 * spawned as a program. A Lua mode is a lone `.lua` file that the spawn path
 * cannot find anything executable at.
 *
 * The test has to resolve EXACTLY what the spawn path resolves, or a working
 * configuration changes meaning. execute_executor does not treat `ed` as a
 * path: it runs g_shell_parse_argv(ed, ...) and spawns argv[0] with
 * G_SPAWN_SEARCH_PATH. So three things matter, and testing the raw `ed` string
 * as a path gets all three wrong:
 *
 *   - A command line with ARGUMENTS is a spawn whatever it ends in.
 *     "bm:/usr/bin/lua5.4 /home/u/bookmarks.lua" ends in .lua and works today,
 *     and it is exactly how someone already running a Lua mode through the
 *     interpreter would have written it.
 *   - A BARE NAME is resolved on $PATH, not against the working directory.
 *     "notes:notes.lua" with an executable notes.lua on $PATH is the form the
 *     manual documents first. Testing it as a relative path would also make
 *     the answer depend on where sofi was launched from.
 *   - A path may be SHELL-ESCAPED. g_shell_parse_argv unescapes
 *     "/home/u/my\ notes/x.lua" before the spawn ever sees it.
 *
 * g_find_program_in_path performs the same resolution G_SPAWN_SEARCH_PATH
 * does -- an absolute or slash-bearing name is accepted only if it is an
 * executable non-directory, and a bare name is searched on $PATH and nowhere
 * else. So NULL from it means the spawn path has nothing to execute, which is
 * the exact complement of "this works as a script mode today". That is what
 * makes the overlap provably empty rather than merely unlikely.
 */
static gboolean script_is_lua_mode(const char *ed, char **out_path) {
  if (out_path != NULL) {
    *out_path = NULL;
  }
  if (ed == NULL) {
    return FALSE;
  }
  int argc = 0;
  char **argv = NULL;
  if (!g_shell_parse_argv(ed, &argc, &argv, NULL)) {
    /* Action purpose: a command line that will not parse is left to the spawn
     * path, which reports it in the terms the user wrote it in. */
    g_strfreev(argv);
    return FALSE;
  }
  gboolean is_lua = FALSE;
  if (argc == 1 && g_str_has_suffix(argv[0], ".lua")) {
    char *found = g_find_program_in_path(argv[0]);
    is_lua = found == NULL;
    g_free(found);
  }
  /* Action purpose: the UNESCAPED argv[0] is handed back, because the reader
   * has to open the same file this decision was made about. Deciding on the
   * unescaped path and then opening the raw one would recognise
   * "x:~/my\ notes/x.lua" as a Lua mode and then fail to find it. */
  if (is_lua && out_path != NULL) {
    *out_path = g_strdup(argv[0]);
  }
  g_strfreev(argv);
  return is_lua;
}

/** Function purpose: the error as a one-row list. `permanent` keeps the row
 * visible while the user types; `nonselectable` is why Return over it reloads
 * instead of dispatching. Built fresh on every call rather than cached,
 * because the delete and hot-key branches of script_mode_result do not test
 * nonselectable -- they reach the producer, and returning NULL there would
 * close the menu on Shift+Delete instead of redisplaying the error. */
static DmenuScriptEntry *script_lua_quarantine(ScriptModePrivateData *pd,
                                               unsigned int *length) {
  DmenuScriptEntry *retv = g_malloc0(2 * sizeof(DmenuScriptEntry));
  const char *msg = pd->message != NULL ? pd->message : "mode failed to load";

  /* Action purpose: one row is one line. A Lua error carries its traceback
   * after the first newline, and the whole of it has already gone to standard
   * error, so the row keeps the "file:line: message" part that identifies the
   * typo and drops the rest. */
  const char *nl = strchr(msg, '\n');
  retv[0].entry =
      nl != NULL ? g_strndup(msg, (gsize)(nl - msg)) : g_strdup(msg);

  /* Action purpose: a mode that set markup-rows before it failed leaves
   * do_markup on, and a Lua traceback contains '<' -- "in function </x.lua:1>"
   * -- so rendering the error as markup would fail to parse and show nothing,
   * which is the one thing an error row must not do. Escaping the DISPLAY
   * rather than clearing the flag keeps the mode's own markup setting intact
   * for its next successful list. */
  if (pd->do_markup) {
    retv[0].display = g_markup_escape_text(retv[0].entry, -1);
  }

  retv[0].nonselectable = TRUE;
  retv[0].permanent = TRUE;
  *length = 1;
  return retv;
}

/** Action purpose: a failure the mode cannot recover from -- the chunk will not
 * load, it returned the wrong shape, or the interpreter could not be created.
 * The message is reported in the menu rather than in a dialog so that _init can
 * keep returning TRUE: a mode that fails _init has the generic "Failed to
 * initialize the mode" dialog stacked over its own, and one broken member would
 * abort a whole combi before any menu existed. Takes ownership of `msg`. */
static void script_lua_quarantine_set(ScriptModePrivateData *pd, char *msg) {
  g_warning("%s", msg);
  g_free(pd->message);
  pd->message = msg;
  pd->quarantined = TRUE;
  if (pd->lua != NULL) {
    lua_close(pd->lua);
    pd->lua = NULL;
  }
}

/** Action purpose: create the interpreter and run the mode's chunk once. On
 * return either pd->lua is non-NULL with the mode's table at stack slot 1, or
 * the mode is quarantined and pd->lua is NULL. Nothing in between. `path` is
 * the unescaped file the classification was made about, not sw->ed. */
static void script_lua_open(Mode *sw, ScriptModePrivateData *pd,
                            const char *path) {
  lua_State *L = luaL_newstate();
  if (L == NULL) {
    script_lua_quarantine_set(
        pd, g_strdup_printf("Could not create a Lua interpreter for mode "
                            "\"%s\": out of memory.",
                            sw->name));
    return;
  }
  pd->lua = L;
  *(ScriptModePrivateData **)lua_getextraspace(L) = pd;

  luaL_openlibs(L);
  script_lua_sandbox(L);
  script_lua_install_table(L, sw);
  script_lua_add_path(L, path);

  /* Action purpose: text mode only. Nobody hand-writes a precompiled chunk, and
   * Lua 5.4 does not verify bytecode, so accepting a binary chunk here would be
   * accepting arbitrary control of sofi's virtual machine from a data file. */
  if (luaL_loadfilex(L, path, "t") != LUA_OK) {
    char *msg = g_strdup_printf("Lua mode \"%s\" would not load: %s", sw->name,
                                lua_tostring(L, -1));
    script_lua_quarantine_set(pd, msg);
    return;
  }

  /* Action purpose: the chunk runs under the same deadline as every later call,
   * because work at a mode's top level is work on the keyboard path too. */
  if (script_lua_call(L, pd, 0) != LUA_OK) {
    char *msg = g_strdup_printf("Lua mode \"%s\" failed while loading: %s",
                                sw->name, lua_tostring(L, -1));
    script_lua_quarantine_set(pd, msg);
    return;
  }

  if (!lua_istable(L, -1)) {
    char *msg = g_strdup_printf(
        "Lua mode \"%s\" must return a table with an entries function, "
        "but returned %s.",
        sw->name, lua_typename(L, lua_type(L, -1)));
    script_lua_quarantine_set(pd, msg);
    return;
  }
  if (script_lua_rawfield(L, lua_gettop(L), "entries") != LUA_TFUNCTION) {
    char *msg = g_strdup_printf(
        "Lua mode \"%s\" returned a table whose `entries` is %s, not a "
        "function.",
        sw->name, lua_typename(L, lua_type(L, -1)));
    script_lua_quarantine_set(pd, msg);
    return;
  }
  lua_pop(L, 1);

  /* Action purpose: the state's own stack is the holder, because there is
   * exactly one mode table and nothing else touches this state between calls.
   * No registry key and no reference count, which would each be a second way
   * to name the same one thing. */
  g_assert(lua_gettop(L) == 1);
}

/** Action purpose: read the array part of `list` into a freshly allocated entry
 * array. g_malloc0 of n + 1 preserves the zeroed trailing sentinel the spawn
 * path maintains with memset, and means every field a row omits is zero --
 * including icon_fallback_index, which the spawn path leaves indeterminate for
 * entry 0. `list` must be an absolute index. */
static DmenuScriptEntry *script_lua_rows(lua_State *L, int list, Mode *sw,
                                         unsigned int *length) {
  lua_Unsigned n = lua_rawlen(L, list);
  DmenuScriptEntry *retv = g_malloc0((n + 1) * sizeof(DmenuScriptEntry));

  for (lua_Unsigned i = 1; i <= n; i++) {
    lua_rawgeti(L, list, (lua_Integer)i);
    int row = lua_gettop(L);
    DmenuScriptEntry *e = &retv[*length];

    if (lua_type(L, row) == LUA_TSTRING) {
      e->entry = g_strdup(lua_tostring(L, row));
      (*length)++;
      lua_settop(L, row - 1);
      continue;
    }
    if (!lua_istable(L, row)) {
      g_warning("Lua mode \"%s\": row %lu is %s, not a string or a table; "
                "skipping it.",
                sw->name, (unsigned long)i, lua_typename(L, lua_type(L, row)));
      lua_settop(L, row - 1);
      continue;
    }

    script_lua_rawfield(L, row, "entry");
    e->entry = script_lua_take(L, -1);
    lua_pop(L, 1);
    /* Action purpose: a row with no entry has nothing to match and nothing to
     * hand back, so it is skipped with its index named and the rest of the list
     * is still shown. A typo in row 400 of 500 must not blank the menu. */
    if (e->entry == NULL) {
      g_warning("Lua mode \"%s\": row %lu has no string `entry`; skipping it.",
                sw->name, (unsigned long)i);
      lua_settop(L, row - 1);
      continue;
    }

    script_lua_rawfield(L, row, "display");
    e->display = script_lua_take(L, -1);
    lua_pop(L, 1);
    script_lua_rawfield(L, row, "meta");
    e->meta = script_lua_take(L, -1);
    lua_pop(L, 1);
    script_lua_rawfield(L, row, "info");
    e->info = script_lua_take(L, -1);
    lua_pop(L, 1);

    script_lua_rawfield(L, row, "urgent");
    e->urgent = lua_toboolean(L, -1);
    lua_pop(L, 1);
    script_lua_rawfield(L, row, "active");
    e->active = lua_toboolean(L, -1);
    lua_pop(L, 1);
    script_lua_rawfield(L, row, "nonselectable");
    e->nonselectable = lua_toboolean(L, -1);
    lua_pop(L, 1);
    script_lua_rawfield(L, row, "permanent");
    e->permanent = lua_toboolean(L, -1);
    lua_pop(L, 1);

    /* Action purpose: an icon is a comma string, which is byte-for-byte what
     * dmenuscript_parse_entry_extras does with the line protocol's icon field,
     * or an array of names tried in order. Either way the result is a strv that
     * script_mode_free_entry_list already frees with g_strfreev. Lua names
     * icons; C resolves them, so a mode never sees a cairo surface. */
    script_lua_rawfield(L, row, "icon");
    if (lua_type(L, -1) == LUA_TSTRING) {
      e->icon_name = g_strsplit(lua_tostring(L, -1), ",", -1);
    } else if (lua_istable(L, -1)) {
      int icons = lua_gettop(L);
      lua_Unsigned ni = lua_rawlen(L, icons);
      GPtrArray *names = g_ptr_array_new();
      for (lua_Unsigned j = 1; j <= ni; j++) {
        lua_rawgeti(L, icons, (lua_Integer)j);
        char *name = script_lua_take(L, -1);
        if (name != NULL) {
          g_ptr_array_add(names, name);
        }
        lua_pop(L, 1);
      }
      g_ptr_array_add(names, NULL);
      e->icon_name = (char **)g_ptr_array_free(names, FALSE);
    }
    lua_pop(L, 1);

    (*length)++;
    lua_settop(L, row - 1);
  }
  return retv;
}

/** Action purpose: the string keys of the returned table are the header
 * options, and they are rendered back into the protocol's own "key\x1fvalue"
 * shape so that parse_header_entry -- the same function the spawn path uses --
 * applies all of them. That is what keeps fourteen option semantics, the
 * display_name borrow and the theme parse in one place instead of two that can
 * drift. `delim` is deliberately absent: it names the byte that separates
 * entries in a stream, and a Lua mode has no stream. */
static void script_lua_options(lua_State *L, int list, Mode *sw) {
  static const char *const options[] = {"message",      "prompt",
                                        "markup-rows",  "urgent",
                                        "active",       "no-custom",
                                        "use-hot-keys", "keep-selection",
                                        "keep-filter",  "new-selection",
                                        "data",         "theme",
                                        "switch-mode",  NULL};

  for (int i = 0; options[i] != NULL; i++) {
    if (script_lua_rawfield(L, list, options[i]) == LUA_TNIL) {
      lua_pop(L, 1);
      continue;
    }
    char *value = script_lua_option_value(L, -1);
    lua_pop(L, 1);
    if (value == NULL) {
      g_warning("Lua mode \"%s\": header option \"%s\" must be a string, a "
                "number or a boolean; ignoring it.",
                sw->name, options[i]);
      continue;
    }
    /* Action purpose: the length passed MUST include the terminating NUL. The
     * guard inside parse_header_entry is `(length_key + 1) < length`, and the
     * spawn path's caller passes a count with the NUL inside it, so a shorter
     * count would silently drop `message = ""` -- which is how the protocol
     * CLEARS a message. */
    char *line = g_strdup_printf("%s\x1f%s", options[i], value);
    parse_header_entry(sw, line, (ssize_t)(strlen(line) + 1));
    g_free(line);
    g_free(value);
  }
}

/** Action purpose: the Lua half of the producer. Mirrors execute_executor step
 * for step: reset the three sticky fields, assemble the interaction's state,
 * cross into the mode exactly once, apply the header options, harvest the
 * rows. */
static DmenuScriptEntry *script_lua_executor(Mode *sw, char *arg,
                                             unsigned int *length, int value,
                                             DmenuScriptEntry *entry,
                                             char *input) {
  ScriptModePrivateData *pd = (ScriptModePrivateData *)sw->private_data;
  lua_State *L = pd->lua;
  *length = 0;

  /* Action purpose: the same three resets execute_executor performs before
   * anything else. script_mode_result snapshots keep_filter and keep_selection
   * BEFORE calling the producer, so a producer that skips these makes a mode
   * that once set keep-selection keep it for the rest of the run. */
  pd->new_selection = -1;
  pd->keep_selection = 0;
  pd->keep_filter = 0;

  g_assert(lua_gettop(L) == 1);

  script_lua_rawfield(L, 1, "entries");

  /* Action purpose: the five pieces of state the line protocol puts in the
   * environment, plus the one it passes as argv[argc]. A field is omitted
   * rather than set to nil, because state.x reads nil either way and the mode
   * then has one shape to handle. SOFI_OUTSIDE has no counterpart: it exists so
   * a CHILD can refuse to launch sofi inside sofi, and a Lua mode has no
   * child. */
  lua_newtable(L);
  lua_pushinteger(L, value);
  lua_setfield(L, -2, "retv");
  if (arg != NULL) {
    lua_pushstring(L, arg);
    lua_setfield(L, -2, "entry");
  }
  if (entry != NULL && entry->info != NULL) {
    lua_pushstring(L, entry->info);
    lua_setfield(L, -2, "info");
  }
  if (pd->data != NULL) {
    lua_pushstring(L, pd->data);
    lua_setfield(L, -2, "data");
  }
  if (input != NULL) {
    lua_pushstring(L, input);
    lua_setfield(L, -2, "input");
  }

  if (script_lua_call(L, pd, 1) != LUA_OK) {
    /* Action purpose: a raised error or a tripped deadline is reported and the
     * interaction answered with the error row, but the mode is NOT
     * quarantined: one failing branch should not permanently break a mode
     * whose other branches work. */
    char *msg =
        g_strdup_printf("Lua mode \"%s\": %s", sw->name, lua_tostring(L, -1));
    g_warning("%s", msg);
    g_free(pd->message);
    pd->message = msg;
    pd->message_is_error = TRUE;
    lua_settop(L, 1);
    return script_lua_quarantine(pd, length);
  }

  /* Action purpose: the mode came back, so the error it reported last time is
   * over. This happens BEFORE script_lua_options so a mode that sets `message`
   * itself still wins, and it clears only sofi's own error text -- an ordinary
   * sticky message the mode set stays sticky. */
  if (pd->message_is_error) {
    g_free(pd->message);
    pd->message = NULL;
    pd->message_is_error = FALSE;
  }

  if (lua_isnil(L, -1)) {
    lua_settop(L, 1);
    return NULL;
  }
  if (!lua_istable(L, -1)) {
    g_warning("Lua mode \"%s\": entries returned %s, not a table or nil.",
              sw->name, lua_typename(L, lua_type(L, -1)));
    lua_settop(L, 1);
    return NULL;
  }

  int list = lua_gettop(L);
  script_lua_options(L, list, sw);
  DmenuScriptEntry *retv = script_lua_rows(L, list, sw, length);
  lua_settop(L, 1);

  /* Action purpose: an empty list and a nil must mean the same thing, because a
   * script that prints nothing produces NULL here and sofi closes. Freeing the
   * sentinel-only allocation and returning NULL is what makes the documented
   * contract and the allocation rule agree. */
  if (*length == 0) {
    g_free(retv);
    return NULL;
  }
  return retv;
}

/** Function purpose: pick the producer. This is the only existing control flow
 * this feature changes: the one call in script_mode_init and the five in
 * script_mode_result name this instead of execute_executor, and its default
 * branch is that call with an identical argument list. */
static DmenuScriptEntry *script_run_executor(Mode *sw, char *arg,
                                             unsigned int *length, int value,
                                             DmenuScriptEntry *entry,
                                             char *input) {
  ScriptModePrivateData *pd = (ScriptModePrivateData *)sw->private_data;
  if (pd->quarantined) {
    return script_lua_quarantine(pd, length);
  }
  if (pd->lua != NULL) {
    return script_lua_executor(sw, arg, length, value, entry, input);
  }
  return execute_executor(sw, arg, length, value, entry, input);
}

static void script_switcher_free(Mode *sw) {
  if (sw == NULL) {
    return;
  }
  g_free(sw->name);
  g_free(sw->ed);
  g_free(sw);
}

static int script_mode_init(Mode *sw) {
  if (sw->private_data == NULL) {
    ScriptModePrivateData *pd = g_malloc0(sizeof(*pd));
    pd->delim = '\n';
    sw->private_data = (void *)pd;
    char *lua_path = NULL;
    if (script_is_lua_mode((const char *)sw->ed, &lua_path)) {
      script_lua_open(sw, pd, lua_path);
    }
    g_free(lua_path);
    pd->cmd_list =
        script_run_executor(sw, NULL, &(pd->cmd_list_length), 0, NULL, NULL);
    pd->switch_mode = -1;
  }
  return TRUE;
}
static unsigned int script_mode_get_num_entries(const Mode *sw) {
  const ScriptModePrivateData *rmpd =
      (const ScriptModePrivateData *)sw->private_data;
  return rmpd->cmd_list_length;
}

static void script_mode_reset_highlight(Mode *sw) {
  ScriptModePrivateData *rmpd = (ScriptModePrivateData *)sw->private_data;

  rmpd->num_urgent_list = 0;
  g_free(rmpd->urgent_list);
  rmpd->urgent_list = NULL;
  rmpd->num_active_list = 0;
  g_free(rmpd->active_list);
  rmpd->active_list = NULL;
}

static void script_mode_free_entry_list(DmenuScriptEntry *list,
                                        unsigned int length) {
  for (unsigned int i = 0; i < length; i++) {
    g_free(list[i].entry);
    g_strfreev(list[i].icon_name);
    g_free(list[i].display);
    g_free(list[i].meta);
    g_free(list[i].info);
  }
  g_free(list);
}
static ModeMode script_mode_result(Mode *sw, int mretv, char **input,
                                   unsigned int selected_line) {
  ScriptModePrivateData *rmpd = (ScriptModePrivateData *)sw->private_data;
  ModeMode retv = MODE_EXIT;
  DmenuScriptEntry *new_list = NULL;
  unsigned int new_length = 0;
  // store them as they might be different on next executor and reset.
  gboolean keep_filter = rmpd->keep_filter;
  gboolean keep_selection = rmpd->keep_selection;

  if ((mretv & MENU_CUSTOM_COMMAND)) {
    if (rmpd->use_hot_keys) {
      script_mode_reset_highlight(sw);
      if (selected_line != UINT32_MAX) {
        new_list =
            script_run_executor(sw, rmpd->cmd_list[selected_line].entry,
                                &new_length, 10 + (mretv & MENU_LOWER_MASK),
                                &(rmpd->cmd_list[selected_line]), *input);
      } else {
        if (rmpd->no_custom == FALSE) {
          new_list =
              script_run_executor(sw, *input, &new_length,
                                  10 + (mretv & MENU_LOWER_MASK), NULL, *input);
        } else {
          return RELOAD_DIALOG;
        }
      }
    } else {
      retv = (mretv & MENU_LOWER_MASK);
      return retv;
    }
  } else if ((mretv & MENU_ENTRY_DELETE) && selected_line != UINT32_MAX) {
    script_mode_reset_highlight(sw);
    new_list = script_run_executor(sw, rmpd->cmd_list[selected_line].entry,
                                   &new_length, 3,
                                   &(rmpd->cmd_list[selected_line]), *input);
  } else if ((mretv & MENU_OK) && rmpd->cmd_list[selected_line].entry != NULL) {
    if (rmpd->cmd_list[selected_line].nonselectable) {
      return RELOAD_DIALOG;
    }
    script_mode_reset_highlight(sw);
    new_list = script_run_executor(sw, rmpd->cmd_list[selected_line].entry,
                                   &new_length, 1,
                                   &(rmpd->cmd_list[selected_line]), *input);
  } else if ((mretv & MENU_CUSTOM_INPUT) && *input != NULL) {
    if (rmpd->no_custom == FALSE) {
      script_mode_reset_highlight(sw);
      new_list = script_run_executor(sw, *input, &new_length, 2, NULL, *input);
    } else {
      return RELOAD_DIALOG;
    }
  }

  if (rmpd->switch_mode >= 0) {
    script_mode_free_entry_list(new_list, new_length);
    retv = rmpd->switch_mode;
    free(*input);
    *input = NULL;
    rmpd->switch_mode = -1;
    return retv;
  }

  // If a new list was generated, use that an loop around.
  if (new_list != NULL) {
    script_mode_free_entry_list(rmpd->cmd_list, rmpd->cmd_list_length);
    rmpd->cmd_list = new_list;
    rmpd->cmd_list_length = new_length;
    if (keep_selection) {
      if (rmpd->new_selection >= 0 &&
          rmpd->new_selection < rmpd->cmd_list_length) {
        sofi_view_set_selected_line(sofi_view_get_active(),
                                    rmpd->new_selection);
      } else {
        sofi_view_set_selected_line(sofi_view_get_active(), selected_line);
      }
    } else {
      sofi_view_set_selected_line(sofi_view_get_active(), 0);
    }
    if (keep_filter == FALSE) {
      g_free(*input);
      *input = NULL;
    }
    retv = RELOAD_DIALOG;
  }
  return retv;
}

static void script_mode_destroy(Mode *sw) {
  ScriptModePrivateData *rmpd = (ScriptModePrivateData *)sw->private_data;
  if (rmpd != NULL) {
    script_mode_free_entry_list(rmpd->cmd_list, rmpd->cmd_list_length);
    g_free(rmpd->message);
    g_free(rmpd->prompt);
    g_free(rmpd->data);
    g_free(rmpd->urgent_list);
    g_free(rmpd->active_list);
    if (rmpd->lua != NULL) {
      lua_close(rmpd->lua);
      rmpd->lua = NULL;
    }
    g_free(rmpd);
    sw->private_data = NULL;
  }
}
static inline unsigned int get_index(unsigned int length, int index) {
  if (index >= 0) {
    return index;
  }
  if (((unsigned int)-index) <= length) {
    return length + index;
  }
  // Out of range.
  return UINT_MAX;
}
static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                G_GNUC_UNUSED int *state,
                                G_GNUC_UNUSED GList **list, int get_entry) {
  ScriptModePrivateData *pd = sw->private_data;
  for (unsigned int i = 0; i < pd->num_active_list; i++) {
    unsigned int start =
        get_index(pd->cmd_list_length, pd->active_list[i].start);
    unsigned int stop = get_index(pd->cmd_list_length, pd->active_list[i].stop);
    if (selected_line >= start && selected_line <= stop) {
      *state |= ACTIVE;
    }
  }
  for (unsigned int i = 0; i < pd->num_urgent_list; i++) {
    unsigned int start =
        get_index(pd->cmd_list_length, pd->urgent_list[i].start);
    unsigned int stop = get_index(pd->cmd_list_length, pd->urgent_list[i].stop);
    if (selected_line >= start && selected_line <= stop) {
      *state |= URGENT;
    }
  }
  if (pd->cmd_list[selected_line].urgent) {
    *state |= URGENT;
  }
  if (pd->cmd_list[selected_line].active) {
    *state |= ACTIVE;
  }
  if (pd->do_markup) {
    *state |= MARKUP;
  }
  if (pd->cmd_list[selected_line].display) {
    return get_entry ? g_strdup(pd->cmd_list[selected_line].display) : NULL;
  } else {
    return get_entry ? g_strdup(pd->cmd_list[selected_line].entry) : NULL;
  }
}

static int script_token_match(const Mode *sw, sofi_int_matcher **tokens,
                              unsigned int index) {
  ScriptModePrivateData *rmpd = sw->private_data;
  /** Strip out the markup when matching. */
  char *esc = NULL;

  if (rmpd->cmd_list[index].permanent == TRUE) {
    // Always match
    return 1;
  }

  if (rmpd->do_markup) {
    pango_parse_markup(rmpd->cmd_list[index].entry, -1, 0, NULL, &esc, NULL,
                       NULL);
  } else {
    esc = rmpd->cmd_list[index].entry;
  }
  if (esc) {
    int match = 1;
    if (tokens) {
      for (int j = 0; match && tokens[j] != NULL; j++) {
        sofi_int_matcher *ftokens[2] = {tokens[j], NULL};
        int test = 0;
        test = helper_token_match(ftokens, esc);
        if (test == tokens[j]->invert && rmpd->cmd_list[index].meta) {
          test = helper_token_match(ftokens, rmpd->cmd_list[index].meta);
        }

        if (test == 0) {
          match = 0;
        }
      }
    }
    if (rmpd->do_markup) {
      g_free(esc);
    }
    return match;
  }
  return FALSE;
}
static char *script_get_message(const Mode *sw) {
  ScriptModePrivateData *pd = sw->private_data;
  return g_strdup(pd->message);
}
static cairo_surface_t *script_get_icon(const Mode *sw,
                                        unsigned int selected_line,
                                        unsigned int height) {
  ScriptModePrivateData *pd =
      (ScriptModePrivateData *)mode_get_private_data(sw);

  const guint scale = display_scale();

  g_return_val_if_fail(pd->cmd_list != NULL, NULL);
  DmenuScriptEntry *dr = &(pd->cmd_list[selected_line]);
  if (dr->icon_name == NULL) {
    return NULL;
  }

  if (dr->icon_fetch_uid > 0) {
    cairo_surface_t *surface = NULL;
    gboolean query_done =
        sofi_icon_fetcher_get_ex(dr->icon_fetch_uid, &surface);

    if (surface != NULL) {
      return surface;
    } else if (query_done) {
      dr->icon_fallback_index++;
      dr->icon_fetch_uid = 0;
    } else {
      return NULL;
    }
  }

  char *current_icon = NULL;
  if (dr->icon_name && dr->icon_fallback_index >= 0) {
    int icon_count = g_strv_length(dr->icon_name);
    if (dr->icon_fallback_index < icon_count) {
      current_icon = dr->icon_name[dr->icon_fallback_index];
    }
  }
  if (current_icon) {
    dr->icon_fetch_uid = sofi_icon_fetcher_query(current_icon, height);
    dr->icon_fetch_size = height;
    dr->icon_fetch_scale = scale;

  } else {
    dr->icon_fetch_uid = 0;
  }

  return NULL;
}

#include "mode-private.h"

/** Structure that holds a user script
 * found in $config/sofi/scripts/
 */
typedef struct ScriptUser {
  /** name of the script */
  char *name;
  /** path to the script. */
  char *path;
} ScriptUser;

/** list of user_scripts. */
ScriptUser *user_scripts = NULL;
/** number of user scripts collected */
size_t num_scripts = 0;

void script_mode_cleanup(void) {
  for (size_t i = 0; i < num_scripts; i++) {
    g_free(user_scripts[i].name);
    g_free(user_scripts[i].path);
  }
  g_free(user_scripts);
}
void script_mode_gather_user_scripts(void) {
  const char *cpath = g_get_user_config_dir();
  char *script_dir = g_build_filename(cpath, "sofi", "scripts", NULL);
  if (g_file_test(script_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) ==
      FALSE) {
    g_free(script_dir);
    return;
  }
  GDir *sd = g_dir_open(script_dir, 0, NULL);
  if (sd) {
    const char *file = NULL;
    while ((file = g_dir_read_name(sd)) != NULL) {
      char *sp = g_build_filename(cpath, "sofi", "scripts", file, NULL);
      user_scripts =
          g_realloc(user_scripts, sizeof(ScriptUser) * (num_scripts + 1));
      user_scripts[num_scripts].path = sp;
      user_scripts[num_scripts].name = g_strdup(file);
      char *dot = strrchr(user_scripts[num_scripts].name, '.');
      if (dot) {
        *dot = '\0';
      }
      num_scripts++;
    }
    g_dir_close(sd);
  }

  g_free(script_dir);
}

static int script_mode_has_user_script(char const *const user) {

  for (size_t i = 0; i < num_scripts; i++) {
    if (g_strcmp0(user_scripts[i].name, user) == 0) {
      return i;
    }
  }
  return -1;
}

Mode *script_mode_parse_setup(const char *str) {
  int ui = 0;
  if ((ui = script_mode_has_user_script(str)) >= 0) {
    Mode *sw = g_malloc0(sizeof(*sw));
    sw->name = g_strdup(user_scripts[ui].name);
    sw->ed = g_strdup(user_scripts[ui].path);
    sw->free = script_switcher_free;
    sw->_init = script_mode_init;
    sw->_get_num_entries = script_mode_get_num_entries;
    sw->_result = script_mode_result;
    sw->_destroy = script_mode_destroy;
    sw->_token_match = script_token_match;
    sw->_get_message = script_get_message;
    sw->_get_icon = script_get_icon;
    sw->_get_completion = NULL, sw->_preprocess_input = NULL,
    sw->_get_display_value = _get_display_value;
    sw->type = MODE_TYPE_SWITCHER;
    return sw;
  }
  Mode *sw = g_malloc0(sizeof(*sw));
  unsigned int index = 0;
  const char *const sep = ":";
  char **tokens = g_strsplit(str, sep, 2);
  if (tokens) {
    index = g_strv_length(tokens);
    sw->name = g_strdup(tokens[0]);
    sw->ed = (void *)sofi_expand_path(tokens[1]);
    g_strfreev(tokens);
  }
  if (index == 2) {
    sw->free = script_switcher_free;
    sw->_init = script_mode_init;
    sw->_get_num_entries = script_mode_get_num_entries;
    sw->_result = script_mode_result;
    sw->_destroy = script_mode_destroy;
    sw->_token_match = script_token_match;
    sw->_get_message = script_get_message;
    sw->_get_icon = script_get_icon;
    sw->_get_completion = NULL, sw->_preprocess_input = NULL,
    sw->_get_display_value = _get_display_value;
    sw->type = MODE_TYPE_SWITCHER;

    return sw;
  }
  fprintf(
      stderr,
      "The script command '%s' has %u options, but needs 2: <name>:<script>.",
      str, index);
  script_switcher_free(sw);
  return NULL;
}

gboolean script_mode_is_valid(const char *token) {
  if (script_mode_has_user_script(token) >= 0) {
    return TRUE;
  }
  return strchr(token, ':') != NULL;
}

void script_user_list(gboolean is_term) {

  for (unsigned int i = 0; i < num_scripts; i++) {
    /* Action purpose: a reader who sees the tag knows the file is read by an
     * interpreter rather than spawned, which is also the answer to "why is my
     * .lua file being executed" -- it has the execute bit. */
    printf("        • %s%s%s (%s)%s\n", is_term ? (color_bold) : "",
           user_scripts[i].name, is_term ? color_reset : "",
           user_scripts[i].path,
           script_is_lua_mode(user_scripts[i].path, NULL) ? " [lua]" : "");
  }
}
