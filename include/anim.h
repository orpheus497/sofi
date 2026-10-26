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
 * Script function and purpose: The tween clock, and the four pieces of motion
 * sofi draws with it. Every animated quantity is advanced from a timestamp the
 * compositor supplied in wl_callback.done, never from a wall clock, so motion
 * is paced by presentation rather than by how often the main loop happens to
 * wake.
 *
 * The tween, easing, clock and hover primitives below are ported from saber's
 * include/saber/anim.h and src/anim.c (MIT, same author), renamed to sofi's
 * conventions. The traffic already runs the other way -- saber's src/sni.c,
 * src/dbusmenu.c and src/appinfo.c are ported from sofi and record it in their
 * own headers -- and this is the same arrangement in reverse. Four properties
 * are the reason for porting rather than writing a second clock: frame-callback
 * pacing, the 32-bit millisecond wrap in sofi_clock_stamp(), the re-anchoring
 * of a dormant clock, and a cancelled tween settling on its target instead of
 * freezing mid-curve. The zero-duration infinite-repaint defect (saber D-036)
 * is already fixed here.
 */

#ifndef SOFI_ANIM_H
#define SOFI_ANIM_H

#include <stdint.h>

#include <glib.h>

typedef enum {
  SOFI_EASE_LINEAR,
  SOFI_EASE_OUT_CUBIC,
  SOFI_EASE_IN_OUT_CUBIC,
  /** Damped overshoot: crosses 1.0 and settles back. */
  SOFI_EASE_SPRING,
} SofiEasing;

typedef struct {
  double from;
  double to;
  double value;
  int64_t start_ms;
  int duration_ms;
  SofiEasing easing;
  gboolean running;
  gboolean repeat;
} SofiTween;

double sofi_ease(SofiEasing easing, double t);

/* Function purpose: a repeating 0..1 phase mapped to a symmetric -1..1 swing,
 * so the caller multiplies by an amplitude rather than reimplementing the curve
 * at every call site. */
double sofi_anim_wiggle(double phase);

/** Half-cycle swell, 1.0 at both ends of the phase and `depth` above 1.0 at its
 * middle -- a scale factor, so an idle row draws at exactly its nominal size. */
double sofi_anim_throb(double phase, double depth);

void sofi_tween_init(SofiTween *tween, double value);

void sofi_tween_start(SofiTween *tween, double from, double to,
                      int duration_ms, SofiEasing easing, int64_t now_ms);

/** Loops from `from` to `to` forever; only sofi_tween_stop() ends it. */
void sofi_tween_start_repeating(SofiTween *tween, double from, double to,
                                int duration_ms, SofiEasing easing,
                                int64_t now_ms);

/* Function purpose: end the tween where it would have finished. Settling on
 * the target rather than freezing mid-curve is what keeps a cancelled animation
 * from leaving a surface permanently half-faded. */
void sofi_tween_stop(SofiTween *tween);

/** Returns whether the tween is still running after the step. */
gboolean sofi_tween_advance(SofiTween *tween, int64_t now_ms);

static inline double sofi_tween_value(const SofiTween *tween) {
  return tween->value;
}

static inline gboolean sofi_tween_running(const SofiTween *tween) {
  return tween->running;
}

typedef struct _SofiClock SofiClock;

SofiClock *sofi_clock_create(void);

void sofi_clock_destroy(SofiClock *clock);

/* Function purpose: registered tweens are borrowed, never owned -- an owner
 * embeds its tweens in its own state and takes them out again when that state
 * dies. Adding the same tween twice is a no-op. */
void sofi_clock_add(SofiClock *clock, SofiTween *tween);

void sofi_clock_remove(SofiClock *clock, SofiTween *tween);

/* Function purpose: turn wl_callback.done's 32-bit millisecond stamp into a
 * monotonic 64-bit one. The wire value wraps roughly every 49 days, and a wrap
 * mid-session would otherwise throw every live tween into the far future. */
int64_t sofi_clock_stamp(SofiClock *clock, uint32_t frame_ms);

/** Advances every registered tween; returns TRUE while any is still running,
 * which is the caller's signal to ask for another frame. */
gboolean sofi_clock_advance(SofiClock *clock, int64_t now_ms);

/* Function purpose: re-anchor the clock on its next frame, for an owner that
 * stops the loop by a route the clock cannot see. sofi_clock_advance() notices
 * the loop settling on its own, but a surface torn down mid-animation takes its
 * frame callbacks with it while tweens are still marked running, so no advance
 * ever records the stop. Calling this from the teardown path keeps the next open
 * from starting its tween against a stale timestamp. */
void sofi_clock_reset(SofiClock *clock);

int64_t sofi_clock_now(const SofiClock *clock);

gboolean sofi_clock_busy(const SofiClock *clock);

/* Function purpose: the cross-fade a pointer leaves behind it. `enter` runs on
 * whatever is under the pointer now and `leave` on what it has just left,
 * started from wherever the incoming fade had reached -- so a pointer swept
 * along a list leaves a trail that settles rather than a highlight that snaps
 * from one row to the next. */
typedef struct {
  SofiTween enter;
  SofiTween leave;
} SofiHover;

void sofi_hover_init(SofiHover *hover);

/** Both halves are borrowed by the clock exactly as a bare tween is. */
void sofi_hover_add(SofiClock *clock, SofiHover *hover);

/* Function purpose: begin the outgoing fade, from wherever the incoming one had
 * reached. Called for the row being left, before whichever of sofi_hover_enter()
 * and sofi_hover_rest() the new state asks for. */
void sofi_hover_leave(SofiHover *hover, int duration_ms, int64_t now_ms);

void sofi_hover_enter(SofiHover *hover, int duration_ms, int64_t now_ms);

/* Function purpose: nothing is under the pointer any more. Settles the incoming
 * fade back at zero rather than on its target, which is where sofi_tween_stop()
 * would land it and would leave the row just left fully lit. The outgoing fade
 * is deliberately untouched: it still has to finish. */
void sofi_hover_rest(SofiHover *hover);

/** Both halves back to nothing, for a surface being torn down. */
void sofi_hover_clear(SofiHover *hover);

static inline double sofi_hover_value(const SofiHover *hover) {
  return hover->enter.value;
}

/** What the row the pointer has just left draws at. */
static inline double sofi_hover_fading(const SofiHover *hover) {
  return hover->leave.value;
}

/*
 * ---------------------------------------------------------------------------
 * sofi's own layer over the clock above.
 * ---------------------------------------------------------------------------
 */

/** How a surface arrives and leaves. */
typedef enum {
  /** Panels: fade up into place. */
  SOFI_ANIM_RISE,
  /** Banners: travel in from the screen edge they are anchored to. */
  SOFI_ANIM_SLIDE,
} SofiAnimMotion;

/** Longest motion a compositor or a theme may ask for, in milliseconds. */
#define SOFI_ANIM_DURATION_MAX 1000

/** What sofi uses when neither a theme nor the compositor says otherwise. The
 * same number saber settles on, so the desktop moves at one speed. */
#define SOFI_ANIM_DURATION_DEFAULT 180

/* Function purpose: the shared clock, created on first use. Lazy because the
 * only alternative is an init call in source/sofi.c, and nothing about the clock
 * needs to exist before the first surface does. */
SofiClock *sofi_anim_clock(void);

void sofi_anim_cleanup(void);

/* Function purpose: declare that this display backend can pace motion from a
 * frame callback. Only the Wayland backend calls it. The XCB backend has no
 * frame-callback equivalent, so it never does and every animation below
 * degrades to instant there. */
void sofi_anim_set_available(gboolean available);

gboolean sofi_anim_available(void);

/* Function purpose: the resolved motion duration in milliseconds, 0 meaning
 * instant. Resolution order is an explicit theme property, then the
 * compositor's own `animation { duration }`, then
 * SOFI_ANIM_DURATION_DEFAULT -- deliberately the order saber resolves in, so
 * neither program invents a third default. */
int sofi_anim_duration(void);

/* Function purpose: let the theme win over the compositor. Fed from the
 * `animation-duration` property on the window widget; a negative value means
 * the theme did not set it and the compositor is still asked. */
void sofi_anim_set_duration_override(int duration_ms);

int64_t sofi_anim_now(void);

/* Function purpose: advance every tween from a wl_callback.done stamp. Returns
 * TRUE while something is still moving, which is the backend's signal that the
 * next frame has work to draw. */
gboolean sofi_anim_frame(uint32_t frame_ms);

gboolean sofi_anim_busy(void);

/* Function purpose: pick the motion the next surface arrives and leaves with.
 * A mode declares its own from _init -- the notification banner is the one that
 * slides -- and the default goes back to SOFI_ANIM_RISE whenever a surface
 * settles, so a mode that never asks gets the panel treatment. */
void sofi_anim_set_motion(SofiAnimMotion motion);

/* Function purpose: the surface is coming up. Idempotent while the entry is
 * already running, so a view stacked on top of another does not restart the
 * fade under it. */
void sofi_anim_surface_map(void);

/* Function purpose: the surface is going away. Returns TRUE if an exit is now
 * running and the caller should defer its teardown until
 * sofi_anim_set_settled_handler()'s handler fires, FALSE if it should tear down
 * at once -- which is the answer on X11, with motion off, or with no surface up. */
gboolean sofi_anim_surface_unmap(void);

/* Function purpose: what the surface draws at this frame. Writes the composite
 * alpha and the pixel offset the whole surface is shifted by, and returns
 * whether either differs from the resting state; a caller that gets FALSE can
 * skip the group it would otherwise composite through. */
gboolean sofi_anim_surface_state(double *alpha, double *dx, double *dy);

/* Function purpose: the surface went away by a route the animation cannot
 * observe -- a teardown, a backend switch, a second view opening over it. Drops
 * any pending exit and re-anchors the clock so the next open starts from now
 * rather than from a stale frame stamp. */
void sofi_anim_surface_settle(void);

/* Function purpose: which screen edge a sliding surface travels in from, in
 * WL_* location terms. Set by the backend from the surface's own anchor, since
 * the animation layer cannot see the shell. */
void sofi_anim_surface_set_anchor(int location);

/* Function purpose: install the exit handler. Called from view_init() rather
 * than from source/sofi.c because the decision an exit defers -- drop the
 * surface, or end the process -- belongs to the view layer. */
void sofi_anim_set_settled_handler(void (*handler)(void));

/* Function purpose: the listview selection has moved. The outgoing and incoming
 * highlights cross-fade over one duration. */
void sofi_anim_selection_move(void);

/** 0 while the outgoing highlight is still fully lit, 1 once the incoming one
 * is. Returns a negative value when no cross-fade is in flight. */
double sofi_anim_selection_progress(void);

void sofi_anim_selection_reset(void);

/* Function purpose: a notification has just arrived or gone live. Drives a
 * single settling fade on the live cue rather than a repeating pulse: a
 * repeating one would keep the clock busy for as long as the panel is open and
 * repaint at the refresh rate the whole time. Called for you by
 * sofi_anim_surface_map(). */
void sofi_anim_live_mark(void);

/** 1.0 at rest, so a caller that never animates still renders a live row fully. */
double sofi_anim_live_value(void);

#endif
