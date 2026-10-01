"""The game's serif lettering: CHCrossword's anti-aliased capitals (its
tools/tilefont.py: DejaVu Serif Bold at 12 px, capitals 9 pixels tall),
with the figures and stops this game also prints drawn the same way. At
size 12 the capitals come out identical to CHCrossword's
tools/art/tilefont.txt.

    python tools/aafont.py [--ttf PATH] [--size 12] [--half 80]
    python tools/aafont.py --preview                 # out/aafont.png: sizes compared

Writes tools/art/aafont.txt, which tools/assets.py packs and which can be
touched up by hand afterwards (this only needs running again to start over
from the typeface). DejaVu is freely redistributable (see NOTICE). Each
glyph: a line "= c", then its rows from the capitals' top: '#' ink, '+'
half ink (drawn in a tone between the letter's colour and what it is on),
'.' clear. The ink is the typeface's own hinted one-bit rendering, so stems
stay crisp; the half tones are where its smooth rendering covers at least
--half of 255 of a pixel the one-bit one left clear: the curves and
diagonals. J's hook is brought up onto the baseline, and nothing goes more
than two rows below it (Q's tail).
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
FONTS = ["C:/Windows/Fonts/DejaVuSerif-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
         "/Library/Fonts/DejaVuSerif-Bold.ttf"]
CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!'+-.:?"


def glyphs(path, size, half):
    font = ImageFont.truetype(path, size)
    base = 24

    def ink(ch, mode):
        im = Image.new("L", (48, 48), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = mode
        d.text((10, base), ch, font=font, fill=255, anchor="ls")
        return im

    top = ink("H", "1").getbbox()[1]                 # the capitals' top row
    out = {}
    for ch in CHARS:
        hard, soft = ink(ch, "1"), ink(ch, "L")
        x0, y0, x1, y1 = hard.getbbox()
        rows = ["".join("#" if hard.getpixel((x, y)) else "+" if soft.getpixel((x, y)) >= half else "."
                        for x in range(x0, x1)) for y in range(top, max(y1, top + 1))]
        if ch == "J":                                # its hook brought up to stand on the baseline
            rows = rows[:base - top - 2] + rows[-2:]
        rows = rows[:base - top + 2]                 # Q's tail: two rows below the baseline at most
        out[ch] = rows
    return out, base - top


def preview(path, half):
    pal = {"INK": (0, 0, 0), "WHITE": (255, 255, 255), "SILVER": (187, 187, 204), "NAVY": (17, 34, 85),
           "GOLD": (255, 204, 34), "WOOD": (119, 68, 17), "FELT": (17, 119, 51), "FELT_DK": (0, 68, 34)}
    lines = [("OPTIONS", "GOLD", "WOOD", "FELT"), ("SOUND ON", "WHITE", "SILVER", "FELT"),
             ("1 PLAYER", "GOLD", "WOOD", "NAVY"), ("PLAY TO 100", "WHITE", "SILVER", "NAVY"),
             ("FIFTEEN!", "GOLD", "WOOD", "FELT"), ("YOUR ROUND!", "GOLD", "WOOD", "NAVY")]
    sizes = [12, 13, 14]
    W, H = 3 * 130, len(lines) * 20
    im = Image.new("RGB", (W, H))
    for k, size in enumerate(sizes):
        g, _ = glyphs(path, size, half)
        for j, (text, c, mid, bg) in enumerate(lines):
            ox, oy = k * 130, j * 20
            for y in range(20):
                for x in range(128):
                    im.putpixel((ox + x, oy + y), pal[bg])
            x = ox + 3
            for ch in text:
                if ch == " ":
                    x += 4
                    continue
                for r, row in enumerate(g[ch]):
                    for i, v in enumerate(row):
                        if v != ".":
                            im.putpixel((x + i, oy + 4 + r), pal[c if v == "#" else mid])
                x += len(g[ch][0]) + 1
    im.resize((W * 3, H * 3), Image.NEAREST).save(ROOT / "out" / "aafont.png")
    print("out/aafont.png: sizes", sizes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ttf")
    ap.add_argument("--size", type=int, default=12)
    ap.add_argument("--half", type=int, default=80)
    ap.add_argument("--preview", action="store_true")
    a = ap.parse_args()
    path = a.ttf or next((f for f in FONTS if Path(f).exists()), None)
    if not path:
        raise SystemExit("DejaVuSerif-Bold.ttf not found: pass --ttf")
    if a.preview:
        (ROOT / "out").mkdir(exist_ok=True)
        preview(path, a.half)
        return
    g, baseline = glyphs(path, a.size, a.half)
    out = [f"# The serif lettering (tools/aafont.py: DejaVu Serif Bold, size {a.size}).",
           f"# '#' ink, '+' half ink. Rows from the capitals' top; the baseline is row {baseline}.", ""]
    for ch in CHARS:
        out.append(f"= {ch}")
        out += g[ch]
        out.append("")
    (HERE / "art" / "aafont.txt").write_text("\n".join(out), newline="\n")
    print(f"tools/art/aafont.txt: {len(CHARS)} glyphs, size {a.size}")


if __name__ == "__main__":
    main()
