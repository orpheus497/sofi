# PLANS

**Last updated:** 2026-08-22 19:02

Forward-looking execution strategy. Nothing here is approved; nothing here has been executed.

Every phase ends with the tree **building and passing tests**, plus a **grep invariant** that
can be checked mechanically. Phases are ordered so that no phase depends on a later one.

---

## Phase 0 — Establish a baseline (blocking, ~15 min)

Nothing can be verified until the project builds.

| Step | Detail |
|---|---|
| Install `devel/bison` | `meson.build:224` needs GNU Bison; `lexer/theme-parser.y:28-33` uses `%define api.pure`, `%glr-parser`, `%skeleton "glr.c"` which byacc cannot provide |
| Install `devel/check` | otherwise `meson.build:148` silently disables the entire test suite |
| `meson setup build && ninja -C build && ninja -C build test` | record the true starting state |

**Expected complication.** `test/helper-test.c:185` calls `utf8_strncmp("aapno", "a", 4)`,
which the audit confirms writes 3 bytes past a 2-byte heap allocation
(`source/helper.c:1357`). A clean baseline may not be reachable until Phase 1 lands.

**Exit criterion.** A recorded, reproducible build + test result — green or not.

---

## Phase 1 — Fix what the rebrand would otherwise cement (~1–2 days)

These are fixed **first**, before renaming, because they live in installed headers or in
code the rename will touch wholesale. Fixing them afterwards means editing the same lines
twice and reviewing a rename diff that also contains logic changes.

| # | Site | Defect |
|---|---|---|
| 1 | `source/helper.c:1357` | `utf8_strncmp` writes OOB when a string is shorter than `n`; callers `source/modes/combi.c:162,320` compute the guard on the un-normalized string |
| 2 | `include/widgets/textbox.h:63` | `short cursor` overflows past 32767 chars, feeding a negative offset to `g_utf8_offset_to_pointer` — this field is in a header |
| 3 | `source/rofi.c:847` | `g_warning(str, NULL)` — user-controlled text used as a printf format string. Regression introduced in this tree by commit `30885f2b` |
| 4 | `include/rofi-types.h:233` | `WindowLocation` is a bitmask enum but `config.location` is used as a 0–8 index (`source/wayland/view.c:151`, `source/xcb/view.c:281`, written as a raw index at `source/modes/dmenu.c:602`). This type is in an installed public header |
| 5 | `include/mode.h:36` | Bump `ABI_VERSION` from 7 so stale plugins fail loudly at `source/rofi.c:648` |

Also enable `-Wformat=2` / `-Wformat-security` in `meson.build` so defect 3 cannot regress.

**Verification.** Build green; `ninja test` green; ASAN build clean over the test suite.
**Grep invariant.** `git grep -n 'g_warning([^"]' -- source/` returns nothing.

---

## Phase 2 — Wayland / layer-shell correctness (~2 days)

The Wayland backend carries the highest concentration of real defects and is the backend the
project most needs to be correct. Independent of the rename.

**Startup path — makes sofi usable on non-wlr compositors.**
- `source/wayland/display.c:1490,1505,1752,1756` — four `g_error()` calls abort the process
  with a core dump. `source/rofi.c:1264` already stores the return value and
  `source/rofi.c:1280-1298` already prints a friendly fallback message that can never run.
  Replace with `g_warning`/`g_critical` + the existing `return FALSE`. In the registry
  handler, skip the unsupported global rather than aborting the whole client.
  This is why sofi cannot run on GNOME/Mutter today.
- `source/wayland/display.c:1791` — `wayland_display_late_setup()` dereferences
  `layer_shell`/`compositor` without NULL checks and unconditionally returns TRUE.

**Buffer pool.**
- `source/wayland/display.c:208` — fixed shm name `"/rofi-wayland-surface"` with
  `O_CREAT|O_EXCL`: two concurrent instances collide, and a SIGKILL between the `shm_open`
  and the `shm_unlink` poisons the name permanently. Use `SHM_ANON` on FreeBSD /
  `memfd_create` on Linux, or a PID+counter name.
- `source/wayland/display.c:319` — `display_buffer_pool_get_next_buffer()` dereferences a
  NULL pool; its only caller (`source/wayland/view.c:428-432`) passes the unchecked result of
  `display_buffer_pool_new()`, which has five NULL returns. This is the crash the poisoned
  shm name causes.
- Unchecked `stride * height * buffer_count` overflow before the int32 offset handed to
  `wl_shm_pool_create_buffer`.

**Input and lifecycle.**
- `source/wayland/display.c:439` — key-repeat `GSource` pointer left dangling on early
  returns, then `g_source_destroy()`d → use-after-free. Store the `guint` id instead.
- `source/wayland/display.c:472` — `repeat_info` `rate == 0` means *repeat disabled*; the
  code repeats at ~33 Hz anyway.
- `source/wayland/display.c:380` — keymap fd, mapping and `xkb_keymap` leaked per event.
- `source/wayland/display.c:1223` — `wl_seat.capabilities` tests the POINTER bit to decide
  whether to release the KEYBOARD.

**Sizing and positioning.**
- `source/wayland/view.c:396` — `calculate_window_width` reads the layer surface size as if
  it were the screen size, so with `-no-click-to-exit` each successive view halves in width.
- `source/wayland/view.c:158` — `-x-offset`/`-y-offset` are silently ignored on Wayland; the
  xcb path seeds the same lookup from `config.x_offset` (`source/xcb/view.c:358-361`).

**Window mode.**
- `source/modes/wayland-window.c:622,635` — selected toplevel dereferenced without a NULL
  check; the list shrinks synchronously at `:354` while the UI reload is coalesced into a
  66 ms timer (`source/wayland/view.c:305-308`), so Enter inside that window crashes.
- `source/modes/wayland-window.c:480,508,520` — no-wlr compositors get an empty list rather
  than a diagnostic; manager stopped after the list is freed; `stop()` called on an
  already-destroyed proxy.

**Verification.** Run under sway (wlr). Weston with `--use-pixman` for the buffer path.

---

## Phase 2b — xdg-shell fallback (~2–3 days)

Per **R11**. The largest single functional gain available: takes sofi from wlroots-only
(sway, Hyprland, river) to broadly usable on Mutter (GNOME) and KWin (Plasma).

Sequenced *after* Phase 2a, because 2a's `g_error` → `g_warning` change is what makes the
unsupported-compositor case reachable instead of an abort.

**Current state.** `xdg-shell.xml` is already compiled (`meson.build:318`) but entirely
unused — `grep xdg source/wayland/` returns nothing. `zwlr_layer_shell_v1` is bound at
`source/wayland/display.c:1471-1474` and treated as mandatory at `:1755`.

**Work.**
- Bind `xdg_wm_base` in the registry handler alongside `zwlr_layer_shell_v1`; make
  layer-shell preferred, xdg-shell the fallback, and fail only when neither is present.
- Introduce a small surface abstraction so `wayland_display_late_setup()`
  (`source/wayland/display.c:1768-1824`) and `display_set_surface_dimensions()` (`:1846-1904`)
  dispatch to either backend rather than calling `zwlr_layer_surface_v1_*` directly. There
  are 14 direct `zwlr_layer_surface_v1_*` call sites to route.
- Implement `xdg_surface.configure` / `ack_configure` and the `xdg_wm_base.ping`/`pong`
  handshake — omitting pong makes compositors kill the client as unresponsive.
- Positioning: xdg-toplevel cannot self-position. Anchors and `x-offset`/`y-offset` degrade
  to compositor placement. Document this rather than faking it.
- `set_keyboard_interactivity` has no xdg equivalent; focus follows normal window rules.
- `click_to_exit` needs a different mechanism (no screen-sized capture surface available).

**Verification.** sway (layer-shell path unchanged) · GNOME/Mutter and KWin (fallback path)
· confirm the layer-shell path is still chosen when both are advertised.

**Documented limitation.** README's "Missing features in Wayland mode" section gains an
xdg-shell subsection: no precise positioning, no keyboard-interactivity override, degraded
click-to-exit.

---

## Phase 3 — The rename, in dependency order (~2 days)

Governed by the 19:02 and 19:15 rulings: **hard fork, no shims, no fallbacks.** No compat
header, no `rofi.pc` alias, no dual env-var exports, no path probing, no script symlinks.
Each sub-phase is independently buildable. **All gates closed — unblocked.**

### 3a. Build identity
`meson.build:1` `project('rofi')` → `sofi`. This one line cascades to `plugindir` (`:43`),
`themedir` (`:44`), `PACKAGE_NAME` (`:152`), `GETTEXT_PACKAGE` (`:155`), installed-header
subdir (`:202`) and doxygen `PROJECT_NAME`.

Three things do **not** follow and must be edited by hand:
`executable('rofi')` (`:367`), `pkg.generate(filebase:'rofi', name:'rofi')` (`:420-423`), and
every literal `rofi*` filename in the install lists.

Also: `PACKAGE_BUGREPORT`/`PACKAGE_URL` (`:156-157`) still point at `davatorium/rofi`.

**Invariant.** `ninja` produces `sofi`; `pkg-config --modversion sofi` resolves.

### 3b. File renames (`git mv`, no content edits)
30 live files. `source/rofi.c`, `include/rofi.h`, `include/rofi-types.{c,h}`,
`include/rofi-icon-fetcher.{c,h}`, `pkgconfig/rofi.pc.in`, `doc/rofi*.markdown` (10),
`doc/rofi.doxy.in`, `data/rofi*`, `script/rofi-*`, `Examples/rofi-file-browser.sh`.

Remember `include/mode.h:30` and `include/helper.h:30` include `"rofi-types.h"` by quoted
relative path.

**Invariant.** `git ls-files | grep -i rofi` returns only intentionally-frozen paths.

### 3c. C identifiers (2,284 lowercase + 516 `ROFI` + 468 `Rofi`, 335 distinct)
Per **R1**: rename all of them to `sofi_*` / `SOFI_*` / `Sofi*`. No compat shim.
Bump `ABI_VERSION` at `include/mode.h:36` (already scheduled in Phase 1).

**Must exclude the per-file MIT copyright headers.** A naive tree-wide substitution rewrites
the license notices, which is a license violation, not a cosmetic slip. Do this as a scripted
rename over a reviewed identifier list that skips comment blocks containing a copyright line
— not a blind `sed`.

**Invariant.** `git grep -lE '\b(rofi_|ROFI_|Rofi)' -- source/ include/` returns nothing.
**Second invariant.** `git diff COPYING AUTHORS` is empty, and
`git grep -c 'Copyright' -- source/ include/` is unchanged from the pre-rename count.

### 3d. On-disk paths, env vars, compositor identity
Per **R2** and **R4**: hard break, no fallback lookups, no dual-name env exports.

- Config/theme/script paths → `sofi/` only (`source/rofi.c:1059,1120,1132`;
  `source/helper.c:1474,1483,1493,1509`; `source/modes/script.c:630,640`)
- Cache files → sofi-named (`source/modes/drun.c:66,69`, `run.c:64`, `ssh.c:83`, filebrowser)
- Env vars → `SOFI_*` only (`source/modes/script.c:216-231`; `source/rofi.c:705,991`;
  `source/view.c:138`)
- Layer-shell namespace and WM_CLASS → pending Q5

**Invariant.** `git grep -n '"rofi' -- source/ config/` returns nothing.

**Required companion work (R2/R4 consequence).** Both breaks are *silent* — no error is
produced, things simply stop being found. Before release:
- README and release notes must state plainly that sofi does not read rofi configuration
- `doc/sofi-script.5.markdown` must lead with the `ROFI_*` → `SOFI_*` rename
- Consider a one-shot startup `g_message` when `~/.config/rofi/` exists but
  `~/.config/sofi/` does not — a pointer, not a fallback. Cheap, and converts the most
  likely support burden into a self-answering message.

### 3e. Theme extension `.rasi`/`.rasinc` → `.sasi`/`.sasinc`
Per **R3**.
- `lexer/theme-lexer.l:56` — the extension array
- 35 theme files renamed (`themes/*.rasi` + `themes/gruvbox-common.rasinc`) and the
  `meson.build:378-415` install list
- `doc/default_theme.rasi`, `doc/default_configuration.rasi`
- `config.rasi` → `config.sasi` (`source/rofi.c:1059`, `:1143-1145`);
  `rofi.rasi` → `sofi.sasi` (`source/rofi.c:1120`, `:1132`)

**No importer edits needed.** The six gruvbox variants use extension-less
`@import "gruvbox-common"` (`themes/gruvbox-*.rasi:61`), which resolves via the extension
array — renaming the include file alone is sufficient. Verified 2026-08-22 19:15.

*Invariant:* `git ls-files | grep -cE '\.rasi(nc)?$'` → 0.

### 3f. Compositor identity, scripts, pkg-config
Per **R5**, **R6**, **R9**.
- Layer-shell namespace `source/wayland/display.c:1792`; WM_CLASS `source/xcb/view.c:773-775`
- `script/sofi-sensible-terminal`, `script/sofi-theme-selector` — **and `config/config.c:65`
  in the same commit**, or `sofi -show run` cannot open a terminal
- `sofi.pc` / `pluginsdir=${libdir}/sofi` (`meson.build:417-428`, `pkgconfig/rofi.pc.in`)

### 3g. Docs, packaging, data
Manpages renamed and their cross-references updated; `data/*.desktop` `Name`/`Exec`/`Icon`/
`StartupWMClass` (must match the R5 WM_CLASS); `data/rofi.svg` → `sofi.svg` (basename must
match `Icon=`); `doc/rofi-thumbnails.5.markdown:62,71` AppArmor path `/usr/bin/rofi` →
`/usr/bin/sofi`; README/INSTALL/CONFIG rewritten; all upstream URLs repointed.

**Attribution work required by R12.** With `AUTHORS` deleted, `COPYING` plus `README.md`
carry the entire MIT provenance. README must gain an explicit fork-provenance statement
crediting Dave Davenport (Qball), Sean Pringle (simpleswitcher) and lbonn (Wayland).
`COPYING` is verified present and unmodified, already carrying both required notices.

---

## Phase 4 — FreeBSD compatibility, proven not assumed (~half day)

**Actually broken today:** GNU Bison not detected/documented (Phase 0);
`shm_open` with a fixed name (Phase 2) — `SHM_ANON` is the native FreeBSD idiom;
`meson.build` never links `librt`, which older glibc needs for `shm_open` (harmless on
FreeBSD, breaks older Linux);
`INSTALL.md:259-262` offers only `pkg install rofi` with no from-source dependency list;
`INSTALL.md:26` documents an autotools `--disable-check` flag meson does not have.

**Merely untested:** the C source is genuinely portable — zero hits for `strcasestr`,
`asprintf`, `qsort_r`, `memmem`, `pipe2`, `execvpe`, `program_invocation_name`,
`canonicalize_file_name`, `__GLIBC__`, `/proc/`. `sysexits.h`, `glob.h`, `sys/file.h`,
`pwd.h` all exist on BSD. `_GNU_SOURCE` at `meson.build:159` is unconditional but is inert
on FreeBSD. There are no `__FreeBSD__` conditionals anywhere — and, encouragingly, none are
needed.

**CI:** `.build.yml` clones `https://sr.ht/~qball/rofi/` (`:32`) and builds *upstream rofi*,
then declares an artifact for version `1.7.8-dev` (`:47`) while `meson.build:2` says
`2.0.0-dev`. `.gitlab-ci.yml` is pure autotools (`autoreconf -i` at `:26`) and cannot run at
all — the project has no `configure.ac`. Both need rewriting or deleting; add a real FreeBSD
job.

---

## Phase 5 — Remaining correctness backlog (~3–5 days)

The 59 medium findings, grouped. Full detail in `AUDIT_REGISTER.md`.

- **Threading / lifetime** — icon fetcher publishes surfaces from worker threads with no
  synchronisation (`source/rofi-icon-fetcher.c:746`), `destroy()` frees state in-flight
  workers still use (`:325`), and a worker calls into the UI layer (`:562`).
- **Unchecked replies (xcb)** — `source/xcb/display.c:466,572,1466`,
  `source/modes/window.c:640`, `source/xcb/view.c:593`.
- **Modes** — dmenu writes one past a fixed array (`source/modes/dmenu.c:157`) and leaves
  `permanent` uninitialized (`:169`); drun trusts the cache to NUL-terminate (`:948`) and
  follows symlinked dirs unbounded (`:839`); ssh follows `Include` with no cycle detection
  (`source/modes/ssh.c:394`); combi misdispatches `!` prefixes (`:158`) and indexes
  `switchers[0]` unchecked (`:195`).
- **Pidfile** — `create_pid_file()` SIGTERMs its own process group and then spins forever on
  a corrupt pidfile (`source/helper.c:615`); unchecked `write()` becomes an infinite loop
  (`:638`); `-replace` busy-waits forever if the target ignores SIGTERM (`:617`).
- **History durability** — rewritten by truncating the live file in place, so a crash
  mid-write destroys it (`source/history.c:244`).
- **Escaping** — `source/modes/window.c:901` appends window titles unescaped into a
  Pango-markup row; the Wayland twin escapes correctly. A title containing `&` renders
  blank on X11.
- **Structure** — `source/view.c:364` makes raw XCB calls from backend-agnostic code;
  `helper_eval_add_str` is copy-pasted between the two window modes and has already
  diverged; `window`/`windowcd` share one global cache either can free
  (`source/modes/window.c:870`).

---

## Phase 6 — Ship `sofi-config/` as the deployed default (~half day)

Added by the USER 2026-08-22 as the standard config and theme. **Validated against the real
parser**: `rofi -config sofi-config/config.rasi -dump-theme` exits 0 with *zero* warnings on
stderr, the `@import` resolves, and every property lands
(`location: west`, `anchor: west`, `width: 280px`, `height: 70%`, `x-offset: 15px`,
`border-radius: 12px`, `accent: #916778`).

| File | Role |
|---|---|
| `sofi-config/config.rasi` | 112 lines. `configuration {}` block + full widget theme. Sidebar layout, west-anchored, 280px, unified black 75% background, dusty-mauve accent |
| `sofi-config/colors-default.rasi` | 6 colour variables, imported by the above |

**Work:**
1. Decide the canonical location. `sofi-config/` is not currently installed by meson and is
   not on any search path. Options: replace `doc/default_configuration.rasi` +
   `doc/default_theme.rasi` (compiled into the binary via
   `resources/resources.xml` → `gnome.compile_resources`), or install to
   `$datadir/sofi/themes/` and ship as a named theme, or both.
2. Wire into `meson.build` `install_data`.
3. Reconcile with the built-in default theme so `@theme "default"`
   (`source/rofi.c:1179`) resolves to this.

**Two things that must not be missed:**

- **`@import "colors-default.rasi"` names the extension explicitly.** Under **R3** these
  files become `.sasi`/`.sasinc`, and unlike the shipped gruvbox themes — whose
  `@import "gruvbox-common"` is extension-less and resolves through the extension array —
  **this import line must be edited by hand** or the theme breaks. Same for the file pair
  itself. Add to the Phase 3e checklist.
- `modi:` is used in the `configuration {}` block. It is still a live alias for `modes`
  (`source/xrmoptions.c:74-86`, alongside the older `switchers`), so it works — but the
  shipped default should use the current spelling `modes:` rather than the deprecated one.

**Verification.** `sofi -config <path> -dump-theme` exits 0 with empty stderr; a visual check
under sway once a session is available.

---

## Phase 7 — New modes: window / workspace / task manager (~1–2 weeks, scope TBD)

Requested by the USER 2026-08-22 to be captured in planning, not built yet.

### 7a. Window switcher — **mostly exists**

`source/modes/window.c` (xcb, 1172 lines) and `source/modes/wayland-window.c` (wayland, 876
lines) already implement this. Phase 2a fixed the crash-on-stale-row, the manager lifecycle
and the silent empty-list failure.

Remaining gaps rather than new work:
- Wayland requires `zwlr_foreign_toplevel_management_v1`; KWin and Mutter implement neither
  wlr protocol, so window mode is unavailable there regardless of Phase 2b.
- The ext↔wlr handle correlation is a documented heuristic keyed on app_id ordering
  (`source/modes/wayland-window.c:581-588`) and can mis-target when two windows of the same
  class arrive in different order via the two protocols.
- `helper_eval_add_str` is duplicated between the two backends and has already diverged —
  the XCB copy fails to escape markup (`source/modes/window.c:901`, backlog B5).

**Recommendation:** treat as hardening, not new development.

### 7b. Workspace switcher — **does not exist; feasible on both backends**

No workspace mode exists. Groundwork is present on X11:
`xcb_ewmh_get_current_desktop()` is already used at `source/modes/window.c:559` and `:796`,
and `WM_PANGO_WORKSPACE_NAMES` handling exists at `source/modes/window.c:646`.

| Backend | Mechanism | Status |
|---|---|---|
| X11 | EWMH `_NET_CURRENT_DESKTOP`, `_NET_NUMBER_OF_DESKTOPS`, `_NET_DESKTOP_NAMES`, `_NET_WM_DESKTOP` | All reachable through the existing `xcb_ewmh` dependency; partly used already |
| Wayland | `ext-workspace-v1` | **Available on this host** — `/usr/local/share/wayland-protocols/staging/ext-workspace/`, wayland-protocols 1.49. Not currently in the `meson.build:317-327` protocol list |

**Work:** add `ext-workspace-v1` to the protocol list; new `source/modes/workspace.c` +
`source/modes/wayland-workspace.c` following the existing window-mode split; list workspaces
with name/index/occupied state; activate on select. Compositor support for `ext-workspace-v1`
is still uneven — needs a runtime capability check and a clear diagnostic, exactly like the
Phase 2a `_init` failure propagation.

### 7c. Task manager — **does not exist; scope genuinely ambiguous**

"Task manager" could mean two quite different things, and the answer changes the whole design:

1. **Process manager** — enumerate processes, show CPU/memory, send signals. Needs a new
   data source (`kvm_getprocs` on FreeBSD, `/proc` on Linux — note the audit found the tree
   currently has *zero* `/proc` dependencies, so this would introduce the first platform
   split of that kind), plus a privilege story for killing processes.
2. **Task/window manager** — a richer window mode with close/minimise/maximise/move-to-
   workspace actions. Largely an extension of 7a + 7b rather than a new data source.

**Open question Q15 — which of these is meant?** Recorded in `DECISIONS_LOG.md`. Reading (2)
as the intent given it sits alongside a window switcher and workspace switcher, but this
must be confirmed before any design work. If (1), the FreeBSD/Linux process-enumeration split
is a significant new portability surface and should be weighed against the project's
currently clean portability record.

---

## Risk register

| # | Risk | Likelihood | Detection / mitigation |
|---|---|---|---|
| RR1 | User's config, themes and script dirs silently orphaned | **Certain** — this is R2, accepted deliberately | Fresh-VM test with only `~/.config/rofi/` populated. Mitigate with README wording + the one-shot startup hint in Phase 3d |
| RR2 | Third-party script modes break silently on `SOFI_*` | **Certain** — this is R4, accepted deliberately | Run a known script mode (e.g. rofi-rbw) against the built binary and confirm it fails *visibly*, not subtly |
| RR3 | Installed rofi plugins orphaned by the `$libdir` move | **Certain** — inherent to R1 | `ABI_VERSION` bump converts a silent no-load into a loud version mismatch at `source/rofi.c:648` |
| RR4 | **MIT copyright headers rewritten by the bulk rename** | **High** — raised from Medium by R12 | With `AUTHORS` deleted, `COPYING` + the 78 per-file notices across 73 files are the *entire* attribution mechanism. Phase 3c invariant 2: `git diff COPYING` empty and the `Copyright` count unchanged at 78. Put it in CI |
| RR9 | Every third-party rofi theme needs a file rename (R3) | **Certain**, accepted | Loud failure (file not found), not silent. Release notes + a note in the theme manpage |
| RR10 | xdg-shell fallback silently degrades positioning (R11) | High on GNOME/KWin | xdg-toplevel cannot self-position. Document in README's Wayland limitations rather than faking it — do not let anchors appear to work when they don't |
| RR5 | CI reports green while building upstream rofi | **Certain today** | Fix `.build.yml:32` before trusting any CI result at all |
| RR6 | Rename diff hides logic changes from review | Medium | Phases 3a–3e stay mechanical; every logic change lands in Phase 1/2/5. Review 3c as a scripted transform + its invariants, not line by line |
| RR7 | No green baseline exists, so regressions are undetectable | **Certain today** | Phase 0 is blocking for exactly this reason |
| RR8 | Zero test coverage of either display backend | High, ongoing | Nothing in Phase 0–5 detects a Wayland regression. Backlog item B6 |

**Note on RR1–RR3.** These are not defects in the plan; they are the accepted cost of the
hard-fork ruling. They are recorded here so the release notes cover them and so nobody later
mistakes them for bugs.
