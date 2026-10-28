// Between the GX FIFO front end (gx_fifo.cpp) and the command decoder
// (gx_decode.cpp) of the emulated graphics processor. Port-internal.
#ifndef RE4_GX_INTERNAL_H
#define RE4_GX_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

namespace re4gx {

// gx_fifo.cpp -------------------------------------------------------------

// Game memory at a physical address as the graphics processor sees it: MEM1
// (0 .. RE4_MEM1_SIZE) or a window of port memory outside MEM1 (static data of
// the libraries, re4_gx_physical). NULL when `size` bytes from `physical` are
// not memory.
uint8_t* memory(uint32_t physical, uint32_t size);
// The same for a run of bytes: the host address of `physical` and in
// *available how many bytes from there on are memory (0 and NULL: none).
uint8_t* memorySpan(uint32_t physical, uint32_t* available);
// Counts the windows of port memory given out so far: what memorySpan answers for an address
// only changes when this does (the decoder keeps its array spans until then).
uint32_t aliasGeneration();

// The address word of a display list call: the SDK writes the list pointer as
// it is (GXCallDisplayList); on the GameCube the GP masks it to physical.
// Here it may also be a pointer to port memory outside MEM1.
uint8_t* listMemory(uint32_t address, uint32_t size);

// The PE raises its interrupts (delivered by re4_gx_deliver_interrupts); a
// token without interrupt (BP 0x47) only sets the token register.
void peFinish();
void peToken(uint16_t token, bool interrupt);

// gx_decode.cpp -----------------------------------------------------------

// Bytes of the command stream, in order (the CPU FIFO linked to the GP).
void feed(const uint8_t* bytes, size_t n);
// Works through what has arrived (a command cut off waits for its bytes).
void drain();
// Waits until the vertex jobs of the draws so far are converted (the core thread helps): before
// memory the GP read may change (a display list recorded over it).
void finishJobs();

}  // namespace re4gx

#endif
