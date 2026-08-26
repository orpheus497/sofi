Manpages are built from the `.markdown` files in this directory with
[pandoc](https://pandoc.org/) (>= 2.9), as part of the ordinary build. The
generated pages are **not** committed — edit the markdown, never the roff.

To build them on their own:

```
ninja -C build generate-manpage
```

Without pandoc the build still succeeds and the `generate-manpage` target does
not exist. Meson then falls back to pre-generated roff files sitting next to the
markdown in this directory; none are committed, so it warns and installs no
manpages at all.
