// DSP, the audio DSP (SDK libdsp), running the AX microcode, rebuilt in C++.
//
// The original AX library (src/lib/AX*.c) drives the DSP as on the GameCube:
// AXOut.c boots its microcode task (DSPAddTask), then once per AI block
// (5 ms, runtime/ai.cpp) sends mail 0xBABE0180 and the address of a command
// list (AXCL.c __AXNextFrame). The microcode (DSPCode.c, DSP machine code)
// cannot run here; this file does what it does with that list:
//
//   0x00 studio   the mix buffers (main, aux A, aux B; left/right/surround)
//                 start with the depop ramps of AXSPB.c
//   0x07 / 0x11   the surround of the last block goes into left and right
//                 (stereo: in phase; surround mode: opposite)
//   0x02 / 0x03   every voice of the PB list (AXVPB.c __AXPB): its samples
//                 from ARAM (DSP-ADPCM, PCM16, PCM8, loops, stream loop
//                 context), sample-rate conversion, volume envelope, low-pass,
//                 mixing into the buses with volume ramps; per millisecond
//                 (32 samples) after the PB updates of that millisecond
//   0x04 / 0x05   aux A / B: the bus goes to the CPU's effect (AXAux.c, an
//                 AXFX reverb, read two blocks later), the effect's output of
//                 two blocks ago is added to the main mix
//   0x13 / 0x10   the same for Dolby Pro Logic II (four channels: front left/right
//                 on the main buses, the rear (Ls, Rs) on aux B's left/right;
//                 aux A's effect gets L, R, Ls (aux A S) and Rs (aux B S))
//   0x12          the compressor (AXCL.c with AXSetCompressor(1), snd_main.cpp): a block
//                 whose peak reaches the threshold ramps down to the table's lowest
//                 gain (-4 dB), quieter blocks ramp back up one step each (10 x 5 ms)
//   0x0E          output: surround to the CPU buffer, left/right as 16 bit into
//                 the block the AI plays (right first, then left)
//   0x0F          end
//
// Then the task's resume callback runs, as the DSP's interrupt would.
// Addresses in the lists are pointers of this process (32-bit builds).
// Sample addresses count ADPCM nibbles, PCM16 samples or PCM8 bytes in ARAM.
//
// Not rebuilt: the polyphase filters of the DSP ROM (a 4-point cubic
// interpolation takes their place; linear stays linear), the time delay of
// AXSetVoiceItdOn, Dolby Pro Logic II's matrix encoding: the
// stereo the AI plays has the rear mixed into left and right (as before
// 20.10.2026), and the four channels of each block stay apart for the app
// (re4_dsp_surround: Android's spatializer can render them for headphones;
// decision of the project owner, 20.10.2026).
#include "dolphin/ax.h"
#include "dolphin/dsp.h"

#include "re4_runtime.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int kMs = 32;              // samples per millisecond (32 kHz)
const int kFrame = 5 * kMs;      // samples per AI block

enum Bus { L, R, S, AL, AR, AS, BL, BR, BS, kBuses };
s32 gBus[kBuses][kFrame];
// Dolby Pro Logic II's rear (Ls, Rs) of the block being mixed: the main mix's (the aux B buses,
// back from the 0x10 round trip) and aux A's effect return (0x13). The stereo output adds them
// to left and right (the main rear fully, the effect's at half); the four channels keep them.
s32 gRearMain[2][kFrame], gRearAux[2][kFrame];
bool gRearUsed;

// The compressor (0x12): the gain of each sample of this block (1.15), its step (0: no gain down,
// n: n steps below full), and the counts for the log (blocks it turned down, samples still past
// 16 bit). Rebuilt from the table AXComp.c gives the DSP (AXCL.c: threshold 0x8000, 10 steps):
// rows 0..10 ramp from step n to the lowest gain within one block, rows 11..20 from step n back
// to step n - 1, 160 samples each.
s32 gCompGain[kFrame];
bool gCompOn;
int gCompStep;
u32 gCompBlocks, gClipped;

// The four channels of the last blocks mixed in Dolby Pro Logic II, by the AI block's address
// (AXOut fills two or three blocks in turn); runtime/ai.cpp takes them when the block plays.
struct SurroundBlock {
    u32 addr;
    bool ready;
    s16 pcm[kFrame * 4];  // front left, front right, rear left, rear right
};
SurroundBlock gSurround[4];
unsigned gSurroundNext;

// ---------------------------------------------------------------- the DSP task
DSPTaskInfo* gTask;
bool gInit;
bool gExpectList;  // got 0xBABE0180: the next mail is a command list
u32 gLists;
u32 gUnknown;

template <class T>
T* ptr(u32 addr)
{
    return (T*) (uintptr_t) addr;
}

u32 hilo(u16 hi, u16 lo)
{
    return ((u32) hi << 16) | lo;
}

s16 clamp16(s32 v)
{
    return (s16) (v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

// ------------------------------------------------------- 0x00: the mix buffers
// AXSPB.c writes each start value as one 32-bit word over its Hi/Lo halves
// (__AXDepopFade), so it is read the same way.
void setup(u32 studioAddr)
{
    const AXSPB* spb = ptr<const AXSPB>(studioAddr);
    const u16* w = (const u16*) spb;
    for (int b = 0; b < kBuses; b++) {
        s32 v;
        memcpy(&v, w + b * 3, 4);
        const s16 delta = (s16) w[b * 3 + 2];
        for (int i = 0; i < kFrame; i++) {
            gBus[b][i] = v;
            v += delta;
        }
    }
    memset(gRearMain, 0, sizeof(gRearMain));
    memset(gRearAux, 0, sizeof(gRearAux));
    gRearUsed = false;
    gCompOn = false;
}

// ------------------------------------------------ 0x07 / 0x11: last surround
void foldSurround(u32 addr, bool opposite)
{
    const s32* s = ptr<const s32>(addr);
    for (int i = 0; i < kFrame; i++) {
        gBus[L][i] += opposite ? -s[i] : s[i];
        gBus[R][i] += s[i];
    }
}

// --------------------------------------------------------- the sample reader
// The DSP's accelerator: one sample at the voice's current address, then the
// end/loop handling of the microcode.
struct Reader {
    AXPB& pb;
    const u8* aram;
    u32 cur, end, loop;
    bool stopped;

    explicit Reader(AXPB& p) : pb(p), aram(re4_aram()), stopped(false)
    {
        cur = hilo(pb.addr.currentAddressHi, pb.addr.currentAddressLo);
        end = hilo(pb.addr.endAddressHi, pb.addr.endAddressLo);
        loop = hilo(pb.addr.loopAddressHi, pb.addr.loopAddressLo);
    }

    u8 byte(u32 at) const
    {
        return at < 0x1000000 ? aram[at] : 0;
    }

    s16 next()
    {
        if (stopped) {
            return 0;
        }
        s16 v;
        const u32 at = cur;
        switch (pb.addr.format) {
        case 0: {  // DSP-ADPCM: 8-byte frames, a header byte then 14 nibbles
            u32 a = cur;
            if ((a & 15) == 0) {
                pb.adpcm.pred_scale = byte(a >> 1);
                a += 2;
            }
            const u8 b = byte(a >> 1);
            int nibble = (a & 1) ? (b & 15) : (b >> 4);
            if (nibble >= 8) {
                nibble -= 16;
            }
            const int scale = 1 << (pb.adpcm.pred_scale & 15);
            const int index = (pb.adpcm.pred_scale >> 4) & 7;
            const s32 c1 = (s16) pb.adpcm.a[index][0];
            const s32 c2 = (s16) pb.adpcm.a[index][1];
            const s32 out = scale * nibble + ((0x400 + c1 * (s16) pb.adpcm.yn1 + c2 * (s16) pb.adpcm.yn2) >> 11);
            v = clamp16(out);
            pb.adpcm.yn2 = pb.adpcm.yn1;
            pb.adpcm.yn1 = (u16) v;
            cur = a + 1;
            if (a == end || at == end) {
                atEnd();
            }
            return v;
        }
        case 10:  // PCM16, big-endian
            v = (s16) ((byte(cur * 2) << 8) | byte(cur * 2 + 1));
            break;
        case 25:  // PCM8
            v = (s16) ((s8) byte(cur) << 8);
            break;
        default:
            v = 0;
            break;
        }
        cur++;
        if (at == end) {
            atEnd();
        }
        return v;
    }

    // The microcode at the end address: to the loop address with the loop's
    // ADPCM context (a stream keeps its history), or the voice stops.
    void atEnd()
    {
        if (pb.addr.loopFlag) {
            cur = loop;
            pb.adpcm.pred_scale = pb.adpcmLoop.loop_pred_scale;
            if (pb.type == 0) {
                pb.adpcm.yn1 = pb.adpcmLoop.loop_yn1;
                pb.adpcm.yn2 = pb.adpcmLoop.loop_yn2;
            }
        } else {
            cur = end;  // the voice stops at its end address
            pb.state = 0;
            stopped = true;
        }
    }

    void store()
    {
        pb.addr.currentAddressHi = (u16) (cur >> 16);
        pb.addr.currentAddressLo = (u16) cur;
    }
};

// 32 input samples at the voice's rate: the last four source samples stay in
// the PB (src.last_samples, oldest first), the output lies between the middle
// two at the fraction src.currentAddressFrac.
void source(AXPB& pb, Reader& in, s16* out)
{
    s32 h[4];
    for (int i = 0; i < 4; i++) {
        h[i] = (s16) pb.src.last_samples[i];
    }
    u32 frac = pb.src.currentAddressFrac;
    u32 ratio = hilo(pb.src.ratioHi, pb.src.ratioLo);
    if (pb.srcSelect == 2) {
        ratio = 0x10000;  // no conversion: one sample each
        frac = 0;
    }
    if (ratio > 0x40000) {
        ratio = 0x40000;
    }
    for (int i = 0; i < kMs; i++) {
        if (pb.srcSelect == 2) {
            out[i] = (s16) h[2];
        } else if (pb.srcSelect == 1) {
            out[i] = (s16) (h[1] + (((h[2] - h[1]) * (s32) (frac >> 1)) >> 15));
        } else {
            // 4-point cubic (Catmull-Rom) in place of the ROM's polyphase filter
            const float t = (float) frac * (1.0f / 65536.0f);
            const float p0 = (float) h[0], p1 = (float) h[1], p2 = (float) h[2], p3 = (float) h[3];
            const float v = p1 + 0.5f * t * (p2 - p0 + t * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 +
                                                            t * (3.0f * (p1 - p2) + p3 - p0)));
            out[i] = clamp16((s32) (v < 0 ? v - 0.5f : v + 0.5f));
        }
        frac += ratio;
        for (u32 n = frac >> 16; n; n--) {
            h[0] = h[1];
            h[1] = h[2];
            h[2] = h[3];
            h[3] = in.next();
        }
        frac &= 0xFFFF;
    }
    for (int i = 0; i < 4; i++) {
        pb.src.last_samples[i] = (u16) h[i];
    }
    pb.src.currentAddressFrac = (u16) frac;
}

// One bus of one voice: the samples at the bus volume (0x8000 = 1.0), ramped
// by its delta per sample when the mixer control says so; the last value is
// the voice's depop value for that bus (AXSPB.c __AXDepopVoice).
void mixInto(s32* bus, const s16* in, u16& volume, u16 delta, bool ramp, s16& dpop)
{
    s32 v = 0;
    for (int i = 0; i < kMs; i++) {
        v = clamp16((s32) in[i] * (s32) volume >> 15);
        bus[i] += v;
        if (ramp) {
            volume = (u16) (volume + delta);
        }
    }
    dpop = (s16) v;
}

// The PB updates of millisecond `ms` (AXSetVoiceUpdateWrite: word offset into
// the PB, value).
void updates(AXPB& pb, int ms)
{
    if (!pb.update.updNum[ms]) {
        return;
    }
    const u16* list = ptr<const u16>(hilo(pb.update.dataHi, pb.update.dataLo));
    if (!list) {
        return;
    }
    u32 first = 0;
    for (int i = 0; i < ms; i++) {
        first += pb.update.updNum[i];
    }
    u16* words = (u16*) &pb;
    for (u32 i = first; i < first + pb.update.updNum[ms]; i++) {
        const u16 at = list[i * 2];
        if (at < sizeof(AXPB) / 2) {
            words[at] = list[i * 2 + 1];
        }
    }
}

u32 gVoiceMs;

void voice(AXPB& pb)
{
    for (int ms = 0; ms < 5; ms++) {
        updates(pb, ms);
        if (pb.state != 1) {
            continue;
        }
        gVoiceMs++;
        Reader in(pb);
        s16 s[kMs];
        source(pb, in, s);
        in.store();
        // the volume envelope
        u16 ve = pb.ve.currentVolume;
        for (int i = 0; i < kMs; i++) {
            s[i] = clamp16((s32) s[i] * (s32) ve >> 15);
            ve = (u16) (ve + pb.ve.currentDelta);
        }
        pb.ve.currentVolume = ve;
        if (pb.lpf.on) {
            s32 y = (s16) pb.lpf.yn1;
            for (int i = 0; i < kMs; i++) {
                y = clamp16((s32) (((int64_t) pb.lpf.a0 * s[i] + (int64_t) pb.lpf.b0 * y) >> 15));
                s[i] = (s16) y;
            }
            pb.lpf.yn1 = (u16) y;
        }
        const u16 c = pb.mixerCtrl;
        AXPBMIX& m = pb.mix;
        AXPBDPOP& d = pb.dpop;
        const int at = ms * kMs;
        if (c & 0x0001) mixInto(gBus[L] + at, s, m.vL, m.vDeltaL, c & 0x0008, d.aL);
        if (c & 0x0002) mixInto(gBus[R] + at, s, m.vR, m.vDeltaR, c & 0x0008, d.aR);
        if (c & 0x0004) mixInto(gBus[S] + at, s, m.vS, m.vDeltaS, c & 0x0008, d.aS);
        if (c & 0x0010) mixInto(gBus[AL] + at, s, m.vAuxAL, m.vDeltaAuxAL, c & 0x0040, d.aAuxAL);
        if (c & 0x0020) mixInto(gBus[AR] + at, s, m.vAuxAR, m.vDeltaAuxAR, c & 0x0040, d.aAuxAR);
        if (c & 0x0080) mixInto(gBus[AS] + at, s, m.vAuxAS, m.vDeltaAuxAS, c & 0x0100, d.aAuxAS);
        if (c & 0x0200) mixInto(gBus[BL] + at, s, m.vAuxBL, m.vDeltaAuxBL, c & 0x0800, d.aAuxBL);
        if (c & 0x0400) mixInto(gBus[BR] + at, s, m.vAuxBR, m.vDeltaAuxBR, c & 0x0800, d.aAuxBR);
        if (c & 0x1000) mixInto(gBus[BS] + at, s, m.vAuxBS, m.vDeltaAuxBS, c & 0x2000, d.aAuxBS);
    }
}

// ----------------------------------------------------- 0x03: the voice list
u32 gPeakVoices;  // most voices running at the start of a frame since the last stats

void process(u32 first)
{
    int guard = 0;
    u32 running = 0;
    for (u32 a = first; a && guard < AX_MAX_VOICES; guard++) {
        AXPB& pb = *ptr<AXPB>(a);
        running += pb.state == 1;
        voice(pb);
        a = hilo(pb.nextHi, pb.nextLo);
    }
    if (running > gPeakVoices) {
        gPeakVoices = running;
    }
}

// ------------------------------------------- 0x04 / 0x05: an aux bus and back
void aux(int first, u32 toCpu, u32 fromCpu)
{
    if (toCpu) {
        s32* out = ptr<s32>(toCpu);
        for (int c = 0; c < 3; c++) {
            memcpy(out + c * kFrame, gBus[first + c], sizeof(gBus[0]));
        }
    }
    if (fromCpu) {
        const s32* in = ptr<const s32>(fromCpu);
        for (int c = 0; c < 3; c++) {
            for (int i = 0; i < kFrame; i++) {
                gBus[L + c][i] += in[c * kFrame + i];
            }
        }
    }
}

// ------------------------------------------- 0x13 / 0x10: Dolby Pro Logic II
// Aux A (0x13): L, R and Ls (aux A S) go out in one buffer, Rs (aux B S: MIX's vBS in the DPL2
// mode) in a second (AXAux.c __AXGetAuxAInputDpl2; until 20.10.2026 aux A S went out twice);
// back come L, R (front) and Ls, Rs (rear). Aux B (0x10): the main rear (aux B L/R) goes out
// and comes back a block later (AXAux.c's two DPL2 positions).
void auxDpl2A(const u16* p)
{
    s32* inLR = ptr<s32>(hilo(p[0], p[1]));
    s32* inRs = ptr<s32>(hilo(p[2], p[3]));
    if (inLR) {
        memcpy(inLR, gBus[AL], sizeof(gBus[0]));
        memcpy(inLR + kFrame, gBus[AR], sizeof(gBus[0]));
        memcpy(inLR + 2 * kFrame, gBus[AS], sizeof(gBus[0]));
    }
    if (inRs) {
        memcpy(inRs, gBus[BS], sizeof(gBus[0]));
    }
    const s32* l = ptr<const s32>(hilo(p[4], p[5]));
    const s32* r = ptr<const s32>(hilo(p[6], p[7]));
    const s32* ls = ptr<const s32>(hilo(p[8], p[9]));
    const s32* rs = ptr<const s32>(hilo(p[10], p[11]));
    for (int i = 0; i < kFrame; i++) {
        gBus[L][i] += l ? l[i] : 0;
        gBus[R][i] += r ? r[i] : 0;
        gRearAux[0][i] += ls ? ls[i] : 0;
        gRearAux[1][i] += rs ? rs[i] : 0;
    }
    gRearUsed = true;
}

void auxDpl2B(const u16* p)
{
    s32* in = ptr<s32>(hilo(p[0], p[1]));
    const s32* back = ptr<const s32>(hilo(p[2], p[3]));
    if (in) {
        memcpy(in, gBus[BL], sizeof(gBus[0]));
        memcpy(in + kFrame, gBus[BR], sizeof(gBus[0]));
    }
    if (back) {
        for (int i = 0; i < kFrame; i++) {
            gRearMain[0][i] += back[i];
            gRearMain[1][i] += back[kFrame + i];
        }
    }
    gRearUsed = true;
}

// ------------------------------------------------------ 0x12: the compressor
// What the output will get as stereo (the rear mixed in as in output()).
s32 stereoLeft(int i)
{
    return gBus[L][i] + gRearMain[0][i] + gRearAux[0][i] / 2;
}

s32 stereoRight(int i)
{
    return gBus[R][i] + gRearMain[1][i] + gRearAux[1][i] / 2;
}

void compressor(u16 threshold, u16 steps, u32 tableAddr)
{
    const u16* table = ptr<const u16>(tableAddr);
    if (!table || steps == 0 || steps > 32) {
        return;
    }
    s32 peak = 0;
    for (int i = 0; i < kFrame; i++) {
        const s32 l = abs(stereoLeft(i)), r = abs(stereoRight(i));
        peak = l > peak ? l : peak;
        peak = r > peak ? r : peak;
        if (gRearUsed) {
            const s32 rl = abs(gRearMain[0][i] + gRearAux[0][i]), rr = abs(gRearMain[1][i] + gRearAux[1][i]);
            peak = rl > peak ? rl : peak;
            peak = rr > peak ? rr : peak;
        }
    }
    int row;
    if (peak >= threshold) {
        row = gCompStep > steps ? steps : gCompStep;
        gCompStep = steps;
        gCompBlocks++;
    } else if (gCompStep > 0) {
        row = steps + gCompStep;
        gCompStep--;
    } else {
        return;
    }
    const u16* g = table + row * kFrame;
    for (int i = 0; i < kFrame; i++) {
        gCompGain[i] = g[i];
    }
    gCompOn = true;
}

s16 gain16(s32 v, int i)
{
    if (gCompOn) {
        v = (s32) (((int64_t) v * gCompGain[i]) >> 15);
    }
    if (v < -32768 || v > 32767) {
        gClipped++;
    }
    return clamp16(v);
}

// --------------------------------------------------------- 0x0E: the output
void output(u32 surroundAddr, u32 blockAddr)
{
    if (surroundAddr) {
        memcpy(ptr<s32>(surroundAddr), gBus[S], sizeof(gBus[0]));
    }
    s16* block = ptr<s16>(blockAddr);
    if (!block) {
        return;
    }
    // the stereo: the rear into left and right, the main rear fully, the effect's at half
    for (int i = 0; i < kFrame; i++) {
        block[i * 2] = gain16(stereoRight(i), i);
        block[i * 2 + 1] = gain16(stereoLeft(i), i);
    }
    SurroundBlock* sb = 0;
    for (SurroundBlock& b : gSurround) {
        if (b.addr == blockAddr) {
            sb = &b;
        }
    }
    if (!gRearUsed) {
        if (sb) {
            sb->ready = false;
        }
        return;
    }
    if (!sb) {
        sb = &gSurround[gSurroundNext++ % 4];
    }
    sb->addr = blockAddr;
    sb->ready = true;
    for (int i = 0; i < kFrame; i++) {
        const s32 gain = gCompOn ? gCompGain[i] : 0x8000;
        sb->pcm[i * 4] = clamp16((s32) (((int64_t) gBus[L][i] * gain) >> 15));
        sb->pcm[i * 4 + 1] = clamp16((s32) (((int64_t) gBus[R][i] * gain) >> 15));
        sb->pcm[i * 4 + 2] = clamp16((s32) (((int64_t) (gRearMain[0][i] + gRearAux[0][i]) * gain) >> 15));
        sb->pcm[i * 4 + 3] = clamp16((s32) (((int64_t) (gRearMain[1][i] + gRearAux[1][i]) * gain) >> 15));
    }
}

void unknown(u16 command)
{
    if (gUnknown++ < 8) {
        re4_log("dsp: AX command 0x%x unknown, list ends", command);
    }
}

void run(const u16* p)
{
    u32 pbs = 0;
    for (int guard = 0; guard < 384; guard++) {
        const u16 command = *p++;
        switch (command) {
        case 0x00:
            setup(hilo(p[0], p[1]));
            p += 2;
            break;
        case 0x02:
            pbs = hilo(p[0], p[1]);
            p += 2;
            break;
        case 0x03:
            process(pbs);
            break;
        case 0x04:
            aux(AL, hilo(p[0], p[1]), hilo(p[2], p[3]));
            p += 4;
            break;
        case 0x05:
            aux(BL, hilo(p[0], p[1]), hilo(p[2], p[3]));
            p += 4;
            break;
        case 0x07:
        case 0x11:
            foldSurround(hilo(p[0], p[1]), command == 0x11);
            p += 2;
            break;
        case 0x10:
            auxDpl2B(p);
            p += 4;
            break;
        case 0x12:  // the compressor: threshold, steps, table
            compressor(p[0], p[1], hilo(p[2], p[3]));
            p += 4;
            break;
        case 0x13:
            auxDpl2A(p);
            p += 12;
            break;
        case 0x0E:
            output(hilo(p[0], p[1]), hilo(p[2], p[3]));
            p += 4;
            break;
        case 0x0F:
            return;
        default:
            unknown(command);
            return;
        }
    }
}

}  // namespace

extern "C" {

// The four channels of the AI block at `blockAddr` (front left, front right, rear left, rear
// right; 160 frames) when the DSP mixed it in Dolby Pro Logic II: 1, and taken; else 0
// (runtime/ai.cpp, when the block plays).
int re4_dsp_surround(unsigned blockAddr, short* out)
{
    for (SurroundBlock& b : gSurround) {
        if (b.ready && b.addr == blockAddr) {
            memcpy(out, b.pcm, sizeof(b.pcm));
            b.ready = false;
            return 1;
        }
    }
    return 0;
}

// For the log and tests: command lists run so far, voice milliseconds mixed
// so far, the most voices running in one frame since the last call.
void re4_dsp_stats(unsigned* lists, unsigned* voiceMs, unsigned* peakVoices)
{
    *lists = gLists;
    *voiceMs = gVoiceMs;
    *peakVoices = gPeakVoices;
    gPeakVoices = 0;
}

// The compressor so far: blocks it turned down, stereo samples still past 16 bit (clipped).
void re4_dsp_loudness(unsigned* compressed, unsigned* clipped)
{
    *compressed = gCompBlocks;
    *clipped = gClipped;
}

void DSPInit(void)
{
    gInit = true;
}

BOOL DSPCheckInit(void)
{
    return gInit;
}

void DSPReset(void) {}
void DSPHalt(void) {}
void DSPUnhalt(void) {}
void DSPAssertInt(void) {}

u32 DSPGetDMAStatus(void)
{
    return 0;
}

// The mailboxes are always empty: a mail is taken at once.
u32 DSPCheckMailToDSP(void)
{
    return 0;
}

u32 DSPCheckMailFromDSP(void)
{
    return 0;
}

u32 DSPReadCPUToDSPMbox(void)
{
    return 0;
}

u32 DSPReadMailFromDSP(void)
{
    return 0;
}

void DSPSendMailToDSP(u32 mail)
{
    if (gExpectList) {
        gExpectList = false;
        gLists++;
        run(ptr<const u16>(mail));
        // the microcode is done with the frame: the DSP interrupt's resume
        // callback (AXOut.c __AXDSPResumeCallback: the next frame may start)
        if (gTask && gTask->res_cb) {
            gTask->res_cb(gTask);
        }
        return;
    }
    if (mail == 0xBABE0180) {
        gExpectList = true;
        return;
    }
    if (gUnknown++ < 8) {
        re4_log("dsp: mail 0x%08x ignored", mail);
    }
}

// The task boots at once: its init callback (AXOut.c waits for it).
//
// AX's own budget for the DSP: __AXSyncPBs adds up an estimate of the DSP
// cycles of every running voice (rate conversion, buses, ramps) and stops the
// voices past OS_BUS_CLOCK / 400 (405000 cycles, what the GameCube's DSP does
// in 5 ms), newest first within a priority; the sound driver then reports
// "ISS Voice Drop" and a sound breaks off. This DSP has no such limit (all 64
// voices with every bus take a few percent of one CPU core), so it gives AX
// four times the cycles, more than 64 voices can need; RE4_DSP_ORIGINAL_LIMIT=1
// keeps the GameCube's.
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task)
{
    gTask = task;
    task->state = 1;
    if (task->init_cb) {
        task->init_cb(task);
    }
    const char* original = getenv("RE4_DSP_ORIGINAL_LIMIT");
    if (!(original && *original && *original != '0')) {
        const u32 gamecube = AXGetMaxDspCycles();
        AXSetMaxDspCycles(gamecube * 4);
        re4_log("dsp: AX may use %u DSP cycles per frame (the GameCube's DSP: %u)", gamecube * 4, gamecube);
    }
    return task;
}

static void taskDone(void* object, int)
{
    DSPTaskInfo* task = (DSPTaskInfo*) object;
    if (task->done_cb) {
        task->done_cb(task);
    }
}

// AXQuit: the task ends with the next interrupt (AXOut.c sleeps until then).
DSPTaskInfo* DSPCancelTask(DSPTaskInfo* task)
{
    if (task == gTask) {
        gTask = 0;
    }
    task->state = 3;
    if (!re4_os_defer(taskDone, task, 0)) {
        taskDone(task, 0);
    }
    return task;
}

DSPTaskInfo* DSPAssertTask(DSPTaskInfo* task)
{
    return task;
}

DSPTaskInfo* __DSPGetCurrentTask(void)
{
    return gTask;
}

}  // extern "C"
