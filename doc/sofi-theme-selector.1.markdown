# sofi-theme-selector(1)

## NAME

**sofi-theme-selector** - Preview and apply themes for **sofi**

## DESCRIPTION

**sofi-theme-selector** is a bash/sofi script to preview and apply themes for
**sofi**. It's part of any installation of **sofi**.

## USAGE

### Running sofi-theme-selector

**sofi-theme-selector** shows a list of all available themes in a **sofi**
window. It lets you preview each theme with the Enter key and apply the theme
to your **sofi** configuration file with Alt+a.

## Theme directories

**sofi-theme-selector** searches the following directories for themes:

- ${PREFIX}/share/sofi/themes
- $XDG_CONFIG_HOME/sofi/themes
- $XDG_DATA_HOME/share/sofi/themes

${PREFIX} reflects the install location of sofi. In most cases this will be
"/usr".<br>
$XDG_CONFIG_HOME is normally unset. Default path is "$HOME/.config".<br>
$XDG_DATA_HOME is normally unset. Default path is "$HOME/.local/share".

## SEE ALSO

sofi(1)

## AUTHORS

sofi is maintained by orpheus497 <orpheus497@gmail.com>.

It is a hard fork of rofi and carries code by its authors and by the authors of
simpleswitcher before it. See the `AUTHORS` file for the full list, and `COPYING`
for the licence.
