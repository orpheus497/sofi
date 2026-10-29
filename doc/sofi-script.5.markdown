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

## Lua modes

A mode can be written in Lua instead of as an executable script. **A Lua mode
is a `.lua` file that sofi cannot run as a program.** If you `chmod +x` it,
sofi runs it, which is what the execute bit means -- so a `#!/usr/bin/env lua`
script keeps working exactly as it does today.

Precisely: the mode's command line is read the way a shell would read it, and
it is a Lua mode only if it is a single word ending in `.lua` with nothing
executable at it. Everything else is spawned, as before. So all three of these
keep working and none of them becomes a Lua mode:

```bash
sofi -modes "bm:/usr/bin/lua5.4 ~/bookmarks.lua"   # it has an argument
sofi -modes "notes:notes.lua"                      # it is on $PATH
sofi -modes 'x:~/my\ notes/x.lua'                  # it is executable
```

A bare name is looked for on `$PATH` and nowhere else, so where you started
sofi from never changes the answer.

Everything in **Script locations** above applies unchanged. Drop the file in
`$XDG_CONFIG_HOME/sofi/scripts/` and it is a mode named after the filename
without its extension, it appears in `sofi -h` tagged `[lua]`, and it can be a
`-combi-modes` member. The `<name>:<path>` form works too:

```bash
cp bookmarks.lua ~/.config/sofi/scripts/      # do NOT chmod +x
sofi -show bookmarks

sofi -modes "bm:~/lua/bookmarks.lua" -show bm
sofi -show combi -combi-modes "drun,bookmarks"
```

A complete, working example ships as `Examples/bookmarks.lua`.

### The contract

The file is loaded as a Lua chunk and run once. It must return a table with one
function:

```lua
entries(state) -> list | nil
```

The `state` table is fresh on every call, and carries the same five pieces of
state the environment variables above carry:

-   **state.retv**:  the same integer as `SOFI_RETV`.

-   **state.entry**: the selected row's `entry`, or the typed input.

-   **state.info**:  the selected row's `info`, or nil.

-   **state.data**:  whatever the previous call set as `data`, or nil.

-   **state.input**: the text currently typed, or nil.

There is no counterpart to `SOFI_OUTSIDE`. That variable exists so a child
process can refuse to launch sofi inside sofi, and a Lua mode has no child.

**Returning nil closes sofi**, exactly as a script that prints nothing does. An
empty table does the same thing.

Otherwise the return value is one table, and it is the line protocol with the
bytes removed: **its array part is the rows, and its string keys are the header
options.** An entry line becomes an array element, that line's `\x1f` extras
become the element's fields, and a `\0key\x1fvalue` header line becomes a string
key on the outer table.

### Rows

A row is a string, which is shorthand for `{ entry = s }`, or a table:

-   **entry**:         required string. What is matched, and what comes back as
    `state.entry`.

-   **display**:       what is shown instead of `entry`.

-   **icon**:          a comma-separated string, or an array of names tried in
    order. Lua names icons; sofi resolves them, so a mode never deals with an
    image.

-   **meta**:          searched, never shown.

-   **info**:          shown nowhere; arrives as `state.info`.

-   **urgent**, **active**, **nonselectable**, **permanent**: booleans.

A row with no string `entry` is reported and skipped, and the rest of the list
is still shown -- a typo in row 400 of 500 does not blank the menu.

Build the array with `#list + 1` or `table.insert` and never by assigning sparse
indices: the array part is read up to its first hole.

### Header options

The string keys use **the protocol's own spelling, hyphens included**, so that
porting a script mode is a textual change and there is one vocabulary rather
than two:

```lua
return {
  "one", "two",
  prompt = "my mode",
  message = "<b>Return</b> to pick",
  ["markup-rows"] = true,
  ["keep-selection"] = true,
}
```

All of the options in **Passing mode options** above are accepted except
`delim`, which is deliberately absent: it names the byte that separates entries
in a stream, and a Lua mode has no stream. Values may be a string, a number or a
boolean; anything else is reported and ignored.

As with a script, `message = ""` clears the message.

### The sofi table

One global table is added. It is small on purpose:

-   **sofi.exec(cmd [, in_terminal])**: launch `cmd` without waiting. Use this
    rather than `os.execute`, which blocks -- and a block inside a mode is a
    block inside sofi's main loop. This is the same call `run` and `drun` make,
    so a mode inherits their terminal and startup-notification handling.

-   **sofi.log(level, message)**: `level` is `"debug"`, `"warning"` or anything
    else for an ordinary message.

-   **sofi.markup_escape(s)**: escape text for a `markup-rows` display. Worth
    using: a row whose markup will not parse silently stops matching rather than
    stops rendering.

-   **sofi.shell_quote(s)**: quote a value for `sofi.exec`, which hands its
    argument to a shell-style parser.

-   **sofi.name**: this mode's name.

-   **sofi.config_dir**: `$XDG_CONFIG_HOME/sofi`.

A mode's own directory is prepended to `package.path`, so a mode that outgrows
one file can `require` its parts from beside it.

### What a Lua mode may do

The rule is one sentence: **a Lua mode gets every capability a script mode has,
and does not get the capabilities that exist only because it shares sofi's
process.** An executable in the scripts directory is spawned with your whole
environment and no restriction of any kind; a Lua mode in the same directory,
written by the same person, is the same trust class. So `io`, `os.execute`,
`io.popen`, `setmetatable`, `pcall`, `coroutine`, `utf8` and `require` for
pure-Lua modules are all present and are not going to be taken away.

Five things are removed, and the test for each is whether a spawned child could
have done it to sofi:

-   **os.exit**: would terminate sofi rather than a child, skipping its cleanup,
    its surface teardown and its pid file.

-   **os.setlocale**: is process-global, and would change number parsing and
    text shaping for every other part of sofi.

-   **debug**, reduced to `traceback`: `debug.sethook` shares one hook slot with
    the deadline below and clears it, which a mode using it for its own tracing
    would do by accident; the rest of the library reaches into sofi's own
    functions, upvalues and stack.

-   **load**, **loadfile**, **dofile**: Lua does not verify bytecode, so
    compiling a malformed binary chunk crashes sofi's interpreter. A mode's own
    file is already a chunk.

-   **package.loadlib**, and the two C module searchers: a shared library loaded
    through `require` would be native code inside sofi with none of the version
    checking a real plugin goes through.

**Nothing reaches this interpreter except text.** A mode's own file is loaded
in text mode, and so is anything it `require`s -- the stock module searcher
accepts a compiled chunk and has been replaced with one that does not, because
`string.dump` and `io` are both present and a mode could otherwise have
produced the bytecode itself.

### The deadline

A mode gets **two seconds** inside one interaction. Past that it is stopped and
the error appears as a row.

**It cannot be caught.** The mode runs on a coroutine and the deadline suspends
it rather than raising an error, because an error is something `pcall` traps --
a mode doing `while true do pcall(f) end` would otherwise discard the deadline
on every sample and run for as long as it liked. Lua propagates a suspension
through `pcall` rather than trapping it, so there is nothing a mode can wrap
its work in to survive the deadline.

This is not a restriction borrowed from somewhere else -- it is strictly more
than a script mode gets. Sofi reads a script's standard output in the thread
that drew the menu, so **a script that wedges without closing its output blocks
sofi for as long as it likes, today, with no bound at all.** Two seconds is
roughly three times what building a hundred thousand rows costs, so no honest
list comes near it.

Three things it does not bound, stated rather than sold past:

- A blocking call into C. The deadline counts Lua work, so it does not fire
  while `os.execute` waits for a browser or a read waits on a dead network
  mount. This is why `sofi.exec` exists.
- A finalizer that loops. Lua runs finalizers with hooks disabled, so no
  deadline can reach one.
- It bounds one call, not one keystroke. That is enough here because a mode is
  entered exactly once per interaction and never per row -- row matching stays
  in C, which is also why a Lua mode cannot replace the matcher.

### Errors

A mode's error is a typo in a file you are editing, so it is reported where you
are looking: as a single row naming the file and the line, with the traceback on
standard error under the `Modes.Script` domain. Fix the file and run
`sofi -show` again.

A file that will not load, or that does not return a table with an `entries`
function, is reported once and not retried. An error raised by one call is
reported for that interaction only, and the next one calls the mode again -- a
failing branch does not break a mode whose other branches work.

A long-running `sofi -daemon` reads a mode's chunk exactly once, so an edit is
picked up on restart. A one-shot `sofi -show` reads it fresh every time.

One thing to know about the scripts directory: **every** file in it is listed
as a mode, which has always been true. So a `.lua` file you keep there as a
library for another mode is listed too, and showing it reports that it has no
`entries` function. Put a library beside the mode that `require`s it -- a
mode's own directory is on `package.path`, and the scripts directory is that
directory -- and simply do not show it.

## SEE ALSO

sofi(1), sofi-sensible-terminal(1), dmenu(1), sofi-theme(5),
sofi-theme-selector(1)

## AUTHOR

sofi is maintained by orpheus497 <orpheus497@gmail.com>.

It is a hard fork of rofi and carries code by its authors and by the authors of
simpleswitcher before it. See the `AUTHORS` file for the full list, and `COPYING`
for the licence.
