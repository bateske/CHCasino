"""Build and run the host tests (the game logic).

    python tools/tests/run_tests.py
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim (find_cxx)
from chsim import find_cxx  # noqa: E402

# The game logic has no graphics or hardware in it: it is compiled as it is.
GAME = ROOT / "src" / "game"
SOURCES = [HERE / "test_board.cpp", GAME / "Board.cpp", GAME / "Nav.cpp", GAME / "Layouts.cpp"]


def main():
    exe = HERE / "build" / "test_board.exe"
    exe.parent.mkdir(exist_ok=True)
    srcs = [str(s) for s in SOURCES if s.exists()]
    cmd = find_cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unknown-pragmas", "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                        "-DCHTEST", *srcs, "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe)]).returncode)


if __name__ == "__main__":
    main()
