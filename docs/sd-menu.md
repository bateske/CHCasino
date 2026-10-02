# The SD game menu

The CHGame bootloader has a game menu built in. Put games on a microSD card,
switch the CHGame on, pick one, and play. No PC is needed to change games.
If you know the Arduboy FX, it works the same way: the menu appears at
power-on, and the game you played last is already selected.

![The menu with the casino games on the card](../platform/bootloader/docs/menu.png)

## Using it

**Switching on.** The menu appears every time, with the installed game
highlighted. It is marked with a red chip.

| Button | In the menu |
|---|---|
| UP / DOWN | move (hold to repeat) |
| LEFT / RIGHT | a page (10 games) up or down |
| A or START | play |
| B | close a message |

- **The game you already have.** Pressing A on the highlighted game starts it
  at once. Nothing is written to flash.
- **Another game.** A on any other game installs it, which takes about a
  second. The screen shows "INSTALLING" and a progress bar. The game then starts.
  The new game is checked completely before anything is erased, so a damaged
  file never costs you the game you had.
- **Back to the menu.** Hold **START for 3 seconds** in any game. Switching
  the CHGame off and on works too, as on an Arduboy FX. The games don't
  mention it on screen: it is a feature of the platform, like a phone's home
  button.
  - Without a card, or under the old bootloader with no menu, the same hold
    simply restarts the game.
  - Anything since the game last saved is lost, as when switching off. The
    first press of START still does its usual job (pause, or start on a
    title screen) before the 3 s are up.
- **No card, or a card without games.** The installed game starts straight
  away, as it did before the menu existed.

**Saved games.** Every game keeps its save in the same small area of
flash. When you switch games, the old game's save stays there until the new
game saves for the first time; after that it is gone. Game settings and
saves are not kept per game yet.

**Flash wear.** Showing the menu never writes flash. Installing a game
rewrites only the parts that differ from the game already there. The
microcontroller's flash is rated for many thousands of rewrites, so in
practice you can switch games as often as you like.

## Preparing a card

- **Format.** The card must be FAT32 or FAT16. Cards up to 32 GB come that
  way. A 64 GB or larger card is usually exFAT, which the menu cannot read;
  reformat it as FAT32.
- **Folder layout.** Games go in a folder called `GAMES` at the top of the
  card:

  ```
  GAMES/
      BACKGAMN.CHG
      BLACKJCK.CHG
      ...
  WORDS.DIC        (CHWords' dictionary: stays at the top, where the game looks)
  PHRASES.BNK      (CHWordWheel's phrases)
  CHCW/            (CHCrossword's puzzle packs)
  ```

- **Names.** A `.CHG` file is a game package. The menu shows the title stored
  inside it, so the file name only has to be a short `NAME.CHG` (8 letters at
  most).
- **Order and fragmentation.** Files can be copied in any order and can be
  fragmented. Up to 128 games are listed, sorted by title.
- **Getting all the casino games onto a card.** Run
  `python tools/sdcard/mkcard.py`. It builds every game and writes the card's
  contents to `out/sdcard/`; copy everything in that folder to the card.

**Copying files without a card reader.** Pick **SD CARD READER** in the
menu. The CHGame becomes a USB drive on the PC. Copy games into `GAMES/`,
eject the drive, then hold B for a second (or START for 3 s, or switch off
and on) to go back to the menu.

## Messages

| Message | Meaning |
|---|---|
| NO GAMES FOUND / SD CARD: /GAMES | No game is installed, and there is no card or no `GAMES` folder with packages. The CHGame waits for a card or a USB upload. |
| CAN'T INSTALL / FILE DAMAGED | The package failed its check (CRC). Nothing was erased. Copy the file again. |
| CAN'T INSTALL / NOT A GAME FILE | The file is not a CHG package. |
| CAN'T INSTALL / WRONG DEVICE | The package was made for a different board or memory layout. |
| CAN'T INSTALL / NEWER FORMAT | The package needs a newer bootloader. |
| CAN'T INSTALL / BAD FILE SIZE | The package is truncated or too large. |
| CAN'T INSTALL / CARD READ ERROR | The card stopped answering. Reseat it and try again. |
| INSTALL FAILED / NO GAME INSTALLED | The card failed partway through an install. The old game is gone, but no half-written game will ever run. Pick a game again. |
| USB UPLOAD / B: MENU | A PC is uploading a sketch, or asked for the bootloader. Press B to go back to the menu. |

A greyed-out title is a file the menu could not read as a package. Its file
name is shown instead of a title.

## Developers

- **Uploading from the Arduino IDE.** This works as before, including while
  the menu is on screen. The uploaded sketch then starts directly. On the
  next power-on the menu shows it as **INSTALLED PROGRAM** at the top.
- **Adding your own game to the card.** Package your sketch's `.bin`. In the
  Arduino IDE, *Sketch > Export compiled Binary* writes it next to the
  sketch. Then run:

  ```
  python tools/chgpack.py pack MyGame.ino.bin MYGAME.CHG --title "MY GAME"
  ```

  Copy `MYGAME.CHG` to `GAMES/`. [chg-format.md](chg-format.md) describes the
  file and what a game needs to know. In short: nothing changes for your
  sketch.
- **Returning to the menu from a game.** Every CHCasino game has it built in.
  The shared core (`src/CHGame.*`) checks for START held 3 s in
  `pollButtons()`.
  - `arduboy.exitToMenu()` leaves on purpose, for example from a QUIT item.
  - `arduboy.startExits = false` in `setup()` turns the hold off, for a game
    that needs long START holds for itself.
  - Without CHCasino's core, call `NVIC_SystemReset()`. Any reset that is not
    an upload request shows the menu.
- **Recovery.**
  - Hold **B** while switching on: the bootloader skips the card and the
    menu and waits in USB upload mode.
  - If even that fails, use the factory ISP: hold BOOT across power-on, see
    [recovery.md](../platform/board/docs/recovery.md).
- **The bootloader itself.** It lives in
  [platform/bootloader](../platform/bootloader): sources, tests, sizes, and
  how to install it.
