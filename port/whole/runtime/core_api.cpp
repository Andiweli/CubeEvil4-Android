// Embedding interface of the game core (runtime/re4_core.h): the Android app
// starts the game through it, the Linux checks too (re4_host --core-test).
//
// Threads: the game runs as the boot thread of the Dolphin OS emulation
// (re4_os_run) on a host thread of its own. It publishes one snapshot per game
// frame from its frame hook (PADRead, once per frame); the app's GL thread
// takes the newest one. Three buffers: the game writes one, the newest waits
// in another, the reader holds the third, so neither side ever waits for the
// other's work and a reader never sees a half-written frame.
#include "re4_card_files.h"
#include "re4_core.h"
#include "re4_instant.h"
#include "re4_gx_frame_io.h"
#include "re4_runtime.h"

#include <dlfcn.h>
#include <strings.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_pad_live(const Re4CorePad* pad);
extern "C" unsigned re4_pad_rumble(void);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));
extern "C" int re4_log_tail(char* out, int size);
extern "C" void re4_state_trace_reconfigure(void);
// the card library's control blocks (src/lib/__card.h, CARDControl): the
// first word of slot A's says whether the library has the card mounted
extern "C" char __CARDBlock[];

namespace {

pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
int gState = RE4_CORE_IDLE;
bool gReserved;
char gMessage[256] = "not started";

Re4CoreSnapshot gBuffers[3];
int gWriting = 0;   // the game's buffer
int gReady = -1;    // newest published frame, -1 none
int gReading = -1;  // held by the reader
uint32_t gSerial;

void setMessage(const char* text)
{
    pthread_mutex_lock(&gLock);
    snprintf(gMessage, sizeof(gMessage), "%s", text);
    pthread_mutex_unlock(&gLock);
    re4_log("core: %s", text);
}

// The app's menu (M11) stops the game at its next frame until it closes
// (re4_core_pause): the game thread waits here, in PADRead, so nothing of the
// game runs meanwhile (no sound, no card access, no time for the enemies).
pthread_mutex_t gPauseLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t gPauseCond = PTHREAD_COND_INITIALIZER;
bool gPauseWanted, gPaused;
int gInstantPending = -1, gInstantResult;
bool gInstantBusy, gInstantDone;
Re4InstantInfo gInstantInfo;
// SELECT held in the app: the debug menu (patch 0061, re4_debug_menu_request)
volatile int gDebugMenuWanted;

void pausePoint()
{
    pthread_mutex_lock(&gPauseLock);
    if (gPauseWanted) {
        re4_instant_game_pause(1);
        gPaused = true;
        pthread_cond_broadcast(&gPauseCond);
        re4_log("core: paused (the app's menu)");
        while (gPauseWanted || gInstantPending >= 0) {
            if (gInstantPending >= 0) {
                const int operation = gInstantPending;
                // Only this thread can touch game objects, including while the menu is paused.
                pthread_mutex_unlock(&gPauseLock);
                const int result = re4_instant_perform(operation, &gInstantInfo);
                pthread_mutex_lock(&gPauseLock);
                gInstantResult = result;
                gInstantPending = -1;
                gInstantDone = true;
                pthread_cond_broadcast(&gPauseCond);
            } else pthread_cond_wait(&gPauseCond, &gPauseLock);
        }
        re4_instant_game_pause(0);
        gPaused = false;
        re4_log("core: running again");
    }
    pthread_mutex_unlock(&gPauseLock);
}

// The card functions of the menu only while the game stands and the card
// library has the card unmounted (between the game's card screens): then
// nothing else touches the card image, and the library reads directory and
// FAT anew at its next mount.
int cardFree()
{
    pthread_mutex_lock(&gPauseLock);
    bool paused = gPaused;
    pthread_mutex_unlock(&gPauseLock);
    int attached;
    memcpy(&attached, __CARDBlock, sizeof(attached));
    if (!paused || attached) {
        re4_log("core: card functions refused (%s)", paused ? "the game uses the card" : "the game does not stand");
        return 0;
    }
    return 1;
}

// PADRead, once per game frame, on the game's thread.
void frameHook(unsigned frame)
{
    pausePoint();
    Re4CoreSnapshot* s = &gBuffers[gWriting];
    re4_bridge_capture(s);
    s->frame = frame;
    pthread_mutex_lock(&gLock);
    s->serial = ++gSerial;
    int published = gWriting;
    // the next buffer to write: neither the one held by the reader nor the
    // one just published
    int next = 0;
    while (next == published || next == gReading) {
        next++;
    }
    gReady = published;
    gWriting = next;
    pthread_mutex_unlock(&gLock);
}

int (*gEntry)(void) = re4_game_main;
// the game's host thread (re4_core_game_thread: the app's hang report samples it)
volatile int gGameTid;

void* gameThread(void*)
{
    gGameTid = (int) syscall(SYS_gettid);
    re4_log("core: starting the game");
    int ret = re4_os_run(gEntry);
    char text[64];
    snprintf(text, sizeof(text), "re4_game_main returned %d", ret);
    setMessage(text);
    pthread_mutex_lock(&gLock);
    gState = RE4_CORE_ENDED;
    pthread_mutex_unlock(&gLock);
    return 0;
}

// RE4_BRIDGE_DUMP=FILE (Linux checks with disc data): every 300 frames one
// JSON line with what the app would draw -- step, room, camera and each model
// with its BIN/TPL disc sources -- so the local r100 run shows whether every
// drawn model can be found on the disc.
FILE* gDump;
Re4CoreSnapshot gDumpSnap;

// unknown sources as "?<where the address lies>" (formats.cpp)
extern "C" int re4_formats_describe(const void* addr, char* out, int size);

void dumpSource(FILE* f, const Re4CoreSource& s, uint32_t address)
{
    if (s.file[0]) {
        fprintf(f, "\"%s@0x%x#%d", s.file, (unsigned) s.fileOffset, (int) s.entry);
        if (s.innerOffset) {
            fprintf(f, "+0x%x", (unsigned) s.innerOffset);
        }
        fprintf(f, "\"");
    } else {
        char where[160];
        re4_formats_describe((const void*) (uintptr_t) address, where, sizeof(where));
        fprintf(f, "\"?%s\"", where);
    }
}

void dumpHook(unsigned frame)
{
    if (frame % 300 != 0) {
        return;
    }
    Re4CoreSnapshot* s = &gDumpSnap;
    re4_bridge_capture(s);
    FILE* f = gDump;
    fprintf(f, "{\"frame\":%u,\"Rno0\":%u,\"room\":\"0x%03x\",\"camera\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.1f]",
            frame, (unsigned) s->rno[0], (unsigned) s->room, s->cameraEye[0], s->cameraEye[1], s->cameraEye[2],
            s->cameraTarget[0], s->cameraTarget[1], s->cameraTarget[2], s->cameraRoll, s->cameraFovy);
    fprintf(f, ",\"screen\":%u,\"parts\":%u,\"dropped\":%u,\"droppedInfos\":%u,\"models\":[", (unsigned) s->screen,
            (unsigned) s->nParts, (unsigned) s->droppedModels, (unsigned) s->droppedInfos);
    for (unsigned i = 0; i < s->nModels; i++) {
        const Re4CoreModel& m = s->models[i];
        bool hidden = (m.flags & RE4_CORE_MODEL_HIDDEN) != 0;
        fprintf(f, "%s{\"mgr\":%u,\"id\":\"0x%02x\",\"type\":%u,\"drawn\":%d,\"parts\":%u,\"pos\":[%.1f,%.1f,%.1f],",
                i ? "," : "", (unsigned) m.manager, (unsigned) m.id, (unsigned) m.type,
                (m.beFlag & 6) == 6 && !hidden, (unsigned) m.nParts, m.matrix[3], m.matrix[7], m.matrix[11]);
        if (hidden) {
            fprintf(f, "\"hidden\":1,");  // the game leaves it out this frame (re4_core.h)
        }
        // how it is drawn, where it differs from a plain model
        fprintf(f, "\"ot\":%u,", (unsigned) m.otType);
        if (m.opacity != 1.0f) {
            fprintf(f, "\"opacity\":%.3f,", m.opacity);
        }
        if (m.zMode) {
            fprintf(f, "\"z\":%u,", (unsigned) m.zMode);
        }
        if (m.cullMode) {
            fprintf(f, "\"cull\":%u,", (unsigned) m.cullMode);
        }
        if (m.scrollIndex != RE4_CORE_NO_SCROLL) {
            fprintf(f, "\"smd\":[%d,%u],", (int) m.scrollBlock, (unsigned) m.scrollIndex);
        }
        fprintf(f, "\"infos\":[");
        for (unsigned k = 0; k < m.nInfos && m.firstInfo + k < s->nInfos; k++) {
            const Re4CoreModelInfo& mi = s->infos[m.firstInfo + k];
            fprintf(f, "%s{\"bin\":", k ? "," : "");
            dumpSource(f, mi.bin, mi.binAddress);
            fprintf(f, ",\"tpl\":");
            dumpSource(f, mi.tpl, mi.tplAddress);
            fprintf(f, ",\"shown\":%d,\"rigid\":%d", (mi.flags & 8) != 0, (int) mi.rigidPart);
            if (mi.blendMode) {
                fprintf(f, ",\"blend\":%u", (unsigned) mi.blendMode);
            }
            if (mi.color[3] != 0xFF || mi.opacity != 1.0f) {
                fprintf(f, ",\"alpha\":%u,\"opacity\":%.3f", (unsigned) mi.color[3], mi.opacity);
            }
            if (mi.nMorph) {
                fprintf(f, ",\"morph\":%u", (unsigned) mi.nMorph);
            }
            fprintf(f, "}");
        }
        fprintf(f, "]}");
    }
    fprintf(f, "]}\n");
    fflush(f);
}

// RE4_BRIDGE_SNAPSHOT=DIR (Linux checks with disc data): whole snapshots as
// binary files (DIR/snapshot-<frame>.bin, the Re4CoreSnapshot bytes; the
// layout is the same on 32 and 64 bit) once the game is in its main loop: 30,
// 150, 300, 450, 600 and 750 frames after it starts and every 2500 frames, at
// most 14. The app's host tool
// core_snapshot_render draws them as the app would (pictures in the report).
char gSnapDir[1024];
unsigned gSnapCount;
long gSnapFirst = -1;

// the opening of a room (r100: the car ride of event s40 runs about 700
// frames) in steps, then every 2500 frames
const unsigned kSnapAfterStart[] = {30, 150, 300, 450, 600, 750};

void snapshotHook(unsigned frame)
{
    if (gSnapCount >= 14) {
        return;
    }
    Re4CoreSnapshot* s = &gDumpSnap;
    re4_bridge_capture(s);
    if (s->rno[0] != 3) {
        return;
    }
    if (gSnapFirst < 0) {
        gSnapFirst = frame;
    }
    bool wanted = frame % 2500 == 0;
    for (unsigned after : kSnapAfterStart) {
        wanted = wanted || frame == (unsigned) gSnapFirst + after;
    }
    if (!wanted) {
        return;
    }
    s->frame = frame;
    s->serial = gSnapCount + 1;
    char path[1100];
    snprintf(path, sizeof(path), "%s/snapshot-%06u.bin", gSnapDir, frame);
    if (FILE* f = fopen(path, "wb")) {
        fwrite(s, sizeof(*s), 1, f);
        fclose(f);
        gSnapCount++;
        re4_log("core: snapshot %s (%u models, %u parts, room r%03x)", path, (unsigned) s->nModels,
                (unsigned) s->nParts, (unsigned) s->room);
    }
}

// RE4_GX_DUMP=DIR (Linux checks with disc data): chosen frames as the GX
// renderer of the app gets them (DIR/gx-<frame>.bin, re4_gx_frame_io.h): some
// frames of the start (logos, title), the room schedule of the snapshots and
// every 2500 frames, at most 24. The hook takes chunks only around those
// frames: the first chunk after attaching starts over (RESET), so each file
// holds the whole state and its textures. The app's host tool
// gx_chunk_render draws them (pictures in the report).
char gGxDir[1024];
unsigned gGxCount;
bool gGxAttached;

// RE4_GX_DUMP_FREE="150,600": also these frames after the player became free
// (state_trace.cpp), e.g. while the test script has the map open
extern "C" long re4_trace_free_at(void);

bool gxWantedAfterFree(unsigned frame)
{
    static const char* offsets = getenv("RE4_GX_DUMP_FREE");
    long freeAt = re4_trace_free_at();
    if (!offsets || !*offsets || freeAt < 0) {
        return false;
    }
    for (const char* p = offsets; *p;) {
        char* end = 0;
        long o = strtol(p, &end, 10);
        if (end == p) {
            break;
        }
        if ((long) frame == freeAt + o) {
            return true;
        }
        p = *end == ',' ? end + 1 : end;
    }
    return false;
}

// RE4_GX_DUMP_AT="4711,4712": these frames (the frame number of the state trace), e.g. the
// first frame where the 32- and the 64-bit run part (port/tools/trace_compare.py)
bool gxWantedAt(unsigned frame)
{
    static const char* frames = getenv("RE4_GX_DUMP_AT");
    for (const char* p = frames; p && *p;) {
        char* end = 0;
        unsigned long f = strtoul(p, &end, 10);
        if (end == p) {
            break;
        }
        if (frame == f) {
            return true;
        }
        p = *end == ',' ? end + 1 : end;
    }
    return false;
}

bool gxWanted(unsigned frame)
{
    if (gxWantedAfterFree(frame) || gxWantedAt(frame)) {
        return true;
    }
    static const unsigned kEarly[] = {60, 300, 600, 900, 1200, 1500, 1800, 2400};
    for (unsigned f : kEarly) {
        if (frame == f) {
            return true;
        }
    }
    if (frame % 2500 == 0) {
        return true;
    }
    if (gSnapFirst >= 0) {
        for (unsigned after : kSnapAfterStart) {
            if (frame == (unsigned) gSnapFirst + after) {
                return true;
            }
        }
    }
    return false;
}

void gxDumpHook(unsigned frame)
{
    if (gGxAttached) {
        if (const Re4GxChunk* c = re4_core_gx_acquire()) {
            if (c->flags & RE4_GX_CHUNK_RESET) {
                char path[1100];
                snprintf(path, sizeof(path), "%s/gx-%06u.bin", gGxDir, frame);
                if (FILE* f = fopen(path, "wb")) {
                    bool ok = re4gxio::Write(f, *c, frame);
                    fclose(f);
                    if (ok) {
                        gGxCount++;
                        re4_log("core: GX frame %s (%u draws, %u vertices, %u texture bytes)", path,
                                (unsigned) c->draws, (unsigned) c->vertexCount, (unsigned) c->textureBytes);
                    }
                }
            }
            re4_core_gx_release(c);
            re4_core_gx_detach();
            gGxAttached = false;
        }
        return;
    }
    if (gGxCount < 24 && gxWanted(frame + 1)) {
        re4_core_gx_acquire();  // attaches; the chunk after the next display copy starts over
        gGxAttached = true;
    }
}

struct DumpInstall {
    DumpInstall()
    {
        const char* path = getenv("RE4_BRIDGE_DUMP");
        if (path && *path) {
            gDump = fopen(path, "w");
            if (gDump) {
                re4_add_frame_hook(dumpHook);
            }
        }
        const char* dir = getenv("RE4_BRIDGE_SNAPSHOT");
        if (dir && *dir) {
            snprintf(gSnapDir, sizeof(gSnapDir), "%s", dir);
            re4_add_frame_hook(snapshotHook);
        }
        const char* gx = getenv("RE4_GX_DUMP");
        if (gx && *gx) {
            snprintf(gGxDir, sizeof(gGxDir), "%s", gx);
            re4_add_frame_hook(gxDumpHook);
        }
    }
} gDumpInstall;

}  // namespace

extern "C" {

int re4_core_reserve(const char* dataDir, const char* moduleDir)
{
    if (gReserved) {
        return 1;
    }
    re4_set_data_dir(dataDir ? dataDir : "");
    re4_set_module_dir(moduleDir ? moduleDir : "");
    re4_window_report();
    if (!re4_mem1_init()) {
        setMessage("MEM1 (0x80000000..0x83000000) is taken in this process");
        pthread_mutex_lock(&gLock);
        gState = RE4_CORE_FAILED;
        pthread_mutex_unlock(&gLock);
        return 0;
    }
    re4_module_window_reserve();  // 32 bit on Android: the room modules go above 0x80000000
    // The game tells its own data from offsets by bit 31 of the address
    // (re4_check_game_address aborts; here the app shows the reason instead).
    Dl_info info;
    const void* base = (const void*) &re4_game_main;
    if (dladdr(base, &info) && info.dli_fbase) {
        base = info.dli_fbase;
    }
    if ((unsigned long) base < RE4_MEM1_BASE) {
        char text[160];
        snprintf(text, sizeof(text), "libre4_main loaded at %p, below 0x80000000: unsupported", base);
        setMessage(text);
        pthread_mutex_lock(&gLock);
        gState = RE4_CORE_FAILED;
        pthread_mutex_unlock(&gLock);
        return 0;
    }
    gReserved = true;
    setMessage("memory reserved");
    return 1;
}

int re4_core_start(void)
{
    return re4_core_start_entry(re4_game_main);
}

int re4_core_start_entry(int (*entry)(void))
{
    gEntry = entry;
    pthread_mutex_lock(&gLock);
    bool ready = gReserved && gState == RE4_CORE_IDLE;
    if (ready) {
        gState = RE4_CORE_RUNNING;
    }
    pthread_mutex_unlock(&gLock);
    if (!ready) {
        return gState == RE4_CORE_RUNNING;
    }
    re4_add_frame_hook(frameHook);
    // the app sets the test switches (RE4_PAD_UNTIL_ROOM ...) after loading
    re4_state_trace_reconfigure();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
#if __SIZEOF_POINTER__ != 4
    // 64 bit (milestone 12): the game keeps addresses of its locals in 32-bit
    // words, so its thread's stack lies in the window below 4 GB like the
    // stacks of the game threads (on Android nothing else puts it there)
    if (void* stack = re4_window_map(8u << 20, PROT_READ | PROT_WRITE)) {
        pthread_attr_setstack(&attr, stack, 8u << 20);
    }
#endif
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int err = pthread_create(&thread, &attr, gameThread, 0);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        setMessage("cannot create the game thread");
        pthread_mutex_lock(&gLock);
        gState = RE4_CORE_FAILED;
        pthread_mutex_unlock(&gLock);
        return 0;
    }
    setMessage("running");
    return 1;
}

int re4_core_state(void)
{
    pthread_mutex_lock(&gLock);
    int s = gState;
    pthread_mutex_unlock(&gLock);
    return s;
}

const char* re4_core_message(void)
{
    // a static copy: the caller may read it while the game writes a new one
    static char copy[sizeof(gMessage)];
    pthread_mutex_lock(&gLock);
    memcpy(copy, gMessage, sizeof(copy));
    pthread_mutex_unlock(&gLock);
    return copy;
}

void re4_core_pad(const Re4CorePad* pad)
{
    re4_pad_live(pad);
}

unsigned re4_core_rumble(void)
{
    return re4_pad_rumble();
}

void re4_core_vsync(int64_t ns)
{
    re4_vi_vsync((long long) ns);
}

int re4_core_audio(int16_t* out, int frames)
{
    return re4_audio_read(out, frames);
}

int re4_core_audio4(int16_t* out, int frames)
{
    return re4_audio_read4(out, frames);
}

int re4_core_audio6(int16_t* out, int frames)
{
    return re4_audio_read6(out, frames);
}

int re4_core_audio_surround(void)
{
    return re4_audio_surround_now();
}

void re4_core_audio_stats(unsigned counters[5])
{
    re4_audio_stats(&counters[0], &counters[1], &counters[2], &counters[3], &counters[4]);
}

const Re4CoreSnapshot* re4_core_acquire(void)
{
    pthread_mutex_lock(&gLock);
    const Re4CoreSnapshot* s = 0;
    if (gReady >= 0 && gReading < 0) {
        gReading = gReady;
        s = &gBuffers[gReading];
    }
    pthread_mutex_unlock(&gLock);
    return s;
}

void re4_core_release(const Re4CoreSnapshot* snapshot)
{
    pthread_mutex_lock(&gLock);
    if (gReading >= 0 && snapshot == &gBuffers[gReading]) {
        gReading = -1;
    }
    pthread_mutex_unlock(&gLock);
}

int re4_core_log_tail(char* out, int size)
{
    return re4_log_tail(out, size);
}

unsigned re4_core_frame_count(void)
{
    pthread_mutex_lock(&gLock);
    unsigned n = gSerial;
    pthread_mutex_unlock(&gLock);
    return n;
}

int re4_core_game_thread(void)
{
    return gGameTid;
}

int re4_core_pause(int on, int waitMs)
{
    pthread_mutex_lock(&gPauseLock);
    gPauseWanted = on != 0;
    pthread_cond_broadcast(&gPauseCond);
    if (on && waitMs > 0 && !gPaused && re4_core_state() == RE4_CORE_RUNNING) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += waitMs / 1000;
        until.tv_nsec += (long) (waitMs % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        while (!gPaused && gPauseWanted) {
            if (pthread_cond_timedwait(&gPauseCond, &gPauseLock, &until) != 0) {
                break;
            }
        }
    }
    int paused = gPaused;
    pthread_mutex_unlock(&gPauseLock);
    return paused;
}

int re4_core_instant(int operation, Re4InstantInfo* info)
{
    if (!info || operation < RE4_INSTANT_STATUS || operation > RE4_INSTANT_LOAD) return RE4_INSTANT_BAD;
    memset(info, 0, sizeof(*info));
    pthread_mutex_lock(&gPauseLock);
    int attached;
    memcpy(&attached, __CARDBlock, sizeof(attached));
    if (!gPaused || !gPauseWanted || gInstantBusy || attached) {
        pthread_mutex_unlock(&gPauseLock);
        return RE4_INSTANT_BUSY;
    }
    gInstantBusy = true;
    gInstantDone = false;
    gInstantPending = operation;
    pthread_cond_broadcast(&gPauseCond);
    while (!gInstantDone) pthread_cond_wait(&gPauseCond, &gPauseLock);
    *info = gInstantInfo;
    const int result = gInstantResult;
    gInstantBusy = false;
    pthread_mutex_unlock(&gPauseLock);
    return result;
}

void re4_core_debug_menu(void)
{
    // the European discs: no debug menu (port/docs/retail-eu-plan.md, decision 2; R6)
    if (re4_disc_retail_eu()) {
        re4_log("core: debug menu asked for (SELECT held), none with the European discs");
        return;
    }
    gDebugMenuWanted = 1;
    re4_log("core: debug menu asked for (SELECT held)");
}

// The app's depth of field switch (M11, wish of the project owner,
// 17.10.2026; the first Android port had it): patch 0065 (src/game/
// filter01.cpp Filter01Trans) asks every frame
volatile int gDofOn = 1;

void re4_core_set_dof(int on)
{
    if (gDofOn != (on != 0)) {
        re4_log("core: depth of field %s", on ? "on" : "off");
    }
    gDofOn = on != 0;
}

int re4_dof_enabled(void)
{
    return gDofOn;
}

// The app's picture format "4:3 without bars" (Anbernic RG 477V report,
// 22.10.2026: a 4:3 screen showed the game's letterbox): patch 0105 asks every
// frame through re4_letterbox_open (runtime/picture.cpp: in play only; the
// bars and the scissor stay in events and sub screens).
volatile int gLetterboxOff = 0;

void re4_core_set_letterbox(int off)
{
    if (gLetterboxOff != (off != 0)) {
        re4_log("core: letterbox bars %s while playing", off ? "off" : "on");
    }
    gLetterboxOff = off != 0;
}

extern "C" void re4_gx_set_fast_cores(int on);

void re4_core_set_fast_cores(int on)
{
    static int told = -1;
    if (told != (on != 0)) {
        re4_log("core: Force Performance %s", on ? "on (use performance cores)" : "off (default: Android decides)");
    }
    told = on != 0;
    re4_gx_set_fast_cores(on);
}

void re4_core_gx_depth(const unsigned* cells, unsigned serial)
{
    re4_gx_depth_put(cells, serial);
}

void re4_core_gx_depth_source(const unsigned* cells, const unsigned* serial)
{
    re4_gx_depth_source(cells, serial);
}

int re4_letterbox_off(void)
{
    return gLetterboxOff;
}

// The language of the European discs (re4_core.h, runtime/game_language.cpp): the app's choice,
// at first from RE4_GAME_LANGUAGE (eng/ger/fra/esp/ita or 2..6); -1 none (English).
volatile int gLanguageWanted = -100;  // -100: not read yet

static int parseLanguage(const char* v)
{
    static const char* const names[] = {"eng", "ger", "fra", "esp", "ita"};
    if (!v || !*v) {
        return -1;
    }
    for (int i = 0; i < 5; i++) {
        if (strcasecmp(v, names[i]) == 0) {
            return i + 2;
        }
    }
    int n = atoi(v);
    return n >= 2 && n <= 6 ? n : -1;
}

void re4_game_restart(void);  // runtime/game_language.cpp

void re4_core_restart_game(int language)
{
    int value = language >= 2 && language <= 6 ? language : -1;
    re4_log("core: restart with game language %d asked by the app (European discs: 2 English .. 6 Italian)", value);
    gLanguageWanted = value;
    re4_game_restart();
}

int re4_core_language_wanted(void)
{
    if (gLanguageWanted == -100) {
        gLanguageWanted = parseLanguage(getenv("RE4_GAME_LANGUAGE"));
    }
    return gLanguageWanted;
}

// patch 0061 (src/game/game.cpp gameDebug), every game frame
int re4_debug_menu_request(void)
{
    if (!gDebugMenuWanted) {
        return 0;
    }
    gDebugMenuWanted = 0;
    return 1;
}

int re4_core_card_list(Re4CardFile* out, int max)
{
    return cardFree() ? re4_card_list(out, max) : RE4_CARD_BUSY;
}

int re4_core_card_free_blocks(void)
{
    return cardFree() ? re4_card_free_blocks() : RE4_CARD_BUSY;
}

int re4_core_card_file_name(int index, char* out, int size)
{
    return cardFree() ? re4_gci_file_name(index, out, size) : RE4_CARD_BUSY;
}

int re4_core_card_export(int index, unsigned char* out, unsigned capacity, char* note, int noteSize)
{
    if (!cardFree()) {
        snprintf(note, (size_t) noteSize, "the game uses the memory card");
        return RE4_CARD_BUSY;
    }
    return re4_gci_export(index, out, capacity, note, noteSize);
}

int re4_core_card_import(const unsigned char* gci, unsigned size, int replace, char* note, int noteSize)
{
    if (!cardFree()) {
        snprintf(note, (size_t) noteSize, "the game uses the memory card");
        return RE4_CARD_BUSY;
    }
    return re4_gci_import(gci, size, replace, note, noteSize);
}

}  // extern "C"
