"""
Draws the Cyrillic glyphs of the system font as U+XXXX.png in this directory.

Letters that look the same as a Latin glyph already here (A, B, E, ...) are
copies of it. The rest are rendered from Play (fonts/, SIL OFL 1.1), sized
and narrowed to match the Latin: capitals and lowercase are fitted apart,
each to the width of comparable Latin letters, and marks above capitals are
squeezed into the six rows the Latin accents use.

    python3 cyrillic.py        # needs Pillow

Then build system_font.h as readme.md says.
"""

import os
import shutil
import unicodedata

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "fonts", "Play-Regular.ttf")

HEIGHT = 48
BASELINE = 40
CAP_TOP = 6  # top of capitals and ascenders
X_TOP = 16  # top of lowercase
OVERSAMPLE = 4
BOLD = 1  # dilation at the oversampled size, to match the Latin stems

# Who needs the letters beyond U+0400..U+045F; each is a capital and its
# lowercase at the next codepoint.
EXTRA = {
    0x0490: "uk",  # Ґ
    0x0492: "kk tg uz ba",  # Ғ
    0x0494: "sah",  # Ҕ
    0x0496: "tt",  # Җ
    0x0498: "ba",  # Ҙ
    0x049A: "kk tg uz",  # Қ
    0x04A0: "ba",  # Ҡ
    0x04A2: "kk ky tt ba",  # Ң
    0x04A4: "sah",  # Ҥ
    0x04AA: "ba cv",  # Ҫ
    0x04AE: "kk ky tt ba mn sah",  # Ү
    0x04B0: "kk",  # Ұ
    0x04B2: "tg uz",  # Ҳ
    0x04B6: "tg",  # Ҷ
    0x04BA: "kk tt ba sah",  # Һ
    0x04C1: "mo",  # Ӂ
    0x04D0: "cv",  # Ӑ
    0x04D6: "cv",  # Ӗ
    0x04D8: "kk tt ba",  # Ә
    0x04E2: "tg",  # Ӣ
    0x04E8: "kk ky tt ba mn sah",  # Ө
    0x04EE: "tg",  # Ӯ
    0x04F2: "cv",  # Ӳ
}

CODEPOINTS = (
    list(range(0x0400, 0x0460))  # ru uk be bg sr mk
    + [c + i for c in EXTRA for i in (0, 1)]
    + [0x04C0, 0x04CF]  # Ӏ ӏ, palochka: Caucasian languages
    + [0x2116]  # №
)

# Cyrillic letters drawn exactly like a Latin glyph
SAME = {
    0x0400: 0xC8, 0x0401: 0xCB, 0x0405: 0x53, 0x0406: 0x49, 0x0407: 0xCF,
    0x0408: 0x4A, 0x0410: 0x41, 0x0412: 0x42, 0x0415: 0x45, 0x041A: 0x4B,
    0x041C: 0x4D, 0x041D: 0x48, 0x041E: 0x4F, 0x0420: 0x50, 0x0421: 0x43,
    0x0422: 0x54, 0x0425: 0x58, 0x0430: 0x61, 0x0435: 0x65, 0x043E: 0x6F,
    0x0440: 0x70, 0x0441: 0x63, 0x0443: 0x79, 0x0445: 0x78, 0x0450: 0xE8,
    0x0451: 0xEB, 0x0455: 0x73, 0x0456: 0x69, 0x0457: 0xEF, 0x0458: 0x6A,
    0x04AE: 0x59, 0x04BB: 0x68, 0x04C0: 0x49,
}

# Latin the drawn letters are fitted to
FIT_UPPER = "BDEFHKLNPRTUZ"
FIT_LOWER = "bdhknpqu"


def png(cp):
    return os.path.join(HERE, f"U+{cp:04X}.png")


def latin(cp):
    image = Image.open(png(cp)).convert("L")
    return image.point(lambda v: 255 if v > 127 else 0)


def crop(image):
    box = image.getbbox()
    return image.crop((box[0], 0, box[2], image.height))


class Face:
    """The font at the size where `ref` spans `top` to the baseline."""

    PAD = 24  # rows above the glyph, for marks that rise too high

    def __init__(self, ref, top, narrow=1.0):
        self.narrow = narrow
        size = 10
        want = (BASELINE - top) * OVERSAMPLE
        while -ImageFont.truetype(FONT, size).getbbox(ref, anchor="ls")[1] < want:
            size += 1
        self.font = ImageFont.truetype(FONT, size)

    def render(self, cp):
        k, pad = OVERSAMPLE, self.PAD
        big = Image.new("L", (160 * k, (HEIGHT + pad) * k), 0)
        ImageDraw.Draw(big).text(
            (20 * k, (BASELINE + pad) * k), chr(cp), font=self.font, fill=255, anchor="ls"
        )
        big = big.filter(ImageFilter.MaxFilter(BOLD * 2 + 1))
        small = big.resize((round(big.width / k * self.narrow), HEIGHT + pad), Image.LANCZOS)
        return small.point(lambda v: 255 if v > 110 else 0)


def fit(ref, top, letters):
    """A face whose glyphs are as wide as these Latin letters."""
    face = Face(ref, top)
    drawn = sum(crop(face.render(ord(c))).width for c in letters)
    target = sum(latin(ord(c)).width for c in letters)
    return Face(ref, top, target / drawn)


def squeeze_marks(image):
    """Moves a mark rising above the glyph area into rows 0..CAP_TOP-2."""
    pad = Face.PAD
    box = image.getbbox()
    if box[1] >= pad:
        return image.crop((0, pad, image.width, pad + HEIGHT))

    def blank(y):
        return not any(image.getpixel((x, y)) for x in range(image.width))

    # the mark ends at the first empty row above the body
    end = box[1]
    while not blank(end):
        end += 1
    mark = image.crop((0, box[1], image.width, end))
    room = CAP_TOP - 1
    if mark.height > room:
        mark = mark.resize((mark.width, room), Image.LANCZOS)
        mark = mark.point(lambda v: 255 if v > 110 else 0)

    out = image.crop((0, pad, image.width, pad + HEIGHT))
    ImageDraw.Draw(out).rectangle((0, 0, out.width, CAP_TOP - 1), fill=0)
    out.paste(mark, (0, room - mark.height), mark)
    return out


def main():
    upper = fit("H", CAP_TOP, FIT_UPPER)
    lower = fit("x", X_TOP, FIT_LOWER)

    for cp in CODEPOINTS:
        if cp in SAME:
            shutil.copyfile(png(SAME[cp]), png(cp))
            continue
        face = upper if unicodedata.category(chr(cp)) == "Lu" or cp == 0x2116 else lower
        glyph = crop(squeeze_marks(face.render(cp)))
        out = Image.new("RGBA", glyph.size, (0, 0, 0, 255))
        out.paste((255, 255, 255, 255), mask=glyph)
        out.save(png(cp))

    print(f"{len(CODEPOINTS)} glyphs, {len(SAME)} of them copies of Latin")


if __name__ == "__main__":
    main()
