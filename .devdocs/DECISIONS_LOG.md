# DECISIONS LOG

Reverse-chronological. Most recent entries at the top.

---

## 2026-08-24 10:20 — sofi becomes the hikari-sakura shell. R16–R23 ruled, Q17 closed.

The USER directed that sofi provide **four system surfaces natively** — application menu on the
left, task/window manager along the bottom, sheet switcher on the right, and a notification
system — and that these be *"the config-less native structure and build of this program as it is
a fork made specifically for this current active hikari-sakura window compositor."*

That is a change of project identity, not a feature request. sofi stops being a rofi/dmenu
drop-in that happens to run on hikari, and becomes hikari-sakura's shell. The rulings below
follow from that.

### Corrections to earlier analysis in this session

Two claims I made were wrong and were retracted before any work was built on them.

**1. `sofi -show window` was reported as non-functional. It was not.** I claimed the installed
hikari lacked `zwlr_foreign_toplevel_manager_v1` because `strings hikari` returned zero hits and
the binary predated the implementing commit. Both legs were bad: **wlroots is dynamically
linked**, so the protocol's name string lives in `libwlroots.so` and can never appear in the
compositor binary; and building from a working tree before committing it is the normal order,
not evidence of staleness. `nm -u` shows the symbols, and a `wl_registry` dump from the running
compositor shows `zwlr_foreign_toplevel_manager_v1 v3` advertised — exactly the version sofi
caps at.

**Method ruling that follows from this: capability claims about the compositor are settled by a
registry dump, not by inference from binaries or timestamps.** A 20-line `wl_registry` listener
was compiled for this purpose and should be reached for again rather than re-derived.

**2. The sheet switcher was reported as fully blocked. It was half-blocked.** `display_sheet()`
(hikari `src/workspace.c:160-192`) hides every view not on the target sheet, and
`hikari_view_hide()` publishes that through foreign-toplevel's minimised bit — hikari's own
comment states it outright: *"hikari's `hidden` flag IS minimised as far as foreign-toplevel
clients are concerned."* So "on the displayed sheet" versus "elsewhere" already reaches sofi on
a live protocol. sofi parsed the bit into `TOPLEVEL_STATE_MINIMIZED` and never read it.

### Rulings

- **R16 — Four compiled-in panel layouts, selected by mode.** `default_configuration.sasi` no
  longer pins a theme. `sofi_surface_name()` derives the surface from the invocation and that one
  answer drives both the built-in layout and the instance lock. Parsed after the default
  configuration and before every user source, so `~/.config/sofi` and `-theme` still override.
  **No config file is required for any of the four surfaces.**

- **R17 — Instance locks are per surface, not per process.** A single session-wide pidfile made
  the panels mutually exclusive; the failure was a `g_warning` on a stderr nobody reads. Locks are
  now `sofi-<surface>.pid`. Verified: launcher and task strip render simultaneously with no flags.

- **R18 — Bind `zwlr_layer_shell_v1` at version 4, and expose keyboard interactivity.**
  `ON_DEMAND` arrived in v4; below it wlroots coerces the argument to `!!interactive`, so
  requesting it on a v1 binding silently means EXCLUSIVE. `wl_registry_bind` still takes
  `MIN(advertised, 4)`, so a v1-only compositor is unaffected. New option
  `-wayland-keyboard-interactivity none|exclusive|on-demand`, defaulting to `exclusive`.

- **R19 — Sheet control travels over a hikari control socket, not a Wayland protocol.**
  Chosen by the USER from three options. `ext_workspace_v1` was rejected because it is larger on
  both sides *and still cannot express send-to-sheet*; virtual-keyboard synthesis was rejected
  because it cannot read state and breaks on any rebinding. See the Q17 closure below.

- **R20 — The notification daemon is built in sofi, not in the compositor.** Ruled by the USER
  and confirmed by analysis. The decisive argument is hikari's own, already recorded in
  `src/topbar.c` about its own bar: *"This deliberately remains a SEPARATE PROCESS […] running
  them inside the compositor would stall the Wayland event loop on every tick."* Notifications are
  a harder case than telemetry — arbitrary applications, on their schedule, with a raw pixel array
  whose dimensions the sender chooses. A crash there would take the session, and hikari has no
  crash recovery.

  **The one argument that could have forced compositor-side does not survive the code.**
  Notifications painting over a locked screen would be a real security problem a client cannot fix
  itself — but hikari `src/lock_mode.c:988-992` disables the `bottom`, `views`, `top` and
  `overlay` scene nodes on lock. A sofi notification is on `overlay`, so it is hidden for the
  duration with no work on either side.

- **R21 — Notification history is a ring buffer from day one.** The USER wants history.
  Retrofitting it onto a transient queue would mean rewriting every accessor; a fixed ring with a
  `live` flag costs almost nothing now. Live entries feed the banner, the full ring feeds a
  separate `notification-history` mode.

- **R22 — Notifications render bottom-right.** hikari's own bar owns the top edge and the sheet
  pane owns the right edge, making top-right the busiest corner on screen. The stack grows upward
  from above the task strip.

- **R23 — `urgency=2` never expires.** Critical notifications ignore both `expire_timeout` and the
  server default, persisting until dismissed or closed by the sender. Consequence to design for:
  this is the one case where the banner surface is up indefinitely, which is why R24's forced
  settings are load-bearing rather than a nicety.

- **R24 — The daemon's two safety settings are forced in code, not read from a theme.**
  `click-to-exit: false` and `keyboard-interactivity: on-demand` are overridden after theme parsing
  in daemon mode. Both, wrong, make the desktop unusable for the entire session, and a user editing
  their own theme must not be able to cause that.

### Q17 — CLOSED: send-to-sheet is expressible after all

Q17 asked how send-to-workspace should be expressed given that no standards-track protocol
carries it. **R19's control socket answers it.** `pin <n>` moves the focused view to a sheet, and
it is verified working: with one window on sheet 5, `pin 8` moved the count from index 5 to index
8, and a subsequent `sheet 8` switched to it.

This is the strongest argument for the socket over `ext_workspace_v1`, whose `assign` moves a
workspace to an output group rather than a window to a workspace.

### Q16 — still open

Deleting the ext-foreign-toplevel binding and `window-command`'s `{window}` support remains
unruled. Nothing this session depended on it. Note it has become slightly *less* attractive to
delete: the ext list is the only source of a stable per-window identifier, which a notification
daemon correlating windows to notifications might eventually want.

### Defects found and fixed this session

| Defect | Location | Consequence |
|---|---|---|
| `-e` never armed the auto-dismiss timer | `view.c` — `sofi_view_error_dialog` was not among the two callers of `sofi_view_set_user_timeout` | Message surfaces never self-dismissed. `timeout { delay: N; }` appeared to do nothing |
| Layer-shell bound at v1 | `include/wayland-internal.h:157` | Four versions of protocol left unused; `ON_DEMAND` unreachable |
| Keyboard interactivity hardcoded | `wayland/display.c` | Every surface took the keyboard exclusively, with no way to opt out |
| Wayland-inappropriate `window-format` default | `config/config.c:152` | `{w}` is X11-only; four dead leading spaces per row, and `{c}` alone renders sibling windows identically |
| Minimised bit parsed and discarded | `modes/wayland-window.c` | Sheet visibility was arriving and being thrown away |

### Defect found and NOT fixed — belongs to the compositor

**`click-to-exit` grows the layer surface to cover the whole output** (`wayland/view.c:262`) so it
can catch clicks outside the menu. Correct for a menu. For a resident daemon it is an invisible
full-screen input trap. This is not a bug — it is the mechanism working as designed — but it is
the single most dangerous interaction in the notification work, and it is why R24 exists.

Separately, two pre-existing hikari-sakura issues were hit and are recorded for the USER rather
than fixed silently:

1. **hikari does not build at HEAD with its default flags.** `action.o` was stale from a
   pre-`NDEBUG` build and `main.o` was root-owned from an earlier `sudo make`. The Makefile has no
   header dependency tracking, so this recurs after any header edit. `make clean` is mandatory.
2. **`hikari_server_stop()` appears not to run on SIGTERM.** The control socket survived a
   SIGTERM that should have unlinked it, which suggests `wl_event_loop_add_signal` is not catching
   the signal on FreeBSD and the whole graceful-shutdown chain is skipped. Unconfirmed. Harmless
   for the socket because startup unlinks a stale node, but it affects every other teardown step.

---

## 2026-08-22 21:35 — Target platform named: hikari-sakura. Phase 7 rescoped.

The USER supplied a compositor-side analysis of `hikari-sakura`
(`/home/orpheus497/Projects/hikari-sakura`), their custom wlroots compositor, and confirmed
**sofi's target is specifically that compositor.** This materially changes Phase 7.

### Corrections to my earlier assessment

**I was right that control is missing, wrong about the significance of one detail.** The
compositor brief warns that once `zwlr_foreign_toplevel_management_v1` is advertised
alongside `ext-foreign-toplevel-list-v1`, a client binding both globals sees every window
twice, and advises "sofi should bind zwlr only."

**Verified: sofi is already immune.** It displays from `pd->wlr_toplevels` only
(`source/modes/wayland-window.c:574`). The ext list is a *separate* list used for exactly one
purpose — harvesting the `identifier` string to fill `{window}` in `window-command`
(`:592-598`). No double-listing exists. No change required on that account.

### The real sofi-side finding underneath it

That ext correlation is the fragile app_id-ordering heuristic the code itself warns about at
`source/modes/wayland-window.c:581-588`, and which `AUDIT_REGISTER.md` flagged. Its **only**
consumer is `window-command`, whose default value is `wmctrl -i -R {window}` —
X11-only, and therefore already non-functional on hikari.

So roughly 90 lines exist to serve a feature that does not work on the target platform, at
the cost of a documented-fragile heuristic. **Recommendation: delete the ext binding and the
correlation.** Not done — it is a user-visible feature removal and needs a ruling (Q16).

### Send-to-workspace is not expressible — verified against the protocol XML

R15 defined the task manager as close / minimise / maximise / **send-to-workspace**. That
last one cannot be built on the protocols in question:

- `zwlr-foreign-toplevel-management-unstable-v1.xml` — **zero** occurrences of "workspace".
- `ext-workspace-v1.xml` request set is `commit`, `stop`, `create_workspace`, `destroy`,
  `activate`, `deactivate`, `assign`, `remove`. **`assign` moves a workspace to an output
  group, not a window to a workspace.**

No standards-track protocol expresses it. It needs a hikari-specific protocol or a CLI
escape hatch — and that decision should be made *before* compositor Part B is built, since it
may change what Part B needs to expose. Raised as **Q17**.

### What sofi actually needs, per feature

| Feature | Compositor | sofi work |
|---|---|---|
| Window switcher | Part A (done, unrun) | **none** — sofi already sends `activate` + `close`, both v1 requests, and binds `MIN(version, 3)` so it degrades gracefully |
| Task manager (R15) | Part A | **real work** — sofi sends *only* `activate` and `close`; `set_minimized` / `set_maximized` / `set_fullscreen` appear nowhere in the source. They must be added as UI actions |
| Workspace switcher | Part B (not started) | **a new mode from nothing**, ~300 lines |
| Send-to-workspace | **no protocol exists** | blocked on Q17 |

### Consequence of the compositor's fullscreen mapping

hikari has no fullscreen state, only `HIKARI_MAXIMIZATION_*`, so `set_fullscreen` and
`set_maximized` are the same operation and a client sees back a state it did not request.
**sofi should expose maximise only** — two actions doing one thing, with confusing state
echo, is worse than one. Recorded so the task-manager UI is not designed around a distinction
that does not exist on the target.

### Open questions

- **Q16** — delete the ext-foreign-toplevel binding and the `window-command` `{window}`
  identifier support? Recommended yes; it is a feature removal.
- **Q17** — how should send-to-workspace be expressed: hikari-specific protocol, or a CLI
  escape hatch through `window-command`?

---

## 2026-08-22 19:55 — RULING R15: "task manager" means task/window manager

**Ruled.** Reading (2) of Q15: a richer window mode with close / minimise / maximise /
send-to-workspace actions — **not** a process manager.

Consequences:
- No new platform-specific data source. No `kvm_getprocs`, no `/proc`, no privilege story
  for signalling processes. The tree's clean portability record (zero `/proc` and zero
  `__FreeBSD__` dependencies outside the new `SHM_ANON` guard) is preserved.
- Phase 7c collapses into an extension of 7a (window mode) and 7b (workspace mode) rather
  than a third independent mode. It should be sequenced *after* both.
- On Wayland the actions map to `zwlr_foreign_toplevel_handle_v1` requests
  (`activate`, `close`, `set_maximized`, `set_minimized`, `set_fullscreen`), most of which
  the existing `source/modes/wayland-window.c` already binds but does not expose. On X11
  they map to EWMH client messages.
- Send-to-workspace depends on 7b landing first, on both backends.

**All 15 questions are now ruled. No decisions outstanding.**

---

## 2026-08-22 19:46 — New scope from USER: default config + three modes

The USER added `sofi-config/` (`config.rasi`, `colors-default.rasi`) as the standard,
deployed default config and theme, and asked that enhancements for a task manager, window
switcher and workspace switcher be captured in planning rather than built now.

Recorded as `PLANS.md` **Phase 6** (ship the default config) and **Phase 7** (the three
modes). Backlog items B8 and B9 in `TODOS.md`.

**Validation performed immediately:** the config parses against the real binary with zero
warnings — `rofi -config sofi-config/config.rasi -dump-theme` exits 0, stderr empty, the
`@import` resolves and every property lands.

### Two findings that affect existing decisions

**R3 has an exception this file creates.** `sofi-config/config.rasi:15` is
`@import "colors-default.rasi"` — the extension is written out explicitly. The shipped
gruvbox themes use extension-less `@import "gruvbox-common"`, which resolves through
`rasi_theme_file_extensions[]`, which is why R3 was assessed as needing no importer edits.
That assessment holds for `themes/` but **not** for `sofi-config/`: this import line must be
edited by hand during Phase 3e or the default theme breaks. Added to the Phase 3e checklist.

**`modi:` is deprecated but live.** `sofi-config/config.rasi:7` uses `modi:`. It still works
— `source/xrmoptions.c:74-86` maps `switchers`, `modi` and `modes` to the same
`config.modes` — but the shipped default should use `modes:`. Not a bug, a freshness issue.

### Q15 — OPEN: what does "task manager" mean?

The term admits two readings with very different designs:

1. **Process manager** (enumerate processes, CPU/memory, send signals). Requires a new
   platform-specific data source — `kvm_getprocs` on FreeBSD, `/proc` on Linux. Note the
   audit established the tree currently has **zero** `/proc` or `__FreeBSD__` dependencies;
   this would introduce the first significant platform split, cutting against the project's
   otherwise clean portability record.
2. **Task/window manager** (window mode plus close / minimise / maximise / send-to-workspace
   actions). Mostly an extension of the window and workspace modes, no new data source.

Reading (2) as the likely intent, since it was named alongside a window switcher and a
workspace switcher. **Not assumed — must be confirmed before design work begins.**

### Feasibility confirmed while scoping

`ext-workspace-v1` is present on this host (wayland-protocols 1.49,
`/usr/local/share/wayland-protocols/staging/ext-workspace/`), so a workspace switcher is
viable on Wayland as well as X11. It is not yet in the `meson.build:317-327` protocol list.
On X11 the EWMH groundwork is already partly in use at `source/modes/window.c:559,796`.

---

## 2026-08-22 19:15 — RULINGS: Q3, Q5, Q6, Q9, Q11, Q12 decided by USER

All remaining Phase 3 gates are now closed. Phase 3 is unblocked.

### R3 (was Q3) — Rename the theme extension to `.sasi` / `.sasinc`

**Ruled.** Full consistency with the hard-fork stance: the format extension is rebranded too.

Scope:
- `lexer/theme-lexer.l:56` — `{".rasi", ".rasinc", NULL}` → `{".sasi", ".sasinc", NULL}`
- 35 shipped theme files renamed (`themes/*.rasi`, `themes/gruvbox-common.rasinc`)
- `meson.build:378-415` install list
- `doc/default_theme.rasi`, `doc/default_configuration.rasi`
- `config.rasi` → `config.sasi` (`source/rofi.c:1059`, `:1143-1145`)
- `rofi.rasi` → `sofi.sasi` (`source/rofi.c:1120`, `:1132`)
- Docs: `doc/rofi-theme.5.markdown`, `CONFIG.md`, `README.md`, the theme-selector script

**Useful finding:** the six gruvbox variants use extension-less `@import "gruvbox-common"`
(`themes/gruvbox-*.rasi:61`), which resolves through the extension array. Renaming
`gruvbox-common.rasinc` → `.sasinc` therefore needs **no** edit to the importers.

Consequence accepted: every third-party rofi theme in existence requires a file rename before
it can be used with sofi. This is loud (file not found), not silent, which makes it more
tolerable than R2.

### R5 (was Q5) — Compositor identity renamed

**Ruled.** Wayland layer-surface namespace `"rofi"` → `"sofi"`
(`source/wayland/display.c:1792`). X11 WM_CLASS `"rofi\0Rofi"` → `"sofi\0Sofi"`
(`source/xcb/view.c:773-775`). Must match `StartupWMClass` in the renamed desktop file.

Consequence: users with compositor/WM rules keyed on `rofi` must update them. Loud, and
belongs in the release notes.

### R6 (was Q6) — Helper scripts renamed, no compat symlinks

**Ruled.** `rofi-sensible-terminal` → `sofi-sensible-terminal`, `rofi-theme-selector` →
`sofi-theme-selector`. No compatibility symlinks.

**Hard constraint:** `config/config.c:65` (the compiled-in default terminal) must change in
the *same commit* as the script rename, or `sofi -show run` cannot open a terminal at all.

### R9 (was Q9) — pkg-config and plugin dir renamed, no alias

**Ruled.** `sofi.pc`, `Name: sofi`, `pluginsdir=${libdir}/sofi`. No `rofi.pc` alias.
Sites: `meson.build:417-428` (hardcoded — does *not* follow a `project()` rename),
`pkgconfig/rofi.pc.in`.

### R11 (was Q11) — xdg-shell fallback IS in scope, as Phase 2b

**Ruled.** Add an xdg-shell fallback so sofi runs on compositors without
`zwlr_layer_shell_v1` — Mutter (GNOME) and KWin (Plasma).

This is the largest single functional gain available: it takes sofi from wlroots-only
(sway, Hyprland, river) to broadly usable. `xdg-shell.xml` is already compiled
(`meson.build:318`) but entirely unused by the backend.

Sequenced *after* Phase 2a (the `g_error` abort fix), because 2a is what makes the
unsupported case reachable in the first place. Estimated 2–3 days.

### R12 (was Q12) — `AUTHORS`, `CODE_OF_CONDUCT.md`, `releasenotes/` stay deleted

**Ruled.** The working-tree deletions are deliberate and are kept.

**Attribution is instead carried by `COPYING` and `README.md`.** Verified at ruling time:
`COPYING` is present and unmodified, and already contains both required notices —
`Copyright (c) 2012 Sean Pringle` and `Modified 2013-2024 Qball Cow`. That satisfies the MIT
obligation on its own.

Required follow-up so provenance is not weakened by the loss of `AUTHORS`:
- `README.md` must carry an explicit "sofi is a hard fork of rofi by Dave Davenport (Qball),
  originally derived from simpleswitcher by Sean Pringle, with Wayland support by lbonn"
  statement.
- The 78 per-file copyright notices across 73 files in `source/`, `include/`, `lexer/`,
  `config/` must survive Phase 3c untouched. This is now the *primary* attribution mechanism,
  which raises the severity of risk RR4 from Medium to High.

### Still open

Q7 (partially resolved by R12 — `releasenotes/` deleted; `mkdocs/docs/1.7.*` and `Changelog`
still undecided), Q13 (`.gitlab-ci.yml` delete vs rewrite), Q14 (CI target platforms).
None gate Phase 3.

---

## 2026-08-22 19:02 — RULINGS: Q1, Q2, Q4, Q10 decided by USER

The USER selected the clean-break option on every compatibility question. The governing
principle, now established for all downstream work:

> **sofi is a hard fork with its own identity, not a drop-in rofi replacement.**
> No compatibility shims, no dual-name exports, no fallback path lookups.

This resolves a large amount of design ambiguity and *removes* work from the plan. It also
means the first release must carry an unambiguous "this is not rofi and will not read your
rofi configuration" statement.

### R1 (was Q1) — Plugin ABI: rename everything, no shim

**Ruled.** Rename all 335 `rofi_*` / `ROFI_*` / `Rofi*` identifiers to `sofi_*` / `SOFI_*` /
`Sofi*`. No compatibility header. No `rofi.pc` alias.

Consequences accepted:
- Every out-of-tree rofi plugin breaks at both source and binary level with no migration path.
- `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted relative
  path and must be edited when those files are renamed.
- `ABI_VERSION` at `include/mode.h:36` is still bumped from 7, so a stale plugin fails
  loudly at `source/rofi.c:648` rather than crashing.

### R2 (was Q2) — On-disk paths: hard break, no fallback

**Ruled.** `$XDG_CONFIG_HOME/sofi/config.rasi`, `sofi.rasi` system-wide, `sofi/themes/`,
`sofi/scripts/`, and sofi-named cache files. No fallback read of any `rofi/` path.

Sites: `source/rofi.c:1059`, `:1120`, `:1132`; `source/helper.c:1474`, `:1483`, `:1493`,
`:1509`; `source/modes/script.c:630`, `:640`; `source/modes/drun.c:66`, `:69`;
`source/modes/run.c:64`; `source/modes/ssh.c:83`; filebrowser cache.

Consequence accepted: an existing rofi user launching sofi gets the built-in default theme
and empty history, with no diagnostic pointing at their old config. **This must be stated
prominently in the README and first release notes** — it is the single most likely source of
"sofi is broken" reports.

### R4 (was Q4) — Script env vars: `SOFI_*` only, hard break

**Ruled.** `SOFI_RETV`, `SOFI_OUTSIDE`, `SOFI_INFO`, `SOFI_DATA`, `SOFI_INPUT`,
`SOFI_PLUGIN_PATH`, `SOFI_PNG_OUTPUT`. The `ROFI_*` names are not exported.

Sites: `source/modes/script.c:216-231`; `source/rofi.c:705`, `:991`; `source/view.c:138`;
docs at `doc/rofi-script.5.markdown:55,65,70,74`.

Consequence accepted: every existing third-party script mode fails *silently* — it reads an
unset variable rather than erroring. Because the failure is silent, the script-mode manpage
must document the rename explicitly at the top, and the sofi script protocol should be
presented as its own contract rather than as a delta from rofi's.

### R10 (was Q10) — glib is a permitted dependency

**Ruled.** `glib-2.0`, `gio-2.0`, `gmodule-2.0` and `gdk-pixbuf-2.0` (all LGPL-2.1+) join
`pango` and `cairo` as permitted copyleft exceptions under the `AGENTS.MD` FOSS rule.

Rationale: inherited from upstream, used in essentially every file, and present in the
public header types themselves. Removing it would be a rewrite, not a rebrand. `AGENTS.MD`
should be amended to record the widened exception so the rule and the codebase agree.

### Constraint that these rulings do NOT override

**MIT attribution is a license obligation, not a compatibility preference.** The clean-break
decision applies to identifiers, paths and protocols — not to copyright notices. Regardless
of R1/R2/R4:

- `COPYING` remains unmodified with the original copyright lines intact.
- The per-file MIT headers in `source/` and `include/` must **not** be rewritten by the
  identifier rename. The bulk substitution in Phase 3c must exclude comment blocks
  containing a copyright line, or it will silently alter the license notices.
- `AUTHORS` is retained. (It currently shows as deleted in the working tree — see Q12.)
- Fork provenance credit to Sean Pringle, Dave Davenport / Qball, and lbonn is preserved.

---

## 2026-08-22 18:55 — Open questions raised by the initial audit

**Superseded entries:** Q1 → R1, Q2 → R2, Q4 → R4, Q10 → R10 above. Retained below for the
reasoning that informed each ruling.

Nine rulings are required from the USER before any renaming begins. Each is a compatibility
decision, not a style preference: the answer determines whether an existing rofi user's
configuration, themes, plugins or script modes survive the transition. No default has been
assumed. A recommendation is given for each, with the reasoning.

### Q1 — Public C API/ABI: rename `rofi_*` / `ROFI_*` / `Rofi*` symbols?

**Scope.** 335 distinct identifiers. Five installed headers form the plugin contract:
`include/mode.h`, `include/mode-private.h`, `include/helper.h`, `include/rofi-types.h`,
`include/rofi-icon-fetcher.h` (`meson.build:195-203`), installed to
`$includedir/<project_name>/`.

**Options.**
- (a) Rename everything to `sofi_*`. Clean, self-consistent, breaks every out-of-tree plugin
  at both source and binary level.
- (b) Keep `rofi_*` internally, rename only the user-visible identity. Zero plugin breakage,
  but the codebase permanently reads as rofi and the rebrand is cosmetic.
- (c) Rename to `sofi_*` and ship a compatibility header of `#define rofi_x sofi_x` plus a
  `rofi.pc` that `Requires: sofi`. Source-compatible for plugins that recompile; still
  breaks already-compiled plugin binaries (which the plugin-dir move breaks regardless).

**Recommendation: (c).** The plugin dir moves from `$libdir/rofi` to `$libdir/sofi`
regardless, so already-installed plugin binaries stop being found no matter what is chosen —
binary compatibility is already lost. Given that, source compatibility is the only thing
worth preserving, and a shim header buys it cheaply. Bump `ABI_VERSION` at
`include/mode.h:36` from 7 so a stale plugin fails loudly at `source/rofi.c:648` instead of
crashing.

**Note.** `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted
relative path, so renaming that file requires editing those two lines.

### Q2 — On-disk config and cache paths: migrate, fall back, or hard break?

**Scope.** `$XDG_CONFIG_HOME/rofi/config.rasi` (`source/rofi.c:1059`);
`rofi.rasi` in `$XDG_CONFIG_DIRS` and `$SYSCONFDIR` (`source/rofi.c:1120`, `:1132`);
the four theme search roots (`source/helper.c:1474`, `:1483`, `:1493`, `:1509`);
`$XDG_CONFIG_HOME/rofi/scripts/` (`source/modes/script.c:630`, `:640`);
and five cache files — `rofi3.druncache` (`source/modes/drun.c:66`),
`rofi-drun-desktop.cache` (`:69`), `rofi-4.runcache` (`source/modes/run.c:64`),
`rofi-2.sshcache` (`source/modes/ssh.c:83`), `rofi3.filebrowsercache`.

**Recommendation: search `sofi/` first, fall back to reading `rofi/` when the sofi path is
absent; always write to `sofi/`.** This is the single highest-blast-radius surface in the
project. A hard break is silent — no error, the user just gets the default theme and an empty
history and has to work out why. Caches are regenerable and can hard-break; config and themes
must not. Log at `g_debug` level when a fallback path is used.

### Q3 — The `.rasi` / `.rasinc` extension: keep or rename?

**Scope.** `lexer/theme-lexer.l:56` (`rasi_theme_file_extensions[]`), consumed at
`lexer/theme-lexer.l:437` and `source/rofi.c:1143-1145`; 33 shipped theme files.

**Recommendation: keep `.rasi` and `.rasinc` unchanged.** RASI is the *format* name, not the
product name. Tens of thousands of user configs and every third-party theme repository use
it. Renaming invalidates the entire community theme ecosystem and buys nothing. If a native
suffix is wanted later, *append* to the array rather than replacing:
`{".rasi", ".rasinc", ".sasi", ".sasinc", NULL}`.

Do **not** rename the shipped theme files either — `@theme "solarized"` in a user config
resolves by basename.

### Q4 — Script-mode environment variables: `ROFI_*` → `SOFI_*`?

**Scope.** `ROFI_RETV`, `ROFI_OUTSIDE`, `ROFI_INFO`, `ROFI_DATA`, `ROFI_INPUT`
(`source/modes/script.c:216-231`), plus `ROFI_PLUGIN_PATH` (`source/rofi.c:705`),
`ROFI_OUTSIDE` read back at `source/rofi.c:991`, and `ROFI_PNG_OUTPUT` (`source/view.c:138`).
Documented at `doc/rofi-script.5.markdown:55,65,70,74`.

**Recommendation: export BOTH names for at least one release; read both, preferring `SOFI_*`.**
This is a documented public protocol. Every script mode ever written reads `ROFI_RETV`.
Renaming alone doesn't produce an error — the script reads an unset variable and
misbehaves silently, which is the worst possible failure mode. Exporting both costs five
extra `g_environ_setenv` calls.

### Q5 — Wayland layer-surface namespace and X11 WM_CLASS?

**Scope.** `source/wayland/display.c:1792` (namespace `"rofi"`);
`source/xcb/view.c:773-775` (`wm_class_name[] = "rofi\0Rofi"`).

**Recommendation: rename to `sofi` / `"sofi\0Sofi"`.** These are the compositor-visible
identity and must match `StartupWMClass` in the desktop file. Users with compositor rules
keyed on `rofi` will need to update them — that is unavoidable and belongs in the release
notes, but it is loud rather than silent, so it is acceptable.

### Q6 — Helper scripts `rofi-sensible-terminal` / `rofi-theme-selector`?

**Scope.** `script/rofi-sensible-terminal`, `script/rofi-theme-selector`, installed at
`meson.build:204-208`; the compiled-in default at `config/config.c:65`.

**Recommendation: rename, and change `config/config.c:65` in the same commit.** If the
default terminal string and the installed script name diverge, `sofi -show run` cannot open a
terminal at all. Ship `rofi-sensible-terminal` as a compatibility symlink only if the USER
wants existing user configs containing `terminal: "rofi-sensible-terminal";` to keep working.

### Q7 — Frozen historical content: rebrand, keep verbatim, or delete?

**Scope.** `mkdocs/docs/1.7.0` … `1.7.9` and `2.0.0` (2,690 occurrences), `releasenotes/`
(210), `Changelog` (22).

**Recommendation: leave all of it verbatim and stop shipping it.** These document releases of
*rofi* that sofi never made. Rewriting them would fabricate a history. Keep the files for
provenance, exclude them from the docs site and the dist tarball. Note the git status shows
many of these already deleted in the working tree — that deletion should be a deliberate,
recorded decision rather than incidental.

### Q8 — MIT attribution: what must survive?

**Scope.** `COPYING`, `AUTHORS`, `README.md:31-41`, and the per-file MIT headers
(`Copyright © 2013-2020 Qball Cow <qball@gmpclient.org>` etc.) in essentially every source
file.

**Ruling required, but the license constrains the answer.** The MIT license requires the
copyright notice and permission notice to be retained in all copies. Concretely:
- `COPYING` must remain, unmodified, with the original copyright lines intact.
- The per-file MIT headers must **not** be stripped or reassigned during the rename. A
  bulk `rofi` → `sofi` substitution across `source/` and `include/` will otherwise rewrite
  the word "rofi" inside those headers — that must be excluded explicitly.
- `AUTHORS` must be retained; new contributors may be appended.
- `README.md` credit to Sean Pringle (simpleswitcher), Dave Davenport / Qball, and lbonn
  (Wayland) should be preserved as fork provenance.

Adding a "sofi is a fork of rofi by …" line is the recommended framing.

### Q9 — `pkg-config` module name and plugin directory?

**Scope.** `meson.build:417-428` (hardcoded `filebase: 'rofi'`, `name: 'rofi'` — these do
**not** follow a `project()` rename), `pkgconfig/rofi.pc.in`, `$libdir/rofi`.

**Recommendation: rename to `sofi.pc` / `$libdir/sofi`, and additionally ship a `rofi.pc`
containing `Requires: sofi`** so existing plugin build systems doing `dependency('rofi')`
keep configuring. Note this is one of exactly three places that will *not* follow a
`meson.build:1` rename automatically — the others are `executable('rofi')` at
`meson.build:367` and every literal `rofi*` filename in the install lists.

---

## 2026-08-22 16:40 — Tooling deviation from COMMAND LAWS (recorded for transparency)

`AGENTS.MD` requires IDE-native tooling over shell commands when running inside an IDE. This
session runs in the VSCode extension, but the harness exposes no native search tool (no
Grep/Glob equivalent is available). Read-only inspection of a 42k-line tree therefore used
`git grep` / `sed -n` via the shell, which was the only mechanism capable of the task. File
creation and editing used the native Write/Edit tools as required. Flagged so the USER can
rule on whether this deviation is acceptable going forward.
