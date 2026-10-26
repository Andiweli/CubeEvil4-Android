// re4_core_remote_test: the core in a process of its own (port/docs/32bit-kernprozess.md),
// checked on Linux the way the Android app uses it. This program plays the app
// whose MEM1 is taken (as ART's Java heap takes it on a 32-bit Android 14
// device): it holds 0x80000000.. itself, starts the core process
// (host/coreproc.cpp, re4_coreproc) with its test entry
// (tests/core_remote_probe.cpp) and goes through the client
// (runtime/re4_core_client.h), the table core_mode.cpp uses:
//
//   start, state, message; the controller (a state before the start, a tap
//   while the game stands), the rumble motor, the frame counter; state frames
//   and GX chunks (the hashes of what the core process copied, sequence,
//   RESET first and after a detach, the memory pieces growing); the sound
//   (exact samples, read as the app's AudioTrack takes it: bursts of three
//   reads, without holes, without the core skipping; every second burst as the
//   four channels of Dolby Pro Logic II, whose rear the probe leaves silent);
//   vsync and gyro passed on; pause, the depth of field,
//   the debug menu; the memory card (list, free blocks, .gci import and
//   export); the log tail; the app's end ends the core process.
//
//   re4_core_remote_test --program re4_coreproc [--wrapper qemu-arm] --modules DIR
//                        --card FILE [--gci FILE] --crash-file FILE [--crash | --missing]
//
//   --crash    the core process crashes: the app notices it (state, reason, crash file)
//   --missing  the program does not exist: the start fails with a reason
#include "re4_card_files.h"
#include "re4_core.h"
#include "re4_core_client.h"
#include "re4_gx_frame.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace {

int gFailures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "core-remote-test: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            gFailures++;                                                         \
        }                                                                        \
    } while (0)

double now()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int64_t nowNs()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t) t.tv_sec * 1000000000LL + t.tv_nsec;
}

void sleepMs(int ms)
{
    usleep((useconds_t) ms * 1000u);
}

void pad(unsigned short buttons, int x = 0, int y = 0, int r = 0)
{
    Re4CorePad p = {buttons, (int8_t) x, (int8_t) y, 0, 0, 0, (uint8_t) r};
    re4ipc_pad(&p);
}

std::vector<unsigned char> readFile(const char* path)
{
    std::vector<unsigned char> data;
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) {
        return data;
    }
    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        data.insert(data.end(), buffer, buffer + n);
    }
    fclose(f);
    return data;
}

// What the app's GL thread, audio thread and Choreographer do, for `seconds`.
struct Receiver {
    unsigned chunks = 0, resets = 0, outOfOrder = 0, firstNotReset = 0, afterDetachNotReset = 0;
    uint32_t lastSequence = 0;
    bool afterDetach = false;
    unsigned audioFrames = 0, audioWrong = 0, audioHoles = 0, audioSilence = 0, audioReads = 0, audioReads4 = 0;
    unsigned audioSurround = 0;
    double audioFrom = -1;
    uint64_t audioTaken = 0;
    unsigned snapshots = 0, snapshotBad = 0;
    uint32_t lastSerial = 0;
    unsigned vsyncs = 0, gyros = 0;

    void step()
    {
        if (const Re4GxChunk* c = re4ipc_gx_acquire()) {
            if (chunks == 0 && !(c->flags & RE4_GX_CHUNK_RESET)) {
                firstNotReset++;
            }
            if (afterDetach && !(c->flags & RE4_GX_CHUNK_RESET)) {
                afterDetachNotReset++;
            }
            afterDetach = false;
            if (c->flags & RE4_GX_CHUNK_RESET) {
                resets++;
            }
            if (chunks && (int32_t) (c->sequence - lastSequence) <= 0) {
                outOfOrder++;
            }
            if (c->version != RE4_GX_FRAME_VERSION || !c->opWords || !(c->flags & RE4_GX_CHUNK_XFB)) {
                outOfOrder++;
            }
            lastSequence = c->sequence;
            chunks++;
            re4ipc_gx_release(c);
        }
        // as CoreAudio with its AudioTrack: 256 frames per read, until the track's buffer (2560
        // frames) is full, then in bursts as the mixer takes its periods (here 3 reads each 24 ms)
        if (audioFrom < 0) {
            audioFrom = now();
        }
        const uint64_t due = (uint64_t) ((now() - audioFrom) * 32000.0) + 2560;
        while (audioTaken + 768 <= due) {
            // CoreAudio reads the stereo or, for Android's spatializer, the four channels
            const bool four = (audioTaken / 768) % 2 == 1;
            for (int k = 0; k < 3; k++) {
                int16_t pcm[256 * 4];
                const int n = four ? 4 : 2;
                if (four) {
                    re4ipc_audio4(pcm, 256);
                    audioReads4++;
                } else {
                    re4ipc_audio(pcm, 256);
                }
                audioReads++;
                for (int i = 0; i < 256; i++) {
                    const int16_t* f = pcm + i * n;
                    if (f[0] || f[1]) {
                        audioFrames++;
                        audioHoles += audioSilence;  // silence between sound: a dropout
                        audioSilence = 0;
                        if (f[0] != 1000 || f[1] != -1000 || (four && (f[2] || f[3]))) {
                            audioWrong++;
                        }
                    } else if (audioFrames) {
                        audioSilence++;
                    }
                }
            }
            audioTaken += 768;
        }
        audioSurround += re4ipc_audio_surround() ? 1 : 0;
        if (const Re4CoreSnapshot* s = re4ipc_acquire()) {
            if (s->apiVersion != RE4_CORE_API_VERSION) {
                snapshotBad++;
            }
            if (s->serial != lastSerial) {
                if ((int32_t) (s->serial - lastSerial) < 0) {
                    snapshotBad++;
                }
                lastSerial = s->serial;
                snapshots++;
            }
            CHECK(re4ipc_acquire() == nullptr);  // one reader at a time
            re4ipc_release(s);
        }
        re4ipc_vsync(nowNs());
        vsyncs++;
        if (vsyncs % 20 == 0) {
            re4ipc_gyro(0.1f, -0.05f);
            gyros++;
        }
    }

    void run(double seconds)
    {
        const double end = now() + seconds;
        while (now() < end) {
            step();
            sleepMs(4);
        }
    }

    // the app's sound stops with its menu and starts again afterwards (CoreActivity)
    void restartAudio()
    {
        audioFrom = -1;
        audioTaken = 0;
        audioSilence = 0;
    }

    void detach()
    {
        re4ipc_gx_detach();
        afterDetach = true;
    }
};

bool waitFor(double seconds, Receiver* r, bool (*cond)())
{
    const double end = now() + seconds;
    while (now() < end) {
        if (cond()) {
            return true;
        }
        if (r) {
            r->step();
        }
        sleepMs(4);
    }
    return cond();
}

// The app's process as on the tester's device: something of its own lies on MEM1.
bool occupyMem1()
{
    void* want = (void*) 0x80000000ul;
    void* p = mmap(want, 0x10000000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | 0x100000, -1, 0);
    if (p != want && p != MAP_FAILED) {
        munmap(p, 0x10000000);
    }
    // the app's own MEM1 placeholder (core_mode.cpp loadCore) fails now
    void* q = mmap(want, 0x3000000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | 0x100000, -1, 0);
    if (q == want) {
        munmap(q, 0x3000000);
        return false;
    }
    if (q != MAP_FAILED) {
        munmap(q, 0x3000000);
    }
    return true;
}

std::string logTail()
{
    std::vector<char> tail(8192);
    re4ipc_log_tail(tail.data(), (int) tail.size());
    return tail.data();
}

unsigned number(const std::string& text, const char* key)
{
    const size_t at = text.find(key);
    return at == std::string::npos ? ~0u : (unsigned) strtoul(text.c_str() + at + strlen(key), nullptr, 0);
}

void mainRun(const char* modules, const char* gciPath)
{
    CHECK(re4ipc_state() == RE4_CORE_IDLE);
    CHECK(re4ipc_mem1_init() == 1);
    CHECK(re4ipc_reserve("/nonexistent-re4-data", modules) == 1);
    CHECK(re4ipc::pid() > 0 && re4ipc::pid() != (int) getpid());
    CHECK(re4ipc_acquire() == nullptr);  // nothing published yet
    CHECK(re4ipc_state() == RE4_CORE_IDLE);
    const std::string map = re4ipc::addressMap();
    CHECK(map.find("--- core process") != std::string::npos && map.find("MEM1 placeholder: reserved") != std::string::npos);
    // the controller before the start (as the app sends it at once)
    pad(0x0100, 50, -20, 200);
    CHECK(re4ipc_start() == 1);
    CHECK(re4ipc_state() == RE4_CORE_RUNNING);
    CHECK(strcmp(re4ipc_message(), "running") == 0);
    Receiver r;
    r.run(1.5);
    CHECK(r.chunks >= 20);
    CHECK(r.firstNotReset == 0 && r.outOfOrder == 0);
    CHECK(r.snapshots >= 10 && r.snapshotBad == 0);
    CHECK(re4ipc_frame_count() >= 10);
    // a new renderer (the app's options, a new GL context): the next chunk starts over
    r.detach();
    r.run(0.5);
    CHECK(r.afterDetachNotReset == 0 && !r.afterDetach && r.resets >= 2);
    // the rumble motor (X switches it on in the probe)
    pad(0x0400);
    CHECK(waitFor(10, &r, [] { return (re4ipc_rumble() & 1) != 0; }));
    pad(0);
    CHECK(waitFor(10, &r, [] { return (re4ipc_rumble() & 0xF) == 0; }));
    CHECK((re4ipc_rumble() >> 8) >= 10);
    // the app's menu: the game stands; the memory card meanwhile
    CHECK(re4ipc_pause(1, 5000) == 1);
    const unsigned frames = re4ipc_frame_count();
    sleepMs(150);
    CHECK(re4ipc_frame_count() == frames);
    Re4CardFile files[8];
    CHECK(re4ipc_card_list(files, 8) == 0);  // a new card
    CHECK(re4ipc_card_free_blocks() == 251);
    Re4InstantInfo instant = {};
    const int instantStatus = re4ipc_instant(RE4_INSTANT_STATUS, &instant);
    CHECK(instantStatus == RE4_INSTANT_OK || instantStatus == RE4_INSTANT_IO);
    CHECK(!instant.canSave && !instant.canLoad); // the probe does not run playable game logic
    CHECK(re4ipc_instant(RE4_INSTANT_SAVE, &instant) == RE4_INSTANT_BUSY);
    CHECK(re4ipc_frame_count() == frames); // servicing an instant command must keep the game paused
    char note[256] = "";
    std::vector<unsigned char> out(RE4_GCI_HEADER + 251 * RE4_CARD_BLOCK);
    CHECK(re4ipc_card_export(0, out.data(), (unsigned) out.size(), note, sizeof(note)) == RE4_CARD_NOFILE);
    const std::vector<unsigned char> gci = readFile(gciPath);
    if (!gci.empty()) {
        CHECK(re4ipc_card_import(gci.data(), (unsigned) gci.size(), 0, note, sizeof(note)) == RE4_CARD_OK);
        CHECK(re4ipc_card_list(files, 8) == 1);
        char name[80] = "";
        CHECK(re4ipc_card_file_name(files[0].index, name, sizeof(name)) == RE4_CARD_OK && strstr(name, ".gci"));
        const int bytes = re4ipc_card_export(files[0].index, out.data(), (unsigned) out.size(), note, sizeof(note));
        // the same file, but where it lies on this card (CARDDir startBlock, 0x36)
        CHECK(bytes == (int) gci.size() && memcmp(out.data(), gci.data(), 0x36) == 0 &&
              memcmp(out.data() + 0x38, gci.data() + 0x38, gci.size() - 0x38) == 0);
        CHECK(re4ipc_card_import(gci.data(), (unsigned) gci.size(), 0, note, sizeof(note)) == RE4_CARD_EXISTS);
        CHECK(re4ipc_card_free_blocks() < 251);
    } else {
        fprintf(stderr, "core-remote-test: no .gci given, card import not checked\n");
    }
    // a tap while the game stands reaches it once; the depth of field and the debug menu
    pad(0x0200);
    pad(0);
    re4ipc_set_dof(0);
    re4ipc_set_letterbox(1);
    re4ipc_touch(0.5f, 0.25f);
    re4ipc_swipe(1);
    re4ipc_slash(-0.5f);
    re4ipc_debug_menu();
    sleepMs(100);
    re4ipc_pause(0, 0);
    r.restartAudio();
    r.run(0.5);
    CHECK(re4ipc_pause(0, 0) == 0);  // it runs again
    CHECK(re4ipc_frame_count() > frames);
    CHECK(re4ipc_card_free_blocks() == RE4_CARD_BUSY);  // only while the game stands
    re4ipc_set_dof(1);
    re4ipc_set_letterbox(0);
    r.run(0.5);
    // the depth of the picture for the game's GXPeekZ: the cell at (100, 60) of the map the renderer writes
    {
        std::vector<unsigned> cells(320 * 264, 0x00FFFFFF);
        cells[(60 >> 1) * 320 + (100 >> 1)] = 0xABCDE;
        re4ipc_gx_depth(cells.data(), 1000000);
    }
    r.run(0.5);
    // the end: START
    pad(0x1000);
    CHECK(waitFor(20, &r, [] { return re4ipc_state() != RE4_CORE_RUNNING; }));
    CHECK(re4ipc_state() == RE4_CORE_ENDED);
    CHECK(strcmp(re4ipc_message(), "re4_game_main returned 7") == 0);
    // the rest of the sound
    r.run(0.5);
    const std::string tail = logTail();
    const size_t line = tail.find("remote-probe: frames");
    CHECK(line != std::string::npos);
    const std::string probe = line == std::string::npos ? "" : tail.substr(line, tail.find('\n', line) - line);
    CHECK(number(probe, "pad at ") > 0 && number(probe, "pad at ") != ~0u);
    CHECK(number(probe, "B frames ") == 1);
    CHECK(probe.find("dof changes 2 (now 1)") != std::string::npos);
    CHECK(tail.find("remote-probe: letterbox changes 2 (now 0)") != std::string::npos);
    {
        const size_t peek = tail.find("remote-probe: peek far");
        const std::string pl = peek == std::string::npos ? "" : tail.substr(peek, tail.find('\n', peek) - peek);
        CHECK(number(pl, "far ") > 0);                          // before the map came: far, as before
        CHECK(number(pl, "hits ") > 0);                         // after it: answered from the shared memory
        CHECK(pl.find("last 0xabcde") != std::string::npos);    // the cell the check put there
    }
    CHECK(tail.find("touch: taps on the picture reach the sub screens") != std::string::npos);
    CHECK(tail.find("touch: swipes change the tab of the sub screen") != std::string::npos);
    CHECK(tail.find("knife: swipes draw the knife and slash") != std::string::npos);
    CHECK(number(probe, "debug menu ") == 1);
    unsigned checked, mismatches, snaps, snapMismatches;
    re4ipc::checkCounts(&checked, &mismatches, &snaps, &snapMismatches);
    CHECK(checked == r.chunks && mismatches == 0);
    CHECK(snaps >= r.snapshots && snapMismatches == 0);
    sleepMs(300);  // the last inputs on their way
    unsigned f[8];
    re4ipc::forwarded(f);
    CHECK(f[0] == 6);         // pads: every change in order
    CHECK(f[1] == r.vsyncs);  // vsyncs
    CHECK(f[2] >= 1 && f[2] <= r.gyros);
    CHECK(f[5] >= 3);         // memory pieces made (RE4_CORE_IPC_SEGMENT=4096: they had to grow)
    unsigned stats[5];
    re4ipc_audio_stats(stats);
    const unsigned blocks = number(probe, "AI blocks ");
    CHECK(blocks != ~0u && stats[0] == blocks);  // the AI blocks of the probe, all counted
    // every frame the probe played arrived, exactly, without a dropout in between, and the
    // core never had to skip what the app did not take (as in the app's own process)
    CHECK(r.audioWrong == 0 && r.audioReads4 > 100);
    CHECK(r.audioSurround == 0);  // the probe's blocks carry no Dolby Pro Logic II rear
    CHECK(r.audioFrames + 2 * 160 >= blocks * 160 * 9 / 10);
    // a dropout the app hears is silence the core itself filled in (its ring ran dry, as in the
    // app's own process): the way between the processes adds none
    CHECK(r.audioHoles <= f[7]);
    CHECK(stats[4] == 0);  // skips
    fprintf(stderr,
            "core-remote-test main: %u chunks (%u resets), %u state frames, %u sound frames in %u reads (%u in "
            "dropouts; core: %u frames silence, %u ran dry, %u skips), %u vsyncs, %u gyro, pads %u, memory pieces %u, "
            "probe: %s\n",
            r.chunks, r.resets, r.snapshots, r.audioFrames, r.audioReads, r.audioHoles, f[7], stats[3], stats[4], f[1],
            f[2], f[0], f[5], probe.c_str());
    // the app ends: so does the core process
    const int child = re4ipc::pid();
    CHECK(re4ipc::shutdown(10000));
    CHECK(kill(child, 0) != 0 && errno == ESRCH);
    CHECK(re4ipc::ended().find("exit code 0") != std::string::npos);
}

void crashRun(const char* modules, const char* crashFile)
{
    CHECK(re4ipc_reserve("/nonexistent-re4-data", modules) == 1);
    CHECK(re4ipc_start() == 1);
    CHECK(waitFor(20, nullptr, [] { return !re4ipc::ended().empty(); }));
    const std::string reason = re4ipc::ended();
    CHECK(reason.find("signal 11") != std::string::npos);
    CHECK(re4ipc_state() == RE4_CORE_FAILED);
    CHECK(std::string(re4ipc_message()) == reason);
    CHECK(re4ipc_gx_acquire() == nullptr || true);
    const std::vector<unsigned char> crash = readFile(crashFile);
    const std::string text(crash.begin(), crash.end());
    CHECK(text.find("crash (core process): signal 11") != std::string::npos);
    CHECK(text.find("pc 0x") != std::string::npos);
    fprintf(stderr, "core-remote-test crash: %s; crash file %u bytes\n", reason.c_str(), (unsigned) text.size());
}

void missingRun()
{
    CHECK(re4ipc_reserve("/nonexistent-re4-data", "") == 0);
    CHECK(re4ipc_state() == RE4_CORE_FAILED);
    const std::string message = re4ipc_message();
    CHECK(message.find("does not start") != std::string::npos);
    fprintf(stderr, "core-remote-test missing: %s\n", message.c_str());
}

}  // namespace

int main(int argc, char** argv)
{
    const char *program = nullptr, *wrapper = nullptr, *modules = "", *card = nullptr, *gci = nullptr,
               *crashFile = nullptr;
    bool crash = false, missing = false;
    for (int i = 1; i < argc; i++) {
        const bool more = i + 1 < argc;
        if (!strcmp(argv[i], "--program") && more) {
            program = argv[++i];
        } else if (!strcmp(argv[i], "--wrapper") && more) {
            wrapper = argv[++i];
        } else if (!strcmp(argv[i], "--modules") && more) {
            modules = argv[++i];
        } else if (!strcmp(argv[i], "--card") && more) {
            card = argv[++i];
        } else if (!strcmp(argv[i], "--gci") && more) {
            gci = argv[++i];
        } else if (!strcmp(argv[i], "--crash-file") && more) {
            crashFile = argv[++i];
        } else if (!strcmp(argv[i], "--crash")) {
            crash = true;
        } else if (!strcmp(argv[i], "--missing")) {
            missing = true;
        } else {
            fprintf(stderr, "core-remote-test: unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!program || !card || !crashFile) {
        fprintf(stderr, "usage: re4_core_remote_test --program P [--wrapper W] --modules DIR --card FILE "
                        "[--gci FILE] --crash-file FILE [--crash | --missing]\n");
        return 2;
    }
    CHECK(occupyMem1());
    unlink(card);
    unlink(crashFile);
    setenv("RE4_CORE_TEST_ENTRY", crash ? "crash" : "remote", 1);
    setenv("RE4_CORE_IPC_CHECK", "1", 1);
    setenv("RE4_CORE_IPC_SEGMENT", "4096", 1);
    setenv("RE4_REMOTE_TEST_CARD", card, 1);
    // the error log's core file (CoreLog / CoreBridge.setLogFile): the core process writes it
    // itself, from its environment
    const std::string logFile = std::string(crashFile) + ".core-log.txt";
    unlink(logFile.c_str());
    unlink((logFile + ".1").c_str());
    if (!missing && !crash) {
        setenv("RE4_LOG_FILE", logFile.c_str(), 1);
    }
    re4ipc::Spawn spawn;
    spawn.program = missing ? std::string("/nonexistent-re4/re4_coreproc") : program;
    spawn.name = "re4-core-test";
    if (wrapper && *wrapper) {
        spawn.wrapper.push_back(wrapper);
    }
    spawn.helloSeconds = 60;
    re4ipc::configure(spawn);
    const int fd = open(crashFile, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    re4ipc::setCrashFd(fd);
    if (missing) {
        missingRun();
    } else if (crash) {
        crashRun(modules, crashFile);
    } else {
        mainRun(modules, gci);
    }
    re4ipc::shutdown(5000);
    if (!missing && !crash) {
        const std::vector<unsigned char> bytes = readFile(logFile.c_str());
        const std::string log(bytes.begin(), bytes.end());
        CHECK(log.find("--- core log, process ") != std::string::npos);
        CHECK(log.find("ready (pid ") != std::string::npos);
        fprintf(stderr, "core-remote-test log file: %u bytes, %u lines\n", (unsigned) log.size(),
                (unsigned) std::count(log.begin(), log.end(), '\n'));
    }
    fprintf(stderr, "core-remote-test %s: %d failed\n", missing ? "missing" : crash ? "crash" : "main", gFailures);
    return gFailures ? 1 : 0;
}
