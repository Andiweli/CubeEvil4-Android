// AI, the audio interface (SDK libai), and the sound output of the core.
//
// On the GameCube the AI plays the 32 kHz stereo blocks AX leaves in main
// memory: AIInitDMA names the next block, and when the AI starts a block it
// raises the DMA interrupt, whose callback (AXOut.c __AXOutAiCallback) has the
// DSP mix the next one. The original AX library (src/lib/AX*.c) runs unchanged
// on top of this; the DSP itself is runtime/dsp.cpp.
//
// Here a block of 160 frames (5 ms) starts 200 times per second of game time,
// delivered with the video retraces (vi.cpp, 3 or 4 per field). Each started
// block goes into a ring the app reads (re4_audio_read, any thread), which
// also absorbs the bursts of the retrace delivery and the drift between the
// game's clock and the device's: the reader keeps about 60 ms in the ring by
// playing a little faster or slower (at most 0.5 %, inaudible), and skips
// ahead after a pause of the app.
//
// RE4_AUDIO_WAV=file writes every block into a WAV file as well (checks;
// RE4_AUDIO_WAV_SECONDS=N stops after N seconds of game time).
//
// A second ring holds the same frames as four channels (front left, front
// right, rear left, rear right; re4_audio_read4): in the game's Dolby Pro
// Logic II mode the DSP keeps the rear apart (runtime/dsp.cpp
// re4_dsp_surround), otherwise the rear is silent. The app reads one of the
// two (Android's spatializer for headphones, 20.10.2026).
#include "re4_runtime.h"

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

typedef unsigned int u32;
typedef void (*AIDCallback)(void);

extern "C" u32 AXGetDspCycles(void);
extern "C" u32 AXGetMaxDspCycles(void);

const u32 kBlockFrames = 160;   // 5 ms at 32 kHz
const u32 kRing = 1 << 14;      // frames (512 ms), a power of two
const u32 kTarget = 1920;       // frames the reader keeps (60 ms)
const u32 kSkip = 8000;         // more than this (250 ms): skip to the target

AIDCallback gCallback;
bool gInit;                     // AIInit done (AIReset clears it, as in ai.c)
bool gStarted;                  // ever initialised: the AI clock runs
bool gDmaOn;
u32 gNextAddr, gNextLength;     // AIInitDMA: the block after the playing one
u32 gPlayAddr, gPlayLength;     // the block the AI plays now
u32 gDspRate = 1;               // AI_SAMPLERATE_32KHZ (0 = 48 kHz)
u32 gFieldAccumulator;
u32 gBlocks;
u32 gPeakCycles;                // AX's highest DSP estimate since the last log line
u32 gOverBudget;                // blocks in which AX stopped voices over its budget
void (*gBlockHook)(void);       // re4_ai_set_block_hook: a look before each block's callback

// the ring: interleaved left/right, written by the game thread, read by the app
int16_t gRing[kRing * 2];
// the same frames as front left, front right, rear left, rear right
int16_t gRing4[kRing * 4];
std::atomic<u32> gSurroundBlocks{0};  // blocks with Dolby Pro Logic II's rear
std::atomic<u32> gSurroundAt{0};      // gBlocks at the last of them
std::atomic<u32> gWrite{0}, gRead{0};
std::atomic<u32> gOverruns{0}, gUnderruns{0}, gSkips{0};
// the reader's resampling position between ring frames (16.16)
u32 gFrac;
std::mutex gReadLock;

FILE* gWav;
u32 gWavFrames;
u32 gWavLimit;  // RE4_AUDIO_WAV_SECONDS: frames, 0 = no limit

void wavHeader(FILE* f, u32 frames)
{
    const u32 bytes = frames * 4;
    unsigned char h[44];
    memcpy(h, "RIFF", 4);
    u32 v = 36 + bytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;
    memcpy(h + 16, &v, 4);
    const uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &channels, 2);
    v = 32000;
    memcpy(h + 24, &v, 4);
    v = 32000 * 4;
    memcpy(h + 28, &v, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), f);
    fseek(f, 0, SEEK_END);
}

// The block the AI starts playing: into the ring (and the WAV file). The DSP
// leaves right before left in each frame (runtime/dsp.cpp, as the AI reads it).
void play(u32 addr, u32 length)
{
    const int16_t* block = (const int16_t*) (uintptr_t) addr;
    const u32 frames = length / 4;
    if (!block || frames == 0) {
        return;
    }
    // the WAV of a check gets every block, also when nothing reads the ring (the PC runs: before
    // 27.10.2026 it ended after the first second, when the ring was full)
    if (gWav) {
        for (u32 i = 0; i < frames; i++) {
            const int16_t lr[2] = { block[i * 2 + 1], block[i * 2] };
            fwrite(lr, sizeof(lr), 1, gWav);
        }
        gWavFrames += frames;
        if (gWavLimit && gWavFrames >= gWavLimit) {
            wavHeader(gWav, gWavFrames);
            fclose(gWav);
            gWav = 0;
            re4_log("audio: WAV complete, %u s", gWavFrames / 32000);
        } else if ((gWavFrames & 0x7FFF) < frames) {
            wavHeader(gWav, gWavFrames);
            fflush(gWav);
        }
    }
    u32 w = gWrite.load(std::memory_order_relaxed);
    const u32 r = gRead.load(std::memory_order_acquire);
    if (w - r + frames > kRing) {
        gOverruns++;  // the app does not read (paused): this block is lost
        return;
    }
    for (u32 i = 0; i < frames; i++) {
        const u32 at = (w + i) & (kRing - 1);
        gRing[at * 2] = block[i * 2 + 1];
        gRing[at * 2 + 1] = block[i * 2];
    }
    static int16_t four[kBlockFrames * 4];
    const bool surround = frames == kBlockFrames && re4_dsp_surround(addr, four);
    for (u32 i = 0; i < frames; i++) {
        const u32 at = (w + i) & (kRing - 1);
        if (surround) {
            memcpy(&gRing4[at * 4], &four[i * 4], 4 * sizeof(int16_t));
        } else {
            gRing4[at * 4] = block[i * 2 + 1];
            gRing4[at * 4 + 1] = block[i * 2];
            gRing4[at * 4 + 2] = 0;
            gRing4[at * 4 + 3] = 0;
        }
    }
    if (surround) {
        gSurroundBlocks++;
        gSurroundAt = gBlocks;
    }
    gWrite.store(w + frames, std::memory_order_release);
}

// One AI block (5 ms): the next block starts, its DMA interrupt runs the
// callback (AX: the DSP mixes the block after it, the game's audio frame).
void tick()
{
    if (!gDmaOn) {
        return;
    }
    gPlayAddr = gNextAddr;
    gPlayLength = gNextLength;
    play(gPlayAddr, gPlayLength);
    gBlocks++;
    if (gBlocks % 2000 == 0) {  // every 10 s
        unsigned lists, voiceMs, peak;
        re4_dsp_stats(&lists, &voiceMs, &peak);
        static unsigned lastVoiceMs;
        re4_log("audio: %u blocks, %u ms waiting, %u lost, %u ran dry, %u skips; DSP %u lists, %.1f voices "
                "(at most %u), AX estimate at most %u of %u cycles, %u blocks over",
                gBlocks, (gWrite.load() - gRead.load()) / 32, gOverruns.load(), gUnderruns.load(),
                gSkips.load(), lists, (voiceMs - lastVoiceMs) / 10000.0, peak, gPeakCycles,
                AXGetMaxDspCycles(), gOverBudget);
        lastVoiceMs = voiceMs;
        gPeakCycles = 0;
    }
    if (gCallback) {
        if (gBlockHook) {
            gBlockHook();
        }
        gCallback();
        // AX's estimate of the DSP's work for this frame (__AXSyncPBs): at or
        // over its budget it stopped voices (runtime/dsp.cpp DSPAddTask)
        const u32 cycles = AXGetDspCycles();
        gPeakCycles = cycles > gPeakCycles ? cycles : gPeakCycles;
        if (AXGetMaxDspCycles() && cycles >= AXGetMaxDspCycles()) {
            gOverBudget++;
        }
    }
    // the DVD reads and ARAM DMAs the driver started finish before the next block
    re4_os_run_deferred();
}

}  // namespace

extern "C" {

// A video field went by (vi.cpp): the AI blocks of that time, 200 per second
// (a field is 1001/60000 s).
void re4_ai_field(void)
{
    if (!gStarted) {
        return;
    }
    gFieldAccumulator += 200 * 1001;
    while (gFieldAccumulator >= 60000) {
        gFieldAccumulator -= 60000;
        tick();
    }
}

// A function the game side runs before each block's callback (the sound driver's frame), on the
// game thread: state_trace.cpp counts the sound requests waiting for it.
void re4_ai_set_block_hook(void (*hook)(void))
{
    gBlockHook = hook;
}

// One AI block at once (tests).
void re4_ai_block(void)
{
    tick();
}

unsigned re4_ai_blocks(void)
{
    return gBlocks;
}

// The sound for the device: `frames` frames of `channels` (2: the ring, 4: the four-channel ring)
// at 32 kHz. Always fills `out` (silence when the ring runs dry); returns the frames that came
// from the game.
// channels 6: both at once, the stereo (2) and then the four channels (the core process, which
// hands both to the app: host/coreproc.cpp)
static int readRing(short* out, int frames, int channels)
{
    struct Source {
        const int16_t* ring;
        int channels;
    };
    Source sources[2] = {{channels == 4 ? gRing4 : gRing, channels == 4 ? 4 : 2}, {gRing4, 4}};
    const int nSources = channels == 6 ? 2 : 1;
    std::lock_guard<std::mutex> lock(gReadLock);
    u32 r = gRead.load(std::memory_order_relaxed);
    const u32 w = gWrite.load(std::memory_order_acquire);
    u32 fill = w - r;
    if (fill > kSkip) {
        // after a pause of the app: what the game played meanwhile is gone
        r = w - kTarget;
        fill = kTarget;
        gFrac = 0;
        gSkips++;
    }
    // a little faster when the ring fills up, slower when it runs low
    int adjust = ((int) fill - (int) kTarget) / 8;  // 1/65536 steps
    adjust = adjust > 328 ? 328 : adjust < -328 ? -328 : adjust;
    const u32 step = (u32) (65536 + adjust);
    int got = 0;
    for (int i = 0; i < frames; i++) {
        if (w - r < 2) {
            for (int c = 0; c < channels; c++) {
                out[i * channels + c] = 0;
            }
            continue;
        }
        const u32 a = r & (kRing - 1), b = (r + 1) & (kRing - 1);
        const int f = (int) (gFrac >> 4);  // 12 bits
        short* o = out + i * channels;
        for (int s = 0; s < nSources; s++) {
            const int16_t* ring = sources[s].ring;
            const int n = sources[s].channels;
            for (int c = 0; c < n; c++) {
                const int16_t va = ring[a * n + c], vb = ring[b * n + c];
                *o++ = (int16_t) (va + ((vb - va) * f >> 12));
            }
        }
        got++;
        gFrac += step;
        r += gFrac >> 16;
        gFrac &= 0xFFFF;
    }
    // a dropout: the sound broke off (not every read while there is none yet)
    static bool dry = true;
    if (got < frames && !dry) {
        gUnderruns++;
    }
    dry = got < frames;
    gRead.store(r, std::memory_order_release);
    return got;
}

int re4_audio_read(short* out, int frames)
{
    return readRing(out, frames, 2);
}

// The same as four channels: front left, front right, rear left, rear right.
int re4_audio_read4(short* out, int frames)
{
    return readRing(out, frames, 4);
}

// Both in one read: per frame the stereo (left, right), then the four channels.
int re4_audio_read6(short* out, int frames)
{
    return readRing(out, frames, 6);
}

// Blocks that carried Dolby Pro Logic II's rear so far (the app's choice of output, tests).
unsigned re4_audio_surround_blocks(void)
{
    return gSurroundBlocks.load();
}

// 1 while the game plays in Dolby Pro Logic II (a block with the rear within the last 0.5 s).
int re4_audio_surround_now(void)
{
    return gSurroundBlocks.load() != 0 && gBlocks - gSurroundAt.load() < 100;
}

// The frames in the ring as they are, without the reader's rate matching
// (tests); returns how many there were.
int re4_audio_take(short* out, int frames)
{
    std::lock_guard<std::mutex> lock(gReadLock);
    u32 r = gRead.load(std::memory_order_relaxed);
    const u32 w = gWrite.load(std::memory_order_acquire);
    int got = 0;
    for (; got < frames && r != w; got++, r++) {
        const u32 a = r & (kRing - 1);
        out[got * 2] = gRing[a * 2];
        out[got * 2 + 1] = gRing[a * 2 + 1];
    }
    gRead.store(r, std::memory_order_release);
    return got;
}

// The same frames with the four channels (tests): `out4` gets front left, front right, rear left,
// rear right of each frame `out` gets.
int re4_audio_take4(short* out, short* out4, int frames)
{
    std::lock_guard<std::mutex> lock(gReadLock);
    u32 r = gRead.load(std::memory_order_relaxed);
    const u32 w = gWrite.load(std::memory_order_acquire);
    int got = 0;
    for (; got < frames && r != w; got++, r++) {
        const u32 a = r & (kRing - 1);
        out[got * 2] = gRing[a * 2];
        out[got * 2 + 1] = gRing[a * 2 + 1];
        memcpy(&out4[got * 4], &gRing4[a * 4], 4 * sizeof(int16_t));
    }
    gRead.store(r, std::memory_order_release);
    return got;
}

// For the overlay and the log: blocks so far, frames in the ring, lost blocks
// (the app did not read), dropouts (the ring ran dry while the sound played),
// skips after a pause.
void re4_audio_stats(unsigned* blocks, unsigned* fill, unsigned* overruns, unsigned* underruns,
                     unsigned* skips)
{
    *blocks = gBlocks;
    *fill = gWrite.load() - gRead.load();
    *overruns = gOverruns.load();
    *underruns = gUnderruns.load();
    *skips = gSkips.load();
}

void AIInit(unsigned char* stack)
{
    (void) stack;
    if (gInit) {
        return;
    }
    gInit = true;
    gCallback = 0;  // ai.c: __AID_Callback = NULL
    gDspRate = 1;
    if (!gStarted) {
        gStarted = true;
        if (const char* wav = getenv("RE4_AUDIO_WAV")) {
            if (*wav && (gWav = fopen(wav, "wb")) != 0) {
                wavHeader(gWav, 0);
                const char* seconds = getenv("RE4_AUDIO_WAV_SECONDS");
                gWavLimit = seconds ? (u32) atoi(seconds) * 32000 : 0;
                re4_log("audio: writing %s", wav);
            }
        }
    }
}

// ai.c: only the init flag (AIInit may run again); the AI keeps playing
void AIReset(void)
{
    gInit = false;
}

int AICheckInit(void)
{
    return gInit;
}

AIDCallback AIRegisterDMACallback(AIDCallback callback)
{
    AIDCallback old = gCallback;
    gCallback = callback;
    return old;
}

void AIInitDMA(u32 start_addr, u32 length)
{
    gNextAddr = start_addr;
    gNextLength = length;
}

int AIGetDMAEnableFlag(void)
{
    return gDmaOn;
}

void AIStartDMA(void)
{
    gDmaOn = true;
}

void AIStopDMA(void)
{
    gDmaOn = false;
}

u32 AIGetDMABytesLeft(void)
{
    return 0;
}

u32 AIGetDMAStartAddr(void)
{
    return gNextAddr;
}

u32 AIGetDMALength(void)
{
    return gNextLength;
}

void AISetDSPSampleRate(u32 rate)
{
    gDspRate = rate;
}

u32 AIGetDSPSampleRate(void)
{
    return gDspRate;
}

// The disc's audio streaming (DTK): RE4 streams through ARAM instead.
typedef void (*AISCallback)(u32 count);
AISCallback AIRegisterStreamCallback(AISCallback callback)
{
    (void) callback;
    return 0;
}

u32 AIGetStreamSampleCount(void)
{
    return 0;
}

void AIResetStreamSampleCount(void) {}
void AISetStreamTrigger(u32) {}
u32 AIGetStreamTrigger(void)
{
    return 0;
}
void AISetStreamPlayState(u32) {}
u32 AIGetStreamPlayState(void)
{
    return 0;
}
void AISetStreamSampleRate(u32) {}
u32 AIGetStreamSampleRate(void)
{
    return 0;
}
void AISetStreamVolLeft(unsigned char) {}
unsigned char AIGetStreamVolLeft(void)
{
    return 0;
}
void AISetStreamVolRight(unsigned char) {}
unsigned char AIGetStreamVolRight(void)
{
    return 0;
}

}  // extern "C"
