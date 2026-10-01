"""Build and run the host tests (rules, dice).

    python tools/tests/run_tests.py
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim (find_cxx)
from chsim import find_cxx  # noqa: E402

# Each test: its source and the game sources it covers (all graphics-free).
TESTS = {
    "test_yacht": ["src/game/Yacht.cpp"],
    "test_dice": ["src/cam/Dice3D.cpp"],
}


def run(name, sources):
    exe = HERE / "build" / f"{name}.exe"
    exe.parent.mkdir(exist_ok=True)
    cmd = find_cxx() + ["-std=gnu++17", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unknown-pragmas",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined", "-DCHTEST",
                        str(HERE / f"{name}.cpp")] + [str(ROOT / s) for s in sources] + ["-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"{name}: build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    print(f"== {name}", flush=True)
    return subprocess.run([str(exe)]).returncode


def main():
    only = sys.argv[1:]
    bad = [n for n, s in TESTS.items() if (not only or n in only) and run(n, s)]
    raise SystemExit(f"failed: {' '.join(bad)}" if bad else 0)


if __name__ == "__main__":
    main()
