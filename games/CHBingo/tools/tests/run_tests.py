"""Build and run the host rules tests (src/game/Bingo), then diff the round
set-up against the independent Python reference (ref_bingo.py).

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
import ref_bingo  # noqa: E402

SOURCES = [HERE / "test_bingo.cpp", *sorted((ROOT / "src" / "game").glob("*.cpp"))]


def cxx():
    if not os.environ.get("CHSIM_CXX") and not shutil.which("zig"):
        work = ROOT.parents[2] / "CH32Sound" / ".work" / "zig"
        for z in sorted(work.glob("zig-*/zig.exe")) + sorted(work.glob("zig-*/zig")):
            return [str(z), "c++"]
    return find_cxx()


def main():
    exe = HERE / "build" / "test_bingo.exe"
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

    # The round set-up against the reference.
    d = subprocess.run([str(exe), "--dump"], capture_output=True, text=True)
    got, want = [g for g in d.stdout.splitlines() if g.strip()], ref_bingo.dump_lines()
    bad = [(g, w) for g, w in zip(got, want) if g != w]
    if len(got) != len(want):
        bad.append((f"{len(got)} lines", f"{len(want)} lines"))
    for g, w in bad[:10]:
        print(f"FAIL reference: got  {g}")
        print(f"                want {w}")
    if bad or d.returncode:
        print(f"reference cross-check: {len(bad)} mismatches")
        code = code or 1
    else:
        print(f"reference cross-check: {len(want)} round set-ups match (draw, cards, the hall's winning call)")
    raise SystemExit(code)


if __name__ == "__main__":
    main()
