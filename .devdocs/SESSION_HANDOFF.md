# SESSION HANDOFF

Reverse-chronological. Most recent session at the top.

---

## 2026-08-24 10:20 — Session 3: sofi becomes the shell

### Request

Analyse the four intended system surfaces — application menu left, task/window manager bottom,
sheet switcher right, notifications — then build them as *"the config-less native structure and
build of this program as it is a fork made specifically for this current active hikari-sakura
window compositor."* Then plan the notification daemon and map the devdocs comprehensively.

### Accomplished

**Phase 8 delivered and verified on hardware.** Four compiled-in panel layouts selected by mode,
per-surface instance locks, task-manager verbs on window mode, layer-shell v1 → v4 with a
keyboard-interactivity option, a hikari control socket, a native `sheets` mode, and `-e`
self-dismissal. 19/19 tests green before and after.

**Phase 9 planned.** Notification daemon scoped into N1–N8, ~950 lines, no new dependencies.

**Q17 closed.** Send-to-sheet — recorded in the previous session as inexpressible by any
standards-track protocol — is implemented as the socket's `pin <n>` and verified moving a window
from sheet 5 to sheet 8.

### Two claims retracted mid-session

Both had been asserted to the USER before being checked, and the USER caught the first one.

1. **`sofi -show window` was reported non-functional.** Wrong twice over: wlroots is dynamically
   linked so `strings` on the compositor cannot see the protocol name, and a binary predating its
   own commit is the normal build-then-commit order. `nm -u` and a `wl_registry` dump both show
   the protocol live at v3.
2. **The sheet switcher was reported fully blocked.** Half of it already worked — hikari
   publishes sheet visibility through foreign-toplevel's minimised bit, and sofi was parsing that
   bit and discarding it.

**Method change adopted as a result:** compositor capability claims are settled by a
`wl_registry` dump. A 20-line listener was compiled for this and should be reused, not
re-derived.

### Files modified — `sofi`

| File | Change |
|---|---|
| `source/sofi.c` | `sofi_surface_name()`, `sofi_builtin_panel_resource()`, per-surface pidfile, sheets mode registration |
| `source/view.c` | Arm the auto-dismiss timer in `sofi_view_error_dialog()` |
| `source/wayland/display.c` | Keyboard-interactivity option with a runtime version guard |
| `source/modes/wayland-window.c` | Surface the minimised bit; minimise/maximise toggles on `kb-custom-1/2` |
| `include/wayland-internal.h` | Layer-shell bind version 1 → 4 |
| `include/settings.h`, `config/config.c`, `source/xrmoptions.c` | `wayland_keyboard_interactivity` |
| `resources/resources.xml`, `doc/default_configuration.sasi` | Panel resources; theme choice moved into C |
| `meson.build`, `meson_options.txt`, `include/modes/modes.h` | `sheets` option and sources |

**New:** `doc/panel-window.sasi`, `doc/panel-sheets.sasi`, `doc/panel-notify.sasi`,
`source/modes/sheets.c`, `include/modes/sheets.h`.

### Files modified — `hikari-sakura`

**New:** `src/ipc.c`, `include/hikari/ipc.h`.
**Modified:** `src/server.c` (setup + teardown), `include/hikari/server.h` (5 fields),
`Makefile` (`ipc.o`).

### Decisions

R16–R24 and the closure of Q17, all in `DECISIONS_LOG.md`. The load-bearing ones: the notification
daemon is built in sofi rather than the compositor (R20), sheet control uses a socket rather than
`ext_workspace_v1` (R19), and the daemon's two safety settings are forced in code rather than read
from a theme (R24).

### State at handoff

**Nothing is committed in either repository.** Both build clean; sofi is 19/19.
`/usr/local/bin/sofi` is current as of 09:48; `/usr/local/bin/hikari` is the Aug 22 build with no
socket, so the sheets mode correctly reports `ENOENT` until hikari is rebuilt and the session
restarted.

### Next steps

1. Commit both repositories — a second agent was active in `hikari-sakura` at 09:11–09:15.
2. `make clean && make` in hikari (clean is mandatory), install both, restart the compositor.
3. Add a sheets binding to `hikari.conf`; `L+s` and `LS+s` are taken.
4. Begin Phase 9 N1 and N2 on approval.

---

## 2026-08-22 18:55 — Session 1: Initialization and deep audit

### Request

Rebrand the project entirely from rofi to sofi; inspect deeply for poor implementation and
bad handling, particularly around functionality, layer-shell, Wayland and FreeBSD
compatibility, and structure; adhere strictly to `AGENTS.MD`; produce a deep analysis and
report a plan.

### Accomplished

- `.devdocs/` did not exist, so Phase 1 Initialization applied: read all existing project
  documentation and code, generated the workspace, and halted for permission.
- Ran a 21-agent read-only audit across 10 domains, each survey followed by an adversarial
  verifier instructed to default to REFUTED. 263 raw findings, 15 refuted, 248 retained.
  166 rebrand surfaces catalogued and classified by blast radius.
- Verified the baseline build state directly rather than assuming it.

### Files modified

**None in the product tree.** Created only inside `.devdocs/`:

| File | Purpose |
|---|---|
| `BRIEFING.md` | Current status and phase |
| `PROGRESS.md` | Macro tracking |
| `SESSION_HANDOFF.md` | This file |
| `DECISIONS_LOG.md` | Nine open questions + the tooling deviation note |
| `TODOS.md` | Granular backlog, B0–B6 |
| `PLANS.md` | Six ordered execution phases with grep invariants |
| `BLUEPRINT.md` | Architecture, dependencies, public contracts |
| `AUDIT_REGISTER.md` | All 248 findings with file:line, scenario, fix, verdict |
| `REBRAND_SURFACES.md` | All 166 identity surfaces by risk class |

### Key discoveries

1. **The project does not configure on this host.** `meson.build:224` needs GNU Bison;
   FreeBSD base ships byacc, and `lexer/theme-parser.y:28-33` uses `%glr-parser` /
   `%skeleton "glr.c"` / `%define api.pure`, which byacc cannot build. `check` is also
   absent, so tests are silently skipped. There is no green baseline yet.

2. **sofi aborts rather than degrades on non-wlr compositors.** Four `g_error()` calls at
   `source/wayland/display.c:1490,1505,1752,1756` call `abort()`. `source/rofi.c:1280-1298`
   contains a friendly fallback message that is unreachable. On GNOME/Mutter the result is
   SIGABRT and a core dump. There is no xdg-shell fallback anywhere in the backend.

3. **A format-string bug was introduced in this tree, not inherited.**
   `source/rofi.c:847` is `g_warning(((GString*)iter->data)->str, NULL)`. `git log -L`
   shows commit `30885f2b` replaced a safe `fputs` pair with it.

4. **The existing test suite triggers a heap overflow.** `test/helper-test.c:185` calls
   `utf8_strncmp("aapno", "a", 4)`; `source/helper.c:1357` writes `'\0'` at offset 4 of a
   2-byte normalized buffer.

5. **CI tests the wrong project.** `.build.yml:32` clones `https://sr.ht/~qball/rofi/` and
   builds upstream rofi. `.gitlab-ci.yml` is pure autotools and cannot run at all — there is
   no `configure.ac`.

6. **The C source is genuinely portable.** Zero hits for `strcasestr`, `asprintf`,
   `qsort_r`, `memmem`, `pipe2`, `execvpe`, `program_invocation_name`, `__GLIBC__`,
   `/proc/`. FreeBSD support is mostly a build-tooling and CI problem, not a code problem.

### Decisions made

**2026-08-22 19:02 — four rulings, all clean-break.** Governing principle established:
*sofi is a hard fork with its own identity, not a drop-in rofi replacement.*

- **R1** Rename all 335 identifiers to `sofi_*`/`SOFI_*`/`Sofi*`. No compat shim, no
  `rofi.pc` alias. `ABI_VERSION` bumped so stale plugins fail loudly.
- **R2** On-disk paths hard-break to `sofi/`. No fallback read of `rofi/`.
- **R4** Script env vars become `SOFI_*` only.
- **R10** glib/gio/gmodule/gdk-pixbuf permitted as LGPL exceptions; `AGENTS.MD` to be amended.

Net effect on the plan: Phase 3 *shrinks* (no shim header, no dual exports, no fallback
probing), but gains required companion work in 3d because R2 and R4 both fail silently.

**Explicitly not overridden:** MIT attribution. Phase 3c must exclude copyright comment
blocks from the bulk rename and now carries a grep invariant asserting the notices survive.

Five questions remain open: Q3 (`.rasi` extension), Q5 (WM_CLASS / layer-shell namespace),
Q6 (helper script names), Q7 (frozen historical docs), Q9 (pkg-config module name).

### Blockers for the next session

1. `devel/bison` and `devel/check` are not installed. Installing them is a system change and
   was not performed without approval. Nothing can be build-verified until this is resolved.
2. Q3, Q5, Q6, Q7, Q9 are unanswered. These gate Phase 3 sub-phases only — Phases 0, 1 and 2
   are fully unblocked and can proceed on approval.

### Next steps

1. Approve and run Phase 0 (baseline: install bison + check, build, test).
2. Approve and run Phase 1 (the five defects the rebrand would otherwise cement).
3. Approve and run Phase 2 (Wayland/layer-shell — 13 items, the largest correctness win).
4. Answer Q3/Q5/Q6/Q7/Q9, then Phase 3 (the rename), per `PLANS.md`.

### Note for the next session

`AGENTS.MD` requires IDE-native tooling over shell commands. This harness exposes no native
search tool, so read-only inspection used `git grep`/`sed -n`; file creation used the native
Write tool. Recorded in `DECISIONS_LOG.md` for a ruling.
