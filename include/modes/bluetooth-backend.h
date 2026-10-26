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

#ifndef SOFI_MODE_BLUETOOTH_BACKEND_H
#define SOFI_MODE_BLUETOOTH_BACKEND_H

/**
 * @file
 * @brief The bluetooth mode's stack-selection boundary.
 *
 * Script function and purpose: the row model the bluetooth panel draws, and the
 * function-pointer table that fills it. One surface, two stacks underneath it --
 * FreeBSD's netgraph and Linux's `org.bluez` -- and nothing above this line
 * knows which answered.
 *
 * The vtable was removed by R12 on the reasoning that one stack needs no
 * indirection, and reinstated by R-102 when a second stack was ruled in. Both
 * steps were right at the time. The half of R12 that survives its own reversal
 * is the probe discipline: a backend claims a machine because something
 * *answered*, never because a tool is installed. Each `probe` below is held to
 * that.
 *
 * The two backends share this row model, the class-of-device decode and the
 * masked PIN prompt, and share nothing else. Everything stack-specific lives
 * behind one of these pointers.
 */

#include <glib.h>

/** Signal cells drawn on a connected device row, matching the volume and
 * network bars so one length means one thing across all three surfaces. */
#define BT_SIGNAL_CELLS 4

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
  /** A pairing question the stack is waiting on an answer to. Enter accepts.
   *
   * Only `org.bluez` produces these: it mediates pairing through an agent the
   * client registers, so the answer has to be rendered *during* the exchange
   * rather than driven to completion by a subprocess. netgraph hands hcsecd a
   * PIN up front and never asks anything back. */
  BT_ROW_REQUEST,
} BtRowKind;

/** Which verb a ::BT_ROW_ACTION row carries. */
typedef enum {
  BT_ACTION_DISCOVERABLE,
  BT_ACTION_RESET,
  BT_ACTION_RESTART_STACK,
  /** Discovery as a toggle rather than a bounded sweep. Only the stack that
   * reports arrivals as they happen can offer this; netgraph's inquiry ends
   * itself, so it stays a key and not a row. */
  BT_ACTION_SCAN,
} BtAction;

/**
 * What the class-of-device says the thing is.
 *
 * This decode is the whole of the "intelligent detection": it is what lets one
 * surface carry a headset and a gamepad without the two sharing a code path,
 * and it is what the setup verb dispatches on -- a Peripheral needs a bthidd
 * stanza, an Audio/Video device needs an audio path that FreeBSD base does not
 * have. Guessing from the device's advertised name would be a coin flip; the
 * class is what the device itself declares it is.
 *
 * Both stacks report the same number: netgraph prints it from an inquiry,
 * `org.bluez` publishes it as `Device1.Class`, so the decode is shared.
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
 * **dynamically loaded backend**, `voss_bt.so`, from the
 * `audio/virtual_oss_bluetooth` port (BSD-2-clause). The backend is loaded only
 * when an invocation names a bluetooth device, which is why the two are probed
 * separately: having the daemon says nothing about having the backend.
 *
 * netgraph-only. BlueZ hands audio to PipeWire or PulseAudio, which sofi's
 * volume mode already drives, so there is no second route to probe there.
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

  /** Backend-native handle, passed back verbatim: a BD_ADDR on a netgraph
   * device row, a `/org/bluez/...` object path on a BlueZ one, a netgraph node
   * name or adapter object path on the adapter row, NULL on an action row. The
   * mode never parses it. */
  char *id;
  /** What the row says. */
  char *label;
  /** Secondary text: the class, the address, a state. May be NULL. */
  char *detail;

  BtKind devkind;
  /** Raw class-of-device, valid only when @c have_cod. */
  guint32 cod;
  gboolean have_cod;

  /** A link key can be negotiated for it: an hcsecd.conf stanza on netgraph,
   * `Device1.Paired` on BlueZ. */
  gboolean paired;
  /** Present in read_connection_list, or `Device1.Connected`. */
  gboolean connected;
  /** bthidd knows it, so it can actually deliver input. netgraph only. */
  gboolean hid_configured;
  /** Seen by an inquiry or sitting in the neighbour cache. */
  gboolean in_range;
  /** `Device1.Trusted`: the stack will let it reconnect unattended. BlueZ only
   * -- netgraph has no such flag, and the mode says so rather than offering a
   * verb that does nothing. */
  gboolean trusted;

  /** Connection handle, or -1. netgraph's disconnect needs this, not the
   * address. */
  int handle;
  /** dBm, valid only when @c have_rssi. */
  int rssi;
  gboolean have_rssi;

  /** Connected, up, or on. Drives the ACTIVE row state. */
  gboolean active;
} BtRow;

typedef struct BtBackend BtBackend;

typedef struct {
  /** The stack that answered. Never NULL once the mode has initialised. */
  const BtBackend *be;
  /** The backend's own state, opaque here and freed by its @c teardown. */
  gpointer be_data;

  /** The adapter handle, in whatever the backend calls one: a netgraph HCI node
   * ("ubt0hci") or a BlueZ adapter object path ("/org/bluez/hci0"). NULL when
   * there is no adapter. */
  char *node;
  /** The USB device the netgraph node belongs to, e.g. "ubt0". Derived from
   * @c node; `service bluetooth` is addressed by this and not by the node
   * name. netgraph only. */
  char *device;

  char *bdaddr;
  char *local_name;
  /** Scan_Enable: bit 0x1 inquiry scan (discoverable), 0x2 page scan
   * (connectable). -1 when it could not be read. BlueZ has no such register and
   * reports the same two bits from `Adapter1.Discoverable`, so one adapter row
   * serves both stacks. */
  int scan_enable;

  /** TRUE once the paired-device list has been read by any route. When FALSE
   * the paired column is unknown rather than empty, and the message bar says
   * so. */
  gboolean have_paired;
  /** TRUE when reading it needed the privilege command. Reported, because it
   * means every open costs one escalation. netgraph only: BlueZ publishes
   * `Paired` to any caller on the system bus. */
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

/**
 * The stack-specific half of the bluetooth mode.
 *
 * Every entry may assume the mode's private data is initialised and that its
 * own @c probe returned TRUE. @c request and @c teardown may be NULL, and the
 * mode tests both before every call. @c trust may be NULL because nothing in
 * the mode calls it at all. Every other entry here is dispatched
 * unconditionally, so a backend has to supply it.
 */
struct BtBackend {
  /** What this stack is called, as the message bar and the warnings name it. */
  const char *name;

  /** Did this stack answer on this machine? Not "is it installed". */
  gboolean (*probe)(BluetoothModePrivateData *pd);

  /** Find the adapter, read what it says about itself, and append its row.
   *
   * @returns FALSE when there is no adapter to enumerate behind, in which case
   *          the row it appended is the whole surface. */
  gboolean (*adapter)(BluetoothModePrivateData *pd);
  /** Append one ::BT_ROW_DEVICE per known device. */
  void (*devices)(BluetoothModePrivateData *pd);
  /** Append this stack's maintenance verbs. */
  void (*actions)(BluetoothModePrivateData *pd);

  gboolean (*power)(BluetoothModePrivateData *pd, gboolean on);
  gboolean (*connect)(BluetoothModePrivateData *pd, BtRow *row);
  gboolean (*disconnect)(BluetoothModePrivateData *pd, BtRow *row);
  gboolean (*pair)(BluetoothModePrivateData *pd, BtRow *row);
  /** Toggle this stack's trust flag; NULL where there is no such flag.
   *
   * Action purpose: nothing above this line reads this slot. org.bluez reaches
   * its own trust verb from inside @c setup, because trusting is what "make
   * this device usable" amounts to there, and netgraph has no flag to toggle.
   * So filling this in buys a new backend nothing by itself: wire the verb up
   * through @c setup or @c action, which are dispatched. */
  gboolean (*trust)(BluetoothModePrivateData *pd, BtRow *row, gboolean on);
  /** Undo everything that makes a device known, wherever this stack records
   * it: the hcsecd stanza, the stored link key and the bthidd entry, or
   * `Adapter1.RemoveDevice`. */
  gboolean (*remove)(BluetoothModePrivateData *pd, BtRow *row);

  /** Begin looking for devices. Sets @c pd->status itself, because what there
   * is to say differs: a bounded sweep reports what it found, a live discovery
   * reports that it started.
   *
   * @returns TRUE when the row list should be rebuilt. */
  gboolean (*scan_start)(BluetoothModePrivateData *pd);
  gboolean (*scan_stop)(BluetoothModePrivateData *pd);
  /** Make this machine discoverable, or stop. */
  gboolean (*scan_enable)(BluetoothModePrivateData *pd, gboolean discoverable);

  /** The class-appropriate setup verb. */
  gboolean (*setup)(BluetoothModePrivateData *pd, BtRow *row);
  /** Whatever this stack's own ::BT_ROW_ACTION rows mean. */
  gboolean (*action)(BluetoothModePrivateData *pd, BtRow *row,
                     gboolean *should_exit);
  /** Answer a ::BT_ROW_REQUEST row. NULL on a stack that never asks. */
  gboolean (*request)(BluetoothModePrivateData *pd, BtRow *row,
                      gboolean accept);

  /** The message bar's standing text, once there is nothing to report. */
  char *(*message)(const BluetoothModePrivateData *pd);
  /** Drop whatever @c probe and the enumeration acquired. */
  void (*teardown)(BluetoothModePrivateData *pd);
};

/* Shared by both backends, defined in bluetooth.c. */

/** Rebuild the row list through whichever backend is in place. A backend that
 * learns of a change out of band -- an arriving device, a completed pairing --
 * calls this and then sofi_view_reload(). */
void bt_refresh(BluetoothModePrivateData *pd);

BtRow *bt_row_new(GPtrArray *rows, BtRowKind kind);
void bt_set_status(BluetoothModePrivateData *pd, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);
BtKind bt_kind_from_cod(guint32 cod);
const char *bt_kind_name(BtKind kind);
gboolean bt_kind_is_audio(BtKind kind);

/** The netgraph stack, in bluetooth.c. */
const BtBackend *bt_netgraph_backend(void);
/** The `org.bluez` stack, in bluetooth-bluez.c. */
const BtBackend *bt_bluez_backend(void);

#endif // SOFI_MODE_BLUETOOTH_BACKEND_H
