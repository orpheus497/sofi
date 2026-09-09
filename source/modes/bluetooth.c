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
 * @brief Bluetooth device management. STUB.
 *
 * Script function and purpose: reports which bluetooth stack this machine has
 * and what is therefore possible. It does not yet pair, connect or disconnect.
 *
 * **This is deliberately a stub and says so on screen.** It exists now so the
 * control panel's button sits in its final place, and because backend detection
 * is the part that decides how the working version gets written -- the two
 * stacks share no vocabulary at all:
 *
 *   FreeBSD   netgraph. `hccontrol`, `sdpcontrol`, `bthidcontrol`. There is no
 *             D-Bus bluetooth daemon in the base system, so this is a
 *             subprocess backend like volume and network.
 *   Linux     BlueZ over `org.bluez`, via GDBus. BlueZ is GPL and is never
 *             linked; talking to a daemon is not linking (AGENTS.md §2, ruled
 *             2026-09-09).
 *
 * A stub that reports the truth is worth more than an empty list: "no backend"
 * and "a backend, but sofi cannot drive it yet" are different problems and the
 * user should not have to guess which one they have.
 */

/** The log domain of this dialog. */
#define G_LOG_DOMAIN "Modes.Bluetooth"

#include "config.h"

#ifdef BLUETOOTH_MODE

#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "helper.h"
#include "modes/bluetooth.h"
#include "settings.h"
#include "widgets/textbox.h"

#include "mode-private.h"

/** A tool that would drive one of the two stacks, and what it means. */
typedef struct {
  const char *binary;
  const char *stack;
} BluetoothProbe;

/** Action purpose: `bluetoothctl` is the BlueZ client and its presence means a
 * Linux-style stack; `hccontrol` is FreeBSD base and means netgraph. Neither is
 * driven yet -- they are probed so the row can name what is actually here. */
static const BluetoothProbe bluetooth_probes[] = {
    {.binary = "bluetoothctl", .stack = "BlueZ (org.bluez)"},
    {.binary = "hccontrol", .stack = "FreeBSD netgraph (hccontrol)"},
};

typedef struct {
  /** The stack that was found, or NULL. Not owned -- points into the table. */
  const char *stack;
} BluetoothModePrivateData;

static int bluetooth_mode_init(Mode *sw) {
  if (mode_get_private_data(sw) != NULL) {
    return TRUE;
  }

  BluetoothModePrivateData *pd = g_malloc0(sizeof(*pd));
  mode_set_private_data(sw, (void *)pd);

  for (unsigned int i = 0; i < G_N_ELEMENTS(bluetooth_probes); i++) {
    char *found = g_find_program_in_path(bluetooth_probes[i].binary);
    if (found != NULL) {
      g_free(found);
      pd->stack = bluetooth_probes[i].stack;
      break;
    }
  }

  /* Action purpose: TRUE even with no stack found. Unlike the volume and
   * network modes there is nothing here that can fail to work, because nothing
   * here works yet -- the single row explains the situation either way, and an
   * error dialog would say less than the row does. */
  return TRUE;
}

static unsigned int
bluetooth_mode_get_num_entries(G_GNUC_UNUSED const Mode *sw) {
  return 1;
}

static char *_get_display_value(const Mode *sw, unsigned int selected_line,
                                int *state,
                                G_GNUC_UNUSED GList **attr_list,
                                int get_entry) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL || selected_line > 0) {
    return get_entry ? g_strdup("") : NULL;
  }

  /* The row is informational, not actionable. URGENT is the state a theme
   * already dims, which is the right reading for a surface that cannot yet do
   * the thing it is named after. */
  *state |= URGENT;

  if (!get_entry) {
    return NULL;
  }

  if (pd->stack == NULL) {
    return g_strdup("No bluetooth stack found");
  }
  return g_strdup_printf("%s found — not yet driven by sofi", pd->stack);
}

static char *bluetooth_mode_get_message(const Mode *sw) {
  const BluetoothModePrivateData *pd =
      (const BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd == NULL) {
    return NULL;
  }

  if (pd->stack == NULL) {
    return g_markup_printf_escaped(
        "Install bluetoothctl (BlueZ) or use FreeBSD's hccontrol.");
  }
  return g_markup_printf_escaped(
      "Pairing and connecting are not implemented yet.");
}

static ModeMode bluetooth_mode_result(G_GNUC_UNUSED Mode *sw, int mretv,
                                      G_GNUC_UNUSED char **input,
                                      G_GNUC_UNUSED unsigned int
                                          selected_line) {
  if (mretv & MENU_NEXT) {
    return NEXT_DIALOG;
  }
  if (mretv & MENU_PREVIOUS) {
    return PREVIOUS_DIALOG;
  }
  if (mretv & MENU_QUICK_SWITCH) {
    return (ModeMode)(mretv & MENU_LOWER_MASK);
  }

  return MODE_EXIT;
}

static void bluetooth_mode_destroy(Mode *sw) {
  BluetoothModePrivateData *pd =
      (BluetoothModePrivateData *)mode_get_private_data(sw);

  if (pd != NULL) {
    g_free(pd);
    mode_set_private_data(sw, NULL);
  }
}

static int bluetooth_token_match(G_GNUC_UNUSED const Mode *sw,
                                 sofi_int_matcher **tokens,
                                 G_GNUC_UNUSED unsigned int index) {
  return helper_token_match(tokens, "bluetooth");
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
