// Disk I/O with host flags (no game prelude / Android fortify renaming). The payload uses the
// existing GCI field converter: fixed-width, checksummed GameCube byte order on disk, no pointers.
#include "re4_instant.h"
#include "re4_card_files.h"
#include "re4_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
const unsigned kHeader = 32, kPayload = 0x12000, kFileBytes = kHeader + kPayload;
// This TU belongs to libre4_main: use no C++ heap allocations or weak STL instantiations.
// They would bind to the game heap and could interpose the platform library's allocators.
unsigned char gFile[kFileBytes];
const unsigned char kMagic[8] = { 'C', 'E', '4', 'I', 'N', 'S', 'T', 0 };
uint32_t get(const unsigned char* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void put(unsigned char* p, uint32_t n)
{
    for (int i = 3; i >= 0; --i) {
        p[i] = n & 255;
        n >>= 8;
    }
}
uint32_t checksum(const unsigned char* p, size_t size)
{
    uint32_t c = 0;
    for (size_t i = 0; i < size; ++i) {
        c ^= uint32_t(p[i]) << 24;
        for (int b = 0; b < 8; ++b)
            c = c & 0x80000000u ? (c << 1) ^ 0x04C11DB7u : c << 1;
    }
    return c;
}
bool transfer(int fd, unsigned char* p, size_t bytes, bool writing)
{
    while (bytes) {
        const ssize_t n = writing ? write(fd, p, bytes) : read(fd, p, bytes);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        p += n;
        bytes -= size_t(n);
    }
    return true;
}
int convert(unsigned char* payload, int toGameCube)
{
    char note[256];
    if (re4_gci_convert("bh4_data00", payload, 0x10000, toGameCube, note, sizeof(note)) != RE4_CARD_OK ||
        re4_gci_convert("bh4_system", payload + 0x10000, 0x2000, toGameCube, note, sizeof(note)) !=
            RE4_CARD_OK)
        return RE4_INSTANT_BAD;
    return RE4_INSTANT_OK;
}
int readSlot(const char* path, unsigned char* file, Re4InstantInfo* info)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? RE4_INSTANT_EMPTY : RE4_INSTANT_IO;
    struct stat st;
    bool ok = fstat(fd, &st) == 0 && st.st_size == kHeader + kPayload;
    if (ok) {
        ok = transfer(fd, file, kFileBytes, false);
    }
    close(fd);
    if (!ok)
        return RE4_INSTANT_BAD;
    unsigned char* p = file;
    if (memcmp(p, kMagic, 8) || get(p + 8) != 1 || get(p + 12) != kPayload ||
        get(p + 28) != checksum(p, 28) || get(p + 20) != checksum(p + kHeader, kPayload))
        return RE4_INSTANT_BAD;
    // Validate both original payloads, even for a status query; damaged data is never loadable.
    if (convert(p + kHeader, 0) != RE4_INSTANT_OK)
        return RE4_INSTANT_BAD;
    const unsigned char* data = p + kHeader;
    uint16_t room;
    uint32_t play;
    memcpy(&room, data + 0x2050, 2);
    memcpy(&play, data + 0x2048, 4);
    info->present = 1;
    info->room = room;
    info->chapter = re4_instant_game_chapter(data[0x203E]);
    info->playTime = play;
    info->savedAt = get(p + 16);
    return RE4_INSTANT_OK;
}
}

extern "C" int re4_instant_perform(int operation, Re4InstantInfo* info)
{
    if (!info)
        return RE4_INSTANT_BAD;
    memset(info, 0, sizeof(*info));
    char directory[1024], path[1100], temporary[1120];
    if (snprintf(directory, sizeof(directory), "%s/instant", re4_data_dir()) >= (int) sizeof(directory))
        return RE4_INSTANT_IO;
    snprintf(path, sizeof(path), "%s/slot-0.ce4save", directory);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    unsigned char* file = gFile;
    const int previous = readSlot(path, file, info);
    const bool ready = re4_instant_game_ready() != 0;
    info->canSave = ready;
    info->reserved = !ready && re4_instant_game_save_blocked() ? 1 : 0;
    info->canLoad = re4_instant_game_can_load() && previous == RE4_INSTANT_OK;
    if (operation == RE4_INSTANT_STATUS)
        return previous == RE4_INSTANT_EMPTY ? RE4_INSTANT_OK : previous;
    if (operation == RE4_INSTANT_LOAD) {
        if (!re4_instant_game_can_load())
            return RE4_INSTANT_BUSY;
        if (previous != RE4_INSTANT_OK)
            return previous;
        return re4_instant_game_restore(file + kHeader);
    }
    if (operation != RE4_INSTANT_SAVE)
        return RE4_INSTANT_BAD;
    if (!ready)
        return RE4_INSTANT_BUSY;
    memset(file, 0, kFileBytes);
    unsigned char* p = file;
    const int capture = re4_instant_game_capture(p + kHeader);
    if (capture != RE4_INSTANT_OK)
        return capture;
    if (convert(p + kHeader, 1) != RE4_INSTANT_OK)
        return RE4_INSTANT_BAD;
    memcpy(p, kMagic, 8);
    put(p + 8, 1);
    put(p + 12, kPayload);
    put(p + 16, (uint32_t) time(nullptr));
    put(p + 20, checksum(p + kHeader, kPayload));
    put(p + 28, checksum(p, 28));
    if (mkdir(directory, 0775) < 0 && errno != EEXIST)
        return RE4_INSTANT_IO;
    // A single core-thread writer. A failed or interrupted write leaves the old slot intact.
    const int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0664);
    if (fd < 0)
        return RE4_INSTANT_IO;
    bool ok = transfer(fd, p, kFileBytes, true) && fsync(fd) == 0;
    if (close(fd) != 0)
        ok = false;
    if (!ok || rename(temporary, path) != 0) {
        unlink(temporary);
        return RE4_INSTANT_IO;
    }
    const int dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dirfd >= 0) {
        fsync(dirfd);
        close(dirfd);
    }
    re4_instant_game_saved();
    const int result = readSlot(path, file, info);
    info->canSave = ready;
    info->canLoad = result == RE4_INSTANT_OK;
    return result;
}
