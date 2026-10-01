# CHChess interaction model, cursor feel and iso rendering: reference for CHRoulette

All paths are under `CHChess\`. Logic runs at a fixed 60 Hz (`config.h:38` `CHCH_FPS 60`). `Frame.cpp:83-89` runs up to 3 catch-up ticks before each draw, and every timing below is in those 60 Hz ticks.

**Version warning:** `CHGfx` is **v1.1.0** and has no `gfx_fillEllipse`. The installed copy CHChess builds against is **v1.3.0** at `<sketchbook>\libraries\CHGfx` (it has `gfx_fillEllipse(cx,cy,rx,ry,c)` at CHGfx.h:356). CHRoulette must build against 1.3.0.

---

## 1. Cursor feel numbers to match exactly

| Parameter | Exact value | Where |
|---|---|---|
| D-pad auto-repeat | Fires on the first held frame (`h==1`), then whenever `h>18 && (h-18)%5==0`. So the first repeat is at held=23 (~383 ms), then every 5 frames (12/s). Defaults `delay=18, rate=5`. | `src/CHGame.h:43`, `src/CHGame.cpp:83-91` |
| Cursor move sfx | `Sfx::Cursor` = `S(2100,0,10)` (2.1 kHz, 10 ms, prio 0). No spot that way: `Sfx::Deny` | `src/audio/Audio.cpp:31`, `Screens.cpp:376-377` |
| Glove glide | Q4 world coords. Each tick `fx16 += ((x<<4)-fx16)>>1` (same for y): the distance halves every frame, with no snap. A 40 px hop goes 20,10,5,2.5,1.25 and is under 1 px in about 6 frames (~100 ms). | `Stage.cpp:618-625` |
| Glove rest point | Fingertip at `worldOf(sq).y - topOf(sq)`. `topOf = zoomed(art.ay + 1)` (1 px above the piece's top), or `zoomed(2)` on an empty square. | `Stage.cpp:158-168` |
| Glove draw | `sprite4(HAND, x - zoomed(HAND_TIP), y - zoomed(HAND[1]) + bob - 1, remap, zscale())`. `HAND_TIP=5`, `HAND[1]=16`. | `Stage.cpp:749-757` |
| Idle bob | `bob = (isin((frame>>3)*40)*2)>>8`, range −2..+2 px (positive = down). It steps at 7.5 Hz (every 8 frames), period 256/40 = 6.4 steps ≈ 51 frames (0.85 s), and is then `zoomed()`. | `Stage.cpp:752,754` |
| Tap | `tapT` runs 1..12 (`if (tapT && ++tapT > 12) tapT=0`). Dip = `(tapT<6 ? tapT : 12-tapT)/2`, giving 0,1,1,2,2,3,2,2,1,1,0,0 px down over 12 frames (0.2 s), plus `Sfx::Select` (1700 Hz 18 ms, then 2600 Hz 30 ms). **Only the CPU's glove taps**; a human select does not set `tapT`. | `Stage.cpp:587,596,753`, `Audio.cpp:32` |
| Deny flash | `deny()` sets `denyT=24` and plays `Sfx::Deny` (900 to 650 Hz sweep, 70 ms). The glove uses `RM_ALERT` while `denyT & 4`, i.e. red on denyT 23-20, 15-12 and 7-4: three 4-frame blinks. The plate's " NO MOVES" word flips RED/SILVER on the same phase. | `Stage.cpp:189-192,522,756,804` |
| Hover outline | The hovered piece's INK index is remapped to FX_A. Palette mode `HOVER` sets `FX_A = tri(ticks>>1)*0x111`, a grey ramp 0x000 to 0xFFF and back in 16 steps over a 64-frame period (~1.07 s). | `Palette.cpp:72,82`, `Stage.cpp:721-725` |
| Picked-up outline | Outline cycles `fx::RAIN[(frame>>3)%5]` = {RED, GOLD, FELT_LT, CYAN, BLUE}: 8 frames per colour, 40-frame cycle. | `Stage.cpp:720`, `Fx.cpp:61` |
| Picked-up lift | INK ellipse shadow `gfx_fillEllipse(x,y,zoomed(3),(tileH+2)/5,INK)`. The piece is drawn `zoomed(3) + (zoomed(isin((frame>>3)*48))>>8)` higher: 3 px plus a ±1 px sine with a ~43-frame period. | `Stage.cpp:731-734` |
| Palette mode | Set every tick: `sel!=0xFF ? TARGETS : gloveOn() ? HOVER : CASINO`. | `Stage.cpp:534` |
| Cursor tile border | `tileBorder(cur,0, sel!=0xFF?WHITE:FX_B, GOLD, ph)` with `ph=frame>>3`. Dashes 2 rows long, marching at 7.5 Hz. | `Stage.cpp:640,667`, `Iso.cpp:159-169` |
| FX_B in CASINO/HOVER | Triangle GOLD (0xFC2) to white over a 32-frame period: `g=12+t*3/15, b=2+t*13/15`. | `Palette.cpp:83-86` |
| TARGETS mode | FX_A uses `SHIMMER[]` (0x6EF to 0xFFF, cyan to white). FX_B uses `PULSE[]` (0xE12 to 0xFC2, red to gold). Both run at `tri(ticks*2)>>1`, a 16-frame period; FX_B is half a period behind. | `Palette.cpp:40-41,78-80` |
| Cursor on a legal target | `tileTint(cur,in,cap?RED:CYAN,(frame>>4)&1)`: alternates dithered and solid every 16 frames. | `Stage.cpp:664` |
| Capturable pieces | Flash on `frame & 8` (8 on, 8 off). `RM_HIT` turns the whole sprite WHITE; `RM_PREY` (RED) is used for the one under the glove. | `Stage.cpp:726-727` |
| Camera follow | Q4, `cx16 += dx >> aimShift`; snaps once within 16 Q4 (1 px). Shift 2 for the human cursor (25%/frame), 3 for CPU/inspect/mate (12.5%), 1 for the 2P hand-over whip (50%). | `Stage.cpp:148-155` |
| Menu highlight blink | `roundRect(..., (frame & 16) ? FX_B : GOLD)`. Choice arrows bob 1 px on `(frame>>3)&1`. | `Screens.cpp:129,233` |

---

## 2. D-pad navigation: nearest-in-direction with wrap (`src/states/Screens.cpp`)

This lives in Screens, not Stage. `spots()` (311-334) builds the list of legal cursor stops:
- With no piece held: your pieces.
- In check: only pieces that can move, cached per position key `plies<<16|lastFrom<<8|lastTo`.
- Holding a piece: its destinations.

`nearest()` (340-358) is the core algorithm. It works on any set of world points; its only dependency is `iso::worldOf`:

```cpp
int along = dx*ux + dy*uy, side = dx*uy - dy*ux;  if (side<0) side=-side;
if (!ux && !uy) along = |dx| + |dy|;                              // (0,0): plain nearest
int32_t sc = along > 0 ? along + 2*side : 4*along + side;
if (along > 0 && sc < bestS) best = ...;                          // ahead: close, and side drift penalised x2
if (along <= 0 && sc < backS) back = ...;                         // wrap: most negative along = FARTHEST the other way
return best != 0xFF ? best : back;
```

How it behaves:
- Directions are in **screen/world space**, so on the iso view "up" is a board diagonal.
- Two keys pressed together give a diagonal move (ux and uy both set; the vector is not normalised).
- With nothing ahead, it wraps to the farthest spot behind, so repeated presses cycle through every spot.

`playInput()` (360-395):
- Re-snaps the cursor onto a valid spot if a new turn left it off one (`nearest(...,0,0)`).
- The D-pad is ignored while B is held, because B+D-pad means inspect.
- A press is ignored while B is held (`!bHeld`).
- A on a piece with no moves calls `stage::deny()`.
- A on a movable piece calls `stage::select()` and moves the cursor straight to the nearest destination.

`playUpdate` (485) gates input on `match::humanToMove() && !stage::busy()`.

The debug `R` command (693-720) runs a BFS over `nearest()` to produce D-pad routes for scripted `goto`; it can be reused for test automation.

For roulette, replace `iso::worldOf(id)` with a bet-spot centre table. The scoring handles irregular layouts (0, dozens, columns, split/corner spots) unchanged.

---

## 3. Inspect (B held) and the hold bar (`Screens.cpp:279-301, 507-512`; `Stage.cpp:207-213, 560-569`)

**Constants and state**
- `HOLD_B = 32` frames; `bHeld` saturates at 255.
- `bUsed` becomes true once a direction is pressed, or once `bHeld > (piece held ? 32 : 8)`.
- `stage::inspect(bHeld && bUsed, dx, dy)`.

**Hold bar**
- `holdBar() = bHeld*(128/HOLD_B)` (4 px per frame), shown only while holding a piece and `!bUsed`.
- Drawn as `gfx_fillRect(0,10,bar,2,CYAN)`, just under the HUD's gold line at y=9. It is full (128 px) at 32 frames.

**Releasing B early** (`justReleased(B) && !bUsed` while holding a piece) counts as a tap:
- `deselect()`
- `setCursor(s)` (back onto the piece)
- `Sfx::Cursor`

**Inspecting**
- `zoomTo = 10`; `tileH` steps by one per *drawn* frame (`zoomDrawn` latch, `Stage.cpp:527-531`), so 5 to 10 takes 5 frames.
- Camera shift is 3 (slow).
- Aim:

```cpp
x = insDX * (insDY ? 4 : 8) * hw();  y = 8*hh() + insDY * (insDX ? 4 : 8) * hh();
```

  A straight push goes to a board corner; a diagonal push goes to the middle of an edge.
- Inspect is ignored on the flat map (`on && !flat`).

`screens::holdFrames()` (777) keeps full-rate frames during CPU think while B is held or a menu is open.

---

## 4. Stage (`src/stage/Stage.h` 52 lines, `Stage.cpp` 880 lines)

### Public API (`Stage.h:12-50`)

- **Lifecycle and drawing:** `begin()`, `update()`, `busy()`, `overShown()`, `render(frame, ui)` (returns bool), `invalidate()`, `profile(us*)`.
- **Cursor and selection:** `cursor()`, `setCursor(sq)`, `flipped()`, `select(sq,to*,cap*,n)`, `deselect()`, `selected()`.
- **Prompts:** `waiting()`, `acknowledge()`, `setBlocked(b)`, `deny()`.
- **View:** `enum View : uint8_t { NORMAL, MAP, VIEWS }`, `view()`, `setView(v)`, `setZoom(tileH)`, `inspect(on,dx,dy)`.
- **CPU and misc:** `thinkPick()`, `setFast(b)`, `opponentName`, `NAMES[7]`, `renderScene(frame)`, `drawPieceAt(piece,x,y)`.

### Remap tables (`Stage.cpp:25-31`)

These are 16-entry art-colour to screen-colour tables passed to `sprite4`. **`sprite4` dereferences the remap unconditionally, so always pass one; use `RM_ID` for identity.**

- `RM_ID` = identity.
- `RM_CPU`: `[8]GOLD→RED, [9]WOOD→WINE` (the CPU glove's red cuff).
- `RM_HIT`: INK stays INK, everything else WHITE.
- `RM_PREY`: INK stays INK, everything else RED.
- `RM_ALERT`: `[1]WHITE→RED, [5]SILVER→WINE, [8]GOLD→RED, [9]WOOD→WINE` (the whole glove goes red).

**Outline idiom** (`Stage.cpp:716-735`). Art is drawn with an INK outline, so a highlight is one changed entry:

```cpp
memcpy(hl, beat ? RM_PREY : remapFor(p), 16);  hl[INK] = edge;  rm = hl;   // edge = FX_A (hover) or RAIN[..] (picked)
```

### State and the event queue (`Stage.cpp:39-101, 505-519`)

`match` pushes `Event`s into an 8-slot ring. `stage::update()` drains them only while `!busy()`; `EV_START` cuts in regardless.

```cpp
while (match::peekEvent(e) && (!busy() || e.type == match::EV_START)) { match::popEvent(e); switch... }
```

```cpp
bool busy() { return mv[0].on || mv[1].on || fly.on || holdT || picking || topT || handT || outWait || waitPress
              || fx::particles() || (tileH != zoomTo && !flat); }
```

`match::update(stageBusy)` (`Match.cpp:184-216`) will not advance its phase while the stage is busy or events are queued. This is the CHBlackjack Presenter pattern; reuse it as-is for spin, then ball settle, then payout.

### waiting / acknowledge (`Stage.cpp:426-442`, `Screens.cpp:479-482`)

- `onCheck` shows `fx::banner("CHECK!", B_RED, 36, 70)` and sets `holdT=40`.
- `waitPress` is set when the check is against a human. `fx::holdBanner(waitPress)` keeps the banner up.
- Any `justPressedMask()` calls `acknowledge()` and plays `Sfx::Select`.
- A blinking "PRESS A" plate appears at y=100 when `waitPress && holdT<20 && (frame&32)` (32 frames on, 32 off) (`Stage.cpp:809-813`).

### CPU acting like a player (timeline)

1. **`onTurn(!human)`** (276-288): `thinking=true`, `holdT=24`, `fingerSq` = its own king. The red-cuff glove goes there, the camera follows at shift 3, and the HUD shows `opponentName` in FX_B with `(frame>>4)&3` dots.
2. **During the search** (`Frame.cpp:30`, constants `FIRST_MS=300, SEARCH_MS=2000, BURST_MS=450, BOB_MS=133, TICK_MS=2000`):
   - One frame every 133 ms, with `frameCount |= 7` so each one advances the bob by exactly one step.
   - Every 2 s, a 450 ms full-rate burst; `thinkPick()` moves the glove to the root move's from-square.
   - A soft tick/tock every 2 s.
   - Roulette has no blocking search, so this whole scheduler is chess-only.
3. **`onPick(from,to)`** (307-314), then per tick (581-595), with `rest = fast?8:24`, `stay = fast?16:60`:
   - `pickT == rest`: tap and `Sfx::Select`.
   - `pickT == rest+12`: `sel = fingerSq` (the piece lifts with its rainbow outline and the palette switches to TARGETS); `intent = fingerSq = pickTo` (the glove glides there; the destination is tinted and has a WHITE-dashed border).
   - `pickT == rest+12+stay`: second tap.
   - `pickT == rest+24+stay`: `picking=false`, so `busy()` clears and `EV_MOVE` follows.
   - Total: 108 frames (1.8 s) on FUN, 48 frames on QUICK.

This beat sequence (rest, tap, glide, rest, tap) is the "dealer places/sweeps chip" template. `RM_CPU` gives a croupier glove for free.

### Move / land juice (316-424; useful for ball/chip landings)

- **Duration:** `T = (fast ? 10+2d : 14+3d) * slowF` frames, d = Chebyshev distance. `fx::ease(IN_OUT)`.
- **Arc:** `arc > 5` uses a sine arc `(arc*isin(t*128/T))>>8` (knight, arc 12). Otherwise a flat-top lift: 4 frames up, hold, 4 frames down (arc 3).
- **Plain landing:**
  - DUST burst of 16 at speed 30, in the square's own colour
  - `fx::shake(4,1)`
  - `Sfx::Land`
  - `holdT = 12`
- **Capture landing:**
  - SPARK 12 at speed 36 in GOLD, plus STAR 4 at speed 24 in WHITE
  - `shake(10,2)`
  - `CAPTURE_SLOW=2` slow motion
  - the victim tumbles off via `spriteRot` (flyAt, 482-490)
- **Whip zoom:** `zoomTo=10` and `Sfx::Whoosh` when the piece lifts. Pull-back waits (`outWait`) until `!fx::particles()`.

### HUD and bottom plate (`Stage.cpp:764-814`)

**HUD**
- `gfx_fillRect(0,0,128,9,INK)` + `gfx_hline(0,9,128,GOLD)` + `text35(3,2,who, thinking?FX_B:WHITE)`.
- Play view is 128×118 below it (`iso::CX=64, CY=69`).

**Plate**

```cpp
static void plate(const char *const *w, const uint8_t *c, uint8_t n, int y, int grow /*Q8 width*/, int t) {
  pw = ((sum text35Width + 8) * grow) >> 8;  fillRound(64-pw/2, y, pw, 11, 2, NAVY);  roundRect(..., GOLD);
  word i shows from t = 6 + 3i, dropping in from 3 px above over 3 frames }
```

- **Hover use** (`py=116`, static `grow=256`, `t=99`): "KNIGHT G1", " NO MOVES", "KNIGHT TO F3", "KNIGHT TAKES PAWN". Word colours: WHITE name, GOLD square, SILVER/RED connectors.
- **Announce use:**
  - `ANN_FRAMES = 120` (2 s)
  - open: `ease(OUT_BACK, t, 8)`
  - close over the last 8 frames: `(120-t)*32`
  - up to 7 words in `annW`/`annC`
  - while `waitPress`, it freezes at `annT < 40`

### Overlay drawing order (`render`, 846-861)

1. `drawTable` (carpet)
2. `drawBoard`
3. `drawOverlays` (tints/borders, 639-669)
4. `drawPieces` (back to front by depth `u+v`, 0..14; movers inserted at their eased depth, 684-747)
5. `drawFinger`
6. `drawHud`
7. `fx::drawParticles((2*tileH+2)/5)`
8. `fx::drawBanner`
9. `fx::applyShake(10,127)` (the HUD never shakes)

Tile inset for tints is `in = (tileH+2)/5`.

### Redraw skipping (830-861)

- `signature()` is an FNV-1a hash of all visible state, including `frame>>3`, so the bob and the marching borders step at 7.5 Hz.
- If the hash is unchanged, `render` returns false and the old framebuffer is flushed again. Palette animation still runs at 60 Hz for free.
- Any particles, banner, shake, movers or a moving camera force a redraw.
- `ui` is the caller's own signature (overlay, menu selection, bar width).

---

## 5. Iso (`src/iso/Iso.h` 64 lines, `Iso.cpp` 252 lines)

### Projection

The lattice point (u,v) sits at world `((u-v)*hw, (u+v)*hh)` with `hw=2*tileH`, `hh=tileH`. It is a 2:1 dimetric, i.e. 30° elevation (`tools/pieces.py` `ELEV = 30°`).

```cpp
CX=64, CY=69; tileH 5..10 (tiles 20x10 .. 40x20); slab()=(3*tileH+2)/5; zscale()=tileH*256/5 (Q8); zoomed(px)=(px*zscale())>>8
MX=10, MY=26, MW=14, MH=11 (flat map); struct Cam{int x,y; bool flip;}; toScreenX(wx)=wx-cam.x+CX
```

### Tile raster

- `halfWidth(k,th) = k < th/2 ? 2k+1 : 2(th-1-k)+1` samples pixel centres, so the 2:1 edges are clean staircases with no gaps or overlaps (`Iso.cpp:48`).
- `isoSquares` (85-105) builds one pattern row per tile-row and word-copies it into all 8 board rows (RAMFUNCs `copyRow`, `patSpan`, `isoSquares`). This works **only for a 2-colour checkerboard**.

### Other pieces

- **`tileTint(sq,inset,c,solid)`:** checker-dither or solid.
- **`tileBorder(sq,inset,c,c2,phase)`:**
  - Iso: 2-px edge segments, dashes alternating every 2 rows, marching with `phase`.
  - Flat: a rectangle walked clockwise with 3-px dashes (`Iso.cpp:145-156`). This is ready-made marching ants for a top-down betting grid.
- **Slab:** contact shadow first (offset `1+z`, depth `s+2+z`), then left face WINE and right face INK, both with GOLD trim (`Iso.cpp:183-212, 235-239`).
- **Camera framing `aimAt`** (`Stage.cpp:116-133`):
  - leans `(10-tileH)/10` toward the board centre
  - clamps to ±44 px x / ±30 px y of the target
  - clamps to content bounds x `±(8hw+6)`, y `-zoomed(24)..16hh+slab+10`
  - centres an axis if the content fits in 128×118

### Could roulette use it?

**Hard-coded to 8×8:** `toView` (`7-r`), `fromView`, `drawBoard` (`8*hw()`, `16*hh()`), `isoSquares` (`rows=8*th`), the labels, and the content bounds in `aimAt`.

**Generic and copyable:**
- the `worldX`/`worldTop` lattice
- `halfWidth` and the diamond tint/border loops
- `leftFace`/`rightFace` (parametrise 8 as N)
- the Q4 camera with `aimShift`
- the stepped zoom

**Layout fit:**
- A 3×12 betting grid in iso at tileH 5 spans (3+12)·20/2 = 150 px wide, versus chess's 160, so it pans like chess.
- `text35` numbers are axis-aligned. "36" is 7×5 px and fits inside a 20×10 diamond's middle rows (widest row 18 px).
- The red/black/green number cells are not a 2-colour checkerboard, so `isoSquares` doesn't apply; per-cell spans via `tileTint(solid)` would be needed.

**Wheel:** a circle at the same 30° elevation is a 2:1 ellipse, `gfx_fillEllipse(cx,cy,rx,rx/2,c)` (CHGfx 1.3).
- Ball/pocket position: `x = cx + (r*isin(a+64)>>8)`, `y = cy + (r*isin(a)>>8)/2`, with `isin` in 1/256 turn giving ±256.
- Draw back-to-front by the sign of `isin(a)`, the same depth idea as `drawPieces`.

---

## 6. Match (`src/game/Match.h` 89 lines, `Match.cpp` 327 lines)

- **Enums:** `Mode{VS_CPU,TWO_PLAYER}`, `Result{PLAYING,WHITE_WINS,BLACK_WINS,STALEMATE,DRAW_50,DRAW_REPETITION,DRAW_MATERIAL}`, `Reason{BY_MATE,BY_RESIGNATION,BY_RULE}`.
- **Events:** `Ev{EV_START,EV_TURN,EV_THINK,EV_PICK,EV_MOVE,EV_CHECK,EV_OVER}` and `struct Event{type,a,b,piece,captured,capSq,rookFrom,rookTo,promo}` (9 B).
- **Internal phase:** `Phase{OFF,WAIT,HUMAN,CPU,CPU_PICKED,OVER}`.
- **Queue (reusable verbatim):**
  - `q[8]` ring; `push()` drops the oldest when full ("never stall"); `peekEvent`/`popEvent`.
  - `update(stageBusy)` only advances when `!stageBusy && !qCount`.
- **Reusable idea:** no graphics or sound in the logic, so it can be host-tested. Undo/save replays the move history (`hist[128]`, `Record` with `m[76]`). Everything else is chess-specific.

---

## 7. Assets and art workflow

### Sprite format (`span4`, `tools/assets.py:148-166`)

```
[w, h, then per row: n, n × ((len-1)<<4 | colour)]
```

- colour 15 = skip, so **FX_B (15) can't be used in art**
- runs ≤ 16 px; trailing transparency is dropped

`sprite4(data,x,y,remap,scale)` is a RAMFUNC (504 B in SRAM) with a separate fast 1:1 path and Q8 nearest-neighbour scaling (`Draw.cpp:57-98`). `spriteRot` decodes into the 1 KB `gfx_chunkScratch`; art up to 32×60.

### Glove

- `HAND[111]` (`Assets.cpp:95`); 13×16; `HAND_TIP = 5`, the centre column of the bottom row's opaque pixels.
- Source is `tools/art/hand.png`, which overrides the older `hand.txt`.
- Colours: k=INK, w=WHITE, s=SILVER, y=GOLD, b=WOOD. `.` = transparent.

```
...kkkkkkkkk.
..kyyyyyyyyyk
..kbyyyyyyybk
..kkkkkkkkkk.
.kwwwwwwwwwsk
.kwwwwkwkwwsk
kwwswwkwkwwsk
kwwswwswswwsk
.kskwwwwwwwsk
.kskwwswswssk
..kkwwsssssk.
...kwwskkkk..
...kwwsk.....
...kwwsk.....
...kwwsk.....
....kkk......
```

### `Assets.h`

```cpp
struct PieceArt { const uint8_t *data; int8_t ax, ay; }   // anchor = base centre
PIECE_ART[6]; SIDE_REMAP[2][16]                           // from tools/art/sides.txt
```

### Tools

- **`tools/assets.py`:**
  - `PALETTE` must match `Palette.cpp`; `LETTER` map `kwdfgsrmybunpcxz` maps to indices 0-15.
  - Pieces come from `tools/art/pieces/*.png` or `gen/*.png`, each with a `.anchor` file.
  - PNGs must be palette-exact; alpha 0 = transparent.
  - Generates `src/assets/Assets.{h,cpp}` and previews in `build/assets/`.
  - Copyable, with chess names swapped.
- **`tools/sheet.py export|import`:**
  - An indexed-PNG sheet: MASTER / WHITE / BLACK rows of 18×32 cells (base at 9,26), the glove in column 6, a SWAP key and a palette swatch.
  - Import rewrites the art, `sides.txt` and `hand.png`, then runs `assets.py`.
  - The layout is chess-specific; the PNG-on-palette read/write is reusable.
- **`tools/pieces.py`** (417 lines): numpy SDF ray-marcher.
  - Lathe profiles; camera at a 30° elevation to match the 2:1 tiles.
  - Shading quantised to BODY `[BLUE,NAVY,SILVER,WHITE,CYAN]` plus TRIM `[WOOD,WOOD,GOLD,GOLD,WHITE]`, INK outline.
  - Its lathe approach could render chips or a ball.

### Palette (`src/gfx/Palette.h:14-17`, `Palette.cpp:9-26`)

| Index | Name | RGB444 | Index | Name | RGB444 |
|---|---|---|---|---|---|
| 0 | INK | 0x000 | 8 | GOLD | 0xFC2 |
| 1 | WHITE | 0xFFF | 9 | WOOD | 0x741 |
| 2 | FELT_DK | 0x042 | 10 | BLUE | 0x26E |
| 3 | FELT | 0x173 | 11 | NAVY | 0x125 |
| 4 | FELT_LT | 0x4B5 | 12 | SKIN | 0xFB8 |
| 5 | SILVER | 0xBBC | 13 | CYAN | 0x6EF |
| 6 | RED | 0xE12 | 14 | FX_A | animated |
| 7 | WINE | 0x702 | 15 | FX_B | animated |

- Felt themes rewrite indices 2-4.
- **FX_A is global:** in HOVER mode anything else drawn in FX_A (e.g. the `B_RAINBOW` banner outline) turns grey too.

### Fx (`src/fx/Fx.h`, `Fx.cpp`)

- Easing: `Ease{LINEAR,OUT_CUBIC,OUT_BACK,IN_OUT,OUT_BOUNCE}`, 17-point Q8 tables.
- `isin` (1/256 turn to ±256); 48 particles `Kind{SPARK,CONFETTI,STAR,DUST}`.
- Banners: `BannerStyle{B_RAINBOW,B_GOLD,B_RED,B_CYAN,B_WHITE}`.
- Shake: rows shifted by RAMFUNC.

### Sfx (`src/audio/Audio.h`)

```
Cursor, Select, Deny, Land, Hop, Capture, Coin, Check, Castle, Promote, Whoosh, Flip, Mate, Win, Lose, Draw, Turn, Title, Tick, Tock
```

- Whoosh = 1200 to 3800 Hz, 90 ms.
- Turn = 2637 Hz 40 ms, then 3520 Hz 90 ms.

---

## 8. README feature list (`README.md`)

- Isometric board; glove cursor; legal moves lit with shimmer and marching borders.
- Camera whip-zoom on each move; slow-motion captures with the piece knocked tumbling.
- Move call-outs ("ROOK TAKES QUEEN ON A4"); CHECK!/CHECKMATE! in gradient letters.
- CPU with a red glove that hovers over pieces while a soft clock ticks, then plays like a human.
- Hover plate ("BISHOP F1 NO MOVES"); the other side's last move lit gold.
- Check: the king beats red (lub-dub) and the cursor is restricted to pieces that can escape.
- SELECT toggles iso/map; START opens pause (resume, undo, resign, save + quit).
- Options: sound, felt (green, blue, red, purple), pace FUN/QUICK. Saved to flash.
- Build: `opt=oslto,rtlib=nano,periph=game,usb=uploadonly`. The image is 48.9 KB with LTO (51.3 KB without) in a 50,944 B region.

---

## 9. Costs (map `build/CHGame.ch32v.CHGame/CHChess.ino.map`, post-LTO kept sections, approximate)

**Totals:** `.text` 45,660 B, `.data` 3,688 B (holds RAMFUNCs), `.bss` 14,200 B.

| Module | Flash | RAM |
|---|---|---|
| stage | text 5,792 + rodata 312 B | ≈273 B |
| iso | text 1,602 B | RAMFUNCs isosquares 190 + mapsquares 104 + isopatspan 72 B |
| screens (incl. `nearest`) | text 2,486 B | — |
| fx | text 878 B | bss 494 (particles 48×8) |
| match | text 1,268 B | bss 430 |
| sprite4 | — | 504 B (SRAM-resident) |

**Render times** (`docs/CHGfx-notes.md`):
- Normal view 6.0 ms, map 5.6 ms, zoomed move 7.8 ms.
- An async flush costs ~5 ms of CPU per frame (out of 16.7 ms).
- Running code from flash costs ~2-3 µs per call per pixel (3 wait states), hence the RAMFUNCs (`src/RamFunc.h`: `section(".gnu.linkonce.r.chch." #name)`).

**Library-swap measurements:**
- Replacing the game's text with CHGfx text: +2,000 to 2,260 B flash.
- `gfx_fillEllipse` saves 84 B flash and 80 B SRAM compared with the game's own version.

---

## 10. What to reuse vs what is chess-only

**Copy as-is:**
- `src/CHGame.*` (input, repeat, pacing)
- `src/Frame.cpp` `run()` (the think-burst part is chess-only)
- `src/gfx/Palette.*` (HOVER/TARGETS/CASINO modes)
- `src/gfx/Draw.*`, `Mask.*`, `Fmt.*`
- `src/fx/Fx.*`
- `src/RamFunc.h`
- `src/audio/Audio.*` (redefine the step tables)

**Lift the pattern:**
- `Stage`'s Q4 glide, bob, tap, `RM_*` tables and outline-by-INK-remap
- `plate()`, the HUD bar, the `busy()` + event drain, `signature()` redraw skipping
- `Screens`' `nearest()`, `lookAround()` + hold bar, `menuItem`/`menuNav`/`title35`/`feltBackdrop`/`panel`, the `go()`/`enter()` 8-frame fades
- `tools/assets.py` packer

**Chess-only:**
- `iso` (8×8 hard-coded)
- `engine/`, `Match` rules, the think-burst scheduler
- piece art, `SIDE_REMAP`, `sheet.py`'s cell layout