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
 * @brief Network management.
 *
 * Script function and purpose: one summoned surface carrying everything a
 * session needs to do to its network -- scan and switch wireless networks,
 * turn the radio on and off, bring interfaces up and down, renew a DHCP lease,
 * re-associate with the access point, and restart every controller.
 *
 * Sofi links no network library and gains no build dependency from this mode.
 * Two backends are driven as subprocesses and chosen at runtime by which one
 * actually answers:
 *
 *   nmcli    NetworkManager, where it is installed AND running. Preferred
 *            there because it owns the interfaces -- driving ifconfig behind
 *            its back would fight it, and it does its own privilege escalation
 *            through polkit, so nothing here needs to.
 *   base     ifconfig, wpa_cli, dhclient and service. The FreeBSD-native path,
 *            and the one that needs no package installed at all.
 *
 * Executing a binary is not linking. NetworkManager is GPL and is never linked,
 * loaded or vendored by any path here -- see AGENTS.md §2, ruled 2026-09-09.
 *
 * ---------------------------------------------------------------------------
 * PRIVILEGE
 *
 * Most verbs on the base-system backend need root: `ifconfig up`, `dhclient`,
 * `service netif restart`. **Sofi installs nothing setuid, writes no sudoers
 * rule and creates no group.** A verb that needs privilege is run through
 * `network-privilege-command` when one is configured, and run directly when it
 * is not -- in which case it fails the way any unprivileged command fails, and
 * the mode says which option would fix it rather than failing silently.
 *
 * The nmcli backend never uses that prefix: NetworkManager escalates through
 * polkit on its own, and prefixing it would break the polkit session it needs.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Network"

#include "config.h"

#ifdef NETWORK_MODE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "helper.h"
#include "modes/network.h"
#include "settings.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** Signal strength cells drawn on a wireless row. */
#define NET_BAR_CELLS 4

/** What a row is, which decides what Enter does to it. */
typedef enum {
  /** A fixed verb: the radio toggle, a renewal, a reset. */
  NET_ROW_ACTION,
  /** An interface. Enter brings it up or takes it down. */
  NET_ROW_INTERFACE,
  /** A wireless network in range. Enter joins it. */
  NET_ROW_NETWORK,
  /** A heading. Not selectable in any useful sense; Enter reloads. */
  NET_ROW_HEADING,
} NetRowKind;

/** Which verb an ::NET_ROW_ACTION row carries. */
typedef enum {
  NET_ACTION_WIFI_TOGGLE,
  NET_ACTION_DHCP_RENEW,
  NET_ACTION_RECONNECT,
  NET_ACTION_RESET_ALL,
} NetAction;

typedef struct {
  NetRowKind kind;
  NetAction action;

  /** Backend-native handle, passed back verbatim: an interface name, an SSID,
   * or NULL on an action row. */
  char *id;
  /** What the row says. */
  char *label;
  /** Secondary text -- an address, a security type, a state. May be NULL. */
  char *detail;

  /** 0-100. Wireless rows only. */
  int signal;
  gboolean secured;
  /** Already configured, so joining it needs no credential. */
  gboolean known;
  /** Connected, up, or on. */
  gboolean active;
  /** Wireless, for an interface row. */
  gboolean wireless;
} NetRow;

typedef struct _NetBackend NetBackend;

struct _NetBackend {
  const char *name;
  const char *binary;

  /** Build the whole row list. TRUE when it produced anything. */
  gboolean (*list)(GPtrArray *rows, const char *wifi_iface);
  /** Bring an interface up or down. */
  gboolean (*set_iface)(const char *iface, gboolean up);
  /** Join a network. @p psk is NULL for open and already-configured networks. */
  gboolean (*join)(const char *wifi_iface, const NetRow *row, const char *psk);
  /** Turn the wireless radio on or off. */
  gboolean (*radio)(const char *wifi_iface, gboolean on);
  /** Renew the DHCP lease. */
  gboolean (*dhcp_renew)(const char *iface);
  /** Re-associate with the access point / bounce the link. */
  gboolean (*reconnect)(const char *wifi_iface);
  /** Restart every controller. */
  gboolean (*reset_all)(void);
  /** Ask for a fresh scan. */
  gboolean (*rescan)(const char *wifi_iface);
  /** Remove a saved network, so a bad entry can be got rid of. */
  gboolean (*forget)(const char *wifi_iface, const NetRow *row);
};

typedef struct {
  const NetBackend *backend;
  /** NetRow*, owned. */
  GPtrArray *rows;
  /** First wireless interface found, or NULL. Owned. */
  char *wifi_iface;
  /** Last thing that happened, for the message bar. Owned, may be NULL. */
  char *status;
} NetworkModePrivateData;

static void net_row_free(gpointer data) {
  NetRow *row = data;

  if (row == NULL) {
    return;
  }
  g_free(row->id);
  g_free(row->label);
  g_free(row->detail);
  g_free(row);
}

/* --------------------------------------------------------------- process */

/**
 * Function purpose: run one command and hand back its stdout.
 *
 * Action purpose: a non-zero exit is reported at debug level only, because the
 * callers use failure as a probe result -- "this backend is not the one", "this
 * interface has no scan results". Warning here would put a line on stderr every
 * time the menu opened on a machine without NetworkManager.
 *
 * KNOWN LIMIT, the same one the volume mode carries: this is synchronous on
 * sofi's main thread and g_spawn_sync has no timeout, so a tool that accepts the
 * request and never answers holds the menu until it does. A wireless scan is the
 * slowest thing here and takes a second or two.
 */
static gboolean net_run(const char *const *argv, char **out) {
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

/**
 * Function purpose: run one command, feeding it @p input on stdin.
 *
 * Action purpose: this exists for exactly one reason -- **a wireless key must
 * never appear in a child's argv.** Anything in argv is world-readable through
 * `ps` and `/proc/<pid>/cmdline` for as long as the process lives, so passing a
 * key that way hands it to every other user on the machine. Feeding it on stdin
 * keeps it in a pipe that only the two processes can see.
 *
 * glib has no `g_spawn_sync` variant that writes to the child, so the child is
 * spawned with pipes and reaped here. `G_SPAWN_DO_NOT_REAP_CHILD` is required
 * for waitpid() to be allowed to see it.
 *
 * @param input written to the child's stdin, which is then closed. May be NULL.
 * @param out   stdout, newly allocated, or NULL when not wanted.
 */
static gboolean net_run_stdin(const char *const *argv, const char *input,
                              char **out) {
  GPid pid = 0;
  gint in_fd = -1;
  gint out_fd = -1;
  GError *error = NULL;

  if (out != NULL) {
    *out = NULL;
  }

  if (!g_spawn_async_with_pipes(
          NULL, (gchar **)argv, NULL,
          G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL, &pid,
          &in_fd, out != NULL ? &out_fd : NULL, NULL, &error)) {
    g_debug("Could not run %s: %s", argv[0], error->message);
    g_error_free(error);
    return FALSE;
  }

  if (input != NULL && in_fd >= 0) {
    size_t len = strlen(input);
    size_t off = 0;
    while (off < len) {
      ssize_t n = write(in_fd, input + off, len - off);
      if (n > 0) {
        off += (size_t)n;
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
  }
  if (in_fd >= 0) {
    /* The child waits for end-of-input; without this close it never exits. */
    close(in_fd);
  }

  GString *captured = NULL;
  if (out_fd >= 0) {
    captured = g_string_sized_new(256);
    char buf[256];
    for (;;) {
      ssize_t n = read(out_fd, buf, sizeof(buf));
      if (n > 0) {
        g_string_append_len(captured, buf, n);
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
    close(out_fd);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    /* retry */
  }
  g_spawn_close_pid(pid);

  gboolean ok = g_spawn_check_wait_status(status, &error);
  if (!ok) {
    g_debug("%s exited non-zero: %s", argv[0], error->message);
    g_error_free(error);
  }

  if (captured != NULL) {
    if (ok && out != NULL) {
      *out = g_string_free(captured, FALSE);
    } else {
      g_string_free(captured, TRUE);
    }
  }

  return ok;
}

/**
 * Function purpose: run a command that changes system state, through the
 * configured privilege command when there is one.
 *
 * Action purpose: `network-privilege-command` is parsed with
 * g_shell_parse_argv so it can carry its own arguments -- `doas`, `sudo -n`,
 * `sudo` -- and the words are prepended to argv. It is deliberately empty by
 * default: choosing how a machine escalates privilege is an administrator's
 * decision, and guessing at `sudo` would be sofi making it for them.
 *
 * When the command fails and no prefix is configured, the warning names the
 * option. That is the difference between "the button does nothing" and "the
 * button needs a line of configuration", and the user should not have to read
 * the source to tell them apart.
 */
static gboolean net_run_priv(const char *const *argv) {
  const char *prefix = config.network_privilege_command;

  if (prefix == NULL || *prefix == '\0') {
    if (net_run(argv, NULL)) {
      return TRUE;
    }
    g_warning("`%s` failed, and most network changes need privilege. Set "
              "`network-privilege-command` in ~/.config/sofi/config.sasi -- "
              "for example \"doas\" or \"sudo -n\" -- so sofi can run it with "
              "the rights it needs. sofi installs nothing setuid.",
              argv[0]);
    return FALSE;
  }

  gchar **prefix_argv = NULL;
  GError *error = NULL;
  if (!g_shell_parse_argv(prefix, NULL, &prefix_argv, &error)) {
    g_warning("`network-privilege-command` is not parseable as a command: %s",
              error->message);
    g_error_free(error);
    return FALSE;
  }

  GPtrArray *full = g_ptr_array_new();
  for (unsigned int i = 0; prefix_argv[i] != NULL; i++) {
    g_ptr_array_add(full, prefix_argv[i]);
  }
  for (unsigned int i = 0; argv[i] != NULL; i++) {
    g_ptr_array_add(full, (gpointer)argv[i]);
  }
  g_ptr_array_add(full, NULL);

  gboolean ok = net_run((const char *const *)full->pdata, NULL);

  g_ptr_array_free(full, TRUE);
  g_strfreev(prefix_argv);

  if (!ok) {
    g_warning("`%s %s` failed. Check that `network-privilege-command` is "
              "correct and does not need a password on a terminal.",
              prefix, argv[0]);
  }

  return ok;
}

/**
 * Function purpose: ask for a wireless key without ever putting it on screen.
 *
 * Action purpose: sofi already has a masked input -- dmenu mode's `-password`
 * -- so the prompt is another sofi, run as a child, rather than a new widget.
 * `-input /dev/null` gives it an empty list, so the surface is a prompt and
 * nothing else, and the typed text comes back as dmenu's custom-input result.
 *
 * The caller hides this panel first. Two layer surfaces competing for the
 * keyboard is not a state worth entering, and the panel has nothing useful to
 * show while the child owns the screen.
 *
 * @returns the key, newly allocated, or NULL when the prompt was dismissed.
 */
static char *net_prompt_password(const char *ssid) {
  char *prompt = g_strdup_printf("Password for %s", ssid);
  const char *argv[] = {"sofi",   "-dmenu", "-password", "-input",
                        "/dev/null", "-p",  prompt,      NULL};
  char *out = NULL;

  gboolean ok = net_run(argv, &out);
  g_free(prompt);

  if (!ok || out == NULL) {
    g_free(out);
    return NULL;
  }

  /* dmenu terminates its answer with a newline; a key with trailing whitespace
   * is not a thing anyone types deliberately, and passing one on would produce
   * an authentication failure that looks like a wrong password. */
  g_strstrip(out);

  if (*out == '\0') {
    g_free(out);
    return NULL;
  }

  return out;
}

/* ---------------------------------------------------------------- helpers */

static NetRow *net_row_new(GPtrArray *rows, NetRowKind kind) {
  NetRow *row = g_malloc0(sizeof(*row));

  row->kind = kind;
  g_ptr_array_add(rows, row);

  return row;
}

/**
 * Function purpose: turn a dBm reading into a 0-100 quality.
 *
 * Action purpose: the conventional mapping, and the one `wpa_supplicant` and
 * NetworkManager both use: -100 dBm is unusable and -50 dBm is as good as it
 * gets, so quality is `2 * (dBm + 100)` clamped. It exists so the four-cell bar
 * on a row means the same thing whichever backend produced the number.
 */
static int net_dbm_to_quality(int dbm) {
  int quality = 2 * (dbm + 100);

  if (quality < 0) {
    return 0;
  }
  if (quality > 100) {
    return 100;
  }
  return quality;
}

/**
 * Function purpose: append the fixed maintenance verbs.
 *
 * They are last rather than first deliberately: the list above them is what the
 * surface is normally opened for, and a destructive verb should not be what the
 * selection lands on when the panel opens.
 */
static void net_add_actions(GPtrArray *rows, gboolean have_wifi,
                            gboolean radio_on) {
  if (have_wifi) {
    NetRow *row = net_row_new(rows, NET_ROW_ACTION);
    row->action = NET_ACTION_WIFI_TOGGLE;
    row->label = g_strdup(radio_on ? "Turn Wi-Fi off" : "Turn Wi-Fi on");
    row->active = radio_on;
  }

  NetRow *row = net_row_new(rows, NET_ROW_ACTION);
  row->action = NET_ACTION_DHCP_RENEW;
  row->label = g_strdup("Renew DHCP lease");

  row = net_row_new(rows, NET_ROW_ACTION);
  row->action = NET_ACTION_RECONNECT;
  row->label = g_strdup("Reconnect to router");

  row = net_row_new(rows, NET_ROW_ACTION);
  row->action = NET_ACTION_RESET_ALL;
  row->label = g_strdup("Reset all network controllers");
}

/* ------------------------------------------------------------ base system */

/**
 * Function purpose: parse `ifconfig -a` into interface rows.
 *
 * Action purpose: `-a` rather than `-l` because `-l` is a BSD extension and the
 * verbose form carries the state as well as the names, in one call. A block
 * starts at a line with no leading whitespace; everything indented under it
 * describes that interface. Fields are matched by prefix and unrecognised ones
 * ignored, so a field appearing in a later ifconfig costs nothing here.
 *
 * Loopback is skipped: there is no verb on this surface that should ever be
 * pointed at it, and offering "take lo0 down" is offering a way to break the
 * machine.
 *
 * @param wifi_iface_out set to the first wireless interface found. Caller owns.
 */
static void net_base_parse_interfaces(GPtrArray *rows, char **wifi_iface_out,
                                      gboolean *radio_on_out) {
  const char *argv[] = {"ifconfig", "-a", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return;
  }

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  NetRow *row = NULL;
  GString *detail = NULL;

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    const char *line = lines[i];

    if (*line == '\0') {
      continue;
    }

    /* A new block: no indentation, and a name terminated by a colon. */
    if (!g_ascii_isspace(*line)) {
      char *colon = strchr(line, ':');
      if (colon == NULL) {
        row = NULL;
        continue;
      }

      char *name = g_strndup(line, (size_t)(colon - line));
      if (g_strcmp0(name, "lo") == 0 || g_str_has_prefix(name, "lo0")) {
        g_free(name);
        row = NULL;
        continue;
      }

      row = net_row_new(rows, NET_ROW_INTERFACE);
      row->id = name;
      row->label = g_strdup(name);
      row->active = (strstr(line, "UP") != NULL);
      /* Names are the cheap wireless test and the only one available before the
       * media line is reached. The media and ssid lines below correct it. */
      row->wireless = g_str_has_prefix(name, "wlan") ||
                      g_str_has_prefix(name, "wlp") ||
                      g_str_has_prefix(name, "ath");

      /* Action purpose: one GString is reused across blocks, so the previous
       * interface's must be released before this one takes the variable --
       * otherwise every interface after the first leaks one, on every call, and
       * net_refresh() calls this on every reload. */
      if (detail != NULL) {
        g_string_free(detail, TRUE);
      }
      detail = g_string_new(NULL);
      continue;
    }

    if (row == NULL) {
      continue;
    }

    const char *field = line;
    while (g_ascii_isspace(*field)) {
      field++;
    }

    if (g_str_has_prefix(field, "inet ")) {
      const char *addr = field + 5;
      const char *end = addr;
      while (*end != '\0' && !g_ascii_isspace(*end)) {
        end++;
      }
      if (detail->len > 0) {
        g_string_append(detail, "  ");
      }
      g_string_append_len(detail, addr, end - addr);
    } else if (g_str_has_prefix(field, "ssid ")) {
      row->wireless = TRUE;
    } else if (g_str_has_prefix(field, "media:")) {
      if (strstr(field, "802.11") != NULL) {
        row->wireless = TRUE;
      }
    } else if (g_str_has_prefix(field, "status:")) {
      const char *state = field + 7;
      while (g_ascii_isspace(*state)) {
        state++;
      }
      if (detail->len > 0) {
        g_string_append(detail, "  ");
      }
      g_string_append(detail, state);
    }

    /* The block may end at the next iteration, so the detail is attached every
     * time rather than at a boundary that has to be detected. */
    g_free(row->detail);
    row->detail = g_strdup(detail->str);
  }

  /* Action purpose: the first wireless interface that is up decides where every
   * wireless verb is aimed, falling back to the first wireless interface at all
   * so the radio can still be turned on when they are all down. Multi-radio
   * machines are not addressed here and the message bar names which one was
   * chosen. */
  for (guint i = 0; i < rows->len; i++) {
    NetRow *candidate = g_ptr_array_index(rows, i);
    if (candidate->kind != NET_ROW_INTERFACE || !candidate->wireless) {
      continue;
    }
    if (*wifi_iface_out == NULL || candidate->active) {
      g_free(*wifi_iface_out);
      *wifi_iface_out = g_strdup(candidate->id);
      *radio_on_out = candidate->active;
      if (candidate->active) {
        break;
      }
    }
  }

  if (detail != NULL) {
    g_string_free(detail, TRUE);
  }
  g_strfreev(lines);
}

/**
 * Function purpose: find a MAC address in a line and report where it starts.
 *
 * Action purpose: `ifconfig list scan` puts the SSID in a fixed-width first
 * column, and an SSID may contain spaces -- so splitting on whitespace corrupts
 * exactly the networks whose names have spaces in them. The BSSID that follows
 * is unambiguous, so it is used as the divider: everything before it is the
 * SSID, everything after it is fields.
 *
 * @returns offset of the MAC, or -1.
 */
static gssize net_find_bssid(const char *line) {
  for (const char *p = line; *p != '\0'; p++) {
    /* xx:xx:xx:xx:xx:xx -- 17 characters, colons at the odd triples. */
    gboolean ok = TRUE;
    for (int i = 0; i < 17; i++) {
      char c = p[i];
      if (c == '\0') {
        return -1;
      }
      if (i % 3 == 2) {
        if (c != ':') {
          ok = FALSE;
          break;
        }
      } else if (!g_ascii_isxdigit(c)) {
        ok = FALSE;
        break;
      }
    }
    if (ok) {
      return p - line;
    }
  }
  return -1;
}

/** Configured networks, so a row can say whether joining it needs a key. */
static GHashTable *net_base_known_networks(const char *wifi_iface) {
  GHashTable *known =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  if (wifi_iface == NULL) {
    return known;
  }

  const char *argv[] = {"wpa_cli", "-i", wifi_iface, "list_networks", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return known;
  }

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    /* `<id>\t<ssid>\t<bssid>\t<flags>`. The header line has no leading digit. */
    if (!g_ascii_isdigit(lines[i][0])) {
      continue;
    }
    char **fields = g_strsplit(lines[i], "\t", 4);
    if (fields[0] != NULL && fields[1] != NULL && fields[1][0] != '\0') {
      g_hash_table_replace(known, g_strdup(fields[1]),
                           GINT_TO_POINTER(atoi(fields[0]) + 1));
    }
    g_strfreev(fields);
  }

  g_strfreev(lines);
  return known;
}

/**
 * Function purpose: parse `ifconfig <if> list scan` into wireless rows.
 *
 * The scan itself is requested separately and is asynchronous in the driver, so
 * the results read here are whatever the last scan produced. That is why the
 * rescan binding exists and why it reloads rather than blocking.
 */
static void net_base_parse_scan(GPtrArray *rows, const char *wifi_iface,
                                const char *current_ssid) {
  if (wifi_iface == NULL) {
    return;
  }

  const char *argv[] = {"ifconfig", wifi_iface, "list", "scan", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return;
  }

  GHashTable *known = net_base_known_networks(wifi_iface);
  GHashTable *seen =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    gssize bssid_at = net_find_bssid(lines[i]);
    if (bssid_at <= 0) {
      continue;
    }

    char *ssid = g_strndup(lines[i], (size_t)bssid_at);
    g_strstrip(ssid);

    if (*ssid == '\0') {
      /* A hidden network. There is nothing to join it by from this surface. */
      g_free(ssid);
      continue;
    }

    /* Action purpose: one row per network, not per access point. A mesh or a
     * repeated SSID appears once per radio in the scan, and three identical
     * rows differing only in a BSSID the user cannot act on is noise. The
     * strongest is kept, because that is the one the driver will pick. */
    if (g_hash_table_contains(seen, ssid)) {
      g_free(ssid);
      continue;
    }

    const char *rest = lines[i] + bssid_at + 17;
    int signal = 0;
    const char *colon = strchr(rest, ':');
    if (colon != NULL) {
      /* `S:N` as `-45:-95`; walk back over the signal figure. */
      const char *start = colon;
      while (start > rest && (g_ascii_isdigit(*(start - 1)) ||
                              *(start - 1) == '-')) {
        start--;
      }
      if (start != colon) {
        signal = net_dbm_to_quality((int)g_ascii_strtoll(start, NULL, 10));
      }
    }

    NetRow *row = net_row_new(rows, NET_ROW_NETWORK);
    row->id = g_strdup(ssid);
    row->label = g_strdup(ssid);
    row->signal = signal;
    row->secured = (strstr(rest, "RSN") != NULL || strstr(rest, "WPA") != NULL);
    row->known = g_hash_table_contains(known, ssid);
    row->active = (current_ssid != NULL && g_strcmp0(current_ssid, ssid) == 0);
    row->detail = g_strdup(row->secured ? "secured" : "open");

    g_hash_table_add(seen, ssid);
  }

  g_strfreev(lines);
  g_hash_table_destroy(known);
  g_hash_table_destroy(seen);
}

/** The SSID currently associated, or NULL. */
static char *net_base_current_ssid(const char *wifi_iface) {
  if (wifi_iface == NULL) {
    return NULL;
  }

  const char *argv[] = {"wpa_cli", "-i", wifi_iface, "status", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return NULL;
  }

  char *ssid = NULL;
  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (g_str_has_prefix(lines[i], "ssid=")) {
      ssid = g_strdup(lines[i] + 5);
      g_strstrip(ssid);
      break;
    }
  }

  g_strfreev(lines);
  return ssid;
}

static gboolean net_base_list(GPtrArray *rows, const char *wifi_iface) {
  char *found = NULL;
  gboolean radio_on = FALSE;

  net_base_parse_interfaces(rows, &found, &radio_on);

  const char *iface = wifi_iface != NULL ? wifi_iface : found;
  char *current = net_base_current_ssid(iface);

  net_base_parse_scan(rows, iface, current);
  net_add_actions(rows, iface != NULL, radio_on);

  g_free(current);
  g_free(found);

  return rows->len > 0;
}

static gboolean net_base_set_iface(const char *iface, gboolean up) {
  const char *argv[] = {"ifconfig", iface, up ? "up" : "down", NULL};

  return net_run_priv(argv);
}

static gboolean net_base_radio(const char *wifi_iface, gboolean on) {
  /* Action purpose: FreeBSD has no rfkill. Taking the wlan interface down is
   * what "off" means there, and it is what the base system offers. */
  return net_base_set_iface(wifi_iface, on);
}

/** One field out of `wpa_cli status`, newly allocated, or NULL. */
static char *net_base_status_field(const char *wifi_iface, const char *key) {
  const char *argv[] = {"wpa_cli", "-i", wifi_iface, "status", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return NULL;
  }

  char *value = NULL;
  char *prefix = g_strdup_printf("%s=", key);
  char **lines = g_strsplit(out, "\n", 0);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (g_str_has_prefix(lines[i], prefix)) {
      value = g_strstrip(g_strdup(lines[i] + strlen(prefix)));
      break;
    }
  }

  g_strfreev(lines);
  g_free(prefix);
  g_free(out);

  return value;
}

/**
 * Function purpose: re-enable every configured network.
 *
 * Action purpose: this is the single most important call in this file, and the
 * defect it exists to prevent was reported from a real desktop. `select_network`
 * does not merely select -- **it disables every other configured network**. So
 * choosing a network with a mistyped key disabled the one that was working,
 * failed to authenticate on the new one, and left the supplicant with nothing
 * enabled to fall back to. The machine could not rejoin any network without a
 * cable.
 *
 * Every path that calls `select_network` therefore calls this afterwards, on
 * success and on failure alike. A network the user did not ask to disable must
 * never end a join disabled.
 */
static gboolean net_base_enable_all(const char *wifi_iface) {
  const char *argv[] = {"wpa_cli", "-i", wifi_iface, "enable_network", "all",
                        NULL};

  return net_run(argv, NULL);
}

static void net_base_remove_network(const char *wifi_iface, const char *id) {
  const char *argv[] = {"wpa_cli", "-i", wifi_iface, "remove_network", id,
                        NULL};

  net_run(argv, NULL);
}

/**
 * Function purpose: wait until the supplicant has actually joined @p ssid.
 *
 * Action purpose: a wrong key is not reported by any command's exit status --
 * `select_network` succeeds, and the failure happens afterwards in the four-way
 * handshake. Without waiting for the result there is no way to tell a good key
 * from a bad one, and nothing to undo the damage on. So the state is polled
 * until it reaches COMPLETED on the requested network, or the budget runs out.
 *
 * The budget is deliberately short. A four-way handshake that is going to
 * succeed completes in a second or two; one that is going to fail is retried by
 * the supplicant until something gives up, and waiting for that would look like
 * a hang. Ten seconds is long enough for a slow association and short enough
 * that the user does not think the menu has crashed.
 *
 * KNOWN LIMIT: this blocks sofi's main loop for up to the budget. The panel is
 * already hidden by the caller, so there is nothing on screen to freeze, but it
 * is a block and it is stated rather than hidden.
 */
static gboolean net_base_wait_connected(const char *wifi_iface,
                                        const char *ssid) {
  const unsigned int poll_ms = 250;
  const unsigned int budget_ms = 10000;

  for (unsigned int waited = 0; waited < budget_ms; waited += poll_ms) {
    g_usleep(poll_ms * 1000);

    char *state = net_base_status_field(wifi_iface, "wpa_state");
    if (state == NULL) {
      continue;
    }

    gboolean completed = (g_strcmp0(state, "COMPLETED") == 0);
    g_free(state);

    if (!completed) {
      continue;
    }

    /* COMPLETED on the wrong network is not success -- it means the supplicant
     * fell back to something else, which is the good outcome for the user but
     * still a failed join of what they asked for. */
    char *current = net_base_status_field(wifi_iface, "ssid");
    gboolean matched = (current != NULL && g_strcmp0(current, ssid) == 0);
    g_free(current);

    if (matched) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Function purpose: join a network, and undo everything on failure.
 *
 * Action purpose: the contract this function has to keep is that **a failed
 * join leaves the machine exactly as it found it**. A mistyped key must not
 * cost the user the network they were already on. That means three things, and
 * an earlier version of this code did none of them:
 *
 *  1. Nothing is persisted until the association has been *seen* to succeed. A
 *     key that turns out to be wrong is never written to wpa_supplicant.conf.
 *  2. A network added for the attempt is removed again when the attempt fails,
 *     so a failed join leaves no half-made entry behind.
 *  3. Every network `select_network` disabled is re-enabled, whatever the
 *     outcome, and the supplicant is told to re-associate so it returns to
 *     whatever it was on before.
 */
static gboolean net_base_join(const char *wifi_iface, const NetRow *row,
                              const char *psk) {
  if (wifi_iface == NULL) {
    return FALSE;
  }

  /* Already configured: select it and let wpa_supplicant do the rest. No key is
   * read, written or replaced. */
  if (row->known) {
    GHashTable *known = net_base_known_networks(wifi_iface);
    gpointer slot = g_hash_table_lookup(known, row->id);
    gboolean ok = FALSE;

    if (slot != NULL) {
      char *id = g_strdup_printf("%d", GPOINTER_TO_INT(slot) - 1);
      const char *argv[] = {"wpa_cli", "-i",  wifi_iface,
                            "select_network", id, NULL};
      ok = net_run(argv, NULL);
      g_free(id);

      if (ok) {
        ok = net_base_wait_connected(wifi_iface, row->id);
      }
      /* Whatever happened, nothing may be left disabled. */
      net_base_enable_all(wifi_iface);
      if (!ok) {
        const char *again[] = {"wpa_cli", "-i", wifi_iface, "reassociate",
                               NULL};
        net_run(again, NULL);
        g_warning("Could not join %s. Every configured network has been "
                  "re-enabled and the supplicant told to re-associate.",
                  row->id);
      }
    }

    g_hash_table_destroy(known);
    return ok;
  }

  /* A new network. wpa_cli's `add_network` answers with the id it allocated. */
  const char *add_argv[] = {"wpa_cli", "-i", wifi_iface, "add_network", NULL};
  char *out = NULL;
  if (!net_run(add_argv, &out) || out == NULL) {
    return FALSE;
  }

  char *id = g_strstrip(g_strdup(out));
  g_free(out);
  /* The reply can carry the interface banner before the number; the id is the
   * last line and is digits only. */
  char *last = strrchr(id, '\n');
  if (last != NULL) {
    char *trimmed = g_strstrip(g_strdup(last + 1));
    g_free(id);
    id = trimmed;
  }
  if (!g_ascii_isdigit(id[0])) {
    g_warning("wpa_cli did not answer add_network with a network id.");
    g_free(id);
    return FALSE;
  }

  /* Action purpose: wpa_supplicant's config format quotes string values, and
   * wpa_cli passes the argument through unchanged -- so the quotes have to be
   * part of the argument. There is no shell here to add them. */
  char *ssid_value = g_strdup_printf("\"%s\"", row->id);
  const char *ssid_argv[] = {"wpa_cli", "-i", wifi_iface, "set_network",
                             id,        "ssid", ssid_value, NULL};
  gboolean ok = net_run(ssid_argv, NULL);
  g_free(ssid_value);

  if (ok) {
    if (psk != NULL) {
      /* Action purpose: the key goes down wpa_cli's stdin in interactive mode,
       * never into its argv. `wpa_cli -i <if>` with no command reads commands a
       * line at a time, so this is the same `set_network` it would otherwise
       * have been given as arguments -- but a key in argv is readable by every
       * user on the machine through `ps`, and one on a pipe is not.
       *
       * The quotes are still part of the value: wpa_supplicant's config format
       * quotes strings and wpa_cli passes the line through unchanged. */
      char *script =
          g_strdup_printf("set_network %s psk \"%s\"\nquit\n", id, psk);
      const char *psk_argv[] = {"wpa_cli", "-i", wifi_iface, NULL};
      char *reply = NULL;

      ok = net_run_stdin(psk_argv, script, &reply);

      /* Interactive wpa_cli exits zero even when a command failed, so the reply
       * is what says whether the key was accepted. */
      if (ok && reply != NULL && strstr(reply, "FAIL") != NULL) {
        g_warning("wpa_cli refused the key for %s.", row->id);
        ok = FALSE;
      }

      /* Neither copy outlives the call that used it. */
      memset(script, 0, strlen(script));
      g_free(script);
      g_free(reply);
    } else {
      const char *open_argv[] = {"wpa_cli", "-i",       wifi_iface,
                                 "set_network", id,     "key_mgmt",
                                 "NONE",     NULL};
      ok = net_run(open_argv, NULL);
    }
  }

  if (ok) {
    const char *select_argv[] = {"wpa_cli", "-i", wifi_iface, "select_network",
                                 id,        NULL};
    ok = net_run(select_argv, NULL);
  }

  /* The association is what decides whether this worked, not any exit status
   * above -- a wrong key fails here and nowhere else. */
  if (ok) {
    ok = net_base_wait_connected(wifi_iface, row->id);
  }

  if (ok) {
    /* Persist only now: the key has been proven. A supplicant built without
     * `update_config=1` refuses, which costs the user a re-prompt next time and
     * nothing else, so it stays best-effort. */
    const char *save_argv[] = {"wpa_cli", "-i", wifi_iface, "save_config",
                               NULL};
    if (!net_run(save_argv, NULL)) {
      g_debug("wpa_cli save_config refused -- the supplicant's configuration "
              "has no update_config=1, so this network will be asked for "
              "again next time.");
    }
  } else {
    /* Undo, in the order that leaves the machine usable soonest: drop the entry
     * that did not work, re-enable everything select_network disabled, then ask
     * the supplicant to go back to whatever it can reach. */
    net_base_remove_network(wifi_iface, id);
    g_warning("Could not join %s -- most likely a wrong password. The network "
              "was not saved, every other configured network has been "
              "re-enabled, and the supplicant told to re-associate.",
              row->id);
  }

  net_base_enable_all(wifi_iface);

  if (!ok) {
    const char *again[] = {"wpa_cli", "-i", wifi_iface, "reassociate", NULL};
    net_run(again, NULL);
  }

  g_free(id);
  return ok;
}

/**
 * Function purpose: delete a saved network and persist the deletion.
 *
 * Action purpose: this exists because "where did that get saved?" is a fair
 * question to ask of a menu that saves things, and it should be answerable from
 * the menu rather than by editing wpa_supplicant.conf as root. The entries live
 * in the supplicant's own configuration -- the file it was started with, shown
 * by the `-c` argument in `ps` -- and this removes one from both the running
 * supplicant and that file.
 */
static gboolean net_base_forget(const char *wifi_iface, const NetRow *row) {
  if (wifi_iface == NULL) {
    return FALSE;
  }

  GHashTable *known = net_base_known_networks(wifi_iface);
  gpointer slot = g_hash_table_lookup(known, row->id);
  gboolean ok = FALSE;

  if (slot != NULL) {
    char *id = g_strdup_printf("%d", GPOINTER_TO_INT(slot) - 1);
    net_base_remove_network(wifi_iface, id);
    g_free(id);

    const char *save_argv[] = {"wpa_cli", "-i", wifi_iface, "save_config",
                               NULL};
    if (!net_run(save_argv, NULL)) {
      g_warning("%s was removed from the running supplicant, but save_config "
                "refused -- the supplicant's configuration has no "
                "update_config=1, so it will come back on restart.",
                row->id);
    }
    /* Removing renumbers nothing, but it can leave the supplicant with no
     * enabled candidate if the removed one was selected. */
    net_base_enable_all(wifi_iface);
    ok = TRUE;
  }

  g_hash_table_destroy(known);
  return ok;
}

static gboolean net_base_dhcp_renew(const char *iface) {
  if (iface == NULL) {
    g_warning("No interface to renew a lease on.");
    return FALSE;
  }

  const char *argv[] = {"dhclient", iface, NULL};

  return net_run_priv(argv);
}

static gboolean net_base_reconnect(const char *wifi_iface) {
  if (wifi_iface != NULL) {
    const char *argv[] = {"wpa_cli", "-i", wifi_iface, "reassociate", NULL};
    if (net_run(argv, NULL)) {
      return TRUE;
    }
  }
  g_warning("Could not re-associate. On a wired link, take the interface down "
            "and up instead.");
  return FALSE;
}

static gboolean net_base_reset_all(void) {
  /* Action purpose: FreeBSD's rc system restarts every configured interface
   * with this, which is exactly "reset all controllers" and does not need each
   * interface naming. */
  const char *argv[] = {"service", "netif", "restart", NULL};

  return net_run_priv(argv);
}

static gboolean net_base_rescan(const char *wifi_iface) {
  if (wifi_iface == NULL) {
    return FALSE;
  }

  const char *argv[] = {"ifconfig", wifi_iface, "scan", NULL};

  return net_run_priv(argv);
}

/* ------------------------------------------------------------------ nmcli */

/**
 * Function purpose: the SSIDs NetworkManager already has a saved profile for.
 *
 * Action purpose: `known` decides whether the caller prompts for a key, so
 * answering it wrongly has consequences in both directions. An earlier version
 * hardcoded it TRUE on the assumption that `device wifi connect` would prompt on
 * its own -- it does not, it has no terminal to prompt on, so joining a secured
 * network sofi had never seen simply failed with no way to supply the key.
 *
 * A profile's name is not required to equal its SSID, so `802-11-wireless.ssid`
 * is read rather than the connection name: a profile the user renamed still
 * counts as saved.
 */
static GHashTable *net_nmcli_known_networks(void) {
  GHashTable *known =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  const char *argv[] = {"nmcli", "-t", "-f",
                        "802-11-wireless.ssid", "connection", "show", NULL};
  char *out = NULL;

  if (!net_run(argv, &out) || out == NULL) {
    return known;
  }

  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    /* `802-11-wireless.ssid:MyNetwork`, and `--` where the profile is wired. */
    const char *colon = strchr(lines[i], ':');
    if (colon == NULL) {
      continue;
    }
    char *ssid = g_strstrip(g_strdup(colon + 1));
    if (*ssid == '\0' || g_strcmp0(ssid, "--") == 0) {
      g_free(ssid);
      continue;
    }
    g_hash_table_add(known, ssid);
  }

  g_strfreev(lines);
  return known;
}

/** Split one `-t` terse line, honouring nmcli's backslash-escaped colons. */
static char **net_nmcli_split(const char *line) {
  GPtrArray *fields = g_ptr_array_new();
  GString *current = g_string_new(NULL);

  for (const char *p = line; *p != '\0'; p++) {
    if (*p == '\\' && *(p + 1) != '\0') {
      p++;
      g_string_append_c(current, *p);
    } else if (*p == ':') {
      g_ptr_array_add(fields, g_strdup(current->str));
      g_string_truncate(current, 0);
    } else {
      g_string_append_c(current, *p);
    }
  }
  g_ptr_array_add(fields, g_strdup(current->str));
  g_ptr_array_add(fields, NULL);

  g_string_free(current, TRUE);
  return (char **)g_ptr_array_free(fields, FALSE);
}

static gboolean net_nmcli_list(GPtrArray *rows,
                               G_GNUC_UNUSED const char *wifi_iface) {
  const char *dev_argv[] = {"nmcli", "-t", "-f",
                            "DEVICE,TYPE,STATE,CONNECTION", "device", NULL};
  char *out = NULL;

  if (!net_run(dev_argv, &out) || out == NULL) {
    return FALSE;
  }

  gboolean have_wifi = FALSE;
  char **lines = g_strsplit(out, "\n", 0);
  g_free(out);

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (lines[i][0] == '\0') {
      continue;
    }

    char **f = net_nmcli_split(lines[i]);
    if (f[0] == NULL || f[1] == NULL || f[2] == NULL ||
        g_strcmp0(f[1], "loopback") == 0) {
      g_strfreev(f);
      continue;
    }

    NetRow *row = net_row_new(rows, NET_ROW_INTERFACE);
    row->id = g_strdup(f[0]);
    row->label = g_strdup(f[0]);
    row->wireless = (g_strcmp0(f[1], "wifi") == 0);
    row->active = (g_strcmp0(f[2], "connected") == 0);
    row->detail = g_strdup_printf(
        "%s%s%s", f[2], (f[3] != NULL && f[3][0] != '\0') ? "  " : "",
        (f[3] != NULL && f[3][0] != '\0') ? f[3] : "");

    if (row->wireless) {
      have_wifi = TRUE;
    }

    g_strfreev(f);
  }
  g_strfreev(lines);

  gboolean radio_on = FALSE;
  const char *radio_argv[] = {"nmcli", "radio", "wifi", NULL};
  char *radio_out = NULL;
  if (net_run(radio_argv, &radio_out) && radio_out != NULL) {
    radio_on = (strstr(radio_out, "enabled") != NULL);
  }
  g_free(radio_out);

  if (have_wifi && radio_on) {
    const char *wifi_argv[] = {"nmcli",  "-t", "-f",
                               "ACTIVE,SSID,SIGNAL,SECURITY",
                               "device", "wifi", "list", NULL};
    char *wifi_out = NULL;

    if (net_run(wifi_argv, &wifi_out) && wifi_out != NULL) {
      GHashTable *seen =
          g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
      GHashTable *saved = net_nmcli_known_networks();
      char **wifi_lines = g_strsplit(wifi_out, "\n", 0);

      for (unsigned int i = 0; wifi_lines[i] != NULL; i++) {
        if (wifi_lines[i][0] == '\0') {
          continue;
        }
        char **f = net_nmcli_split(wifi_lines[i]);
        if (f[0] == NULL || f[1] == NULL || f[1][0] == '\0' ||
            g_hash_table_contains(seen, f[1])) {
          g_strfreev(f);
          continue;
        }

        NetRow *row = net_row_new(rows, NET_ROW_NETWORK);
        row->id = g_strdup(f[1]);
        row->label = g_strdup(f[1]);
        row->active = (g_strcmp0(f[0], "yes") == 0);
        row->signal = f[2] != NULL ? (int)g_ascii_strtoll(f[2], NULL, 10) : 0;
        row->secured = (f[3] != NULL && f[3][0] != '\0');
        /* True only where NetworkManager actually holds a profile for this
         * SSID. It stores the credential itself, so a saved profile joins with
         * no key from us -- but an unsaved secured network needs one, and
         * `nmcli device wifi connect` has no terminal to ask on. Getting this
         * wrong in that direction is what made such a network unjoinable. */
        row->known = g_hash_table_contains(saved, f[1]);
        row->detail = g_strdup(row->secured ? f[3] : "open");

        g_hash_table_add(seen, g_strdup(f[1]));
        g_strfreev(f);
      }

      g_strfreev(wifi_lines);
      g_hash_table_destroy(seen);
      g_hash_table_destroy(saved);
    }
    g_free(wifi_out);
  }

  net_add_actions(rows, have_wifi, radio_on);

  return rows->len > 0;
}

static gboolean net_nmcli_set_iface(const char *iface, gboolean up) {
  const char *argv[] = {"nmcli", "device", up ? "connect" : "disconnect", iface,
                        NULL};

  return net_run(argv, NULL);
}

/**
 * Function purpose: join a network through NetworkManager.
 *
 * Action purpose: `nmcli ... password <key>` puts the key in argv, where every
 * user on the machine can read it out of `ps` for as long as the command runs.
 * `--passwd-file` takes it from a file instead. The file is created by
 * g_file_open_tmp, which uses mkstemp and so creates it 0600 -- owner-only from
 * the moment it exists, with no window in which it is readable -- and it is
 * overwritten and unlinked as soon as nmcli has finished with it.
 */
static gboolean net_nmcli_join(G_GNUC_UNUSED const char *wifi_iface,
                               const NetRow *row, const char *psk) {
  if (psk == NULL) {
    const char *argv[] = {"nmcli", "device", "wifi", "connect", row->id, NULL};
    return net_run(argv, NULL);
  }

  char *path = NULL;
  GError *error = NULL;
  gint fd = g_file_open_tmp("sofi-network-XXXXXX", &path, &error);

  if (fd < 0) {
    g_warning("Could not create a private file for the wireless key: %s",
              error->message);
    g_error_free(error);
    return FALSE;
  }

  char *contents =
      g_strdup_printf("802-11-wireless-security.psk:%s\n", psk);
  size_t len = strlen(contents);
  size_t off = 0;
  gboolean written = TRUE;

  while (off < len) {
    ssize_t n = write(fd, contents + off, len - off);
    if (n > 0) {
      off += (size_t)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    written = FALSE;
    break;
  }
  close(fd);

  gboolean ok = FALSE;
  if (written) {
    const char *argv[] = {"nmcli",   "device",       "wifi", "connect",
                          row->id,   "--passwd-file", path,  NULL};
    ok = net_run(argv, NULL);
  } else {
    g_warning("Could not write the wireless key to its private file.");
  }

  /* Action purpose: overwrite before unlinking. Unlink alone leaves the key in
   * whatever blocks the file occupied until they are reused. */
  fd = open(path, O_WRONLY);
  if (fd >= 0) {
    memset(contents, 0, len);
    ssize_t ignored = write(fd, contents, len);
    (void)ignored;
    close(fd);
  }
  g_unlink(path);

  memset(contents, 0, len);
  g_free(contents);
  g_free(path);

  return ok;
}

static gboolean net_nmcli_radio(G_GNUC_UNUSED const char *wifi_iface,
                                gboolean on) {
  const char *argv[] = {"nmcli", "radio", "wifi", on ? "on" : "off", NULL};

  return net_run(argv, NULL);
}

static gboolean net_nmcli_dhcp_renew(const char *iface) {
  if (iface == NULL) {
    return FALSE;
  }

  /* `reapply` re-runs the device's configuration, which is the closest thing
   * NetworkManager offers to a lease renewal without dropping the link. */
  const char *argv[] = {"nmcli", "device", "reapply", iface, NULL};

  return net_run(argv, NULL);
}

static gboolean net_nmcli_reconnect(const char *iface) {
  if (iface == NULL) {
    return FALSE;
  }

  const char *down[] = {"nmcli", "device", "disconnect", iface, NULL};
  const char *up[] = {"nmcli", "device", "connect", iface, NULL};

  net_run(down, NULL);
  return net_run(up, NULL);
}

static gboolean net_nmcli_reset_all(void) {
  const char *off[] = {"nmcli", "networking", "off", NULL};
  const char *on[] = {"nmcli", "networking", "on", NULL};

  net_run(off, NULL);
  return net_run(on, NULL);
}

static gboolean net_nmcli_forget(G_GNUC_UNUSED const char *wifi_iface,
                                 const NetRow *row) {
  /* NetworkManager keys a saved profile by connection name, which for a
   * wireless profile it created itself is the SSID. */
  const char *argv[] = {"nmcli", "connection", "delete", row->id, NULL};

  return net_run(argv, NULL);
}

static gboolean net_nmcli_rescan(G_GNUC_UNUSED const char *wifi_iface) {
  const char *argv[] = {"nmcli", "device", "wifi", "rescan", NULL};

  return net_run(argv, NULL);
}

/* ---------------------------------------------------------------- backend */

/** Action purpose: nmcli first, because where NetworkManager is running it owns
 * the interfaces and driving ifconfig behind its back would fight it. The base
 * system is second and always present, so it is the backend that answers when
 * NetworkManager is installed but not running -- which is the common case on
 * FreeBSD and the reason presence on $PATH is not the test. */
static const NetBackend net_backends[] = {
    {.name = "nmcli (NetworkManager)",
     .binary = "nmcli",
     .list = net_nmcli_list,
     .set_iface = net_nmcli_set_iface,
     .join = net_nmcli_join,
     .radio = net_nmcli_radio,
     .dhcp_renew = net_nmcli_dhcp_renew,
     .reconnect = net_nmcli_reconnect,
     .reset_all = net_nmcli_reset_all,
     .rescan = net_nmcli_rescan,
     .forget = net_nmcli_forget},
    {.name = "ifconfig / wpa_cli (base system)",
     .binary = "ifconfig",
     .list = net_base_list,
     .set_iface = net_base_set_iface,
     .join = net_base_join,
     .radio = net_base_radio,
     .dhcp_renew = net_base_dhcp_renew,
     .reconnect = net_base_reconnect,
     .reset_all = net_base_reset_all,
     .rescan = net_base_rescan,
     .forget = net_base_forget},
};

/**
 * Function purpose: the first interface that is up, wired or wireless.
 *
 * Action purpose: for the verbs that are not wireless ideas. Loopback is
 * excluded because renewing a lease on it is meaningless, and the row list is
 * already in enumeration order, so "first up" is the same interface a user
 * would point at.
 *
 * @returns a borrowed pointer into the row list, or NULL. Not owned.
 */
static const char *net_first_active_iface(const NetworkModePrivateData *pd) {
  for (guint i = 0; i < pd->rows->len; i++) {
    const NetRow *row = g_ptr_array_index(pd->rows, i);
    if (row->kind != NET_ROW_INTERFACE || !row->active || row->id == NULL) {
      continue;
    }
    if (g_str_has_prefix(row->id, "lo")) {
      continue;
    }
    return row->id;
  }
  return NULL;
}

/** The wireless interface this invocation aims its wireless verbs at. */
static char *net_find_wifi_iface(GPtrArray *rows) {
  for (guint i = 0; i < rows->len; i++) {
    NetRow *row = g_ptr_array_index(rows, i);
    if (row->kind == NET_ROW_INTERFACE && row->wireless) {
      return g_strdup(row->id);
    }
  }
  return NULL;
}

/**
 * Function purpose: choose the backend and build the first row list together.
 *
 * Action purpose: as with the volume mode, presence on $PATH is necessary and
 * not sufficient -- a backend is accepted only once it has produced a row. That
 * is what lets a machine with the NetworkManager client installed and the
 * daemon stopped fall through to the base system instead of showing nothing.
 */
static const NetBackend *net_select_backend(GPtrArray *rows) {
  for (unsigned int i = 0; i < G_N_ELEMENTS(net_backends); i++) {
    const NetBackend *backend = &net_backends[i];

    char *found = g_find_program_in_path(backend->binary);
    if (found == NULL) {
      g_debug("%s is not on $PATH; trying the next backend.", backend->binary);
      continue;
    }
    g_free(found);

    if (backend->list(rows, NULL)) {
      g_debug("Network backend: %s", backend->name);
      return backend;
    }

    g_debug("%s is installed but reported nothing; trying the next backend.",
            backend->binary);
    g_ptr_array_set_size(rows, 0);
  }

  return NULL;
}

static void net_refresh(NetworkModePrivateData *pd) {
  g_ptr_array_set_size(pd->rows, 0);
  pd->backend->list(pd->rows, pd->wifi_iface);

  if (pd->wifi_iface == NULL) {
    pd->wifi_iface = net_find_wifi_iface(pd->rows);
  }
}

static NetRow *net_row_at(const NetworkModePrivateData *pd, unsigned int line) {
  if (pd == NULL || line >= pd->rows->len) {
    return NULL;
  }
  return g_ptr_array_index(pd->rows, line);
}

static void net_set_status(NetworkModePrivateData *pd, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

static void net_set_status(NetworkModePrivateData *pd, const char *fmt, ...) {
  va_list args;

  g_free(pd->status);
  va_start(args, fmt);
  pd->status = g_strdup_vprintf(fmt, args);
  va_end(args);
}

/* ------------------------------------------------------------------- mode */

static int network_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  NetworkModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->rows = g_ptr_array_new_with_free_func(net_row_free);
  mode_set_private_data(sw, (void *)pd);

  pd->backend = net_select_backend(pd->rows);

  if (pd->backend == NULL) {
    g_warning("No network backend answered. This needs either NetworkManager "
              "running (nmcli) or the base system's ifconfig.");
    return FALSE;
  }

  pd->wifi_iface = net_find_wifi_iface(pd->rows);

  return TRUE;
}

static unsigned int network_mode_get_num_entries(const Mode *sw) {
  const NetworkModePrivateData *pd =
      (const NetworkModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  return pd->rows->len;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const NetworkModePrivateData *pd =
      (const NetworkModePrivateData *)mode_get_private_data(sw);
  NetRow *row = net_row_at(pd, selected_line);

  if (row == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  if (row->active) {
    *state |= ACTIVE;
  }
  /* Action purpose: an interface that is down is styled the way a muted sink
   * and a minimised window are -- present, but not doing its job. One theme
   * rule covers all three. */
  if (row->kind == NET_ROW_INTERFACE && !row->active) {
    *state |= URGENT;
  }

  if (!get_entry) {
    return NULL;
  }

  *state |= MARKUP;

  char *label = g_markup_escape_text(row->label, -1);
  char *detail =
      row->detail != NULL ? g_markup_escape_text(row->detail, -1) : NULL;
  char *text = NULL;

  switch (row->kind) {
  case NET_ROW_NETWORK: {
    /* A four-cell strength bar, on the same principle as the volume mode's:
     * a quantity reads faster as a length than as a number. */
    GString *bar = g_string_sized_new(NET_BAR_CELLS * 3);
    int filled = (row->signal * NET_BAR_CELLS + 50) / 100;
    for (int i = 0; i < NET_BAR_CELLS; i++) {
      g_string_append(bar, i < filled ? "▮" : "▯");
    }

    text = g_strdup_printf(
        "%s  %s<span alpha='65%%'>  %s%s</span>", bar->str, label,
        detail != NULL ? detail : "",
        (row->secured && !row->known) ? ", key needed" : "");
    g_string_free(bar, TRUE);
    break;
  }

  case NET_ROW_INTERFACE:
    text = g_strdup_printf("%s%s<span alpha='65%%'>  %s</span>",
                           row->wireless ? "wlan  " : "eth   ", label,
                           detail != NULL ? detail : "");
    break;

  case NET_ROW_ACTION:
  case NET_ROW_HEADING:
  default:
    text = g_strdup_printf("<span alpha='85%%'>%s</span>", label);
    break;
  }

  g_free(label);
  g_free(detail);

  return text;
}

/**
 * Function purpose: state the verbs and name the backend in the message bar.
 *
 * The last thing that happened is shown in preference to the hints, because
 * after pressing a key the question is "did that work", not "what are the
 * keys". It falls back to the hints once there is nothing to report.
 */
static char *network_mode_get_message(const Mode *sw) {
  const NetworkModePrivateData *pd =
      (const NetworkModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL || pd->backend == NULL) {
    return NULL;
  }

  if (pd->status != NULL) {
    return g_markup_printf_escaped("%s", pd->status);
  }

  return g_markup_printf_escaped("Enter act  ·  Alt+1 rescan  ·  Alt+2 "
                                 "disconnect  ·  Alt+3 forget  ·  %s%s%s",
                                 pd->backend->name,
                                 pd->wifi_iface != NULL ? "  ·  " : "",
                                 pd->wifi_iface != NULL ? pd->wifi_iface : "");
}

/** Do whatever the highlighted row means. */
static gboolean net_activate(NetworkModePrivateData *pd, NetRow *row,
                             gboolean *should_exit) {
  *should_exit = FALSE;

  switch (row->kind) {
  case NET_ROW_ACTION:
    switch (row->action) {
    case NET_ACTION_WIFI_TOGGLE:
      if (pd->wifi_iface == NULL) {
        net_set_status(pd, "No wireless interface.");
        return FALSE;
      }
      if (pd->backend->radio(pd->wifi_iface, !row->active)) {
        net_set_status(pd, "Wi-Fi %s.", row->active ? "off" : "on");
        return TRUE;
      }
      net_set_status(pd, "Could not change the radio.");
      return FALSE;

    case NET_ACTION_DHCP_RENEW: {
      /* Action purpose: a lease is not a wireless idea, and aiming this verb at
       * the wireless interface made it useless on a wired-only machine -- there
       * is no wifi_iface there, so it failed with "DHCP renewal failed" while a
       * perfectly good ethernet link sat in the list. Fall back to the first
       * interface that is actually up.
       *
       * Deliberately not done for RECONNECT below: that is `wpa_cli
       * reassociate`, which has no wired meaning at all, and the base backend
       * already says so rather than pretending. */
      const char *iface = pd->wifi_iface;
      if (iface == NULL) {
        iface = net_first_active_iface(pd);
      }
      if (iface == NULL) {
        net_set_status(pd, "No interface is up to renew a lease on.");
        return FALSE;
      }
      if (pd->backend->dhcp_renew(iface)) {
        net_set_status(pd, "DHCP lease renewed on %s.", iface);
        return TRUE;
      }
      net_set_status(pd, "DHCP renewal failed on %s.", iface);
      return FALSE;
    }

    case NET_ACTION_RECONNECT:
      if (pd->backend->reconnect(pd->wifi_iface)) {
        net_set_status(pd, "Re-associating.");
        return TRUE;
      }
      net_set_status(pd, "Could not re-associate.");
      return FALSE;

    case NET_ACTION_RESET_ALL:
      /* Action purpose: this drops every link, including the one carrying the
       * session if it is remote. It closes the panel rather than reloading,
       * because the list it would reload is about to be meaningless and
       * re-reading it would race the restart. */
      sofi_view_hide();
      pd->backend->reset_all();
      *should_exit = TRUE;
      return TRUE;
    }
    return FALSE;

  case NET_ROW_INTERFACE:
    if (pd->backend->set_iface(row->id, !row->active)) {
      net_set_status(pd, "%s is %s.", row->id, row->active ? "down" : "up");
      return TRUE;
    }
    net_set_status(pd, "Could not bring %s %s.", row->id,
                   row->active ? "down" : "up");
    return FALSE;

  case NET_ROW_NETWORK: {
    char *psk = NULL;

    /* Action purpose: only ask for a key when one is actually needed -- the
     * network is secured and nothing has it stored. Prompting for a network the
     * supplicant already knows would invite the user to retype a key that is
     * working, and getting it wrong would replace a good one. */
    if (row->secured && !row->known) {
      sofi_view_hide();
      psk = net_prompt_password(row->label);
      if (psk == NULL) {
        net_set_status(pd, "Cancelled.");
        return FALSE;
      }
    }

    gboolean ok = pd->backend->join(pd->wifi_iface, row, psk);

    if (psk != NULL) {
      /* The key is not kept a moment longer than the call that used it. */
      memset(psk, 0, strlen(psk));
      g_free(psk);
    }

    if (ok) {
      net_set_status(pd, "Joining %s.", row->label);
      *should_exit = TRUE;
      return TRUE;
    }
    net_set_status(pd, "Could not join %s.", row->label);
    return FALSE;
  }

  case NET_ROW_HEADING:
  default:
    return FALSE;
  }
}

static ModeMode network_mode_result(Mode *sw, int mretv,
                                    G_GNUC_UNUSED char **input,
                                    unsigned int selected_line) {
  NetworkModePrivateData *pd =
      (NetworkModePrivateData *)mode_get_private_data(sw);

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

  NetRow *row = net_row_at(pd, selected_line);

  if (mretv & MENU_OK) {
    if (row == NULL) {
      return RELOAD_DIALOG;
    }

    gboolean should_exit = FALSE;
    gboolean changed = net_activate(pd, row, &should_exit);

    if (should_exit) {
      return MODE_EXIT;
    }
    if (changed) {
      net_refresh(pd);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    switch (custom) {
    /* kb-custom-1: rescan. The driver scans asynchronously, so this asks and
     * reloads rather than waiting -- results appear on this reload or the
     * next. */
    case 0:
      if (pd->wifi_iface == NULL) {
        net_set_status(pd, "No wireless interface to scan with.");
        return RELOAD_DIALOG;
      }
      pd->backend->rescan(pd->wifi_iface);
      net_set_status(pd, "Scanning.");
      net_refresh(pd);
      return RELOAD_DIALOG;

    /* kb-custom-2: disconnect, by taking the wireless interface down. Separate
     * from the radio toggle only in intent; the verb is the same one, which is
     * why it says so rather than pretending otherwise. */
    case 1:
      if (pd->wifi_iface == NULL) {
        net_set_status(pd, "No wireless interface.");
        return RELOAD_DIALOG;
      }
      if (pd->backend->set_iface(pd->wifi_iface, FALSE)) {
        net_set_status(pd, "%s is down.", pd->wifi_iface);
        net_refresh(pd);
      }
      return RELOAD_DIALOG;

    /* kb-custom-3: forget a saved network. A menu that saves a credential has
     * to offer a way to unsave it, or the only route back is editing the
     * supplicant's configuration as root. */
    case 2:
      if (row == NULL || row->kind != NET_ROW_NETWORK) {
        net_set_status(pd, "Select a network to forget.");
        return RELOAD_DIALOG;
      }
      if (!row->known) {
        net_set_status(pd, "%s is not saved.", row->label);
        return RELOAD_DIALOG;
      }
      if (pd->backend->forget(pd->wifi_iface, row)) {
        net_set_status(pd, "Forgot %s.", row->label);
        net_refresh(pd);
      } else {
        net_set_status(pd, "Could not forget %s.", row->label);
      }
      return RELOAD_DIALOG;

    default:
      return (ModeMode)custom;
    }
  }

  return MODE_EXIT;
}

static void network_mode_destroy(Mode *sw) {
  NetworkModePrivateData *pd =
      (NetworkModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_ptr_array_free(pd->rows, TRUE);
    g_free(pd->wifi_iface);
    g_free(pd->status);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int network_token_match(const Mode *sw, sofi_int_matcher **tokens,
                               unsigned int index) {
  const NetworkModePrivateData *pd =
      (const NetworkModePrivateData *)mode_get_private_data(sw);
  NetRow *row = net_row_at(pd, index);

  if (row == NULL) {
    return FALSE;
  }

  return helper_token_match(tokens, row->label);
}

Mode network_mode = {.name = "network",
                     .cfg_name_key = "display-network",
                     ._init = network_mode_init,
                     ._get_num_entries = network_mode_get_num_entries,
                     ._result = network_mode_result,
                     ._destroy = network_mode_destroy,
                     ._token_match = network_token_match,
                     ._get_display_value = _get_display_value,
                     ._get_icon = NULL,
                     ._get_completion = NULL,
                     ._preprocess_input = NULL,
                     ._get_message = network_mode_get_message,
                     .private_data = NULL,
                     .free = NULL,
                     .type = MODE_TYPE_SWITCHER};

#endif // NETWORK_MODE
