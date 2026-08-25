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
 * @brief Sheet switcher for hikari-sakura.
 *
 * hikari divides each workspace into ten fixed sheets, 0-9, permanently bound
 * to their output. That model is entirely internal to the compositor: the
 * foreign-toplevel protocols have no notion of a workspace at all, and
 * ext-workspace-v1 -- which hikari does not implement -- could not express
 * moving a window to a sheet even if it did. So this mode does not speak a
 * Wayland protocol. It speaks to hikari's control socket, which exists for
 * exactly this purpose. See hikari-sakura include/hikari/ipc.h.
 *
 * The socket is request/response and one exchange per connection:
 *
 *   -> state            <- sheet 3
 *                          output eDP-1
 *                          counts 2 0 1 0 0 0 0 0 4 0
 *                          END
 *   -> sheet 7          <- ok
 *   -> pin 7            <- ok
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Sheets"

#include "config.h"

#ifdef SHEETS_MODE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <glib.h>

#include "helper.h"
#include "modes/sheets.h"
#include "settings.h"
#include "sofi.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** hikari has exactly this many sheets per workspace and the number is fixed
 * in the compositor (HIKARI_NR_OF_SHEETS). Sheets cannot be created or
 * destroyed, so the list length is a constant rather than a query result. */
#define SHEETS_COUNT 10

/** A response is four short lines. */
#define SHEETS_REPLY_MAX 1024

typedef struct {
  /** Sheet currently displayed, or -1 when the compositor did not say. */
  int current;
  /** Number of views on each sheet. */
  int counts[SHEETS_COUNT];
  /** Output the sheets belong to, for the prompt. NULL when unknown. */
  char *output;
} SheetsModePrivateData;

/**
 * Build the control socket path.
 *
 * @returns a newly allocated path, or NULL when there is no runtime dir.
 */
static char *sheets_socket_path(void) {
  const char *runtime_dir = g_get_user_runtime_dir();

  if (runtime_dir == NULL) {
    return NULL;
  }
  return g_build_filename(runtime_dir, "hikari.sock", NULL);
}

/**
 * Send one request and read the whole reply.
 *
 * Action purpose: this runs on sofi's main thread, so a compositor that
 * accepted the connection and then stopped answering would hang the menu.
 * Both directions get a receive timeout; failing fast with an empty pane beats
 * a frozen one.
 *
 * @returns the reply, newly allocated, or NULL on any failure.
 */
static char *sheets_request(const char *request) {
  char *path = sheets_socket_path();

  if (path == NULL) {
    g_warning("XDG_RUNTIME_DIR is unset; cannot reach the hikari control "
              "socket.");
    return NULL;
  }

  struct sockaddr_un addr = {0};
  addr.sun_family = AF_UNIX;
  if (g_strlcpy(addr.sun_path, path, sizeof(addr.sun_path)) >=
      sizeof(addr.sun_path)) {
    g_warning("Control socket path is too long: %s", path);
    g_free(path);
    return NULL;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);

  if (fd < 0) {
    g_warning("Could not create a socket: %s", g_strerror(errno));
    g_free(path);
    return NULL;
  }

  struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    /* Action purpose: separate the two failures, because they mean opposite
     * things to whoever is reading the message. ENOENT is "this compositor
     * does not serve a control socket" -- any compositor that is not
     * hikari-sakura, or one too old to have one. ECONNREFUSED is "the socket
     * file is there but nothing is listening", which is a stale node from a
     * compositor that did not get to unlink it. Reporting the second as the
     * first sends the reader looking for a version problem they do not have. */
    if (errno == ECONNREFUSED) {
      g_warning("The hikari control socket at %s is stale -- the file exists "
                "but no compositor is listening. It is removed and recreated "
                "when hikari next starts.",
                path);
    } else {
      g_warning("Could not reach the hikari control socket at %s: %s. Is this "
                "hikari-sakura, and is it new enough to serve one?",
                path, g_strerror(errno));
    }
    close(fd);
    g_free(path);
    return NULL;
  }
  g_free(path);

  size_t len = strlen(request);
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, request + off, len - off);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    g_warning("Could not write to the hikari control socket: %s",
              g_strerror(errno));
    close(fd);
    return NULL;
  }

  /* Action purpose: half-close so the compositor sees end-of-request without
   * waiting. The protocol is one exchange per connection and the server reads
   * until a newline, so this is belt and braces today -- but it makes the
   * request boundary explicit rather than dependent on the server's parser. */
  if (shutdown(fd, SHUT_WR) < 0 && errno != ENOTCONN) {
    g_debug("Could not half-close the control socket: %s", g_strerror(errno));
  }

  GString *reply = g_string_sized_new(256);
  char buf[256];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
      /* Action purpose: bound the reply. The peer is the compositor, but a
       * bug there should not become unbounded growth here. */
      if (reply->len + (size_t)n > SHEETS_REPLY_MAX) {
        g_warning("Reply from the hikari control socket is too long.");
        g_string_free(reply, TRUE);
        close(fd);
        return NULL;
      }
      g_string_append_len(reply, buf, n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    /* Action purpose: SO_RCVTIMEO surfaces as EAGAIN, and a timeout after the
     * server has already answered is not a failure -- discarding a complete
     * reply because the peer was slow to close would turn a working exchange
     * into an empty pane. Keep what arrived; only report a hard error when
     * nothing did. */
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (reply->len == 0) {
        g_warning("Timed out waiting for the hikari control socket.");
        g_string_free(reply, TRUE);
        close(fd);
        return NULL;
      }
      break;
    }
    if (n < 0) {
      g_warning("Could not read from the hikari control socket: %s",
                g_strerror(errno));
      g_string_free(reply, TRUE);
      close(fd);
      return NULL;
    }
    break;
  }
  close(fd);

  return g_string_free(reply, FALSE);
}

/**
 * Parse a `state` reply into the private data.
 *
 * Unrecognised lines are ignored rather than treated as errors, so the
 * compositor can add fields without breaking an older sofi.
 */
static void sheets_parse_state(SheetsModePrivateData *pd, const char *reply) {
  char **lines = g_strsplit(reply, "\n", 0);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (g_str_has_prefix(lines[i], "sheet ")) {
      pd->current = (int)g_ascii_strtoll(lines[i] + 6, NULL, 10);
    } else if (g_str_has_prefix(lines[i], "output ")) {
      g_free(pd->output);
      pd->output = g_strdup(lines[i] + 7);
    } else if (g_str_has_prefix(lines[i], "counts ")) {
      char **fields = g_strsplit_set(lines[i] + 7, " ", 0);
      unsigned int slot = 0;
      for (unsigned int f = 0; fields[f] != NULL && slot < SHEETS_COUNT; f++) {
        if (fields[f][0] == '\0') {
          continue;
        }
        pd->counts[slot++] = (int)g_ascii_strtoll(fields[f], NULL, 10);
      }
      g_strfreev(fields);
    }
  }

  g_strfreev(lines);
}

/** Ask the compositor for the current state, replacing what we hold. */
static gboolean sheets_refresh(SheetsModePrivateData *pd) {
  char *reply = sheets_request("state\n");

  if (reply == NULL) {
    return FALSE;
  }
  if (g_str_has_prefix(reply, "error ")) {
    g_warning("hikari refused the state request: %s", reply + 6);
    g_free(reply);
    return FALSE;
  }

  pd->current = -1;
  memset(pd->counts, 0, sizeof(pd->counts));
  sheets_parse_state(pd, reply);
  g_free(reply);

  return TRUE;
}

/** Send a command that returns ok/error, and report which. */
static gboolean sheets_command(const char *verb, unsigned int nr) {
  char *request = g_strdup_printf("%s %u\n", verb, nr);
  char *reply = sheets_request(request);
  g_free(request);

  if (reply == NULL) {
    return FALSE;
  }

  gboolean ok = g_str_has_prefix(reply, "ok");
  if (!ok) {
    g_warning("hikari refused '%s %u': %s", verb, nr, reply);
  }
  g_free(reply);

  return ok;
}

static int sheets_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  SheetsModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->current = -1;
  mode_set_private_data(sw, (void *)pd);

  /* Action purpose: propagate the failure. A sheet pane that cannot reach the
   * compositor should say so, not present ten rows that do nothing when
   * chosen. sofi turns FALSE here into a visible error. */
  if (!sheets_refresh(pd)) {
    return FALSE;
  }

  return TRUE;
}

static unsigned int sheets_mode_get_num_entries(G_GNUC_UNUSED const Mode *sw) {
  return SHEETS_COUNT;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  SheetsModePrivateData *pd =
      (SheetsModePrivateData *)mode_get_private_data(sw);

  g_return_val_if_fail(pd != NULL, NULL);

  if (selected_line >= SHEETS_COUNT) {
    return get_entry ? g_strdup("") : NULL;
  }

  int count = pd->counts[selected_line];

  if ((int)selected_line == pd->current) {
    *state |= ACTIVE;
  } else if (count == 0) {
    /* Empty and not displayed: nothing to go to. */
    *state |= URGENT;
  }

  if (!get_entry) {
    return NULL;
  }

  /* Action purpose: one shape for every sheet, occupied or not, because the
   * pane renders as a horizontal row of chips whose widths follow their own
   * content. Two shapes would mean the chip positions moved whenever a window
   * changed sheet, and a row the user aims at with the pointer has to stay put
   * between invocations. The count is shown rather than suppressed for the same
   * reason -- "0" occupies exactly the width any other digit would, and the
   * empty state is already carried by URGENT above.
   *
   * The word "Sheet" is gone with the vertical pane it was written for: ten
   * chips under the top bar have no room for it, and the row is unambiguous
   * without it. */
  return g_strdup_printf("%u · %d", selected_line, count);
}

static ModeMode sheets_mode_result(Mode *sw, int mretv,
                                   G_GNUC_UNUSED char **input,
                                   unsigned int selected_line) {
  SheetsModePrivateData *pd =
      (SheetsModePrivateData *)mode_get_private_data(sw);

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

  if (mretv & MENU_OK) {
    if (selected_line >= SHEETS_COUNT) {
      return RELOAD_DIALOG;
    }
    sofi_view_hide();
    sheets_command("sheet", selected_line);
    return MODE_EXIT;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    /* Action purpose: kb-custom-1 sends the focused window to the highlighted
     * sheet. This is the operation no Wayland protocol can express, which is
     * most of why the control socket exists. The pane stays up and reloads so
     * the counts reflect the move. */
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);
    if (custom == 0) {
      if (selected_line >= SHEETS_COUNT) {
        return RELOAD_DIALOG;
      }
      if (sheets_command("pin", selected_line)) {
        sheets_refresh(pd);
      }
      return RELOAD_DIALOG;
    }
    return (ModeMode)custom;
  }

  return MODE_EXIT;
}

static void sheets_mode_destroy(Mode *sw) {
  SheetsModePrivateData *pd =
      (SheetsModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_free(pd->output);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int sheets_token_match(const Mode *sw, sofi_int_matcher **tokens,
                              unsigned int index) {
  SheetsModePrivateData *pd =
      (SheetsModePrivateData *)mode_get_private_data(sw);

  g_return_val_if_fail(pd != NULL, FALSE);

  if (index >= SHEETS_COUNT) {
    return FALSE;
  }

  char *entry = g_strdup_printf("Sheet %u", index);
  int match = helper_token_match(tokens, entry);
  g_free(entry);

  return match;
}

Mode sheets_mode = {.name = "sheets",
                    .cfg_name_key = "display-sheets",
                    ._init = sheets_mode_init,
                    ._get_num_entries = sheets_mode_get_num_entries,
                    ._result = sheets_mode_result,
                    ._destroy = sheets_mode_destroy,
                    ._token_match = sheets_token_match,
                    ._get_display_value = _get_display_value,
                    ._get_icon = NULL,
                    ._get_completion = NULL,
                    ._preprocess_input = NULL,
                    ._get_message = NULL,
                    .private_data = NULL,
                    .free = NULL,
                    .type = MODE_TYPE_SWITCHER};

#endif // SHEETS_MODE
