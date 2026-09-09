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
 * @brief Bluetooth device management on the FreeBSD netgraph stack.
 *
 * Script function and purpose: one surface for the whole bluetooth stack --
 * power, discovery, pairing, connecting, forgetting, and the per-class setup
 * that makes an input device or an audio device actually usable once it is
 * connected. Subprocess-driven throughout; nothing here is linked.
 *
 * ---------------------------------------------------------------------------
 * THERE IS ONE STACK AND IT IS NOT BlueZ
 *
 * Ruled by USER 2026-09-09 (R12). FreeBSD's bluetooth stack is netgraph, driven
 * by `hccontrol(8)`, `sdpcontrol(8)`, `bthidcontrol(8)` and the `hcsecd(8)` and
 * `bthidd(8)` daemons. **There is no D-Bus bluetooth daemon in the base system
 * and BlueZ is Linux-only**, so the earlier plan for an `org.bluez` backend is
 * deleted rather than deferred.
 *
 * With one stack there is no backend vtable, and its absence is deliberate. The
 * half of that pattern which carries its weight is **runtime selection by what
 * answers rather than by what is installed**, and here that is:
 *
 *     the adapter is found because `hccontrol read_node_list` produced a node,
 *     not because `hccontrol` exists on $PATH.
 *
 * A machine with the base system present and `service bluetooth` not started is
 * exactly the case those two tests disagree on, and it is the common one.
 *
 * ---------------------------------------------------------------------------
 * THE PRIVILEGE BOUNDARY IS NOT WHERE IT LOOKS
 *
 * Most of what this mode reads needs no privilege at all. `read_node_list`,
 * `read_connection_list`, `read_bd_addr`, `read_local_name`, `read_scan_enable`,
 * `read_class_of_device` and `read_neighbor_cache` all answer an ordinary user,
 * which is what makes opening the pane instant.
 *
 * What does need it, verified rather than assumed:
 *
 *   `hccontrol read_stored_link_key`   Operation not permitted on the raw socket
 *   /etc/bluetooth/hcsecd.conf         0600 root -- the paired-device list
 *   /var/db/hcsecd.keys                0600 root -- the link keys
 *
 * **So on FreeBSD the paired-device list is a root-only file.** Everything in
 * this mode that involves pairing therefore goes through
 * `network-privilege-command` -- the same option the network mode uses, reused
 * on USER's ruling rather than duplicated under a second name. It is empty by
 * default and sofi installs nothing setuid; when a privileged verb fails, the
 * warning names the option, because "the button does nothing" and "the button
 * needs a line of configuration" are different problems.
 *
 * ---------------------------------------------------------------------------
 * EVERY OUTPUT FORMAT PARSED HERE WAS READ OUT OF /usr/src
 *
 * R10 shipped a parser for output that could never appear on the target. The
 * formats below came from the printers that emit them, cited so the next person
 * can check them the same way:
 *
 *   inquiry               link_control.c:153   "Inquiry result #N", "\tBD_ADDR: ",
 *                                              "\tClass: %02x:%02x:%02x"
 *   read_neighbor_cache   node.c:237           "%1s %-17.17s " + 8 feature bytes.
 *                                              **Carries no name and no class.**
 *   read_connection_list  node.c:293           "%-17.17s %6d %4.4s %4d %4.4s ..."
 *   read_class_of_device  host_controller_baseband.c:954
 *                                              uclass[2]:uclass[1]:uclass[0], so
 *                                              the printed string is MSB first
 *   read_scan_enable      host_controller_baseband.c:603  "... [%#02x]"
 *   remote_name_request   link_control.c:599   "Name: %s"
 *
 * That the neighbor cache carries neither a name nor a class is not a detail: it
 * is why classes learned from an inquiry are remembered across a refresh, and
 * why a device's name is assembled from three sources instead of read from one.
 *
 * ---------------------------------------------------------------------------
 * KNOWN LIMIT, shared with the volume and network modes
 *
 * Every subprocess here is synchronous on sofi's main thread and `g_spawn_sync`
 * has no timeout, so a tool that accepts a request and never answers holds the
 * menu until it does. The inquiry is the slow one by design and is bounded by
 * the controller itself, not by sofi: ::BT_INQUIRY_LENGTH is in units of 1.28
 * seconds and the controller stops on its own when it expires.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Bluetooth"

#include "config.h"

#ifdef BLUETOOTH_MODE

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "helper.h"
#include "modes/bluetooth.h"
#include "settings.h"
#include "sofi.h"
#include "view.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** Signal cells drawn on a connected device row, matching the volume and
 * network bars so one length means one thing across all three surfaces. */
#define BT_SIGNAL_CELLS 4

/** General Inquiry Access Code. The LAP every discoverable device answers. */
#define BT_INQUIRY_LAP "0x9e8b33"

/** Inquiry duration in units of 1.28s. Four is a shade over five seconds, which
 * is long enough for a device in pairing mode to answer and short enough that
 * the pane is not mistaken for hung. The controller ends the inquiry itself. */
#define BT_INQUIRY_LENGTH "4"

/** Responses to stop early on. More than a household has; a flat in a block of
 * flats can exceed it, which is why Alt+1 is repeatable rather than exhaustive. */
#define BT_INQUIRY_RESPONSES "16"

/** Disconnect reason: remote user terminated connection. The only honest code
 * for "a person pressed a key in a menu". */
#define BT_DISCONNECT_REASON "0x13"

/** hcsecd's own comment: the default entry MUST exist and MUST carry this
 * address. The forget path refuses it explicitly. */
#define BT_DEFAULT_BDADDR "00:00:00:00:00:00"

#define BT_HCSECD_CONF "/etc/bluetooth/hcsecd.conf"
#define BT_BTHIDD_CONF "/etc/bluetooth/bthidd.conf"
#define BT_HOSTS "/etc/bluetooth/hosts"

/** `virtual_oss(8)` needs this; without it the daemon exits before doing
 * anything. It is a kernel module (`cuse_load="YES"` in loader.conf) and the
 * node is `0600 root`, which is why the audio verb escalates even for a user
 * who is in `operator`. */
#define BT_CUSE_DEVICE "/dev/cuse"

/** How `virtual_oss(8)` names a bluetooth device. It is not a real path and
 * `/dev/bluetooth` does not exist as a directory -- the daemon recognises the
 * prefix and hands the address to `voss_bt.so`. Checking for the directory
 * would report a working setup as broken. */
#define BT_AUDIO_DEV_PREFIX "/dev/bluetooth/"

/** The bluetooth backend, from the audio/virtual_oss_bluetooth port. Probed by
 * path because it is a plugin rather than a binary, so `$PATH` cannot find it
 * and `pkg` is not something to shell out to on a menu open. */
static const char *const bt_voss_backends[] = {
    "/usr/local/lib/virtual_oss/voss_bt.so",
    "/usr/lib/virtual_oss/voss_bt.so",
};

/** What a row is, which decides what Enter does to it. */
typedef enum {
  /** The controller itself. Enter powers the stack down or up. */
  BT_ROW_ADAPTER,
  /** A device, in any combination of paired, connected and in-range. */
  BT_ROW_DEVICE,
  /** A fixed verb: discoverability, a controller reset, a stack restart. */
  BT_ROW_ACTION,
  /** A heading. Not selectable in any useful sense; Enter reloads. */
  BT_ROW_HEADING,
} BtRowKind;

/** Which verb a ::BT_ROW_ACTION row carries. */
typedef enum {
  BT_ACTION_DISCOVERABLE,
  BT_ACTION_RESET,
  BT_ACTION_RESTART_STACK,
} BtAction;

/**
 * What the class-of-device says the thing is.
 *
 * This decode is the whole of the "intelligent detection": it is what lets one
 * surface carry a headset and a gamepad without the two sharing a code path,
 * and it is what ::BT_ACTION setup dispatches on -- a Peripheral needs a bthidd
 * stanza, an Audio/Video device needs an audio path that FreeBSD base does not
 * have. Guessing from the device's advertised name would be a coin flip; the
 * class is what the device itself declares it is.
 */
typedef enum {
  BT_KIND_UNKNOWN,
  BT_KIND_HEADSET,
  BT_KIND_HANDSFREE,
  BT_KIND_MICROPHONE,
  BT_KIND_SPEAKER,
  BT_KIND_HEADPHONES,
  BT_KIND_AUDIO,
  BT_KIND_KEYBOARD,
  BT_KIND_MOUSE,
  BT_KIND_COMBO,
  BT_KIND_JOYSTICK,
  BT_KIND_GAMEPAD,
  BT_KIND_TABLET,
  BT_KIND_PHONE,
  BT_KIND_COMPUTER,
  BT_KIND_NETWORK,
  BT_KIND_IMAGING,
  BT_KIND_WEARABLE,
  BT_KIND_PERIPHERAL,
} BtKind;

/**
 * What the audio route can be. Four states, and each wants a different sentence
 * from the user -- which is the whole reason they are not collapsed.
 *
 * `virtual_oss(8)` is a **base system** daemon and its bluetooth support is a
 * **dynamically loaded backend**, `voss_bt.so`, from the `audio/virtual_oss_bluetooth`
 * port (BSD-2-clause). The backend is loaded only when an invocation names a
 * bluetooth device, which is why the two are probed separately: having the
 * daemon says nothing about having the backend.
 */
typedef enum {
  /** No `virtual_oss` on $PATH. It is in the base system, so this means a
   * non-FreeBSD machine or a stripped install. */
  BT_AUDIO_NO_DAEMON,
  /** `virtual_oss` is there and `voss_bt.so` is not. **Actionable**: install
   * `audio/virtual_oss_bluetooth`. This is the state the message must never
   * confuse with the others, because it is the only one the user can fix by
   * installing something. */
  BT_AUDIO_NO_BACKEND,
  /** Both present, but `cuse(3)` is not available. `virtual_oss` refuses to
   * start without it and says so in a way that reads like a fault in the tool. */
  BT_AUDIO_NO_CUSE,
  /** Everything is in place. */
  BT_AUDIO_READY,
} BtAudioRoute;

typedef struct {
  BtRowKind kind;
  BtAction action;

  /** Backend-native handle, passed back verbatim: a BD_ADDR on a device row, a
   * netgraph node name on the adapter row, NULL on an action row. */
  char *id;
  /** What the row says. */
  char *label;
  /** Secondary text: the class, the address, a state. May be NULL. */
  char *detail;

  BtKind devkind;
  /** Raw class-of-device, valid only when @c have_cod. */
  guint32 cod;
  gboolean have_cod;

  /** Has a stanza in hcsecd.conf, so a link key can be negotiated for it. */
  gboolean paired;
  /** Present in read_connection_list. */
  gboolean connected;
  /** bthidd knows it, so it can actually deliver input. */
  gboolean hid_configured;
  /** Seen by an inquiry or sitting in the neighbour cache. */
  gboolean in_range;

  /** Connection handle, or -1. Disconnect needs this, not the address. */
  int handle;
  /** dBm, valid only when @c have_rssi. */
  int rssi;
  gboolean have_rssi;

  /** Connected, up, or on. Drives the ACTIVE row state. */
  gboolean active;
} BtRow;

typedef struct {
  /** The netgraph HCI node, e.g. "ubt0hci". NULL when the stack is down. */
  char *node;
  /** The USB device the node belongs to, e.g. "ubt0". Derived from @c node;
   * `service bluetooth` is addressed by this and not by the node name. */
  char *device;

  char *bdaddr;
  char *local_name;
  /** Scan_Enable: bit 0x1 inquiry scan (discoverable), 0x2 page scan
   * (connectable). -1 when it could not be read. */
  int scan_enable;

  /** TRUE once hcsecd.conf has been read by any route. When FALSE the paired
   * column is unknown rather than empty, and the message bar says so. */
  gboolean have_paired;
  /** TRUE when reading it needed the privilege command. Reported, because it
   * means every open costs one escalation. */
  gboolean paired_needed_privilege;

  BtAudioRoute audio_route;
  /** Path to virtual_oss, or NULL. Owned. */
  char *audio_binary;

  /** BtRow*, owned. */
  GPtrArray *rows;

  /** BD_ADDR -> friendly name. Assembled from hosts, hcsecd.conf and any
   * remote_name_request that succeeded. Owned, string->string. */
  GHashTable *names;
  /** BD_ADDR -> the raw hcsecd.conf stanza's presence. Owned, string->NULL. */
  GHashTable *paired;
  /** BD_ADDR -> class-of-device, stored as GUINT(cod + 1) so that a real zero
   * class is distinguishable from absence.
   *
   * Action purpose: this outlives a refresh on purpose. The neighbour cache
   * carries no class, so a device discovered by an inquiry would lose its
   * identity -- and with it the correct Alt+4 verb -- on the very next reload
   * if the class were not remembered here. */
  GHashTable *classes;
  /** BD_ADDR -> NULL, for devices bthidd is configured for. Owned. */
  GHashTable *hids;

  /** Last thing that happened, for the message bar. Owned, may be NULL. */
  char *status;
} BluetoothModePrivateData;

static void bt_row_free(gpointer data) {
  BtRow *row = data;

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
 * callers use failure as a probe result -- "the stack is not running", "this
 * command needs privilege". Warning here would put a line on stderr every time
 * the menu opened on a machine with the adapter switched off.
 */
static gboolean bt_run(const char *const *argv, char **out) {
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
 * Function purpose: run a command through the configured privilege command,
 * optionally capturing its stdout.
 *
 * Action purpose: `network-privilege-command` is reused here on USER's ruling
 * (R12) rather than duplicated under a bluetooth-specific name -- one machine
 * escalates one way. It is parsed with g_shell_parse_argv so it can carry its
 * own arguments (`doas`, `sudo -n`), and the words are prepended to argv.
 *
 * It is deliberately empty by default. Choosing how a machine escalates is an
 * administrator's decision and guessing at `sudo` would be sofi making it for
 * them. When the command fails and no prefix is configured, the warning names
 * the option and says what it is for.
 *
 * @param quiet suppresses the warning, for the probe on menu open where a
 *              failure is an expected answer rather than a fault.
 */
static gboolean bt_run_priv(const char *const *argv, char **out,
                            gboolean quiet) {
  const char *prefix = config.network_privilege_command;

  if (out != NULL) {
    *out = NULL;
  }

  if (prefix == NULL || *prefix == '\0') {
    if (!quiet) {
      g_warning("`%s` needs privilege and no privilege command is configured. "
                "Set `network-privilege-command` in ~/.config/sofi/config.sasi "
                "-- for example \"doas\" or \"sudo -n\" -- so sofi can run it "
                "with the rights it needs. sofi installs nothing setuid. The "
                "same option serves the network mode; there is deliberately "
                "not a second one.",
                argv[0]);
    }
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

  gboolean ok = bt_run((const char *const *)full->pdata, out);

  g_ptr_array_free(full, TRUE);
  g_strfreev(prefix_argv);

  if (!ok && !quiet) {
    g_warning("`%s %s` failed. Check that `network-privilege-command` is "
              "correct and does not need a password on a terminal.",
              prefix, argv[0]);
  }

  return ok;
}

/**
 * Function purpose: ask for a pairing PIN without ever putting it on screen.
 *
 * Action purpose: sofi already has a masked input -- dmenu mode's `-password`
 * -- so the prompt is another sofi run as a child rather than a new widget, the
 * same route the network mode's key prompt takes. `-input /dev/null` gives it an
 * empty list, so the surface is a prompt and nothing else.
 *
 * The caller hides this panel first. Two layer surfaces competing for the
 * keyboard is not a state worth entering.
 *
 * @returns the PIN, newly allocated, or NULL when the prompt was dismissed. An
 *          empty answer is returned as an empty string rather than NULL,
 *          because "no PIN" is a legitimate answer for a just-works device and
 *          has to be distinguishable from "cancelled".
 */
static char *bt_prompt_pin(const char *name) {
  char *prompt = g_strdup_printf("PIN for %s (empty for none)", name);
  const char *argv[] = {"sofi", "-dmenu", "-password", "-input",
                        "/dev/null", "-p", prompt, NULL};
  char *out = NULL;

  gboolean ok = bt_run(argv, &out);
  g_free(prompt);

  if (!ok || out == NULL) {
    g_free(out);
    return NULL;
  }

  /* dmenu terminates its answer with a newline, and a PIN with trailing
   * whitespace is not a thing anyone types deliberately. Passing one on would
   * produce an authentication failure that looks like a wrong PIN. */
  g_strstrip(out);

  return out;
}

/* ------------------------------------------------------------ hci helpers */

/** Run one hccontrol command against @p node, capturing stdout. */
static gboolean bt_hci(const char *node, char **out, const char *cmd, ...) {
  GPtrArray *argv = g_ptr_array_new();
  va_list args;

  g_ptr_array_add(argv, (gpointer) "hccontrol");
  g_ptr_array_add(argv, (gpointer) "-n");
  g_ptr_array_add(argv, (gpointer)node);
  g_ptr_array_add(argv, (gpointer)cmd);

  va_start(args, cmd);
  for (;;) {
    const char *arg = va_arg(args, const char *);
    if (arg == NULL) {
      break;
    }
    g_ptr_array_add(argv, (gpointer)arg);
  }
  va_end(args);
  g_ptr_array_add(argv, NULL);

  gboolean ok = bt_run((const char *const *)argv->pdata, out);

  g_ptr_array_free(argv, TRUE);

  return ok;
}

/** The same, escalated. Used only by the verbs the raw socket gates. */
static gboolean bt_hci_priv(const char *node, const char *cmd, const char *a1,
                            const char *a2) {
  GPtrArray *argv = g_ptr_array_new();

  g_ptr_array_add(argv, (gpointer) "hccontrol");
  g_ptr_array_add(argv, (gpointer) "-n");
  g_ptr_array_add(argv, (gpointer)node);
  g_ptr_array_add(argv, (gpointer)cmd);
  if (a1 != NULL) {
    g_ptr_array_add(argv, (gpointer)a1);
  }
  if (a2 != NULL) {
    g_ptr_array_add(argv, (gpointer)a2);
  }
  g_ptr_array_add(argv, NULL);

  gboolean ok = bt_run_priv((const char *const *)argv->pdata, NULL, FALSE);

  g_ptr_array_free(argv, TRUE);

  return ok;
}

/**
 * Function purpose: pull the value that follows "<key>: " on a line.
 *
 * hccontrol answers almost every read command with one `Label: value` line, so
 * one extractor serves read_bd_addr, read_local_name, read_scan_enable and the
 * rest rather than a parser each.
 */
static char *bt_field(const char *text, const char *key) {
  if (text == NULL) {
    return NULL;
  }

  char *needle = g_strdup_printf("%s: ", key);
  const char *found = strstr(text, needle);
  g_free(needle);

  if (found == NULL) {
    return NULL;
  }

  found += strlen(key) + 2;
  const char *end = strchr(found, '\n');
  char *value =
      end != NULL ? g_strndup(found, (gsize)(end - found)) : g_strdup(found);

  return g_strstrip(value);
}

/** TRUE when @p s starts with a BD_ADDR, which is the only 17-character token
 * of this exact shape anything here emits. */
static gboolean bt_is_bdaddr(const char *s) {
  if (s == NULL) {
    return FALSE;
  }

  for (int i = 0; i < 17; i++) {
    if ((i % 3) == 2) {
      if (s[i] != ':') {
        return FALSE;
      }
    } else if (!g_ascii_isxdigit(s[i])) {
      return FALSE;
    }
  }

  return TRUE;
}

/** The first BD_ADDR on a line, newly allocated, or NULL. */
static char *bt_line_bdaddr(const char *line) {
  for (const char *p = line; *p != '\0'; p++) {
    if (bt_is_bdaddr(p)) {
      return g_ascii_strdown(p, 17);
    }
  }
  return NULL;
}

/* ------------------------------------------------------- class of device */

/**
 * Function purpose: decode `Class: aa:bb:cc` into a class-of-device word.
 *
 * Action purpose: hccontrol prints `uclass[2]:uclass[1]:uclass[0]` -- the array
 * is little-endian but the printed string is not -- so the leftmost byte is the
 * most significant. Verified against host_controller_baseband.c:954 and
 * link_control.c:161, which use the identical argument order.
 */
static gboolean bt_parse_cod(const char *text, guint32 *out) {
  unsigned int a = 0, b = 0, c = 0;

  if (text == NULL || sscanf(text, "%2x:%2x:%2x", &a, &b, &c) != 3) {
    return FALSE;
  }

  *out = ((guint32)a << 16) | ((guint32)b << 8) | (guint32)c;

  return TRUE;
}

/**
 * Function purpose: turn a class-of-device into the one thing a person would
 * call it.
 *
 * Action purpose: major class is bits 8-12, minor is bits 2-7. The Peripheral
 * major splits twice over -- `minor & 0x30` says keyboard, pointing device or
 * both, and `minor & 0x0f` says which kind of specialised peripheral -- so a
 * gamepad and a keyboard are told apart here rather than by their names, which
 * are chosen by the manufacturer and mean nothing.
 */
static BtKind bt_kind_from_cod(guint32 cod) {
  const unsigned int major = (cod >> 8) & 0x1f;
  const unsigned int minor = (cod >> 2) & 0x3f;

  switch (major) {
  case 0x01:
    return BT_KIND_COMPUTER;

  case 0x02:
    return BT_KIND_PHONE;

  case 0x03:
    return BT_KIND_NETWORK;

  case 0x04:
    switch (minor) {
    case 0x01:
      return BT_KIND_HEADSET;
    case 0x02:
      return BT_KIND_HANDSFREE;
    case 0x04:
      return BT_KIND_MICROPHONE;
    case 0x05:
      return BT_KIND_SPEAKER;
    case 0x06:
      return BT_KIND_HEADPHONES;
    default:
      return BT_KIND_AUDIO;
    }

  case 0x05:
    /* The specialised kinds are checked first: a gamepad that also reports the
     * pointing-device bit is a gamepad, and calling it a mouse would send
     * Alt+4 down a path that configures it as one. */
    switch (minor & 0x0f) {
    case 0x01:
      return BT_KIND_JOYSTICK;
    case 0x02:
      return BT_KIND_GAMEPAD;
    case 0x05:
      return BT_KIND_TABLET;
    default:
      break;
    }
    switch (minor & 0x30) {
    case 0x10:
      return BT_KIND_KEYBOARD;
    case 0x20:
      return BT_KIND_MOUSE;
    case 0x30:
      return BT_KIND_COMBO;
    default:
      return BT_KIND_PERIPHERAL;
    }

  case 0x06:
    return BT_KIND_IMAGING;

  case 0x07:
    return BT_KIND_WEARABLE;

  default:
    return BT_KIND_UNKNOWN;
  }
}

static const char *bt_kind_name(BtKind kind) {
  switch (kind) {
  case BT_KIND_HEADSET:
    return "headset";
  case BT_KIND_HANDSFREE:
    return "hands-free";
  case BT_KIND_MICROPHONE:
    return "microphone";
  case BT_KIND_SPEAKER:
    return "speaker";
  case BT_KIND_HEADPHONES:
    return "headphones";
  case BT_KIND_AUDIO:
    return "audio";
  case BT_KIND_KEYBOARD:
    return "keyboard";
  case BT_KIND_MOUSE:
    return "mouse";
  case BT_KIND_COMBO:
    return "keyboard + pointer";
  case BT_KIND_JOYSTICK:
    return "joystick";
  case BT_KIND_GAMEPAD:
    return "gamepad";
  case BT_KIND_TABLET:
    return "tablet";
  case BT_KIND_PHONE:
    return "phone";
  case BT_KIND_COMPUTER:
    return "computer";
  case BT_KIND_NETWORK:
    return "network";
  case BT_KIND_IMAGING:
    return "imaging";
  case BT_KIND_WEARABLE:
    return "wearable";
  case BT_KIND_PERIPHERAL:
    return "peripheral";
  case BT_KIND_UNKNOWN:
  default:
    return NULL;
  }
}

/** A short glyph, so the kind reads before the label does. */
static const char *bt_kind_glyph(BtKind kind) {
  switch (kind) {
  case BT_KIND_HEADSET:
  case BT_KIND_HANDSFREE:
  case BT_KIND_HEADPHONES:
    return "🎧";
  case BT_KIND_MICROPHONE:
    return "🎙";
  case BT_KIND_SPEAKER:
  case BT_KIND_AUDIO:
    return "🔈";
  case BT_KIND_KEYBOARD:
  case BT_KIND_COMBO:
    return "⌨";
  case BT_KIND_MOUSE:
  case BT_KIND_TABLET:
    return "🖱";
  case BT_KIND_JOYSTICK:
  case BT_KIND_GAMEPAD:
    return "🎮";
  case BT_KIND_PHONE:
    return "📱";
  case BT_KIND_COMPUTER:
    return "💻";
  case BT_KIND_NETWORK:
    return "🌐";
  case BT_KIND_IMAGING:
    return "🖨";
  case BT_KIND_WEARABLE:
    return "⌚";
  case BT_KIND_PERIPHERAL:
  case BT_KIND_UNKNOWN:
  default:
    return "•";
  }
}

/** TRUE for the classes bthidd can drive: keyboards, mice, gamepads, tablets. */
static gboolean bt_kind_is_hid(BtKind kind) {
  switch (kind) {
  case BT_KIND_KEYBOARD:
  case BT_KIND_MOUSE:
  case BT_KIND_COMBO:
  case BT_KIND_JOYSTICK:
  case BT_KIND_GAMEPAD:
  case BT_KIND_TABLET:
  case BT_KIND_PERIPHERAL:
    return TRUE;
  default:
    return FALSE;
  }
}

/** TRUE for the classes that want an audio path. */
static gboolean bt_kind_is_audio(BtKind kind) {
  switch (kind) {
  case BT_KIND_HEADSET:
  case BT_KIND_HANDSFREE:
  case BT_KIND_MICROPHONE:
  case BT_KIND_SPEAKER:
  case BT_KIND_HEADPHONES:
  case BT_KIND_AUDIO:
    return TRUE;
  default:
    return FALSE;
  }
}

/* ------------------------------------------------------------ hcsecd.conf */

/**
 * Function purpose: read hcsecd.conf, unprivileged first.
 *
 * Action purpose: the file is 0600 root on a stock system, but trying it as the
 * user first costs one failed open and means a machine that has relaxed the
 * permissions never escalates at all. Only when that fails does the privilege
 * command run, and it runs quietly, because on a machine with no privilege
 * command configured this failing is the expected answer and not a fault --
 * the pane still works, it just cannot show which devices are paired.
 *
 * @param used_privilege set TRUE when escalation was required, so the message
 *                       bar can say the paired list costs one escalation per
 *                       open rather than leaving it to be discovered. May be
 *                       NULL where the caller does not care, which is every
 *                       caller that is writing rather than listing.
 */
static char *bt_hcsecd_read(gboolean *used_privilege) {
  char *contents = NULL;

  if (used_privilege != NULL) {
    *used_privilege = FALSE;
  }

  if (g_file_get_contents(BT_HCSECD_CONF, &contents, NULL, NULL)) {
    return contents;
  }

  const char *argv[] = {"cat", BT_HCSECD_CONF, NULL};
  if (bt_run_priv(argv, &contents, TRUE)) {
    if (used_privilege != NULL) {
      *used_privilege = TRUE;
    }
    return contents;
  }

  g_debug("Could not read %s by any route; the paired list will be unknown.",
          BT_HCSECD_CONF);

  return NULL;
}

/**
 * Function purpose: find one `device { ... }` stanza by address.
 *
 * Action purpose: brace matching rather than a line scan, so a stanza spanning
 * any number of lines with any indentation is found whole. The returned span
 * starts at the beginning of the line the `device` keyword is on -- not at the
 * keyword -- so splicing it out cannot leave a stray indent behind, and it ends
 * just past the closing brace's newline.
 *
 * @returns TRUE when a stanza for @p bdaddr was found, with @p start and
 *          @p end set to its byte range within @p text.
 */
static gboolean bt_hcsecd_find(const char *text, const char *bdaddr,
                               gsize *start, gsize *end) {
  if (text == NULL || bdaddr == NULL) {
    return FALSE;
  }

  const char *p = text;

  while ((p = strstr(p, "device")) != NULL) {
    const char *keyword = p;
    p += strlen("device");

    /* Only a `device` followed by a brace opens a stanza; the word also occurs
     * in this file's own comment block, which must not be matched. */
    const char *brace = p;
    while (*brace == ' ' || *brace == '\t' || *brace == '\n' ||
           *brace == '\r') {
      brace++;
    }
    if (*brace != '{') {
      continue;
    }

    /* A `#` comment cannot open a stanza either. Walk back to the line start
     * and reject the match if a hash precedes it there. */
    const char *line = keyword;
    while (line > text && line[-1] != '\n') {
      line--;
    }
    gboolean commented = FALSE;
    for (const char *q = line; q < keyword; q++) {
      if (*q == '#') {
        commented = TRUE;
        break;
      }
    }
    if (commented) {
      continue;
    }

    int depth = 0;
    const char *close = brace;
    for (; *close != '\0'; close++) {
      if (*close == '{') {
        depth++;
      } else if (*close == '}') {
        depth--;
        if (depth == 0) {
          break;
        }
      }
    }
    if (*close != '}') {
      /* Unbalanced. Refuse to guess where it ends -- a rewrite based on a
       * guess here would truncate someone's configuration. */
      g_warning("%s has an unterminated device stanza; sofi will not rewrite "
                "it. Fix the file by hand.",
                BT_HCSECD_CONF);
      return FALSE;
    }

    gsize span_start = (gsize)(line - text);
    gsize span_end = (gsize)(close - text) + 1;
    if (text[span_end] == '\n') {
      span_end++;
    }

    char *body = g_strndup(brace, (gsize)(close - brace) + 1);
    char *found = bt_line_bdaddr(body);
    gboolean match = found != NULL && g_ascii_strcasecmp(found, bdaddr) == 0;
    g_free(found);
    g_free(body);

    if (match) {
      *start = span_start;
      *end = span_end;
      return TRUE;
    }

    p = close;
  }

  return FALSE;
}

/** Every address that has a stanza, into @p out as a key set. */
static void bt_hcsecd_collect(const char *text, GHashTable *out,
                              GHashTable *names) {
  if (text == NULL) {
    return;
  }

  const char *p = text;

  while ((p = strstr(p, "device")) != NULL) {
    const char *keyword = p;
    p += strlen("device");

    const char *brace = p;
    while (*brace == ' ' || *brace == '\t' || *brace == '\n' ||
           *brace == '\r') {
      brace++;
    }
    if (*brace != '{') {
      continue;
    }

    const char *line = keyword;
    while (line > text && line[-1] != '\n') {
      line--;
    }
    gboolean commented = FALSE;
    for (const char *q = line; q < keyword; q++) {
      if (*q == '#') {
        commented = TRUE;
        break;
      }
    }
    if (commented) {
      continue;
    }

    int depth = 0;
    const char *close = brace;
    for (; *close != '\0'; close++) {
      if (*close == '{') {
        depth++;
      } else if (*close == '}') {
        depth--;
        if (depth == 0) {
          break;
        }
      }
    }
    if (*close != '}') {
      return;
    }

    char *body = g_strndup(brace, (gsize)(close - brace) + 1);
    char *addr = bt_line_bdaddr(body);

    if (addr != NULL && g_ascii_strcasecmp(addr, BT_DEFAULT_BDADDR) != 0) {
      /* The name is optional and quoted. It is the best name source there is
       * for a paired device, because it is whatever the device called itself
       * when it was paired. */
      const char *namekey = strstr(body, "name");
      if (namekey != NULL) {
        const char *open_quote = strchr(namekey, '"');
        const char *close_quote =
            open_quote != NULL ? strchr(open_quote + 1, '"') : NULL;
        if (close_quote != NULL) {
          char *name = g_strndup(open_quote + 1,
                                 (gsize)(close_quote - open_quote - 1));
          if (*name != '\0') {
            g_hash_table_replace(names, g_strdup(addr), name);
          } else {
            g_free(name);
          }
        }
      }
      g_hash_table_replace(out, g_strdup(addr), NULL);
    }

    g_free(addr);
    g_free(body);

    p = close;
  }
}

/**
 * Function purpose: put new text into a root-owned file without a shell.
 *
 * Action purpose: three things are being avoided at once and each has bitten
 * somebody before.
 *
 * **No `sh -c`.** A device name goes into this file, and a shell in the path
 * would give it a quoting surface to escape through. `install(1)` takes its
 * arguments as arguments.
 *
 * **Atomic.** `install` writes and renames within the destination directory, so
 * a failure part-way leaves the old file intact. A half-written hcsecd.conf
 * costs every pairing on the machine.
 *
 * **The temporary file is 0600 from the moment it exists** -- g_mkstemp_full
 * sets the mode at creation, not after -- and is overwritten before it is
 * unlinked, because it held a PIN. That is the treatment R8 gave the nmcli
 * password file and the reason is the same: unlink does not erase.
 */
static gboolean bt_install_root_file(const char *contents, const char *dest) {
  const char *runtime = g_get_user_runtime_dir();
  char *tmpl = g_build_filename(runtime != NULL ? runtime : g_get_tmp_dir(),
                                "sofi-bt-XXXXXX", NULL);
  gboolean ok = FALSE;

  int fd = g_mkstemp_full(tmpl, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    g_warning("Could not create a temporary file to stage %s: %s", dest,
              g_strerror(errno));
    g_free(tmpl);
    return FALSE;
  }

  const gsize len = strlen(contents);
  gsize off = 0;
  while (off < len) {
    ssize_t n = write(fd, contents + off, len - off);
    if (n > 0) {
      off += (gsize)n;
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  gboolean written = off == len;
  close(fd);

  if (written) {
    const char *argv[] = {"install", "-m", "600", "-o",  "root",
                          "-g",      "wheel", tmpl, dest, NULL};
    ok = bt_run_priv(argv, NULL, FALSE);
  } else {
    g_warning("Short write staging %s; nothing was installed.", dest);
  }

  /* Action purpose: overwrite before unlinking. The staged text carried a PIN
   * and unlink only drops the name. */
  int scrub = g_open(tmpl, O_WRONLY, 0600);
  if (scrub >= 0) {
    char zeros[256];
    memset(zeros, 0, sizeof(zeros));
    for (gsize done = 0; done < len; done += sizeof(zeros)) {
      if (write(scrub, zeros, sizeof(zeros)) < 0) {
        break;
      }
    }
    close(scrub);
  }
  g_unlink(tmpl);
  g_free(tmpl);

  return ok;
}

/** The stanza text for one device. */
static char *bt_hcsecd_stanza(const char *bdaddr, const char *name,
                              const char *pin) {
  /* The name is quoted in the file, so a quote or a newline inside it would
   * break the stanza. Neither belongs in a device name; they are dropped
   * rather than escaped, because hcsecd's lexer has no escape sequence. */
  GString *safe = g_string_sized_new(32);
  for (const char *p = name != NULL ? name : ""; *p != '\0'; p++) {
    if (*p == '"' || *p == '\n' || *p == '\r' || *p == '\\') {
      continue;
    }
    g_string_append_c(safe, *p);
  }
  if (safe->len == 0) {
    g_string_append(safe, "Paired by sofi");
  }

  GString *pin_safe = g_string_sized_new(16);
  for (const char *p = pin != NULL ? pin : ""; *p != '\0'; p++) {
    if (*p == '"' || *p == '\n' || *p == '\r' || *p == '\\') {
      continue;
    }
    g_string_append_c(pin_safe, *p);
  }

  /* `key nokey` is correct for a new pairing and is not a placeholder: it tells
   * hcsecd there is no link key yet, so it negotiates one and stores it in
   * /var/db/hcsecd.keys itself. Writing a key here by hand is what the file is
   * for when restoring a backup, not when pairing. */
  char *stanza = g_strdup_printf("\n# Added by sofi\ndevice {\n"
                                 "\tbdaddr\t%s;\n"
                                 "\tname\t\"%s\";\n"
                                 "\tkey\tnokey;\n"
                                 "\tpin\t%s%s%s;\n"
                                 "}\n",
                                 bdaddr, safe->str,
                                 pin_safe->len > 0 ? "\"" : "",
                                 pin_safe->len > 0 ? pin_safe->str : "nopin",
                                 pin_safe->len > 0 ? "\"" : "");

  memset(pin_safe->str, 0, pin_safe->allocated_len);
  g_string_free(pin_safe, TRUE);
  g_string_free(safe, TRUE);

  return stanza;
}

/** Tell hcsecd to re-read its configuration. */
static gboolean bt_hcsecd_reload(void) {
  /* Action purpose: `service ... reload` and not `pkill`. rc.subr sends SIGHUP
   * to the pid in /var/run/hcsecd.pid, so it reaches exactly the running
   * daemon; hcsecd.c:419-424 handles SIGHUP by calling read_config_file(). */
  const char *argv[] = {"service", "hcsecd", "reload", NULL};

  return bt_run_priv(argv, NULL, FALSE);
}

/**
 * Function purpose: add or replace one device's stanza in hcsecd.conf.
 *
 * Action purpose: replace is remove-then-append rather than an in-place edit,
 * so re-pairing a device with a different PIN cannot leave two stanzas for one
 * address -- hcsecd matches on the first and the second would be invisible and
 * wrong. Appending is what makes this safe: no byte above the append point is
 * touched, so comments, ordering and hand-written entries all survive.
 */
static gboolean bt_hcsecd_upsert(const char *bdaddr, const char *name,
                                 const char *pin) {
  char *text = bt_hcsecd_read(NULL);

  if (text == NULL) {
    g_warning("Could not read %s, so sofi will not write it either. Pairing "
              "needs `network-privilege-command` set to something that can "
              "read and replace a root-owned file.",
              BT_HCSECD_CONF);
    return FALSE;
  }

  GString *out = g_string_new(NULL);
  gsize start = 0;
  gsize end = 0;

  if (bt_hcsecd_find(text, bdaddr, &start, &end)) {
    g_string_append_len(out, text, start);
    g_string_append(out, text + end);
  } else {
    g_string_append(out, text);
  }

  if (out->len > 0 && out->str[out->len - 1] != '\n') {
    g_string_append_c(out, '\n');
  }

  char *stanza = bt_hcsecd_stanza(bdaddr, name, pin);
  g_string_append(out, stanza);
  memset(stanza, 0, strlen(stanza));
  g_free(stanza);

  gboolean ok = bt_install_root_file(out->str, BT_HCSECD_CONF);

  memset(out->str, 0, out->allocated_len);
  g_string_free(out, TRUE);
  g_free(text);

  if (!ok) {
    return FALSE;
  }

  return bt_hcsecd_reload();
}

/** Remove one device's stanza. Refuses the default entry. */
static gboolean bt_hcsecd_remove(const char *bdaddr) {
  if (g_ascii_strcasecmp(bdaddr, BT_DEFAULT_BDADDR) == 0) {
    /* Action purpose: hcsecd.conf's own comment says the default entry MUST
     * exist and MUST carry this address. Removing it would break pairing for
     * every device that has no stanza of its own, which is all of them until
     * one is made. */
    g_warning("%s is hcsecd's default entry and must not be removed.",
              BT_DEFAULT_BDADDR);
    return FALSE;
  }

  char *text = bt_hcsecd_read(NULL);

  if (text == NULL) {
    return FALSE;
  }

  gsize start = 0;
  gsize end = 0;

  if (!bt_hcsecd_find(text, bdaddr, &start, &end)) {
    /* Not an error: the device is not saved, which is the state asked for. */
    g_free(text);
    return TRUE;
  }

  GString *out = g_string_new(NULL);
  g_string_append_len(out, text, start);
  g_string_append(out, text + end);

  gboolean ok = bt_install_root_file(out->str, BT_HCSECD_CONF);

  g_string_free(out, TRUE);
  g_free(text);

  if (!ok) {
    return FALSE;
  }

  return bt_hcsecd_reload();
}

/* ------------------------------------------------------------------ hosts */

/** /etc/bluetooth/hosts is world-readable and is the one name source that
 * never needs privilege. Format is `BD_ADDR name [aliases...]`, hashes
 * comment. */
static void bt_read_hosts(GHashTable *names) {
  char *contents = NULL;

  if (!g_file_get_contents(BT_HOSTS, &contents, NULL, NULL)) {
    return;
  }

  char **lines = g_strsplit(contents, "\n", -1);
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    char *line = g_strstrip(lines[i]);
    if (*line == '\0' || *line == '#') {
      continue;
    }

    char *addr = bt_line_bdaddr(line);
    if (addr == NULL) {
      continue;
    }

    char **fields = g_strsplit_set(line, " \t", -1);
    char *name = NULL;
    gboolean seen_addr = FALSE;
    for (unsigned int f = 0; fields[f] != NULL; f++) {
      if (*fields[f] == '\0') {
        continue;
      }
      if (!seen_addr) {
        seen_addr = TRUE;
        continue;
      }
      name = fields[f];
      break;
    }

    if (name != NULL && *name != '\0') {
      g_hash_table_replace(names, g_strdup(addr), g_strdup(name));
    }

    g_strfreev(fields);
    g_free(addr);
  }

  g_strfreev(lines);
  g_free(contents);
}

/** Devices bthidd is configured for. Its config may not exist at all, which is
 * the state on a machine that has never set up an input device, and is why the
 * failure is silent rather than warned about. */
static void bt_read_hids(GHashTable *hids) {
  char *out = NULL;
  const char *argv[] = {"bthidcontrol", "known", NULL};

  if (!bt_run(argv, &out) || out == NULL) {
    g_free(out);
    return;
  }

  char **lines = g_strsplit(out, "\n", -1);
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    char *addr = bt_line_bdaddr(lines[i]);
    if (addr != NULL) {
      g_hash_table_replace(hids, addr, NULL);
    }
  }

  g_strfreev(lines);
  g_free(out);
}

/* ------------------------------------------------------------------ audio */

/**
 * Function purpose: work out whether there is a route from a connected headset
 * to a speaker, in four states.
 *
 * Action purpose: **this must not be probed by running `virtual_oss -h`.** The
 * daemon opens /dev/cuse before it prints anything, and that node is `0600
 * root` -- so as an ordinary user the probe fails on a machine where everything
 * is correctly installed, and reports a working setup as broken. Being in the
 * `operator` group does not help, because the mode grants the group nothing.
 *
 * So each piece is tested for what it is:
 *
 *   virtual_oss     on $PATH. It is a **base system** daemon, which is why it
 *                   is absent from the pkg database -- that is not a sign of a
 *                   stray binary.
 *   voss_bt.so      a **dynamically loaded backend**, not a binary, from the
 *                   audio/virtual_oss_bluetooth port. $PATH cannot find a
 *                   plugin, so it is probed by path.
 *   /dev/cuse       the node virtual_oss needs. Its absence means the module is
 *                   not loaded (`cuse_load="YES"`).
 *
 * The one that matters most is the middle: **it is the only state a user fixes
 * by installing something**, and reporting it as either of the others sends
 * them somewhere useless. That is R10's finding, and it is why four states are
 * carried rather than a boolean.
 */
static BtAudioRoute bt_probe_audio(char **binary_out) {
  char *found = g_find_program_in_path("virtual_oss");

  *binary_out = NULL;

  if (found == NULL) {
    return BT_AUDIO_NO_DAEMON;
  }

  gboolean have_backend = FALSE;
  for (unsigned int i = 0; i < G_N_ELEMENTS(bt_voss_backends); i++) {
    if (g_file_test(bt_voss_backends[i], G_FILE_TEST_EXISTS)) {
      have_backend = TRUE;
      break;
    }
  }

  if (!have_backend) {
    g_free(found);
    return BT_AUDIO_NO_BACKEND;
  }

  if (!g_file_test(BT_CUSE_DEVICE, G_FILE_TEST_EXISTS)) {
    g_free(found);
    return BT_AUDIO_NO_CUSE;
  }

  *binary_out = found;

  return BT_AUDIO_READY;
}

/**
 * Function purpose: hand a connected audio device to `virtual_oss(8)`.
 *
 * Action purpose: three things here are load-bearing and each would be a defect
 * if dropped.
 *
 * **`-B`, so it runs in the background.** `virtual_oss` is a daemon that stays
 * up to serve the device it creates. Without `-B` this call never returns and
 * the menu hangs for as long as the headset is connected.
 *
 * **The privilege command, because /dev/cuse is `0600 root`.** Not a
 * convenience: without it the daemon exits before opening the device.
 *
 * **Duplex or playback-only, chosen from the class.** A headset, hands-free
 * unit or microphone has an input worth capturing, so it gets `-f`; headphones
 * and speakers get `-R /dev/null -P`, because asking for a recording channel a
 * device does not have makes the daemon fail to start. This is the class decode
 * earning its place a second time -- the same information that picks between a
 * bthidd stanza and this function also picks the argument list inside it.
 *
 * The invocation follows virtual_oss(8)'s own two bluetooth examples verbatim,
 * including the 48kHz/16-bit/4ms figures, rather than parameters chosen here.
 */
static gboolean bt_audio_attach(BluetoothModePrivateData *pd,
                                const BtRow *row) {
  char *device = g_strconcat(BT_AUDIO_DEV_PREFIX, row->id, NULL);
  gboolean duplex = row->devkind == BT_KIND_HEADSET ||
                    row->devkind == BT_KIND_HANDSFREE ||
                    row->devkind == BT_KIND_MICROPHONE;
  gboolean ok = FALSE;

  if (duplex) {
    const char *argv[] = {pd->audio_binary, "-B", "-C", "2",  "-c",     "2",
                          "-r",  "48000",   "-b", "16", "-s", "4ms",
                          "-f",  device,    "-d", "dsp", NULL};
    ok = bt_run_priv(argv, NULL, FALSE);
  } else {
    const char *argv[] = {pd->audio_binary, "-B", "-C", "2",  "-c",  "2",
                          "-r",  "48000",   "-b", "16", "-s", "4ms",
                          "-R",  "/dev/null", "-P", device, "-d", "dsp", NULL};
    ok = bt_run_priv(argv, NULL, FALSE);
  }

  g_free(device);

  return ok;
}

/* ------------------------------------------------------------ enumeration */

static BtRow *bt_row_new(GPtrArray *rows, BtRowKind kind) {
  BtRow *row = g_malloc0(sizeof(*row));

  row->kind = kind;
  row->handle = -1;
  g_ptr_array_add(rows, row);

  return row;
}

/**
 * Function purpose: find the adapter, and with it decide whether the stack is
 * up at all.
 *
 * Action purpose: this is the runtime-selection discipline the backend vtable
 * used to carry. `hccontrol` being installed proves nothing -- it is in the
 * base system on every FreeBSD machine ever built. What proves the stack is
 * running is a node in `read_node_list`, so that is the test.
 *
 * Output is a header line then `%-15s %08x %9d`, from node.c:476.
 */
static char *bt_find_node(void) {
  char *out = NULL;
  const char *argv[] = {"hccontrol", "read_node_list", NULL};

  if (!bt_run(argv, &out) || out == NULL) {
    g_free(out);
    return NULL;
  }

  char **lines = g_strsplit(out, "\n", -1);
  char *node = NULL;

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    char *line = g_strstrip(lines[i]);
    if (*line == '\0' || g_str_has_prefix(line, "Name")) {
      continue;
    }

    char **fields = g_strsplit_set(line, " \t", -1);
    if (fields[0] != NULL && *fields[0] != '\0') {
      node = g_strdup(fields[0]);
    }
    g_strfreev(fields);

    if (node != NULL) {
      break;
    }
  }

  g_strfreev(lines);
  g_free(out);

  return node;
}

/**
 * Function purpose: the USB device behind a netgraph node.
 *
 * Action purpose: `service bluetooth start|stop` takes `ubt0`, not `ubt0hci`.
 * The node is named by appending `hci` to the device, so the device is the node
 * with that suffix removed -- and if the suffix is not there, the node name is
 * passed through unchanged rather than mangled.
 */
static char *bt_device_from_node(const char *node) {
  if (node == NULL) {
    return NULL;
  }

  if (g_str_has_suffix(node, "hci")) {
    return g_strndup(node, strlen(node) - 3);
  }

  return g_strdup(node);
}

/** Connected devices, by address, with their connection handles. */
static void bt_read_connections(const char *node, GHashTable *handles) {
  char *out = NULL;

  if (!bt_hci(node, &out, "read_connection_list", NULL) || out == NULL) {
    g_free(out);
    return;
  }

  char **lines = g_strsplit(out, "\n", -1);
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    if (g_str_has_prefix(lines[i], "Remote")) {
      continue;
    }

    char *addr = bt_line_bdaddr(lines[i]);
    if (addr == NULL) {
      continue;
    }

    /* The handle is the field after the address: `%-17.17s %6d`. */
    const char *rest = strstr(lines[i], addr);
    int handle = -1;
    if (rest != NULL) {
      handle = atoi(rest + 17);
    }

    g_hash_table_replace(handles, addr, GINT_TO_POINTER(handle));
  }

  g_strfreev(lines);
  g_free(out);
}

/** Addresses sitting in the controller's neighbour cache. Unprivileged, and
 * instant, which is what makes opening the pane free. It carries no name and
 * no class -- see the file header. */
static void bt_read_neighbours(const char *node, GHashTable *seen) {
  char *out = NULL;

  if (!bt_hci(node, &out, "read_neighbor_cache", NULL) || out == NULL) {
    g_free(out);
    return;
  }

  char **lines = g_strsplit(out, "\n", -1);
  for (unsigned int i = 0; lines[i] != NULL; i++) {
    /* Data rows begin with the address-type column; the advertising-data lines
     * that follow each entry are tab-indented, and the header starts "T ". */
    if (lines[i][0] == '\0' || lines[i][0] == '\t' || lines[i][0] == ' ' ||
        g_str_has_prefix(lines[i], "T ")) {
      continue;
    }

    char *addr = bt_line_bdaddr(lines[i]);
    if (addr != NULL) {
      g_hash_table_replace(seen, addr, NULL);
    }
  }

  g_strfreev(lines);
  g_free(out);
}

/**
 * Function purpose: run one inquiry and record what answered, with classes.
 *
 * Action purpose: the inquiry is the only source of a class-of-device for a
 * device that is not connected, which is why its results go into the private
 * data's class table and survive every later refresh. Format, from
 * link_control.c:153: `Inquiry result #N` then tab-indented `BD_ADDR:` and
 * `Class:` lines.
 *
 * This blocks for roughly ::BT_INQUIRY_LENGTH times 1.28 seconds. The bound is
 * the controller's, not sofi's -- it ends the inquiry itself.
 */
static unsigned int bt_inquiry(const char *node, GHashTable *seen,
                               GHashTable *classes) {
  char *out = NULL;

  if (!bt_hci(node, &out, "inquiry", BT_INQUIRY_LAP, BT_INQUIRY_LENGTH,
              BT_INQUIRY_RESPONSES, NULL) ||
      out == NULL) {
    g_free(out);
    return 0;
  }

  char **lines = g_strsplit(out, "\n", -1);
  char *current = NULL;
  unsigned int found = 0;

  for (unsigned int i = 0; lines[i] != NULL; i++) {
    char *line = g_strstrip(lines[i]);

    if (g_str_has_prefix(line, "BD_ADDR:")) {
      g_free(current);
      current = bt_line_bdaddr(line);
      if (current != NULL) {
        g_hash_table_replace(seen, g_strdup(current), NULL);
        found++;
      }
      continue;
    }

    if (g_str_has_prefix(line, "Class:") && current != NULL) {
      guint32 cod = 0;
      if (bt_parse_cod(line + strlen("Class:") + 1, &cod)) {
        g_hash_table_replace(classes, g_strdup(current),
                             GUINT_TO_POINTER(cod + 1));
      }
    }
  }

  g_free(current);
  g_strfreev(lines);
  g_free(out);

  return found;
}

/** Ask a device its name over the air. Only used for devices that have no name
 * from any cheaper source, because it needs the device awake and takes a
 * moment. */
static char *bt_remote_name(const char *node, const char *bdaddr) {
  char *out = NULL;

  if (!bt_hci(node, &out, "remote_name_request", bdaddr, NULL) ||
      out == NULL) {
    g_free(out);
    return NULL;
  }

  char *name = bt_field(out, "Name");
  g_free(out);

  if (name != NULL && *name == '\0') {
    g_free(name);
    return NULL;
  }

  return name;
}

/* --------------------------------------------------------------- refresh */

static void bt_set_status(BluetoothModePrivateData *pd, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

static void bt_set_status(BluetoothModePrivateData *pd, const char *fmt, ...) {
  va_list args;

  g_free(pd->status);
  va_start(args, fmt);
  pd->status = g_strdup_vprintf(fmt, args);
  va_end(args);
}

/** Read everything the adapter will tell us about itself. */
static void bt_read_adapter(BluetoothModePrivateData *pd) {
  char *out = NULL;

  g_clear_pointer(&pd->bdaddr, g_free);
  g_clear_pointer(&pd->local_name, g_free);
  pd->scan_enable = -1;

  if (pd->node == NULL) {
    return;
  }

  if (bt_hci(pd->node, &out, "read_bd_addr", NULL)) {
    pd->bdaddr = bt_field(out, "BD_ADDR");
    g_free(out);
    out = NULL;
  }

  if (bt_hci(pd->node, &out, "read_local_name", NULL)) {
    pd->local_name = bt_field(out, "Local name");
    g_free(out);
    out = NULL;
  }

  if (bt_hci(pd->node, &out, "read_scan_enable", NULL)) {
    /* `Scan enable: <words> [0x02]`. The words vary; the bracketed value does
     * not, so that is what is parsed. */
    const char *bracket = out != NULL ? strchr(out, '[') : NULL;
    if (bracket != NULL) {
      pd->scan_enable = (int)strtol(bracket + 1, NULL, 0);
    }
    g_free(out);
    out = NULL;
  }
}

/**
 * Function purpose: rebuild the whole row list from every source.
 *
 * Action purpose: the ordering is the point. The adapter comes first because it
 * answers "is this thing even on", which is the question a user has before any
 * other. Connected devices come next, then paired, then merely in range, then
 * the maintenance verbs -- frequency of use, with the destructive ones last.
 */
static void bt_refresh(BluetoothModePrivateData *pd) {
  g_ptr_array_set_size(pd->rows, 0);

  g_clear_pointer(&pd->node, g_free);
  g_clear_pointer(&pd->device, g_free);

  pd->node = bt_find_node();
  pd->device = bt_device_from_node(pd->node);

  bt_read_adapter(pd);

  /* ------------------------------------------------------------- adapter */
  BtRow *adapter = bt_row_new(pd->rows, BT_ROW_ADAPTER);
  adapter->id = g_strdup(pd->device != NULL ? pd->device : "");
  adapter->active = pd->node != NULL;

  if (pd->node == NULL) {
    adapter->label = g_strdup("Bluetooth is off");
    adapter->detail = g_strdup("Enter to start the stack");
    /* Nothing below this can be listed with the stack down, so the rest of the
     * enumeration is skipped rather than run against a node that is not
     * there. */
    return;
  }

  adapter->label = g_strdup(pd->local_name != NULL && *pd->local_name != '\0'
                                ? pd->local_name
                                : "Bluetooth");
  adapter->detail =
      g_strdup_printf("%s%s%s", pd->bdaddr != NULL ? pd->bdaddr : pd->node,
                      pd->scan_enable >= 0 ? " · " : "",
                      pd->scan_enable < 0        ? ""
                      : (pd->scan_enable & 0x01) ? "discoverable"
                      : (pd->scan_enable & 0x02) ? "connectable"
                                                 : "not scanning");

  /* --------------------------------------------------------------- names */
  g_hash_table_remove_all(pd->names);
  g_hash_table_remove_all(pd->paired);
  g_hash_table_remove_all(pd->hids);

  bt_read_hosts(pd->names);
  bt_read_hids(pd->hids);

  gboolean used_priv = FALSE;
  char *hcsecd = bt_hcsecd_read(&used_priv);
  pd->have_paired = hcsecd != NULL;
  pd->paired_needed_privilege = used_priv;
  if (hcsecd != NULL) {
    bt_hcsecd_collect(hcsecd, pd->paired, pd->names);
    g_free(hcsecd);
  }

  /* ------------------------------------------------------------- devices */
  GHashTable *handles =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GHashTable *seen =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  bt_read_connections(pd->node, handles);
  bt_read_neighbours(pd->node, seen);

  /* One key set over all three sources, so a device that is paired AND
   * connected AND in range is one row and not three. */
  GHashTable *all =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GHashTableIter iter;
  gpointer key = NULL;
  gpointer value = NULL;

  g_hash_table_iter_init(&iter, handles);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    g_hash_table_replace(all, g_strdup(key), NULL);
  }
  g_hash_table_iter_init(&iter, pd->paired);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    g_hash_table_replace(all, g_strdup(key), NULL);
  }
  g_hash_table_iter_init(&iter, seen);
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    g_hash_table_replace(all, g_strdup(key), NULL);
  }

  GList *addresses = g_hash_table_get_keys(all);
  addresses = g_list_sort(addresses, (GCompareFunc)g_strcmp0);

  for (GList *node = addresses; node != NULL; node = g_list_next(node)) {
    const char *addr = node->data;

    BtRow *row = bt_row_new(pd->rows, BT_ROW_DEVICE);
    row->id = g_strdup(addr);
    row->connected = g_hash_table_contains(handles, addr);
    row->paired = g_hash_table_contains(pd->paired, addr);
    row->in_range = g_hash_table_contains(seen, addr) || row->connected;
    row->hid_configured = g_hash_table_contains(pd->hids, addr);
    row->active = row->connected;

    if (row->connected) {
      row->handle = GPOINTER_TO_INT(g_hash_table_lookup(handles, addr));
    }

    gpointer cod = g_hash_table_lookup(pd->classes, addr);
    if (cod != NULL) {
      row->cod = (guint32)(GPOINTER_TO_UINT(cod) - 1);
      row->have_cod = TRUE;
      row->devkind = bt_kind_from_cod(row->cod);
    }

    const char *name = g_hash_table_lookup(pd->names, addr);
    if (name == NULL && row->connected) {
      /* Only for connected devices: the link is already up, so asking costs
       * nothing extra and a connected device with no name is the case where a
       * bare address is least useful. */
      char *remote = bt_remote_name(pd->node, addr);
      if (remote != NULL) {
        g_hash_table_replace(pd->names, g_strdup(addr), remote);
        name = g_hash_table_lookup(pd->names, addr);
      }
    }

    row->label = g_strdup(name != NULL ? name : addr);

    GString *detail = g_string_sized_new(64);
    if (name != NULL) {
      g_string_append(detail, addr);
    }
    const char *kindname = bt_kind_name(row->devkind);
    if (kindname != NULL) {
      g_string_append_printf(detail, "%s%s", detail->len > 0 ? " · " : "",
                             kindname);
    }
    if (row->connected) {
      g_string_append_printf(detail, "%sconnected", detail->len > 0 ? " · " : "");
    }
    if (row->paired) {
      g_string_append_printf(detail, "%spaired", detail->len > 0 ? " · " : "");
    }
    if (!row->paired && !row->connected && row->in_range) {
      g_string_append_printf(detail, "%sin range",
                             detail->len > 0 ? " · " : "");
    }
    /* Action purpose: a paired keyboard, mouse or gamepad that bthidd has no
     * stanza for produces no input at all, and that is invisible otherwise --
     * it pairs, it connects, and nothing happens. Saying so on the row is the
     * difference between a two-second fix and an evening. */
    if (bt_kind_is_hid(row->devkind) && !row->hid_configured) {
      g_string_append_printf(detail, "%sneeds input setup (Alt+4)",
                             detail->len > 0 ? " · " : "");
    }
    /* Action purpose: an audio device says what is stopping it, not merely
     * that something is. "needs virtual_oss_bluetooth" is a package to
     * install; "no audio path" would send the reader to look for a fault. */
    if (bt_kind_is_audio(row->devkind)) {
      const char *audio_note = NULL;
      switch (pd->audio_route) {
      case BT_AUDIO_READY:
        audio_note = row->connected ? "audio: Alt+4" : NULL;
        break;
      case BT_AUDIO_NO_BACKEND:
        audio_note = "needs virtual_oss_bluetooth";
        break;
      case BT_AUDIO_NO_CUSE:
        audio_note = "needs cuse(3)";
        break;
      case BT_AUDIO_NO_DAEMON:
      default:
        audio_note = "no virtual_oss";
        break;
      }
      if (audio_note != NULL) {
        g_string_append_printf(detail, "%s%s", detail->len > 0 ? " · " : "",
                               audio_note);
      }
    }

    row->detail = g_string_free(detail, FALSE);
  }

  g_list_free(addresses);
  g_hash_table_destroy(all);
  g_hash_table_destroy(seen);
  g_hash_table_destroy(handles);

  /* ------------------------------------------------------------- actions */
  BtRow *discoverable = bt_row_new(pd->rows, BT_ROW_ACTION);
  discoverable->action = BT_ACTION_DISCOVERABLE;
  discoverable->active = pd->scan_enable > 0 && (pd->scan_enable & 0x01) != 0;
  discoverable->label = g_strdup(discoverable->active
                                     ? "Stop being discoverable"
                                     : "Make this machine discoverable");
  discoverable->detail =
      g_strdup("Other devices can find this one while it is on");

  BtRow *reset = bt_row_new(pd->rows, BT_ROW_ACTION);
  reset->action = BT_ACTION_RESET;
  reset->label = g_strdup("Reset the controller");
  reset->detail = g_strdup("Drops every link; the stack stays up");

  BtRow *restart = bt_row_new(pd->rows, BT_ROW_ACTION);
  restart->action = BT_ACTION_RESTART_STACK;
  restart->label = g_strdup("Restart the bluetooth stack");
  restart->detail = g_strdup("bluetooth, hcsecd and bthidd");
}

static BtRow *bt_row_at(const BluetoothModePrivateData *pd,
                        unsigned int line) {
  if (pd == NULL || line >= pd->rows->len) {
    return NULL;
  }
  return g_ptr_array_index(pd->rows, line);
}

/* ----------------------------------------------------------------- verbs */

/* Forward declaration: the connect path performs the input-device setup itself
 * for HID-class devices, and the setup needs the row that connect is holding.
 * Defining it above connect would put a hundred lines of bthidd.conf handling
 * between the two verbs a reader is comparing. */
static gboolean bt_setup_hid(BluetoothModePrivateData *pd, BtRow *row);

/** Bring the whole stack up or down. */
static gboolean bt_power(BluetoothModePrivateData *pd, gboolean on) {
  /* Action purpose: `service bluetooth` takes the USB device name, so when the
   * stack is already down and there is no node to derive one from, ubt0 is the
   * only reasonable guess -- it is what the handbook, rc.conf examples and
   * every single-adapter machine use. Named as a guess rather than presented as
   * a fact. */
  const char *device = pd->device != NULL && *pd->device != '\0' ? pd->device
                                                                 : "ubt0";
  const char *argv[] = {"service", "bluetooth", on ? "start" : "stop", device,
                        NULL};

  return bt_run_priv(argv, NULL, FALSE);
}

/** Connect, and for an input device do the second half that makes it work. */
static gboolean bt_connect(BluetoothModePrivateData *pd, BtRow *row) {
  if (!bt_hci_priv(pd->node, "create_connection", row->id, NULL)) {
    return FALSE;
  }

  /* Action purpose: pairing and connecting a keyboard, mouse or gamepad does
   * not give it a driver. bthidd needs a stanza in its own configuration file
   * before a single keystroke arrives, so the connect path writes one -- doing
   * it here rather than leaving it to Alt+4 is the difference between the
   * device working and the device appearing to be ignored. Failure is not fatal
   * to the connection, which is real either way, so it is reported and not
   * rolled back. */
  if (bt_kind_is_hid(row->devkind) && !row->hid_configured) {
    if (!bt_setup_hid(pd, row)) {
      bt_set_status(pd, "%s is connected but bthidd could not be configured; "
                        "it will not send input yet.",
                    row->label);
      return TRUE;
    }
  }

  return TRUE;
}

static gboolean bt_disconnect(BluetoothModePrivateData *pd, BtRow *row) {
  if (row->handle < 0) {
    bt_set_status(pd, "%s is not connected.", row->label);
    return FALSE;
  }

  char *handle = g_strdup_printf("%d", row->handle);
  gboolean ok =
      bt_hci_priv(pd->node, "disconnect", handle, BT_DISCONNECT_REASON);
  g_free(handle);

  return ok;
}

/**
 * Function purpose: pair a device, which on this stack means giving hcsecd the
 * PIN before the link is made.
 *
 * Action purpose: the order matters and is not interchangeable. hcsecd answers
 * the controller's PIN request from its configuration, so the stanza has to be
 * in the file and the daemon has to have re-read it **before** the connection
 * is attempted. Doing it the other way round produces a pairing failure that
 * looks exactly like a wrong PIN.
 *
 * The R5 rule applies: nothing is reported as paired until the connection is
 * observed, and a failed connection removes the stanza again rather than
 * leaving a half-pairing behind that would make the row lie on the next open.
 */
static gboolean bt_pair(BluetoothModePrivateData *pd, BtRow *row) {
  sofi_view_hide();

  char *pin = bt_prompt_pin(row->label);
  if (pin == NULL) {
    bt_set_status(pd, "Cancelled.");
    return FALSE;
  }

  gboolean ok = bt_hcsecd_upsert(row->id, row->label, pin);

  memset(pin, 0, strlen(pin));
  g_free(pin);

  if (!ok) {
    bt_set_status(pd, "Could not write the pairing entry for %s.", row->label);
    return FALSE;
  }

  if (!bt_connect(pd, row)) {
    /* Action purpose: roll the stanza back. R5's defect was a half-applied
     * change persisted after the thing it was for had failed, and it cost USER
     * their network. A stanza left here would show the device as paired on
     * every later open while nothing about it worked. */
    if (!bt_hcsecd_remove(row->id)) {
      g_warning("Pairing %s failed and its hcsecd.conf entry could not be "
                "removed. Check %s by hand.",
                row->id, BT_HCSECD_CONF);
    }
    bt_set_status(pd, "Pairing %s failed; nothing was saved.", row->label);
    return FALSE;
  }

  bt_set_status(pd, "Paired and connected %s.", row->label);

  return TRUE;
}

/**
 * Function purpose: undo everything that makes a device known, in all three
 * places it can be recorded.
 *
 * Action purpose: forgetting from one of them is worse than forgetting from
 * none, because the device then half-reconnects from whichever record survived
 * and the menu says something untrue about it. The stored link key, the hcsecd
 * stanza and the bthidd entry all go.
 */
static gboolean bt_forget(BluetoothModePrivateData *pd, BtRow *row) {
  gboolean any = FALSE;

  if (row->connected) {
    bt_disconnect(pd, row);
  }

  if (bt_hcsecd_remove(row->id)) {
    any = TRUE;
  }

  /* Best effort from here: a controller that stores no keys and a bthidd with
   * no entry for this device both fail, and neither is a problem. */
  if (bt_hci_priv(pd->node, "delete_stored_link_key", row->id, NULL)) {
    any = TRUE;
  }

  if (row->hid_configured) {
    const char *argv[] = {"bthidcontrol", "-a", row->id, "forget", NULL};
    if (bt_run_priv(argv, NULL, TRUE)) {
      any = TRUE;
    }
  }

  return any;
}

/**
 * Function purpose: write the bthidd stanza that turns a paired input device
 * into one that actually sends input.
 *
 * Action purpose: `bthidcontrol Query` interrogates the device over SDP and
 * prints a complete bthidd.conf stanza -- HID descriptor and all -- so sofi
 * neither invents nor parses that content, it only appends it. The file is
 * appended to rather than rewritten because it may not exist at all, which is
 * the normal state on a machine that has never had a bluetooth input device,
 * and because everything already in it belongs to other devices.
 */
static gboolean bt_setup_hid(BluetoothModePrivateData *pd, BtRow *row) {
  char *stanza = NULL;
  const char *query[] = {"bthidcontrol", "-a", row->id, "query", NULL};

  if (!bt_run(query, &stanza) || stanza == NULL || *stanza == '\0') {
    g_free(stanza);
    g_warning("`bthidcontrol -a %s query` produced nothing. The device has to "
              "be connected and answering SDP for this to work.",
              row->id);
    return FALSE;
  }

  char *existing = NULL;
  if (!g_file_get_contents(BT_BTHIDD_CONF, &existing, NULL, NULL)) {
    const char *argv[] = {"cat", BT_BTHIDD_CONF, NULL};
    if (!bt_run_priv(argv, &existing, TRUE)) {
      /* Not an error: bthidd.conf legitimately does not exist until the first
       * input device is set up, and this is that moment. */
      existing = g_strdup("");
    }
  }

  GString *out = g_string_new(existing != NULL ? existing : "");
  g_free(existing);

  if (out->len > 0 && out->str[out->len - 1] != '\n') {
    g_string_append_c(out, '\n');
  }
  g_string_append_c(out, '\n');
  g_string_append(out, stanza);
  if (out->str[out->len - 1] != '\n') {
    g_string_append_c(out, '\n');
  }

  g_free(stanza);

  gboolean ok = bt_install_root_file(out->str, BT_BTHIDD_CONF);
  g_string_free(out, TRUE);

  if (!ok) {
    return FALSE;
  }

  /* bthidd reads its configuration at start only, so this is a restart and not
   * a reload. It briefly drops any other input device it is carrying, which is
   * why it is not done on every connect. */
  const char *restart[] = {"service", "bthidd", "restart", NULL};
  if (!bt_run_priv(restart, NULL, FALSE)) {
    g_warning("%s was written but bthidd could not be restarted, so %s will "
              "not send input until it is.",
              BT_BTHIDD_CONF, row->id);
    return FALSE;
  }

  g_hash_table_replace(pd->hids, g_strdup(row->id), NULL);

  return TRUE;
}

/**
 * Function purpose: do the class-appropriate setup for a device.
 *
 * Action purpose: this is the verb the class decode exists for. One key means
 * "write a bthidd stanza and restart the daemon" on a gamepad and "hand this to
 * the audio route" on a headset, and the user does not have to know which --
 * which is the entire practical value of reading the class rather than the
 * name.
 */
static gboolean bt_setup(BluetoothModePrivateData *pd, BtRow *row) {
  if (bt_kind_is_hid(row->devkind)) {
    if (bt_setup_hid(pd, row)) {
      bt_set_status(pd, "%s is set up as an input device.", row->label);
      return TRUE;
    }
    bt_set_status(pd, "Could not set %s up as an input device.", row->label);
    return FALSE;
  }

  if (bt_kind_is_audio(row->devkind)) {
    /* Action purpose: attaching audio to a device that is not connected would
     * start a daemon against a link that does not exist, and virtual_oss would
     * sit there failing rather than reporting anything this menu could show. */
    if (!row->connected) {
      bt_set_status(pd, "Connect %s first.", row->label);
      return FALSE;
    }

    switch (pd->audio_route) {
    case BT_AUDIO_READY:
      if (bt_audio_attach(pd, row)) {
        bt_set_status(pd, "%s is on /dev/dsp via virtual_oss.", row->label);
        return TRUE;
      }
      bt_set_status(pd, "virtual_oss would not take %s.", row->label);
      return FALSE;

    case BT_AUDIO_NO_BACKEND:
      /* The only one of these a user fixes by installing something, so it is
       * the only one that names a package. */
      bt_set_status(pd, "Bluetooth audio needs the virtual_oss backend: "
                        "pkg install virtual_oss_bluetooth");
      return FALSE;

    case BT_AUDIO_NO_CUSE:
      bt_set_status(pd, "virtual_oss needs cuse(3): kldload cuse, or "
                        "cuse_load=\"YES\" in loader.conf.");
      return FALSE;

    case BT_AUDIO_NO_DAEMON:
    default:
      bt_set_status(pd, "virtual_oss is not on $PATH, so %s connects but "
                        "cannot carry audio.",
                    row->label);
      return FALSE;
    }
  }

  bt_set_status(pd, "Nothing to set up for %s.", row->label);

  return FALSE;
}

/** Do whatever the highlighted row means. */
static gboolean bt_activate(BluetoothModePrivateData *pd, BtRow *row,
                            gboolean *should_exit) {
  *should_exit = FALSE;

  switch (row->kind) {
  case BT_ROW_ADAPTER:
    if (bt_power(pd, !row->active)) {
      bt_set_status(pd, "Bluetooth %s.", row->active ? "off" : "on");
      return TRUE;
    }
    bt_set_status(pd, "Could not turn bluetooth %s.",
                  row->active ? "off" : "on");
    return FALSE;

  case BT_ROW_DEVICE:
    if (row->connected) {
      if (bt_disconnect(pd, row)) {
        bt_set_status(pd, "Disconnected %s.", row->label);
        return TRUE;
      }
      bt_set_status(pd, "Could not disconnect %s.", row->label);
      return FALSE;
    }

    /* Action purpose: an unpaired device needs a PIN before a link can be
     * made, and a paired one must not be asked for one again -- retyping a PIN
     * that already works can only make it wrong. So Enter means "pair and
     * connect" or plain "connect" depending on what the row already is, and
     * the user does not have to choose between two keys. */
    if (!row->paired) {
      return bt_pair(pd, row);
    }

    if (bt_connect(pd, row)) {
      if (pd->status == NULL) {
        bt_set_status(pd, "Connected %s.", row->label);
      }
      return TRUE;
    }
    bt_set_status(pd, "Could not connect %s.", row->label);
    return FALSE;

  case BT_ROW_ACTION:
    switch (row->action) {
    case BT_ACTION_DISCOVERABLE: {
      /* Page scan is left on either way: turning it off would stop already
       * paired devices reconnecting, which is not what "discoverable" means to
       * anyone. */
      const char *value = row->active ? "2" : "3";
      if (bt_hci_priv(pd->node, "write_scan_enable", value, NULL)) {
        bt_set_status(pd, "%s.",
                      row->active ? "No longer discoverable" : "Discoverable");
        return TRUE;
      }
      bt_set_status(pd, "Could not change discoverability.");
      return FALSE;
    }

    case BT_ACTION_RESET:
      if (bt_hci_priv(pd->node, "reset", NULL, NULL)) {
        bt_set_status(pd, "Controller reset; every link was dropped.");
        return TRUE;
      }
      bt_set_status(pd, "Could not reset the controller.");
      return FALSE;

    case BT_ACTION_RESTART_STACK: {
      /* Action purpose: this drops every link and takes several seconds, and
       * the list it would reload is meaningless while the daemons come back.
       * It closes the panel rather than reloading, on the same reasoning as the
       * network mode's reset-all. */
      sofi_view_hide();

      const char *bluetooth[] = {"service", "bluetooth", "restart",
                                 pd->device != NULL ? pd->device : "ubt0",
                                 NULL};
      const char *hcsecd[] = {"service", "hcsecd", "restart", NULL};
      const char *bthidd[] = {"service", "bthidd", "restart", NULL};

      bt_run_priv(bluetooth, NULL, FALSE);
      bt_run_priv(hcsecd, NULL, FALSE);
      bt_run_priv(bthidd, NULL, FALSE);

      *should_exit = TRUE;
      return TRUE;
    }
    }
    return FALSE;

  case BT_ROW_HEADING:
  default:
    return FALSE;
  }
}

/* ------------------------------------------------------------------- mode */

static int bluetooth_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  BluetoothModePrivateData *pd = g_malloc0(sizeof(*pd));
  pd->rows = g_ptr_array_new_with_free_func(bt_row_free);
  pd->names = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  pd->paired = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  pd->classes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  pd->hids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  pd->scan_enable = -1;
  mode_set_private_data(sw, (void *)pd);

  pd->audio_route = bt_probe_audio(&pd->audio_binary);

  char *found = g_find_program_in_path("hccontrol");
  if (found == NULL) {
    g_warning("hccontrol is not on $PATH. It is part of the FreeBSD base "
              "system; on any other platform this mode has nothing to drive, "
              "because sofi speaks the netgraph bluetooth stack and not "
              "BlueZ.");
    return FALSE;
  }
  g_free(found);

  /* Action purpose: TRUE even with the stack down. Unlike the network mode
   * there is a useful surface either way -- the adapter row says bluetooth is
   * off and Enter turns it on, which is precisely what someone opening this
   * menu with the adapter off wants. Refusing to open would hide the one verb
   * that fixes it. */
  bt_refresh(pd);

  return TRUE;
}

static unsigned int bluetooth_mode_get_num_entries(const Mode *sw) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return 0;
  }
  return pd->rows->len;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);
  BtRow *row = bt_row_at(pd, selected_line);

  if (row == NULL) {
    return get_entry ? g_strdup("") : NULL;
  }

  if (row->active) {
    *state |= ACTIVE;
  }
  /* Action purpose: the same reading the volume pane gives a muted sink and the
   * network pane gives an interface that is down -- present, but not doing its
   * job. A paired device that is out of range, and the adapter when the stack
   * is off, are both exactly that. */
  if (row->kind == BT_ROW_ADAPTER && !row->active) {
    *state |= URGENT;
  }
  if (row->kind == BT_ROW_DEVICE && !row->connected && !row->in_range) {
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
  case BT_ROW_DEVICE:
    text = g_strdup_printf("%s  %s<span alpha='65%%'>  %s</span>",
                           bt_kind_glyph(row->devkind), label,
                           detail != NULL ? detail : "");
    break;

  case BT_ROW_ADAPTER:
    text = g_strdup_printf("<b>%s</b><span alpha='65%%'>  %s</span>", label,
                           detail != NULL ? detail : "");
    break;

  case BT_ROW_ACTION:
  case BT_ROW_HEADING:
  default:
    text = g_strdup_printf("<span alpha='85%%'>%s</span><span alpha='55%%'>"
                           "%s%s</span>",
                           label, detail != NULL ? "  " : "",
                           detail != NULL ? detail : "");
    break;
  }

  g_free(label);
  g_free(detail);

  return text;
}

/**
 * Function purpose: state the verbs and the stack's condition in the message
 * bar.
 *
 * The last thing that happened is shown in preference to the hints, because
 * after pressing a key the question is "did that work", not "what are the
 * keys". It falls back to the hints once there is nothing to report.
 */
static char *bluetooth_mode_get_message(const Mode *sw) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return NULL;
  }

  if (pd->status != NULL) {
    return g_markup_printf_escaped("%s", pd->status);
  }

  if (pd->node == NULL) {
    return g_markup_printf_escaped(
        "Bluetooth is off  ·  Enter starts the netgraph stack");
  }

  /* Action purpose: which of these two is true decides whether the paired
   * column can be trusted, so it is stated rather than left to be inferred from
   * an unexpectedly short list. */
  const char *paired_note =
      pd->have_paired
          ? ""
          : "  ·  paired list needs network-privilege-command";

  return g_markup_printf_escaped(
      "Enter connect  ·  Alt+1 scan  ·  Alt+2 disconnect  ·  Alt+3 forget  ·  "
      "Alt+4 set up  ·  netgraph %s%s",
      pd->node, paired_note);
}

static ModeMode bluetooth_mode_result(Mode *sw, int mretv,
                                      G_GNUC_UNUSED char **input,
                                      unsigned int selected_line) {
  BluetoothModePrivateData *pd =
      (BluetoothModePrivateData *)mode_get_private_data(sw);

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

  BtRow *row = bt_row_at(pd, selected_line);

  if (mretv & MENU_OK) {
    if (row == NULL) {
      return RELOAD_DIALOG;
    }

    gboolean should_exit = FALSE;
    gboolean changed = bt_activate(pd, row, &should_exit);

    if (should_exit) {
      return MODE_EXIT;
    }
    if (changed) {
      bt_refresh(pd);
    }
    return RELOAD_DIALOG;
  }

  if (mretv & MENU_CUSTOM_COMMAND) {
    unsigned int custom = (unsigned int)(mretv & MENU_LOWER_MASK);

    switch (custom) {
    /* kb-custom-1: inquiry. This is the one deliberately slow verb on the
     * surface -- it blocks for about five seconds while the controller looks
     * -- and it is a key rather than something the menu does on open, so that
     * opening the pane to disconnect a headset costs nothing. */
    case 0:
      if (pd->node == NULL) {
        bt_set_status(pd, "Bluetooth is off.");
        return RELOAD_DIALOG;
      }
      {
        GHashTable *seen =
            g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
        unsigned int found = bt_inquiry(pd->node, seen, pd->classes);
        g_hash_table_destroy(seen);
        bt_refresh(pd);
        /* Two calls rather than a chosen format string: bt_set_status is
         * G_GNUC_PRINTF, and handing it a non-literal format defeats the check
         * that exists to catch exactly the mismatch this line could hide. */
        if (found > 0) {
          bt_set_status(pd, "Found %u device%s.", found,
                        found == 1 ? "" : "s");
        } else {
          bt_set_status(pd, "Nothing answered.");
        }
      }
      return RELOAD_DIALOG;

    /* kb-custom-2: disconnect without forgetting. Separate from Enter because
     * Enter on a connected device already disconnects it -- this one exists so
     * the verb has a key of its own that does the same thing from anywhere in
     * the list, which is what a person reaching for it expects. */
    case 1:
      if (row == NULL || row->kind != BT_ROW_DEVICE) {
        bt_set_status(pd, "Select a device to disconnect.");
        return RELOAD_DIALOG;
      }
      if (bt_disconnect(pd, row)) {
        bt_set_status(pd, "Disconnected %s.", row->label);
        bt_refresh(pd);
      }
      return RELOAD_DIALOG;

    /* kb-custom-3: forget. A menu that saves a credential has to offer a way to
     * unsave it, or the only route back is editing a root-owned file by hand --
     * which on this stack is exactly what it would be. */
    case 2:
      if (row == NULL || row->kind != BT_ROW_DEVICE) {
        bt_set_status(pd, "Select a device to forget.");
        return RELOAD_DIALOG;
      }
      if (!row->paired && !row->hid_configured) {
        bt_set_status(pd, "%s is not saved.", row->label);
        return RELOAD_DIALOG;
      }
      if (bt_forget(pd, row)) {
        bt_set_status(pd, "Forgot %s.", row->label);
        bt_refresh(pd);
      } else {
        bt_set_status(pd, "Could not forget %s.", row->label);
      }
      return RELOAD_DIALOG;

    /* kb-custom-4: class-appropriate setup. See bt_setup(). */
    case 3:
      if (row == NULL || row->kind != BT_ROW_DEVICE) {
        bt_set_status(pd, "Select a device to set up.");
        return RELOAD_DIALOG;
      }
      if (bt_setup(pd, row)) {
        bt_refresh(pd);
      }
      return RELOAD_DIALOG;

    default:
      return (ModeMode)custom;
    }
  }

  return MODE_EXIT;
}

static void bluetooth_mode_destroy(Mode *sw) {
  BluetoothModePrivateData *pd =
      (BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_ptr_array_free(pd->rows, TRUE);
    g_hash_table_destroy(pd->names);
    g_hash_table_destroy(pd->paired);
    g_hash_table_destroy(pd->classes);
    g_hash_table_destroy(pd->hids);
    g_free(pd->node);
    g_free(pd->device);
    g_free(pd->bdaddr);
    g_free(pd->local_name);
    g_free(pd->audio_binary);
    g_free(pd->status);
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int bluetooth_token_match(const Mode *sw, sofi_int_matcher **tokens,
                                 unsigned int index) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);
  BtRow *row = bt_row_at(pd, index);

  if (row == NULL) {
    return FALSE;
  }

  return helper_token_match(tokens, row->label);
}

Mode bluetooth_mode = {.name = "bluetooth",
                       .cfg_name_key = "display-bluetooth",
                       ._init = bluetooth_mode_init,
                       ._get_num_entries = bluetooth_mode_get_num_entries,
                       ._result = bluetooth_mode_result,
                       ._destroy = bluetooth_mode_destroy,
                       ._token_match = bluetooth_token_match,
                       ._get_display_value = _get_display_value,
                       ._get_icon = NULL,
                       ._get_completion = NULL,
                       ._preprocess_input = NULL,
                       ._get_message = bluetooth_mode_get_message,
                       .private_data = NULL,
                       .free = NULL,
                       .type = MODE_TYPE_SWITCHER};

#endif // BLUETOOTH_MODE
