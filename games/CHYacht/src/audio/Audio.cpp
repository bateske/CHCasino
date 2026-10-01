// A small sound sequencer for the CHGame piezo (CHBlackjack's, without the
// music player).
//
// The CHGameSound library does far more than a table game needs, and costs
// ~6.5 KB of flash on a 50 KB part. This keeps its 8-byte effect steps and
// plays them with TIM1 channel 2 on PB10 (the library's pin setup), driven
// by the core's 1 kHz SysTick hook (osSystickHandler), so no other timer is
// used. tools/audio/preview.py renders the effects to WAV from this file.
#pragma GCC optimize("Os")
#include <Arduino.h>
#include "Audio.h"

#ifdef CHSIM
// The simulator is silent: same interface, no hardware.
static bool simOn = true;
namespace audio {
void begin(bool o) { simOn = o; }
void setOn(bool o) { simOn = o; }
bool on() { return simOn; }
void sfx(Sfx) {}
void blip(uint16_t, uint16_t) {}
void update() {}
void led(Led) {}
}
#else

struct Step { uint16_t hz, endHz, ms; };        // effect step; hz 0 = rest

#define S(hz, end, ms) { (uint16_t)(hz), (uint16_t)(end), (uint16_t)(ms) }
#define REST(ms)       { 0, 0, (uint16_t)(ms) }

static const Step CURSOR[]    = { S(2100, 0, 10) };
static const Step SELECT[]    = { S(1700, 0, 18), S(2600, 0, 30) };
static const Step DENY[]      = { S(900, 650, 70) };
static const Step CHIP[]      = { S(3100, 0, 12), REST(9), S(3700, 0, 26) };
static const Step CHIPTAKE[]  = { S(3700, 0, 10), REST(8), S(2900, 0, 20) };
static const Step THROW[]     = { S(1100, 3300, 90) };
static const Step BOUNCE[]    = { S(2700, 0, 6), REST(5), S(2000, 0, 5) };
static const Step WALL[]      = { S(1900, 0, 7), REST(4), S(2600, 0, 7), REST(5), S(1500, 0, 7),
                                  REST(6), S(2300, 0, 10) };
static const Step CLACK[]     = { S(3300, 0, 6), REST(3), S(2800, 0, 7) };
static const Step POINT[]     = { S(1400, 0, 30), REST(30), S(2093, 0, 50), S(2637, 0, 110) };
static const Step WIN[]       = { S(2093, 0, 60), S(2637, 0, 60), S(3136, 0, 60), S(4186, 0, 170) };
static const Step BIGWIN[]    = {
    S(1568, 0, 50), S(2093, 0, 50), S(2637, 0, 50), S(3136, 0, 90),
    S(2093, 0, 40), S(2637, 0, 40), S(2093, 0, 40), S(2637, 0, 40),
    S(3136, 0, 40), S(4186, 0, 40), S(3136, 0, 40), S(4186, 0, 40),
    S(2000, 4200, 220) };
static const Step SEVENOUT[]  = { S(1600, 950, 110), S(950, 560, 130), S(560, 330, 260) };
static const Step CRAPS[]     = { S(800, 620, 90), REST(30), S(700, 450, 150) };
static const Step COIN[]      = { S(2800, 0, 10), S(3700, 0, 28) };
static const Step SWEEP[]     = { S(2600, 900, 110) };
static const Step HOT[]       = { S(1568, 0, 40), S(2093, 0, 40), S(2637, 0, 40), S(3136, 0, 40),
                                  S(4186, 0, 60), REST(30), S(3136, 4186, 160) };
static const Step WHOOSH[]    = { S(1200, 3800, 90) };
static const Step LOSE[]      = { S(1300, 950, 140), S(950, 700, 220) };
static const Step BROKE[]     = { S(1568, 1480, 300), S(1480, 1397, 300), S(1397, 1319, 300), S(1319, 1180, 800) };

struct SfxDef { const Step *steps; uint8_t n, prio; };
#define DEF(a, p) { a, (uint8_t)(sizeof(a) / sizeof(a[0])), p }
static const SfxDef DEFS[(int)Sfx::COUNT] = {
    DEF(CURSOR, 0), DEF(SELECT, 1), DEF(DENY, 1), DEF(CHIP, 1), DEF(CHIPTAKE, 1), DEF(THROW, 2),
    DEF(BOUNCE, 1), DEF(WALL, 2), DEF(CLACK, 1), DEF(POINT, 3), DEF(WIN, 3), DEF(BIGWIN, 4),
    DEF(SEVENOUT, 4), DEF(CRAPS, 3), DEF(COIN, 1), DEF(SWEEP, 1), DEF(HOT, 4), DEF(WHOOSH, 1),
    DEF(LOSE, 3), DEF(BROKE, 4),
};

// --- Sequencer state (shared with the 1 kHz interrupt) ----------------------
static volatile const Step *fxSteps = nullptr;
static volatile uint8_t fxN = 0, fxI = 0, fxPrio = 0;
static volatile uint16_t fxT = 0;
static Step blipStep;

static bool started = false, enabled = true, running = false;
static uint16_t lastHz = 0;
static uint8_t ledPattern = 0;
static uint16_t ledT = 0;

extern "C" volatile uint32_t CFGHR_tmpB;        // GPIOB CFGHR is write-only: go through the shadow

static void pb10(uint32_t nibble) {
    uint32_t v = (CFGHR_tmpB & ~(15u << 8)) | (nibble << 8);
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
}

static void hwInit() {
    RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB | RCC_APB2Periph_TIM1;
    AFIO->PCFR1 = (AFIO->PCFR1 & ~(7u << 15)) | (1u << 15);   // TIM1 partial remap: CH2 on PB10
    GPIOB->BCR = 1u << 10;
    pb10(11u);                                                // alternate-function push-pull
    TIM1->CTLR1 = 0; TIM1->CTLR2 = 0; TIM1->SMCFGR = 0; TIM1->DMAINTENR = 0;
    TIM1->CCER = 0;
    TIM1->CHCTLR1 = 0x6800;                                   // CH2 PWM1 + preload
    TIM1->CHCTLR2 = 0;
    TIM1->PSC = 47;                                           // 1 MHz
    TIM1->RPTCR = 0;
    TIM1->ATRLR = 999;
    TIM1->CH2CVR = 0;
    TIM1->CNT = 0;
    TIM1->BDTR = 0x8000;                                      // MOE
    TIM1->CCER = 0x10;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    running = false; lastHz = 0;
}

static void tone(uint16_t hz) {
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz || !enabled) {
        TIM1->CH2CVR = 0; TIM1->SWEVGR = 1; TIM1->CTLR1 = 0; TIM1->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    running = true;
    TIM1->CTLR1 = 0;
    TIM1->ATRLR = period - 1;
    TIM1->CH2CVR = period / 2;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    TIM1->CTLR1 = 0x81;
}

extern "C" void osSystickHandler(void) {
    if (!started) return;
    const Step *s = (const Step *)fxSteps;
    uint16_t hz = 0;
    if (s) {
        const Step &st = s[fxI];
        hz = st.hz;
        if (hz && st.endHz) hz = (uint16_t)(st.hz + ((int32_t)st.endHz - st.hz) * fxT / st.ms);
        if (++fxT >= st.ms) {
            fxT = 0;
            if (++fxI >= fxN) { fxSteps = nullptr; fxPrio = 0; }
        }
    }
    tone(hz);
}

namespace audio {

void begin(bool o) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    uint32_t v = (CFGHR_tmpB & ~(15u << 4)) | (3u << 4);          // PB9 LED: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
    GPIOB->BCR = 1u << 9;
    if (!started) hwInit();
    started = true;
    setOn(o);
}

void setOn(bool o) { enabled = o; lastHz = 1; }
bool on() { return enabled; }

static void play(const Step *st, uint8_t n, uint8_t prio) {
    if (!started || !enabled) return;
    if (fxSteps && prio < fxPrio) return;
    __disable_irq();
    fxSteps = st; fxN = n; fxI = 0; fxT = 0; fxPrio = prio;
    __enable_irq();
}

void sfx(Sfx s) {
    const SfxDef &d = DEFS[(int)s];
    play(d.steps, d.n, d.prio);
}

void blip(uint16_t hz, uint16_t ms) {
    if (fxSteps && fxPrio > 1) return;
    __disable_irq();
    blipStep.hz = hz; blipStep.endHz = 0; blipStep.ms = ms;
    __enable_irq();
    play(&blipStep, 1, 0);
}

void led(Led p) { ledPattern = p; ledT = 0; }

void update() {
    if (!ledPattern) return;
    ledT++;
    bool lit = false;
    switch (ledPattern) {
        case LED_BLINK:  lit = ledT < 12; if (ledT > 12) ledPattern = 0; break;
        case LED_TRIPLE: lit = (ledT % 16) < 8; if (ledT > 48) ledPattern = 0; break;
        case LED_PARTY:  lit = (ledT % 8) < 4; if (ledT > 240) ledPattern = 0; break;
    }
    if (lit && ledPattern) GPIOB->BSHR = 1u << 9;
    else GPIOB->BCR = 1u << 9;
}

}  // namespace audio
#endif  // CHSIM
