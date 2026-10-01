"""Saving in the simulator: SAVE & QUIT in the middle of a round, power off
and on, CONTINUE from the title, and the round must come back exactly (the
purse, the jackpot, the cards and their daubs, the calls so far, the call a
rival wins on). Also: options survive, and going broke leaves no game to
continue.

    python tools/tests/sim_save.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent / "chsim"))
sys.path.insert(0, str(HERE.parents[3] / "tools" / "chsim"))  # CHCasino/tools/chsim: chsim.py, fbimage.py
from chsim import build  # noqa: E402
from chdrive import Driver, SimTransport, mask_of  # noqa: E402

FIELDS = ["purse", "jackpot", "phase", "cards", "called", "focus", "hall", "daubs", "hasGame", "rounds",
          "speed", "screen"]
CALLING, QUIT = 2, 6
LOSE = 4


def say(d, line):
    d.t.send(line)
    got = None
    for _ in range(100):
        r = d.t.readline()
        if r.startswith("STATE"):
            got = dict(zip(FIELDS, map(int, r.split()[1:])))
        elif r.startswith("OK"):
            return got
        elif r.startswith("ERR"):
            raise SystemExit(f"refused: {line}")


def tap(d, btn, hold=3):
    d.buttons(mask_of(btn))
    d.frames(hold)
    d.buttons(0)
    d.frames(2)


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    return 0 if ok else 1


def main():
    d = Driver(SimTransport(build(str(ROOT))), "CHBN")
    d.handshake()
    d.cmd("L1")
    fails = 0
    # A round in progress: four cards, a few calls, some daubed, the third card in play.
    say(d, "R 11")
    say(d, "J P")
    d.frames(20)
    say(d, "C 4")
    d.frames(100 + 5 * 120)
    say(d, "D")
    d.frames(2 * 120)                                  # two more calls left waiting
    tap(d, "RIGHT")
    tap(d, "RIGHT")
    d.frames(10)
    before = say(d, "Y")
    # Pause -> SAVE & QUIT (the fourth item).
    tap(d, "START")
    for _ in range(3):
        tap(d, "DOWN")
    tap(d, "A")
    d.frames(40)
    # Power off and on (the game in memory is gone), then CONTINUE.
    say(d, "V")
    d.frames(20)
    off = say(d, "Y")
    fails += check(off["hasGame"] == 1 and off["phase"] == QUIT, f"after a power cycle there is a game to continue: {off}")
    tap(d, "A")
    d.frames(40)
    after = say(d, "Y")
    same = all(before[k] == after[k] for k in FIELDS if k != "screen")
    fails += check(same and before["phase"] == CALLING and before["called"] >= 6 and before["focus"] == 2,
                   f"save mid-round: {before} -> {after}")
    # The round plays on from there.
    d.frames(400)
    later = say(d, "Y")
    fails += check(later["called"] > after["called"], f"the caller carries on ({after['called']} -> {later['called']} calls)")
    # Options are saved when the menu is left.
    say(d, "J O")
    d.frames(10)
    tap(d, "DOWN")
    tap(d, "DOWN")
    tap(d, "RIGHT")                                    # SPEED -> FAST
    tap(d, "B")
    d.frames(30)
    say(d, "V")
    d.frames(20)
    st = say(d, "Y")
    fails += check(st["speed"] == 1, f"options survive a power cycle (speed {st['speed']})")
    # Going broke: the last $5 on one card, a rival wins at once.
    say(d, "J P")
    d.frames(20)
    say(d, "M 5")
    say(d, "C 1")
    say(d, "H 4")
    d.frames(1200)
    st = say(d, "Y")
    fails += check(st["screen"] == LOSE and st["purse"] == 0, f"broke: the broke screen ({st})")
    say(d, "V")
    d.frames(20)
    st = say(d, "Y")
    fails += check(st["hasGame"] == 0, f"broke: no game to continue ({st})")
    d.t.close()
    raise SystemExit(1 if fails else 0)


if __name__ == "__main__":
    main()
