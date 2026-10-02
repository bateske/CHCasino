"""CHSDtoUSB on the PC: build, play a PC session against it, save the frames.

    python tools/sim/sim.py build                      -> the simulator
    python tools/sim/sim.py run OUT [--scenario demo]  -> OUT/*.png, OUT/demo.gif, the event log
    python tools/sim/sim.py run OUT --docs             -> also docs/screens.png and docs/session.gif
    python tools/sim/sim.py run OUT --no-screen        -> the same session with the screen drawn once

The run's last line gives the time the session took. Frames cost what the
board takes to draw them (main.cpp) and commands wait for a flush in
flight, so the difference with --no-screen is what the screen costs a
transfer.

`run` writes the scenario's trace with pcsession.py, plays it, turns the
screenshots (`snap`) into PNGs and the recorded frames (`rec`) into a GIF,
and prints what the sketch logged as events, one line each. The frames are
deterministic: a change that should not alter the picture must give the
same PNG and GIF (compare the hashes it prints).

Built with CHCasino's shared simulator pieces (tools/chsim): its Arduino
stand-in and its CHGfx transport. The USB stack and the card driver are
replaced by tools/sim/host: a pretend PC and a card in memory.
"""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent                  # CHSDtoUSB/tools/sim
SKETCH = HERE.parents[1]
REPO = SKETCH.parents[1]
sys.path.insert(0, str(REPO / "tools" / "chsim"))
import chsim  # noqa: E402
import fbimage  # noqa: E402

REPLACED = {"UsbMsc.cpp", "Sd2Card.cpp"}                # the hardware halves


def build(defines=()):
    chgfx = chsim.chgfx_dir()
    bdir = HERE / "build"
    bdir.mkdir(exist_ok=True)
    unit = bdir / "sketch_ino.cpp"
    ino = SKETCH / "CHSDtoUSB.ino"
    unit.write_text("#include <Arduino.h>\n" + f'#line 1 "{ino.as_posix()}"\n' +
                    ino.read_text(encoding="utf-8") + "\n", encoding="utf-8", newline="\n")
    srcs = [unit]
    srcs += sorted(p for p in (SKETCH / "src").rglob("*.cpp") if p.name not in REPLACED)
    srcs += sorted(p for p in chgfx.glob("*.cpp") if p.name != "CHGfx.cpp")
    srcs += [REPO / "tools" / "chsim" / "host" / "chgfx_host.cpp"]
    srcs += sorted((HERE / "host").glob("*.cpp"))
    exe = bdir / "sim.exe"
    cmd = chsim.find_cxx() + ["-std=gnu++17", "-O1", "-g0", "-w", "-DCHSIM", "-DCH32X035",
                              "-DSD_CH32_DISABLE_FAST", "-DARDUINO=10800"]
    cmd += [f"-I{HERE / 'host'}", f"-I{REPO / 'tools' / 'chsim' / 'host'}", f"-I{SKETCH}", f"-I{chgfx}"]
    cmd += [f"-D{d}" for d in defines]
    cmd += [str(s) for s in srcs] + ["-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("simulator build failed")
    return exe


DOC_SCREENS = ["copying", "events", "stats", "card", "ejected"]


def docs(out):
    """The README's pictures from a run of the demo scenario."""
    from PIL import Image
    shots = [Image.open(out / f"{n}.png").convert("RGB") for n in DOC_SCREENS]
    w, h = shots[0].size
    sheet = Image.new("RGB", (len(shots) * w + (len(shots) - 1) * 12, h), (255, 255, 255))
    for i, im in enumerate(shots):
        sheet.paste(im, (i * (w + 12), 0))
    sheet.save(SKETCH / "docs" / "screens.png", optimize=True)
    (SKETCH / "docs" / "session.gif").write_bytes((out / "demo.gif").read_bytes())


def run(out, scenario, scale, defines=()):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    for f in list(out.glob("*.fb")):
        f.unlink()
    exe = build(defines)
    trace = out / "trace"
    subprocess.run([sys.executable, str(HERE / "pcsession.py"), str(trace), "--scenario", scenario],
                   check=True, stdout=subprocess.DEVNULL)
    r = subprocess.run([str(exe), str(trace), str(out)], capture_output=True, text=True)
    (out / "events.txt").write_text(r.stderr, encoding="utf-8", newline="\n")
    sys.stdout.write(r.stderr)
    if r.returncode:
        raise SystemExit(f"simulator exited with {r.returncode}")
    digests = []
    for fb in sorted(out.glob("*.fb")):
        if fb.name.startswith("rec_"):
            continue
        im = fbimage.to_image(fb.read_bytes(), scale)
        im.save(out / (fb.stem + ".png"))
        digests.append((fb.stem + ".png", hashlib.sha1(fb.read_bytes()).hexdigest()[:12]))
    rec = sorted(out.glob("rec_*.fb"))
    if rec:
        frames = [fbimage.to_image(f.read_bytes(), scale) for f in rec]
        fbimage.save_gif(frames, out / f"{scenario}.gif", 50)
        h = hashlib.sha1(b"".join(f.read_bytes() for f in rec)).hexdigest()[:12]
        digests.append((f"{scenario}.gif ({len(rec)} frames)", h))
    for f in out.glob("*.fb"):
        f.unlink()
    for name, h in digests:
        print(f"{h}  {name}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("build")
    r = sub.add_parser("run")
    r.add_argument("out")
    r.add_argument("--scenario", default="demo")
    r.add_argument("--scale", type=int, default=2)
    r.add_argument("--docs", action="store_true", help="refresh docs/screens.png and docs/session.gif (demo)")
    r.add_argument("--no-screen", action="store_true", help="draw only the first frame, to time the screen's cost")
    r.add_argument("-D", dest="defines", action="append", default=[], help="a define for the sketch, e.g. CHSD_TEST")
    a = ap.parse_args()
    if a.cmd == "build":
        print(build())
    else:
        run(a.out, "demo" if a.docs else a.scenario, a.scale, a.defines + (["CHSIM_NOSCREEN"] if a.no_screen else []))
        if a.docs:
            docs(Path(a.out))


if __name__ == "__main__":
    main()
