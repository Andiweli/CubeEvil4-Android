/* The GX FIFO of the port (runtime/gx_fifo.cpp).
 *
 * On the GameCube every GX command goes through one write address, the
 * write-gather pipe at 0xCC008000 (GXWGFifo): the GX library and the game's
 * inline vertex functions store bytes, halfwords, words and floats there.
 * whole_prepare.py rewrites each such store (`GXWGFifo.T = x`,
 * `GXWGFifo->T = x`) into a call of re4_gx_put_T(x), in the game's gx.h, the
 * SDK's GXVert.h and __gx.h. The port appends the value, big-endian as the
 * hardware sees it, to the command stream of the emulated graphics processor
 * (or, while a display list is recorded, to the list in memory).
 *
 * The hardware register blocks the GX library reads and writes (CP, PE, PI,
 * MEM) are port memory (re4_gx_register_block, patch 0030 in GXInit.c). */
#ifndef RE4_GX_FIFO_H
#define RE4_GX_FIFO_H

#ifdef __cplusplus
extern "C" {
#endif

void re4_gx_put_u8(unsigned char v);
void re4_gx_put_u16(unsigned short v);
void re4_gx_put_u32(unsigned int v);
void re4_gx_put_s8(signed char v);
void re4_gx_put_s16(short v);
void re4_gx_put_s32(int v);
void re4_gx_put_f32(float v);
/* The owner of the draws that follow (patch 0137, re4_gx_frame.h RE4_GX_OP_OWNER). */
void re4_gx_put_owner(unsigned int object, unsigned int function, unsigned int table, unsigned int life);

/* The register block at physical address `paddr` (0x0C000000 CP, 0x0C001000
 * PE, 0x0C003000 PI, 0x0C004000 MEM). */
void* re4_gx_register_block(unsigned int paddr);

/* The physical address the graphics processor reads `pointer` at (patch
 * 0031: GXSetArray, GXInitTexObj, GXInitTexObjData, GXInitTlutObj): MEM1
 * pointers as on the GameCube, port memory outside MEM1 (static arrays of the
 * libraries, e.g. filter02.cpp CLUTdata) through a window above MEM1. */
unsigned int re4_gx_physical(const void* pointer);

/* The pointer to a physical address (MEM1 or the window above). */
void* re4_gx_cached(unsigned int physical);

/* A display list the game built with CPU stores (patch 0032: the sprite quad
 * in eff_sys.cpp, the water and sand grids in Espgen42/43/45): its counts,
 * indices and floats are in host order. The graphics processor reads it so
 * while its contents stay as registered. */
void re4_gx_host_order_list(const void* list, unsigned int size);

/* EFB access (GXPeekARGB, GXPeekZ, GXPoke*): the emulated frame buffer is
 * not readable from the game's side; a peek answers "nothing drawn" (colour
 * 0, depth far), a poke is ignored. */
unsigned int re4_gx_efb_peek(unsigned short x, unsigned short y, int z);

#ifdef __cplusplus
}
#endif

#endif
