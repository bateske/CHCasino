"""Copy CHSd into the games that read the SD card, or check their copies.

    python tools/vendor.py            copy into every game below that exists
    python tools/vendor.py --check    exit 1 if any game's copy differs
    python tools/vendor.py GAME ...   only these (folder names or paths)

An Arduino sketch compiles only what is inside its own folder (or an
installed library), so each game carries a copy of CHSd in src/sd, and the
simulator stand-in in tools/chsim/host. The copies are generated: edit CHSd,
run its tests (tests/run_tests.py), then run this. Each copied file starts
with a line saying so. The stand-in's card variable is the game's own
(CHWD_CARD, ...).
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WORKSPACE = ROOT.parents[2] / "games"  # CHCasino/games

# Each game: its card variable, and where else it keeps CHSd files.
GAMES = {
    "CHWords": {"env": "CHWD_CARD"},
    "CHCrossword": {"env": "CHCW_CARD", "extra": {"tools/fatimg.py": "tools/puzzles/fatimg.py"}},
    "CHWordWheel": {"env": "CHWW_CARD"},
}

FILES = {
    "src/SdSpi.h": "src/sd/SdSpi.h",
    "src/SdSpi.cpp": "src/sd/SdSpi.cpp",
    "src/Fat.h": "src/sd/Fat.h",
    "src/Fat.cpp": "src/sd/Fat.cpp",
    "host/VCard.h": "tools/chsim/host/VCard.h",
    "host/sd_host.cpp": "tools/chsim/host/sd_host.cpp",
}


def version():
    for line in (ROOT / "library.properties").read_text().splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    return "?"


def render(src, game):
    text = (ROOT / src).read_text(encoding="utf-8").replace("\r\n", "\n")
    text = text.replace("@CARD_ENV@", GAMES[game]["env"])
    note = f"CHSd {version()} (generated: edit CHSd/{src}, then run CHSd/tools/vendor.py)"
    if src.endswith(".py"):
        # After a module docstring's first line would break nothing, but a
        # comment line first is simplest.
        return f"# {note}\n{text}"
    return f"// {note}\n{text}"


def targets(game):
    out = dict(FILES)
    out.update(GAMES[game].get("extra", {}))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("games", nargs="*")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    names = [Path(g).name for g in a.games] or list(GAMES)
    bad = 0
    for game in names:
        if game not in GAMES:
            sys.exit(f"{game}: not a CHSd game (see GAMES in {Path(__file__).name})")
        gdir = WORKSPACE / game
        if not gdir.is_dir():
            print(f"{game}: not here, skipped")
            continue
        changed = []
        for src, dst in targets(game).items():
            want = render(src, game)
            path = gdir / dst
            have = path.read_text(encoding="utf-8").replace("\r\n", "\n") if path.exists() else None
            if have == want:
                continue
            changed.append(dst)
            if not a.check:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(want, encoding="utf-8", newline="\n")
        if a.check:
            print(f"{game}: {'differs: ' + ', '.join(changed) if changed else 'up to date'}")
            bad += bool(changed)
        else:
            print(f"{game}: {'updated ' + ', '.join(changed) if changed else 'up to date'}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
