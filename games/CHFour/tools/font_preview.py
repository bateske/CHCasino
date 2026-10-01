"""Preview of the display font (tools/art/font.txt), drawn as the game draws
its lettering: gradient fill, ink outline, a shadow a pixel down and right.

    python tools/font_preview.py [OUT.png]      (default build/assets/font_sample.png)
"""
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from assets import PALETTE, load_font, rgb  # noqa: E402

SAMPLES = ["FOUR", "IN A ROW", "CURSES!", "I WIN!", "RED WINS!", "GOLD WINS!", "DRAW", "YOU WIN!", "YOU LOSE",
           "ROOKIE SHARK", "THE BOSS", "TAKE TURNS", "ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ", "0123456789", "?+-.,:' DEALER"]


def text_mask(font, s, gap=1):
    w = sum(font[c]["w"] + gap if c in font else 4 if c == " " else 10 for c in s)
    h = 13
    m = [[0] * (w + 2) for _ in range(h + 2)]
    x = 1
    for c in s:
        if c not in font:
            x += 4 if c == " " else 10
            continue
        g = font[c]
        for r, row in enumerate(g["rows"]):
            for i, v in enumerate(row):
                if v:
                    m[1 + g["top"] + r][x + i] = 1
        x += g["w"] + gap
    return m


def draw(img, m, x0, y0, ramp, outline=0, shadow=7):
    h, w = len(m), len(m[0])
    grown = [[any(m[yy][xx] for yy in range(max(0, y - 1), min(h, y + 2)) for xx in range(max(0, x - 1), min(w, x + 2)))
              for x in range(w)] for y in range(h)]
    for y in range(h):
        for x in range(w):
            if grown[y][x]:
                img.putpixel((x0 + x + 1, y0 + y + 1), rgb(shadow))
    for y in range(h):
        for x in range(w):
            if grown[y][x]:
                img.putpixel((x0 + x, y0 + y), rgb(outline))
    for y in range(h):
        for x in range(w):
            if m[y][x]:
                img.putpixel((x0 + x, y0 + y), rgb(ramp[min(y, len(ramp) - 1)]))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "build/assets/font_sample.png"
    font = load_font()
    W, rowh = 150, 20
    img = Image.new("RGB", (W, rowh * len(SAMPLES) + 4), rgb(3))
    gold = [1, 1, 1, 15] + [8] * 6 + [9] * 6        # FX_B shows as gold here: white top rows instead
    gold = [1] * 3 + [8] * 7 + [9] * 6
    for k, s in enumerate(SAMPLES):
        m = text_mask(font, s)
        draw(img, m, (W - len(m[0])) // 2, 2 + k * rowh, gold)
        print(f"{s!r}: {len(m[0]) - 2} px")
    img.resize((W * 4, img.height * 4), Image.NEAREST).save(out)
    print(out)


if __name__ == "__main__":
    main()
