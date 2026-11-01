// yz2Decode_Decode, the PPC assembly half of Capcom's yz2 decompressor
// (src/game/yz2asm.cpp). The game's own C half (yz2code.cpp: Yz2DecodeSet,
// Yz2DecodeExec) runs unchanged and builds the models in its work buffer; the
// decode loop is the reviewed native translation port/src/yz2_kernel.cpp,
// compiled unchanged. This adapter maps the game's input state onto the
// kernel's, which carries bounds and statistics in addition.
#include "re4_runtime.h"
#include "yz2_internal.h"

#include <exception>
#include <stdlib.h>

namespace detail = re4::port::detail;

namespace {

// The game's Yz2InEv (yz2code.cpp): the first five members of the kernel's.
struct GameInEv {
    u8* src;
    u8* heap;
    u8* free;
    u32 size0;
    u32 size1;
};

static_assert(offsetof(detail::Yz2InEv, size1) == offsetof(GameInEv, size1),
              "kernel and game share the original Yz2InEv prefix");

}  // namespace

extern "C" void yz2Decode_Decode(void* ctx, void* dst, u32 size, void* in)
{
    GameInEv* game = static_cast<GameInEv*>(in);
    re4::port::Yz2Statistics stats;
    detail::Yz2InEv ev{};
    ev.src = game->src;
    ev.heap = game->heap;
    ev.free = game->free;
    ev.size0 = game->size0;
    ev.size1 = game->size1;
    // The game does not pass the stream length (size0 is the packed size the
    // header claims); the stream lies in MEM1 or in host memory, never past the
    // end of the address space.
    ev.srcEnd = reinterpret_cast<const u8*>(~uintptr_t{0});
    ev.workEnd = game->heap + detail::yz2WorkBytes;
    ev.statistics = &stats;
    const long long t0 = re4_formats_now_ns();
    try {
        detail::yz2Decode_Decode(ctx, dst, size, &ev);
    } catch (const std::exception& e) {
        re4_log("yz2: corrupt stream at %p (%u bytes wanted): %s", (void*) game->src, (unsigned) size,
                e.what());
        abort();
    }
    game->src = const_cast<u8*>(ev.src);
    game->free = ev.free;
    re4_formats_timed("yz2 stream", size, re4_formats_now_ns() - t0, "unpack");
    re4_formats_unpacked(dst, size);
}
