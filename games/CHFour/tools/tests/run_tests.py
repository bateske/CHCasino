"""Build and run the host tests (rules, the CPU, the game flow, the dealer's lines).

    python tools/tests/run_tests.py [--quick]
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim (find_cxx)
from chsim import find_cxx  # noqa: E402

# The pure-logic sources, compiled beside the tests as they are.
SOURCES = [HERE / "test_four.cpp", ROOT / "src" / "rules" / "Board.cpp", ROOT / "src" / "ai" / "Ai.cpp",
           ROOT / "src" / "game" / "Game.cpp", ROOT / "src" / "game" / "Taunt.cpp"]


def main():
    exe = HERE / "build" / "test_four.exe"
    exe.parent.mkdir(exist_ok=True)
    cmd = find_cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unknown-pragmas",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                        "-DCHTEST", "-DCHSIM", *[str(s) for s in SOURCES], "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe), *sys.argv[1:]]).returncode)


if __name__ == "__main__":
    main()
