#!/usr/bin/env python3
"""Rebuild the menu pictures in docs/ from the native suite's panel dumps.

    python3 test/native/run_tests.py -k boot     # writes test/native/build/frames/
    python3 tools/screens.py

docs/menu.png          the menu with the casino card (out/sdcard.img)
docs/menu_screens.png  the menu with a game installed, and three messages
docs/menu_themes.png   the same list in the three themes (src/menu.c)
docs/menu_rainbow.gif  the default theme's colours turning (4 s)
"""
import subprocess
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parents[1]
FRAMES = HERE / "test" / "native" / "build" / "frames"
DOCS = HERE / "docs"
GAP, GAP_RGB = 8, (24, 24, 24)


def frame(name):
    return Image.open(FRAMES / f"{name}.png").convert("RGB").resize((256, 256), Image.NEAREST)


def strip(names):
    out = Image.new("RGB", (len(names) * (256 + GAP) - GAP, 256), GAP_RGB)
    for i, n in enumerate(names):
        out.paste(frame(n), (i * (256 + GAP), 0))
    return out


def main():
    frame("real_menu").save(DOCS / "menu.png")
    strip(["real_menu_installed", "error_box", "usb_notice", "install_failed"]).save(DOCS / "menu_screens.png")
    strip(["menu_installed", "plain_menu_installed", "casino_menu_installed"]).save(DOCS / "menu_themes.png")
    # the boot test binary in its picture mode, on the casino card
    native = HERE / "test" / "native" / "build"
    subprocess.run([str(native / "boot"), str(native / "real_manifest.txt"), str(FRAMES), "anim"], check=True,
                   capture_output=True)
    anim = []
    for i in range(32):
        ppm = FRAMES / f"anim_{i:02d}.ppm"
        anim.append(Image.open(ppm).convert("RGB").resize((256, 256), Image.NEAREST))
        ppm.unlink()
    anim[0].save(DOCS / "menu_rainbow.gif", save_all=True, append_images=anim[1:], duration=120, loop=0)
    print("docs/menu.png, docs/menu_screens.png, docs/menu_themes.png, docs/menu_rainbow.gif")


if __name__ == "__main__":
    main()
