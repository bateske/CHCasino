"""Build and run the host rules tests (src/game: Wheel, Spots, Nav, Roulette),
then diff the spot model against the independent Python reference.

    python tools/tests/run_tests.py

Compiler: $CHSIM_CXX, else zig on the PATH, else the zig kept beside the
workspace (CH32Sound/.work/zig), else what tools/chsim's find_cxx() finds.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim (find_cxx)
sys.path.insert(0, str(HERE))
from chsim import find_cxx  # noqa: E402
import ref_roulette  # noqa: E402

SOURCES = [HERE / "test_rules.cpp", *sorted((ROOT / "src" / "game").glob("*.cpp")),
           ROOT / "src" / "fx" / "Ease.cpp", ROOT / "src" / "assets" / "WheelMap.cpp"]


def cxx():
    if not os.environ.get("CHSIM_CXX") and not shutil.which("zig"):
        work = ROOT.parents[2] / "CH32Sound" / ".work" / "zig"
        for z in sorted(work.glob("zig-*/zig.exe")) + sorted(work.glob("zig-*/zig")):
            return [str(z), "c++"]
    return find_cxx()


def main():
    exe = HERE / "build" / "test_rules.exe"
    exe.parent.mkdir(exist_ok=True)
    cmd = cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                   "-Wno-unknown-pragmas", "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                   "-DCHTEST", *[str(s) for s in SOURCES], "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    code = subprocess.run([str(exe)]).returncode

    # The spot model against the reference.
    d = subprocess.run([str(exe), "--dump"], capture_output=True, text=True)
    got, want = d.stdout.split("\n"), ref_roulette.lines()
    got = [g for g in got if g.strip()]
    bad = [(g, w) for g, w in zip(got, want) if g != w]
    if len(got) != len(want):
        bad.append((f"{len(got)} lines", f"{len(want)} lines"))
    for g, w in bad[:20]:
        print(f"FAIL reference: got  {g}\n                want {w}")
    if bad or d.returncode:
        print(f"reference cross-check: {len(bad)} mismatches")
        code = code or 1
    else:
        eu = sum(1 for w in want if w.startswith("EU"))
        print(f"reference cross-check: {eu} EU + {len(want) - eu} US spots match "
              "(name, numbers, payout, coverage)")
    raise SystemExit(code)


if __name__ == "__main__":
    main()
