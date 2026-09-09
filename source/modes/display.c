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
 * @brief Display configuration: resolution, refresh, position, scale, rotation
 * and brightness, per output.
 *
 * Script function and purpose: a working control surface for the outputs, not a
 * read-only listing. It drives `wlr-randr(1)` over
 * `wlr-output-management-unstable-v1` for everything the protocol carries, and
 * `backlight(8)` for the one thing it does not.
 *
 * ---------------------------------------------------------------------------
 * THIS IS A DRILL-DOWN, AND THAT IS THE WHOLE DESIGN
 *
 * A flat list cannot express this. One output on this machine advertises
 * twenty-five modes; flattening two outputs' worth of modes, scales,
 * transforms and positions into a single column produces a hundred-row list
 * where nothing is findable and every row looks like every other row. The
 * previous version dodged that by doing nothing at all -- it listed output
 * names and had no verbs.
 *
 * So the surface has levels, the way the tray menu and the file browser
 * already do in this program:
 *
 *   Displays          every output, with what it is doing right now
 *     DP-3            that output's settings, each showing its current value
 *       Resolution    the mode list, current one marked
 *       Scale         a scale list
 *       Rotation      the four rotations and their flipped variants
 *       Position      relative placement against the other outputs
 *
 * `..` returns, and Escape closes. Each level rewrites the prompt, so the
 * input bar is a breadcrumb rather than an unlabelled box.
 *
 * ---------------------------------------------------------------------------
 * WHAT DRIVES WHAT, AND WHY TWO TOOLS
 *
 * `wlr-randr` speaks `wlr-output-management-unstable-v1`, which carries mode,
 * refresh, position, scale, transform, adaptive sync and enablement. It does
 * **not** carry brightness -- no Wayland protocol does -- so brightness is
 * `backlight(8)` from the FreeBSD base system, writing the backlight device.
 *
 * That device is `root:video`, so a user in the `video` group changes
 * brightness with no privilege at all and this mode never escalates. It is
 * also **per-machine rather than per-output**: there is one panel backlight and
 * it belongs to the internal display, so the brightness row appears only on an
 * internal output and says so on the others rather than pretending. An external
 * monitor's brightness is DDC/CI, which needs `ddcutil` and is not attempted
 * here.
 *
 * ---------------------------------------------------------------------------
 * THE OUTPUT FORMAT WAS READ, NOT GUESSED
 *
 * Parsed from `wlr-randr`'s plain output, observed on a live session rather
 * than inferred:
 *
 *     DP-3 "GWD ARZOPA 000000000000 (DP-3)"
 *       Make: GWD
 *       Enabled: yes
 *       Modes:
 *         1920x1080 px, 60.000000 Hz (preferred, current)
 *       Position: 1920,0
 *       Transform: normal
 *       Scale: 1.000000
 *       Adaptive Sync: disabled
 *
 * An output header is at column zero; its fields are indented two spaces; modes
 * are indented four under `Modes:`. `--json` is available and is deliberately
 * not used: it would need a JSON parser, and sofi links none -- adding one for
 * a format this regular would be the first new build dependency of the whole
 * system-menu programme.
 *
 * ---------------------------------------------------------------------------
 * KNOWN LIMIT, shared with the volume, network and bluetooth modes
 *
 * The subprocesses are synchronous on sofi's main thread and `g_spawn_sync` has
 * no timeout, so a tool that accepts a request and never answers holds the menu
 * until it does. `wlr-randr` answers immediately or not at all.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Display"

#include "config.h"

#ifdef DISPLAY_MODE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "helper.h"
#include "modes/display.h"
#include "settings.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** The backlight device `backlight(8)` writes by default. Its presence is what
 * decides whether a brightness row is offered at all. */
#define DISPLAY_BACKLIGHT_DEV "/dev/backlight/backlight0"

/** One step of the brightness keys, in percent. */
#define DISPLAY_BRIGHTNESS_STEP 5

/** Which level of the drill-down is on screen. */
typedef enum {
  DISPLAY_VIEW_OUTPUTS,
  DISPLAY_VIEW_OUTPUT,
  DISPLAY_VIEW_MODES,
  DISPLAY_VIEW_SCALE,
  DISPLAY_VIEW_TRANSFORM,
  DISPLAY_VIEW_POSITION,
} DisplayView;

/** What a row is, which decides what Enter does to it. */
typedef enum {
  /** The `..` row. Enter goes back up a level. */
  DISPLAY_ROW_BACK,
  /** An output at the top level. Enter opens its settings. */
  DISPLAY_ROW_OUTPUT,
  /** A named setting. Enter opens its picker, or toggles it. */
  DISPLAY_ROW_CONTROL,
  /** A value in a picker. Enter applies it. */
  DISPLAY_ROW_VALUE,
  /** Explanatory, not actionable. */
  DISPLAY_ROW_INFO,
} DisplayRowKind;

/** Which setting a ::DISPLAY_ROW_CONTROL row opens or toggles. */
typedef enum {
  DISPLAY_CONTROL_MODES,
  DISPLAY_CONTROL_SCALE,
  DISPLAY_CONTROL_TRANSFORM,
  DISPLAY_CONTROL_POSITION,
  DISPLAY_CONTROL_BRIGHTNESS,
  DISPLAY_CONTROL_ADAPTIVE,
  DISPLAY_CONTROL_ENABLED,
  DISPLAY_CONTROL_PREFERRED,
} DisplayControl;

typedef struct {
  int width;
  int height;
  double refresh;
  gboolean preferred;
  gboolean current;
} DisplayModeInfo;

typedef struct {
  /** `DP-3`. What `--output` takes. */
  char *name;
  /** `GWD ARZOPA 000000000000 (DP-3)`. */
  char *description;

  gboolean enabled;
  int x;
  int y;
  double scale;
  char *transform;
  gboolean adaptive_sync;
  gboolean has_adaptive;

  /** DisplayModeInfo*, owned. */
  GPtrArray *modes;
  /** Index into @c modes of the current one, or -1. */
  int current_mode;

  /** eDP-*, LVDS-* -- the panel the backlight belongs to. */
  gboolean internal;
} DisplayOutput;

typedef struct {
  DisplayRowKind kind;
  DisplayControl control;

  char *label;
  /** The current value, shown in the value column. May be NULL. */
  char *value;
  /** Secondary note, dimmer still. May be NULL. */
  char *note;

  /** Index into the private data's output array, or -1. */
  int output;
  /** Index into that output's mode array, for a mode row. */
  int mode;
  /** The literal argument a value row applies (`1.5`, `90`, `left-of DP-3`). */
  char *arg;

  /** Currently in effect. Drives the ACTIVE row state. */
  gboolean active;
  /** Not doing its job -- a disabled output, the `..` row. */
  gboolean dim;
} DisplayRow;

typedef struct {
  DisplayView view;
  /** Which output the deeper levels are about. -1 at the top. */
  int focus;

  /** DisplayOutput*, owned. */
  GPtrArray *outputs;
  /** DisplayRow*, owned. Rebuilt on every navigation and every apply. */
  GPtrArray *rows;

  /** wlr-randr is on $PATH. */
  gboolean have_randr;
  /** wlr-randr ran and produced at least one output. */
  gboolean answered;

  /** backlight(8) is usable: the tool and the device are both present. */
  gboolean have_backlight;
  /** 0-100, or -1 when unknown. */
  int brightness;

  /** Last thing that happened, for the message bar. Owned, may be NULL. */
  char *status;
} DisplayModePrivateData;

/* --------------------------------------------------------------- process */

/**
 * Function purpose: run one command and hand back its stdout.
 *
 * Action purpose: failure is reported at debug level, because the callers use
 * it as a probe result. The one failure a user must hear about -- a refused
 * configuration -- is reported through the message bar by the caller, where it
 * is on screen instead of on a stderr nobody is reading.
 */
static gboolean display_run(const char *const *argv, char **out) {
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

/* ---------------------------------------------------------------- model */

static void display_mode_info_free(gpointer data) { g_free(data); }

static void display_output_free(gpointer data) {
  DisplayOutput *output = data;

  if (output == NULL) {
    return;
  }
  g_free(output->name);
  g_free(output->description);
  g_free(output->transform);
  g_ptr_array_free(output->modes, TRUE);
  g_free(output);
}

static void display_row_free(gpointer data) {
  DisplayRow *row = data;

  if (row == NULL) {
    return;
  }
  g_free(row->label);
  g_free(row->value);
  g_free(row->note);
  g_free(row->arg);
  g_free(row);
}

static DisplayRow *display_row_new(DisplayModePrivateData *pd,
                                   DisplayRowKind kind) {
  DisplayRow *row = g_malloc0(sizeof(*row));

  row->kind = kind;
  row->output = -1;
  row->mode = -1;
  g_ptr_array_add(pd->rows, row);

  return row;
}

static DisplayOutput *display_output_at(const DisplayModePrivateData *pd,
                                        int index) {
  if (pd == NULL || index < 0 || (guint)index >= pd->outputs->len) {
    return NULL;
  }
  return g_ptr_array_index(pd->outputs, index);
}

static DisplayRow *display_row_at(const DisplayModePrivateData *pd,
                                  unsigned int line) {
  if (pd == NULL || line >= pd->rows->len) {
    return NULL;
  }
  return g_ptr_array_index(pd->rows, line);
}

static void display_set_status(DisplayModePrivateData *pd, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

static void display_set_status(DisplayModePrivateData *pd, const char *fmt,
                               ...) {
  va_list args;

  g_free(pd->status);
  va_start(args, fmt);
  pd->status = g_strdup_vprintf(fmt, args);
  va_end(args);
}

/* --------------------------------------------------------------- parsing */

/** The value after `Key: ` on an indented field line, or NULL. */
static const char *display_field(const char *line, const char *key) {
  const char *p = line;

  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (!g_str_has_prefix(p, key)) {
    return NULL;
  }
  p += strlen(key);
  if (*p != ':') {
    return NULL;
  }
  p++;
  while (*p == ' ') {
    p++;
  }

  return p;
}

/**
 * Function purpose: parse one `Modes:` entry.
 *
 * Format, observed: `    1920x1080 px, 60.000000 Hz (preferred, current)`. The
 * parenthesised flags are optional and either may appear alone.
 */
static gboolean display_parse_mode(const char *line, DisplayModeInfo *info) {
  int w = 0;
  int h = 0;
  double refresh = 0.0;

  while (*line == ' ' || *line == '\t') {
    line++;
  }

  if (sscanf(line, "%dx%d px, %lf Hz", &w, &h, &refresh) != 3) {
    return FALSE;
  }

  info->width = w;
  info->height = h;
  info->refresh = refresh;
  info->preferred = strstr(line, "preferred") != NULL;
  info->current = strstr(line, "current") != NULL;

  return TRUE;
}

/** TRUE for the connector names that mean a built-in panel, which is the one
 * the backlight belongs to. */
static gboolean display_is_internal(const char *name) {
  return g_str_has_prefix(name, "eDP") || g_str_has_prefix(name, "LVDS") ||
         g_str_has_prefix(name, "DSI");
}

/**
 * Function purpose: turn `wlr-randr`'s plain output into the output list.
 *
 * Action purpose: an output header sits at column zero and every one of its
 * fields is indented, so "does this line start with whitespace" is the whole
 * of the state machine. Mode lines are recognised by their own shape rather
 * than by tracking whether `Modes:` was seen, which keeps a malformed or
 * reordered block from silently swallowing the fields after it.
 */
static void display_parse(DisplayModePrivateData *pd, const char *text) {
  char **lines = g_strsplit(text, "\n", -1);
  DisplayOutput *current = NULL;

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    const char *line = lines[i];

    if (*line == '\0') {
      continue;
    }

    if (*line != ' ' && *line != '\t') {
      /* `DP-3 "GWD ARZOPA 000000000000 (DP-3)"` */
      current = g_malloc0(sizeof(*current));
      current->modes = g_ptr_array_new_with_free_func(display_mode_info_free);
      current->current_mode = -1;
      current->scale = 1.0;
      current->enabled = TRUE;

      const char *space = strchr(line, ' ');
      current->name =
          space != NULL ? g_strndup(line, (gsize)(space - line)) : g_strdup(line);

      const char *open_quote = strchr(line, '"');
      const char *close_quote =
          open_quote != NULL ? strrchr(open_quote + 1, '"') : NULL;
      if (close_quote != NULL) {
        current->description =
            g_strndup(open_quote + 1, (gsize)(close_quote - open_quote - 1));
      }

      current->internal = display_is_internal(current->name);
      g_ptr_array_add(pd->outputs, current);
      continue;
    }

    if (current == NULL) {
      continue;
    }

    /* A mode line before anything else, because its shape is unambiguous and
     * it is the only indented line that is not `Key: value`. */
    DisplayModeInfo probe;
    memset(&probe, 0, sizeof(probe));
    if (display_parse_mode(line, &probe)) {
      DisplayModeInfo *info = g_malloc0(sizeof(*info));
      *info = probe;
      if (info->current) {
        current->current_mode = (int)current->modes->len;
      }
      g_ptr_array_add(current->modes, info);
      continue;
    }

    const char *value = NULL;

    if ((value = display_field(line, "Enabled")) != NULL) {
      current->enabled = g_str_has_prefix(value, "yes");
    } else if ((value = display_field(line, "Position")) != NULL) {
      sscanf(value, "%d,%d", &current->x, &current->y);
    } else if ((value = display_field(line, "Transform")) != NULL) {
      g_free(current->transform);
      current->transform = g_strstrip(g_strdup(value));
    } else if ((value = display_field(line, "Scale")) != NULL) {
      current->scale = g_ascii_strtod(value, NULL);
    } else if ((value = display_field(line, "Adaptive Sync")) != NULL) {
      current->has_adaptive = TRUE;
      current->adaptive_sync = g_str_has_prefix(value, "enabled");
    }
  }

  g_strfreev(lines);
}

/* ------------------------------------------------------------ brightness */

/** Read the panel brightness, or -1. `backlight` with no argument prints
 * `brightness: N`. */
static int display_read_brightness(void) {
  const char *argv[] = {"backlight", NULL};
  char *out = NULL;

  if (!display_run(argv, &out) || out == NULL) {
    g_free(out);
    return -1;
  }

  const char *found = strstr(out, "brightness:");
  int value = -1;
  if (found != NULL) {
    value = (int)strtol(found + strlen("brightness:"), NULL, 10);
  }
  g_free(out);

  if (value < 0 || value > 100) {
    return -1;
  }

  return value;
}

static gboolean display_set_brightness(DisplayModePrivateData *pd, int percent) {
  percent = CLAMP(percent, 0, 100);

  char *value = g_strdup_printf("%d", percent);
  const char *argv[] = {"backlight", value, NULL};
  gboolean ok = display_run(argv, NULL);
  g_free(value);

  if (ok) {
    pd->brightness = display_read_brightness();
  }

  return ok;
}

/* --------------------------------------------------------------- reading */

/** Re-read every output from the compositor. */
static void display_reload(DisplayModePrivateData *pd) {
  g_ptr_array_set_size(pd->outputs, 0);
  pd->answered = FALSE;

  if (!pd->have_randr) {
    return;
  }

  const char *argv[] = {"wlr-randr", NULL};
  char *out = NULL;

  if (!display_run(argv, &out) || out == NULL) {
    g_free(out);
    return;
  }

  display_parse(pd, out);
  g_free(out);

  pd->answered = pd->outputs->len > 0;

  if (pd->have_backlight) {
    pd->brightness = display_read_brightness();
  }
}

/* --------------------------------------------------------------- applying */

/**
 * Function purpose: hand one `--output NAME ...` change to the compositor and
 * reload.
 *
 * Action purpose: **one setting per invocation, deliberately.** The protocol
 * answers a whole configuration with a single yes or no, so batching two
 * changes means a rejection tells you neither which one failed nor that the
 * other was fine. One at a time costs a process and makes every failure
 * attributable, which is what the message bar needs to say anything true.
 *
 * The reload afterwards is not optional: the compositor may have applied
 * something adjacent -- a position shifting because another output resized --
 * and the rows have to show what is, not what was asked for.
 */
static gboolean display_apply(DisplayModePrivateData *pd, const char *name,
                              const char *flag, const char *value) {
  GPtrArray *argv = g_ptr_array_new();

  g_ptr_array_add(argv, (gpointer) "wlr-randr");
  g_ptr_array_add(argv, (gpointer) "--output");
  g_ptr_array_add(argv, (gpointer)name);
  g_ptr_array_add(argv, (gpointer)flag);
  if (value != NULL) {
    g_ptr_array_add(argv, (gpointer)value);
  }
  g_ptr_array_add(argv, NULL);

  gboolean ok = display_run((const char *const *)argv->pdata, NULL);

  g_ptr_array_free(argv, TRUE);

  display_reload(pd);

  return ok;
}

/* ----------------------------------------------------------- row building */

/** `1920x1080@60` — the compact form used in the value column. */
static char *display_mode_label(const DisplayModeInfo *info) {
  return g_strdup_printf("%dx%d@%.0f", info->width, info->height,
                         info->refresh);
}

/** The exact argument `--mode` takes. Three decimals distinguish the
 * near-duplicates a panel reports (60.000 against 59.940) without demanding a
 * precision wlr-randr would not match. */
static char *display_mode_arg(const DisplayModeInfo *info) {
  return g_strdup_printf("%dx%d@%.3fHz", info->width, info->height,
                         info->refresh);
}

static void display_build_outputs(DisplayModePrivateData *pd) {
  for (guint i = 0; i < pd->outputs->len; i++) {
    const DisplayOutput *output = g_ptr_array_index(pd->outputs, i);
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_OUTPUT);

    row->output = (int)i;
    row->label = g_strdup(output->name);
    row->active = output->enabled;
    row->dim = !output->enabled;

    if (!output->enabled) {
      row->value = g_strdup("off");
    } else if (output->current_mode >= 0) {
      const DisplayModeInfo *info =
          g_ptr_array_index(output->modes, (guint)output->current_mode);
      row->value = display_mode_label(info);
    } else {
      row->value = g_strdup("—");
    }

    GString *note = g_string_sized_new(64);
    if (output->enabled) {
      g_string_append_printf(note, "%.2fx  %d,%d", output->scale, output->x,
                             output->y);
      if (output->transform != NULL &&
          g_strcmp0(output->transform, "normal") != 0) {
        g_string_append_printf(note, "  %s", output->transform);
      }
      if (output->internal && pd->brightness >= 0) {
        g_string_append_printf(note, "  ☀%d%%", pd->brightness);
      }
    }
    if (output->description != NULL) {
      g_string_append_printf(note, "%s%s", note->len > 0 ? "  ·  " : "",
                             output->description);
    }
    row->note = g_string_free(note, FALSE);
  }

  if (pd->outputs->len == 0) {
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_INFO);
    row->dim = TRUE;
    if (!pd->have_randr) {
      row->label = g_strdup("wlr-randr is not installed");
      row->note = g_strdup("It speaks wlr-output-management to the compositor");
    } else {
      row->label = g_strdup("wlr-randr ran but reported no outputs");
      row->note =
          g_strdup("The compositor may not advertise output management");
    }
  }
}

static void display_build_output(DisplayModePrivateData *pd) {
  const DisplayOutput *output = display_output_at(pd, pd->focus);

  if (output == NULL) {
    pd->view = DISPLAY_VIEW_OUTPUTS;
    display_build_outputs(pd);
    return;
  }

  DisplayRow *back = display_row_new(pd, DISPLAY_ROW_BACK);
  back->label = g_strdup("..");
  back->note = g_strdup("Back to displays");
  back->dim = TRUE;

  DisplayRow *row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_ENABLED;
  row->output = pd->focus;
  row->label = g_strdup(output->enabled ? "Turn this display off"
                                        : "Turn this display on");
  row->value = g_strdup(output->enabled ? "on" : "off");
  row->active = output->enabled;
  /* Action purpose: the compositor moves this screen's windows elsewhere before
   * it goes dark -- hikari_output_set_wants_enabled() evacuates first -- so
   * this is safe to offer. Saying so on the row is the difference between a
   * verb people use and one they are afraid of. */
  row->note = g_strdup("Windows are moved to another screen first");

  if (!output->enabled) {
    /* Everything below configures a screen that is not on. The protocol carries
     * no position for a disabled head, and a mode set on one is not observable,
     * so the rows would report values that mean nothing. */
    DisplayRow *info = display_row_new(pd, DISPLAY_ROW_INFO);
    info->label = g_strdup("Settings appear once it is on");
    info->dim = TRUE;
    return;
  }

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_MODES;
  row->output = pd->focus;
  row->label = g_strdup("Resolution");
  if (output->current_mode >= 0) {
    const DisplayModeInfo *info =
        g_ptr_array_index(output->modes, (guint)output->current_mode);
    row->value = display_mode_label(info);
  }
  row->note = g_strdup_printf("%u available", output->modes->len);

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_PREFERRED;
  row->output = pd->focus;
  row->label = g_strdup("Use the preferred mode");
  row->note = g_strdup("What the display says it wants");

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_SCALE;
  row->output = pd->focus;
  row->label = g_strdup("Scale");
  row->value = g_strdup_printf("%.2fx", output->scale);

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_TRANSFORM;
  row->output = pd->focus;
  row->label = g_strdup("Rotation");
  row->value = g_strdup(output->transform != NULL ? output->transform
                                                  : "normal");

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_POSITION;
  row->output = pd->focus;
  row->label = g_strdup("Position");
  row->value = g_strdup_printf("%d,%d", output->x, output->y);
  row->note = g_strdup(pd->outputs->len > 1
                           ? "Place it against another display"
                           : "Only one display is connected");

  if (output->has_adaptive) {
    row = display_row_new(pd, DISPLAY_ROW_CONTROL);
    row->control = DISPLAY_CONTROL_ADAPTIVE;
    row->output = pd->focus;
    row->label = g_strdup("Adaptive sync");
    row->value = g_strdup(output->adaptive_sync ? "enabled" : "disabled");
    row->active = output->adaptive_sync;
  }

  row = display_row_new(pd, DISPLAY_ROW_CONTROL);
  row->control = DISPLAY_CONTROL_BRIGHTNESS;
  row->output = pd->focus;
  row->label = g_strdup("Brightness");
  if (output->internal && pd->have_backlight && pd->brightness >= 0) {
    row->value = g_strdup_printf("%d%%", pd->brightness);
    row->note = g_strdup("Alt+1 dimmer · Alt+2 brighter · Enter cycles");
  } else if (!output->internal) {
    /* Action purpose: three distinct reasons a brightness row can be inert, and
     * they want three different responses. An external monitor is DDC/CI, which
     * is a different tool entirely; a missing backlight device is a kernel
     * matter; a missing group membership is one line of configuration. */
    row->value = g_strdup("—");
    row->note = g_strdup("External displays need DDC/CI (ddcutil), not "
                         "backlight(8)");
    row->dim = TRUE;
  } else if (!pd->have_backlight) {
    row->value = g_strdup("—");
    row->note = g_strdup("No " DISPLAY_BACKLIGHT_DEV " on this machine");
    row->dim = TRUE;
  } else {
    row->value = g_strdup("—");
    row->note = g_strdup("backlight(8) could not read it — is your user in "
                         "the `video` group?");
    row->dim = TRUE;
  }
}

static void display_build_modes(DisplayModePrivateData *pd) {
  const DisplayOutput *output = display_output_at(pd, pd->focus);

  DisplayRow *back = display_row_new(pd, DISPLAY_ROW_BACK);
  back->label = g_strdup("..");
  back->note = g_strdup_printf("Back to %s",
                               output != NULL ? output->name : "displays");
  back->dim = TRUE;

  if (output == NULL) {
    return;
  }

  for (guint i = 0; i < output->modes->len; i++) {
    const DisplayModeInfo *info = g_ptr_array_index(output->modes, i);
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_VALUE);

    row->control = DISPLAY_CONTROL_MODES;
    row->output = pd->focus;
    row->mode = (int)i;
    row->label = g_strdup_printf("%dx%d", info->width, info->height);
    row->value = g_strdup_printf("%.3f Hz", info->refresh);
    row->arg = display_mode_arg(info);
    row->active = info->current;
    if (info->preferred) {
      row->note = g_strdup("preferred");
    }
  }
}

/** The scales worth offering. Fractional values are included because hikari
 * advertises `wp_fractional_scale_v1`; sofi's own surfaces still round to an
 * integer buffer scale, which is a separate omission and not this mode's. */
static const char *const display_scales[] = {"1", "1.25", "1.5",
                                             "1.75", "2", "2.5", "3"};

static void display_build_scale(DisplayModePrivateData *pd) {
  const DisplayOutput *output = display_output_at(pd, pd->focus);

  DisplayRow *back = display_row_new(pd, DISPLAY_ROW_BACK);
  back->label = g_strdup("..");
  back->note = g_strdup_printf("Back to %s",
                               output != NULL ? output->name : "displays");
  back->dim = TRUE;

  if (output == NULL) {
    return;
  }

  for (unsigned int i = 0; i < G_N_ELEMENTS(display_scales); i++) {
    const double value = g_ascii_strtod(display_scales[i], NULL);
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_VALUE);

    row->control = DISPLAY_CONTROL_SCALE;
    row->output = pd->focus;
    row->label = g_strdup_printf("%.2fx", value);
    row->arg = g_strdup(display_scales[i]);
    /* A double compared with a tolerance rather than for equality: the value
     * came back through a text round trip and 1.25 will not necessarily be
     * bit-identical to the one parsed out of wlr-randr. */
    row->active = ABS(value - output->scale) < 0.005;
    if (ABS(value - 1.0) < 0.005) {
      row->note = g_strdup("no scaling");
    }
  }
}

/** The eight transforms `--transform` accepts, in the order a person thinks of
 * them: the four rotations, then their mirrored variants. */
static const char *const display_transforms[] = {
    "normal", "90", "180", "270", "flipped", "flipped-90", "flipped-180",
    "flipped-270"};

static void display_build_transform(DisplayModePrivateData *pd) {
  const DisplayOutput *output = display_output_at(pd, pd->focus);

  DisplayRow *back = display_row_new(pd, DISPLAY_ROW_BACK);
  back->label = g_strdup("..");
  back->note = g_strdup_printf("Back to %s",
                               output != NULL ? output->name : "displays");
  back->dim = TRUE;

  if (output == NULL) {
    return;
  }

  for (unsigned int i = 0; i < G_N_ELEMENTS(display_transforms); i++) {
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_VALUE);

    row->control = DISPLAY_CONTROL_TRANSFORM;
    row->output = pd->focus;
    row->arg = g_strdup(display_transforms[i]);
    row->active = g_strcmp0(output->transform, display_transforms[i]) == 0;

    if (g_strcmp0(display_transforms[i], "normal") == 0) {
      row->label = g_strdup("Normal");
    } else if (g_str_has_prefix(display_transforms[i], "flipped")) {
      const char *degrees = strchr(display_transforms[i], '-');
      row->label = degrees != NULL
                       ? g_strdup_printf("Mirrored, %s°", degrees + 1)
                       : g_strdup("Mirrored");
    } else {
      row->label = g_strdup_printf("Rotated %s°", display_transforms[i]);
    }
    row->value = g_strdup(display_transforms[i]);
  }
}

/**
 * Function purpose: offer placement against the other outputs rather than
 * coordinates.
 *
 * Action purpose: `--pos` wants absolute layout pixels, which nobody knows off
 * the top of their head and which go wrong the moment a resolution changes.
 * `--left-of`/`--right-of`/`--above`/`--below` take another output's name and
 * let the compositor work out the arithmetic, which is both what the user
 * means and what stays correct afterwards.
 */
static void display_build_position(DisplayModePrivateData *pd) {
  const DisplayOutput *output = display_output_at(pd, pd->focus);

  DisplayRow *back = display_row_new(pd, DISPLAY_ROW_BACK);
  back->label = g_strdup("..");
  back->note = g_strdup_printf("Back to %s",
                               output != NULL ? output->name : "displays");
  back->dim = TRUE;

  if (output == NULL) {
    return;
  }

  static const char *const sides[] = {"--left-of", "--right-of", "--above",
                                      "--below"};
  static const char *const words[] = {"Left of", "Right of", "Above", "Below"};

  gboolean any = FALSE;

  for (guint i = 0; i < pd->outputs->len; i++) {
    const DisplayOutput *other = g_ptr_array_index(pd->outputs, i);

    if ((int)i == pd->focus || !other->enabled) {
      continue;
    }
    any = TRUE;

    for (unsigned int s = 0; s < G_N_ELEMENTS(sides); s++) {
      DisplayRow *row = display_row_new(pd, DISPLAY_ROW_VALUE);

      row->control = DISPLAY_CONTROL_POSITION;
      row->output = pd->focus;
      row->label = g_strdup_printf("%s %s", words[s], other->name);
      row->value = g_strdup(sides[s] + 2);
      /* The flag and its argument travel together in `arg`, split at the space
       * by the applier, because a position is the one change here that takes
       * two words. */
      row->arg = g_strdup_printf("%s %s", sides[s], other->name);
    }
  }

  if (!any) {
    DisplayRow *row = display_row_new(pd, DISPLAY_ROW_INFO);
    row->label = g_strdup("Nothing to place it against");
    row->note = g_strdup("Another display has to be connected and on");
    row->dim = TRUE;
  }
}

/** Rebuild the row list for whichever level is current. */
static void display_refresh(DisplayModePrivateData *pd) {
  g_ptr_array_set_size(pd->rows, 0);

  switch (pd->view) {
  case DISPLAY_VIEW_OUTPUT:
    display_build_output(pd);
    break;
  case DISPLAY_VIEW_MODES:
    display_build_modes(pd);
    break;
  case DISPLAY_VIEW_SCALE:
    display_build_scale(pd);
    break;
  case DISPLAY_VIEW_TRANSFORM:
    display_build_transform(pd);
    break;
  case DISPLAY_VIEW_POSITION:
    display_build_position(pd);
    break;
  case DISPLAY_VIEW_OUTPUTS:
  default:
    display_build_outputs(pd);
    break;
  }
}

/* ----------------------------------------------------------------- verbs */

/** Apply a value row, whichever picker it came from. */
static void display_activate_value(DisplayModePrivateData *pd,
                                   const DisplayRow *row) {
  const DisplayOutput *output = display_output_at(pd, row->output);

  if (output == NULL || row->arg == NULL) {
    return;
  }

  char *name = g_strdup(output->name);
  gboolean ok = FALSE;

  switch (row->control) {
  case DISPLAY_CONTROL_MODES:
    ok = display_apply(pd, name, "--mode", row->arg);
    display_set_status(pd, ok ? "%s is now %s." : "%s refused %s.", name,
                       row->arg);
    break;

  case DISPLAY_CONTROL_SCALE:
    ok = display_apply(pd, name, "--scale", row->arg);
    display_set_status(pd, ok ? "%s scaled to %sx." : "%s refused scale %s.",
                       name, row->arg);
    break;

  case DISPLAY_CONTROL_TRANSFORM:
    ok = display_apply(pd, name, "--transform", row->arg);
    display_set_status(pd, ok ? "%s rotated to %s." : "%s refused %s.", name,
                       row->arg);
    break;

  case DISPLAY_CONTROL_POSITION: {
    /* `--left-of DP-3` arrives as one string because it is the only two-word
     * change; split it back into flag and argument. */
    char **parts = g_strsplit(row->arg, " ", 2);
    if (parts[0] != NULL && parts[1] != NULL) {
      ok = display_apply(pd, name, parts[0], parts[1]);
      display_set_status(pd, ok ? "Moved %s." : "The compositor refused to "
                                                "move %s.",
                         name);
    }
    g_strfreev(parts);
    break;
  }

  default:
    break;
  }

  g_free(name);
}

/** Do whatever the highlighted row means. */
static void display_activate(DisplayModePrivateData *pd, DisplayRow *row) {
  switch (row->kind) {
  case DISPLAY_ROW_BACK:
    pd->view = pd->view == DISPLAY_VIEW_OUTPUT ? DISPLAY_VIEW_OUTPUTS
                                               : DISPLAY_VIEW_OUTPUT;
    if (pd->view == DISPLAY_VIEW_OUTPUTS) {
      pd->focus = -1;
    }
    return;

  case DISPLAY_ROW_OUTPUT:
    pd->focus = row->output;
    pd->view = DISPLAY_VIEW_OUTPUT;
    return;

  case DISPLAY_ROW_VALUE:
    display_activate_value(pd, row);
    /* Stay in the picker. Choosing a resolution and immediately wanting a
     * different one is the normal case, not an unusual one, and being thrown
     * back a level after every attempt would make that tedious. */
    return;

  case DISPLAY_ROW_CONTROL: {
    const DisplayOutput *output = display_output_at(pd, row->output);
    if (output == NULL) {
      return;
    }

    switch (row->control) {
    case DISPLAY_CONTROL_MODES:
      pd->view = DISPLAY_VIEW_MODES;
      return;
    case DISPLAY_CONTROL_SCALE:
      pd->view = DISPLAY_VIEW_SCALE;
      return;
    case DISPLAY_CONTROL_TRANSFORM:
      pd->view = DISPLAY_VIEW_TRANSFORM;
      return;
    case DISPLAY_CONTROL_POSITION:
      pd->view = DISPLAY_VIEW_POSITION;
      return;

    case DISPLAY_CONTROL_PREFERRED: {
      char *name = g_strdup(output->name);
      gboolean ok = display_apply(pd, name, "--preferred", NULL);
      display_set_status(pd, ok ? "%s is on its preferred mode."
                                : "%s refused its preferred mode.",
                         name);
      g_free(name);
      return;
    }

    case DISPLAY_CONTROL_ADAPTIVE: {
      char *name = g_strdup(output->name);
      const gboolean on = output->adaptive_sync;
      gboolean ok = display_apply(pd, name, "--adaptive-sync",
                                  on ? "disabled" : "enabled");
      display_set_status(pd, ok ? "Adaptive sync %s on %s."
                                : "Could not change adaptive sync on %s.",
                         ok ? (on ? "off" : "on") : name, name);
      g_free(name);
      return;
    }

    case DISPLAY_CONTROL_ENABLED: {
      char *name = g_strdup(output->name);
      const gboolean on = output->enabled;
      gboolean ok = display_apply(pd, name, on ? "--off" : "--on", NULL);
      display_set_status(pd, ok ? "%s is %s." : "The compositor refused to "
                                                "turn %s %s.",
                         name, on ? "off" : "on");
      g_free(name);
      return;
    }

    case DISPLAY_CONTROL_BRIGHTNESS:
      if (!output->internal || !pd->have_backlight || pd->brightness < 0) {
        display_set_status(pd, "No backlight to change on %s.", output->name);
        return;
      }
      /* Enter cycles rather than opening a fifth picker: brightness is a
       * single number people nudge, and a list of twenty percentages would be
       * a worse way to say "a bit dimmer" than the two keys already bound. */
      {
        int next = pd->brightness >= 100 ? 10 : pd->brightness + 25;
        next = CLAMP(next, 5, 100);
        if (display_set_brightness(pd, next)) {
          display_set_status(pd, "Brightness %d%%.", pd->brightness);
        } else {
          display_set_status(pd, "Could not set the brightness.");
        }
      }
      return;
    }
    return;
  }

  case DISPLAY_ROW_INFO:
  default:
    return;
  }
}

/* ------------------------------------------------------------------- mode */

static int display_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  DisplayModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->outputs = g_ptr_array_new_with_free_func(display_output_free);
  pd->rows = g_ptr_array_new_with_free_func(display_row_free);
  pd->view = DISPLAY_VIEW_OUTPUTS;
  pd->focus = -1;
  pd->brightness = -1;
  mode_set_private_data(sw, (void *)pd);

  char *found = g_find_program_in_path("wlr-randr");
  pd->have_randr = found != NULL;
  g_free(found);

  /* Both halves matter: the tool without the device cannot write, and the
   * device without the tool cannot be reached from here. */
  found = g_find_program_in_path("backlight");
  pd->have_backlight =
      found != NULL && g_file_test(DISPLAY_BACKLIGHT_DEV, G_FILE_TEST_EXISTS);
  g_free(found);

  display_reload(pd);
  display_refresh(pd);

  /* Action purpose: TRUE even with nothing found. The single explanatory row
   * says which of the two situations this is, and that is more use than an
   * error dialog -- refusing to open would hide the only text that explains
   * why there is nothing to show. */
  return TRUE;
}

static unsigned int display_mode_get_num_entries(const Mode *sw) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  return pd->rows->len;
}

/**
 * Function purpose: draw one row as label, value and note.
 *
 * Action purpose: the value sits in a fixed-width span so that the values line
 * up into a column down the pane -- which is the column the eye actually scans
 * on this surface. Pango has no table, and this is what stands in for one
 * without pulling a layout engine into a menu.
 */
static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);
  DisplayRow *row = display_row_at(pd, selected_line);

  if (row == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  if (row->active) {
    *state |= ACTIVE;
  }
  if (row->dim) {
    *state |= URGENT;
  }

  if (!get_entry) {
    return NULL;
  }

  *state |= MARKUP;

  char *label = g_markup_escape_text(row->label, -1);
  char *value =
      row->value != NULL ? g_markup_escape_text(row->value, -1) : NULL;
  char *note = row->note != NULL ? g_markup_escape_text(row->note, -1) : NULL;
  char *text = NULL;

  if (value != NULL) {
    text = g_strdup_printf(
        "<b>%s</b>  <tt>%s</tt>%s<span alpha='55%%'>%s</span>", label, value,
        note != NULL ? "  " : "", note != NULL ? note : "");
  } else {
    text = g_strdup_printf("<b>%s</b>%s<span alpha='55%%'>%s</span>", label,
                           note != NULL ? "  " : "",
                           note != NULL ? note : "");
  }

  g_free(label);
  g_free(value);
  g_free(note);

  return text;
}

/** The breadcrumb shown in the input bar's prompt. */
static char *display_mode_preprocess_input(Mode *sw,
                                           G_GNUC_UNUSED const char *input) {
  DisplayModePrivateData *pd =
      (DisplayModePrivateData *)mode_get_private_data(sw);
  const DisplayOutput *output =
      pd != NULL ? display_output_at(pd, pd->focus) : NULL;

  if (pd == NULL) {
    return NULL;
  }

  const char *name = output != NULL ? output->name : "Displays";

  switch (pd->view) {
  case DISPLAY_VIEW_MODES:
    return g_strdup_printf("%s / Resolution", name);
  case DISPLAY_VIEW_SCALE:
    return g_strdup_printf("%s / Scale", name);
  case DISPLAY_VIEW_TRANSFORM:
    return g_strdup_printf("%s / Rotation", name);
  case DISPLAY_VIEW_POSITION:
    return g_strdup_printf("%s / Position", name);
  case DISPLAY_VIEW_OUTPUT:
    return g_strdup(name);
  case DISPLAY_VIEW_OUTPUTS:
  default:
    return g_strdup("Displays");
  }
}

static char *display_mode_get_message(const Mode *sw) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return NULL;
  }

  if (pd->status != NULL) {
    return g_markup_printf_escaped("%s", pd->status);
  }

  if (!pd->have_randr) {
    return g_markup_printf_escaped(
        "wlr-randr is not installed — it is what speaks output management to "
        "the compositor");
  }
  if (!pd->answered) {
    return g_markup_printf_escaped(
        "wlr-randr reported no outputs — the compositor may not advertise "
        "output management");
  }

  if (pd->view == DISPLAY_VIEW_OUTPUTS) {
    return g_markup_printf_escaped(
        "Enter opens a display  ·  Alt+1 dimmer  ·  Alt+2 brighter  ·  "
        "Alt+3 reload  ·  %u output%s",
        pd->outputs->len, pd->outputs->len == 1 ? "" : "s");
  }

  return g_markup_printf_escaped(
      "Enter applies  ·  .. or Alt+4 goes back  ·  Alt+1 dimmer  ·  "
      "Alt+2 brighter  ·  Alt+3 reload");
}

static ModeMode display_mode_result(Mode *sw, int mretv,
                                    G_GNUC_UNUSED char **input,
                                    unsigned int selected_line) {
  DisplayModePrivateData *pd =
      (DisplayModePrivateData *)mode_get_private_data(sw);

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

  DisplayRow *row = display_row_at(pd, selected_line);

  if (mretv & MENU_OK) {
    if (row != NULL) {
      display_activate(pd, row);
      display_refresh(pd);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    switch (custom) {
    /* kb-custom-1 / kb-custom-2: brightness. Bound at every level rather than
     * only on the brightness row, because "a bit dimmer" is something you want
     * while looking at anything, and hunting for a specific row first would
     * make two keys behave like five. */
    case 0:
    case 1: {
      if (!pd->have_backlight || pd->brightness < 0) {
        display_set_status(pd, "No backlight on this machine.");
        return RELOAD_DIALOG;
      }
      const int step =
          custom == 0 ? -DISPLAY_BRIGHTNESS_STEP : DISPLAY_BRIGHTNESS_STEP;
      if (display_set_brightness(pd, pd->brightness + step)) {
        display_set_status(pd, "Brightness %d%%.", pd->brightness);
      } else {
        display_set_status(pd, "Could not change the brightness.");
      }
      display_refresh(pd);
      return RELOAD_DIALOG;
    }

    /* kb-custom-3: re-read. Hot-plugging a monitor while this is open is the
     * obvious case, and nothing else here would notice it. */
    case 2:
      display_reload(pd);
      display_refresh(pd);
      display_set_status(pd, "Reloaded — %u output%s.", pd->outputs->len,
                         pd->outputs->len == 1 ? "" : "s");
      return RELOAD_DIALOG;

    /* kb-custom-4: back up a level, for people who reach for a key rather than
     * the `..` row. */
    case 3:
      if (pd->view == DISPLAY_VIEW_OUTPUTS) {
        return MODE_EXIT;
      }
      pd->view = pd->view == DISPLAY_VIEW_OUTPUT ? DISPLAY_VIEW_OUTPUTS
                                                 : DISPLAY_VIEW_OUTPUT;
      if (pd->view == DISPLAY_VIEW_OUTPUTS) {
        pd->focus = -1;
      }
      display_refresh(pd);
      return RELOAD_DIALOG;

    default:
      return (ModeMode)custom;
    }
  }

  return MODE_EXIT;
}

static void display_mode_destroy(Mode *sw) {
  DisplayModePrivateData *pd =
      (DisplayModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_ptr_array_free(pd->rows, TRUE);
    g_ptr_array_free(pd->outputs, TRUE);
    g_free(pd->status);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int display_token_match(const Mode *sw, sofi_int_matcher **tokens,
                               unsigned int index) {
  const DisplayModePrivateData *pd =
      (const DisplayModePrivateData *)mode_get_private_data(sw);
  DisplayRow *row = display_row_at(pd, index);

  if (row == NULL) {
    return FALSE;
  }

  /* Match the value as well as the label: in the mode list the label is a
   * resolution and the value is a refresh rate, and filtering on "60" is a
   * thing someone will reasonably try. */
  if (helper_token_match(tokens, row->label)) {
    return TRUE;
  }
  if (row->value != NULL && helper_token_match(tokens, row->value)) {
    return TRUE;
  }

  return FALSE;
}

Mode display_mode = {.name = "display",
                     .cfg_name_key = "display-display",
                     ._init = display_mode_init,
                     ._get_num_entries = display_mode_get_num_entries,
                     ._result = display_mode_result,
                     ._destroy = display_mode_destroy,
                     ._token_match = display_token_match,
                     ._get_display_value = _get_display_value,
                     ._get_icon = NULL,
                     ._get_completion = NULL,
                     ._preprocess_input = display_mode_preprocess_input,
                     ._get_message = display_mode_get_message,
                     .private_data = NULL,
                     .free = NULL,
                     .type = MODE_TYPE_SWITCHER};

#endif // DISPLAY_MODE
