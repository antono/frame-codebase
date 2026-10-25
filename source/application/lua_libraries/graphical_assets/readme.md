# Graphical Assets

This directory contains all of the graphical assets which come prebuilt into the Frame firmware. All of the `.png` files here are converted into `system_font.h` using the `create_sprites` tool which is a part of [`frameutils`](https://github.com/brilliantlabsAR/frame-utilities-for-python).

Install `frameutils` using `pip`:

```sh
pip3 install frameutils
```

Generate the font pack using the `create_sprites` command:

```sh
frameutils create_sprites -c 2 --header source/application/lua_libraries/graphical_assets source/application/lua_libraries/graphical_assets/system_font.h
```
## Monospace fonts

`mono_font.h`, the fonts of `frame.display.mono`, is not made from PNGs:
`mono.py` writes it from the Terminus Font source (BDF, SIL OFL 1.1,
`fonts/Terminus-OFL.txt`), printable ASCII in sizes 20, 24, 28 and 32,
normal and bold:

```sh
python3 mono.py $(nix build --no-link --print-out-paths nixpkgs#terminus_font.src)
```

## Emoji

The 137 most used emoji (Unicode's frequency list) are in the system font
at their own codepoints, two colors, 40 pixels tall. `emoji.py` draws
them as `U+XXXX.png` from Noto Emoji, the monochrome one (SIL OFL 1.1,
`fonts/NotoEmoji-OFL.txt`); then build `system_font.h` as above:

```sh
python3 emoji.py $(nix build --no-link --print-out-paths nixpkgs#noto-fonts-monochrome-emoji)/share/fonts/noto/NotoEmoji.ttf
```
