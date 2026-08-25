# sofi-script(5)

## NAME

**sofi script mode** - Sofi format for scriptable mode.

## DESCRIPTION

**sofi** supports modes that use simple scripts in the background to generate a
list and process the result from user actions.  This provide a simple interface
to make simple extensions to sofi.

## USAGE

To specify a script mode, set a mode with the following syntax:
"{name}:{executable}"

For example:

```bash
sofi -show fb -modes "fb:file_browser.sh"
```

The name should be unique.

## API

Sofi calls the executable without arguments on startup.  This should generate a
list of options, separated by a newline (`\n`) (This can be changed by the
script). If the user selects an option, sofi calls the executable with the text
of that option as the first argument. If the script returns no entries, sofi
quits.

A simple script would be:

```bash
#!/usr/bin/env bash

if [ x"$@" = x"quit" ]
then
    exit 0
fi
echo "reload"
echo "quit"

```

This shows two entries, reload and quit. When the quit entry is selected, sofi
closes.

## Environment

Sofi sets the following environment variable when executing the script:

### `SOFI_RETV`

An integer number with the current state:

- **0**: Initial call of script.
- **1**: Selected an entry.
- **2**: Selected a custom entry.
- **3**: Deleted an entry.
- **10-28**: Custom keybinding 1-19 ( need to be explicitly enabled by script ).

### `SOFI_INFO`

Environment get set when selected entry get set with the property value of the
'info' row option, if set.

### `SOFI_DATA`

Environment get set when script sets `data` option in header.

### `SOFI_INPUT`

The original input string from user.

## Passing mode options

Extra options, like setting the prompt, can be set by the script. Extra options
are lines that start with a NULL character (`\0`) followed by a key, separator
(`\x1f`) and value.

For example to set the prompt:

```bash
    echo -en "\0prompt\x1fChange prompt\n"
```

The following extra options exists:

-   **prompt**:      Update the prompt text.

-   **message**:     Update the message text.

-   **markup-rows**: If 'true' renders markup in the row.

-   **urgent**:      Mark rows as urgent. (for syntax see the urgent option in
    dmenu mode)

-   **active**:      Mark rows as active. (for syntax see the active option in
    dmenu mode)

-   **delim**:       Set the delimiter for for next rows. Default is '\n' and
    this option should finish with this. Only call this on first call of script,
    it is remembered for consecutive calls.

-   **no-custom**:   If set to 'true'; only accept listed entries, ignore custom
    input.

-   **use-hot-keys**: If set to true, it enabled the Custom keybindings for
    script. Warning this breaks the normal sofi flow.

-   **keep-selection**: If set, the selection is not moved to the first entry,
    but the current position is maintained. The filter is cleared.

-   **keep-filter**: If set, the filter is not cleared.

-   **new-selection**: If `keep-selection` is set, this allows you to override
    the selected entry (absolute position).

-   **data**:         Passed data to the next execution of the script via
    **SOFI\_DATA**.

-   **theme**:       Small theme snippet to f.e. change the background color of
    a widget.

-   **switch-mode**:  Switches to the given mode if enabled, otherwise ignored.

The **theme** property cannot change the interface while running, it is only
usable for small changes in, for example background color, of widgets that get
updated during display like the row color of the listview.

## Parsing row options

Extra options for individual rows can be set. The extra option can be specified
following the same syntax as mode option, but following the entry.

For example:

```bash
    echo -en "aap\0icon\x1ffolder\n"
```

The following options are supported:

-   **icon**: Set the icon for that row. Multiple fallback icons can be specified using comma-separated values.

-   **display**: Replace the displayed string. (Original string will still be used for filtering)

-   **meta**: Specify invisible search terms used for filtering.

-   **nonselectable**: If true the row cannot activated.

-   **permanent**: If true the row always shows, independent of filter.

-   **info**: Info that, on selection, gets placed in the `SOFI_INFO`
    environment variable. This entry does not get searched for filtering.

-   **urgent**: Set urgent flag on entry (true/false)

-   **active**: Set active flag on entry (true/false)

multiple entries can be passed using the `\x1f` separator.

```bash
    echo -en "aap\0icon\x1ffolder,inode-directory\x1finfo\x1ftest\n"
```

## Executing external program

If you want to launch an external program from the script, you need to make
sure it is launched in the background. If not sofi will wait for its output (to
display).

In bash the best way to do this is using `coproc`.

```bash
 coproc ( myApp  > /dev/null  2>&1 )
```

## DASH shell

If you use the `dash` shell for your script, take special care with how dash
handles escaped values for the separators. See issue #1201 on github.

## Script locations

To specify a script there are the following options:

- Specify an absolute path to the script.
- The script is executable and located in your $PATH

Scripts located in the following location are **loaded** on startup
and can be directly launched based on the filename (without extension):

- The script is in `$XDG_CONFIG_HOME/sofi/scripts/`, this is usually
  `~/.config/sofi/scripts/`.

If you have a script 'mymode.sh' in this folder you can open it using:

```bash
sofi -show mymode
```

See `sofi -h` output for a list of detected scripts.

## SEE ALSO

sofi(1), sofi-sensible-terminal(1), dmenu(1), sofi-theme(5),
sofi-theme-selector(1)

## AUTHOR

sofi is maintained by orpheus497 <orpheus497@gmail.com>.

It is a hard fork of rofi and carries code by its authors and by the authors of
simpleswitcher before it. See the `AUTHORS` file for the full list, and `COPYING`
for the licence.
