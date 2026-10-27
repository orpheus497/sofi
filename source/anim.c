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

/*
 * Script function and purpose: Easing curves, the frame-driven tween clock, and
 * the four animations sofi draws with them. Nothing here reads a clock of its
 * own: every entry point takes the timestamp, which is what lets the Wayland
 * backend drive motion from wl_callback.done and keeps the module testable
 * without a compositor.
 *
 * The easing, tween, clock and hover code is ported from saber's src/anim.c and
 * include/saber/anim.h (MIT, same author), renamed to sofi's conventions; see
 * include/anim.h for why it was ported rather than rewritten. saber's src/sni.c,
 * src/dbusmenu.c and src/appinfo.c are ported from sofi in the other direction
 * and record it the same way.
 */

/** The Sofi Animation log domain */
#define G_LOG_DOMAIN "Anim"

#include <config.h>
#include <math.h>
#include <stdint.h>

#include <glib.h>

#ifdef HAVE_LUA
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#endif

#include "anim.h"
#ifdef HAVE_LUA
#include "hikari-lua.h"
#endif
#include "sofi-types.h"

struct _SofiClock {
  GPtrArray *tweens; /* borrowed SofiTween * */
  int64_t now_ms;
  int64_t base_ms; /* accumulated wrap offset for sofi_clock_stamp */
  uint32_t last_raw;
  gboolean stamped;
  gboolean busy;
  gboolean dormant; /* nothing was running at the last advance */
};

double sofi_ease(SofiEasing easing, double t) {
  if (t <= 0.0) {
    return 0.0;
  }

  if (t >= 1.0) {
    return 1.0;
  }

  switch (easing) {
  case SOFI_EASE_OUT_CUBIC: {
    double inverse = 1.0 - t;

    return 1.0 - inverse * inverse * inverse;
  }

  case SOFI_EASE_IN_OUT_CUBIC:
    if (t < 0.5) {
      return 4.0 * t * t * t;
    } else {
      double inverse = -2.0 * t + 2.0;

      return 1.0 - (inverse * inverse * inverse) / 2.0;
    }

  /* Action purpose: a decaying cosine rather than a true spring solve. It is
   * zero at t=0, overshoots once to 1.0670 at t=0.3925, and has decayed to
   * 0.99762 by t=1, where the clamp above lands it exactly -- which is all a
   * short throb needs and costs one exp and one cos. */
  case SOFI_EASE_SPRING:
    return 1.0 - exp(-6.0 * t) * cos(6.0 * t);

  case SOFI_EASE_LINEAR:
  default:
    return t;
  }
}

double sofi_anim_wiggle(double phase) { return sin(phase * 2.0 * G_PI); }

double sofi_anim_throb(double phase, double depth) {
  return 1.0 + depth * sin(phase * G_PI);
}

void sofi_tween_init(SofiTween *tween, double value) {
  tween->from = value;
  tween->to = value;
  tween->value = value;
  tween->start_ms = 0;
  tween->duration_ms = 0;
  tween->easing = SOFI_EASE_LINEAR;
  tween->running = FALSE;
  tween->repeat = FALSE;
}

static void tween_begin(SofiTween *tween, double from, double to,
                        int duration_ms, SofiEasing easing, int64_t now_ms,
                        gboolean repeat) {
  tween->from = from;
  tween->to = to;
  tween->value = from;
  tween->start_ms = now_ms;
  tween->duration_ms = duration_ms > 0 ? duration_ms : 0;
  tween->easing = easing;
  tween->repeat = repeat;
  tween->running = TRUE;

  /* Action purpose: a zero-length cycle cannot repeat. Without this a repeating
   * tween started with duration 0 -- which is what a compositor saying
   * `duration = 0` produces -- stays running for ever, because
   * sofi_tween_advance() answers `running = repeat` on the zero-duration path.
   * The clock is then permanently busy and its owner repaints for ever: exactly
   * the failure that once held a core for an hour (D-036). A repeat of nothing
   * lands on its target and stops. */
  if (tween->duration_ms == 0) {
    tween->value = to;
    tween->running = FALSE;
  }
}

void sofi_tween_start(SofiTween *tween, double from, double to,
                      int duration_ms, SofiEasing easing, int64_t now_ms) {
  tween_begin(tween, from, to, duration_ms, easing, now_ms, FALSE);
}

void sofi_tween_start_repeating(SofiTween *tween, double from, double to,
                                int duration_ms, SofiEasing easing,
                                int64_t now_ms) {
  tween_begin(tween, from, to, duration_ms, easing, now_ms, TRUE);
}

void sofi_tween_stop(SofiTween *tween) {
  if (!tween->running) {
    return;
  }

  tween->running = FALSE;
  tween->repeat = FALSE;
  tween->value = tween->to;
}

gboolean sofi_tween_advance(SofiTween *tween, int64_t now_ms) {
  if (!tween->running) {
    return FALSE;
  }

  /* Same reasoning as tween_begin(): a cycle of no length is not an animation,
   * so it settles rather than repeating for ever. */
  if (tween->duration_ms <= 0) {
    tween->value = tween->to;
    tween->running = FALSE;

    return FALSE;
  }

  double elapsed = (double)(now_ms - tween->start_ms);

  /* A frame stamp behind the start means the clock was restamped under us; hold
   * at the first value rather than running the curve backwards. */
  if (elapsed < 0.0) {
    elapsed = 0.0;
  }

  double t = elapsed / (double)tween->duration_ms;

  if (tween->repeat) {
    t = fmod(t, 1.0);
  } else if (t >= 1.0) {
    tween->value = tween->to;
    tween->running = FALSE;

    return FALSE;
  }

  tween->value =
      tween->from + (tween->to - tween->from) * sofi_ease(tween->easing, t);

  return TRUE;
}

void sofi_hover_init(SofiHover *hover) {
  sofi_tween_init(&hover->enter, 0.0);
  sofi_tween_init(&hover->leave, 0.0);
}

void sofi_hover_add(SofiClock *clock, SofiHover *hover) {
  sofi_clock_add(clock, &hover->enter);
  sofi_clock_add(clock, &hover->leave);
}

void sofi_hover_leave(SofiHover *hover, int duration_ms, int64_t now_ms) {
  sofi_tween_start(&hover->leave, hover->enter.value, 0.0, duration_ms,
                   SOFI_EASE_OUT_CUBIC, now_ms);
}

void sofi_hover_enter(SofiHover *hover, int duration_ms, int64_t now_ms) {
  sofi_tween_start(&hover->enter, 0.0, 1.0, duration_ms, SOFI_EASE_OUT_CUBIC,
                   now_ms);
}

void sofi_hover_rest(SofiHover *hover) {
  sofi_tween_stop(&hover->enter);
  hover->enter.value = 0.0;
}

void sofi_hover_clear(SofiHover *hover) {
  sofi_hover_rest(hover);
  sofi_tween_stop(&hover->leave);
  hover->leave.value = 0.0;
}

SofiClock *sofi_clock_create(void) {
  SofiClock *clock = g_new0(SofiClock, 1);

  clock->tweens = g_ptr_array_new();

  return clock;
}

void sofi_clock_destroy(SofiClock *clock) {
  if (clock == NULL) {
    return;
  }

  g_ptr_array_free(clock->tweens, TRUE);
  g_free(clock);
}

void sofi_clock_add(SofiClock *clock, SofiTween *tween) {
  guint at;

  if (g_ptr_array_find(clock->tweens, tween, &at)) {
    return;
  }

  g_ptr_array_add(clock->tweens, tween);
}

void sofi_clock_remove(SofiClock *clock, SofiTween *tween) {
  g_ptr_array_remove_fast(clock->tweens, tween);
}

int64_t sofi_clock_stamp(SofiClock *clock, uint32_t frame_ms) {
  /* Action purpose: the first frame stamp is rebased onto wherever the clock
   * already stood, so tweens started before any frame arrived do not see the
   * compositor's arbitrary epoch land on them as one enormous elapsed time and
   * finish instantly.
   *
   * A clock whose loop has stopped is re-anchored the same way, and for the same
   * reason. While nothing runs no frames are counted, so now_ms stands still,
   * but base_ms goes on mapping the compositor's stamp onto the epoch of the
   * first frame ever seen -- the time this returns keeps pace with the wall
   * clock. A tween started during that gap is anchored to the frozen now_ms by
   * sofi_clock_now(), so it would see the whole gap as elapsed on its very first
   * frame: finished before it drew once, and finished without ever returning
   * TRUE, so its owner never damages and the settled value is never painted.
   * Discarding the gap is what makes sofi_clock_now() a valid anchor again, and
   * costs nothing: no tween can observe time in which none of them ran. */
  if (!clock->stamped || clock->dormant) {
    clock->stamped = TRUE;
    clock->dormant = FALSE;
    clock->base_ms = clock->now_ms - (int64_t)frame_ms;
  } else if (frame_ms < clock->last_raw) {
    clock->base_ms += (int64_t)UINT32_MAX + 1;
  }

  clock->last_raw = frame_ms;

  return clock->base_ms + (int64_t)frame_ms;
}

gboolean sofi_clock_advance(SofiClock *clock, int64_t now_ms) {
  clock->now_ms = now_ms;
  clock->busy = FALSE;

  for (guint i = 0; i < clock->tweens->len; i++) {
    if (sofi_tween_advance(g_ptr_array_index(clock->tweens, i), now_ms)) {
      clock->busy = TRUE;
    }
  }

  /* Nothing is running, so this is the last frame with work in it: the next
   * stamp, whenever it comes, re-anchors rather than counting the gap. */
  clock->dormant = !clock->busy;

  return clock->busy;
}

void sofi_clock_reset(SofiClock *clock) { clock->dormant = TRUE; }

int64_t sofi_clock_now(const SofiClock *clock) { return clock->now_ms; }

gboolean sofi_clock_busy(const SofiClock *clock) {
  for (guint i = 0; i < clock->tweens->len; i++) {
    const SofiTween *tween = g_ptr_array_index(clock->tweens, i);

    if (tween->running) {
      return TRUE;
    }
  }

  return FALSE;
}

/*
 * ---------------------------------------------------------------------------
 * sofi's own layer over the clock above.
 * ---------------------------------------------------------------------------
 */

/** How far a panel drops below its resting place before rising into it. */
#define ANIM_RISE_PX 12.0

/** How far a banner travels in from the edge it is anchored to. Larger than the
 * rise because a slide reads as travel rather than as a settle. */
#define ANIM_SLIDE_PX 40.0

/** Ceiling on the work a hikari configuration may do before it is abandoned.
 * A configuration is data; one that does not finish is a bug in the
 * configuration and must not take sofi's startup with it. */
#define ANIM_LUA_STEP_LIMIT 200000

static struct {
  SofiClock *clock;
  gboolean available;

  int duration_ms;
  int theme_duration_ms;
  gboolean duration_resolved;

  SofiTween surface;
  SofiTween selection;
  SofiTween live;

  SofiAnimMotion motion;
  int anchor;
  gboolean mapped;
  gboolean exiting;
  gboolean was_busy;
  gboolean trailing;
  guint settle_source;
  guint guard_source;
  void (*settled)(void);
} Anim = {
    .clock = NULL,
    .available = FALSE,
    .duration_ms = SOFI_ANIM_DURATION_DEFAULT,
    .theme_duration_ms = -1,
    .duration_resolved = FALSE,
    .motion = SOFI_ANIM_RISE,
    .anchor = WL_CENTER,
    .mapped = FALSE,
    .exiting = FALSE,
    .was_busy = FALSE,
    .trailing = FALSE,
    .settle_source = 0,
    .guard_source = 0,
    .settled = NULL,
    /* Action purpose: at rest before any clock exists. The surface is at zero
     * because it has not come up yet; the other two rest at one so a caller
     * that never animates reads a fully drawn selection and a fully drawn live
     * cue. Written here rather than left to the clock's lazy creation so that
     * nothing can set a tween and then have the clock reset it. */
    .surface = {.easing = SOFI_EASE_LINEAR},
    .selection = {.from = 1.0, .to = 1.0, .value = 1.0,
                  .easing = SOFI_EASE_LINEAR},
    .live = {.from = 1.0, .to = 1.0, .value = 1.0, .easing = SOFI_EASE_LINEAR},
};

/* Function purpose: drop both pending ends of an exit at once. One of them is
 * the exit finishing and the other is it failing to -- see
 * sofi_anim_surface_unmap() -- and whichever arrives first must leave nothing
 * behind that could fire a second time against a surface that is already gone. */
static void anim_cancel_sources(void) {
  if (Anim.settle_source > 0) {
    g_source_remove(Anim.settle_source);
    Anim.settle_source = 0;
  }
  if (Anim.guard_source > 0) {
    g_source_remove(Anim.guard_source);
    Anim.guard_source = 0;
  }
}

SofiClock *sofi_anim_clock(void) {
  if (Anim.clock == NULL) {
    Anim.clock = sofi_clock_create();
    sofi_clock_add(Anim.clock, &Anim.surface);
    sofi_clock_add(Anim.clock, &Anim.selection);
    sofi_clock_add(Anim.clock, &Anim.live);
  }

  return Anim.clock;
}

void sofi_anim_cleanup(void) {
  anim_cancel_sources();
  sofi_clock_destroy(Anim.clock);
  Anim.clock = NULL;
  Anim.mapped = FALSE;
  Anim.exiting = FALSE;
  Anim.was_busy = FALSE;
  Anim.trailing = FALSE;
  sofi_tween_init(&Anim.surface, 0.0);
  sofi_tween_init(&Anim.selection, 1.0);
  sofi_tween_init(&Anim.live, 1.0);
}

void sofi_anim_set_available(gboolean available) {
  Anim.available = available;
}

gboolean sofi_anim_available(void) { return Anim.available; }

#ifdef HAVE_LUA
/* Function purpose: the allowed half of the Lua standard library, left on the
 * stack as the chunk's _ENV. A hikari configuration is data sofi reads out of
 * another program's directory, so it gets arithmetic and string handling and
 * nothing that touches the filesystem, the process or the loader: no io, no os,
 * no package, no require, no load, no dofile. */
static void anim_lua_push_sandbox(lua_State *lua) {
  static const char *const allowed[] = {
      LUA_MATHLIBNAME, LUA_STRLIBNAME, LUA_TABLIBNAME, "pairs",
      "ipairs",        "type",         "tonumber",     "tostring",
      "assert",        "error",        NULL};

  lua_createtable(lua, 0, 10);

  for (gsize i = 0; allowed[i] != NULL; i++) {
    lua_getglobal(lua, allowed[i]);
    lua_setfield(lua, -2, allowed[i]);
  }

  /* Action purpose: the real string library the loop above just copied in is
   * written over with the bounded one. Two of the real library's functions loop
   * inside a single C call, and the step hook below cannot interrupt one of
   * those: it counts VM instructions and a C call is one of them. The call also
   * repoints the string metatable, which is the half that closes the
   * method-call form. Shared with source/hikari-palette.c, which reads this
   * same file from the same four paths -- a bound only one of the two readers
   * applies does not stop that file hanging sofi. */
  sofi_hikari_lua_push_bounded_string(lua);
  lua_setfield(lua, -2, LUA_STRLIBNAME);

  /* Action purpose: hikari provides `env` to the file it reads, and its shipped
   * hikari.lua uses it. Without one, indexing that absent global raises and the
   * chunk ends before `animation` is reached, so this reader would take its
   * duration from the built-in default against the very file it is following.
   * The same table goes into source/hikari-palette.c's sandbox, because the two
   * readers read one file and must read it to one result. */
  sofi_hikari_lua_push_env(lua);
  lua_setfield(lua, -2, "env");
}

/* Function purpose: anim_lua_push_sandbox() as something lua_pcall() can run.
 * Every step of building the sandbox allocates, an allocation the ceiling
 * refuses raises LUA_ERRMEM, and a raise with no protected call beneath it
 * reaches Lua's panic function and abort()s -- so a configuration this reader
 * is meant to decline would instead end sofi. The sandbox is the call's one
 * result and lands where the direct call left it, which is what lets the caller
 * keep treating it as a value on the stack. Matches push_sandbox_protected() in
 * source/hikari-palette.c, which solves the same problem for the other reader
 * of the same file. */
static int anim_lua_push_sandbox_protected(lua_State *lua) {
  anim_lua_push_sandbox(lua);

  return 1;
}

/* Action purpose: a step hook is the only way to stop a configuration that
 * loops. luaL_error from inside the hook unwinds into the pcall below. */
static void anim_lua_step_hook(lua_State *lua, G_GNUC_UNUSED lua_Debug *debug) {
  luaL_error(lua, "configuration did not finish");
}

/* Function purpose: leave the `animation` block on top of the stack, or push
 * nothing and return FALSE. The configuration's shape is still settling, so both
 * places it can live are accepted: `ui.animation` first, because that is where
 * the rest of the compositor's presentation settings sit, then a top-level
 * `animation`. The table searched is whatever the chunk returned, falling back
 * to the globals it assigned into its own sandbox. */
static gboolean anim_lua_find_block(lua_State *lua, int root) {
  if (lua_getfield(lua, root, "ui") == LUA_TTABLE) {
    if (lua_getfield(lua, -1, "animation") == LUA_TTABLE) {
      lua_remove(lua, -2);

      return TRUE;
    }
    lua_pop(lua, 1);
  }
  lua_pop(lua, 1);

  if (lua_getfield(lua, root, "animation") == LUA_TTABLE) {
    return TRUE;
  }
  lua_pop(lua, 1);

  return FALSE;
}

/* Function purpose: read `animation { enabled, duration }` out of one hikari
 * configuration. Returns whether a duration was found; the compositor's own
 * number is what keeps sofi, saber and hikari moving at one speed. */
static gboolean anim_read_hikari_lua(const char *path, int *duration_ms) {
  SofiHikariLuaBudget budget;
  lua_State *lua = sofi_hikari_lua_newstate(&budget);

  if (lua == NULL) {
    return FALSE;
  }

  luaL_openlibs(lua);

  /* A missing configuration is a first run, not an error: hikari need not be
   * the compositor sofi is running under. */
  if (luaL_loadfilex(lua, path, "t") != LUA_OK) {
    lua_close(lua);

    return FALSE;
  }

  lua_pushcfunction(lua, anim_lua_push_sandbox_protected);

  if (lua_pcall(lua, 0, 1, 0) != LUA_OK) {
    lua_close(lua);

    return FALSE;
  }

  lua_pushvalue(lua, -1);

  /* _ENV is always upvalue 1 of a main chunk. */
  if (lua_setupvalue(lua, -3, 1) == NULL) {
    lua_close(lua);

    return FALSE;
  }

  /* Action purpose: leave the stack as [sandbox, chunk] so lua_pcall finds the
   * chunk on top and the sandbox survives underneath the result -- a
   * configuration that assigns instead of returning leaves its table there. */
  lua_insert(lua, -2);

  lua_sethook(lua, anim_lua_step_hook, LUA_MASKCOUNT, ANIM_LUA_STEP_LIMIT);

  int status = lua_pcall(lua, 0, 1, 0);

  lua_sethook(lua, NULL, 0, 0);

  /* Action purpose: the allocation ceiling comes off here and not at the close,
   * because from this line on the memory being asked for is this function's own
   * -- the lua_getfield calls below intern their key names -- and not the
   * configuration's. See sofi_hikari_lua_release(). */
  sofi_hikari_lua_release(&budget);

  if (status != LUA_OK) {
    g_debug("%s: %s", path, lua_tostring(lua, -1));
    lua_close(lua);

    return FALSE;
  }

  /* The sandbox is underneath the result; a chunk with no `return` leaves nil
   * there and its assignments in the sandbox instead. */
  int root = lua_istable(lua, -1) ? lua_absindex(lua, -1) : lua_absindex(lua, -2);
  gboolean found = FALSE;

  if (lua_istable(lua, root) && anim_lua_find_block(lua, root)) {
    if (lua_getfield(lua, -1, "duration") == LUA_TNUMBER) {
      /* Action purpose: read as a number and round, because a configuration
       * that computes its duration hands over a float -- `1000/6` is a float in
       * Lua 5.4 even though both operands are integers. lua_tointeger() is
       * wrong here: it reports a number with no exact integer representation as
       * 0, which passes the range check below and is indistinguishable from
       * `enabled = false`, so one non-integral duration would quietly stop
       * every animation. The range is tested before the rounding so that a NaN,
       * which fails both comparisons, is rejected rather than cast. */
      lua_Number value = lua_tonumber(lua, -1);

      if (value >= 0.0 && value <= (lua_Number)SOFI_ANIM_DURATION_MAX) {
        *duration_ms = (int)nearbyint(value);
        found = TRUE;
      }
    }
    lua_pop(lua, 1);

    /* Action purpose: `enabled = false` is the compositor saying it does not
     * animate, and it is read after the duration because it overrides it -- the
     * two keys arrive here as one number, and zero is already what the tween
     * code reads as "settle immediately". */
    if (lua_getfield(lua, -1, "enabled") == LUA_TBOOLEAN &&
        !lua_toboolean(lua, -1)) {
      *duration_ms = 0;
      found = TRUE;
    }
    lua_pop(lua, 1);
  }

  lua_close(lua);

  return found;
}
#endif

/* Function purpose: ask the compositor's configuration, user copy first. Split
 * out so the Lua dependency is one call site and the resolution order below
 * reads the same whether or not sofi was built with it. */
static gboolean anim_duration_from_hikari(int *duration_ms) {
#ifdef HAVE_LUA
  const char *xdg = g_getenv("XDG_CONFIG_HOME");
  const char *home = g_getenv("HOME");
  char *candidates[4];
  int count = 0;
  gboolean found = FALSE;

  if (xdg != NULL && *xdg != '\0') {
    candidates[count++] = g_build_filename(xdg, "hikari", "hikari.lua", NULL);
  }
  if (home != NULL && *home != '\0') {
    candidates[count++] =
        g_build_filename(home, ".config", "hikari", "hikari.lua", NULL);
  }
  candidates[count++] = g_build_filename(SYSCONFDIR, "hikari", "hikari.lua", NULL);
  candidates[count++] = g_strdup("/etc/hikari/hikari.lua");

  for (int i = 0; i < count; i++) {
    if (!found && anim_read_hikari_lua(candidates[i], duration_ms)) {
      found = TRUE;
    }
    g_free(candidates[i]);
  }

  return found;
#else
  (void)duration_ms;
  return FALSE;
#endif
}

int sofi_anim_duration(void) {
  if (Anim.duration_resolved) {
    return Anim.duration_ms;
  }
  Anim.duration_resolved = TRUE;

  /* Action purpose: the theme is the most specific answer and wins outright.
   * Otherwise the compositor is asked, and only then does sofi fall back -- to
   * the number saber falls back to, so that a desktop with no animation block
   * anywhere still moves at one speed rather than three. */
  if (Anim.theme_duration_ms >= 0) {
    Anim.duration_ms = MIN(Anim.theme_duration_ms, SOFI_ANIM_DURATION_MAX);
  } else if (!anim_duration_from_hikari(&Anim.duration_ms)) {
    Anim.duration_ms = SOFI_ANIM_DURATION_DEFAULT;
  }

  g_debug("motion duration %dms", Anim.duration_ms);

  return Anim.duration_ms;
}

void sofi_anim_set_duration_override(int duration_ms) {
  Anim.theme_duration_ms = duration_ms;
  Anim.duration_resolved = FALSE;
}

int64_t sofi_anim_now(void) { return sofi_clock_now(sofi_anim_clock()); }

/* Function purpose: whether anything is worth animating at all. Both halves
 * matter: the XCB backend never reports a frame callback, and a duration of 0 --
 * a theme or a compositor asking for instant -- must leave every surface exactly
 * where it would have been with no animation code in the process. */
static gboolean anim_enabled(void) {
  return Anim.available && sofi_anim_duration() > 0;
}

static gboolean anim_settle_idle(G_GNUC_UNUSED gpointer data) {
  void (*handler)(void) = Anim.settled;

  Anim.settle_source = 0;
  anim_cancel_sources();
  Anim.exiting = FALSE;
  Anim.mapped = FALSE;
  sofi_tween_stop(&Anim.surface);
  sofi_clock_reset(sofi_anim_clock());

  if (handler != NULL) {
    handler();
  }

  return G_SOURCE_REMOVE;
}

/* Action purpose: a deferred exit is only ever ended by a frame callback, and a
 * surface the compositor has stopped presenting -- occluded, or on an inactive
 * output -- gets none. Without this, sofi would sit with its panel up waiting
 * for a frame that is not coming, having already decided to quit. The guard
 * finishes the exit by the clock instead, so the worst case is an exit that
 * does not animate rather than one that never happens. */
static gboolean anim_settle_guard(G_GNUC_UNUSED gpointer data) {
  Anim.guard_source = 0;

  if (Anim.exiting && Anim.settle_source == 0) {
    Anim.settle_source = g_idle_add(anim_settle_idle, NULL);
  }

  return G_SOURCE_REMOVE;
}

gboolean sofi_anim_frame(uint32_t frame_ms) {
  SofiClock *clock = sofi_anim_clock();
  gboolean busy = sofi_clock_advance(clock, sofi_clock_stamp(clock, frame_ms));

  /* Action purpose: ask for one frame beyond the last one that moved. The step
   * that takes a tween past its end reports "not running" and settles the value
   * in the same breath, so without this the last frame anyone paints is the
   * second-to-last sample of the curve and the panel comes to rest a few per
   * cent short of where it was going. */
  Anim.trailing = Anim.was_busy && !busy;
  Anim.was_busy = busy;

  /* Action purpose: hand the exit back to the main loop rather than running it
   * here. The handler drops the surface, and this is called from inside the
   * frame-callback dispatch -- destroying the surface the callback is still
   * walking is how that goes wrong. */
  if (Anim.exiting && !sofi_tween_running(&Anim.surface) &&
      Anim.settle_source == 0) {
    Anim.settle_source = g_idle_add(anim_settle_idle, NULL);
  }

  return busy;
}

gboolean sofi_anim_busy(void) {
  if (Anim.clock == NULL) {
    return FALSE;
  }

  return Anim.trailing || sofi_clock_busy(Anim.clock);
}

void sofi_anim_set_motion(SofiAnimMotion motion) { Anim.motion = motion; }

void sofi_anim_surface_map(void) {
  if (!anim_enabled()) {
    Anim.mapped = TRUE;
    Anim.exiting = FALSE;
    sofi_tween_init(&Anim.surface, 1.0);
    return;
  }

  /* A view stacked over one already up is not a new surface; restarting the
   * entry would fade the panel the user is reading back out. */
  if (Anim.mapped && !Anim.exiting) {
    return;
  }

  Anim.mapped = TRUE;
  Anim.exiting = FALSE;
  sofi_tween_start(&Anim.surface, 0.0, 1.0, sofi_anim_duration(),
                   SOFI_EASE_OUT_CUBIC, sofi_anim_now());

  /* Action purpose: the live cue rides on the surface coming up rather than on
   * a call of its own. The only thing that reads it is a panel of notifications
   * that are live by definition, and in the daemon that panel appearing IS the
   * arrival -- the surface goes down between notifications and comes back for
   * the next one. */
  sofi_anim_live_mark();
}

gboolean sofi_anim_surface_unmap(void) {
  if (!anim_enabled() || !Anim.mapped) {
    Anim.mapped = FALSE;
    Anim.exiting = FALSE;
    sofi_tween_init(&Anim.surface, 0.0);
    return FALSE;
  }

  if (Anim.exiting) {
    return TRUE;
  }

  Anim.exiting = TRUE;
  sofi_tween_start(&Anim.surface, Anim.surface.value, 0.0,
                   sofi_anim_duration(), SOFI_EASE_OUT_CUBIC, sofi_anim_now());

  /* Action purpose: a zero-length exit settles inside sofi_tween_start, so no
   * frame will ever report it finished. Say so now and let the caller tear down
   * as it always did. */
  if (!sofi_tween_running(&Anim.surface)) {
    Anim.exiting = FALSE;
    Anim.mapped = FALSE;
    return FALSE;
  }

  if (Anim.guard_source == 0) {
    Anim.guard_source =
        g_timeout_add(sofi_anim_duration() * 2 + 100, anim_settle_guard, NULL);
  }

  return TRUE;
}

gboolean sofi_anim_surface_state(double *alpha, double *dx, double *dy) {
  double progress = Anim.surface.value;
  double travel = 0.0;
  double offset_x = 0.0;
  double offset_y = 0.0;

  if (alpha != NULL) {
    *alpha = 1.0;
  }
  if (dx != NULL) {
    *dx = 0.0;
  }
  if (dy != NULL) {
    *dy = 0.0;
  }

  /* Action purpose: keep answering while an exit is settled but not yet acted
   * on. The frame that takes the tween past its end stops it, and the repaint
   * for that frame runs ahead of the idle that drops the surface -- reporting
   * "nothing is moving" there would paint the panel back at full opacity for
   * one frame on its way out. */
  if (!anim_enabled() || (!sofi_tween_running(&Anim.surface) && !Anim.exiting)) {
    return FALSE;
  }

  travel = (Anim.motion == SOFI_ANIM_SLIDE ? ANIM_SLIDE_PX : ANIM_RISE_PX) *
           (1.0 - progress);

  /* Action purpose: a panel rises into place from below wherever it sits, but a
   * banner has an edge to come from, so it travels along the axis it is anchored
   * to and in the direction that puts it off-screen. WL_CENTER has no edge to
   * use, so it rises like a panel. */
  if (Anim.motion == SOFI_ANIM_SLIDE) {
    if ((Anim.anchor & WL_NORTH) != 0) {
      offset_y = -travel;
    } else if ((Anim.anchor & WL_SOUTH) != 0) {
      offset_y = travel;
    } else if ((Anim.anchor & WL_WEST) != 0) {
      offset_x = -travel;
    } else if ((Anim.anchor & WL_EAST) != 0) {
      offset_x = travel;
    } else {
      offset_y = travel;
    }
  } else {
    offset_y = travel;
  }

  if (alpha != NULL) {
    *alpha = CLAMP(progress, 0.0, 1.0);
  }
  if (dx != NULL) {
    *dx = offset_x;
  }
  if (dy != NULL) {
    *dy = offset_y;
  }

  return TRUE;
}

void sofi_anim_surface_settle(void) {
  /* Action purpose: read before anim_cancel_sources() clears the two pending
   * ends of an exit, because an exit that was in flight is a promise this
   * function must not break. sofi_anim_surface_unmap() answered TRUE for it,
   * and on that answer the caller deferred its own teardown until the settled
   * handler fires. Cancelling the sources and clearing the flag without firing
   * it would leave that caller holding a finished view for ever: in the daemon
   * a widget tree per occurrence, and in a one-shot run a process whose only
   * route to exit was the callback just dropped. */
  gboolean pending_close = Anim.exiting;

  anim_cancel_sources();
  Anim.exiting = FALSE;
  Anim.mapped = FALSE;
  Anim.was_busy = FALSE;
  Anim.trailing = FALSE;
  Anim.motion = SOFI_ANIM_RISE;
  sofi_tween_stop(&Anim.surface);
  sofi_tween_init(&Anim.surface, 0.0);
  sofi_anim_selection_reset();

  if (Anim.clock != NULL) {
    sofi_clock_reset(Anim.clock);
  }

  /* Action purpose: finish that exit from an idle rather than from here. The
   * caller is in the middle of destroying the surface -- the layer-shell close
   * handler rebuilds it immediately afterwards -- and the handler reaches back
   * into the view layer to tear a widget tree down, so running it inside a
   * teardown that has not returned yet is a re-entrancy this will not
   * introduce. An idle runs once the surface is whole again, which is the same
   * mechanism anim_settle_guard() uses and the same bookkeeping, since it is
   * the same callback. On the shutdown path the loop is already quitting and
   * the idle is simply dropped with it -- the behaviour that was there
   * before. */
  if (pending_close && Anim.settled != NULL) {
    Anim.settle_source = g_idle_add(anim_settle_idle, NULL);
  }
}

void sofi_anim_surface_set_anchor(int location) { Anim.anchor = location; }

void sofi_anim_set_settled_handler(void (*handler)(void)) {
  Anim.settled = handler;
}

void sofi_anim_selection_move(void) {
  if (!anim_enabled()) {
    sofi_tween_init(&Anim.selection, 1.0);
    return;
  }

  sofi_tween_start(&Anim.selection, 0.0, 1.0, sofi_anim_duration(),
                   SOFI_EASE_OUT_CUBIC, sofi_anim_now());
}

double sofi_anim_selection_progress(void) {
  if (!sofi_tween_running(&Anim.selection)) {
    return -1.0;
  }

  return CLAMP(Anim.selection.value, 0.0, 1.0);
}

void sofi_anim_selection_reset(void) { sofi_tween_init(&Anim.selection, 1.0); }

void sofi_anim_live_mark(void) {
  if (!anim_enabled()) {
    sofi_tween_init(&Anim.live, 1.0);
    return;
  }

  sofi_tween_start(&Anim.live, 0.0, 1.0, sofi_anim_duration(),
                   SOFI_EASE_OUT_CUBIC, sofi_anim_now());
}

double sofi_anim_live_value(void) {
  if (!sofi_tween_running(&Anim.live)) {
    return 1.0;
  }

  return CLAMP(Anim.live.value, 0.0, 1.0);
}
