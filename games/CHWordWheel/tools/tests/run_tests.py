"""Build and run the host tests: the rules (src/game) and the phrase bank
(src/bank against tools/phrases/build_bank.py's own decoder).

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

GAME = [*sorted((ROOT / "src" / "game").glob("*.cpp")), ROOT / "src" / "gfx" / "Fmt.cpp"]
TESTS = {
    "test_show": [HERE / "test_show.cpp", *GAME],
    "test_bank": [HERE / "test_bank.cpp", *GAME, ROOT / "src" / "bank" / "FlashBank.cpp",
                  ROOT / "src" / "bank" / "SdBank.cpp", ROOT / "src" / "bank" / "BankData.cpp",
                  ROOT / "src" / "sd" / "Fat.cpp"],
}


def cxx():
    if not os.environ.get("CHSIM_CXX") and not shutil.which("zig"):
        work = ROOT.parents[2] / "CH32Sound" / ".work" / "zig"
        for z in sorted(work.glob("zig-*/zig.exe")) + sorted(work.glob("zig-*/zig")):
            return [str(z), "c++"]
    return find_cxx()


def main():
    code = 0
    for name, sources in TESTS.items():
        if not sources[0].exists():
            continue
        exe = HERE / "build" / f"{name}.exe"
        exe.parent.mkdir(exist_ok=True)
        cmd = cxx() + ["-std=gnu++17", "-O2", "-Wall", "-Wextra", "-Wno-unused-parameter",
                       "-Wno-unknown-pragmas", "-fsanitize=undefined",
                       "-fno-sanitize-recover=undefined", "-DCHTEST",
                       *[str(s) for s in sources], "-o", str(exe)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit(f"{name}: build failed")
        if r.stderr.strip():
            sys.stderr.write(r.stderr)
        code = subprocess.run([str(exe), *sys.argv[1:]], cwd=ROOT).returncode or code
    raise SystemExit(code)


if __name__ == "__main__":
    main()
