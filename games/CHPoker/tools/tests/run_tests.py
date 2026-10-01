"""Build and run the host tests (evaluator, table rules, CPU players).

    python tools/tests/run_tests.py [--long]

Compiles tools/tests/test_*.cpp with the game logic (src/game/*.cpp, no
graphics or sound) under UBSan, then runs it. --long adds the exhaustive
seven-card enumeration (a minute or two).
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim (find_cxx)
from chsim import find_cxx  # noqa: E402


def main():
    exe = HERE / "build" / "test_poker.exe"
    exe.parent.mkdir(exist_ok=True)
    srcs = sorted(HERE.glob("test_*.cpp")) + sorted((ROOT / "src" / "game").glob("*.cpp"))
    cmd = find_cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-unused-function", "-Wno-unknown-pragmas",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                        "-DCHTEST", *[str(s) for s in srcs], "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe), *sys.argv[1:]]).returncode)


if __name__ == "__main__":
    main()
