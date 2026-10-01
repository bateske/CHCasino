"""Saving in the simulator: SAVE & QUIT in the middle of a turn, CONTINUE
from the title, and the game must come back exactly (purse, whose turn, the
dice on the table, the held ones, every card). Also: a finished game leaves
nothing to continue but keeps the purse.

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
            got = r.strip()
        elif r.startswith("OK"):
            return got
        elif r.startswith("ERR"):
            raise SystemExit(f"refused: {line}")


def tap(d, btn, hold=3):
    d.buttons(mask_of(btn))
    d.frames(hold)
    d.buttons(0)
    d.frames(2)


def idle(d):
    for _ in range(4000):
        st = say(d, "H").split("|")[0].split()
        if st[5] == "0" and st[6] == "0":
            return
        d.frames(4)
    raise SystemExit("the game never came to rest")


def roll(d):
    d.buttons(mask_of("A")); d.frames(30); d.buttons(0)
    d.frames(10)
    idle(d)
    d.frames(30)


def main():
    d = Driver(SimTransport(build(str(ROOT))), "CHYD")
    d.handshake()
    d.cmd("L1")
    fails = 0
    # Versus the dealer: one full round each, then mid-turn with two dice held.
    say(d, "R 99")
    say(d, "J C")
    d.frames(20)
    say(d, "F 2 2 2 5 6")
    roll(d)
    say(d, "G 1")                                     # twos: 6
    idle(d)                                           # the dealer's whole turn
    d.frames(40)
    say(d, "F 6 6 1 3 4")
    roll(d)
    tap(d, "A")                                       # hold the first die
    tap(d, "RIGHT")
    tap(d, "A")                                       # and the second
    d.frames(10)
    before = say(d, "H")
    held = before.split("|")[1].split()[5]
    # Pause -> SAVE & QUIT (the fourth item).
    tap(d, "START")
    for _ in range(3):
        tap(d, "DOWN")
    tap(d, "A")
    d.frames(40)
    # Power off and on (the game in memory is gone), then CONTINUE.
    say(d, "V")
    d.frames(20)
    tap(d, "A")
    d.frames(40)
    after = say(d, "H")

    def game(s):                                      # all but the cursor
        a, b, c = s.split("|")
        return a.split()[1:5], b, c

    ok = game(before) == game(after) and held == "3"
    print(("ok  " if ok else "FAIL") + f" save mid-turn:\n     {before}\n     {after}")
    fails += not ok
    # The dice kept their holds: rolling again changes only the other three.
    say(d, "F 1 1 5 5 5")
    tap(d, "DOWN")
    roll(d)
    dice = say(d, "H").split("|")[1].split()[:5]
    ok = dice == ["6", "6", "5", "5", "5"]
    print(("ok  " if ok else "FAIL") + f" held dice stay after continue: {dice}")
    fails += not ok
    # The house plays the game out; then there is nothing to continue.
    say(d, "A 1")
    for _ in range(150):
        st = say(d, "H")
        if st.split("|")[2].split()[-1] == "1":
            break
        d.frames(400)
    purse = st.split()[1]
    d.frames(60)
    say(d, "A 0")
    say(d, "V")
    d.frames(20)
    tap(d, "A")                                       # no CONTINUE: this starts a new game (ante $5)
    d.frames(40)
    st = say(d, "H")
    head = st.split("|")[0].split()
    ok = st.split("|")[2].split()[-1] == "0" and head[3] == "0" and int(head[1]) == int(purse) - 5
    print(("ok  " if ok else "FAIL") + f" finished game: purse {purse} kept, a new game antes $5 ({head[1]})")
    fails += not ok
    d.t.close()
    raise SystemExit(1 if fails else 0)


if __name__ == "__main__":
    main()
