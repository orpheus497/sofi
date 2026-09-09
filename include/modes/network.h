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

#ifndef SOFI_MODE_NETWORK_H
#define SOFI_MODE_NETWORK_H

#include "mode.h"
/**
 * @defgroup NETWORKMode Network
 * @ingroup MODES
 *
 * Network management: wireless scanning and switching, the wireless radio,
 * interfaces up and down, a DHCP renewal, a re-association with the access
 * point, and a full restart of every controller.
 *
 * A *summoned* surface, like every other sofi mode. It subscribes to nothing,
 * polls nothing and holds no state between invocations. A persistent network
 * indicator belongs to hikari-sakura's top bar, and a persistent panel to
 * saber; neither is duplicated here.
 *
 * Sofi links no network library. Both backends are ordinary command-line tools
 * driven as subprocesses and chosen at runtime from what actually answers:
 *
 *  - `nmcli`  — NetworkManager, where it is installed *and running*
 *  - the base system — `ifconfig`, `wpa_cli`, `dhclient` and `service`
 *
 * Executing a binary is not linking, so this adds no dependency to the build
 * and raises no licence question under `AGENTS.md` §2 — which is what makes
 * NetworkManager usable as a backend at all, since it is GPL.
 *
 * **Most verbs here need privilege**, and sofi installs nothing setuid and
 * writes no `sudoers` rule. See `network-privilege-command` in sofi(1).
 *
 * @{
 */
/**
 * #Mode object representing the network manager
 */
extern Mode network_mode;
/**@}*/
#endif // SOFI_MODE_NETWORK_H
