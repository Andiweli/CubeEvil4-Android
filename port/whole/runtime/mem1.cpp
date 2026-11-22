// GameCube main RAM ("MEM1") at its original address.
//
// The game addresses memory the way the GameCube lays it out: SystemMemInit
// builds its heaps over the fixed map 0x80350000..0x817F4000 (in the port since
// 1.1.0 up to RE4_MEM1_HEAP_END, re4_mem1_map.h, patch 0245), read.cpp loads
// the core/player/weapon archives to fixed addresses, the OS clock is read from
// 0x800000F8 (OS_BUS_CLOCK), OSPhysicalToCached adds 0x80000000 and dozens of
// checks accept a pointer only inside 0x80000000..0x82FFFFFF. The port reserves
// that range at the same address (48 MB, the size of a development console) so
// all of this keeps working unchanged.
#include "re4_runtime.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// The GameCube's locked data cache (0xE0000000 there), see re4_ppc.h.
extern "C" {
__attribute__((aligned(32))) unsigned char re4_locked_cache[0x4000];
}

namespace {

bool gMapped;

void put32(unsigned addr, unsigned value)
{
    *(volatile unsigned*) (unsigned long) addr = value;
}

}  // namespace

extern "C" int re4_mem1_init(void)
{
    if (gMapped) {
        return 1;
    }
    void* want = (void*) (unsigned long) RE4_MEM1_BASE;
    // The Linux loader (host/loader.c) holds the range with an inaccessible
    // placeholder of its own; then MEM1 replaces it.
    int fixed = getenv("RE4_MEM1_PLACEHOLDER") ? MAP_FIXED : MAP_FIXED_NOREPLACE;
    void* p = mmap(want, RE4_MEM1_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | fixed, -1, 0);
    if (p != want) {
        // Old kernels ignore MAP_FIXED_NOREPLACE and treat the address as a hint.
        re4_log("MEM1: cannot reserve 0x%08x..0x%08x (got %p)", RE4_MEM1_BASE,
                RE4_MEM1_BASE + RE4_MEM1_SIZE, p);
        if (p != MAP_FAILED) {
            munmap(p, RE4_MEM1_SIZE);
        }
        return 0;
    }
    gMapped = true;

    // Low-memory globals (OSBootInfo and the OS globals page), as the IPL and
    // OSInit leave them. Big-endian on the GameCube; the port reads them
    // natively, so they are stored in host order.
    memcpy((void*) (unsigned long) RE4_MEM1_BASE, "G4BE08", 6);  // DVDDiskID game/company
    put32(RE4_MEM1_BASE + 0x20, 0x0D15EA5E);                     // magic: booted from disc
    put32(RE4_MEM1_BASE + 0x24, 1);                               // version
    put32(RE4_MEM1_BASE + 0x28, RE4_MEM1_RETAIL_SIZE);            // physical memory size
    put32(RE4_MEM1_BASE + 0x2C, RE4_CONSOLE_TYPE);                // console type
    put32(RE4_MEM1_BASE + 0x30, RE4_MEM1_ARENA_LO);               // arena lo
    put32(RE4_MEM1_BASE + 0x34, RE4_MEM1_FST_ADDR);               // arena hi
    put32(RE4_MEM1_BASE + 0x38, RE4_MEM1_FST_ADDR);               // FST location
    put32(RE4_MEM1_BASE + 0x3C, RE4_MEM1_FST_MAX);                // FST max length
    put32(RE4_MEM1_BASE + 0xF0, RE4_MEM1_RETAIL_SIZE);            // simulated memory size
    put32(RE4_MEM1_BASE + 0xF8, 162000000);                       // bus clock (OS_BUS_CLOCK)
    put32(RE4_MEM1_BASE + 0xFC, 486000000);                       // core clock
    re4_log("MEM1: 0x%08x..0x%08x reserved", RE4_MEM1_BASE, RE4_MEM1_BASE + RE4_MEM1_SIZE);
    return 1;
}

extern "C" void re4_check_game_address(const char* what, const void* addr)
{
    Dl_info info;
    const void* base = addr;
    if (dladdr(addr, &info) && info.dli_fbase) {
        base = info.dli_fbase;
    }
    if ((unsigned long) base < RE4_MEM1_BASE) {
        re4_log("%s loaded at %p, below 0x%08x: unsupported, the game tests bit 31 of its own addresses",
                what, base, RE4_MEM1_BASE);
        abort();
    }
    // 64 bit: the whole library inside the window (its last byte as well)
    re4_check_window(what, addr);
}

extern "C" int re4_in_mem1(const void* p)
{
    unsigned long a = (unsigned long) p;
    return a >= RE4_MEM1_BASE && a < (unsigned long) RE4_MEM1_BASE + RE4_MEM1_SIZE;
}
