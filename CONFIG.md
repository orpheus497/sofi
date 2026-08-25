> This page does not describe all of **SOFI**'s configuration options, just the
> most common usecase. For the full configuration options, check the manpages.

<br />

Sofi works with no configuration file at all -- every surface has a layout
compiled into the binary. Everything below is optional, and overrides the
built-in defaults.

## Where does the configuration live

Sofi's configurations, custom themes live in `${XDG_CONFIG_HOME}/sofi/`, on
most systems this is `~/.config/sofi/`.

The name of the main configuration file is `config.sasi`. (`~/.config/sofi/config.sasi`).

## Create an empty configuration file

Open `~/.config/sofi/config.sasi` in your favorite text editor and add the
following block:

```css
configuration {

}
```

You can now set the options in the `configuration` block.

## Create a configuration file from current setup

If you do not want to start from scratch, you can tell sofi to dump its
configuration:

```bash
sofi -dump-config > ~/.config/sofi/config.sasi
```

This will have all the possible settings and their current value.
If a value is the default value, the entry will be commented.

For example:

```css
configuration {               
/*  modes: "window,run,ssh,drun";*/
/*  font: "mono 12";*/
/*  location: 0;*/
/*  yoffset: 0;*/
/*  xoffset: 0;*/
/*  fixed-num-lines: true;*/
... cut ...
/*  ml-row-down: "ScrollDown";*/                                                                                        
/*  me-select-entry: "MousePrimary";*/                                                                                  
/*  me-accept-entry: "MouseDPrimary";*/                                                                                 
/*  me-accept-custom: "Control+MouseDPrimary";*/ 
}
```

To create a copy of the current theme, you can run:

```bash
sofi -dump-theme > ~/.config/sofi/current.sasi
```

## Configuration file format

### Encoding

The encoding of the file is utf-8. Both Unix (`\n`) and windows (`\r\n`)
newlines format are supported. But Unix is preferred.

### Comments

C and C++ file comments are supported.

- Anything after  `//` and before a newline is considered a comment.
- Everything between `/*` and `*/` is a comment.

Comments can be nested and the C comments can be inline.

The following is valid:

```css
// Magic comment.
property: /* comment */ value;
```

However, this is not:

```css
prop/*comment*/erty: value;
```

### White space

White space and newlines, like comments, are ignored by the parser.

This:

```css
property: name;
```

Is identical to:

```css
     property             :
name

;
```

### Data types

**SOFI**'s configuration supports several data formats:

#### String

A string is always surrounded by double quotes (`"`). Between the quotes there
can be any printable character.

For example:

```css

 ml-row-down: "ScrollDown";
```

#### Number

An integer may contain any full number.

For example:

```css
eh: 2;                        
```

#### Boolean

Boolean value is either `true` or `false`. This is case-sensitive.

For example:

```css
show-icons: true;
```

This is equal to the `-show-icons` option on the commandline, and `show-icons:
false;` is equal to `-no-show-icons`.

#### List

A list starts with a '[' and ends with a ']'. The entries in the list are
comma-separated. The entry in the list single ASCII words.

```css
 combi-modes: [window,drun];
```

A comma-separated string is also accepted:

```css
 combi-modes: "window,drun";
```

## Get a list of all possible options

There are 2 ways to get a list of all options:

1. Dump the configuration file explained above. (`sofi -dump-config`)
1. Look at output of `sofi -h`.

To see what values an option support check the manpage, it describes most of
them.

NOTE: not all options might be in the manpage, as options can be added at
run-time. (f.e. by plugins).

## Splitting configuration over multiple files

It is possible to split configuration over multiple files using imports. For
example in `~/.config/sofi/config.sasi`

```css
configuration {
}
@import "myConfig"
@theme "MyTheme"

```

Sofi will first parse the config block in `~/.config/sofi/config.sasi`, then
parse `~/.config/sofi/myConfig.sasi` and then load the theme `myTheme`.  More
information can be obtained from the **sofi-theme(5)** manpage.  Imports can be
nested.
