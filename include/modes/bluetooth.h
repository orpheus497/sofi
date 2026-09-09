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

#ifndef SOFI_MODE_BLUETOOTH_H
#define SOFI_MODE_BLUETOOTH_H

#include "mode.h"
/**
 * @defgroup BLUETOOTHMode Bluetooth
 * @ingroup MODES
 *
 * Bluetooth device management on the FreeBSD netgraph stack.
 *
 * Power, discovery, pairing, connecting, forgetting, and the per-class setup
 * that makes an input or audio device usable once the link exists. Everything
 * is subprocess-driven; nothing is linked and no build dependency is added.
 *
 * **One stack, and it is not BlueZ.** Ruled by USER 2026-09-09 (R12). FreeBSD's
 * bluetooth stack is netgraph -- `hccontrol(8)`, `sdpcontrol(8)`,
 * `bthidcontrol(8)`, with `hcsecd(8)` holding PINs and link keys and
 * `bthidd(8)` driving input devices. There is no D-Bus bluetooth daemon in the
 * base system and BlueZ is Linux-only, so no `org.bluez` backend exists here.
 *
 * Three things about this stack shape the mode and are worth knowing before
 * reading it:
 *
 *  - **The paired-device list is a root-only file.** `/etc/bluetooth/hcsecd.conf`
 *    is `0600 root`, so listing, pairing and forgetting all go through
 *    `network-privilege-command` -- the same option the network mode uses,
 *    reused rather than duplicated. Everything else reads unprivileged.
 *  - **Class-of-device is what tells a gamepad from a headset**, and it decides
 *    which setup verb applies. Names are chosen by manufacturers and mean
 *    nothing.
 *  - **Pairing an input device is not enough to make it send input.** `bthidd`
 *    needs its own stanza in `/etc/bluetooth/bthidd.conf`, which the connect
 *    path writes.
 *
 * Runtime tools, none of them linked and none a build dependency: `hccontrol`,
 * `bthidcontrol` and `service` from the FreeBSD base system, and optionally
 * `virtual_oss` for an audio path.
 *
 * @{
 */
/**
 * #Mode object representing bluetooth management
 */
extern Mode bluetooth_mode;
/**@}*/
#endif // SOFI_MODE_BLUETOOTH_H
