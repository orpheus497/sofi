# Contributing to sofi

sofi is a small, purpose-built project: it is the shell for the
[hikari-sakura](https://github.com/orpheus497/hikari-sakura) compositor, developed
primarily on FreeBSD and targeting Wayland. It is a hard fork of rofi and does
not track upstream. Please do not file rofi issues here, and please do not file
sofi issues upstream.

Bug reports, patches and questions are all welcome. Be clear and be specific —
that is the whole of the etiquette.

## Where things go

- **Bugs** — the [issue tracker](https://github.com/orpheus497/sofi/issues).
- **Questions, ideas, and anything you are unsure about** —
  [GitHub Discussions](https://github.com/orpheus497/sofi/discussions).
  A discussion can be promoted to an issue later; that is easier than the reverse.

## Reporting a bug

Before you file:

- Build from the latest `master` and check the problem still happens. sofi is not
  packaged by any distribution, so there is no "latest release" to compare against
  yet — see [INSTALL.md](../INSTALL.md).
- Search existing issues.
- Check the manpages. `sofi(1)` covers the options, `sofi-theme(5)` the theme
  format, and `sofi-debugging(5)` explains how to produce a useful trace.

Include:

- **Version** — the output of `sofi -v`, or the commit you built.
- **Environment** — compositor or window manager, and whether you are on Wayland
  or X11. If Wayland, say which compositor: layer-shell support varies, and sofi
  behaves differently under `xdg-shell` fallback.
- **How to reproduce it**, as precisely as you can.
- **What you expected** and **what actually happened**.
- **Configuration**, if you have one. sofi runs with no config file at all, so
  please check whether the problem still occurs with `-no-config` before blaming
  your theme.

For anything involving speed, attach a timing trace — `sofi-debugging(5)` has the
instructions.

## Requesting a feature

Check `master` first, and check existing requests. Then describe:

- the problem you are trying to solve, not just the solution you have in mind
- how you would expect to use it — a config option, a key binding, a mode
- who else it helps

sofi is deliberately narrow in scope. Requests that make it a better shell for
hikari-sakura, or that fix something genuinely broken, are the most likely to land.
Requests that make it a general-purpose application platform are the least. Asking
is free either way.

## Patches

- Target `master`.
- Keep commits reasonably scoped, with descriptive summaries.
- Match the surrounding code. C99, and the documentation conventions in
  [AGENTS.md](../AGENTS.md) — comment where the code is not self-explanatory,
  not everywhere.
- Make sure `ninja -C build` is clean and `meson test -C build` passes.
- Mark work in progress with `[WIP]` in the title.
