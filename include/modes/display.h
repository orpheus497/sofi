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

#ifndef SOFI_MODE_DISPLAY_H
#define SOFI_MODE_DISPLAY_H

#include "mode.h"
/**
 * @defgroup DISPLAYMode Display
 * @ingroup MODES
 *
 * Resolution, refresh rate, position, scale, rotation and brightness, per
 * output.
 *
 * A drill-down rather than a flat list: displays, then one display's settings,
 * then a picker for whichever setting was chosen. A single output can advertise
 * twenty-five modes, so flattening a multi-monitor machine's settings into one
 * column produces a list nothing can be found in.
 *
 * Two tools, because one protocol does not cover it. `wlr-randr(1)` speaks
 * `wlr-output-management-unstable-v1` and carries mode, refresh, position,
 * scale, transform, adaptive sync and enablement. **No Wayland protocol carries
 * brightness**, so that is `backlight(8)` from the FreeBSD base system --
 * unprivileged for a user in the `video` group, and **per-machine rather than
 * per-output**, so it applies to the internal panel only. External monitors
 * need DDC/CI, which is not attempted here.
 *
 * Neither tool is linked and neither is a build dependency.
 *
 * @{
 */
/**
 * #Mode object representing output management
 */
extern Mode display_mode;
/**@}*/
#endif // SOFI_MODE_DISPLAY_H
