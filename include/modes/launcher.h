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

#ifndef SOFI_MODE_LAUNCHER_H
#define SOFI_MODE_LAUNCHER_H

#include "mode.h"
/**
 * @defgroup LAUNCHERMode Launcher
 * @ingroup MODES
 *
 * The sofi manager: a horizontal strip of buttons along the bottom of the
 * screen, one per sofi indexer, each with an icon.
 *
 * **This is what `sofi -show window` opens.** The strip is the entry point to
 * the whole suite -- applications, windows, sheets, volume, network,
 * notifications, files, commands and hosts -- so a single keybinding reaches
 * every surface sofi has instead of one binding per surface.
 *
 * The system tray keeps its place in the strip's right-hand corner, because the
 * strip is still the same surface it always was; only its list changed.
 *
 * The window switcher is not gone. It is the **Windows** button, and it opens
 * *in place* in this same strip rather than as a separate surface, which is the
 * shape it always had. It is also still reachable directly as
 * `sofi -show windowlist`.
 *
 * @{
 */
/**
 * #Mode object representing the sofi manager strip
 */
extern Mode launcher_mode;
/**@}*/
#endif // SOFI_MODE_LAUNCHER_H
