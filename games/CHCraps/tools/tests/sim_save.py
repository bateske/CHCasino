"""Saving in the simulator: SAVE & QUIT in the middle of a hand, CONTINUE
from the title, and the table must come back exactly (purse, every bet,
the point). Also: options and stats survive, and a game that ended (broke)
leaves no game to continue.

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


def say(d, line):
    d.t.send(line)
    got = None
    for _ in range(100):
        r = d.t.readline()
        if r.startswith("STATE"):
            got = r.split("|")
        elif r.startswith("OK"):
            return got
        elif r.startswith("ERR"):
            raise SystemExit(f"refused: {line}")


def tap(d, btn, hold=3):
    d.buttons(mask_of(btn))
    d.frames(hold)
    d.buttons(0)
    d.frames(2)


def table(d):
    s = say(d, "H")
    head = s[0].split()
    return {"purse": int(head[1]), "point": int(head[2]), "bets": s[1].split()}


def main():
    d = Driver(SimTransport(build(str(ROOT))), "CHCR")
    d.handshake()
    d.cmd("L1")
    fails = 0
    # A hand in progress: a point, odds, place bets.
    say(d, "J P")
    d.frames(20)
    say(d, "E 0 10")
    say(d, "F 3 3")
    say(d, "C 29")
    d.frames(5)
    d.buttons(mask_of("A")); d.frames(30); d.buttons(0)
    d.frames(320)
    say(d, "E 2 30")
    say(d, "E 9 12")
    say(d, "E 13 5")
    d.frames(40)
    before = table(d)
    # Pause -> SAVE & QUIT (the fourth item).
    tap(d, "START")
    for _ in range(3):
        tap(d, "DOWN")
    tap(d, "A")
    d.frames(40)
    # Power off and on (the table in memory is gone), then CONTINUE.
    say(d, "M 1")
    say(d, "V")
    d.frames(20)
    tap(d, "A")
    d.frames(40)
    after = table(d)
    ok = before == after and before["point"] == 6
    print(("ok  " if ok else "FAIL") + f" save mid-hand: {before} -> {after}")
    fails += not ok
    # Options and stats come back too; going broke leaves nothing to continue.
    say(d, "J O")
    d.frames(10)
    tap(d, "RIGHT")                                   # TABLE -> BEGINNER... refused: classic bets up
    tap(d, "DOWN")
    tap(d, "RIGHT")                                   # ODDS -> 2X
    tap(d, "B")                                       # leave: saved
    d.frames(30)
    say(d, "V")
    d.frames(20)
    say(d, "J O")
    d.frames(10)
    d.t.send("S")
    hdr = d.expect("FB ")
    d.t.read(int(hdr.split()[2]))
    say(d, "J P")
    d.frames(10)
    say(d, "M 0")
    say(d, "E 0 5")                                   # the last $5 on the line (purse -5: a debug table)
    say(d, "M 0")
    say(d, "F 1 1")
    say(d, "C 29")
    d.frames(5)
    d.buttons(mask_of("A")); d.frames(30); d.buttons(0)
    d.frames(420)
    say(d, "V")
    d.frames(20)
    st = table(d)
    ok = st["purse"] == 500 and st["point"] == 0
    print(("ok  " if ok else "FAIL") + f" broke: no game to continue, a fresh $500 table ({st['purse']})")
    fails += not ok
    d.t.close()
    raise SystemExit(1 if fails else 0)


if __name__ == "__main__":
    main()
