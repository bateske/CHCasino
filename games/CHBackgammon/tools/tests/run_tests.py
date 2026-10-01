"""Build and run the host tests (rules, game flow, the CPU).

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
SOURCES = [HERE / "test_backgammon.cpp", ROOT / "src" / "rules" / "Board.cpp"]
MATCH = [ROOT / "src" / "ai" / "Net.cpp", ROOT / "src" / "ai" / "NetData.cpp", ROOT / "src" / "ai" / "Race.cpp",
         ROOT / "src" / "ai" / "RaceData.cpp", ROOT / "src" / "ai" / "Ai.cpp", ROOT / "src" / "ai" / "Cube.cpp",
         ROOT / "src" / "ai" / "MetData.cpp", ROOT / "src" / "game" / "Match.cpp", ROOT / "src" / "game" / "Notation.cpp",
         ROOT / "src" / "gfx" / "Fmt.cpp"]


def main():
    exe = HERE / "build" / "test_backgammon.exe"
    exe.parent.mkdir(exist_ok=True)
    srcs = [str(s) for s in SOURCES]
    flags = []
    if (HERE / "test_match.h").exists() and all(m.exists() for m in MATCH):
        srcs += [str(m) for m in MATCH]
        flags.append("-DWITH_MATCH")
    cmd = find_cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                        "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unknown-pragmas",
                        "-fsanitize=undefined", "-fno-sanitize-recover=undefined",
                        "-DCHTEST", *flags, *srcs, "-o", str(exe)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("build failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    raise SystemExit(subprocess.run([str(exe), *sys.argv[1:]]).returncode)


if __name__ == "__main__":
    main()
