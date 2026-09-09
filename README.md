<h1 align="center">
  <img src="data/sofi.svg" alt="" width="96" height="96"><br>
  Sofi
</h1>
<p align="center"><b>S</b>akura <b>O</b>fficial <b>F</b>ull <b>I</b>ndexer</p>
<p align="center"><i>The UI display and layer-shell layer of the hikari-sakura Wayland compositor — control panel, application menu, sheet switcher, volume and network panes, and notification daemon, from one binary.</i></p>

<p align="center">
  <img src=".github/sofi_screenshot.png" alt="Four sofi surfaces on one hikari-sakura desktop: the sheet switcher as a row of ten chips under the top bar, the application menu rising from the bottom centre, the notification history down the right edge, and a strip along the bottom." width="100%">
</p>
<p align="center"><sub>Four surfaces at once — sheet switcher under the top bar, application menu bottom centre, notification history on the right edge, and the bottom strip. One binary, one invocation each, no configuration file. <b>This screenshot is historical:</b> the bottom strip shown here listed windows and is now the <a href="#the-control-panel">control panel</a>, and the tray it later grew has since moved to <a href="https://github.com/orpheus497/saber">saber</a>.</sub></p>

## The name

**Sofi** is the **Sakura Official Full Indexer**.

Sofi began as a hard fork of [rofi](https://github.com/davatorium/rofi). The
acronym came with it, and rather than discard a name users would recognise, it
was kept and given a meaning that describes what the program actually became:

**Indexer** is the literal job. Sofi builds indexes and puts them on screen —
applications from desktop files, executables from `$PATH`, windows from the
compositor, sheets from hikari's control socket, notifications from the session
bus, tray items from StatusNotifierItem, files from the filesystem, hosts from
your SSH config. Every surface you see is an index rendered.

**Full** is the scope. Sofi is not a launcher that a desktop is assembled
around; it *is* the desktop's shell — every system surface hikari-sakura needs,
in one binary.

**Sakura Official** is the family. Sofi is one of four programs built as a set,
described below.

Sofi is developed independently and does not track upstream. It is MIT licensed
— see [COPYING](COPYING) and [AUTHORS](AUTHORS).

> **Sofi is a separate program, not a rofi drop-in.** It does not read rofi's
> configuration, themes, cache or script-mode environment variables, and rofi
> plugins will not load. Configuration lives in `~/.config/sofi/config.sasi`,
> themes use the `.sasi` extension, and script modes receive `SOFI_*` variables.
> File sofi issues here and rofi issues upstream; neither project supports the
> other.

## The Sakura set

Four programs, built to be used together, each usable on its own:

| | Program | Written in | Role |
|---|---|---|---|
| 1 | [**sakura**](https://github.com/orpheus497/sakura) | Zig | **Display manager.** A TUI login manager on a FreeBSD virtual terminal. Talks to OpenPAM directly; no toolkit, no session bus, no login-manager framework |
| 2 | [**hikari-sakura**](https://github.com/orpheus497/hikari-sakura) | C | **Compositor.** A stacking Wayland compositor with tiling, built on wlroots and on views, groups and *sheets*. Draws its own top bar **and its own lock screen**, the latter authenticating through a separate setuid `hikari-unlocker` |
| 3 | [**saber**](https://github.com/orpheus497/saber) | C | **Panel.** A persistent Unity-7-style vertical launcher down the left edge, carrying running-application tiles, quicklists, the system tray and session controls |
| 4 | **sofi** — this repository | C | **Overlays.** Every surface that is summoned rather than always present, plus the notification daemon |

### The line between sofi and saber

**Persistence.** That is the whole distinction, and it decides every case:

- **saber is always on screen.** It reserves an exclusive zone, so windows tile
  beside it rather than under it. It is aimed at without looking, and it is
  where the system tray lives.
- **Every sofi surface is summoned.** It appears on a keypress or a click,
  does one job, and dismisses on selection. It reserves no space and holds no
  state between invocations.

So the two do not compete for screen area even where they cover the same
subject: saber's Dash is a docked application grid, `sofi -show drun` is a
summoned one, and using both is normal.

**There is exactly one hard conflict, and it is the system tray** — see
[System tray](#system-tray). Everything else coexists.

**What sofi deliberately does not do**, because saber does it: a persistent
taskbar, a persistent tray, application launcher tiles, `com.canonical.Unity`
count badges and progress bars, quicklists, removable-device and trash items,
and session controls (suspend, reboot, shut down). None of these are planned
here.

**What saber deliberately does not do**, because sofi does it: notifications of
any kind. `sofi -notification-daemon` and `sofi -show notification-history` are
unaffected by saber and should keep running alongside it.

### How they hand off

Nothing here is private glue — each step is an ordinary published interface, and
that is deliberate:

1. **sakura** enumerates session files from
   `/usr/local/share/wayland-sessions/` — the freedesktop convention, so any
   session works with it, not only this one.
2. **hikari-sakura** installs one: `hikari.desktop`, `Exec=start-hikari`.
   Selecting it at the login screen starts the compositor.
3. **hikari-sakura's shipped configuration already calls sofi.** Its
   `actions {}` block binds four of sofi's surfaces out of the box:

   | Key | Action | Runs |
   |---|---|---|
   | `Logo`+`Space` | `action-menu` | `sofi -show drun` |
   | `Logo`+`w` | `action-windows` | `sofi -show window` — the control panel |
   | `Logo`+`e` | `action-sheets` | `sofi -show sheets` |
   | `Logo`+`n` | `action-notifications` | `sofi -show notification-history` |

4. **sofi's daemons** are autostarted in the session, alongside `saber` — see
   [Autostart](#autostart).
5. **saber triggers sofi by running it.** There is no sofi IPC socket and none
   is needed: each surface is one invocation with its own instance lock, so a
   panel button, a keybinding and a shell script all reach sofi the same way —
   see [Triggering sofi from saber](#triggering-sofi-from-saber).

### One palette, and the honest limit of it

Sofi's sixteen colour slots are **byte-identical** to hikari-sakura's
`ui { palette }` block — `color0 #2b1e3a` through `color15 #f0edf2`, all sixteen.
One scheme dresses the compositor, its bar and the shell together, and sofi's
semantic names map onto hikari's `ui { colorscheme }` slot for slot. Change the
sixteen values in one place and the whole desktop follows.

**The display manager cannot join that.** sakura draws on a `vt(4)` console,
which stores a colour in three bits plus a brightness bit — true 24-bit output is
not possible there. It *echoes* the scheme in the sixteen console colours; it
does not share the file. That is a limit of the console, not an omission.

## What sofi does

Sofi presents **ten system surfaces** across **twelve invocations** of one
binary. Each has its own layout compiled in and its own instance lock, so they
coexist rather than replacing one another — and **none of them needs a
configuration file**.

| Surface | Invocation | Indexes | Where it renders |
|---|---|---|---|
| **Application menu** | `sofi -show drun` | Desktop files | Bottom centre, 560px wide, above the control panel |
| **Control panel** | `sofi -show window` | Sofi's own indexers, as buttons | Strip along the bottom, inset from the edges |
| **Sheet switcher** | `sofi -show sheets` | hikari sheets 0–9 | Top centre, a row of ten chips under the compositor's bar |
| **Keys** | `sofi -show keys` | Sofi's own keybindings | Centre, 980px wide, two columns |
| **Display** | `sofi -show display` | Outputs, read-only *(stub)* | The default menu shape |
| **Volume** | `sofi -show volume` | Audio sinks, via `wpctl`/`pactl`/`mixer` | Top-right corner, 460px wide, under the compositor's bar |
| **Bluetooth** | `sofi -show bluetooth` | Adapter and devices, via FreeBSD netgraph (`hccontrol`/`bthidcontrol`) | Top-right corner, 520px wide, under the compositor's bar |
| **Network** | `sofi -show network` | Interfaces and wireless networks, via `nmcli` or `ifconfig`/`wpa_cli` | Top-right corner, 460px wide, under the compositor's bar |
| **Notifications** — daemon | `sofi -notification-daemon` | `org.freedesktop.Notifications` | Stack in the bottom-right corner |
| **Notifications** — history | `sofi -show notification-history` | The persisted ring | Right edge, 420px wide |
| **System tray** — host | `sofi -tray-daemon` | `org.kde.StatusNotifierWatcher` | **No surface at all by default** — saber owns the tray. See [System tray](#system-tray) |
| *Message toast* | `sofi -e <message>` | *(a utility, not a system surface)* | Top-right corner |

`~/.config/sofi/` is optional, and anything you put there still overrides the
built-in layout. Every placement above is one property in a compiled-in layout,
changeable in four lines — see [Theming](#theming).

Sofi is developed primarily on FreeBSD and targets Wayland via
`zwlr_layer_shell_v1` (bound at version 4). It retains a working X11/xcb backend
and every general-purpose mode it inherited, so it is also usable as a standalone
launcher on other compositors and window managers.

### What sofi is not

- **Not a UI toolkit, and not a library.** It is an application.
- **Not a rofi drop-in.** See the banner above. The shared ancestry is history,
  not compatibility.
- **Not a compositor.** Sofi draws surfaces and indexes state; hikari-sakura owns
  windows, input routing, output management and the lock screen. Where a
  capability needs the compositor, sofi says so rather than pretending — and the
  lock screen is the clearest case. **hikari already locks**, on its own scene
  layer with its own indicator, authenticating through a separate setuid
  `hikari-unlocker`; what it does not do is expose a verb a client could call, so
  sofi has no way to trigger it and offers no lock entry. That is a missing
  interface, not a missing feature, and the same is true of logging out.
- **Not required by hikari-sakura, and hikari-sakura is not required by sofi.**
  The general-purpose modes run anywhere; the sheet switcher is the one part that
  needs hikari's socket, and it exits cleanly when there isn't one.

## Table of contents

- [The surfaces](#the-surfaces) — what each one does, in detail
- [Theming](#theming)
- [Features](#features)
- [Modes](#modes)
- [Wayland support](#wayland-support)
- [Documentation](#documentation)
- [Installation](#installation)
- [Quickstart](#quickstart)

For exhaustive per-capability reference — every mode, verb, keybinding, daemon,
D-Bus interface and build option — see **[FEATURES.md](FEATURES.md)**.

## The surfaces

### Application menu

```bash
sofi -show drun
```

Rises from the bottom centre, clearing the control panel, with icons and a two-tier
row — application name, then generic name beside it in a lighter weight. This is
the default surface: any mode that is not one of those below gets the same shape,
so `run`, `ssh`, `combi`, `filebrowser` and user script modes all look
consistent.

### The control panel

```bash
sofi -show window
```

**The entry point to the whole suite.** A strip along the bottom, inset from the
edges so it reads as a floating bar, carrying one button per sofi indexer with
an icon on each.

| Button | Runs | Gated by |
|---|---|---|
| Keys | `-show keys` — the keyboard shortcut helper | — |
| Applications | `-show drun` — the application menu | `-Ddrun` |
| Run | `-show run` — a command from `$PATH` | — |
| Files | `-show filebrowser` | — |
| Find Files | `-show recursivebrowser` | — |
| SSH | `-show ssh` — hosts from your SSH config | — |
| Display | `-show display` — outputs | `-Ddisplay` |
| Sheets | `-show sheets` | `-Dsheets` |
| Volume | `-show volume` | `-Dvolume` |
| Bluetooth | `-show bluetooth` | `-Dbluetooth` |
| Network | `-show network` | `-Dnetwork` |
| Notifications | `-show notification-history` | `-Dnotify` |

**Keys is first** because it is the one button that explains all the others.

**Display is a stub** — see [Display](#display). It is on the panel because
that is where it will be, and it says on screen what it can and cannot do
rather than looking broken.

Six things are deliberately not on it:

| Not on the panel | Why |
|---|---|
| Dismiss, Clear History | Verbs of the **notification menu**, where the list they act on is on screen. A button that discards a notification history from a strip showing no notifications has nothing to aim at |
| `combi` | Merging every index into one list is a thing to configure for a keybinding, not a button beside the individual indexes it duplicates |
| The window switcher (`windowlist`) | **saber's job.** Retained for desktops without saber |
| The system tray (`-tray-daemon`) | **saber's job.** Retained, but no longer has a surface |
| `-dmenu` | Reads its list from stdin — there is nothing for a button to pipe it |
| The two daemons | `-notification-daemon` and `-tray-daemon` belong in your autostart, not on a key |

Your own script modes are not on it either: they are discovered per user at
runtime and this list is compiled in.

One keybinding therefore reaches every surface sofi has, instead of one binding
per surface. The shipped `hikari.conf` already binds this to `Logo`+`w`.

**A button whose mode is not in your binary is not shown.** Build without
`-Dvolume` and there is no Volume button, rather than one that reports "mode not
found".

Every button runs `sofi -show <mode>` — the same contract saber and
`hikari.conf` use, so a button here and a keybinding there reach a surface by
exactly one path, and each indexer gets its own layout rather than being forced
into the strip's shape.

> **No tray, no window list.** This strip used to be a window switcher with the
> system tray in its right-hand corner. **Both are saber's**, and duplicating
> them here was the overlap this project set out to close.
>
> `sofi -tray-daemon` still exists for sessions that do not run saber, but it
> now has no built-in surface — `doc/panel-window.sasi` ends with the widget
> block to paste into your own config if you want it back.
>
> The window switcher is retained as `sofi -show windowlist`. It draws in the
> general menu layout now rather than in this strip.

### Sheet switcher

```bash
sofi -show sheets
```

Renders as a horizontal row of ten chips centred under the compositor's top bar.
Each chip is a sheet number and its window count; empty sheets are dimmed and the
current sheet is filled. `kb-custom-1` sends the focused window to the
highlighted chip.

The row is a fixed ten-cell grid rather than a content-sized strip, so the chips
stay in the same place from one invocation to the next.

This mode speaks to hikari's control socket at `$XDG_RUNTIME_DIR/hikari.sock`
rather than to a Wayland protocol — **no standards-track protocol can express
send-to-sheet.** On any other compositor the mode reports that the socket is
absent and exits cleanly; it does not abort.

### Keys

```bash
sofi -show keys
```

Every keybinding sofi has, with what it does. **A near-square pane in the middle
of the screen, two columns wide** — deliberately unlike every other surface,
because this one is a reference you *read* rather than a list you pick from.
Nothing on it is actionable; Enter dismisses.

A tall narrow column suits a list you scan for one item. A reference wants as
much of itself visible at once as possible, which means width as well as height.
Two columns, not three: the rows carry a sentence of description each, and a
third column ellipsizes exactly the half of the row that explains what a binding
is for.

The filter searches binding names and descriptions, which is how you find the
one you half-remember.

### Display

```bash
sofi -show display
```

**A stub, and the reason is the compositor.** hikari-sakura creates
`wlr_xdg_output_manager_v1` — read-only geometry — and
`wlr_fractional_scale_manager_v1`. It does **not** create
`wlr_output_manager_v1`, which is the protocol that sets modes, scales and
positions. **No client on that compositor can change an output**, so a working
display mode is compositor work before it is sofi work.

**Nor can it list them on that compositor, and it is the same cause.** The mode
shells out to `wlr-randr` where that is installed — but `wlr-randr` speaks
`wlr-output-management-unstable-v1`, *the very protocol hikari-sakura does not
advertise*. So on hikari-sakura it fails and the pane lists nothing; on other
wlroots compositors it works, which is why it is still called.

The pane says which of three things happened — `wlr-randr` absent, `wlr-randr`
present but unable to read the outputs, or nothing connected — so "install
`wlr-randr`" is not the conclusion you draw on a compositor where it cannot
help.

**The route that would list outputs here is sofi's own Wayland backend**, which
already binds `wl_output` and `zxdg_output_manager_v1` and knows every output's
name, position and logical size — it is what `sofi -h` prints. Exposing that as
an enumerator both display backends implement would need no external tool at
all. That is not built yet.

Build without it with `-Ddisplay=false`.

### Volume

```bash
sofi -show volume
```

One row per audio sink: a level bar, the percentage, the sink's label, and
`muted` when it is. The sink the session is using is shown `ACTIVE`; a muted one
is shown `URGENT`, the same state the window switcher uses for a minimised window, so
a theme can style both with one rule.

**What the label is depends on the backend**, because the three do not name a
sink the same way:

| Backend | The row shows |
|---|---|
| `wpctl` | The node name from `wpctl status`, which is normally the device's description |
| `pactl` | The sink's `Description:`, not its `Name:` — "Built-in Audio Analog Stereo" rather than `alsa_output.pci-0000_00_1f.3.analog-stereo`. This is why the verbose `pactl list sinks` is parsed instead of `list short` |
| `mixer` | The mixer device name — `vol`, `pcm`, `speaker`, `line` or `headphone` |

| Key | What it does |
|---|---|
| `Enter` | Toggle mute on the highlighted sink. The menu stays open, because the change is invisible if the row that made it has gone |
| `kb-custom-1` | Make this sink the session default, and close |
| `←` (`kb-custom-2`) | Lower by 5% |
| `→` (`kb-custom-3`) | Raise by 5% |

**Sofi links no audio library.** The mode drives whichever control tool is
installed as a subprocess and picks between them at runtime, in this order:

| Backend | Tool | When it is chosen |
|---|---|---|
| WirePlumber / PipeWire | `wpctl` | Preferred where PipeWire is the sound server |
| PulseAudio | `pactl` | Answers on PulseAudio *and* on PipeWire's PulseAudio shim |
| FreeBSD base | `mixer` | Needs nothing installed; the only one that works with no sound server at all |

Being installed is not enough — a backend is only chosen once it has actually
reported a sink. That is what makes a FreeBSD box with the PulseAudio client
tools installed and no server running fall through to `mixer(8)` instead of
showing an empty list. The message bar names the backend that answered.

Two limits, stated rather than left to be discovered:

- **`mixer(8)` has no default-sink concept**, so `kb-custom-1` reports that and
  does nothing. It also cannot report *whether* a device is muted on either
  FreeBSD generation, so no row is ever marked muted under that backend —
  toggling still works on FreeBSD 14 and later, which is where `mixer` gained
  the verb.
- **The backend commands are synchronous.** A control tool that accepts a
  request and never answers will hold the menu until it does.

Build without it with `-Dvolume=false`.

### Bluetooth

```bash
sofi -show bluetooth
```

The whole bluetooth stack in one summoned pane: the adapter, every device that
is connected, paired or in range, and the maintenance verbs. Top-right corner,
520px wide, under the compositor's bar.

**This is the FreeBSD netgraph stack and only that.** `hccontrol(8)`,
`bthidcontrol(8)`, with `hcsecd(8)` holding PINs and link keys and `bthidd(8)`
driving input devices. There is no D-Bus bluetooth daemon in the FreeBSD base
system and **BlueZ is Linux-only, so sofi does not speak it**. Nothing is
linked; every tool is run as a subprocess.

| Key | Does |
|---|---|
| `Enter` on the adapter | Starts or stops the stack (`service bluetooth`) |
| `Enter` on a device | Connects it. An unpaired device is paired first — one key, not two |
| `Enter` on an action | Toggles discoverability, resets the controller, or restarts the stack |
| `Alt+1` | Inquiry. Roughly five seconds, and the only slow verb here |
| `Alt+2` | Disconnect, keeping the pairing |
| `Alt+3` | Forget — the hcsecd stanza, the stored link key and the bthidd entry |
| `Alt+4` | Set the device up for what it is |

**Opening the pane is instant.** It lists from the controller's neighbour cache,
the live connection list and the saved-device file — no scan runs until you
press `Alt+1`. That way opening it to disconnect a headset costs nothing.

**It works out what a device is from its class-of-device**, not from its name,
because names are chosen by manufacturers and mean nothing. That is what makes
`Alt+4` a single key: on a keyboard, mouse or gamepad it writes the `bthidd`
stanza and restarts the daemon; on a headset or microphone it hands the device
to the audio route.

#### Two things this stack will surprise you with

**Pairing an input device does not make it send input.** `bthidd` needs its own
entry in `/etc/bluetooth/bthidd.conf` before a single keystroke arrives.
sofi writes it for you on connect, and a device still missing it says
`needs input setup (Alt+4)` on its own row rather than silently doing nothing.

**Audio needs a port installed**, and it is not the one you would guess.

**Bluetooth audio does not come from PipeWire or PulseAudio.** Their backend is
`libspa-bluez5`, which needs BlueZ, so on FreeBSD neither carries a single
sample over bluetooth. The route that does work is `virtual_oss(8)` — a base
system daemon — plus **`audio/virtual_oss_bluetooth`**, which installs
`voss_bt.so`, a backend loaded dynamically when an invocation names a bluetooth
device.

`Alt+4` on a connected audio device starts it, following `virtual_oss(8)`'s own
bluetooth examples, and picks the argument list from the device class: a
headset, hands-free unit or microphone gets a duplex `-f` so its microphone
works, while headphones and speakers get playback-only. It runs in the
background and needs privilege, because `/dev/cuse` is `0600 root`.

Four states are reported rather than one, because each wants a different
response:

| Row says | Means |
|---|---|
| `audio: Alt+4` | Ready — press it |
| `needs virtual_oss_bluetooth` | `pkg install virtual_oss_bluetooth`. **The only one you fix by installing something** |
| `needs cuse(3)` | `kldload cuse`, or `cuse_load="YES"` in `loader.conf` |
| `no virtual_oss` | Not on `$PATH`; it is in the FreeBSD base system |

#### Privilege

Listing what is paired, pairing, forgetting and every controller write need
root: `/etc/bluetooth/hcsecd.conf` and `/var/db/hcsecd.keys` are both `0600
root`. sofi installs nothing setuid and uses **`network-privilege-command`** —
the same option the network mode uses, deliberately not a second one. It is
empty by default; set it to `doas` or `sudo -n` in `~/.config/sofi/config.sasi`.

Without it every read but the paired list still works — the adapter, the
connection list, the neighbour cache and discovery. The paired list is tried
unprivileged first too, but on stock permissions that fails, so the message bar
says `paired list needs network-privilege-command` rather than showing an empty
list you would read as "nothing is paired".

**Connect, disconnect, discoverability and reset are tried unprivileged before
they escalate**, because which HCI commands the raw socket gates is kernel
policy, not something sofi should assume — `read_stored_link_key` is refused
here while `read_scan_enable` is not. Where the kernel allows them they work
with no configuration. Starting and stopping the stack goes through `service`
and always needs the privilege command.

When sofi does rewrite `hcsecd.conf` it appends or splices out one whole
`device { }` stanza, leaves every other byte alone, never removes the mandatory
`00:00:00:00:00:00` default entry, and installs the result atomically with
`install(1)` rather than through a shell. A pairing that fails removes its own
entry again rather than leaving a device that claims to be paired.

Build without it with `-Dbluetooth=false`.

### Network

```bash
sofi -show network
```

Everything a session needs to do to its network, in one summoned pane: the
interfaces, the wireless networks in range, and the maintenance verbs.

| Row | `Enter` does |
|---|---|
| An interface | Brings it up, or takes it down — this is the ethernet enable/disable |
| A wireless network | Joins it, asking for a key only when the network is secured and nothing has one stored |
| **Turn Wi-Fi on / off** | Toggles the radio |
| **Renew DHCP lease** | |
| **Reconnect to router** | Re-associates with the access point |
| **Reset all network controllers** | Restarts every interface, and closes the pane — this drops the link it is running over |

`kb-custom-1` rescans; `kb-custom-2` disconnects; `kb-custom-3` forgets a saved
network. Interfaces that are down are
shown `URGENT`, and whatever is connected is shown `ACTIVE`. The message bar
carries the result of the last action, because every verb here changes system
state and most take a moment.

**Sofi links no network library.** Two backends, chosen at runtime by which one
answers:

| Backend | Tool | When it is chosen |
|---|---|---|
| NetworkManager | `nmcli` | Where NetworkManager is installed **and running** — it owns the interfaces there, so driving `ifconfig` behind its back would fight it |
| Base system | `ifconfig`, `wpa_cli`, `dhclient`, `service` | The FreeBSD-native path, and the one that needs nothing installed |

As with the volume mode, being installed is not enough: a backend is chosen only
once it has produced a row, so a machine with the NetworkManager client and a
stopped daemon falls through to the base system rather than showing nothing.

#### Privilege

**Most of these verbs need root**, and **sofi installs nothing setuid, writes no
`sudoers` rule and creates no group.** Tell it how this machine escalates:

```css
configuration {
    network-privilege-command: "doas";
}
```

`sudo -n` works too. It is empty by default because choosing how a machine
escalates privilege is an administrator's decision, not sofi's — with nothing
set, the privileged verbs run directly, fail as any unprivileged command does,
and say which option would fix it.

The `nmcli` backend never uses the prefix: NetworkManager escalates through
polkit on its own, and prefixing it would break the polkit session it needs.

#### Wireless keys

A key is asked for only when the network is secured *and* nothing has one
stored — retyping a working key is how a working key gets replaced with a typo.

The prompt is **another sofi**, run as a child with dmenu mode's `-password`, so
the key is masked by the same code that masks any other password sofi collects.
This pane hides itself first rather than have two layer surfaces compete for the
keyboard. sofi keeps no copy and wipes the buffer as soon as the call returns.

**A failed join leaves the machine exactly as it found it.** This matters more
than it sounds: `wpa_cli select_network` does not merely select, it *disables
every other configured network*, so a naive implementation of "join this one"
costs you the network you were already on the moment the key is wrong. So:

- Nothing is saved until the association has been **seen** to succeed. A key
  that turns out to be wrong is never written to `wpa_supplicant.conf`.
- A network added for the attempt is removed again when the attempt fails.
- Every network `select_network` disabled is re-enabled, on success and on
  failure alike, and the supplicant is told to re-associate so it returns to
  whatever it was on before.

**Where a key is saved**, when it works: the supplicant's own configuration —
the file `wpa_supplicant` was started with, which `ps` shows as its `-c`
argument, usually `/etc/wpa_supplicant.conf`. `kb-custom-3` removes a saved
network from both the running supplicant and that file, so the menu that saved
it can unsave it. If the file has no `update_config=1`, the supplicant refuses
to write at all — sofi says so, and the network is simply asked for again next
time.

Two limits, stated rather than left to be discovered:

- **Hidden networks cannot be joined from here.** A scan reports them with an
  empty SSID and there is nothing on this surface to join them by.
- **One radio.** The wireless verbs aim at the first wireless interface that is
  up; the message bar names which one that is.

Build without it with `-Dnetwork=false`.

### Notification daemon

```bash
sofi -notification-daemon
```

Owns `org.freedesktop.Notifications` on the session bus and renders the
notification stack in the bottom-right corner. Notifications are kept in a ring
buffer; `urgency=2` (critical) notifications never expire on their own. Browse
what has arrived with:

```bash
sofi -show notification-history
```

The daemon idles with no surface mapped and brings one back when a notification
arrives, so it costs nothing while the desktop is quiet.

Notifications look deliberately unlike the menus — separate cards with a leading
urgency stripe, rather than rows with a filled selection — so a banner is never
mistaken for something you are about to launch.

#### Clearing notifications

Two separate actions, because clearing banners off your screen should not also
lose the list of what you missed:

```bash
sofi -notification-clear           # dismiss what is on screen, keep history
sofi -notification-clear-history   # discard everything
```

Both are available inside the history panel as `kb-custom-1` / `kb-custom-2` and
as buttons. The live banner carries the dismiss button too — it takes no
keyboard, so a button is the only way to reach it.

Inside the history panel, **Dismiss is hidden when no daemon is running**: with
nothing on screen it has nothing to retire. Clear still works, because
discarding the stored history does not need a daemon.

Per entry, Enter tries three things in order:

1. **Run the notification's default action**, if it is still on screen and
   offered one. The application said what Enter should mean.
2. **Raise the window of the application that sent it.** This is what a history
   list is for that a banner is not — seeing something from an hour ago and
   wanting to go and deal with it — so it works on retired entries too. It needs
   the sender to have set the `desktop-entry` hint, and matching is strict: when
   nothing matches, nothing is raised, rather than the wrong window.
3. **Acknowledge it**, if it is still on screen, and leave the panel open —
   going through a list of missed notifications means going through it.

An entry that is already retired and has no window left simply closes the panel.
Shift+Delete retires one entry while keeping it in history.

### System tray

```bash
sofi -tray-daemon
```

Owns `org.kde.StatusNotifierWatcher` and collects the tray items applications
publish. **It has no surface of its own, and since 2026-09-09 it has no surface
at all**: the control panel that used to carry a tray zone no longer does,
because saber owns the persistent tray.

It is kept for sessions that do not run saber. To render its icons there, paste
the `tray` and `tray-icon` widget blocks from the foot of
`doc/panel-window.sasi` into your own configuration.

> **This is the one either/or with saber. Run one tray host, not two.**
>
> `org.kde.StatusNotifierWatcher` is a well-known D-Bus name and exactly one
> process per session bus can own it. `saber` hosts a tray too. Whichever starts
> second finds the name taken, says so, and leaves its tray zone empty rather
> than fighting for it.
>
> **It does not fix itself when you stop the loser.** A tray application asks
> whether a host exists once, at its own startup, and one that found none never
> asks again — so after changing which host runs, restart the applications whose
> icons you want.
>
> **If you run saber, that is the tray you want** — it is on screen permanently,
> and sofi's no longer has anywhere to draw. Leave `sofi -tray-daemon` out of
> `~/.config/hikari/autostart`:
>
> ```sh
> pipewire &
> sofi -notification-daemon &
> saber &
> ```
>
> **If you do not run saber, sofi's tray is the one to use** — that is what it
> is for, it is not deprecated, and it is built by default. To leave it out of
> the binary entirely, configure with `-Dtray=false`.
>
> The same either/or applies to `com.canonical.Unity`, which saber owns and sofi
> does not claim at all.

Four things worth knowing:

- **Start it before the applications whose icons you want.** A tray application
  asks once, at its own startup, whether a host exists. One that finds none shows
  no icon and never asks again — so a host started later means restarting those
  applications.
- **Restart it after upgrading sofi.** `org.sofi.Tray` is private between two
  sofi processes and changes with the code; an older daemon serves a shape the
  new strip cannot read, and you get an empty tray zone plus a warning saying
  so. The applications themselves do not need restarting — they watch for the
  watcher and re-register.
- **It needs no display.** The protocol is D-Bus only, so it runs with no Wayland
  or X11 session at all.
- **It is a separate process from the notification daemon**, deliberately. The
  two share no state, and a fault in one should not take the other with it.

**This needs a restored tray zone.** The control panel has shipped without a
`tray` widget since 2026-09-09, so on a default configuration there are no icons
to click until you paste the `tray` and `tray-icon` blocks from the foot of
`doc/panel-window.sasi` into your own configuration.

With those in place, **clicking an icon opens that application's menu, in the
strip**; Escape or choosing an entry closes it. Submenus open in place with a
`..` row to go back, the way the file browser descends into directories. It does
not replace a window list — the strip no longer carries one, and the switcher is
`-show windowlist` on its own surface.

| Button | What it does | Binding |
|---|---|---|
| Left | The item's menu, or `Activate` when it published none | `mt-activate` |
| Right | The same menu, or the item's own `ContextMenu` when it published none | `mt-context-menu` |
| Middle | `SecondaryActivate` | `mt-secondary-activate` |

**Why sofi draws the menu rather than the application.** Under
StatusNotifierItem an application publishes a *description* of its menu over
`com.canonical.dbusmenu` — labels, separators, toggles, which rows open
submenus — and there is no method in that protocol that asks it to display
anything. Rendering is the host's job. That is the deliberate break from the old
X11 tray, where an application embedded a window and drew its own menu; moving
the menu out of the application's process is what lets the panel theme it. So
there is no "native menu" to show: for most tray applications the menu exists
only as data until something draws it.

### Autostart

Sofi has two long-running services, neither of which belongs on a key. Put them
in `~/.config/hikari/autostart`.

**Running saber** — the tray comes from saber, so sofi contributes the
notification daemon only:

```sh
sofi -notification-daemon &
saber &
```

**Not running saber** — sofi hosts the tray as well:

```sh
sofi -notification-daemon &
sofi -tray-daemon &
```

Starting both tray hosts is the one configuration that does not work; see
[System tray](#system-tray).

### Triggering sofi from saber

**There is no sofi IPC socket, and none is needed.** Every surface is one
invocation of the binary, so a saber button, a `hikari.conf` keybinding and a
shell script all reach sofi the same way — and each surface holds its own
instance lock, so pressing the same trigger twice does not stack two copies.

| Surface | Command | Instance lock |
|---|---|---|
| Application menu | `sofi -show drun` | `menu` |
| Control panel | `sofi -show window` | `window` |
| Sheet switcher | `sofi -show sheets` | `sheets` |
| Volume | `sofi -show volume` | `volume` |
| Network | `sofi -show network` | `network` |
| Notification history | `sofi -show notification-history` | `notification-history` |
| Message toast | `sofi -e <message>` | `notify` |
| Notification daemon | `sofi -notification-daemon` | its bus name |
| System tray host | `sofi -tray-daemon` | its bus name |

Each exits non-zero when it cannot do its job — no compositor socket for
`sheets`, no audio backend for `volume` — so it composes in a script.

**Do not use `org.sofi.Tray` for this.** It is a private interface between two
sofi processes, its signature changes with the build, and it is not a supported
handoff surface for anything else. The commands above are the contract.

`org.sofi.Notifications` on the session bus **is** stable and public, for the
one case a command cannot cover — acting on notifications without opening a
panel:

| Method | Does |
|---|---|
| `DismissAll` | Retire every banner on screen, keep the history |
| `ClearHistory` | Discard the stored history |
| `Dismiss(u id)` | Retire one banner |
| `InvokeAction(u id, u index)` | Trigger a notification's own action |
| `GetLive() → a(uus)` | What is on screen right now |

## Theming

Sofi has no theme file to install and no theme to pick. It compiles in a palette
and nine layouts, and your config file *edits* them rather than replacing them.

### The palette

One file defines every colour, in two layers. Sixteen positional slots in the
conventional terminal arrangement:

```css
color0  #2b1e3a   color8  #5e5966
color1  #c96464   color9  #df8787
color2  #df9f87   color10 #f2bda8
color3  #e4b382   color11 #f5cf9e
color4  #8e7cc3   color12 #aba0d9
color5  #b18fc7   color13 #cfaedc
color6  #9fa0a6   color14 #b8b9be
color7  #d4d4d9   color15 #f0edf2
```

…and semantic names that reference them, which is what the layouts actually use:

| Name | Slot | Used for |
|---|---|---|
| `background` | color0 @ 90% | Panel grounds |
| `surface` | derived | Inset fields, scrollbar troughs |
| `foreground` | color7 | Body text |
| `foreground-dim` | color6 | App names, timestamps, counts |
| `accent` | color12 | Selection |
| `accent-soft` | color4 | Prompts and leading stripes |
| `accent-strong` | color13 | Current sheet, live notification |
| `on-accent` | color0 | Text on an accent fill |
| `urgent` / `critical` | color9 / color1 | Critical, as text / as a fill |
| `muted` | color8 | Separators and troughs — **never text** |

These sixteen values are the same ones hikari-sakura takes in its `ui { palette }`
block, and the semantic names map onto its `ui { colorscheme }` slot for slot —
`accent` is the compositor's `selected`, `background` is its `bar` exactly. One
scheme dresses the compositor, its bar and the shell together, which also means
a terminal colorscheme can be dropped into both.

The application icon is drawn from these same slots: a `color0` ground, petals
running `color4` → `color13`, a `color11` centre.

Every text-on-fill pair is checked against WCAG AA. Three tones are constrained
by that and not by taste: `muted` cannot carry text at 2.29:1, `critical` is for
fills rather than text at 4.07:1, and `on-accent` is dark rather than white
because white on `color12` is 2.40:1.

### Recolour everything

```css
/* ~/.config/sofi/config.sasi */
* {
    color0:  #1c1b22;
    color12: #89b4fa;
}
```

Every surface follows, because every semantic name is a reference to a slot.
Supply as many or as few as you like.

### Recolour one role

```css
* {
    accent: #f5c2e7;   /* selections only */
}
```

### Move or resize a panel

```css
window {
    location: west;
    anchor:   west;
    width:    340px;
}
```

### Why this works

Sources are parsed in order — default configuration, palette, the one layout for
the surface you asked for, your `config.sasi`, then `-theme` — and later sources
override earlier ones property by property. Colour references are resolved after
all of them have been read, so redefining a name in your own file reaches every
layout that uses it.

Full instructions, including per-surface theming and the offset-sign trap, are in
[sofi-customisation(5)](doc/sofi-customisation.5.markdown).

## Features

Grouped by the index each one serves. Exhaustive detail is in
[FEATURES.md](FEATURES.md).

**Indexing and matching**

- Type to filter, tokenized — any word in any order
- Fuzzy, regex, prefix, glob and normal matching
- Case-insensitive, togglable, or SmartCase
- Levenshtein or fzf-style sorting of matches
- History-based ordering: the last 25 choices float to the top
- UTF-8 throughout, with UTF-8-aware collation
- International keyboard support (`` `e `` → è) and RTL languages

**System surfaces** *(hikari-sakura)*

- Application menu, task and window manager, sheet switcher
- Notification daemon with a persistent history ring
- StatusNotifierItem system tray host, with `com.canonical.dbusmenu` menus
  rendered by sofi
- Per-surface instance locks, so all of them coexist
- Layouts compiled in — every surface works with no configuration file

**Presentation**

- Cairo drawing, Pango font rendering
- One sixteen-slot palette shared with the compositor
- Full theme engine with per-surface overrides
- Fully configurable keyboard and mouse navigation

**Extension**

- Script modes — write a mode as a shell script
- Plugin ABI for out-of-tree modes
- dmenu-compatible mode for scripting
- Combi mode, merging several indexes into one list

## Modes

Each mode is one index. Which are available depends on the backend and on build
options — run `sofi -h` to see what your binary offers.

| Mode | Indexes | Requires |
|---|---|---|
| `drun` | Applications, from XDG desktop files | `-Ddrun` |
| `run` | Executables on `$PATH` | — |
| `window` | The control panel — a button per sofi indexer | — |
| `windowlist` | Windows (X11/EWMH, or Wayland toplevels with minimise/maximise verbs) | `-Dwindow` |
| `windowcd` | Windows on the current desktop | `-Dwindow`, xcb |
| `sheets` | hikari-sakura sheets 0–9 | `-Dsheets`, hikari socket |
| `volume` | Audio sinks, level and mute | `-Dvolume`, one of `wpctl`/`pactl`/`mixer` |
| `bluetooth` | Adapter and devices, FreeBSD netgraph | `-Dbluetooth`, `hccontrol` (base system) |
| `display` | Outputs, read-only *(stub)* | `-Ddisplay` |
| `network` | Interfaces and wireless networks | `-Dnetwork`, `nmcli` or `ifconfig`+`wpa_cli` |
| `notifications` | The live notification stack | `-Dnotify` |
| `notification-history` | Notifications already shown | `-Dnotify` |
| `tray-menu` | One tray item's dbusmenu tree | `-Dtray` |
| `ssh` | Hosts from your SSH config and known-hosts | — |
| `filebrowser` | Files in one directory | — |
| `recursivebrowser` | Files, descending | — |
| `combi` | Several of the above, merged | — |
| `keys` | Sofi's own keybindings | — |
| `script` | Whatever your script prints | — |
| `dmenu` | Whatever you pipe in (`-dmenu`, not `-show`) | — |

**Sofi is known to work on FreeBSD and Linux.**

## Wayland support

### Build

Please follow the [build instructions](INSTALL.md) to build sofi. Wayland
support is enabled by default, along with X11/xcb.

Sofi can also be built *without* X11/xcb or Wayland, but at least one backend
must be enabled:

    meson build -Dxcb=disabled
    meson build -Dwayland=disabled

### Usage

Sofi selects the xcb or Wayland backend automatically from the environment. To
force xcb, if it was enabled at build time:

    sofi -x11 ...

### Missing features in Wayland mode

A few options are difficult or impossible to implement under Wayland's
architecture and available APIs:

- `-normal-window`. Not impossible, but it would require real work, and it is a
  toy feature for a program that renders as a layer surface.
- `-monitor -n` for fine-grained selection of monitor to display sofi on. A
  Wayland client is not told where the pointer is or which monitor has focus,
  so the position specifiers cannot be implemented. **Selecting a monitor by
  name works under layer-shell** (`-monitor DP-3`, as listed by `sofi -h`),
  where the output is named at surface creation and the surface is pinned to
  it; under the `xdg-shell` fallback a named output only seeds the window's
  initial size and the compositor still chooses the screen. With no name,
  placement is the compositor's decision, which is what the layer-shell
  protocol specifies.
- `@media (monitor-id: n)` in a theme, for the same reason — the monitor a
  surface landed on is not known until after the theme is resolved. The size
  and aspect constraints still work **under layer-shell**, where the monitor's
  dimensions are known by the time the theme resolves; under the `xdg-shell`
  fallback nothing reports the output's size and the compositor has not yet
  identified which output will host the surface, so those queries are ignored
  too. See **sofi-theme(5)**.
- some window locations parameters work partially, `x-offset` and `y-offset` are only working from screen edges
- fake transparency
- window mode on KWin which implements different protocols than the wlr family

### Shell protocols on Wayland

The Wayland backend prefers `zwlr_layer_shell_v1`, which lets it position and size
itself precisely. It binds version 4, because the `on-demand` keyboard
interactivity the notification daemon needs is unreachable below it and
wlroots silently degrades the request rather than erroring. Compositors that do
not implement layer-shell — notably Mutter (GNOME) and KWin (Plasma) — fall back
to `xdg-shell`, where the surface is an ordinary toplevel window and
**placement is the compositor's decision**. In that mode:

- `location`, `anchor`, `x-offset` and `y-offset` have no effect; the compositor
  places the window
- keyboard interactivity cannot be forced, so focus follows normal window rules
  rather than being grabbed
- `click-to-exit` cannot capture clicks outside the window
- the `wayland-layer` option (`overlay` / `top` / `bottom` / `background`) is ignored
- the monitor's size is unavailable, so theme `@media` size and aspect-ratio
  queries are ignored with a warning, as `monitor-id` already is on Wayland —
  no configure reports the output's dimensions and the compositor, not sofi,
  decides which output the window lands on

The panel surfaces depend on layer-shell for their placement, so under
`xdg-shell` they degrade to ordinary windows.

The backend logs which shell it selected at debug level. Run with `-log-level debug`
to confirm. If neither protocol is available, the backend reports the failure and
exits rather than aborting.

### Wayland DPI

On Wayland the output is only known after the first surface is shown, which makes
sizing in absolute units (mm) difficult — a problem unique to a layer-shell
client. Work around it by passing the right DPI through the configuration
system. If `dpi` is `0` and one monitor is connected, sofi uses that monitor's
DPI; with several monitors, name one and sofi uses its DPI.

## Documentation

| Document | Covers |
|---|---|
| **[FEATURES.md](FEATURES.md)** | **Reference by capability** — every surface, mode, verb, keybinding, daemon and interface |
| [CONFIG.md](CONFIG.md) | Configuration, task-first: recipes for the thing you want to change |
| [INSTALL.md](INSTALL.md) | Dependencies and building |
| [sofi(1)](doc/sofi.1.markdown) | **Reference by flag** — every command-line and configuration option |
| [sofi-customisation(5)](doc/sofi-customisation.5.markdown) | Theming the surfaces: palette, per-surface overrides, offsets |
| [sofi-theme(5)](doc/sofi-theme.5.markdown) | The `.sasi` theme format in full |
| [sofi-keys(5)](doc/sofi-keys.5.markdown) | Every keybinding and mouse binding |
| [sofi-script(5)](doc/sofi-script.5.markdown) | Writing a mode as a script |
| [sofi-dmenu(5)](doc/sofi-dmenu.5.markdown) | dmenu-compatible mode |
| [sofi-actions(5)](doc/sofi-actions.5.markdown) | Custom actions |
| [sofi-thumbnails(5)](doc/sofi-thumbnails.5.markdown) | Thumbnailing |
| [sofi-debugging(5)](doc/sofi-debugging.5.markdown) | Log levels, timings, bug reports |
| [sofi-theme-selector(1)](doc/sofi-theme-selector.1.markdown) | The theme-selector helper |
| [sofi-sensible-terminal(1)](doc/sofi-sensible-terminal.1.markdown) | The terminal-picking helper |

The manpages are the most closely maintained reference. If something here and a
manpage disagree, the manpage is right — please
[file it](https://github.com/orpheus497/sofi/issues) either way.

## Installation

See the [installation guide](INSTALL.md). Sofi is not yet packaged by any
distribution; build from source.

## Quickstart

### Running sofi

Launch a mode directly with `sofi -show <mode>`:

```bash
sofi -show run
```

Get the options from a script instead:

```bash
~/my_script.sh | sofi -dmenu
```

Restrict which modes are available — they can still be switched at runtime with
`Ctrl+Tab`. With none specified, all configured modes are enabled:

```bash
sofi -modes "run,ssh" -show run
```

Merge several modes into one list with `combi`:

```bash
sofi -show combi -combi-modes "drun,run,ssh" -modes combi
```

### Configuration

Every surface works with no configuration file at all. If you want to change
something, generate one:

```bash
mkdir -p ~/.config/sofi
sofi -dump-config > ~/.config/sofi/config.sasi
```

`config.sasi` in `~/.config/sofi/` is the file sofi looks for by default. See
[CONFIG.md](CONFIG.md) for a task-first guide, and the manpages for the full
option set.

### Themes

See [Theming](#theming) above, and
[sofi-customisation(5)](doc/sofi-customisation.5.markdown) for the full guide.

Sofi needs no theme installed to work. For anyone who would rather edit a whole
file than write overrides, the application-menu layout is installed as an
ordinary theme:

```bash
mkdir -p ~/.config/sofi
cp /usr/local/share/sofi/themes/config.sasi ~/.config/sofi/config.sasi
cp /usr/local/share/sofi/themes/colors-default.sasinc ~/.config/sofi/
```

`colors-default.sasinc` is the palette and `config.sasi` is the layout that
imports it. Note what copying it actually does: a config file cannot know which
surface it was loaded for, and `~/.config/sofi/config.sasi` is parsed *after*
whichever panel layout the invocation selected. Its `window`, `mainbox`,
`listview` and `element` rules therefore land on every surface — the control panel
and the sheet row pick up the menu's geometry too. Treat it as a starting point
to edit, not as a drop-in that leaves the other surfaces alone.

To change one surface and no other, override on that invocation instead:

```bash
sofi -show drun -theme-str 'window { width: 640px; }'
```

`-theme-str` merges over the layout that was loaded; `-theme` replaces the whole
theme, palette included, so a file passed that way must stand on its own.
