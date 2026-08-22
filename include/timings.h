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

/**
 * @defgroup TIMINGS Timings
 * @ingroup HELPERS
 * @{
 */
#ifndef SOFI_TIMINGS_H
#define SOFI_TIMINGS_H

/**
 * Init the timestamping mechanism .
 * implementation.
 */
void sofi_timings_init(void);
/**
 * @param file filename tick originates from
 * @param str function name.
 * @param line line number
 * @param msg message
 *
 * Report a tick.
 */
void sofi_timings_tick(const char *file, char const *str, int line,
                       char const *msg);
/**
 * Stop the timestamping mechanism
 */
void sofi_timings_quit(void);

/**
 * Start timestamping mechanism.
 * Call to this function is time 0.
 */
#define TIMINGS_START() sofi_timings_init()
/**
 * Report current time since TIMINGS_START
 */
#define TICK() sofi_timings_tick(__FILE__, __func__, __LINE__, "")
/**
 * @param a an string
 * Report current time since TIMINGS_START
 */
#define TICK_N(a) sofi_timings_tick(__FILE__, __func__, __LINE__, a)
/**
 * Stop timestamping mechanism.
 */
#define TIMINGS_STOP() sofi_timings_quit()

#else

/**
 * Start timestamping mechanism.
 * Call to this function is time 0.
 */
#define TIMINGS_START()
/**
 * Stop timestamping mechanism.
 */
#define TIMINGS_STOP()
/**
 * Report current time since TIMINGS_START
 */
#define TICK()
/**
 * @param a an string
 * Report current time since TIMINGS_START
 */
#define TICK_N(a)

#endif // SOFI_TIMINGS_H
/**@}*/
