// The core process's test entry (host/coreproc.cpp with RE4_CORE_TEST_ENTRY,
// Linux; the check is tests/core_remote_test.cpp). It runs instead of the
// game, as its boot thread, and gives the app's side something to receive:
//
//   remote  one game frame after the other (PADRead, then a GX frame of the
//           port's GX probe) and the sound at the AI's pace (32 kHz: one
//           block of 5 ms each 5 ms, left 1000, right -1000; 120 ms ahead at
//           the start; after a stand it goes on from now); it watches the controller (the A-and-stick
//           state the check sends first, B taps, X switches the rumble motor
//           on), the depth of field switch and the debug menu request, and
//           ends with START (return 7), one log line with what it saw
//   crash   a crash on purpose (the app must notice the core process's end)
//
// The memory card: RE4_REMOTE_TEST_CARD (a new, formatted card when missing).
#include "re4_runtime.h"

#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

namespace {

double seconds()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

struct PadStatus {  // PADStatus
    unsigned short button;
    signed char stickX, stickY, substickX, substickY;
    unsigned char triggerLeft, triggerRight, analogA, analogB;
    signed char err;
};

}  // namespace

extern "C" {
unsigned PADRead(PadStatus* status);
void PADControlMotor(long chan, unsigned cmd);
int re4_dof_enabled(void);
int re4_letterbox_off(void);
int re4_debug_menu_request(void);
unsigned int re4_gx_efb_peek(unsigned short x, unsigned short y, int z);
void re4probe_gx_produce(void* fifo, void* xfb, int frames, int init);
void AIInit(unsigned char* stack);
void AIInitDMA(unsigned start, unsigned length);
void AIStartDMA(void);
void re4_ai_block(void);
void re4_memcard_open(const char* path);
}

extern "C" __attribute__((visibility("default"))) int re4_coreproc_test_entry(void)
{
    const char* what = getenv("RE4_CORE_TEST_ENTRY");
    if (what && strcmp(what, "crash") == 0) {
        re4_log("remote-probe: a crash on purpose");
        volatile int* nowhere = 0;
        *nowhere = 1;
        return 1;
    }
    if (const char* card = getenv("RE4_REMOTE_TEST_CARD")) {
        re4_memcard_open(card);
    }
    // the sound: the DSP leaves right before left in each frame (runtime/ai.cpp)
    short* block = (short*) (unsigned long) 0x82A00000u;
    for (int i = 0; i < 160; i++) {
        block[i * 2] = -1000;
        block[i * 2 + 1] = 1000;
    }
    AIInit(0);
    AIInitDMA(0x82A00000u, 160 * 4);
    AIStartDMA();
    const unsigned kLead = 24;  // 120 ms: the app's first reads fill its AudioTrack (80 ms)
    unsigned blocks = 0;
    double soundFrom = seconds();
    void* fifo = (void*) (unsigned long) 0x82800000u;
    void* xfb = (void*) (unsigned long) 0x82830000u;
    unsigned frames = 0, padAt = 0, bFrames = 0, buttons = 0, dofChanges = 0, debugMenu = 0, letterboxChanges = 0;
    int dof = re4_dof_enabled(), letterbox = re4_letterbox_off();
    unsigned peekFar = 0, peekHits = 0, peekLast = 0;
    for (; frames < 60000; frames++) {
        PadStatus pads[4];
        PADRead(pads);
        const PadStatus& p = pads[0];
        // the sound due by now (the AI plays a block each 5 ms); after a stand (the app's menu) from now on
        unsigned due = kLead + (unsigned) ((seconds() - soundFrom) * 200.0);
        if (due > blocks + 40) {
            soundFrom = seconds() - ((double) blocks - kLead) / 200.0;
            due = blocks;
        }
        for (; blocks < due; blocks++) {
            re4_ai_block();
        }
        if (!padAt && p.button == 0x0100 && p.stickX == 50 && p.stickY == -20 && p.triggerRight == 200 &&
            p.analogA == 0xFF) {
            padAt = frames + 1;
        }
        if (p.button & 0x0200) {
            bFrames++;
        }
        buttons |= p.button;
        PADControlMotor(0, (p.button & 0x0400) ? 1 : 0);
        const int d = re4_dof_enabled();
        if (d != dof) {
            dofChanges++;
            dof = d;
        }
        const int l = re4_letterbox_off();
        if (l != letterbox) {
            letterboxChanges++;
            letterbox = l;
        }
        // the game's GXPeekZ: the depth map the app writes into the shared memory (cell 50, 30)
        peekLast = re4_gx_efb_peek(100, 60, 1);
        (peekLast == 0x00FFFFFF ? peekFar : peekHits)++;
        if (re4_debug_menu_request()) {
            debugMenu++;
        }
        re4probe_gx_produce(fifo, xfb, 1, frames == 0);
        if (p.button & 0x1000) {
            break;
        }
        usleep(3000);
    }
    re4_log("remote-probe: letterbox changes %u (now %d)", letterboxChanges, letterbox);
    re4_log("remote-probe: peek far %u, hits %u, last 0x%x", peekFar, peekHits, peekLast);
    re4_log("remote-probe: frames %u, pad at %u, B frames %u, buttons 0x%04x, dof changes %u (now %d), debug menu %u, "
            "AI blocks %u",
            frames, padAt, bFrames, buttons, dofChanges, dof, debugMenu, blocks);
    return 7;
}
