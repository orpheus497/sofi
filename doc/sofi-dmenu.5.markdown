# sofi-dmenu(5)

## NAME

**sofi dmenu mode** - Sofi dmenu emulation

## DESCRIPTION

To integrate **sofi** into scripts as simple selection dialogs, 
**sofi** supports emulating **dmenu(1)** (A dynamic menu for X11).

The website for `dmenu` can be found [here](http://tools.suckless.org/dmenu/).

**sofi** does not aim to be 100% compatible with `dmenu`. There are simply too
many flavors of `dmenu`. The idea is that the basic usage command-line flags
are obeyed, theme-related flags are not. Besides, **sofi** offers some extended
features (like multi-select, highlighting, message bar, extra key bindings).

## BASIC CONCEPT

In `dmenu` mode, **sofi** reads data from standard in, splits them into
separate entries and displays them. If the user selects a row, this is printed
out to standard out, allowing the script to process it further.

By default separation of rows is done on new lines, making it easy to pipe the
output a one application into **sofi** and the output of sofi into the next.

## USAGE 

By launching **sofi** with the `-dmenu` flag it will go into dmenu emulation
mode.

```bash
ls | sofi -dmenu
```

### DMENU DROP-IN REPLACEMENT

If `argv[0]` (calling command) is dmenu, **sofi** will start in dmenu mode.
This way, it can be used as a drop-in replacement for dmenu. Just copy or
symlink **sofi** to dmenu in `$PATH`.

```bash
ln -s /usr/bin/sofi /usr/bin/dmenu
```

### DMENU VS SCRIPT MODE

Script mode is used to extend **sofi**, dmenu mode is used to extend a script.
The two do share much of the same input format. Please see the
**sofi-script(5)** manpage for more information.

### DMENU SPECIFIC COMMANDLINE FLAGS

A lot of these options can also be modified by the script using special input.
See the **sofi-script(5)** manpage for more information about this syntax.

`-sep` *separator*

Separator for `dmenu`. Example: To show a list of 'a' to 'e' with '|' as a
separator:

```bash
echo "a|b|c|d|e" | sofi -sep '|' -dmenu
```

`-p` *prompt*

Specify the prompt to show in `dmenu` mode. For example, select 'monkey',
a,b,c,d, or e.

```bash
echo "a|b|c|d|e" | sofi -sep '|' -dmenu -p "monkey"
```

Default: *dmenu*

`-l` *number of lines to show*

Maximum number of lines the menu may show before scrolling.

```bash
sofi -dmenu -l 25
```

Default: *15*

`-i`

Makes `dmenu` searches case-insensitive

`-a` *X*

Active row, mark *X* as active. Where *X* is a comma-separated list of
python(1)-style indices and ranges, e.g.  indices start at 0, -1 refers to the
last row with -2 preceding it, ranges are left-open and right-close, and so on.
You can specify:

- A single row: '5'
- A range of (last 3) rows: '-3:'
- 4 rows starting from row 7: '7:11' (or in legacy notation: '7-10')
- A set of rows: '2,0,-9'
- Or any combination: '5,-3:,7:11,2,0,-9'

`-u` *X*

Urgent row, mark *X* as urgent. See `-a` option for details.

`-only-match`

Only return a selected item, do not allow custom entry.
This mode always returns an entry. It will not return if no matching entry is
selected.

`-no-custom`

Only return a selected item, do not allow custom entry.
This mode returns directly when no entries given.

`-format` *format*

Allows the output of dmenu to be customized (N is the total number of input
entries):

- 's' selected string
- 'i' index (0 - (N-1))
- 'd' index (1 - N)
- 'q' quote string
- 'p' Selected string stripped from Pango markup (Needs to be a valid string)
- 'f' filter string (user input)
- 'F' quoted filter string (user input)

Default: 's'

`-select` *string*

Select first line that matches the given string

`-mesg` *string*

Add a message line below the filter entry box. Supports Pango markup. For more
information on supported markup, see
[here](https://docs.gtk.org/Pango/pango_markup.html)

`-dump`

Dump the filtered list to stdout and quit.
This can be used to get the list as **sofi** would filter it.
Use together with `-filter` command.

`-input` *file*

Reads from *file* instead of stdin.

`-password`

Hide the input text. This should not be considered secure!

`-markup-rows`

Tell **sofi** that DMenu input is Pango markup encoded, and should be rendered.
See [here](https://docs.gtk.org/Pango/pango_markup.html)
for details about Pango markup.

`-multi-select`

Allow multiple lines to be selected. Adds a small selection indicator to the
left of each entry.

`-sync`

Force **sofi** mode to first read all data from stdin before showing the
selection window. This is original dmenu behavior.

Note: the default asynchronous mode will also be automatically disabled if used
with conflicting options,
such as `-dump`, `-only-match` or `-auto-select`.

`-window-title` *title*

Set name used for the window title. Will be shown as Sofi - *title*

`-w` *windowid*

Position **sofi** over the window with the given X11 window ID.

`-keep-right`

Set ellipsize mode to start. So, the end of the string is visible.

`-display-columns`

A comma separated list of columns to show.

`-display-column-separator`

The column separator. This is a regex. 

*default*: '\t'

`-ballot-selected-str` *string*

When multi-select is enabled, prefix this string when element is selected.

*default*: "☑ "

`-ballot-unselected-str` *string*

When multi-select is enabled, prefix this string when element is not selected.

*default*: "☐ "

`-ellipsize-mode` (start|middle|end)

Set ellipsize mode on the listview.

*default* "end"

## PARSING ROW OPTIONS

Extra options for individual rows can be also set. See the **sofi-script(5)**
manpage for details; the syntax and supported features are identical.

## RETURN VALUE

- **0**: Row has been selected accepted by user.
- **1**: User cancelled the selection.
- **10-28**: Row accepted by custom keybinding.

## SEE ALSO

sofi(1), sofi-sensible-terminal(1), dmenu(1), sofi-theme(5), sofi-script(5),
sofi-theme-selector(1), ascii(7)

## AUTHOR

sofi is maintained by orpheus497 <orpheus497@gmail.com>.

It is a hard fork of rofi and carries code by its authors and by the authors of
simpleswitcher before it. See the `AUTHORS` file for the full list, and `COPYING`
for the licence.
