// A small sound sequencer for the CHGame piezo (CHBlackjack's, without the
// music player).
//
// The CHGameSound library does far more than this game needs, and costs
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
void music(Song) {}
void setMusicOn(bool) {}
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
static const Step LEVER[]     = { S(900, 1500, 40), REST(10), S(2600, 0, 8), REST(6), S(1900, 0, 10) };
static const Step THUNK[]     = { S(1500, 1000, 16), S(2400, 0, 5) };
static const Step ANTIC[]     = { S(1400, 2800, 160) };
static const Step LOCK[]      = { S(3300, 0, 8), REST(4), S(2200, 0, 8), REST(4), S(3900, 0, 30) };
static const Step COIN[]      = { S(2800, 0, 10), S(3700, 0, 28) };
static const Step WIN[]       = { S(2093, 0, 60), S(2637, 0, 60), S(3136, 0, 60), S(4186, 0, 170) };
static const Step BIGWIN[]    = {
    S(1568, 0, 50), S(2093, 0, 50), S(2637, 0, 50), S(3136, 0, 90),
    S(2093, 0, 40), S(2637, 0, 40), S(2093, 0, 40), S(2637, 0, 40),
    S(3136, 0, 40), S(4186, 0, 40), S(3136, 0, 40), S(4186, 0, 40),
    S(2000, 4200, 220) };
static const Step JACKPOT[]   = {
    S(2093, 0, 70), S(2637, 0, 70), S(3136, 0, 70), S(4186, 0, 140), REST(40),
    S(3136, 0, 70), S(4186, 0, 260), REST(40),
    S(2093, 4186, 90), S(2093, 4186, 90), S(2093, 4186, 90), S(2093, 4186, 90),
    S(4186, 0, 60), S(3520, 0, 60), S(4186, 0, 60), S(3520, 0, 60), S(4186, 0, 400) };
static const Step GONG[]      = { S(1175, 0, 30), S(1568, 1175, 120), S(1175, 1100, 380) };
static const Step ROAR[]      = { S(700, 1600, 70), S(1600, 800, 90), S(800, 2400, 60), S(2400, 900, 140) };
static const Step BROKE[]     = { S(1568, 1480, 300), S(1480, 1397, 300), S(1397, 1319, 300), S(1319, 1180, 800) };

struct SfxDef { const Step *steps; uint8_t n, prio; };
#define DEF(a, p) { a, (uint8_t)(sizeof(a) / sizeof(a[0])), p }
static const SfxDef DEFS[(int)Sfx::COUNT] = {
    DEF(CURSOR, 0), DEF(SELECT, 1), DEF(DENY, 1), DEF(CHIP, 1), DEF(LEVER, 2), DEF(THUNK, 1),
    DEF(ANTIC, 2), DEF(LOCK, 2), DEF(COIN, 1), DEF(WIN, 3), DEF(BIGWIN, 4), DEF(JACKPOT, 5),
    DEF(GONG, 3), DEF(ROAR, 3), DEF(BROKE, 4),
};

// --- Music: note, length pairs. A note is octave << 4 | semitone (octave 0 =
// C5), 0xFF a rest; a length is in units of the song's tempo. All three
// tunes are new for this game.
#define R_ 0xFF
enum : uint8_t { nC6 = 0x10, nD6 = 0x12, nE6 = 0x14, nF6 = 0x15, nG6 = 0x17, nA6 = 0x19, nB6 = 0x1B,
                 nC7 = 0x20, nD7 = 0x22, nE7 = 0x24, nG5 = 0x07, nA5 = 0x09 };
// Title: a bouncing rag in C.
static const uint8_t TITLE[] = {
    nE6,1, nG6,1, nC7,2, nG6,1, nE6,1, nG6,2,   nF6,1, nA6,1, nC7,2, nA6,1, nF6,1, nA6,2,
    nG6,1, nB6,1, nD7,2, nB6,1, nG6,1, nD7,2,   nC7,2, nG6,2, nE6,2, R_,2,
    nE6,1, nG6,1, nC7,2, nE7,1, nC7,1, nG6,2,   nF6,1, nA6,1, nC7,2, nD7,1, nC7,1, nA6,2,
    nG6,1, nG6,1, nB6,2, nD7,1, nB6,1, nG6,2,   nC7,3, R_,1, nC7,2, R_,2,
};
// Free games: a pentatonic run that keeps climbing.
static const uint8_t FREE[] = {
    nA6,1, nG6,1, nE6,2, nD6,1, nE6,1, nG6,2,   nA6,1, nC7,1, nA6,2, nG6,1, nE6,1, nD6,2,
    nE6,1, nG6,1, nA6,2, nG6,1, nE6,1, nD6,1, nC6,1,   nD6,2, nE6,2, nC6,2, R_,2,
};
// The bonus wheel: a fairground arpeggio.
static const uint8_t WHEEL[] = {
    nC6,1, nE6,1, nG6,1, nC7,1, nG6,1, nE6,1,   nD6,1, nF6,1, nA6,1, nD7,1, nA6,1, nF6,1,
    nE6,1, nG6,1, nB6,1, nE7,1, nB6,1, nG6,1,   nD6,1, nG6,1, nB6,1, nD7,1, nB6,1, nG6,1,
};
struct SongDef { const uint8_t *notes; uint8_t n, unitMs; };
#define SONG(a, ms) { a, (uint8_t)(sizeof(a) / 2), ms }
static const SongDef SONGS[(int)Song::COUNT] = { {nullptr, 0, 0}, SONG(TITLE, 105), SONG(FREE, 95), SONG(WHEEL, 80) };
// The top octave used; lower ones halve it.
static const uint16_t SEMI[12] = {2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951};

static volatile const uint8_t *musNotes = nullptr;
static volatile uint8_t musN = 0, musI = 0, musUnit = 0;
static volatile uint16_t musT = 0;
static bool musicOn = true;
static uint8_t musWant = 0;

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
    const uint8_t *m = (const uint8_t *)musNotes;
    if (m) {
        uint8_t note = m[musI * 2];
        uint16_t len = (uint16_t)(m[musI * 2 + 1] * musUnit);
        // The tune sounds only while no effect has the piezo; each note
        // stops a little early so repeats stay apart.
        if (!s && note != R_ && musT + 14 < len) hz = (uint16_t)(SEMI[note & 15] >> (2 - (note >> 4)));
        if (++musT >= len) { musT = 0; if (++musI >= musN) musI = 0; }
    }
    tone(hz);
}

namespace audio {

static void startMusic();

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

void setOn(bool o) { enabled = o; lastHz = 1; if (started) startMusic(); }
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

static void startMusic() {
    const SongDef &d = SONGS[musicOn && enabled ? musWant : 0];
    __disable_irq();
    musNotes = d.notes; musN = d.n; musUnit = d.unitMs; musI = 0; musT = 0;
    __enable_irq();
}

void music(Song s) {
    if ((uint8_t)s == musWant) return;
    musWant = (uint8_t)s;
    startMusic();
}

void setMusicOn(bool on) { if (on != musicOn) { musicOn = on; startMusic(); } }

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
