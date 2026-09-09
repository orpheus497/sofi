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
 * Bluetooth device management.
 *
 * **This is a stub.** It detects which bluetooth stack the machine has and says
 * so; it does not yet pair, connect or disconnect anything. It exists now
 * because the control panel needs its button in the right place, and because
 * the detection is the part that decides how the rest gets written.
 *
 * Two stacks, and they share nothing:
 *
 *  - **FreeBSD** is netgraph, with `hccontrol`, `sdpcontrol` and
 *    `bthidcontrol`. There is **no D-Bus bluetooth daemon in the base system**,
 *    so the native path is subprocesses, like the volume and network modes.
 *  - **Linux** is BlueZ over `org.bluez`. BlueZ is GPL and is never linked --
 *    talking to a daemon over its published interface is not linking, ruled
 *    2026-09-09 (`AGENTS.md` §2).
 *
 * @{
 */
/**
 * #Mode object representing bluetooth management
 */
extern Mode bluetooth_mode;
/**@}*/
#endif // SOFI_MODE_BLUETOOTH_H
