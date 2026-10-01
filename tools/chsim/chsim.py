"""Build a CHGame sketch for the PC simulator.

    python tools/chsim/chsim.py build <sketch dir> [-D NAME=VAL ...]   -> prints the .exe path

This is CHCasino's shared simulator: every game in games/ builds with it.

Compiles the sketch's .ino and every .cpp/.c under its src/ folder, CHGfx's
portable code (every src/*.cpp except CHGfx.cpp, unmodified: drawing,
extras, text effects, palette), and the host shims in host/, where
chgfx_host.cpp stands in for CHGfx.cpp.

A game may add shims of its own in <sketch>/tools/chsim/host/ (the SD games
keep CHSd's pretend card there): its .cpp files are compiled too, and one
with the same name as a shared shim replaces it. Its headers come first on
the include path, so a header there must not share a name with one here.

CHGfx is $CHSIM_CHGFX (its src folder) if set, else CHCasino's own copy in
platform/libraries/CHGfx, else the Arduino sketchbook's libraries/CHGfx, or
libraries/CHGfx* (a GitHub zip installs as CHGfx-main). The sketchbook is
$CHSIM_SKETCHBOOK, else what `arduino-cli config get directories.user`
reports, else ~/Documents/Arduino (~/Arduino on Linux).

Compiler: $CHSIM_CXX (e.g. "zig c++"), else zig on the PATH, else the
ziglang pip package (`pip install ziglang`), else clang++ or g++.

The executable is <sketch>/tools/chsim/build/<name>/sim.exe.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent                  # CHCasino/tools/chsim
VENDORED_CHGFX = HERE.parents[1] / "platform" / "libraries" / "CHGfx" / "src"


def sketchbook():
    env = os.environ.get("CHSIM_SKETCHBOOK")
    if env:
        return Path(env)
    try:
        r = subprocess.run(["arduino-cli", "config", "get", "directories.user"],
                           capture_output=True, text=True, timeout=30)
        if r.returncode == 0 and r.stdout.strip():
            return Path(r.stdout.strip())
    except (OSError, subprocess.TimeoutExpired):
        pass
    home = Path.home()
    return home / "Arduino" if sys.platform.startswith("linux") else home / "Documents" / "Arduino"


def chgfx_dir():
    env = os.environ.get("CHSIM_CHGFX")
    if env:
        d = Path(env)
    elif (VENDORED_CHGFX / "CHGfx_draw.cpp").exists():
        d = VENDORED_CHGFX
    else:
        d = sketchbook() / "libraries" / "CHGfx" / "src"
        if not (d / "CHGfx_draw.cpp").exists():
            # A GitHub zip installs as libraries/CHGfx-main (or -1.3.0, ...).
            found = sorted((sketchbook() / "libraries").glob("CHGfx*/src/CHGfx_draw.cpp"))
            if found:
                d = found[-1].parent
    if not (d / "CHGfx_draw.cpp").exists():
        raise SystemExit(f"CHGfx not found at {d}: install the library or set CHSIM_CHGFX")
    return d


def find_cxx():
    env = os.environ.get("CHSIM_CXX")
    if env:
        return env.split()
    if shutil.which("zig"):
        return ["zig", "c++"]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "c++"]
    except ImportError:
        pass
    for c in ("clang++", "g++"):
        if shutil.which(c):
            return [c]
    raise SystemExit("no C++ compiler: set CHSIM_CXX, put zig/clang++/g++ on the PATH, "
                     "or `pip install ziglang`")


def build(sketch, defines=(), out=None):
    sketch = Path(sketch).resolve()
    name = sketch.name
    chgfx = chgfx_dir()
    game = sketch / "tools" / "chsim"
    bdir = game / "build" / name
    bdir.mkdir(parents=True, exist_ok=True)
    own = game / "host"
    clash = sorted({p.name for p in own.glob("*.h")} & {p.name for p in (HERE / "host").glob("*.h")})
    if clash:
        raise SystemExit(f"{name}: tools/chsim/host/{clash[0]} has the name of a shared shim header")
    shims = {p.name: p for p in (HERE / "host").glob("*.cpp")}
    shims.update({p.name: p for p in own.glob("*.cpp")})
    inos = sorted(sketch.glob("*.ino"))
    main_ino = sketch / f"{name}.ino"
    if main_ino in inos:
        inos.remove(main_ino)
        inos.insert(0, main_ino)
    unit = bdir / "sketch_ino.cpp"
    with open(unit, "w", encoding="utf-8") as f:
        f.write("#include <Arduino.h>\n")
        for ino in inos:
            f.write(f'#line 1 "{ino.as_posix()}"\n')
            f.write(ino.read_text(encoding="utf-8"))
            f.write("\n")
    srcs = [unit]
    srcs += sorted(p for p in (sketch / "src").rglob("*") if p.suffix in (".cpp", ".c"))
    srcs += sorted(p for p in chgfx.glob("*.cpp") if p.name != "CHGfx.cpp")
    srcs += [shims[n] for n in sorted(shims)]
    exe = Path(out) if out else bdir / "sim.exe"
    cmd = find_cxx() + [
        "-std=gnu++17", "-O1", "-g0", "-w",
        "-DCHSIM", "-DCH32X035", "-DARDUINO=10800",
    ]
    cmd += [f"-I{d}" for d in (own, HERE / "host") if d.is_dir()]
    cmd += [f"-I{sketch}", f"-I{chgfx}"]
    for d in defines:
        cmd.append(f"-D{d}")
    cmd += [str(s) for s in srcs] + ["-o", str(exe)]
    # zig treats .c as C; everything here is compiled as C++ on purpose.
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"chsim build failed for {name}")
    return exe


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("sketch")
    b.add_argument("-D", dest="defines", action="append", default=[])
    a = ap.parse_args()
    if a.cmd == "build":
        print(build(a.sketch, a.defines))


if __name__ == "__main__":
    main()
