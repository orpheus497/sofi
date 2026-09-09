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

#ifndef SOFI_MODE_VOLUME_H
#define SOFI_MODE_VOLUME_H

#include "mode.h"
/**
 * @defgroup VOLUMEMode Volume
 * @ingroup MODES
 *
 * Audio output control: one row per sink, with its level, its mute state and
 * which one the session is using.
 *
 * This is a *summoned* surface, not an indicator. It holds no state between
 * invocations, subscribes to nothing, and exits on selection like every other
 * sofi mode. The persistent volume readout belongs to hikari-sakura's top bar
 * and the persistent panel belongs to saber; neither is duplicated here.
 *
 * Sofi links no audio library. Every backend is an ordinary command-line tool
 * driven as a subprocess, chosen at runtime from what is installed:
 *
 *  - `wpctl`  — WirePlumber / PipeWire
 *  - `pactl`  — PulseAudio, and PipeWire's PulseAudio shim
 *  - `mixer`  — the FreeBSD base system, which needs nothing installed at all
 *
 * Executing a binary is not linking, so this adds no dependency to the build
 * and raises no licence question under `AGENTS.md` §2.
 *
 * @{
 */
/**
 * #Mode object representing the volume control
 */
extern Mode volume_mode;
/**@}*/
#endif // SOFI_MODE_VOLUME_H
