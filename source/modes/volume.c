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
 * @brief Audio output control.
 *
 * Script function and purpose: presents every audio sink the session has, with
 * its level and mute state, and lets one be adjusted, muted or made the
 * default. A summoned surface -- it reads the state once, acts, and exits.
 *
 * Sofi links no audio library and gains no build dependency from this mode.
 * Three backends are driven as subprocesses and chosen at runtime by which
 * tool is actually installed:
 *
 *   wpctl   WirePlumber / PipeWire.  Preferred where PipeWire is the server,
 *           because it is the native control surface and reports node ids that
 *           survive a sink being renamed.
 *   pactl   PulseAudio, and PipeWire's PulseAudio shim.  The widest reach: it
 *           answers on both servers, and its `get-sink-*` verbs have a stable
 *           output shape across a decade of releases.
 *   mixer   FreeBSD base system.  Needs nothing installed, and is the only one
 *           that answers on a machine running neither sound server.
 *
 * Executing a binary is not linking. No GPL or copyleft code is linked, loaded
 * or vendored by any path here -- see AGENTS.md §2.
 *
 * The mode holds no state between invocations and subscribes to no events. A
 * persistent volume readout is hikari-sakura's top bar; a persistent control is
 * saber's panel. Neither is duplicated here.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Volume"

#include "config.h"

#ifdef VOLUME_MODE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "helper.h"
#include "modes/volume.h"
#include "settings.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** How far one press of the raise/lower binding moves the level, in percent. */
#define VOLUME_STEP 5

/** Ceiling for a level this mode will set. Sinks can usually be driven above
 * 100% and it distorts; a menu should not make that the easy accident. */
#define VOLUME_MAX 100

/** Cells in the level bar drawn on each row.
 *
 * Action purpose: this is `100 / VOLUME_STEP` and must stay that way. At twenty
 * cells one press of the raise/lower binding moves the bar by exactly one cell,
 * every time. Any other count makes some presses move it and others not, which
 * reads as the key having missed rather than as rounding. */
#define VOLUME_BAR_CELLS (100 / VOLUME_STEP)

/** One audio sink, normalised across the three backends. */
typedef struct {
  /** Backend-native handle: a node id for wpctl, a sink name for pactl, a
   * mixer device name for mixer. Passed back to the backend verbatim. */
  char *id;
  /** What the row says. */
  char *label;
  /** 0-100. */
  int volume;
  gboolean muted;
  gboolean is_default;
  /** FALSE when the backend has no mute verb for this sink -- older FreeBSD
   * mixer(8). The row says so rather than offering an action that fails. */
  gboolean can_mute;
} VolumeSink;

typedef struct _VolumeBackend VolumeBackend;

struct _VolumeBackend {
  /** Shown in the message bar, so the user can see which tool is answering. */
  const char *name;
  /** The executable that must be on $PATH for this backend to be chosen. */
  const char *binary;
  /** Fill @p sinks with newly allocated VolumeSink. TRUE when it found any. */
  gboolean (*list)(GPtrArray *sinks);
  /** Set an absolute level, 0-100. */
  gboolean (*set_volume)(const VolumeSink *sink, int percent);
  /** Toggle mute. */
  gboolean (*set_mute)(const VolumeSink *sink);
  /** Make this the session default. NULL where the backend has no such
   * concept, which is the case for mixer(8). */
  gboolean (*set_default)(const VolumeSink *sink);
};

typedef struct {
  /** Never NULL once _init has returned TRUE. */
  const VolumeBackend *backend;
  /** VolumeSink*, owned. */
  GPtrArray *sinks;
} VolumeModePrivateData;

static void volume_sink_free(gpointer data) {
  VolumeSink *sink = data;

  if (sink == NULL) {
    return;
  }
  g_free(sink->id);
  g_free(sink->label);
  g_free(sink);
}

/**
 * Function purpose: run one backend command and hand back its stdout.
 *
 * Action purpose: a non-zero exit is reported at debug level only. The callers
 * use failure as a probe result -- "this backend is not the one" -- and warning
 * on every probe would put a line on stderr for every menu opened on a machine
 * that happens not to run PipeWire.
 *
 * KNOWN LIMIT, stated rather than left to be discovered: this is synchronous on
 * sofi's main thread and g_spawn_sync has no timeout, so a control tool that
 * accepts the request and never answers hangs the menu until it does. Every
 * command here is a short local query against a running daemon, which is why it
 * is tolerable; making it bounded means driving the child asynchronously
 * through the main loop, which is a larger change than this mode needs today.
 *
 * @param argv NULL-terminated. argv[0] is looked up on $PATH.
 * @param out  stdout, newly allocated, or NULL when not wanted.
 *
 * @returns TRUE when the command ran and exited zero.
 */
static gboolean volume_run(const char *const *argv, char **out) {
  gchar *stdout_buf = NULL;
  gint status = 0;
  GError *error = NULL;

  if (out != NULL) {
    *out = NULL;
  }

  if (!g_spawn_sync(NULL, (gchar **)argv, NULL,
                    G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL,
                    NULL, out != NULL ? &stdout_buf : NULL, NULL, &status,
                    &error)) {
    g_debug("Could not run %s: %s", argv[0], error->message);
    g_error_free(error);
    return FALSE;
  }

  if (!g_spawn_check_wait_status(status, &error)) {
    g_debug("%s exited non-zero: %s", argv[0], error->message);
    g_error_free(error);
    g_free(stdout_buf);
    return FALSE;
  }

  if (out != NULL) {
    *out = stdout_buf;
  } else {
    g_free(stdout_buf);
  }

  return TRUE;
}

/** Clamp a level into the range this mode is willing to set. */
static int volume_clamp(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > VOLUME_MAX) {
    return VOLUME_MAX;
  }
  return percent;
}

/* ------------------------------------------------------------------ wpctl */

/**
 * Function purpose: read one sink's level and mute state from wpctl.
 *
 * `wpctl get-volume <id>` answers `Volume: 0.45`, with ` [MUTED]` appended when
 * the sink is muted. The level is a float in 0.0-1.0 that can exceed 1.0, so it
 * is scaled and clamped rather than trusted.
 */
static gboolean volume_wpctl_read(const char *id, int *percent,
                                  gboolean *muted) {
  const char *argv[] = {"wpctl", "get-volume", id, NULL};
  char *out = NULL;

  if (!volume_run(argv, &out) || out == NULL) {
    return FALSE;
  }

  const char *p = strstr(out, "Volume:");
  if (p == NULL) {
    g_free(out);
    return FALSE;
  }

  *percent = volume_clamp((int)(g_ascii_strtod(p + 7, NULL) * 100.0 + 0.5));
  *muted = (strstr(out, "MUTED") != NULL);

  g_free(out);
  return TRUE;
}

/**
 * Function purpose: enumerate sinks from `wpctl status`.
 *
 * Action purpose: the Sinks block is found by heading and left by the first
 * following heading, because the tree-drawing characters around each row differ
 * between wireplumber releases and are not safe to match on. Within the block a
 * row is recognised by the one shape that has been stable: an optional `*` for
 * the default, then `<id>. <name>`. Anything else in the block is skipped
 * rather than treated as an error, so a new field added by a later wireplumber
 * costs nothing here.
 */
static gboolean volume_wpctl_list(GPtrArray *sinks) {
  const char *argv[] = {"wpctl", "status", NULL};
  char *out = NULL;

  if (!volume_run(argv, &out) || out == NULL) {
    return FALSE;
  }

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  gboolean in_sinks = FALSE;
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (strstr(lines[i], "Sinks:") != NULL) {
      in_sinks = TRUE;
      continue;
    }
    if (!in_sinks) {
      continue;
    }
    /* Any other heading ends the block. Headings are the only lines carrying a
     * colon at the end of a word with no digits before it, so the cheap test is
     * a known set: Sources, Filters, Streams, Devices. */
    if (strstr(lines[i], "Sources:") != NULL ||
        strstr(lines[i], "Filters:") != NULL ||
        strstr(lines[i], "Streams:") != NULL ||
        strstr(lines[i], "Devices:") != NULL) {
      break;
    }

    /* Walk to the first digit, remembering whether a '*' was passed on the way
     * -- that marks the default sink. */
    const char *p = lines[i];
    gboolean is_default = FALSE;
    while (*p != '\0' && !g_ascii_isdigit(*p)) {
      if (*p == '*') {
        is_default = TRUE;
      }
      p++;
    }
    if (*p == '\0') {
      continue;
    }

    char *endptr = NULL;
    guint64 node_id = g_ascii_strtoull(p, &endptr, 10);
    if (endptr == p || endptr == NULL || *endptr != '.') {
      continue;
    }

    char *name = g_strstrip(g_strdup(endptr + 1));
    /* wpctl appends `[vol: 0.45]`; the authoritative read is get-volume, so the
     * decoration is stripped rather than parsed. */
    char *bracket = strchr(name, '[');
    if (bracket != NULL) {
      *bracket = '\0';
      g_strchomp(name);
    }
    if (*name == '\0') {
      g_free(name);
      continue;
    }

    VolumeSink *sink = g_malloc0(sizeof(*sink));
    sink->id = g_strdup_printf("%" G_GUINT64_FORMAT, node_id);
    sink->label = name;
    sink->is_default = is_default;
    sink->can_mute = TRUE;
    if (!volume_wpctl_read(sink->id, &sink->volume, &sink->muted)) {
      sink->volume = 0;
      sink->muted = FALSE;
    }
    g_ptr_array_add(sinks, sink);
  }

  g_strfreev(lines);

  return sinks->len > 0;
}

static gboolean volume_wpctl_set_volume(const VolumeSink *sink, int percent) {
  char *arg = g_strdup_printf("%d%%", volume_clamp(percent));
  const char *argv[] = {"wpctl", "set-volume", sink->id, arg, NULL};
  gboolean ok = volume_run(argv, NULL);

  g_free(arg);
  return ok;
}

static gboolean volume_wpctl_set_mute(const VolumeSink *sink) {
  const char *argv[] = {"wpctl", "set-mute", sink->id, "toggle", NULL};

  return volume_run(argv, NULL);
}

static gboolean volume_wpctl_set_default(const VolumeSink *sink) {
  const char *argv[] = {"wpctl", "set-default", sink->id, NULL};

  return volume_run(argv, NULL);
}

/* ------------------------------------------------------------------ pactl */

/**
 * Function purpose: enumerate sinks from `pactl list sinks`.
 *
 * Action purpose: the verbose listing is used rather than `list short` because
 * it is the only one carrying `Description:` -- the name a person recognises.
 * The short listing gives `alsa_output.pci-0000_00_1f.3.analog-stereo`, which is
 * an identifier, not a label, and a menu row made of one is unreadable.
 *
 * It is also *fewer* subprocesses, not more: one call yields the name, the
 * description, the level and the mute state of every sink at once, where the
 * per-sink `get-sink-*` verbs cost two calls each.
 *
 * A record starts at `Sink #` and every field is matched by its own prefix, so
 * fields arriving in a different order -- or new ones appearing -- cost nothing.
 * `Name:` is the handle rather than the index, because an index is reassigned
 * when a sink is removed and re-added, and acting on a recycled index would
 * adjust the wrong device.
 */
static gboolean volume_pactl_list(GPtrArray *sinks) {
  const char *argv[] = {"pactl", "list", "sinks", NULL};
  char *out = NULL;

  if (!volume_run(argv, &out) || out == NULL) {
    return FALSE;
  }

  char *default_sink = NULL;
  const char *default_argv[] = {"pactl", "get-default-sink", NULL};
  if (volume_run(default_argv, &default_sink) && default_sink != NULL) {
    g_strstrip(default_sink);
  }

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  VolumeSink *sink = NULL;
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    char *line = g_strstrip(lines[i]);

    if (g_str_has_prefix(line, "Sink #")) {
      sink = g_malloc0(sizeof(*sink));
      sink->can_mute = TRUE;
      g_ptr_array_add(sinks, sink);
      continue;
    }
    if (sink == NULL) {
      continue;
    }

    if (g_str_has_prefix(line, "Name:")) {
      g_free(sink->id);
      sink->id = g_strdup(g_strstrip(line + 5));
      sink->is_default =
          (default_sink != NULL && g_strcmp0(default_sink, sink->id) == 0);
    } else if (g_str_has_prefix(line, "Description:")) {
      g_free(sink->label);
      sink->label = g_strdup(g_strstrip(line + 12));
    } else if (g_str_has_prefix(line, "Mute:")) {
      sink->muted = (strstr(line, "yes") != NULL);
    } else if (g_str_has_prefix(line, "Volume:")) {
      /* Action purpose: the reply names each channel separately -- `front-left:
       * 29491 / 45% / -18.06 dB, front-right: ...`. The first percentage is
       * taken: a row shows one number, and per-channel balance is not something
       * this surface edits. The dB figure carries no '%', so the first one found
       * is always a level. */
      const char *pct = strchr(line, '%');
      if (pct != NULL) {
        const char *start = pct;
        while (start > line && g_ascii_isdigit(*(start - 1))) {
          start--;
        }
        if (start != pct) {
          sink->volume = volume_clamp((int)g_ascii_strtoll(start, NULL, 10));
        }
      }
    }
  }

  g_strfreev(lines);
  g_free(default_sink);

  /* Action purpose: a record with no `Name:` is unusable -- there is nothing to
   * pass back to pactl -- and one can exist if the listing was truncated. Drop
   * them rather than carrying a row whose every verb would fail. Iterating
   * downwards keeps the indices valid while removing. */
  for (guint i = sinks->len; i > 0; i--) {
    VolumeSink *candidate = g_ptr_array_index(sinks, i - 1);
    if (candidate->id == NULL) {
      g_ptr_array_remove_index(sinks, i - 1);
      continue;
    }
    if (candidate->label == NULL) {
      candidate->label = g_strdup(candidate->id);
    }
  }

  return sinks->len > 0;
}

static gboolean volume_pactl_set_volume(const VolumeSink *sink, int percent) {
  char *arg = g_strdup_printf("%d%%", volume_clamp(percent));
  const char *argv[] = {"pactl", "set-sink-volume", sink->id, arg, NULL};
  gboolean ok = volume_run(argv, NULL);

  g_free(arg);
  return ok;
}

static gboolean volume_pactl_set_mute(const VolumeSink *sink) {
  const char *argv[] = {"pactl", "set-sink-mute", sink->id, "toggle", NULL};

  return volume_run(argv, NULL);
}

static gboolean volume_pactl_set_default(const VolumeSink *sink) {
  const char *argv[] = {"pactl", "set-default-sink", sink->id, NULL};

  return volume_run(argv, NULL);
}

/* ------------------------------------------------------------------ mixer */

/** The mixer(8) devices worth showing. Every one of these is optional and a
 * device the hardware does not have is skipped rather than reported. */
static const char *const volume_mixer_devices[] = {"vol", "pcm", "speaker",
                                                   "line", "headphone", NULL};

/**
 * Function purpose: read one mixer device, tolerating every output shape
 * FreeBSD's mixer(8) has used.
 *
 * Action purpose: mixer(8) was rewritten between FreeBSD 13 and 14 and its
 * output changed with it -- `vol 100:100`, `vol.volume=0.75:0.75` and
 * `Mixer vol is currently set to 75:75` have all been current. Rather than
 * guess a version, this scans the whole reply for the one thing every shape
 * has: a left:right pair. Values at or below 1.0 with a decimal point are read
 * as a fraction, anything else as a percentage, which is what tells the two
 * generations apart without asking which one is installed.
 *
 * @returns FALSE when the device does not exist on this hardware.
 */
static gboolean volume_mixer_read(const char *device, int *percent) {
  const char *argv[] = {"mixer", device, NULL};
  char *out = NULL;

  if (!volume_run(argv, &out) || out == NULL) {
    return FALSE;
  }

  gboolean found = FALSE;
  for (const char *p = out; *p != '\0'; p++) {
    if (*p != ':') {
      continue;
    }

    /* Walk back over the left-hand number, which may carry a decimal point. */
    const char *start = p;
    gboolean seen_digit = FALSE;
    while (start > out &&
           (g_ascii_isdigit(*(start - 1)) || *(start - 1) == '.')) {
      if (g_ascii_isdigit(*(start - 1))) {
        seen_digit = TRUE;
      }
      start--;
    }
    if (!seen_digit) {
      continue;
    }
    /* The right-hand side must be a number too, or this colon belongs to prose
     * rather than to a level. */
    if (!g_ascii_isdigit(*(p + 1))) {
      continue;
    }

    double value = g_ascii_strtod(start, NULL);
    gboolean fractional = (memchr(start, '.', (size_t)(p - start)) != NULL);

    *percent = volume_clamp(fractional ? (int)(value * 100.0 + 0.5)
                                       : (int)(value + 0.5));
    found = TRUE;
    break;
  }

  g_free(out);
  return found;
}

/**
 * Function purpose: enumerate the mixer devices this hardware actually has.
 *
 * There is no default-sink concept in mixer(8) and no enumeration verb whose
 * output is stable across the rewrite, so the known device names are probed one
 * at a time and the ones that answer are listed.
 */
static gboolean volume_mixer_list(GPtrArray *sinks) {
  for (unsigned int i = 0; volume_mixer_devices[i] != NULL; i++) {
    int percent = 0;

    if (!volume_mixer_read(volume_mixer_devices[i], &percent)) {
      continue;
    }

    VolumeSink *sink = g_malloc0(sizeof(*sink));
    sink->id = g_strdup(volume_mixer_devices[i]);
    sink->label = g_strdup(volume_mixer_devices[i]);
    sink->volume = percent;
    /* Action purpose: mixer(8) gained `.mute` in the FreeBSD 14 rewrite and has
     * no way to report whether it is muted on either generation. Rather than
     * show a state that would be a guess, mute is offered as an action and the
     * state is left unclaimed -- `can_mute` gates the action, `muted` stays
     * FALSE and the row shows no mute marker. */
    sink->can_mute = TRUE;
    sink->muted = FALSE;
    /* The first device that answers is the one the system is using. */
    sink->is_default = (sinks->len == 0);
    g_ptr_array_add(sinks, sink);
  }

  return sinks->len > 0;
}

static gboolean volume_mixer_set_volume(const VolumeSink *sink, int percent) {
  char *arg = g_strdup_printf("%s=%d", sink->id, volume_clamp(percent));
  const char *argv[] = {"mixer", arg, NULL};
  gboolean ok = volume_run(argv, NULL);

  g_free(arg);
  return ok;
}

static gboolean volume_mixer_set_mute(const VolumeSink *sink) {
  char *arg = g_strdup_printf("%s.mute=^", sink->id);
  const char *argv[] = {"mixer", arg, NULL};
  gboolean ok = volume_run(argv, NULL);

  g_free(arg);
  if (!ok) {
    g_warning("mixer(8) on this system has no mute for '%s'. That verb arrived "
              "with the FreeBSD 14 rewrite; on an older base system, lower the "
              "level instead.",
              sink->id);
  }
  return ok;
}

/* ---------------------------------------------------------------- backend */

/** Action purpose: order is preference, not availability. wpctl first because
 * it is native where PipeWire is the server; pactl second because it answers on
 * both servers; mixer last because it is always present on FreeBSD and would
 * otherwise mask a running sound server. */
static const VolumeBackend volume_backends[] = {
    {.name = "wpctl (PipeWire)",
     .binary = "wpctl",
     .list = volume_wpctl_list,
     .set_volume = volume_wpctl_set_volume,
     .set_mute = volume_wpctl_set_mute,
     .set_default = volume_wpctl_set_default},
    {.name = "pactl (PulseAudio)",
     .binary = "pactl",
     .list = volume_pactl_list,
     .set_volume = volume_pactl_set_volume,
     .set_mute = volume_pactl_set_mute,
     .set_default = volume_pactl_set_default},
    {.name = "mixer (FreeBSD)",
     .binary = "mixer",
     .list = volume_mixer_list,
     .set_volume = volume_mixer_set_volume,
     .set_mute = volume_mixer_set_mute,
     .set_default = NULL},
};

/**
 * Function purpose: choose the backend and fill the sink list in one pass.
 *
 * Action purpose: presence on $PATH is necessary but not sufficient -- pactl is
 * installed on plenty of machines where nothing is listening, and it exits
 * non-zero there. So a backend is only accepted once it has actually produced a
 * sink, and the search continues past one that is installed but silent. That is
 * what makes the fallback to mixer(8) work on a FreeBSD box with the PulseAudio
 * client libraries installed and no server running.
 *
 * @returns the chosen backend, or NULL when none answered.
 */
static const VolumeBackend *volume_select_backend(GPtrArray *sinks) {
  for (unsigned int i = 0; i < G_N_ELEMENTS(volume_backends); i++) {
    const VolumeBackend *backend = &volume_backends[i];

    char *found = g_find_program_in_path(backend->binary);
    if (found == NULL) {
      g_debug("%s is not on $PATH; trying the next backend.", backend->binary);
      continue;
    }
    g_free(found);

    if (backend->list(sinks)) {
      g_debug("Volume backend: %s", backend->name);
      return backend;
    }

    g_debug("%s is installed but reported no sink; trying the next backend.",
            backend->binary);
    g_ptr_array_set_size(sinks, 0);
  }

  return NULL;
}

/** Re-read the sink list from the backend already chosen. */
static void volume_refresh(VolumeModePrivateData *pd) {
  g_ptr_array_set_size(pd->sinks, 0);
  pd->backend->list(pd->sinks);
}

/** The selected sink, or NULL when the line is out of range. */
static VolumeSink *volume_sink_at(const VolumeModePrivateData *pd,
                                  unsigned int line) {
  if (pd == NULL || line >= pd->sinks->len) {
    return NULL;
  }
  return g_ptr_array_index(pd->sinks, line);
}

/* ------------------------------------------------------------------- mode */

static int volume_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  VolumeModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->sinks = g_ptr_array_new_with_free_func(volume_sink_free);
  mode_set_private_data(sw, (void *)pd);

  pd->backend = volume_select_backend(pd->sinks);

  /* Action purpose: propagate the failure. A volume menu that found no way to
   * reach the audio system should say so, not present an empty list that looks
   * like a machine with no sound card. sofi turns FALSE here into a visible
   * error. */
  if (pd->backend == NULL) {
    g_warning("No audio control backend answered. Install one of wpctl "
              "(WirePlumber), pactl (PulseAudio), or use FreeBSD's mixer(8) -- "
              "and check that a sound server is actually running.");
    return FALSE;
  }

  return TRUE;
}

static unsigned int volume_mode_get_num_entries(const Mode *sw) {
  const VolumeModePrivateData *pd =
      (const VolumeModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  return pd->sinks->len;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const VolumeModePrivateData *pd =
      (const VolumeModePrivateData *)mode_get_private_data(sw);
  VolumeSink *sink = volume_sink_at(pd, selected_line);

  if (sink == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  if (sink->is_default) {
    *state |= ACTIVE;
  }
  /* Action purpose: a muted sink is styled the way the task strip styles a
   * minimised window -- URGENT is the one display state a theme can already
   * reach, and "present but not currently doing its job" is the same idea in
   * both places. */
  if (sink->muted) {
    *state |= URGENT;
  }

  if (!get_entry) {
    return NULL;
  }

  /* Action purpose: a level is a quantity, and a quantity reads faster as a
   * length than as a number -- the bar is the thing the eye lands on, the
   * percentage is there to be exact when it matters. Both are drawn from block
   * characters rather than a widget because a listview row is one text run:
   * there is no second widget to put a progress bar in without inventing one.
   *
   * The name is demoted with `alpha` rather than a colour, the way the task
   * strip demotes a window class, so it follows the row's own text colour when
   * the row is selected instead of fighting it. */
  *state |= MARKUP;

  GString *bar = g_string_sized_new(VOLUME_BAR_CELLS * 3);
  int filled = (sink->volume * VOLUME_BAR_CELLS + 50) / 100;
  for (int i = 0; i < VOLUME_BAR_CELLS; i++) {
    g_string_append(bar, i < filled ? "█" : "░");
  }

  char *escaped = g_markup_escape_text(sink->label, -1);
  char *row = g_strdup_printf(
      "<span alpha='%s'>%s</span>  %3d%%  <span alpha='65%%'>%s</span>%s",
      sink->muted ? "40%" : "85%", bar->str, sink->volume, escaped,
      sink->muted ? "  <span alpha='65%'>muted</span>" : "");

  g_string_free(bar, TRUE);
  g_free(escaped);

  return row;
}

/**
 * Function purpose: state the verbs, and name the backend, in the message bar.
 *
 * Action purpose: a volume menu whose only discoverable action is Enter is a
 * volume menu that cannot change the volume, so the keys are written on it. The
 * raise and lower bindings are the ones that need saying -- `kb-custom-N`
 * defaults to `Alt+N`, which nobody guesses, and the panel layout rebinds them
 * to the arrow keys precisely because that is not a reasonable thing to expect
 * a user to know.
 *
 * The backend is named alongside because which tool answered decides which
 * verbs work: mixer(8) has no default sink and cannot report mute at all.
 * Naming it is what makes "that key did nothing" self-explanatory.
 */
static char *volume_mode_get_message(const Mode *sw) {
  const VolumeModePrivateData *pd =
      (const VolumeModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL || pd->backend == NULL) {
    return NULL;
  }

  const char *default_hint =
      pd->backend->set_default != NULL ? "  ·  Alt+1 default" : "";

  return g_markup_printf_escaped(
      "←/→ volume  ·  Enter mute%s  ·  %s", default_hint, pd->backend->name);
}

static ModeMode volume_mode_result(Mode *sw, int mretv,
                                   G_GNUC_UNUSED char **input,
                                   unsigned int selected_line) {
  VolumeModePrivateData *pd =
      (VolumeModePrivateData *)mode_get_private_data(sw);

  g_return_val_if_fail(pd != NULL, MODE_EXIT);

  if (mretv & MENU_NEXT) {
    return NEXT_DIALOG;
  }
  if (mretv & MENU_PREVIOUS) {
    return PREVIOUS_DIALOG;
  }
  if (mretv & MENU_QUICK_SWITCH) {
    return (ModeMode)(mretv & MENU_LOWER_MASK);
  }

  VolumeSink *sink = volume_sink_at(pd, selected_line);

  if (mretv & MENU_OK) {
    /* Action purpose: Enter mutes, and the panel stays up. Muting is the verb a
     * volume menu is summoned for most often, and it is the one whose effect is
     * invisible unless the row that changed is still on screen to show it. */
    if (sink == NULL) {
      return RELOAD_DIALOG;
    }
    if (sink->can_mute && pd->backend->set_mute(sink)) {
      volume_refresh(pd);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    if (sink == NULL) {
      return custom < 3 ? RELOAD_DIALOG : (ModeMode)custom;
    }

    switch (custom) {
    /* kb-custom-1: make this sink the session default and close, because
     * choosing where sound goes is a decision, not an adjustment -- there is
     * nothing to watch afterwards. */
    case 0:
      if (pd->backend->set_default == NULL) {
        g_warning("%s has no concept of a default sink.", pd->backend->name);
        return RELOAD_DIALOG;
      }
      sofi_view_hide();
      pd->backend->set_default(sink);
      return MODE_EXIT;

    /* kb-custom-2 / kb-custom-3: step the level down and up. Both reload so the
     * row tracks the change, which is what makes holding the key usable. */
    case 1:
      if (pd->backend->set_volume(sink, sink->volume - VOLUME_STEP)) {
        volume_refresh(pd);
      }
      return RELOAD_DIALOG;

    case 2:
      if (pd->backend->set_volume(sink, sink->volume + VOLUME_STEP)) {
        volume_refresh(pd);
      }
      return RELOAD_DIALOG;

    default:
      return (ModeMode)custom;
    }
  }

  return MODE_EXIT;
}

static void volume_mode_destroy(Mode *sw) {
  VolumeModePrivateData *pd =
      (VolumeModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_ptr_array_free(pd->sinks, TRUE);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int volume_token_match(const Mode *sw, sofi_int_matcher **tokens,
                              unsigned int index) {
  const VolumeModePrivateData *pd =
      (const VolumeModePrivateData *)mode_get_private_data(sw);
  VolumeSink *sink = volume_sink_at(pd, index);

  if (sink == NULL) {
    return FALSE;
  }

  return helper_token_match(tokens, sink->label);
}

Mode volume_mode = {.name = "volume",
                    .cfg_name_key = "display-volume",
                    ._init = volume_mode_init,
                    ._get_num_entries = volume_mode_get_num_entries,
                    ._result = volume_mode_result,
                    ._destroy = volume_mode_destroy,
                    ._token_match = volume_token_match,
                    ._get_display_value = _get_display_value,
                    ._get_icon = NULL,
                    ._get_completion = NULL,
                    ._preprocess_input = NULL,
                    ._get_message = volume_mode_get_message,
                    .private_data = NULL,
                    .free = NULL,
                    .type = MODE_TYPE_SWITCHER};

#endif // VOLUME_MODE
