// File-format / transaction tests, without disc data. Only the game-thread capture/restore
// boundary is substituted; production disk I/O and the original GCI field converter are used.
#include "re4_instant.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string root;
bool ready = true, titleReady, failCapture;
unsigned commits, restores;
std::vector<unsigned char> source(0x12000), restored;
uint32_t crc(const unsigned char* p, size_t n)
{
    uint32_t c = 0;
    while (n--) {
        c ^= uint32_t(*p++) << 24;
        for (int i = 0; i < 8; ++i)
            c = c & 0x80000000u ? (c << 1) ^ 0x04C11DB7u : c << 1;
    }
    return c;
}
void put(unsigned offset, uint32_t value)
{
    memcpy(source.data() + offset, &value, 4);
}
std::vector<unsigned char> read(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
}
void write(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write((const char*) bytes.data(), bytes.size());
    assert(out.good());
}
}
extern "C" const char* re4_data_dir()
{
    return root.c_str();
}
extern "C" void re4_log(const char*, ...) {}
extern "C" int re4_instant_game_ready()
{
    return ready;
}
extern "C" int re4_instant_game_can_load()
{
    return ready || titleReady;
}
extern "C" unsigned re4_instant_game_chapter(unsigned n)
{
    return 101 + n;
}
extern "C" int re4_instant_game_capture(unsigned char* out)
{
    if (failCapture)
        return RE4_INSTANT_BAD;
    memcpy(out, source.data(), source.size());
    return RE4_INSTANT_OK;
}
extern "C" int re4_instant_game_restore(const unsigned char* in)
{
    restored.assign(in, in + source.size());
    ++restores;
    return RE4_INSTANT_OK;
}
extern "C" void re4_instant_game_saved()
{
    ++commits;
}
// The converter shares a translation unit with the card APIs. The instant path must never call them.
extern "C" int re4_card_read(int, unsigned char*, unsigned char*, unsigned)
{
    abort();
}
extern "C" int re4_card_write(const unsigned char*, const unsigned char*, int)
{
    abort();
}
extern "C" int re4_card_free_blocks()
{
    abort();
}

int main(int argc, char** argv)
{
    Re4InstantInfo info = {};
    if (argc == 3 && std::string(argv[1]) == "--reload") {
        root = argv[2];
        assert(re4_instant_perform(RE4_INSTANT_STATUS, &info) == RE4_INSTANT_OK);
        assert(info.present && info.canLoad && info.room == 0x100 && info.playTime == 12345);
        assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == RE4_INSTANT_OK && restores == 1);
        return 0;
    }
    char dir[] = "/tmp/re4-instant-test-XXXXXX";
    assert(mkdtemp(dir));
    root = dir;
    const std::string slot = root + "/instant/slot-0.ce4save";
    // Valid original global, inventory, room, subscreen, merchant and system blocks. Values in
    // each part expose missing copies and incorrect byte conversion, including negative floats.
    put(0x2004, 0x116);
    put(0x2030, 1);
    put(0x2048, 12345);
    source[0x2050] = 0;
    source[0x2051] = 1;
    put(0x204C, 0x12345678);
    float x = -1340.5f;
    memcpy(source.data() + 0x2074, &x, 4);
    source[0x203E] = 2;
    source[0x572C] = 0x23;
    source[0x572D] = 0x80;
    put(0x6930, 0x10 + 0xD8);
    put(0x6934, 1);
    source[0x6941] = 1;
    put(0xE7D0, 0x01020304);
    source[0xE7D4] = 0x35;
    put(0x11E44, 0x12345678);
    put(0x2000, crc(source.data() + 0x2004, 0x1FC));
    put(0xEAF8, crc(source.data(), 0xEAF8));
    put(0x11E78, crc(source.data() + 0x10000, 0x1E78));
    assert(re4_instant_perform(RE4_INSTANT_STATUS, &info) == 0 && !info.present && info.canSave &&
           !info.canLoad);
    assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == RE4_INSTANT_EMPTY && !restores);
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == 0 && commits == 1);
    assert(info.present && info.canLoad && info.room == 0x100 && info.chapter == 103 &&
           info.playTime == 12345 && info.savedAt);
    const auto good = read(slot);
    assert(good.size() == 32 + source.size() && good[32 + 0x2004] == 0 && good[32 + 0x2005] == 0 &&
           good[32 + 0x2006] == 1 && good[32 + 0x2007] == 0x16);
    assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == 0 && restored == source);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        const char* runner = getenv("RE4_INSTANT_TEST_RUNNER");
        if (runner && *runner)
            execl(runner, runner, argv[0], "--reload", dir, (char*) nullptr);
        else
            execl(argv[0], argv[0], "--reload", dir, (char*) nullptr);
        _exit(127);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    failCapture = true;
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == RE4_INSTANT_BAD && read(slot) == good &&
           commits == 1);
    failCapture = false;
    ready = false;
    assert(re4_instant_perform(RE4_INSTANT_STATUS, &info) == 0 && info.present && !info.canLoad &&
           !info.canSave);
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == RE4_INSTANT_BUSY && read(slot) == good);
    assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == RE4_INSTANT_BUSY);
    titleReady = true;
    assert(re4_instant_perform(RE4_INSTANT_STATUS, &info) == 0 && info.canLoad && !info.canSave);
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == RE4_INSTANT_BUSY && read(slot) == good);
    assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == 0 && restored == source);
    titleReady = false;
    ready = true;
    assert(mkdir((slot + ".tmp").c_str(), 0775) == 0); // deterministic write failure, even as root
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == RE4_INSTANT_IO && read(slot) == good &&
           commits == 1);
    rmdir((slot + ".tmp").c_str());
    auto bad = good;
    bad[1000] ^= 0x80;
    write(slot, bad);
    assert(re4_instant_perform(RE4_INSTANT_LOAD, &info) == RE4_INSTANT_BAD && restores == 2);
    bad = good;
    bad.resize(31);
    write(slot, bad);
    assert(re4_instant_perform(RE4_INSTANT_STATUS, &info) == RE4_INSTANT_BAD && !info.canLoad);
    assert(re4_instant_perform(RE4_INSTANT_SAVE, &info) == 0 && commits == 2); // replaces corrupt slot
    // The instant backend creates no memory card directory or bh4_dataNN file.
    struct stat st;
    assert(stat((root + "/memcard").c_str(), &st) < 0);
    unlink(slot.c_str());
    rmdir((root + "/instant").c_str());
    rmdir(root.c_str());
    puts("instant-store-test: persistence, all payloads, byte order, corruption, busy and failed writes OK");
}
