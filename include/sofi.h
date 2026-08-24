/*
 * sofi
 *
 * MIT/X11 License
 * Copyright © 2013-2023 Qball Cow <qball@gmpclient.org>
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

#ifndef SOFI_MAIN_H
#define SOFI_MAIN_H
#include "keyb.h"
#include "mode.h"
#include "sofi-types.h"
#include "view.h"
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon.h>

/**
 * @defgroup Main Main
 * @{
 */

/**
 * Pointer to xdg cache directory.
 */
extern const char *cache_dir;

/**
 * Get the number of enabled modes.
 *
 * @returns the number of enabled modes.
 */
unsigned int sofi_get_num_enabled_modes(void);

/**
 * @param index The mode to return. (should be smaller then
 * sofi_get_num_enabled_mode)
 *
 * Get an enabled mode handle.
 *
 * @returns a Mode handle.
 */
const Mode *sofi_get_mode(unsigned int index);

/**
 * @param name Name of the mode to lookup.
 *
 * Find the index of the mode with name.
 *
 * @returns index of the mode in modes, -1 if not found.
 */
int mode_lookup(const char *name);

/**
 * @param str A GString with an error message to display.
 *
 * Queue an error.
 */
void sofi_add_error_message(GString *str);

/**
 * Clear the list of stored error messages.
 */
void sofi_clear_error_messages(void);

/**
 * @param str A GString with an warning message to display.
 *
 * Queue an warning.
 */
void sofi_add_warning_message(GString *str);

/**
 * Clear the list of stored warning messages.
 */
void sofi_clear_warning_messages(void);
/**
 * @param code the code to return
 *
 * Return value are used for integrating dmenu sofi in scripts.
 * This function sets the code that sofi will return on exit.
 */
void sofi_set_return_code(int code);

void sofi_quit_main_loop(void);

/**
 * @param name Search for mode with this name.
 *
 * @return returns Mode * when found, NULL if not.
 */
Mode *sofi_collect_modes_search(const char *name);

/**
 * Bring the notification banner in step with the store.
 *
 * The daemon's render loop: creates the view on the first notification,
 * reloads while any are live, and cancels it when the last one goes. Only
 * defined when built with the notification daemon.
 */
void sofi_notify_daemon_refresh(void);

/**
 * Query the configure file completer.
 *
 * @returns the Mode that can be used for file completion or NULL when not
 * found.
 */
const Mode *sofi_get_completer(void);
/** Reset terminal */
#define color_reset "\033[0m"
/** Set terminal text bold */
#define color_bold "\033[1m"
/** Set terminal text italic */
#define color_italic "\033[2m"
/** Set terminal foreground text green */
#define color_green "\033[0;32m"
/** Set terminal foreground text red */
#define color_red "\033[0;31m"

/** Appends instructions on how to report an error. */
#define ERROR_MSG(a)                                                           \
  a "\n"                                                                       \
    "If you suspect this is caused by a bug in sofi,\n"                        \
    "please report the following information to sofi's github page:\n"         \
    " * The generated commandline output when the error occurred.\n"           \
    " * Output of -dump-xresource\n"                                           \
    " * Steps to reproduce\n"                                                  \
    " * The version of sofi you are running\n\n"                               \
    " <i>https://github.com/orpheus497/sofi/</i>"
/** Indicates if ERROR_MSG uses pango markup */
#define ERROR_MSG_MARKUP TRUE
/**@}*/
#endif
