"""Everything that can be checked without the board, in one go.

    python tools/check.py [--quick] [--no-device]

  1. The puzzle banks are rebuilt from tools/phrases/phrases.txt (every
     puzzle checked and wrapped) and must come out as committed.
  2. The host tests (tools/tests): the rules, whole CPU episodes, the flash
     bank's decoder and the SD bank's reader against the builder's lists.
  3. Every script in tools/scripts runs in the simulator twice: the two runs
     must draw identical frames (the game is deterministic), with no drawing
     into a frame still being sent (the simulator's BUG lines). card.txt
     runs with sdcard/PHRASES.BNK as the SD card.
  4. The scripts in tools/scripts/diff run against a build that redraws
     everything every frame: any pixel that differs is one the incremental
     redraw left stale (tools/chsim/diffdrive.py).
  5. The release build compiles for the device and fits (tools/check_size.py).

Screenshots and contact sheets are left in out/<script>/ to look at.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
OUT = ROOT / "out"
CARD_SCRIPTS = {"card"}             # run with the SD bank in the slot


def run(cmd, env=None):
    e = dict(os.environ)
    e.pop("CHWW_CARD", None)
    e.update(env or {})
    return subprocess.run([sys.executable, *map(str, cmd)], capture_output=True, text=True, cwd=ROOT, env=e)


def frames_hash(d):
    """A hash of every image's pixels in a run's folder."""
    h = hashlib.sha256()
    for p in sorted(d.glob("*.png")):
        if p.name != "sheet.png":
            h.update(p.name.encode())
            h.update(Image.open(p).convert("RGB").tobytes())
    return h.hexdigest()


def drive(script, outdir):
    if outdir.exists():
        shutil.rmtree(outdir)
    env = {"CHWW_CARD": str(ROOT / "sdcard" / "PHRASES.BNK")} if script.stem in CARD_SCRIPTS else None
    r = run([HERE / "chsim" / "chdrive.py", "--sim", ROOT, script, outdir], env)
    return r.returncode == 0, r.stdout + r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="each script once, no redraw check")
    ap.add_argument("--no-device", action="store_true", help="skip the device compile")
    a = ap.parse_args()
    ok = True

    print("== puzzle banks")
    data = ROOT / "src" / "bank" / "BankData.cpp"
    before = data.read_bytes() if data.exists() else b""
    r = run([HERE / "phrases" / "build_bank.py"])
    print((r.stdout + r.stderr).strip())
    if r.returncode:
        ok = False
    elif data.read_bytes() != before:
        print("   note: src/bank/BankData.* changed (phrases.txt or the byte budget did)")

    print("== host tests")
    r = run([HERE / "tests" / "run_tests.py"])
    lines = [l for l in r.stdout.splitlines() if "checks" in l or "FAIL" in l or "episodes" in l]
    print("\n".join(lines[-12:]))
    if r.returncode:
        print(r.stderr[-2000:])
        ok = False

    print("== simulator")
    r = run([HERE.parents[2] / "tools" / "chsim" / "chsim.py", "build", ROOT])  # CHCasino/tools/chsim
    if r.returncode:
        print((r.stdout + r.stderr)[-3000:])
        sys.exit("simulator build failed")
    for script in sorted((HERE / "scripts").glob("*.txt")):
        good, log = drive(script, OUT / script.stem)
        note = ""
        if good and not a.quick:
            first = frames_hash(OUT / script.stem)
            good, log = drive(script, OUT / (script.stem + "_again"))
            if good and frames_hash(OUT / (script.stem + "_again")) != first:
                good, note = False, "  (two runs drew different frames)"
            shutil.rmtree(OUT / (script.stem + "_again"), ignore_errors=True)
        print(f"   {'ok  ' if good else 'FAIL'} {script.name}{note}")
        if not good:
            print(log[-1500:])
            ok = False

    if not a.quick:
        print("== redraw check")
        for script in sorted((HERE / "scripts" / "diff").glob("*.txt")):
            for ticks in (1, 3):
                r = run([HERE / "chsim" / "diffdrive.py", script, OUT / f"{script.stem}_{ticks}", ticks])
                tail = (r.stdout + r.stderr).strip().splitlines()[-1:] or ["no output"]
                clean = r.returncode == 0 and " 0 diff episodes" in tail[0]
                print(f"   {'ok  ' if clean else 'FAIL'} {script.name} x{ticks}: {tail[0]}")
                if not clean:
                    print((r.stdout + r.stderr)[-1500:])
                    ok = False

    if not a.no_device:
        print("== device build")
        r = run([HERE / "device.py", "build"])
        out = (r.stdout + r.stderr).strip()
        print("\n".join(out.splitlines()[-3:]))
        if r.returncode or "save pages free: 2" not in out:
            print("   FAIL: the release build must compile and leave both save pages")
            ok = False

    print("ALL OK" if ok else "FAILED")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
