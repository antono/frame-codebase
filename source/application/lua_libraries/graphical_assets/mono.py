"""
Writes mono_font.h: the monospace fonts of frame.display.mono, printable
ASCII from Terminus Font (SIL OFL 1.1, fonts/Terminus-OFL.txt), in four
sizes, each normal and bold. Terminus is drawn by hand pixel by pixel, so
it needs no resizing: a cell is size/2 by size pixels.

    python3 mono.py $(nix build --no-link --print-out-paths nixpkgs#terminus_font.src)

The argument is the Terminus source tarball (or its unpacked folder); the
BDF files in it are read as they are.
"""

import os
import sys
import tarfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "mono_font.h")

SIZES = (20, 24, 28, 32)
WEIGHTS = (("n", False), ("b", True))
FIRST, LAST = 0x20, 0x7E


def read_bdfs(source):
    """{name: text} for the BDF files wanted, from a tarball or a folder."""
    wanted = {f"ter-u{s}{w}.bdf" for s in SIZES for w, _ in WEIGHTS}
    found = {}
    if os.path.isdir(source):
        for name in wanted:
            with open(os.path.join(source, name)) as f:
                found[name] = f.read()
    else:
        with tarfile.open(source) as tar:
            for member in tar.getmembers():
                name = os.path.basename(member.name)
                if name in wanted:
                    found[name] = tar.extractfile(member).read().decode("ascii")
    missing = wanted - found.keys()
    if missing:
        sys.exit(f"not in {source}: {', '.join(sorted(missing))}")
    return found


def parse(text):
    """(width, height, {codepoint: [row bits, MSB the leftmost pixel]})."""
    width = height = None
    glyphs = {}
    lines = iter(text.splitlines())
    for line in lines:
        if line.startswith("FONTBOUNDINGBOX"):
            width, height, _, _ = map(int, line.split()[1:])
        elif line.startswith("ENCODING"):
            cp = int(line.split()[1])
        elif line.startswith("BBX"):
            if tuple(map(int, line.split()[1:3])) != (width, height):
                sys.exit(f"U+{cp:04X}: not a full cell")
        elif line == "BITMAP":
            hex_width = (width + 7) // 8 * 2
            rows = [int(next(lines), 16) for _ in range(height)]
            # left-align in 16 bits whatever the hex padding was
            shift = 16 - hex_width * 4
            glyphs[cp] = [(r << shift) & 0xFFFF if shift >= 0 else r >> -shift for r in rows]
    return width, height, glyphs


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    bdfs = read_bdfs(sys.argv[1])

    out = [
        "// Made by mono.py from Terminus Font 4.49.1, Copyright (C) 2020",
        "// Dimitar Toshkov Zhekov, SIL Open Font License 1.1",
        "// (fonts/Terminus-OFL.txt). Do not edit; run mono.py again.",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
        f"#define MONO_FIRST 0x{FIRST:02X}",
        f"#define MONO_LAST 0x{LAST:02X}",
        "",
        "// A glyph is `height` rows, each a uint16_t with the leftmost pixel in",
        "// the top bit; glyph c starts at rows[(c - MONO_FIRST) * height].",
        "typedef struct mono_font_t",
        "{",
        "    uint8_t size;",
        "    bool bold;",
        "    uint8_t width;",
        "    uint8_t height;",
        "    const uint16_t *rows;",
        "} mono_font_t;",
        "",
    ]
    fonts = []
    for size in SIZES:
        for w, bold in WEIGHTS:
            name = f"ter-u{size}{w}"
            width, height, glyphs = parse(bdfs[name + ".bdf"])
            if width > 16 or height != size:
                sys.exit(f"{name}: {width}x{height}")
            ident = f"mono_{size}{w}"
            out.append(f"static const uint16_t {ident}[] = {{")
            for cp in range(FIRST, LAST + 1):
                rows = ", ".join(f"0x{r:04X}" for r in glyphs[cp])
                out.append(f"    {rows}, // {chr(cp)!r}")
            out.append("};")
            out.append("")
            fonts.append(f"    {{{size}, {'true' if bold else 'false'}, {width}, {height}, {ident}}},")

    out.append("static const mono_font_t mono_fonts[] = {")
    out.extend(fonts)
    out.append("};")
    out.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print(f"{OUT}: {len(fonts)} fonts, {LAST - FIRST + 1} glyphs each")


if __name__ == "__main__":
    main()
