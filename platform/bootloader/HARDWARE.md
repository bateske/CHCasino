# Hardware steps

Everything that could be checked without a board was done on the PC
(`test/native`, see [README.md](README.md)). These steps put the bootloader
on a real CHGame. They need a person to press buttons and switch the power,
and a computer with the board on USB: a local Claude Code session (Claude
Desktop, or `claude remote-control` in this repository) can run the commands.

The steps are ordered so each change meets the hardware alone:
- **HW1** tries the card, panel and menu code as an ordinary program, under
  the bootloader already on the board.
- **HW2a** changes the bootloader without the menu.
- **HW2b** adds the menu.

Every step can be rolled back over USB, and the factory ISP is the last
resort ([recovery.md](../board/docs/recovery.md)).

## Before starting

- **Software.** This branch checked out. Python 3 with `pip install -r
  tools/requirements.txt`. arduino-cli with `CHGame:ch32v@0.2.4`
  (CLAUDE.md, Setup).
- **Commands.** Run from the repository root. `UP` is
  `python platform/bootloader/host/py/chgame_upload.py`, the uploader with
  the self-update command. `chgame-upload` is the board package's own tool;
  either uploader works for `flash`.
- **Another session using the board.** Check that nothing else is using it
  (CLAUDE.md, "The device"). `UP probe` lists the board's port and what it
  is running.
- **Rollback image.** Keep `platform/board/arduino/CHGame/bootloaders/CHGame/chgame_bootloader.bin`
  (the 0.2.4 bootloader) at hand.
- **What gets flashed.** The binaries are in [release/](release), with
  SHA-256 sums. `tools/dist.sh` rebuilds them byte-identical.
- **Card contents.** `python tools/sdcard/mkcard.py` builds and packs all 20
  games plus the SD card reader into `out/sdcard/`.

## HW1: the menu as a program (the bootloader is not touched)

1. **Load the card.**
   ```
   cd utilities/CHSDtoUSB
   arduino-cli compile -b CHGame:ch32v:CHGame --library ../../platform/libraries/CHGfx .
   arduino-cli upload  -b CHGame:ch32v:CHGame -p <PORT> .
   ```
   - A drive appears (vendor "CHGame"). Copy everything in `out/sdcard/`
     to its root, then eject it.
   - The card must be FAT32 or FAT16. `python tools/chgpack.py info <drive>`
     should list 21 packages, all "ok".
2. **Upload the dry-run menu.** The board enumerates on a new port after
   CHSDtoUSB.
   ```
   UP flash platform/bootloader/release/chgame_menu_dryrun.bin --run --port <PORT>
   ```
   This build has no USB, so the port disappears, which is expected.
3. **What to check and report:**
   - **The panel.** The menu appears: a gold "CHGAME" on dark green, 21
     titles sorted A-Z (BACKGAMMON first), and "1/21" in the footer. If the
     gold looks cyan or the green looks purple, red and blue are swapped:
     report it.
   - **Keys.** UP/DOWN move and repeat when held; LEFT/RIGHT page by 10.
   - **The card.** A on a few games shows "DRY RUN OK" and "x.x MS PER
     BLOCK" after a moment, without installing anything. Note the
     milliseconds.
   - **The SD clock.** START cycles "SD 24 MHZ", "SD 12 MHZ" and "SD 6 MHZ".
     Try A at each speed and note any failure ("CARD READ ERROR"). The menu
     uses 12 MHz unless this says otherwise.
   - **A second card**, if one is at hand: one formatted on another
     computer, or FAT16.
4. **Leave.** Press SELECT. The board resets into the 0.2.4 bootloader's USB
   mode: the LED blinks and the port is back.

## HW2a: the trimmed bootloader, still without the menu

This changes the proven USB upload path: page writes now go through the
shared update code, there is no second erase, and RUN resets instead of
jumping. It changes nothing else.

1. **Install it** (from HW1's USB mode, or from any game: the uploader
   touches the port itself).
   ```
   UP selfupdate platform/bootloader/release/chgame_boot_nomenu.bin --yes
   ```
   - The board resets into the new bootloader. The staging erased the
     sketch, so it waits in USB mode.
   - `UP info` reports `BOOT_VERSION 2`.
2. **The HIL suite.** `test_protocol.py` destroys the installed sketch.
   ```
   python platform/bootloader/test/hil/test_protocol.py --port <PORT>
   python platform/bootloader/test/hil/test_powercut.py arm --at 50    # switch off when told
   python platform/bootloader/test/hil/test_powercut.py verify         # after switching on
   ```
3. **A normal upload.** `cd games/CHFour && python tools/device.py upload`.
   The game runs. Switch off and on: it runs again.

## HW2b: the menu bootloader

1. **Install it**, from a running game.
   ```
   UP selfupdate platform/bootloader/release/chgame_sdboot.bin --yes
   ```
2. **Switch off and on with the card in.** The menu appears in under half a
   second. No game is installed, because the staging erased it.
3. **Record the MCU marking** (spec section 3).
   - Hold BOOT, switch on, run `wchisp info`. The expected chip is
     `CH32X035G8U6`.
   - Switch off and on without BOOT to leave the factory ISP.

## HW3: the matrix

Tick each line and note anything odd. Each line is one action and its
expected result.

**Menu and install**
- [ ] Power-on with the card shows the menu, nothing preselected beyond the
      first title.
- [ ] A on BACKGAMMON: "INSTALLING", gold bar, about 1-2 s, then the game
      starts. Time it.
- [ ] Switch off and on: the menu shows BACKGAMMON with a red chip, and
      selected. A starts it at once, with no bar.
- [ ] Install each of the other 19 games and the SD card reader once; each
      one starts and plays.
- [ ] CHWords, CHWordWheel and CHCrossword find their card data
      (dictionary, phrases, packs).
- [ ] Hold UP for 3 s: the cursor repeats. LEFT/RIGHT page.

**USB and uploads**
- [ ] With the menu on screen, upload from the IDE (or
      `cd games/CHFour && python tools/device.py upload`). The upload works,
      CHFour starts, and on the next power-on the menu shows
      INSTALLED PROGRAM.
- [ ] With the menu on screen, `UP probe` or `UP info` answers, and the menu
      stays (it does not switch to "USB UPLOAD").
- [ ] From a running game, upload: the touch reaches the bootloader as
      before.
- [ ] The touch with no upload (`UP touch`) shows "USB UPLOAD / B: MENU";
      B goes back to the menu.
- [ ] Hold B while switching on: the screen does not change, and the LED
      blinks at 2 Hz (USB mode). Release B, press it again: the menu.

**Cards**
- [ ] No card: the installed game starts straight away. With no game
      installed: "NO GAMES FOUND" and USB mode.
- [ ] Card in, no GAMES folder (another card): the installed game starts.
- [ ] Pull the card while the menu is up, then press A on another game:
      "CAN'T INSTALL / CARD READ ERROR", and the old game is still there.

**Damaged packages**
- [ ] Copy these into `GAMES/` through the SD card reader:
      `python platform/bootloader/test/native/run_tests.py -k boot` writes
      the zoo to `platform/bootloader/test/native/build/pk/`. Take
      BADHCRC, NOTCHG, WRONGTGT, ZEROLEN, TRUNC, BOOTIMG and BADPCRC.
- [ ] Each is greyed or refused with its message, nothing is erased, and
      the installed game still starts.

**Fragmentation**
- [ ] Through the SD card reader, fill the card with a few large filler
      files, delete every other one, then copy a game package.
- [ ] `python tools/chgpack.py info` on an image of the card (or `chkdsk`)
      shows it fragmented.
- [ ] It installs and runs.

**Power cuts**
- [ ] Start installing a large game (CHWords) and switch off during the gold
      bar, at three different points.
- [ ] Each time, the next power-on shows the menu with no game marked
      installed.
- [ ] A installs the game again, correctly.

**The boot region after all of the above**
- [ ] With the board at the menu, the whole region 0x0000-0x2FFF read back
      over USB matches the image (spec section 39):
      ```
      python platform/bootloader/tools/bootcheck.py platform/bootloader/release/chgame_sdboot.bin
      ```
- [ ] `test_protocol.py` and the power-cut test from HW2a pass again on this
      bootloader.

## HW4: final state

- [ ] The menu bootloader installed (`release/chgame_sdboot.bin`).
- [ ] The card holds all 20 casino games and the SD card reader.
- [ ] A game installed. After any debug upload, put the release build back
      (CLAUDE.md).

## Rolling back

- **Back to 0.2.4 over USB:**
  `UP selfupdate platform/board/arduino/CHGame/bootloaders/CHGame/chgame_bootloader.bin --yes`.
  The menu bootloader still has the self-update commands, which is the
  point of keeping them in the release.
- **If USB is gone:**
  1. Hold BOOT across power-on (factory ISP).
  2. `wchisp flash <image>`. `wchisp flash` erases only what it writes
     (gotcha 5), so the old sketch stays.
  3. Re-upload or reinstall a game afterwards.
