// A small sound sequencer for the CHGame piezo (from CHBlackjack, via CHChess).
//
// Effects play on TIM1 channel 2 (PB10), stepped by the core's 1 kHz
// SysTick hook (osSystickHandler), so no other timer is used. One pin plays
// one note at a time; a higher-priority effect is never cut off by a lower
// one. tools/audio/preview.py renders every effect to WAV from this file.
#pragma GCC optimize("Os")
#include <Arduino.h>
#include "Audio.h"

#ifdef CHSIM
// The simulator is silent: same interface, no hardware. It remembers the
// last effect so scripts and tests can check what would have sounded.
namespace audio {
static bool simOn = true;
uint8_t simLast = 0xFF;
bool begin(bool on) { simOn = on; return true; }
void setOn(bool on) { simOn = on; }
void sfx(Sfx s) { if (simOn) simLast = (uint8_t)s; }
void blip(uint16_t, uint16_t) {}
bool playing() { return false; }
void update() {}
void led(Led) {}
}
#else

struct Step { uint16_t hz, endHz, ms; };        // effect step; hz 0 = rest

#define S(hz, end, ms) { (uint16_t)(hz), (uint16_t)(end), (uint16_t)(ms) }
#define REST(ms)       { 0, 0, (uint16_t)(ms) }

static const Step CURSOR[]  = { S(2100, 0, 10) };
static const Step SELECT[]  = { S(1700, 0, 18), S(2600, 0, 30) };
static const Step DENY[]    = { S(900, 650, 70) };
static const Step DEAL[]    = { S(3600, 1500, 22) };               // a card put down
static const Step FLIP[]    = { S(2300, 0, 8), REST(5), S(3300, 0, 12) };
static const Step PICK[]    = { S(2500, 3400, 16) };               // a card lifted
static const Step BACK[]    = { S(2600, 900, 70) };                // and put back where it was
// A suit complete, king on top.
static const Step SUIT[]    = { S(2093, 0, 60), S(2637, 0, 60), S(3136, 0, 60), S(4186, 0, 170) };
// CHBlackjack's BLACKJACK fanfare: the game is won.
static const Step BIGWIN[]  = {
    S(1568, 0, 50), S(2093, 0, 50), S(2637, 0, 50), S(3136, 0, 90),
    S(2093, 0, 40), S(2637, 0, 40), S(2093, 0, 40), S(2637, 0, 40),
    S(3136, 0, 40), S(4186, 0, 40), S(3136, 0, 40), S(4186, 0, 40),
    S(2000, 4200, 220) };
static const Step SHUFFLE[] = {
    S(3000, 0, 7), REST(12), S(3400, 0, 7), REST(12), S(3100, 0, 7), REST(12), S(3500, 0, 7), REST(12),
    S(3000, 0, 7), REST(12), S(3400, 0, 7), REST(12), S(3200, 0, 7), REST(12), S(3600, 0, 7), REST(60),
    S(2400, 3800, 120) };
static const Step COIN[]    = { S(2800, 0, 10), S(3700, 0, 28) };
static const Step BUST[]    = { S(1600, 950, 110), S(950, 560, 130), S(560, 330, 230) };
static const Step TITLE[]   = {
    S(1568, 0, 90), S(2093, 0, 90), S(2637, 0, 90), S(3136, 0, 180), REST(40),
    S(2637, 0, 90), S(3136, 0, 360) };
static const Step WHOOSH[]  = { S(1200, 3800, 90) };

struct SfxDef { const Step *steps; uint8_t n, prio; };
#define DEF(a, p) { a, (uint8_t)(sizeof(a) / sizeof(a[0])), p }
static const SfxDef DEFS[(int)Sfx::COUNT] = {
    DEF(CURSOR, 0), DEF(SELECT, 1), DEF(DENY, 1), DEF(DEAL, 1), DEF(FLIP, 1), DEF(PICK, 1),
    DEF(BACK, 1), DEF(SUIT, 3), DEF(BIGWIN, 4), DEF(SHUFFLE, 2), DEF(COIN, 1), DEF(BUST, 3),
    DEF(TITLE, 2), DEF(WHOOSH, 1),
};

// --- Sequencer state (shared with the 1 kHz interrupt) ----------------------
static volatile const Step *fxSteps = nullptr;
static volatile uint8_t fxN = 0, fxI = 0, fxPrio = 0;
static volatile uint16_t fxT = 0;

static bool started = false, running = false;
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

// smooth: change pitch at the end of the current wave cycle (the timer's
// preloaded reload) instead of restarting it - sweeps glide without a click
// a millisecond (CHBlackjack played its music this way).
static void tone(uint16_t hz, bool smooth = false) {
    if (hz == lastHz) return;
    lastHz = hz;
    if (!hz) {
        TIM1->CH2CVR = 0; TIM1->SWEVGR = 1; TIM1->CTLR1 = 0; TIM1->INTFR = 0;
        running = false;
        return;
    }
    uint32_t period = (1000000u + hz / 2u) / hz;
    if (period < 2) period = 2;
    uint32_t duty = period / 2;
    if (smooth && running) {
        TIM1->ATRLR = period - 1;
        TIM1->CH2CVR = duty;
        return;
    }
    running = true;
    TIM1->CTLR1 = 0;
    TIM1->ATRLR = period - 1;
    TIM1->CH2CVR = duty;
    TIM1->SWEVGR = 1;
    TIM1->INTFR = 0;
    TIM1->CTLR1 = 0x81;
}

extern "C" void osSystickHandler(void) {
    if (!started) return;
    const Step *s = (const Step *)fxSteps;
    if (!s) { tone(0); return; }
    const Step &st = s[fxI];
    uint16_t hz = st.hz;
    bool sweep = hz && st.endHz && fxT;
    if (hz && st.endHz) hz = (uint16_t)(st.hz + ((int32_t)st.endHz - st.hz) * fxT / st.ms);
    if (++fxT >= st.ms) {
        fxT = 0;
        if (++fxI >= fxN) { fxSteps = nullptr; fxPrio = 0; }
    }
    tone(hz, sweep);
}

namespace audio {

bool begin(bool on) {
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOB;
    uint32_t v = (CFGHR_tmpB & ~(15u << 4)) | (3u << 4);          // PB9 LED: push-pull output
    CFGHR_tmpB = v;
    GPIOB->CFGHR = v;
    GPIOB->BCR = 1u << 9;
    if (!on) { started = false; lastHz = 1; tone(0); return true; }
    if (!started) hwInit();
    started = true;
    return true;
}

void setOn(bool on) { begin(on); }

void sfx(Sfx s) {
    const SfxDef &d = DEFS[(int)s];
    if (!started) return;
    if (fxSteps && d.prio < fxPrio) return;
    __disable_irq();
    fxSteps = d.steps; fxN = d.n; fxI = 0; fxT = 0; fxPrio = d.prio;
    __enable_irq();
}

bool playing() { return fxSteps != nullptr; }

static Step blipStep;
void blip(uint16_t hz, uint16_t ms) {
    if (!started || fxSteps) return;                  // never over an effect
    __disable_irq();
    blipStep.hz = hz; blipStep.endHz = 0; blipStep.ms = ms;
    fxSteps = &blipStep; fxN = 1; fxI = 0; fxT = 0; fxPrio = 0;
    __enable_irq();
}

void led(Led p) { ledPattern = p; ledT = 0; }

void update() {
    if (!ledPattern) return;
    ledT++;
    bool on = false;
    switch (ledPattern) {
        case LED_BLINK:  on = ledT < 12; if (ledT > 12) ledPattern = 0; break;
        case LED_TRIPLE: on = (ledT % 16) < 8; if (ledT > 48) ledPattern = 0; break;
        case LED_PARTY:  on = (ledT % 8) < 4; if (ledT > 240) ledPattern = 0; break;
    }
    if (on && ledPattern) GPIOB->BSHR = 1u << 9;
    else GPIOB->BCR = 1u << 9;
}

}  // namespace audio
#endif  // CHSIM
