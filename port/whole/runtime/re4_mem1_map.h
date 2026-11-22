/* The port's map of the GameCube main memory ("MEM1", 0x80000000..0x83000000, 48 MB like a development console;
 * mem1.cpp reserves it at that address). Numbers only, so that the game-flag files (cheats.cpp, state_trace.cpp)
 * and the runtime (re4_runtime.h) share them.
 *
 * Since 1.1.0 (the project owner, 20.11.2026; the idea from a fork's "heap 4 across the board", re-made here): the
 * game's heaps end at RE4_MEM1_HEAP_END on ILP32 and LP64 alike (patch 0245 SystemMemInit's Heap_end_addr) instead of
 * 0x817F4000 (LP64 0x818F4000). The last heap is the room heap 4, where a room's enemy modules load: it had 14.5 MB
 * (LP64 15.5 MB) and was full in many rooms, more so with "More enemies" and "Random enemies" (the game then loads
 * through ARAM or leaves an enemy kind out). It now has 15 MB more (LP64 14 MB). The game's own pointer checks accept
 * 0x80000000..0x82FFFFFF (VALID_PTR, patch 0037), so all of it stays below 0x83000000. The port's own places, which
 * lay in the development half before (0x81C00000..0x824E0400), move above the heaps:
 *
 *   0x80300000 .. 0x826F4000  the game: the fixed map up to 0x80974000, then the heaps
 *   0x826F4000 .. 0x82700000  the disc's file system table (FST, at most 48 KB; dvd.cpp puts it at the top)
 *   0x82700000 .. 0x82C00000  the cheats' places (cheats.cpp: Leon's archive, Krauser's weapon buffer, Ada's grips,
 *                             the stand-ins' Ganado entries)
 *   0x82C00000 .. 0x82F00000  a larger etc/core.das (the European release, patch 0117)
 *   0x82F00000 .. 0x83000000  a larger SS/<lang>/option.dat (the European discs, patch 0121)
 */
#ifndef RE4_MEM1_MAP_H
#define RE4_MEM1_MAP_H

#define RE4_MEM1_HEAP_END 0x826F4000u
#define RE4_MEM1_FST_ADDR RE4_MEM1_HEAP_END
#define RE4_MEM1_FST_MAX 0x0000C000u
#define RE4_MEM1_CHEAT_ADDR 0x82700000u
#define RE4_MEM1_CHEAT_MAX 0x00500000u
#define RE4_MEM1_CORE_ALT_ADDR 0x82C00000u
#define RE4_MEM1_CORE_ALT_MAX 0x00300000u
#define RE4_MEM1_OPTION_ALT_ADDR 0x82F00000u
#define RE4_MEM1_OPTION_ALT_MAX 0x00100000u
#define RE4_MEM1_END 0x83000000u

/* no overlaps, all below the end of MEM1 */
#if RE4_MEM1_FST_ADDR + RE4_MEM1_FST_MAX > RE4_MEM1_CHEAT_ADDR
#error "MEM1 map: the FST runs into the cheats' places"
#endif
#if RE4_MEM1_CHEAT_ADDR + RE4_MEM1_CHEAT_MAX > RE4_MEM1_CORE_ALT_ADDR
#error "MEM1 map: the cheats' places run into the core's second place"
#endif
#if RE4_MEM1_CORE_ALT_ADDR + RE4_MEM1_CORE_ALT_MAX > RE4_MEM1_OPTION_ALT_ADDR
#error "MEM1 map: the core's second place runs into the option data's"
#endif
#if RE4_MEM1_OPTION_ALT_ADDR + RE4_MEM1_OPTION_ALT_MAX > RE4_MEM1_END
#error "MEM1 map: the option data's second place runs past the end of MEM1"
#endif

#endif
