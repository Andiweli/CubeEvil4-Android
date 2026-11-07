// Compiled with the game's flags and headers (re4_game_flags) for the movie
// test (re4_host --movie-test, port/tools/test_whole_movie.py): a Sofdec movie
// of the data folder is played through the CRI middleware of src/lib the way
// game/sofdec.cpp does it (SofdecInit, cSofdec::initApp / startApp / appMain /
// loadMvFrmFx / finishMovie), with the boot order of main.cpp (DVD, ARAM,
// Sofdec, then AI/AX as Snd_system_init). One loop pass is one video field:
// the audio frames of the field as vi.cpp delivers them (an interrupt), then
// the movie's main step. Every new frame (its number from the frame record)
// is converted to the Y8 / C4:4 textures like loadMvFrmFx and written back as
// planar YUV 4:2:0, the sound of every field is taken from the AI output.
#include "mwply.h"

// RE4_MOVIE_GX: the frames are drawn by the game's own cSofdec::drawTex
// (private: opened up for this test only) through the GX emulation
#define private public
#include "sofdec.h"
#undef private
#include "main_sub.h"
#include "../runtime/re4_gx_frame.h"
#define RE4_GX_FRAME_IO_WRITE_ONLY  // game flags: no STL here
#include "../runtime/re4_gx_frame_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {
void ADXGC_SetupDvdFs(int mode);
void ADXM_ExecMain(void);
u32 ARInit(u32* stack, u32 entries);
u32 ARAlloc(u32 length);
void ARQInit(void);
void AIInit(u8* stack);
void AXInitEx(u32 mode);
void MIXInit(void);
void* GXInit(void* base, u32 size);
void VISetNextFrameBuffer(void* fb);
void VIFlush(void);
void MIXSetSoundMode(u32 mode);
void re4_gx_sync(void);  // runtime/gx_thread.cpp: the GP thread caught up (RE4_GX_THREAD=1)
void MIXUpdateSettings(void);
typedef void (*AXCallback)(void);
AXCallback AXRegisterCallback(AXCallback callback);
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, u8 mipmap, u8 max_lod);
void re4_os_interrupt_begin(void);
void re4_os_interrupt_end(void);
void re4_ai_field(void);
void re4_os_idle(void);
int re4_audio_take(short* out, int frames);
const char* re4_data_dir(void);
int re4_mem1_init(void);
void re4_gx_stats(unsigned out[11]);
void re4_gx_stats2(unsigned out[9]);
}
#include <dolphin/dvd.h>

namespace {

unsigned fnv1a(const void* data, size_t n)
{
    const unsigned char* p = (const unsigned char*) data;
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

int gErrors;
char gLastError[256];

void errorCallback(void*, const char* msg)
{
    // cSofdec's callback hangs (ap_mwply_err_func); the test counts and goes on
    gErrors++;
    snprintf(gLastError, sizeof(gLastError), "%s", msg ? msg : "?");
    fprintf(stderr, "[movie] CRI error: %s\n", gLastError);
}

// the part of the game's audio frame (snd_main.cpp cb_audio_frame) the movie
// voices need: MIX hands their volume and pan to AX
void audioFrame()
{
    MIXUpdateSettings();
}

void field()
{
    re4_os_interrupt_begin();
    re4_ai_field();
    re4_os_interrupt_end();
}

void writeWavHeader(FILE* f, unsigned frames)
{
    unsigned bytes = frames * 4;
    unsigned char h[44];
    memcpy(h, "RIFF", 4);
    unsigned v = 36 + bytes;
    memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16;
    memcpy(h + 16, &v, 4);
    unsigned short s = 1;
    memcpy(h + 20, &s, 2);
    s = 2;
    memcpy(h + 22, &s, 2);
    v = 32000;
    memcpy(h + 24, &v, 4);
    v = 32000 * 4;
    memcpy(h + 28, &v, 4);
    s = 4;
    memcpy(h + 32, &s, 2);
    s = 16;
    memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

// The Y8 (I8: 8x4 tiles) and C4:4 (IA8: 4x4 tiles, Cb then Cr) textures of
// loadMvFrmFx back to planes of the movie's size.
void untile(const u8* ybuf, const u8* cbuf, int tw, int th, int cw, int w, int h, u8* y, u8* cb, u8* cr)
{
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tile = (py / 4) * (tw / 8) + px / 8;
            y[py * w + px] = ybuf[tile * 32 + (py % 4) * 8 + px % 8];
        }
    }
    (void) th;
    for (int py = 0; py < h / 2; py++) {
        for (int px = 0; px < w / 2; px++) {
            int tile = (py / 4) * (cw / 4) + px / 4;
            const u8* t = cbuf + tile * 32 + ((py % 4) * 4 + px % 4) * 2;
            cb[py * (w / 2) + px] = t[0];
            cr[py * (w / 2) + px] = t[1];
        }
    }
}

// --- the frames through the game's GX drawing (RE4_MOVIE_GX=DIR) -----------------
// The Y8 and C4:4 textures, the FIFO and the display copy live in MEM1 above
// what the test uses (the GP reads memory physically, as on the GameCube).
const u32 kGxFifo = 0x82000000u, kGxXfb = 0x82100000u, kGxTex = 0x82200000u;
bool gGxInit;

void gxCopy()
{
    GXSetDispCopySrc(0, 0, 512, 448);
    GXSetDispCopyDst(512, 448);
    Bg_brightness_set(64.0f);  // the game's copy filter at normal brightness (Render_done)
    GXCopyDisp((void*) (unsigned long) kGxXfb, 1);
    VISetNextFrameBuffer((void*) (unsigned long) kGxXfb);
    VIFlush();
}

// Draws the frame in the textures as cSofdec::draw does (draw mode 0) and
// writes the GX chunk to DIR/gx-movie-FNO.bin for gx_chunk_render.
void gxDrawFrame(const char* dir, int fno, const u8* ybuf, const u8* cbuf, u32 ysize, u32 csize, int texW,
                 int texH, int w2)
{
    u8* yt = (u8*) (unsigned long) kGxTex;
    u8* ct = yt + ((ysize + 31) & ~31u);
    memcpy(yt, ybuf, ysize);
    memcpy(ct, cbuf, csize);
    if (!gGxInit) {
        gGxInit = true;
        GXInit((void*) (unsigned long) kGxFifo, 0x10000);
        Rmode.fbWidth = 512;  // ScreenReSize(0x200, 0x1C0) of cSofdec::initSync
        Rmode.efbHeight = 448;
        Rmode.xfbHeight = 448;
    }
    re4_core_gx_acquire();  // attaches the renderer side (nothing built yet)
    gxCopy();               // the chunk starts at this frame boundary
    GXSetViewport(0.0f, 0.0f, 512.0f, 448.0f, 0.0f, 1.0f);
    GXSetScissor(0, 0, 512, 448);
    GXSetCullMode(0);           // none
    GXSetZMode(0, 7, 0);        // no depth test or update
    GXSetColorUpdate(1);
    TexObj* tex = &Sofdec.m_scn_ctrl.tex;  // cSofdec::allocTexMem, draw mode 0
    Sofdec.m_draw_mode = 0;
    tex->width = (u16) texW;
    tex->height = (u16) texH;
    tex->ybuf = yt;
    tex->cbuf = ct;
    GXInitTexObj(&tex->ytobj, yt, (u16) texW, (u16) texH, 1, 0, 0, 0);  // I8, clamp
    GXInitTexObj(&tex->ctobj, ct, (u16) w2, (u16) (texH / 2), 3, 0, 0, 0);  // IA8, clamp
    Sofdec.drawTex();
    gxCopy();
    re4_gx_sync();
    const Re4GxChunk* c = re4_core_gx_acquire();
    if (!c) {
        fprintf(stderr, "[movie] GX: no chunk for frame %d\n", fno);
    }
    if (c) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/gx-movie-%05d.bin", dir, fno);
        if (FILE* f = fopen(path, "wb")) {
            re4gxio::Write(f, *c, (uint32_t) fno);
            fclose(f);
        }
        re4_core_gx_release(c);
    }
    re4_core_gx_detach();
}

}  // namespace

// Plays `fname` for at most `maxFields` fields. Writes the frames to `yuvPath`
// (4:2:0 planes, one record per new frame number: a 16-byte header "FRM",
// frame number, first field, then the planes), the sound to `wavPath` (32 kHz
// stereo) and a summary line per frame to stderr. Returns the number of
// frames written, -1 when the player could not start.
extern "C" int re4probe_movie_play(const char* fname, const char* yuvPath, const char* wavPath, int maxFields)
{
    static bool booted;
    if (!booted) {
        booted = true;
        re4_mem1_init();  // MEM1 with the OS globals (bus clock at 0x800000F8)
        ADXGC_SetupDvdFs(0);  // cDvd::Init
        static u32 aramStack[3];
        ARInit(aramStack, 3);  // SndInit
        ARAlloc(0x6FC000);
        ARQInit();
        MWS_PLY_INIT_SFD prm;  // SofdecInit
        memset(&prm, 0, sizeof(prm));
        prm.disp_cycle = 59.94f;
        prm.x04 = 1;
        prm.x08 = 1;
        prm.x0C = 1;
        ADXM_SetCbErr(errorCallback, NULL);
        mwPlyInitSfdFx(&prm);
        AIInit(NULL);  // Snd_system_init (Snd_sound_mode_init: stereo)
        AXInitEx(1);
        MIXInit();
        MIXSetSoundMode(1);
        AXRegisterCallback(audioFrame);
    }
    gErrors = 0;

    // RE4_MOVIE_DISC=1: the movie is on the second disc (the game asks for it
    // with DVDChangeDisk before such a movie)
    int disc = 0;
    if (const char* d = getenv("RE4_MOVIE_DISC")) {
        disc = atoi(d) == 1 ? 1 : 0;
    }
    if (disc) {
        static DVDCommandBlock block;
        static DVDDiskID id;
        memset(&id, 0, sizeof(id));
        id.diskNumber = 1;
        DVDChangeDiskAsync(&block, &id, NULL);
    }
    // cSofdec::initApp: the size from the first 0x5000 bytes of the file
    char path[1024];
    snprintf(path, sizeof(path), "%s/disc%d/files/%s", re4_data_dir(), disc, fname);
    FILE* in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "[movie] %s missing\n", path);
        return -1;
    }
    static u8 head[0x5000];
    size_t got = fread(head, 1, sizeof(head), in);
    fclose(in);
    MWS_SFD_HDRINF info;
    memset(&info, 0, sizeof(info));
    mwPlyGetHdrInf(head, (int) got, &info);
    int width = info.width, height = info.height;
    fprintf(stderr, "[movie] header: %d x %d\n", width, height);
    if (width <= 0 || height <= 0) {
        return -1;
    }

    // cSofdec::startApp
    // in AP_OBJ the parameters are followed by zeroed bytes the CRI library
    // reads as its buffer format (MWSFD_CRPRM is 0x30 bytes, include/sofdec.h)
    static struct {
        MWS_PLY_CPRM_SFD cprm;
        u8 pad[0xC];
    } ap;
    memset(&ap, 0, sizeof(ap));
    MWS_PLY_CPRM_SFD& cprm = ap.cprm;
    cprm.compo_mode = 0;
    cprm.ftype = 1;
    cprm.max_bps = 8000000;
    cprm.nfrm_pool_wk = 4;
    cprm.max_width = width;
    cprm.max_height = height;
    cprm.max_stm = 2;
    cprm.wksize = mwPlyCalcWorkCprmSfd(&cprm);
    void* work = aligned_alloc(32, ((size_t) cprm.wksize + 31) & ~(size_t) 31);
    memset(work, 0, (size_t) cprm.wksize);
    cprm.work = work;
    MWPLY hn = mwPlyCreateSofdec(&cprm);
    if (!hn) {
        fprintf(stderr, "[movie] mwPlyCreateSofdec failed (work %d bytes)\n", cprm.wksize);
        return -1;
    }
    mwPlyStartFname(hn, fname);

    // cSofdec::allocTexMem (draw mode 0)
    int w2 = (width / 2 + 31) & ~31;
    int texW = w2 * 2, texH = height;
    u32 ysize = GXGetTexBufferSize((u16) texW, (u16) texH, 1, 0, 0);
    u32 csize = GXGetTexBufferSize((u16) w2, (u16) (height / 2), 3, 0, 0);
    u8* ybuf = (u8*) aligned_alloc(32, (ysize + 31) & ~31u);
    u8* cbuf = (u8*) aligned_alloc(32, (csize + 31) & ~31u);
    memset(ybuf, 0, ysize);
    memset(cbuf, 0x80, csize);

    FILE* yuv = fopen(yuvPath, "wb");
    FILE* wav = fopen(wavPath, "wb");
    if (!yuv || !wav) {
        return -1;
    }
    unsigned char zero[44] = {0};
    fwrite(zero, 1, 44, wav);
    unsigned audioFrames = 0;
    size_t planes = (std::size_t) width * height * 3 / 2;
    u8* plane = (u8*) malloc(planes);
    int lastFno = -1, frames = 0, stat = 0, f = 0;
    bool trace = getenv("RE4_MOVIE_TRACE") != NULL;
    const char* gxDir = getenv("RE4_MOVIE_GX");
    long long stepTotal = 0, stepMax = 0;
    // real time, as in the game: the CRI player smooths the sound clock with
    // the time base (sfd_adxt.c sfadxt_UpdateTime, OSGetTime), so fields
    // faster than 59.94 Hz would make the clock lag at the end of the sound.
    // RE4_MOVIE_FAST=1 (with RE4_VI_FAST=1, as the PC check runs the game):
    // the fields as vi.cpp delivers them, at once, the CRI clock counts them
    bool fast = getenv("RE4_MOVIE_FAST") != NULL;
    // RE4_MOVIE_HZ=60: fields at another rate, as on a 60 Hz display the
    // retraces follow it (runtime/vi.cpp); the CRI clock stays real time
    double hz = getenv("RE4_MOVIE_HZ") ? atof(getenv("RE4_MOVIE_HZ")) : 0.0;
    long long fieldNs = hz > 1.0 ? (long long) (1e9 / hz) : 1000000000LL * 1001 / 60000;
    // RE4_MOVIE_EVERY=N: a frame record at most every N fields (stills of a
    // whole movie; the count in the end line stays the frames handed out)
    int every = getenv("RE4_MOVIE_EVERY") ? atoi(getenv("RE4_MOVIE_EVERY")) : 0, nextRecord = 0;
    // fields that began more than half a field after their time (the machine was busy): the CRI
    // clock is real time, a frame then stays one or three fields (test_whole_movie.py excuses those)
    int lateFields = 0;
    // RE4_TRACE=FILE: a state trace line per field (port/tools/trace_compare.py): the frame shown,
    // the player status, checksums of the decoded picture (Y8 and C4:4 textures) and of the sound
    // of the field, the GP counts; the same movie on a 32- and a 64-bit build must give the same
    // lines (the CRI middleware's own 64-bit view of its structures, milestone 12)
    FILE* stateTrace = getenv("RE4_TRACE") ? fopen(getenv("RE4_TRACE"), "w") : NULL;
    unsigned gxLast[12] = {0};
    unsigned picY = 0, picC = 0;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (; f < maxFields; f++) {
        long long due = (long long) f * fieldNs;
        for (bool first = true; !fast; first = false) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long ns = (now.tv_sec - start.tv_sec) * 1000000000LL + (now.tv_nsec - start.tv_nsec);
            if (ns >= due) {
                if (first && f > 0 && ns - due > fieldNs / 2) {
                    lateFields++;
                }
                break;
            }
            struct timespec wait = {0, (long) (due - ns)};
            nanosleep(&wait, NULL);
        }
        if (fast) {
            re4_os_idle();
        } else {
            field();
        }
        static short pcm[2 * 1024];
        int n = re4_audio_take(pcm, 1024);
        if (n > 0) {
            fwrite(pcm, 4, (size_t) n, wav);
            audioFrames += (unsigned) n;
        }
        unsigned sound = n > 0 ? fnv1a(pcm, (size_t) n * 4) : 0;
        // cSofdec::appMain
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        ADXM_ExecMain();
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long long us = ((t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec)) / 1000;
        stepTotal += us;
        stepMax = us > stepMax ? us : stepMax;
        MWS_FRM frm;
        mwPlyGetCurFrm(hn, &frm);
        if (frm.bufadr != NULL) {
            mwPlyFxSetOutBufSize(hn, texW, texH);  // loadMvFrmFx
            mwPlyFxCnvFrmY84C44(hn, &frm, ybuf, cbuf);
            if (frm.fno != lastFno && f >= nextRecord) {
                nextRecord = f + every;
                untile(ybuf, cbuf, texW, texH, w2, width, height, plane, plane + width * height,
                       plane + width * height + (width / 2) * (height / 2));
                unsigned rec[4] = {0x4D5246u, (unsigned) frm.fno, (unsigned) f, (unsigned) (width | height << 16)};
                fwrite(rec, 4, 4, yuv);
                fwrite(plane, 1, planes, yuv);
                if (gxDir && (frm.fno == 10 || frm.fno == 60 || frm.fno == 100)) {
                    gxDrawFrame(gxDir, frm.fno, ybuf, cbuf, ysize, csize, texW, texH, w2);
                }
            }
            if (frm.fno != lastFno) {
                lastFno = frm.fno;
                frames++;
                if (stateTrace) {
                    picY = fnv1a(ybuf, ysize);
                    picC = fnv1a(cbuf, csize);
                }
            }
            mwPlyRelCurFrm(hn);
        }
        stat = mwPlyGetStat(hn);
        if (stateTrace) {
            int c = 0, sc = 1;
            mwPlyGetTime(hn, &c, &sc);
            unsigned a[11], b[9];
            re4_gx_stats(a);
            re4_gx_stats2(b);
            const unsigned now[12] = {a[1], a[6], a[7], a[8], a[4], a[2], b[3], b[4], b[0], b[1], b[2], a[9]};
            fprintf(stateTrace, "{\"frame\":%d,\"Rno0\":%d,\"room\":\"movie\",\"n\":[%d,%d],"
                                "\"chk\":[%u,%u,%u],\"gx\":[",
                    f, stat, lastFno, c, picY, picC, sound);
            for (int i = 0; i < 12; i++) {
                fprintf(stateTrace, "%s%u", i ? "," : "", now[i] - gxLast[i]);
                gxLast[i] = now[i];
            }
            fprintf(stateTrace, "]}\n");
        }
        if (trace && f % 10 == 0) {
            int c = 0, s = 1;
            mwPlyGetTime(hn, &c, &s);
            fprintf(stderr, "[movie] field %d: status %d, time %d/%d, frame %d\n", f, stat, c, s, lastFno);
        }
        if (stat == MWE_PLY_STAT_PLAYEND || stat == MWE_PLY_STAT_ERROR) {
            break;
        }
    }
    int count = 0, scale = 0;
    mwPlyGetTime(hn, &count, &scale);
    fprintf(stderr, "[movie] end: status %d after %d fields, %d frames, time %d/%d, skipped %d decode / %d display, %d CRI errors, %d late fields\n",
            stat, f, frames, count, scale, mwPlyGetNumSkipDec(hn), mwPlyGetNumSkipDisp(hn), gErrors, lateFields);
    if (stateTrace) {
        fclose(stateTrace);
    }
    fprintf(stderr, "[movie] step (ADXM_ExecMain): %.2f ms on average, at most %.2f ms; size %d x %d\n",
            f > 0 ? stepTotal / 1000.0 / f : 0.0, stepMax / 1000.0, width, height);
    if (gErrors) {
        fprintf(stderr, "[movie] last CRI error: %s\n", gLastError);
    }
    // cSofdec::finishMovie
    mwPlyDestroy(hn);
    free(work);
    free(ybuf);
    free(cbuf);
    free(plane);
    fclose(yuv);
    writeWavHeader(wav, audioFrames);
    fclose(wav);
    if (stat == MWE_PLY_STAT_PLAYEND || (getenv("RE4_MOVIE_PART") && stat == 2 && gErrors == 0)) {
        return frames;  // RE4_MOVIE_PART: the first fields of a long movie
    }
    return -2;
}
