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
 * Output resolution, scale and position.
 *
 * **This is a stub, and the reason is the compositor rather than sofi.**
 * hikari-sakura creates `wlr_xdg_output_manager_v1` -- read-only geometry --
 * and `wlr_fractional_scale_manager_v1`, and does **not** create
 * `wlr_output_manager_v1`. There is therefore no protocol on that compositor by
 * which any client can *set* a mode, a scale or an output position. Display
 * management is compositor work before it is sofi work.
 *
 * What this mode does today is list the outputs, read-only, from `wlr-randr`
 * where that is installed, and state the blocker. That is worth having: it
 * answers "what does the system think is connected" without a terminal.
 *
 * @{
 */
/**
 * #Mode object representing output management
 */
extern Mode display_mode;
/**@}*/
#endif // SOFI_MODE_DISPLAY_H
