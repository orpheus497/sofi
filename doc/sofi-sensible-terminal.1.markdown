# sofi-sensible-terminal(1)

## NAME

**sofi-sensible-terminal** -  launches $TERMINAL with fallbacks

## SYNOPSIS

sofi-sensible-terminal [arguments]

## DESCRIPTION

sofi-sensible-terminal is invoked in the sofi default config to start a terminal. This
wrapper script is necessary since there is no distribution-independent terminal launcher
(but for example Debian has x-terminal-emulator). Distribution packagers are responsible for
shipping this script in a way which is appropriate for the distribution.

It tries to start one of the following (in that order):

* `$TERMINAL` (this is a non-standard variable)
* x-terminal-emulator
* urxvt
* rxvt
* st
* terminology
* qterminal
* Eterm
* aterm
* uxterm
* xterm
* roxterm
* xfce4-terminal.wrapper
* mate-terminal
* lxterminal
* konsole
* alacritty
* kitty
* wezterm
* foot


## SEE ALSO

sofi(1)

## AUTHORS

Dave Davenport and contributors

Copied script from i3:
Michael Stapelberg and contributors
