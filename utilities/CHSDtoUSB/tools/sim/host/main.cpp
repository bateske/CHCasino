// CHSDtoUSB on a PC: a pretend PC sends the reader block commands from a
// trace (tools/sim/pcsession.py writes them), in virtual time, and the
// frames the sketch draws are saved for screenshots and GIFs.
//
//     sim.exe TRACE_DIR OUT_DIR
//
// Timing: a block read takes ~1.0 ms (full-speed USB caps the board at
// ~500 KB/s), a block written ~1.25 ms plus ~1.2 ms for each command to
// program the card, with a little jitter. A command waits for a panel
// flush in flight, as the real card driver does, so the sketch's drawing
// shows up in the throughput as it would on the board.
#include <Arduino.h>
#include <CHGfx.h>
#include <vector>
#include <string>
#include "sim.h"
#include "simpc.h"
#include "src/mon/Monitor.h"
#include "src/ui/Ui.h"

void setup();
void loop();

SimSerial Serial;
uint32_t sim_ledState = 0;

static uint32_t s_now = 0;
static std::string s_out;

// ---- Frames -----------------------------------------------------------------
static uint8_t s_frame[GFX_FB_BYTES + 32];
static bool s_haveFrame = false;
static uint32_t s_recEvery = 0, s_recNext = 0, s_recCount = 0, s_frames = 0;

void sim_present() {
    memcpy(s_frame, gfx_fb, GFX_FB_BYTES);
    for (int i = 0; i < 16; i++) {
        uint16_t c = gfx_paletteOut((uint8_t)i);
        s_frame[GFX_FB_BYTES + 2 * i] = (uint8_t)c;
        s_frame[GFX_FB_BYTES + 2 * i + 1] = (uint8_t)(c >> 8);
    }
    s_haveFrame = true;
    s_frames++;
}

static void dump(const std::string &name) {
    if (!s_haveFrame) return;
    std::string p = s_out + "/" + name + ".fb";
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) { perror(p.c_str()); exit(2); }
    fwrite(s_frame, 1, sizeof s_frame, f);
    fclose(f);
}

static void sample() {
    while (s_recEvery && (int32_t)(s_now - s_recNext) >= 0) {
        char n[32];
        snprintf(n, sizeof n, "rec_%05u", (unsigned)s_recCount++);
        dump(n);
        s_recNext += s_recEvery * 1000;
    }
}

uint32_t sim_now() { return s_now; }
void sim_advance(uint32_t us) { s_now += us; sample(); }
void sim_bug(const char *msg) { fprintf(stderr, "BUG: %s (t=%u us)\n", msg, (unsigned)s_now); exit(3); }
void sim_waitInput() {}
uint64_t sim_hostNanos() { return 0; }
uint32_t sim_cardBlocks() { return 0; }
void sim_cardEject(bool) {}

uint32_t micros() { return s_now; }
uint32_t millis() { return s_now / 1000; }
void delay(uint32_t ms) { sim_advance(ms * 1000); }
void delayMicroseconds(uint32_t us) { sim_advance(us); }
void pinMode(uint32_t, uint32_t) {}
void digitalWrite(uint32_t pin, uint32_t v) { if (pin == LED_BUILTIN) sim_ledState = v; }

static uint32_t s_btnPin = 0, s_btnUntil = 0;
int digitalRead(uint32_t pin) { return pin == s_btnPin && (int32_t)(s_btnUntil - s_now) > 0 ? LOW : HIGH; }

static uint32_t s_rng = 1;
static uint32_t rng() { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }
void randomSeed(unsigned long s) { s_rng = s ? (uint32_t)s : 1; }
long random(long hi) { return hi > 0 ? (long)(rng() % (uint32_t)hi) : 0; }
long random(long lo, long hi) { return hi > lo ? lo + random(hi - lo) : lo; }

int SimSerial::available() { return 0; }
int SimSerial::read() { return -1; }
size_t SimSerial::write(uint8_t) { return 1; }
size_t SimSerial::write(const uint8_t *, size_t n) { return n; }
size_t SimSerial::print(const char *s) { return strlen(s); }
size_t SimSerial::println(const char *s) { return strlen(s) + 2; }
int SimSerial::printf(const char *, ...) { return 0; }

void NVIC_SystemReset() { fprintf(stderr, "reset (back to the menu)\n"); exit(0); }
extern "C" void chgame_enter_bootloader(void) { fprintf(stderr, "bootloader\n"); exit(0); }

// ---- The pretend PC ---------------------------------------------------------------
// The sketch's loop, between commands. A frame it drew takes the time the
// board would take (counted on an emulated RV32 core, see the README):
// ~5 ms for the top part, ~7.5 ms with the panel.
static uint32_t s_drawUs = 0;
static void idle(uint32_t us) {
    uint32_t end = s_now + us;
    while ((int32_t)(end - s_now) > 0) {
        uint32_t frames = ui::perf.frames, rows = ui::perf.rows;
        loop();
        if (ui::perf.frames != frames) {
            uint32_t d = ui::perf.rows - rows > 100 ? 7500 : 5000;
            s_drawUs += d;
            sim_advance(d);
        }
        sim_advance(250);
    }
}

static uint32_t jitter(uint32_t us) { return us - us / 20 + rng() % (us / 10 + 1); }

static const char *const EVNAME[] = {"-", "NEW", "NEWDIR", "DEL", "DELDIR", "REN", "MOVE", "MOD",
                                       "CARDIN", "CARDOUT", "PC", "EJECT", "RO", "RW", "FORMAT", "PART",
                                       "RETRY", "FAIL", "MILE"};
static uint32_t s_events = 0;
static void reportEvents() {
    for (; s_events < mon::events; s_events++) {
        const mon::Event &e = mon::ev[s_events % mon::EVENTS];
        fprintf(stderr, "%8.3f  %-7s %-24s %u\n", s_now / 1e6, EVNAME[e.type], e.name, (unsigned)e.value);
    }
}

static void command(bool write, uint32_t lba, uint32_t n, const uint8_t *data) {
    using namespace usbmsc;
    alignas(4) static uint8_t buf[512];
    simBusy = true;
    simState = write ? WRITING : READING;
    sim_advance(150);                                   // CBW in, decoded
    bool ok;
    if (write) {
        ok = simDev.writeStart(lba, n);
        sim_advance(jitter(900));                       // the card starts programming
        for (uint32_t i = 0; ok && i < n; i++) {
            if (data) memcpy(buf, data + 512 * i, 512);
            else {                                      // file data: anything but a directory
                uint32_t x = (lba + i) * 2654435761u + 1;
                for (int k = 0; k < 512; k++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; buf[k] = (uint8_t)x; }
            }
            sim_advance(jitter(1240));
            if (!simDev.writeBlock(buf)) ok = false;
            else blocksWritten++;
        }
        simDev.writeStop();
    } else {
        ok = simDev.readStart(lba);
        sim_advance(jitter(350));                       // access latency
        for (uint32_t i = 0; ok && i < n; i++) {
            if (!simDev.readBlock(buf)) ok = false;
            else blocksRead++;
            sim_advance(jitter(1000));
        }
        simDev.readStop();
    }
    sim_advance(100);                                   // CSW out
    simBusy = false;
    simState = CONFIGURED;
    reportEvents();
    if (!ok) fprintf(stderr, "%8.3f  command failed: %c %u %u\n", s_now / 1e6, write ? 'W' : 'R', (unsigned)lba, (unsigned)n);
    idle(200);                                          // the PC's turnaround
}

static uint32_t pinOf(const char *b) {
    static const struct { const char *n; uint32_t p; } P[] = {
        {"UP", PIN_BTN_UP}, {"DOWN", PIN_BTN_DOWN}, {"LEFT", PIN_BTN_LEFT}, {"RIGHT", PIN_BTN_RIGHT},
        {"A", PIN_BTN_A}, {"B", PIN_BTN_B}, {"SELECT", PIN_BTN_SELECT}, {"START", PIN_BTN_START}};
    for (auto &x : P)
        if (!strcmp(b, x.n)) return x.p;
    fprintf(stderr, "unknown button %s\n", b);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: sim TRACE_DIR OUT_DIR\n"); return 2; }
    std::string dir = argv[1];
    s_out = argv[2];
    std::vector<uint8_t> blob;
    if (FILE *f = fopen((dir + "/blob.bin").c_str(), "rb")) {
        fseek(f, 0, SEEK_END);
        blob.resize(ftell(f));
        fseek(f, 0, SEEK_SET);
        if (fread(blob.data(), 1, blob.size(), f) != blob.size()) return 2;
        fclose(f);
    }
    FILE *t = fopen((dir + "/trace.txt").c_str(), "r");
    if (!t) { perror("trace.txt"); return 2; }
    char line[256];
    bool started = false;
    while (fgets(line, sizeof line, t)) {
        char op[16] = "", arg[200] = "";
        unsigned a = 0, b = 0;
        sscanf(line, "%15s", op);
        if (!started && strcmp(op, "card")) { setup(); started = true; }
        if (!strcmp(op, "card")) {
            sscanf(line, "%*s %u", &a);
            card_load((dir + "/card.bin").c_str(), a);
            setup();
            started = true;
            sim_advance(1000);
        } else if (!strcmp(op, "usb")) {
            usbmsc::simState = usbmsc::CONFIGURED;
            idle(1000);
        } else if (op[0] == 'R' || op[0] == 'W') {
            char at[32] = "";
            sscanf(line, "%*s %u %u %31s", &a, &b, at);
            const uint8_t *data = nullptr;
            if (op[0] == 'W' && at[0] == '@') data = blob.data() + strtoul(at + 1, nullptr, 10);
            command(op[0] == 'W', a, b, data);
        } else if (!strcmp(op, "wait")) {
            sscanf(line, "%*s %u", &a);
            idle(a * 1000);
        } else if (!strcmp(op, "eject")) {
            usbmsc::simEjected = true;
            idle(1000);
        } else if (!strcmp(op, "pull") || !strcmp(op, "insert")) {
            card_present(op[0] == 'i');
            idle(1000);
        } else if (!strcmp(op, "fault")) {             // fault LBA: its next read needs a retry
            sscanf(line, "%*s %u", &a);
            card_badBlock(a);
        } else if (!strcmp(op, "press")) {
            sscanf(line, "%*s %199s %u", arg, &a);
            s_btnPin = pinOf(arg);
            s_btnUntil = s_now + a * 1000;
            idle(a * 1000 + 100000);
        } else if (!strcmp(op, "snap")) {
            sscanf(line, "%*s %199s", arg);
            idle(1000);
            dump(arg);
        } else if (!strcmp(op, "rec")) {
            sscanf(line, "%*s %u", &a);
            s_recEvery = a;
            s_recNext = s_now;
        } else if (!strcmp(op, "stop")) {
            s_recEvery = 0;
        } else if (!strcmp(op, "note")) {
            fprintf(stderr, "%8.3f  -- %s", s_now / 1e6, line + 5);
        }
        reportEvents();
    }
    fprintf(stderr, "%8.3f  the log at the end, newest first:\n", s_now / 1e6);
    for (int i = 0; const mon::Event *e = mon::newest(i); i++)
        fprintf(stderr, "          %-7s %-24s %u/%u\n", EVNAME[e->type], e->name, (unsigned)e->done, (unsigned)e->value);
    fprintf(stderr, "          free %llu of %llu bytes, created %u deleted %u renamed %u\n",
            (unsigned long long)mon::freeBytes(), (unsigned long long)mon::volumeBytes(), mon::st.created,
            mon::st.deleted, mon::st.renamed);
    fprintf(stderr, "%8.3f  done: %u frames drawn (%u ms drawing), %u recorded, %u KB read, %u KB written\n",
            s_now / 1e6, (unsigned)s_frames, (unsigned)(s_drawUs / 1000), (unsigned)s_recCount,
            (unsigned)(usbmsc::blocksRead / 2), (unsigned)(usbmsc::blocksWritten / 2));
    return 0;
}
uint8_t _ebss;
