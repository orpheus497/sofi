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
 * @brief The `org.bluez` backend, over GDBus.
 *
 * Script function and purpose: the Linux half of the bluetooth mode. Adapter
 * discovery, device enumeration, the verbs, and the pairing agent BlueZ requires
 * a client to register before it will pair anything. It fills the same row model
 * the netgraph backend fills and shares the class-of-device decode with it;
 * nothing else is shared, because nothing else is the same.
 *
 * Mandated by R-102, which reverses R12. The netgraph path is retained in full
 * and none of it was ported here -- the two stacks agree on the shape of a
 * device row and on almost nothing else.
 *
 * No new dependency: `org.bluez` is a D-Bus interface and sofi already speaks
 * GDBus in `tray-service.c`, `notify-service.c` and `dbusmenu.c`.
 *
 * ---------------------------------------------------------------------------
 * THE SYSTEM BUS, AND WHY THE PROBE IS A CALL AND NOT A $PATH LOOKUP
 *
 * `bluetoothd` sits on the **system** bus, not the session bus every other
 * GDBus user in this tree touches.
 *
 * The probe is `GetManagedObjects` on `org.bluez` with auto-start refused, and
 * an adapter exists because an `org.bluez.Adapter1` object came back. That is
 * R12's discipline, which outlives R12's reversal: `bluetoothctl` being on
 * $PATH proves nothing, and a machine with BlueZ installed and
 * `bluetooth.service` stopped is exactly the case the two tests disagree on --
 * the same argument the netgraph path makes for `read_node_list`. Auto-start is
 * refused for the same reason: starting the daemon would make the test true by
 * acting on the machine instead of asking it.
 *
 * ---------------------------------------------------------------------------
 * THIS BACKEND IS NOTIFIED; THE OTHER ONE POLLS
 *
 * `InterfacesAdded`, `InterfacesRemoved` and `PropertiesChanged` say when
 * something changed, so a device that walks into range appears on the open pane
 * without a key being pressed. The netgraph path has no equivalent and has to
 * be asked -- its scan is a bounded sweep whose results are already stale when
 * they print. **This is the one place where the newer backend is strictly
 * better rather than merely different**, and it is worth knowing which of the
 * two surfaces you are looking at.
 *
 * The signals are used as "something changed" and not as an incremental cache
 * update: each one coalesces into one `GetManagedObjects` and one rebuild. A
 * single call to a local daemon is cheaper than the several subprocesses the
 * other backend spends per refresh, and it cannot drift out of step with the
 * daemon the way a hand-maintained cache can.
 *
 * ---------------------------------------------------------------------------
 * THE PRIVILEGE BOUNDARY IS POLICY, NOT FILE MODES
 *
 * **None of netgraph's privilege model applies here and none of it was carried
 * across.** There is no root-owned PIN file, no `0600` key store, and nothing
 * for `network-privilege-command` to escalate -- which is why this backend never
 * reaches for it.
 *
 * What refuses instead is policy: the system bus's own rules for `org.bluez`
 * plus whatever polkit the distribution wires behind them. A refusal arrives as
 * a D-Bus error, `org.freedesktop.DBus.Error.AccessDenied`, and it is an answer
 * rather than a fault. Re-running the same call through a privilege command
 * would not change it, because the decision was made about the caller and not
 * about a file.
 *
 * So the discipline the mode already has is re-established against those
 * answers rather than against `errno`: `bz_explain` turns each error name into
 * the one sentence that is true of it, and a refusal is reported rather than
 * retried. The mode offers the verbs the stack advertises and says plainly when
 * the stack said no.
 *
 * ---------------------------------------------------------------------------
 * THE AGENT, AND WHAT IT DELIBERATELY WILL NOT DO
 *
 * BlueZ will not pair without an `org.bluez.Agent1` registered through
 * `AgentManager1.RegisterAgent`, so pairing cannot be driven to completion by a
 * subprocess and read back -- the confirmation has to be rendered *during* the
 * exchange. `Device1.Pair` is therefore called asynchronously and the question
 * arrives on the main loop as a row.
 *
 * **Nothing here accepts a pairing on its own.** There is no auto-accept, no
 * timeout that answers, and no configuration that would enable one: every
 * question is answered by Enter or refused by Escape, and a panel that closes
 * refuses what it was holding. An agent that silently accepted would pair
 * whatever asked.
 *
 * The capability registered is `DisplayYesNo`, which is exactly what this agent
 * can do, and `RequestPasskey` and `RequestPinCode` are refused with a sentence
 * that says so. Declaring `KeyboardDisplay` and then refusing those two would
 * tell BlueZ to choose a pairing method this surface cannot finish.
 *
 * `RequestDefaultAgent` is **not** called. BlueZ routes the questions raised by
 * a client's own `Pair` call to that client's agent, which is all this surface
 * needs; becoming the desktop's default agent would answer for pairings nobody
 * started here, and that is not a claim a menu should make.
 */

/** The log domain of this dialog, shared with the mode it serves. */
#define G_LOG_DOMAIN "Modes.Bluetooth"

#include "config.h"

#ifdef BLUETOOTH_MODE

#include <gio/gio.h>
#include <glib.h>
#include <string.h>

#include "modes/bluetooth-backend.h"
#include "view.h"

#define BZ_SERVICE "org.bluez"
#define BZ_ADAPTER_IFACE "org.bluez.Adapter1"
#define BZ_DEVICE_IFACE "org.bluez.Device1"
#define BZ_AGENT_MANAGER_IFACE "org.bluez.AgentManager1"
#define BZ_AGENT_MANAGER_PATH "/org/bluez"
#define BZ_PROPERTIES_IFACE "org.freedesktop.DBus.Properties"
#define BZ_OBJECT_MANAGER_IFACE "org.freedesktop.DBus.ObjectManager"

/** Where this process exports its agent. Registered on sofi's own connection,
 * so it needs no well-known name on the system bus. */
#define BZ_AGENT_PATH "/org/sofi/bluez/agent"

/** What this agent can actually do: show a passkey and take a yes or a no. */
#define BZ_AGENT_CAPABILITY "DisplayYesNo"

/** The error name BlueZ expects a refusal under. */
#define BZ_ERROR_REJECTED "org.bluez.Error.Rejected"

/** Reads and property writes. Short, because a local daemon that has not
 * answered in this long is not going to. */
#define BZ_CALL_TIMEOUT_MS 5000

/** `Connect` and `Disconnect` do radio work and routinely take seconds. This is
 * synchronous on the main thread, which is the limit the volume and network
 * modes share -- but bounded, which `g_spawn_sync` in the other backend is
 * not. */
#define BZ_LINK_TIMEOUT_MS 20000

/** `Pair` waits on a person pressing a button on a device, so its bound is a
 * person's patience and not a machine's. Asynchronous, so nothing is held. */
#define BZ_PAIR_TIMEOUT_MS 120000

/**
 * Action purpose: collapse a burst of signals into one rebuild.
 *
 * An arriving device emits `InterfacesAdded` and then several
 * `PropertiesChanged` as its name, class and RSSI resolve. Rebuilding on each
 * would re-list every device four times for one arrival. 120ms is under the
 * frame the view reloads on anyway, so nothing is perceptibly later for it.
 */
#define BZ_COALESCE_MS 120

/** What the stack is waiting on an answer to. */
typedef enum {
  BZ_REQ_NONE,
  /** `RequestConfirmation`: both ends show a number and it has to match. */
  BZ_REQ_CONFIRM,
  /** `RequestAuthorization`: no number, just "is this you". */
  BZ_REQ_AUTHORIZE,
  /** `AuthorizeService`: an already-paired device wants a profile. */
  BZ_REQ_SERVICE,
  /** `DisplayPasskey` or `DisplayPinCode`: an instruction, not a question.
   * There is nothing to answer -- BlueZ wanted it on screen -- so Enter only
   * dismisses it. */
  BZ_REQ_NOTICE,
} BzRequest;

/**
 * Action purpose: file-static rather than heap, and for a specific reason.
 *
 * `Device1.Pair` is asynchronous and its reply can land after the panel has
 * closed and the mode's private data has been freed, so the callback cannot be
 * handed a pointer it would then dereference. One statically allocated state
 * with a @c live flag the teardown clears is the same shape `tray-service.c`
 * uses, and it is sound here for the same reason: `bluetooth_mode` is one
 * global #Mode, so there is exactly one of these per process.
 */
static struct {
  gboolean live;
  BluetoothModePrivateData *pd;

  GDBusConnection *bus;
  GDBusNodeInfo *agent_info;
  guint agent_registration;
  gboolean agent_registered;

  guint sub_added;
  guint sub_removed;
  guint sub_props;
  guint coalesce;

  /** The last `GetManagedObjects` reply, `a{oa{sa{sv}}}`. Owned. */
  GVariant *objects;
  /** The adapter this surface is about, e.g. "/org/bluez/hci0". Owned. */
  char *adapter;

  /** `Adapter1.Discovering` and `Adapter1.Powered`, read with the rest of the
   * adapter's properties so the scan row and the message bar do not have to
   * ask a second time. */
  gboolean discovering;
  gboolean powered;

  /** A pairing is in flight, so a second one is refused rather than started
   * alongside it. */
  gboolean pairing;

  BzRequest req;
  /** Held until answered; NULL for ::BZ_REQ_NOTICE. Consumed by the reply. */
  GDBusMethodInvocation *req_invocation;
  /** The device the question is about. Owned. */
  char *req_device;
  char *req_label;
  char *req_detail;
} bluez;

/* ------------------------------------------------------------------ errors */

/**
 * Function purpose: turn a D-Bus error into the one sentence that is true of
 * it.
 *
 * Action purpose: this is where the privilege boundary is re-derived. The
 * netgraph backend can say "set `network-privilege-command`" because its
 * refusals are file modes and a privilege command really does fix them. Here a
 * refusal is a policy decision about the caller, so saying the same thing would
 * send the reader to configure something that cannot help. Each name below gets
 * the answer it actually means, and `AccessDenied` says plainly that there is
 * nothing to escalate.
 */
static const char *bz_explain(const GError *error) {
  if (error == NULL) {
    return "it failed and said nothing";
  }

  if (g_dbus_error_is_remote_error((GError *)error)) {
    char *name = g_dbus_error_get_remote_error((GError *)error);
    const char *answer = NULL;

    if (g_strcmp0(name, "org.freedesktop.DBus.Error.AccessDenied") == 0) {
      answer = "the system bus refused it. That is a policy answer about this "
               "session, not a file permission, so there is nothing for a "
               "privilege command to escalate";
    } else if (g_strcmp0(name, "org.freedesktop.DBus.Error.ServiceUnknown") ==
                   0 ||
               g_strcmp0(name, "org.freedesktop.DBus.Error.NameHasNoOwner") ==
                   0) {
      answer = "bluetoothd is no longer on the bus";
    } else if (g_strcmp0(name, "org.bluez.Error.NotReady") == 0) {
      answer = "the adapter is not powered";
    } else if (g_strcmp0(name, "org.bluez.Error.NotPermitted") == 0) {
      answer = "BlueZ does not permit it in this state";
    } else if (g_strcmp0(name, "org.bluez.Error.NotSupported") == 0) {
      answer = "the device or the adapter does not support it";
    } else if (g_strcmp0(name, "org.bluez.Error.InProgress") == 0) {
      answer = "it is already under way";
    } else if (g_strcmp0(name, "org.bluez.Error.AlreadyExists") == 0) {
      answer = "it is already done";
    } else if (g_strcmp0(name, "org.bluez.Error.DoesNotExist") == 0) {
      answer = "BlueZ no longer knows that device";
    } else if (g_strcmp0(name, "org.bluez.Error.AuthenticationCanceled") == 0) {
      answer = "the pairing was cancelled";
    } else if (g_strcmp0(name, "org.bluez.Error.AuthenticationRejected") == 0) {
      answer = "the device refused the pairing";
    } else if (g_strcmp0(name, "org.bluez.Error.AuthenticationTimeout") == 0) {
      answer = "the device stopped answering";
    } else if (g_strcmp0(name, "org.bluez.Error.AuthenticationFailed") == 0) {
      answer = "the pairing was not accepted -- usually a wrong code";
    } else if (g_strcmp0(name, "org.bluez.Error.ConnectionAttemptFailed") ==
               0) {
      answer = "the device did not answer the connection";
    }
    g_free(name);

    if (answer != NULL) {
      return answer;
    }
  }

  if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)) {
    return "it did not answer in time";
  }

  return error->message;
}

/** TRUE when the error is the one refusal nothing can be done about, which the
 * message bar says once rather than per verb. */
static gboolean bz_is_denied(const GError *error) {
  if (error == NULL || !g_dbus_error_is_remote_error((GError *)error)) {
    return FALSE;
  }

  char *name = g_dbus_error_get_remote_error((GError *)error);
  gboolean denied =
      g_strcmp0(name, "org.freedesktop.DBus.Error.AccessDenied") == 0;

  g_free(name);

  return denied;
}

/* -------------------------------------------------------------- bus access */

/** One `GetManagedObjects`, or NULL with @p error_out set. */
static GVariant *bz_managed_objects(GError **error_out) {
  if (bluez.bus == NULL) {
    g_set_error_literal(error_out, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED,
                        "there is no connection to the system bus");
    return NULL;
  }

  /* Action purpose: NO_AUTO_START is the probe. Letting the bus activate
   * org.bluez would answer "is bluetooth running" by starting it, which is an
   * action and not a test. */
  GVariant *reply = g_dbus_connection_call_sync(
      bluez.bus, BZ_SERVICE, "/", BZ_OBJECT_MANAGER_IFACE, "GetManagedObjects",
      NULL, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
      G_DBUS_CALL_FLAGS_NO_AUTO_START, BZ_CALL_TIMEOUT_MS, NULL, error_out);

  if (reply == NULL) {
    return NULL;
  }

  GVariant *objects = g_variant_get_child_value(reply, 0);
  g_variant_unref(reply);

  return objects;
}

/** The interface dictionary of one object, or NULL. Does not take a reference
 * on anything the caller must free beyond the returned dictionary. */
static GVariant *bz_interface(GVariant *interfaces, const char *iface) {
  if (interfaces == NULL) {
    return NULL;
  }
  return g_variant_lookup_value(interfaces, iface, G_VARIANT_TYPE_VARDICT);
}

/** Set one boolean property, reporting what refused it. */
static gboolean bz_set_bool(BluetoothModePrivateData *pd, const char *path,
                            const char *iface, const char *property,
                            gboolean value) {
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bluez.bus, BZ_SERVICE, path, BZ_PROPERTIES_IFACE, "Set",
      g_variant_new("(ssv)", iface, property, g_variant_new_boolean(value)),
      NULL, G_DBUS_CALL_FLAGS_NONE, BZ_CALL_TIMEOUT_MS, NULL, &error);

  if (reply != NULL) {
    g_variant_unref(reply);
    return TRUE;
  }

  bt_set_status(pd, "%s could not be changed: %s", property,
                bz_explain(error));
  g_error_free(error);

  return FALSE;
}

/** One method call with no arguments and no return value. */
static gboolean bz_call(BluetoothModePrivateData *pd, const char *path,
                        const char *iface, const char *method, int timeout_ms,
                        const char *subject) {
  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bluez.bus, BZ_SERVICE, path, iface, method, NULL, NULL,
      G_DBUS_CALL_FLAGS_NONE, timeout_ms, NULL, &error);

  if (reply != NULL) {
    g_variant_unref(reply);
    return TRUE;
  }

  bt_set_status(pd, "%s: %s", subject, bz_explain(error));
  g_error_free(error);

  return FALSE;
}

/* ------------------------------------------------------------ live updates */

/** Function purpose: rebuild once for a burst of signals, then redraw.
 *
 * Action purpose: the rebuild happens here rather than in sofi_view_reload(),
 * which only recomputes the row count and the message bar -- it does not ask
 * the mode to re-read anything. Doing the rebuild first and the redraw second
 * is the order the icon fetcher uses for the same reason. */
static gboolean bz_rebuild_now(G_GNUC_UNUSED gpointer data) {
  bluez.coalesce = 0;

  if (!bluez.live || bluez.pd == NULL) {
    return G_SOURCE_REMOVE;
  }

  bt_refresh(bluez.pd);
  sofi_view_reload();

  return G_SOURCE_REMOVE;
}

static void bz_changed(void) {
  if (!bluez.live || bluez.coalesce > 0) {
    return;
  }
  bluez.coalesce = g_timeout_add(BZ_COALESCE_MS, bz_rebuild_now, NULL);
}

static void bz_on_signal(G_GNUC_UNUSED GDBusConnection *connection,
                         G_GNUC_UNUSED const gchar *sender,
                         G_GNUC_UNUSED const gchar *object_path,
                         G_GNUC_UNUSED const gchar *interface_name,
                         G_GNUC_UNUSED const gchar *signal_name,
                         G_GNUC_UNUSED GVariant *parameters,
                         G_GNUC_UNUSED gpointer user_data) {
  bz_changed();
}

/* ------------------------------------------------------------------ agent */

static const gchar bz_agent_xml[] =
    "<node>"
    "  <interface name='org.bluez.Agent1'>"
    "    <method name='Release'/>"
    "    <method name='RequestPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPinCode'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='pincode' direction='in'/>"
    "    </method>"
    "    <method name='RequestPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='out'/>"
    "    </method>"
    "    <method name='DisplayPasskey'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "      <arg type='q' name='entered' direction='in'/>"
    "    </method>"
    "    <method name='RequestConfirmation'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='u' name='passkey' direction='in'/>"
    "    </method>"
    "    <method name='RequestAuthorization'>"
    "      <arg type='o' name='device' direction='in'/>"
    "    </method>"
    "    <method name='AuthorizeService'>"
    "      <arg type='o' name='device' direction='in'/>"
    "      <arg type='s' name='uuid' direction='in'/>"
    "    </method>"
    "    <method name='Cancel'/>"
    "  </interface>"
    "</node>";

static void bz_request_clear(void) {
  bluez.req = BZ_REQ_NONE;
  bluez.req_invocation = NULL;
  g_clear_pointer(&bluez.req_device, g_free);
  g_clear_pointer(&bluez.req_label, g_free);
  g_clear_pointer(&bluez.req_detail, g_free);
}

/**
 * Function purpose: the address a BlueZ device object path encodes.
 *
 * Action purpose: the last resort for naming a device, and it needs no bus
 * traffic at all. BlueZ names a device object after its address with the colons
 * written as underscores, so the address is recoverable from the path. That
 * matters for a device pairing for the first time: it is being asked about
 * before it has ever appeared in an object listing, so a cached name cannot
 * exist for it and the raw path is not something to ask a person to recognise.
 */
static char *bz_address_from_path(const char *path) {
  const char *leaf = strrchr(path, '/');

  if (leaf == NULL || !g_str_has_prefix(leaf + 1, "dev_")) {
    return g_strdup(path);
  }

  char *address = g_strdup(leaf + 1 + strlen("dev_"));
  for (char *p = address; *p != '\0'; p++) {
    if (*p == '_') {
      *p = ':';
    }
  }

  return address;
}

/**
 * Function purpose: the name to put on a question about a device.
 *
 * Action purpose: **answered from the last object listing and never by asking
 * the daemon.** The question arrives while BlueZ is blocked on this reply, so a
 * synchronous call back to it is a round trip to a peer that is waiting on
 * us -- which is a deadlock whenever the daemon dispatches its agent calls
 * without a thread to spare, and a stalled menu even when it does not. The
 * listing fetched on the last refresh already holds every name this can need,
 * and the path yields the address for the one case it does not.
 */
static char *bz_device_label(const char *path) {
  GVariantIter iter;
  const gchar *candidate = NULL;
  GVariant *interfaces = NULL;
  char *label = NULL;

  if (bluez.objects != NULL) {
    g_variant_iter_init(&iter, bluez.objects);
    while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &candidate,
                               &interfaces)) {
      if (label != NULL || g_strcmp0(candidate, path) != 0) {
        continue;
      }

      GVariant *props = bz_interface(interfaces, BZ_DEVICE_IFACE);
      if (props == NULL) {
        continue;
      }

      const char *alias = NULL;
      const char *address = NULL;
      if (g_variant_lookup(props, "Alias", "&s", &alias) && alias != NULL &&
          *alias != '\0') {
        label = g_strdup(alias);
      } else if (g_variant_lookup(props, "Name", "&s", &alias) &&
                 alias != NULL && *alias != '\0') {
        label = g_strdup(alias);
      } else if (g_variant_lookup(props, "Address", "&s", &address) &&
                 address != NULL) {
        label = g_strdup(address);
      }
      g_variant_unref(props);
    }
  }

  return label != NULL ? label : bz_address_from_path(path);
}

/**
 * Function purpose: take one agent question and put it on the surface.
 *
 * Action purpose: the invocation is held rather than answered, which is the
 * whole point -- BlueZ is blocked on this reply, and holding it is what makes
 * the confirmation happen *during* the exchange instead of after it. It is
 * answered by a keypress, by the teardown, or not at all, and the last of those
 * is why BlueZ has its own timeout.
 */
static void bz_request_hold(BzRequest kind, GDBusMethodInvocation *invocation,
                            const char *device, char *label, char *detail) {
  /* Action purpose: a held question outranks an instruction, always. A notice
   * carries no invocation and can be replaced or dropped for free, while
   * replacing a question would abandon a reply BlueZ is blocked on -- so an
   * instruction arriving behind one is dropped rather than shown. It has
   * already been acknowledged by the time this is reached, so nothing is left
   * waiting for it either. */
  if (kind == BZ_REQ_NOTICE && bluez.req_invocation != NULL) {
    g_free(label);
    g_free(detail);
    return;
  }

  bluez.req = kind;
  bluez.req_invocation = invocation;
  g_free(bluez.req_device);
  bluez.req_device = g_strdup(device);
  g_free(bluez.req_label);
  bluez.req_label = label;
  g_free(bluez.req_detail);
  bluez.req_detail = detail;

  if (bluez.live && bluez.pd != NULL) {
    bt_refresh(bluez.pd);
    sofi_view_reload();
  }
}

static void bz_agent_method(G_GNUC_UNUSED GDBusConnection *connection,
                            G_GNUC_UNUSED const gchar *sender,
                            G_GNUC_UNUSED const gchar *object_path,
                            G_GNUC_UNUSED const gchar *interface_name,
                            const gchar *method, GVariant *parameters,
                            GDBusMethodInvocation *invocation,
                            G_GNUC_UNUSED gpointer user_data) {
  if (g_strcmp0(method, "Release") == 0) {
    /* BlueZ is done with the agent; it is not an error and nothing is left to
     * unregister. */
    bluez.agent_registered = FALSE;
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }

  if (g_strcmp0(method, "Cancel") == 0) {
    /* The device or the daemon withdrew the question. Whatever is on screen is
     * now about nothing, so it goes rather than waiting to be answered into a
     * void. */
    if (bluez.req_invocation != NULL) {
      g_dbus_method_invocation_return_dbus_error(
          bluez.req_invocation, BZ_ERROR_REJECTED, "The request was cancelled.");
    }
    bz_request_clear();
    g_dbus_method_invocation_return_value(invocation, NULL);
    if (bluez.live && bluez.pd != NULL) {
      bt_set_status(bluez.pd, "The pairing request was withdrawn.");
      bt_refresh(bluez.pd);
      sofi_view_reload();
    }
    return;
  }

  /* Action purpose: one *question* at a time, and the test is the held
   * invocation rather than the pending kind.
   *
   * It was the kind until a `DisplayPasskey` notice -- which is an instruction
   * nobody has to answer -- made the surface refuse the real question that
   * followed it. A second question is still refused rather than queued or
   * allowed to replace the first: replacing it would silently drop a question
   * the user is looking at, and queueing would need a surface that can show
   * two, which this one cannot. */
  if (bluez.req_invocation != NULL &&
      (g_strcmp0(method, "RequestConfirmation") == 0 ||
       g_strcmp0(method, "RequestAuthorization") == 0 ||
       g_strcmp0(method, "AuthorizeService") == 0)) {
    g_dbus_method_invocation_return_dbus_error(
        invocation, BZ_ERROR_REJECTED,
        "sofi is already showing another pairing request.");
    return;
  }

  if (g_strcmp0(method, "RequestConfirmation") == 0) {
    const gchar *device = NULL;
    guint32 passkey = 0;
    g_variant_get(parameters, "(&ou)", &device, &passkey);

    char *label = bz_device_label(device);
    char *name = g_strdup_printf("Pair with %s?", label);
    /* BlueZ prints the passkey six digits wide and so does every phone, so a
     * short code reads as a mismatch unless it is padded the same way. */
    char *detail = g_strdup_printf(
        "Both ends should show %06u  ·  Enter accepts, Escape refuses",
        passkey);
    g_free(label);
    bz_request_hold(BZ_REQ_CONFIRM, invocation, device, name, detail);
    return;
  }

  if (g_strcmp0(method, "RequestAuthorization") == 0) {
    const gchar *device = NULL;
    g_variant_get(parameters, "(&o)", &device);

    char *label = bz_device_label(device);
    char *name = g_strdup_printf("Pair with %s?", label);
    char *detail = g_strdup("No code to compare  ·  Enter accepts, Escape "
                            "refuses");
    g_free(label);
    bz_request_hold(BZ_REQ_AUTHORIZE, invocation, device, name, detail);
    return;
  }

  if (g_strcmp0(method, "AuthorizeService") == 0) {
    const gchar *device = NULL;
    const gchar *uuid = NULL;
    g_variant_get(parameters, "(&o&s)", &device, &uuid);

    char *label = bz_device_label(device);
    char *name = g_strdup_printf("Let %s use a service?", label);
    char *detail = g_strdup_printf("%s  ·  Enter allows, Escape refuses",
                                   uuid != NULL ? uuid : "unnamed profile");
    g_free(label);
    bz_request_hold(BZ_REQ_SERVICE, invocation, device, name, detail);
    return;
  }

  if (g_strcmp0(method, "DisplayPasskey") == 0) {
    const gchar *device = NULL;
    guint32 passkey = 0;
    guint16 entered = 0;
    g_variant_get(parameters, "(&ouq)", &device, &passkey, &entered);

    /* Action purpose: answered first, because this one is not a question. BlueZ
     * wanted a number on screen and carries on as soon as it is told that it
     * is; holding the reply would stall the exchange this is meant to help. It
     * is also called again for every digit the user types on the device, which
     * is why it replaces the notice rather than refusing a second one. */
    g_dbus_method_invocation_return_value(invocation, NULL);

    char *label = bz_device_label(device);
    char *name = g_strdup_printf("Type %06u on %s", passkey, label);
    char *detail = entered > 0
                       ? g_strdup_printf("%u digit%s so far", entered,
                                         entered == 1 ? "" : "s")
                       : g_strdup("Enter dismisses this");
    g_free(label);
    bz_request_hold(BZ_REQ_NOTICE, NULL, device, name, detail);
    return;
  }

  if (g_strcmp0(method, "DisplayPinCode") == 0) {
    const gchar *device = NULL;
    const gchar *pincode = NULL;
    g_variant_get(parameters, "(&o&s)", &device, &pincode);

    g_dbus_method_invocation_return_value(invocation, NULL);

    char *label = bz_device_label(device);
    char *name = g_strdup_printf("Enter %s on %s",
                                 pincode != NULL ? pincode : "the PIN", label);
    g_free(label);
    bz_request_hold(BZ_REQ_NOTICE, NULL, device,
                    name, g_strdup("Enter dismisses this"));
    return;
  }

  /**
   * Action purpose: refused, and said out loud.
   *
   * These two want a code typed into sofi and handed back, which this surface
   * cannot do without putting a pairing code in the unmasked filter line. The
   * registered capability is `DisplayYesNo` precisely so BlueZ picks a method
   * that ends in a question this agent can answer, and these are reached only
   * by a device that has no other option. Refusing with a reason is the honest
   * answer; returning a guess would fail authentication and look like a wrong
   * code.
   */
  if (g_strcmp0(method, "RequestPasskey") == 0 ||
      g_strcmp0(method, "RequestPinCode") == 0) {
    g_dbus_method_invocation_return_dbus_error(
        invocation, BZ_ERROR_REJECTED,
        "sofi's agent answers yes-or-no pairings and cannot type a code.");
    if (bluez.live && bluez.pd != NULL) {
      bt_set_status(bluez.pd,
                    "This device wants a code typed in, which this pane cannot "
                    "do yet. Pair it with bluetoothctl once and it will "
                    "connect from here afterwards.");
      sofi_view_reload();
    }
    return;
  }

  g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
                                        G_DBUS_ERROR_UNKNOWN_METHOD,
                                        "Unknown method %s", method);
}

static const GDBusInterfaceVTable bz_agent_vtable = {
    .method_call = bz_agent_method,
    .get_property = NULL,
    .set_property = NULL,
    .padding = {NULL},
};

/**
 * Function purpose: export the agent and tell BlueZ about it.
 *
 * Action purpose: a failure here is not fatal to the mode. Everything except
 * pairing works without an agent, and a pane that refuses to open because it
 * could not register one would hide the disconnect verb from someone who only
 * wanted that. So it warns, records the fact, and the pair verb is the only
 * thing that then reports it.
 */
static gboolean bz_agent_register(void) {
  GError *error = NULL;

  if (bluez.agent_registered) {
    return TRUE;
  }

  if (bluez.agent_info == NULL) {
    bluez.agent_info = g_dbus_node_info_new_for_xml(bz_agent_xml, &error);
    if (bluez.agent_info == NULL) {
      g_warning("Could not parse the bluetooth agent interface: %s",
                error != NULL ? error->message : "unknown error");
      g_clear_error(&error);
      return FALSE;
    }
  }

  if (bluez.agent_registration == 0) {
    bluez.agent_registration = g_dbus_connection_register_object(
        bluez.bus, BZ_AGENT_PATH, bluez.agent_info->interfaces[0],
        &bz_agent_vtable, NULL, NULL, &error);
    if (bluez.agent_registration == 0) {
      g_warning("Could not export the bluetooth pairing agent: %s",
                error != NULL ? error->message : "unknown error");
      g_clear_error(&error);
      return FALSE;
    }
  }

  GVariant *reply = g_dbus_connection_call_sync(
      bluez.bus, BZ_SERVICE, BZ_AGENT_MANAGER_PATH, BZ_AGENT_MANAGER_IFACE,
      "RegisterAgent",
      g_variant_new("(os)", BZ_AGENT_PATH, BZ_AGENT_CAPABILITY), NULL,
      G_DBUS_CALL_FLAGS_NONE, BZ_CALL_TIMEOUT_MS, NULL, &error);

  if (reply == NULL) {
    g_warning("BlueZ would not take sofi's pairing agent: %s. Pairing from "
              "this pane will not work; everything else will.",
              bz_explain(error));
    g_clear_error(&error);
    return FALSE;
  }
  g_variant_unref(reply);

  bluez.agent_registered = TRUE;

  return TRUE;
}

static void bz_agent_unregister(void) {
  if (bluez.agent_registered) {
    GVariant *reply = g_dbus_connection_call_sync(
        bluez.bus, BZ_SERVICE, BZ_AGENT_MANAGER_PATH, BZ_AGENT_MANAGER_IFACE,
        "UnregisterAgent", g_variant_new("(o)", BZ_AGENT_PATH), NULL,
        G_DBUS_CALL_FLAGS_NONE, BZ_CALL_TIMEOUT_MS, NULL, NULL);
    if (reply != NULL) {
      g_variant_unref(reply);
    }
    bluez.agent_registered = FALSE;
  }

  if (bluez.agent_registration > 0) {
    g_dbus_connection_unregister_object(bluez.bus, bluez.agent_registration);
    bluez.agent_registration = 0;
  }
}

/* ------------------------------------------------------------------ probe */

static gboolean bz_probe(BluetoothModePrivateData *pd) {
  GError *error = NULL;

  if (bluez.live) {
    bluez.pd = pd;
    return TRUE;
  }

  bluez.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bluez.bus == NULL) {
    g_debug("No system bus, so no org.bluez: %s",
            error != NULL ? error->message : "unknown error");
    g_clear_error(&error);
    return FALSE;
  }

  GVariant *objects = bz_managed_objects(&error);
  if (objects == NULL) {
    /* Action purpose: two failures, two remedies, so they are not one message.
     *
     * bluetoothd simply not being on the bus is the answer to the question
     * rather than a fault, and it is the ordinary state of a machine with BlueZ
     * installed and the service stopped -- exactly the case this test exists to
     * tell apart from "BlueZ is installed". So it is a debug line.
     *
     * A refusal is different: the daemon is there and would answer somebody
     * else. That is a configuration the user can change and would otherwise
     * spend a long time mistaking for an absent service, so it is said out
     * loud. */
    if (bz_is_denied(error)) {
      g_warning("org.bluez is running but refused to list its objects: %s. "
                "This session is not permitted to talk to bluetoothd -- a bus "
                "or polkit policy decision, so sofi has nothing to escalate.",
                bz_explain(error));
    } else {
      g_debug("org.bluez did not answer GetManagedObjects: %s",
              error != NULL ? error->message : "unknown error");
    }
    g_clear_error(&error);
    g_clear_object(&bluez.bus);
    return FALSE;
  }
  g_variant_unref(objects);

  bluez.live = TRUE;
  bluez.pd = pd;

  /* Sender-filtered rather than argument-filtered: everything org.bluez emits
   * on these three signals is about this surface, and one subscription per
   * interface would need two for PropertiesChanged alone. */
  bluez.sub_added = g_dbus_connection_signal_subscribe(
      bluez.bus, BZ_SERVICE, BZ_OBJECT_MANAGER_IFACE, "InterfacesAdded", NULL,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, bz_on_signal, NULL, NULL);
  bluez.sub_removed = g_dbus_connection_signal_subscribe(
      bluez.bus, BZ_SERVICE, BZ_OBJECT_MANAGER_IFACE, "InterfacesRemoved",
      NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, bz_on_signal, NULL, NULL);
  bluez.sub_props = g_dbus_connection_signal_subscribe(
      bluez.bus, BZ_SERVICE, BZ_PROPERTIES_IFACE, "PropertiesChanged", NULL,
      NULL, G_DBUS_SIGNAL_FLAGS_NONE, bz_on_signal, NULL, NULL);

  bz_agent_register();

  return TRUE;
}

static void bz_teardown(G_GNUC_UNUSED BluetoothModePrivateData *pd) {
  if (!bluez.live) {
    return;
  }

  /* Action purpose: answer before leaving. A held invocation that is simply
   * dropped leaves BlueZ waiting on its own timeout with a half-open pairing,
   * and a panel that closes is a refusal -- so it says so. */
  if (bluez.req_invocation != NULL) {
    g_dbus_method_invocation_return_dbus_error(
        bluez.req_invocation, BZ_ERROR_REJECTED,
        "The sofi panel closed without answering.");
  }
  bz_request_clear();

  if (bluez.coalesce > 0) {
    g_source_remove(bluez.coalesce);
    bluez.coalesce = 0;
  }

  bz_agent_unregister();

  if (bluez.sub_added > 0) {
    g_dbus_connection_signal_unsubscribe(bluez.bus, bluez.sub_added);
    bluez.sub_added = 0;
  }
  if (bluez.sub_removed > 0) {
    g_dbus_connection_signal_unsubscribe(bluez.bus, bluez.sub_removed);
    bluez.sub_removed = 0;
  }
  if (bluez.sub_props > 0) {
    g_dbus_connection_signal_unsubscribe(bluez.bus, bluez.sub_props);
    bluez.sub_props = 0;
  }

  if (bluez.agent_info != NULL) {
    g_dbus_node_info_unref(bluez.agent_info);
    bluez.agent_info = NULL;
  }

  g_clear_pointer(&bluez.objects, g_variant_unref);
  g_clear_pointer(&bluez.adapter, g_free);
  g_clear_object(&bluez.bus);

  bluez.pairing = FALSE;
  bluez.pd = NULL;
  /* Last, so a reply that lands between the frees above and here still finds
   * the flag it checks. */
  bluez.live = FALSE;
}

/* ------------------------------------------------------------ enumeration */

/** The pending question, as the one row on the surface. */
static void bz_request_row(BluetoothModePrivateData *pd) {
  BtRow *row = bt_row_new(pd->rows, BT_ROW_REQUEST);

  row->id = g_strdup(bluez.req_device != NULL ? bluez.req_device : "");
  row->label = g_strdup(bluez.req_label != NULL ? bluez.req_label
                                                : "Bluetooth is asking");
  row->detail = g_strdup(bluez.req_detail != NULL ? bluez.req_detail : "");
}

/**
 * Function purpose: find the adapter by asking what objects exist.
 *
 * Action purpose: an adapter exists because an `org.bluez.Adapter1` object came
 * back -- the probe discipline, applied to the thing it is about. The first one
 * wins, which matches the netgraph side taking the first node: a second
 * controller is a case neither stack's surface has ever shown.
 */
static gboolean bz_adapter(BluetoothModePrivateData *pd) {
  GError *error = NULL;

  /* Action purpose: first, and before anything that can fail.
   *
   * A question BlueZ is blocked on has to reach the screen whatever else is
   * wrong -- it was hidden behind a failed object listing until this moved up
   * here, which is the worst possible outcome: the pairing hangs with nothing
   * on the pane to answer it. Nothing is read either, so the adapter the
   * previous refresh found stays current and the scan key keeps working.
   *
   * It is also the whole surface. A pane that still listed devices and verbs
   * while an answer was outstanding would invite the user to start something
   * else with a pairing half-open, and Enter would then mean two things. */
  if (bluez.req != BZ_REQ_NONE) {
    bz_request_row(pd);
    return FALSE;
  }

  g_clear_pointer(&pd->node, g_free);
  g_clear_pointer(&pd->bdaddr, g_free);
  g_clear_pointer(&pd->local_name, g_free);
  g_clear_pointer(&bluez.adapter, g_free);
  g_clear_pointer(&bluez.objects, g_variant_unref);
  pd->scan_enable = -1;
  /* BlueZ publishes `Paired` to any caller on the system bus, so the column is
   * never the unknown the netgraph side has to warn about. */
  pd->have_paired = TRUE;
  pd->paired_needed_privilege = FALSE;

  bluez.objects = bz_managed_objects(&error);
  if (bluez.objects == NULL) {
    BtRow *gone = bt_row_new(pd->rows, BT_ROW_ADAPTER);
    gone->label = g_strdup("Bluetooth is not answering");
    gone->detail = g_strdup_printf("org.bluez: %s", bz_explain(error));
    gone->id = g_strdup("");
    g_clear_error(&error);
    return FALSE;
  }

  GVariantIter iter;
  const gchar *path = NULL;
  GVariant *interfaces = NULL;
  gboolean discoverable = FALSE;

  bluez.discovering = FALSE;
  bluez.powered = FALSE;

  /* Action purpose: the loop runs to the end and the first adapter is kept by
   * a guard rather than by a `break`. g_variant_iter_loop owns what it hands
   * out and frees it on the following turn, so leaving early would require
   * freeing it here -- and getting that wrong is a leak on every menu open.
   * Running on costs a dictionary lookup per remaining object. */
  g_variant_iter_init(&iter, bluez.objects);
  while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
    GVariant *props = bz_interface(interfaces, BZ_ADAPTER_IFACE);
    if (props == NULL) {
      continue;
    }
    if (bluez.adapter != NULL) {
      g_variant_unref(props);
      continue;
    }

    const char *address = NULL;
    const char *alias = NULL;

    bluez.adapter = g_strdup(path);
    if (g_variant_lookup(props, "Address", "&s", &address)) {
      pd->bdaddr = g_ascii_strdown(address, -1);
    }
    if (g_variant_lookup(props, "Alias", "&s", &alias) ||
        g_variant_lookup(props, "Name", "&s", &alias)) {
      pd->local_name = g_strdup(alias);
    }
    g_variant_lookup(props, "Powered", "b", &bluez.powered);
    g_variant_lookup(props, "Discoverable", "b", &discoverable);
    g_variant_lookup(props, "Discovering", "b", &bluez.discovering);

    g_variant_unref(props);
  }

  pd->node = g_strdup(bluez.adapter);

  if (bluez.adapter == NULL) {
    /* Action purpose: bluetoothd is running and has no controller. There is no
     * verb for that -- nothing sofi can do makes a dongle appear -- so the row
     * says so and offers nothing, rather than offering a power switch for a
     * device that is not there. */
    BtRow *none = bt_row_new(pd->rows, BT_ROW_ADAPTER);
    none->id = g_strdup("");
    none->label = g_strdup("No bluetooth adapter");
    none->detail = g_strdup("bluetoothd is running but reported no controller");
    return FALSE;
  }

  /* The two bits the netgraph side reads out of Scan_Enable, so one adapter row
   * serves both stacks: page scan is always on under BlueZ, and inquiry scan is
   * what `Discoverable` means. */
  pd->scan_enable = 0x02 | (discoverable ? 0x01 : 0x00);

  BtRow *adapter = bt_row_new(pd->rows, BT_ROW_ADAPTER);
  adapter->id = g_strdup(bluez.adapter);
  adapter->active = bluez.powered;

  if (!bluez.powered) {
    adapter->label = g_strdup("Bluetooth is off");
    adapter->detail = g_strdup("Enter to power the adapter on");
    /* Unpowered, BlueZ lists no devices and refuses every verb with NotReady,
     * so there is nothing below this worth drawing. */
    return FALSE;
  }

  adapter->label = g_strdup(pd->local_name != NULL && *pd->local_name != '\0'
                                ? pd->local_name
                                : "Bluetooth");
  adapter->detail = g_strdup_printf(
      "%s · %s%s", pd->bdaddr != NULL ? pd->bdaddr : bluez.adapter,
      discoverable ? "discoverable" : "connectable",
      bluez.discovering ? " · scanning" : "");

  return TRUE;
}

/** One device, kept past the iterator that produced it. */
typedef struct {
  char *path;
  /** The `org.bluez.Device1` property dictionary. Owned -- bz_interface()
   * returns a reference of its own, which is what lets it outlive the loop. */
  GVariant *props;
} BzDevice;

static void bz_device_free(gpointer data) {
  BzDevice *dev = data;

  if (dev == NULL) {
    return;
  }
  g_free(dev->path);
  g_variant_unref(dev->props);
  g_free(dev);
}

static int bz_device_cmp(gconstpointer a, gconstpointer b) {
  const BzDevice *const *x = a;
  const BzDevice *const *y = b;

  return g_strcmp0((*x)->path, (*y)->path);
}

/** Function purpose: one row per device BlueZ knows about on this adapter. */
static void bz_devices(BluetoothModePrivateData *pd) {
  if (bluez.objects == NULL || bluez.adapter == NULL) {
    return;
  }

  GVariantIter iter;
  const gchar *path = NULL;
  GVariant *interfaces = NULL;
  GPtrArray *devices = g_ptr_array_new_with_free_func(bz_device_free);

  /* Action purpose: collected and sorted before any row is built, because
   * GetManagedObjects returns objects in whatever order the daemon happens to
   * hold them -- and a list that reorders itself under the cursor between two
   * refreshes is worse than one that is slow. */
  g_variant_iter_init(&iter, bluez.objects);
  while (g_variant_iter_loop(&iter, "{&o@a{sa{sv}}}", &path, &interfaces)) {
    GVariant *props = bz_interface(interfaces, BZ_DEVICE_IFACE);
    if (props == NULL) {
      continue;
    }

    const char *owner = NULL;
    if (g_variant_lookup(props, "Adapter", "&o", &owner) &&
        g_strcmp0(owner, bluez.adapter) != 0) {
      g_variant_unref(props);
      continue;
    }

    BzDevice *dev = g_malloc0(sizeof(*dev));
    dev->path = g_strdup(path);
    dev->props = props;
    g_ptr_array_add(devices, dev);
  }

  g_ptr_array_sort(devices, bz_device_cmp);

  for (guint i = 0; i < devices->len; i++) {
    const BzDevice *dev = g_ptr_array_index(devices, i);
    const char *devpath = dev->path;
    GVariant *props = dev->props;

    const char *address = NULL;
    const char *alias = NULL;
    guint32 cls = 0;
    gint16 rssi = 0;

    BtRow *row = bt_row_new(pd->rows, BT_ROW_DEVICE);
    row->id = g_strdup(devpath);

    g_variant_lookup(props, "Address", "&s", &address);
    if (!g_variant_lookup(props, "Alias", "&s", &alias)) {
      g_variant_lookup(props, "Name", "&s", &alias);
    }
    g_variant_lookup(props, "Paired", "b", &row->paired);
    g_variant_lookup(props, "Connected", "b", &row->connected);
    g_variant_lookup(props, "Trusted", "b", &row->trusted);

    if (g_variant_lookup(props, "Class", "u", &cls)) {
      row->cod = cls;
      row->have_cod = TRUE;
      row->devkind = bt_kind_from_cod(cls);
    }
    if (g_variant_lookup(props, "RSSI", "n", &rssi)) {
      row->rssi = rssi;
      row->have_rssi = TRUE;
      /* Action purpose: RSSI is what makes "in range" knowable here. BlueZ
       * drops the property when a device stops being seen, so its presence is
       * the live signal the netgraph neighbour cache only approximates. */
      row->in_range = TRUE;
    }
    row->in_range = row->in_range || row->connected;
    row->active = row->connected;
    row->label = g_strdup(alias != NULL && *alias != '\0'
                              ? alias
                              : (address != NULL ? address : devpath));

    GString *detail = g_string_sized_new(64);
    if (address != NULL && g_strcmp0(address, row->label) != 0) {
      g_string_append(detail, address);
    }
    const char *kindname = bt_kind_name(row->devkind);
    if (kindname != NULL) {
      g_string_append_printf(detail, "%s%s", detail->len > 0 ? " · " : "",
                             kindname);
    }
    if (row->connected) {
      g_string_append_printf(detail, "%sconnected",
                             detail->len > 0 ? " · " : "");
    }
    if (row->paired) {
      g_string_append_printf(detail, "%spaired", detail->len > 0 ? " · " : "");
    }
    if (!row->paired && !row->connected && row->in_range) {
      g_string_append_printf(detail, "%sin range",
                             detail->len > 0 ? " · " : "");
    }
    /* Action purpose: a paired device that is not trusted has to be let in by
     * hand every time it comes back, which looks like an unreliable device
     * rather than a setting. Saying so on the row is what makes Alt+4 findable
     * -- the same reasoning the other backend's "needs input setup" note has. */
    if (row->paired && !row->trusted) {
      g_string_append_printf(detail, "%snot trusted (Alt+4)",
                             detail->len > 0 ? " · " : "");
    }
    /* Action purpose: BlueZ hands audio to PipeWire or PulseAudio, both of
     * which sofi's volume pane already drives, so there is nothing to set up
     * here and saying where the volume lives is more use than a verb would
     * be. */
    if (bt_kind_is_audio(row->devkind) && row->connected) {
      g_string_append_printf(detail, "%saudio: volume pane",
                             detail->len > 0 ? " · " : "");
    }

    row->detail = g_string_free(detail, FALSE);
  }

  g_ptr_array_free(devices, TRUE);
}

static void bz_actions(BluetoothModePrivateData *pd) {
  BtRow *discoverable = bt_row_new(pd->rows, BT_ROW_ACTION);
  discoverable->action = BT_ACTION_DISCOVERABLE;
  discoverable->active = pd->scan_enable > 0 && (pd->scan_enable & 0x01) != 0;
  discoverable->label = g_strdup(discoverable->active
                                     ? "Stop being discoverable"
                                     : "Make this machine discoverable");
  discoverable->detail =
      g_strdup("Other devices can find this one while it is on");

  /* Action purpose: a row and not only a key, because this stack's discovery
   * has a state -- it runs until it is stopped and says so -- and a state the
   * user can see is a state they can turn off. The netgraph sweep ends itself,
   * which is why it has no row there. */
  BtRow *scan = bt_row_new(pd->rows, BT_ROW_ACTION);
  scan->action = BT_ACTION_SCAN;
  scan->active = bluez.discovering;
  scan->label = g_strdup(bluez.discovering ? "Stop looking for devices"
                                           : "Look for devices");
  scan->detail = g_strdup(bluez.discovering
                              ? "New devices appear as they answer"
                              : "Same as Alt+1");
}

/* ----------------------------------------------------------------- verbs */

static gboolean bz_power(BluetoothModePrivateData *pd, gboolean on) {
  if (bluez.adapter == NULL) {
    bt_set_status(pd, "There is no adapter to power %s.", on ? "on" : "off");
    return FALSE;
  }
  return bz_set_bool(pd, bluez.adapter, BZ_ADAPTER_IFACE, "Powered", on);
}

static gboolean bz_connect(BluetoothModePrivateData *pd, BtRow *row) {
  return bz_call(pd, row->id, BZ_DEVICE_IFACE, "Connect", BZ_LINK_TIMEOUT_MS,
                 row->label);
}

static gboolean bz_disconnect(BluetoothModePrivateData *pd, BtRow *row) {
  return bz_call(pd, row->id, BZ_DEVICE_IFACE, "Disconnect",
                 BZ_LINK_TIMEOUT_MS, row->label);
}

/**
 * Function purpose: report how an asynchronous pairing ended.
 *
 * Action purpose: `bluez.live` is checked before anything else because this can
 * land after the panel closed and the mode's data went with it. The flag is the
 * whole reason the state this reads is file-static.
 */
static void bz_pair_done(GObject *source, GAsyncResult *result,
                         G_GNUC_UNUSED gpointer user_data) {
  GError *error = NULL;
  /* Action purpose: the connection is taken from @p source and not from the
   * file-static state, because this reply can land after the teardown has
   * dropped that reference -- which is the very case the live check below is
   * here for. GDBus keeps the connection alive for the duration of its own
   * call, so @p source is valid when nothing else is. */
  GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
                                                  result, &error);

  if (reply != NULL) {
    g_variant_unref(reply);
  }

  if (!bluez.live || bluez.pd == NULL) {
    g_clear_error(&error);
    return;
  }

  bluez.pairing = FALSE;

  if (error == NULL) {
    bt_set_status(bluez.pd, "Paired.");
  } else {
    bt_set_status(bluez.pd, "Pairing failed: %s", bz_explain(error));
    g_error_free(error);
  }

  bt_refresh(bluez.pd);
  sofi_view_reload();
}

/**
 * Function purpose: start a pairing and leave the main loop free to answer for
 * it.
 *
 * Action purpose: asynchronous, and it has to be. BlueZ raises the
 * confirmation through the agent *during* this call, so a synchronous `Pair`
 * would block the loop that has to deliver the question -- the pairing would
 * then fail on BlueZ's own agent timeout with the panel looking idle. This is
 * the one shape nothing in the netgraph path could have been ported into: there
 * the PIN is written to hcsecd's file before the link is attempted and nothing
 * is ever asked back.
 */
static gboolean bz_pair(BluetoothModePrivateData *pd, BtRow *row) {
  if (!bluez.agent_registered && !bz_agent_register()) {
    bt_set_status(pd, "BlueZ has not accepted a pairing agent from sofi, so "
                      "pairing cannot be answered from here.");
    return FALSE;
  }

  if (bluez.pairing) {
    bt_set_status(pd, "A pairing is already under way.");
    return FALSE;
  }

  bluez.pairing = TRUE;
  g_dbus_connection_call(bluez.bus, BZ_SERVICE, row->id, BZ_DEVICE_IFACE,
                         "Pair", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                         BZ_PAIR_TIMEOUT_MS, NULL, bz_pair_done, NULL);

  bt_set_status(pd, "Pairing %s. Confirm on the device if it asks.",
                row->label);

  return TRUE;
}

static gboolean bz_trust(BluetoothModePrivateData *pd, BtRow *row,
                         gboolean on) {
  return bz_set_bool(pd, row->id, BZ_DEVICE_IFACE, "Trusted", on);
}

/**
 * Function purpose: make BlueZ forget a device.
 *
 * Action purpose: `RemoveDevice` lives on the adapter and takes the device's
 * path, and it drops the link key, the pairing and the cached object in one
 * step. That is the whole of forgetting here -- there is no second and third
 * place a device is recorded, which is the entire reason the netgraph version
 * of this function is five times longer.
 */
static gboolean bz_remove(BluetoothModePrivateData *pd, BtRow *row) {
  if (bluez.adapter == NULL) {
    bt_set_status(pd, "There is no adapter to remove it from.");
    return FALSE;
  }

  GError *error = NULL;
  GVariant *reply = g_dbus_connection_call_sync(
      bluez.bus, BZ_SERVICE, bluez.adapter, BZ_ADAPTER_IFACE, "RemoveDevice",
      g_variant_new("(o)", row->id), NULL, G_DBUS_CALL_FLAGS_NONE,
      BZ_CALL_TIMEOUT_MS, NULL, &error);

  if (reply != NULL) {
    g_variant_unref(reply);
    return TRUE;
  }

  bt_set_status(pd, "Could not forget %s: %s", row->label, bz_explain(error));
  g_error_free(error);

  return FALSE;
}

static gboolean bz_scan_start(BluetoothModePrivateData *pd) {
  if (bluez.adapter == NULL) {
    bt_set_status(pd, "There is no adapter to scan with.");
    return FALSE;
  }

  if (!bz_call(pd, bluez.adapter, BZ_ADAPTER_IFACE, "StartDiscovery",
               BZ_CALL_TIMEOUT_MS, "Discovery")) {
    return FALSE;
  }

  /* Action purpose: "started", not "found". The sweep has no end here -- it
   * runs until it is stopped and devices arrive on signals -- so counting what
   * is known at this instant would report a number that is wrong a second
   * later. The other backend can count because its sweep has finished by the
   * time it returns. */
  bt_set_status(pd, "Looking for devices. They appear as they answer.");

  return TRUE;
}

static gboolean bz_scan_stop(BluetoothModePrivateData *pd) {
  if (bluez.adapter == NULL) {
    return FALSE;
  }

  if (!bz_call(pd, bluez.adapter, BZ_ADAPTER_IFACE, "StopDiscovery",
               BZ_CALL_TIMEOUT_MS, "Discovery")) {
    return FALSE;
  }

  bt_set_status(pd, "Stopped looking.");

  return TRUE;
}

static gboolean bz_scan_enable(BluetoothModePrivateData *pd,
                               gboolean discoverable) {
  if (bluez.adapter == NULL) {
    return FALSE;
  }
  /* Only inquiry scan moves. BlueZ keeps page scan on while the adapter is
   * powered, so an already-paired device can always come back -- the same
   * property the netgraph side preserves by leaving bit 0x02 set. */
  return bz_set_bool(pd, bluez.adapter, BZ_ADAPTER_IFACE, "Discoverable",
                     discoverable);
}

/**
 * Function purpose: the class-appropriate setup verb on this stack, which is
 * trust.
 *
 * Action purpose: the two things the netgraph setup verb does -- write a bthidd
 * stanza, start a virtual_oss -- both have a kernel or a sound server doing them
 * already here, so neither has anything to do. What a device does need, and what
 * is invisible until it bites, is `Trusted`: without it a paired keyboard has to
 * be authorised by hand every time it reconnects. So Alt+4 keeps its meaning of
 * "make this device actually usable" and does the one thing that means here.
 */
static gboolean bz_setup(BluetoothModePrivateData *pd, BtRow *row) {
  if (!row->paired) {
    bt_set_status(pd, "Pair %s first; trust is about a device that is already "
                      "paired.",
                  row->label);
    return FALSE;
  }

  if (!bz_trust(pd, row, !row->trusted)) {
    return FALSE;
  }

  if (row->trusted) {
    bt_set_status(pd, "%s is no longer trusted and will be asked about each "
                      "time it reconnects.",
                  row->label);
  } else {
    bt_set_status(pd, "%s is trusted and may reconnect on its own.",
                  row->label);
  }

  return TRUE;
}

static gboolean bz_action(BluetoothModePrivateData *pd, BtRow *row,
                          G_GNUC_UNUSED gboolean *should_exit) {
  switch (row->action) {
  case BT_ACTION_SCAN:
    return row->active ? bz_scan_stop(pd) : bz_scan_start(pd);

  /* Action purpose: this stack has no equivalent of either and must not
   * pretend. `hccontrol reset` drops every link through a raw HCI socket BlueZ
   * owns, and restarting the daemons is the init system's business and not a
   * menu's. Neither row is ever built here; the cases exist so that a row that
   * somehow arrives says something true. */
  case BT_ACTION_RESET:
  case BT_ACTION_RESTART_STACK:
    bt_set_status(pd, "That verb belongs to the netgraph stack and has no "
                      "equivalent under BlueZ.");
    return FALSE;

  case BT_ACTION_DISCOVERABLE:
  default:
    return FALSE;
  }
}

/** Answer the held question, or dismiss a notice that was never one. */
static gboolean bz_request(BluetoothModePrivateData *pd, BtRow *row,
                           gboolean accept) {
  const char *what = bluez.req_label != NULL ? bluez.req_label : row->label;

  if (bluez.req == BZ_REQ_NONE) {
    return FALSE;
  }

  if (bluez.req_invocation == NULL) {
    /* A ::BZ_REQ_NOTICE: BlueZ was told immediately and is not waiting. */
    bz_request_clear();
    bt_set_status(pd, "Dismissed.");
    return TRUE;
  }

  if (accept) {
    g_dbus_method_invocation_return_value(bluez.req_invocation, NULL);
    bt_set_status(pd, "Accepted. %s", what);
  } else {
    g_dbus_method_invocation_return_dbus_error(
        bluez.req_invocation, BZ_ERROR_REJECTED, "Refused in sofi.");
    bt_set_status(pd, "Refused. %s", what);
  }

  /* The reply consumed the invocation, so the pointer must go with it. */
  bluez.req_invocation = NULL;
  bz_request_clear();

  return TRUE;
}

static char *bz_message(const BluetoothModePrivateData *pd) {
  if (bluez.req != BZ_REQ_NONE) {
    /* Two calls rather than a chosen format string, for the reason the scan
     * status gives: a non-literal format defeats the check that catches a
     * mismatch between it and its arguments. */
    if (bluez.req_invocation != NULL) {
      return g_markup_printf_escaped("Enter accepts  ·  Escape refuses  ·  "
                                     "nothing is accepted on its own");
    }
    return g_markup_printf_escaped("Enter dismisses this");
  }

  if (pd->node == NULL) {
    /* Action purpose: these two are different problems and were one message
     * until a probe against a machine with no system bus printed the wrong one.
     * "No controller" is a missing dongle; "not answering" is a daemon that is
     * not there, which is the state this backend's probe exists to detect. */
    if (bluez.objects == NULL) {
      return g_markup_printf_escaped(
          "org.bluez is not answering on the system bus");
    }
    return g_markup_printf_escaped(
        "No adapter  ·  bluetoothd answered but reported no controller");
  }

  if (!bluez.powered) {
    return g_markup_printf_escaped(
        "Bluetooth is off  ·  Enter powers the adapter on");
  }

  return g_markup_printf_escaped(
      "Enter connect  ·  Alt+1 scan  ·  Alt+2 disconnect  ·  Alt+3 forget  ·  "
      "Alt+4 trust  ·  org.bluez %s",
      pd->node);
}

static const BtBackend bt_bluez = {
    .name = "org.bluez",
    .probe = bz_probe,
    .adapter = bz_adapter,
    .devices = bz_devices,
    .actions = bz_actions,
    .power = bz_power,
    .connect = bz_connect,
    .disconnect = bz_disconnect,
    .pair = bz_pair,
    .trust = bz_trust,
    .remove = bz_remove,
    .scan_start = bz_scan_start,
    .scan_stop = bz_scan_stop,
    .scan_enable = bz_scan_enable,
    .setup = bz_setup,
    .action = bz_action,
    .request = bz_request,
    .message = bz_message,
    .teardown = bz_teardown,
};

const BtBackend *bt_bluez_backend(void) { return &bt_bluez; }

#endif // BLUETOOTH_MODE
