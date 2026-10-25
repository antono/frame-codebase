"""
Draws the 137 most used emoji as U+XXXX.png in this directory, at their own
Unicode codepoints, so text from a phone shows them as they are.

The order is Unicode's emoji frequency list (2021,
https://home.unicode.org/emoji/emoji-frequency/), its first 137 that are a
single codepoint: no flags, skin tones or ZWJ sequences. An emoji sent with
U+FE0F after it (as in a red heart) still draws, since the font has no glyph
for the selector and text() skips it.

They are rendered from Noto Emoji (SIL OFL 1.1, fonts/NotoEmoji-OFL.txt),
the monochrome one, bold, two colors, 40 pixels tall in the 48 rows of a
text line, so they draw in the color text() is given.

    python3 emoji.py $(nix build --no-link --print-out-paths nixpkgs#noto-fonts-monochrome-emoji)/share/fonts/noto/NotoEmoji.ttf

Needs Pillow. Then build system_font.h as readme.md says.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

HEIGHT = 48
TOP = 4  # first row of the emoji, so it sits on the text's cap height
SIZE = 40  # rows from TOP
OVERSAMPLE = 4

EMOJI = (
    "😂❤😍🤣😊🙏💕😭😘👍😅👏😁♥🔥💔💖💙😢🤔😆🙄💪😉☺👌🤗💜😔😎😇🌹"
    "🤦🎉‼💞✌✨🤷😱😌🌸🙌😋💗💚😏💛🙂💓🤩😄😀🖤😃💯🙈👇🎶😒🤭❣❗😜💋"
    "👀😪😑💥🙋😞😩😡🤪👊☀😥🤤👉💃😳✋😚😝😴🌟😬🙃🍀🌷😻😓⭐✅🌈😈🤘💦"
    "✔😣🏃💐☹🎊💘😠☝😕🌺🎂🌻😐🖕💝🙊😹🗣💫💀👑🎵🤞😛🔴😤🌼😫⚽🤙☕🏆🧡"
    "🎁⚡🌞🎈❌"
)


def png(cp):
    return os.path.join(HERE, f"U+{cp:04X}.png")


def render(font, char):
    """The emoji fitted into SIZE by SIZE, as a 1 bit mask HEIGHT rows tall."""
    k = OVERSAMPLE
    big = Image.new("L", (SIZE * 2 * k, SIZE * 2 * k), 0)
    ImageDraw.Draw(big).text((SIZE * k, SIZE * k), char, font=font, fill=255, anchor="mm")
    big = big.crop(big.getbbox())
    scale = SIZE * k / max(big.size)
    w, h = round(big.width * scale / k), round(big.height * scale / k)
    small = big.resize((w, h), Image.LANCZOS).point(lambda v: 255 if v > 110 else 0)
    out = Image.new("L", (w, HEIGHT), 0)
    out.paste(small, (0, TOP + (SIZE - h) // 2))
    return out


def main():
    if len(EMOJI) != 137:
        sys.exit(f"{len(EMOJI)} emoji, not 137")
    font = ImageFont.truetype(sys.argv[1], SIZE * OVERSAMPLE)
    font.set_variation_by_name("Bold")

    for char in EMOJI:
        glyph = render(font, char)
        out = Image.new("RGBA", glyph.size, (0, 0, 0, 255))
        out.paste((255, 255, 255, 255), mask=glyph)
        out.save(png(ord(char)))

    print(f"{len(EMOJI)} emoji")


if __name__ == "__main__":
    main()
