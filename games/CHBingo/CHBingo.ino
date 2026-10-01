// CHBingo - 75-ball bingo for the CHGame handheld (CH32X035, 128x128 ST7735,
// piezo), in the casino style of CHBlackjack and its siblings: the dealer
// from CHBlackjack's tables calls the balls, and the player races a hall of
// rivals across up to nine cards, only one and a half of them in view.
//
// Frame loop: logic runs while the previous frame is still going out over
// DMA; drawing waits for it (one framebuffer), then the new frame is sent.
#include "config.h"
#include <CHGfx.h>
#include "src/CHGame.h"
#include "src/gfx/Palette.h"
#include "src/states/Screens.h"
#include "src/save/Save.h"
#include "src/debug/Debug.h"

#if CHBN_DEBUG
#ifdef CHSIM
#include <string.h>
#include "src/gfx/Fmt.h"
uint64_t sim_hostNanos();
// Q: calibration for chdrive's cal - host ns for the primitives the CHGfx
// benchmark measured on the board (benchmark-results.txt), so the
// simulator's render times can be read as device milliseconds.
static void calibrate() {
    static uint8_t spr[8 * 16];
    memset(spr, 0x3F, sizeof spr);
    uint64_t t0, r[5];
    t0 = sim_hostNanos(); for (int i = 0; i < 200; i++) gfx_clear((uint8_t)i); r[0] = (sim_hostNanos() - t0) / 200;
    t0 = sim_hostNanos(); for (int i = 0; i < 20000; i++) gfx_hline(0, i & 127, 128, (uint8_t)i); r[1] = (sim_hostNanos() - t0) / 20000;
    t0 = sim_hostNanos(); for (int i = 0; i < 2000; i++) gfx_blit(spr, i & 63, i & 63, 16, 16, 15); r[2] = (sim_hostNanos() - t0) / 2000;
    t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_text(0, i & 63, "ABCDEFGHIJKLMNOPQRSTUVWX", 1); r[3] = (sim_hostNanos() - t0) / 1000;
    t0 = sim_hostNanos(); for (int i = 0; i < 1000; i++) gfx_fillCircle(64, 64, 30, (uint8_t)i); r[4] = (sim_hostNanos() - t0) / 1000;
    char buf[96], *p = fmtStr(buf, "CAL");
    for (int k = 0; k < 5; k++) { *p++ = ' '; p = fmtInt(p, (int32_t)r[k]); }
    fmtStr(p, "\n");
    dbg::print(buf);
}
#endif

// Game commands for the debug protocol (tools/chsim/chdrive.py 'say').
//   R <seed>          reseed the rules' generator
//   J <T|P|O|S|L>     jump to a screen (P: a new game at the buy-in)
//   C <n>             buy n cards and start the round
//   F <n[,n...]>      the next balls called
//   G <card>          put that card in play (0-based)
//   D                 daub every waiting number on every card
//   W                 daub the middle row of the card in play: a bingo
//   I <0|1|2>         the win banner: by chance, always "IT'S A BINGO!", never
//   H <call>          the call a rival completes a line on
//   X <1|2|3>         hand over a power-up (wild, freeze, 2x)
//   M <amount>        set the purse;  A <amount>  set the jackpot
//   Y                 print the game state (the save test)
//   V                 power off and on: reload from the save pages
//   E <1|0>           let a debug build on the board write its save pages
static bool debugHook(char cmd, const char *args) {
    switch (cmd) {
        case 'R': screens::debugSeed(dbg::parseNum(args, 10)); return true;
        case 'J': screens::debugJump(args[0]); return true;
        case 'F': {
            const char *p = args;
            while (*p) {
                if (!screens::debugGame('F', dbg::parseNum(p, 10))) return false;
                while (*p == ' ' || *p == ',') p++;
                if (*p && (*p < '0' || *p > '9')) break;
            }
            return true;
        }
        case 'Y': {
            char buf[96];
            screens::debugState(buf);
            dbg::print(buf);
            return true;
        }
        case 'E': save::allowWrites(args[0] == '1'); return true;
#ifdef CHSIM
        case 'Q': calibrate(); return true;
#endif
        default: return screens::debugGame(cmd, dbg::parseNum(args, 10));
    }
}
#endif

void setup() {
    arduboy.boot();
    dbg::paintStack();
    gfx_begin(GFX_DIV2, GFX_12BPP);
    pal::init();
    screens::begin();
    arduboy.setFrameRate(CHBN_FPS);
#if CHBN_DEBUG
    dbg::hook = debugHook;
#endif
}

void loop() {
    dbg::poll();
    if (!arduboy.nextFrame()) return;
    dbg::markUpdateStart();
    // Logic runs at a fixed 60 Hz. If a heavy frame made drawing fall
    // behind, catch up (up to three ticks) before drawing again, so the
    // caller's clock never slows down.
    uint8_t ticks = 0;
    do {
        arduboy.pollButtons();
        pal::tick();
        screens::update();
    } while (++ticks < 3 && arduboy.nextFrame());
    pal::commit();                  // staged by CHGfx: lands with the next flush
    gfx_wait();
    dbg::markRenderStart();
    screens::render(arduboy.frameCount);
    dbg::markRenderEnd();
    gfx_flushAsync();
}
