# sofi-keys(5)

## NAME

**sofi keys** - Sofi Key and Mouse bindings

## DESCRIPTION

**sofi** supports overriding of any of it key and mouse binding.

## Setting binding

Bindings can be done on the commandline (-{bindingname}):

```bash
sofi -show run -kb-accept-entry 'Control+Shift+space'
```

or via the configuration file:

```css
configuration {
  kb-accept-entry: "Control+Shift+space";
}
```

The key can be set by its name (see above) or its keycode:

```css
configuration {
  kb-accept-entry: "Control+Shift+[65]";
}
```

An easy way to look up keycode is xev(1).

Multiple keys can be specified for an action as a comma separated list:

```css
configuration {
  kb-accept-entry: "Control+Shift+space,Return";
}
```

By Default **sofi** reacts on pressing, to act on the release of all keys
prepend the binding with `!`:

```css
configuration {
  kb-accept-entry: "!Control+Shift+space,Return";
}
```

## Unsetting a binding

To unset a binding, pass an empty string.

```css
configuration {
  kb-clear-line: "";
}
```

## Keyboard Bindings

`kb-primary-paste`

Paste primary selection

Default:  Control+V,Shift+Insert

`kb-secondary-paste`

Paste clipboard

Default:  Control+v,Insert

`kb-secondary-copy`

Copy current selection to clipboard

Default:  Control+c

`kb-clear-line`

Clear input line

Default:  Control+w

`kb-move-front`

Beginning of line

Default:  Control+a

`kb-move-end`

End of line

Default:  Control+e

`kb-move-word-back`

Move back one word

Default:  Alt+b,Control+Left

`kb-move-word-forward`

Move forward one word

Default:  Alt+f,Control+Right

`kb-move-char-back`

Move back one char

Default:  Left,Control+b

`kb-move-char-forward`

Move forward one char

Default:  Right,Control+f

`kb-remove-word-back`

Delete previous word

Default:  Control+Alt+h,Control+BackSpace

`kb-remove-word-forward`

Delete next word

Default:  Control+Alt+d

`kb-remove-char-forward`

Delete next char

Default:  Delete,Control+d

`kb-remove-char-back`

Delete previous char

Default:  BackSpace,Shift+BackSpace,Control+h

`kb-remove-to-eol`

Delete till the end of line

Default:  Control+k

`kb-remove-to-sol`

Delete till the start of line

Default:  Control+u

`kb-transpose-chars`

Transpose (swap) the two characters before the cursor

Default:  Control+t

`kb-accept-entry`

Accept entry

Default:  Control+j,Control+m,Return,KP\_Enter

`kb-accept-custom`

Use entered text as command (in ssh/run modes)

Default:  Control+Return

`kb-accept-custom-alt`

Use entered text as command (in ssh/run modes)

Default:  Control+Shift+Return

`kb-accept-alt`

Use alternate accept command.

Default:  Shift+Return

`kb-delete-entry`

Delete entry from history

Default:  Shift+Delete

`kb-mode-next`

Switch to the next mode.

Default:  Shift+Right,Control+Tab

`kb-mode-previous`

Switch to the previous mode.

Default:  Shift+Left,Control+ISO\_Left\_Tab

`kb-mode-complete`

Start completion for mode.

Default:  Control+l

`kb-row-left`

Go to the previous column

Default:  Control+Page\_Up

`kb-row-right`

Go to the next column

Default:  Control+Page\_Down

`kb-row-up`

Select previous entry

Default:  Up,Control+p

`kb-row-down`

Select next entry

Default:  Down,Control+n

`kb-row-tab`

Go to next row, if one left, accept it, if no left next mode.

Default:

`kb-element-next`

Go to next row.

Default: Tab

`kb-element-prev`

Go to previous row.

Default: ISO\_Left\_Tab

`kb-page-prev`

Go to the previous page

Default:  Page\_Up

`kb-page-next`

Go to the next page

Default:  Page\_Down

`kb-row-first`

Go to the first entry

Default:  Home,KP\_Home

`kb-row-last`

Go to the last entry

Default:  End,KP\_End

`kb-row-select`

Set selected item as input text

Default:  Control+space

`kb-screenshot`

Take a screenshot of the sofi window

Default:  Alt+S

`kb-ellipsize`

Toggle between ellipsize modes for displayed data

Default:  Alt+period

`kb-toggle-case-sensitivity`

Toggle case sensitivity

Default:  grave,dead\_grave

`kb-toggle-sort`

Toggle filtered menu sort

Default:  Alt+grave

`kb-cancel`

Quit sofi

Default:  Escape,Control+g,Control+bracketleft,MouseSecondary

`kb-custom-1`

Custom keybinding 1

In `window` mode on the Wayland backend, this toggles minimise on the selected
window. In `sheets` mode, it sends the focused window to the highlighted sheet.

Default:  Alt+1

`kb-custom-2`

Custom keybinding 2

In `window` mode on the Wayland backend, this toggles maximise on the selected
window. On hikari-sakura, maximise maps onto full-maximize; there is no separate
fullscreen state.

Default:  Alt+2

`kb-custom-3`

Custom keybinding 3

Default:  Alt+3

`kb-custom-4`

Custom keybinding 4

Default:  Alt+4

`kb-custom-5`

Custom Keybinding 5

Default:  Alt+5

`kb-custom-6`

Custom keybinding 6

Default:  Alt+6

`kb-custom-7`

Custom Keybinding 7

Default:  Alt+7

`kb-custom-8`

Custom keybinding 8

Default:  Alt+8

`kb-custom-9`

Custom keybinding 9

Default:  Alt+9

`kb-custom-10`

Custom keybinding 10

Default:  Alt+0

`kb-custom-11`

Custom keybinding 11

Default:  Alt+exclam

`kb-custom-12`

Custom keybinding 12

Default:  Alt+at

`kb-custom-13`

Custom keybinding 13

Default:  Alt+numbersign

`kb-custom-14`

Custom keybinding 14

Default:  Alt+dollar

`kb-custom-15`

Custom keybinding 15

Default:  Alt+percent

`kb-custom-16`

Custom keybinding 16

Default:  Alt+dead\_circumflex

`kb-custom-17`

Custom keybinding 17

Default:  Alt+ampersand

`kb-custom-18`

Custom keybinding 18

Default:  Alt+asterisk

`kb-custom-19`

Custom Keybinding 19

Default:  Alt+parenleft

`kb-select-1`

Select row 1

Default:  Super+1

`kb-select-2`

Select row 2

Default:  Super+2

`kb-select-3`

Select row 3

Default:  Super+3

`kb-select-4`

Select row 4

Default:  Super+4

`kb-select-5`

Select row 5

Default:  Super+5

`kb-select-6`

Select row 6

Default:  Super+6

`kb-select-7`

Select row 7

Default:  Super+7

`kb-select-8`

Select row 8

Default:  Super+8

`kb-select-9`

Select row 9

Default:  Super+9

`kb-select-10`

Select row 10

Default:  Super+0

`kb-entry-history-up`

Go up in the entry history.

Default:    Control+Up

`kb-entry-history-down`

Go down in the entry history.

Default:    Control+Down

`kb-matcher-up`

Select the next matcher.

Default: Super+equal

`kb-matcher-down`

Select the previous matcher.

Default: Super+minus

## Mouse Bindings

`ml-row-left`

Go to the previous column

Default:  ScrollLeft

`ml-row-right`

Go to the next column

Default:  ScrollRight

`mt-activate`

Activate the hovered tray icon: open the item's menu when it published one, and
send `Activate` when it did not.

Default:  MousePrimary

`mt-context-menu`

Open the hovered tray icon's menu. When the item published none, its own
`ContextMenu` is called instead.

Default:  MouseSecondary

`mt-secondary-activate`

Send `SecondaryActivate` to the hovered tray icon.

Default:  MouseMiddle

`ml-row-up`

Select previous entry

Default:  ScrollUp

`ml-row-down`

Select next entry

Default:  ScrollDown

`me-select-entry`

Select hovered row

Default:  MousePrimary

`me-accept-entry`

Accept hovered row

Default:  MouseDPrimary

`me-accept-custom`

Accept hovered row with custom action

Default:  Control+MouseDPrimary

## Mouse key bindings

The following mouse buttons can be bound:

* `Primary`: Primary (Left) mouse button click.
* `Secondary`:  Secondary (Right) mouse button click.
* `Middle`: Middle mouse button click.
* `Forward`: The forward mouse button.
* `Back`: The back mouse button.
* `ExtraN`: The N'the mouse button. (Depending on mouse support).

The Identifier is constructed as follow:

`Mouse<D><Button>`

* `D` indicates optional Double press.
* `Button` is the button name.

So `MouseDPrimary` is Primary (`Left`) mouse button double click.

## SEE ALSO

sofi(1), sofi-sensible-terminal(1), sofi-theme(5), sofi-script(5)

## AUTHOR

sofi is maintained by orpheus497 <orpheus497@gmail.com>.

It is a hard fork of rofi and carries code by its authors and by the authors of
simpleswitcher before it. See the `AUTHORS` file for the full list, and `COPYING`
for the licence.
