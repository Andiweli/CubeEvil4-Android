// Compiled with the game's flags and headers (re4_game_flags) for the runtime
// test "gx": the SDK's GX library (src/lib/GX*.c, patch 0030) and the game's
// inline vertex functions (gx.h, rewritten by whole_prepare.py) feed the GX
// FIFO of the port (runtime/gx_fifo.cpp). Checks that the bytes arrive as the
// GameCube's graphics processor would read them: register encodings, vertex
// sizes (a command stream that lost its framing shows up as unknown commands),
// display lists recorded into memory and called, the PE interrupts.
#include "types.h"
#include "gx.h"

#include <string.h>

extern "C" {
void* GXInit(void* base, u32 size);
void GXBeginDisplayList(void* list, u32 size);
u32 GXEndDisplayList(void);
void GXSetDrawSync(u16 token);
typedef void (*GXDrawSyncCallback)(u16 token);
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb);
void GXFlush(void);
void re4_gx_stats(unsigned out[11]);
unsigned re4_gx_bp(unsigned reg);
unsigned re4_gx_xf(unsigned address);
void re4_gx_deliver_interrupts(void);
}

namespace {

enum { kBytes, kCommands, kBp, kCp, kXf, kXfIndexed, kDraws, kVertices, kCalls, kUnknown, kDlBytes };

u16 gSyncToken;
int gSyncCalls;

void onSync(u16 token)
{
    gSyncToken = token;
    gSyncCalls++;
}

void stats(unsigned out[11])
{
    re4_gx_stats(out);
}

}  // namespace

// `fifo` and `list`: 32-byte aligned MEM1 memory (0x10000 and 0x400 bytes).
// Returns a bit mask of failed checks (0 = passed).
extern "C" int re4probe_gx_test(void* fifo, void* list)
{
    int failed = 0;
    unsigned s0[11], s1[11];
    // the FIFO object GXInit uses: 32-byte aligned (patch 0038; the SDK stores
    // words into it, which fault on ARM at an odd address)
    if (((unsigned long) GXInit(fifo, 0x10000) & 31) != 0) {
        failed |= 0x100;
    }
    stats(s0);
    if (s0[kBp] < 30 || s0[kXf] == 0 || s0[kUnknown] != 0) {
        failed |= 0x1;  // GXInit programs every register
    }
    // BP 0x41 (cmode0): blend on, source SRCALPHA (4) bits 8-10, destination
    // INVSRCALPHA (5) bits 5-7
    GXSetBlendMode(1, 4, 5, 0);
    if ((re4_gx_bp(0x41) & 0xFE3) != (1 | 5 << 5 | 4 << 8)) {
        failed |= 0x2;
    }
    // a triangle of float positions (12 bytes a vertex)
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);                // GX_VA_POS, GX_DIRECT
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);    // XYZ, F32
    GXBegin(0x90, 0, 3);               // GX_TRIANGLES, GX_VTXFMT0
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(1.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, 1.0f, 0.0f);
    // a quad of s16 positions, RGBA8 colours and float texture coordinates (18 bytes)
    GXSetVtxDesc(11, 1);               // GX_VA_CLR0
    GXSetVtxDesc(13, 1);               // GX_VA_TEX0
    GXSetVtxAttrFmt(1, 9, 1, 3, 0);    // XYZ, S16
    GXSetVtxAttrFmt(1, 11, 1, 5, 0);   // RGBA, RGBA8
    GXSetVtxAttrFmt(1, 13, 1, 4, 0);   // ST, F32
    GXBegin(0x80, 1, 4);               // GX_QUADS, GX_VTXFMT1
    for (int i = 0; i < 4; i++) {
        GXPosition3s16((s16) i, (s16) -i, 7);
        GXColor4u8(255, 128, 0, 255);
        GXTexCoord2f32(0.5f, 0.25f);
    }
    GXSetVtxDesc(11, 0);
    GXSetVtxDesc(13, 0);
    GXFlush();
    stats(s1);
    if (s1[kDraws] - s0[kDraws] != 2 || s1[kVertices] - s0[kVertices] != 7 || s1[kUnknown] != 0) {
        failed |= 0x4;
    }
    // a display list: recorded into memory, not drawn; then called
    memset(list, 0xEE, 0x400);
    GXBeginDisplayList(list, 0x400);
    GXBegin(0x90, 0, 3);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(2.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, 2.0f, 0.0f);
    u32 size = GXEndDisplayList();
    unsigned s2[11];
    stats(s2);
    const u8* dl = (const u8*) list;
    // the list may start with the vertex format the GXBegin flushed (CP writes)
    const u8* draw = (const u8*) memchr(dl, 0x90, size ? size : 1);
    if (size == 0 || size % 32 != 0 || size > 0x400 || draw == 0 || draw[1] != 0 || draw[2] != 3 ||
        s2[kDraws] != s1[kDraws]) {
        failed |= 0x8;
    }
    GXCallDisplayList(list, size);
    GXFlush();
    unsigned s3[11];
    stats(s3);
    if (s3[kCalls] - s2[kCalls] != 1 || s3[kDraws] - s2[kDraws] != 1 || s3[kVertices] - s2[kVertices] != 3 ||
        s3[kUnknown] != 0) {
        failed |= 0x10;
    }
    // PE interrupts: the draw-sync token reaches the callback, GXDrawDone returns
    gSyncCalls = 0;
    GXSetDrawSyncCallback(onSync);
    GXSetDrawSync(0xADEB);
    re4_gx_deliver_interrupts();
    if (gSyncCalls != 1 || gSyncToken != 0xADEB) {
        failed |= 0x20;
    }
    GXDrawDone();  // would wait for ever without the finish interrupt
    GXSetDrawSyncCallback(0);
    // colours reach the registers with R in the high byte, whatever the host's
    // byte order (patch 0033): TEV colour, konstant colour, material colour,
    // light colour, fog colour
    {
        GXColor c = {10, 20, 30, 40};
        GXSetTevColor(1, c);  // GX_TEVREG0: BP 0xE2 (r, a), 0xE3 (b, g)
        bool tev = (re4_gx_bp(0xE2) & 0x7FF) == 10 && ((re4_gx_bp(0xE2) >> 12) & 0x7FF) == 40 &&
                   (re4_gx_bp(0xE3) & 0x7FF) == 30 && ((re4_gx_bp(0xE3) >> 12) & 0x7FF) == 20;
        GXSetTevKColor(2, c);  // GX_KCOLOR2: BP 0xE4/0xE5 with the konst bit
        bool konst = (re4_gx_bp(0xE4) & 0x7FF) == 10 && ((re4_gx_bp(0xE4) >> 12) & 0x7FF) == 40 &&
                     (re4_gx_bp(0xE5) & 0x7FF) == 30 && ((re4_gx_bp(0xE5) >> 12) & 0x7FF) == 20;
        GXSetChanMatColor(4, c);  // GX_COLOR0A0: XF 0x100C
        bool mat = re4_gx_xf(0x100C) == 0x0A141E28u;
        GXLightObj light;
        memset(&light, 0, sizeof(light));
        GXInitLightColor(&light, c);
        GXLoadLightObjImm(&light, 1);  // GX_LIGHT0: XF 0x600, colour word at +3
        bool lit = re4_gx_xf(0x603) == 0x0A141E28u;
        GXSetFog(2, 0.0f, 1.0f, 0.1f, 1.0f, c);  // fog colour: BP 0xF2 = r g b
        bool fog = (re4_gx_bp(0xF2) & 0xFFFFFF) == 0x0A141Eu;
        if (!tev || !konst || !mat || !lit || !fog) {
            failed |= 0x40;
        }
    }
    // GXSetTevOp builds its register words from tables (patch 0036: words as
    // the GameCube holds them, not bit-fields in the host's order)
    {
        GXSetTevOp(0, 0);  // GX_TEVSTAGE0, GX_MODULATE: BP 0xC0: a=ZERO b=TEXC c=RASC d=ZERO, clamp
        bool c0 = (re4_gx_bp(0xC0) & 0xFFFFFF) == 0x08F8AFu;
        bool a0 = (re4_gx_bp(0xC1) & 0xFFFFF0) == 0x08F2F0u;  // a=ZERO b=TEXA c=RASA d=ZERO
        GXSetTevOp(1, 3);  // GX_TEVSTAGE1, GX_REPLACE: BP 0xC2: d=TEXC; 0xC3: d=TEXA
        bool c1 = (re4_gx_bp(0xC2) & 0xFFFFFF) == 0x08FFF8u;
        bool a1 = (re4_gx_bp(0xC3) & 0xFFFFF0) == 0x08FFC0u;
        if (!c0 || !a0 || !c1 || !a1) {
            failed |= 0x80;
        }
    }
    // GXSetCullMode swaps front and back for the hardware (patch 0042: the
    // __rlwimi of the swap was dropped, GX_CULL_FRONT came out as no culling);
    // the gen mode register (BP 0x00, bits 14-15) goes out with the next draw
    {
        static const int modes[4][2] = {{1, 2}, {2, 1}, {0, 0}, {3, 3}};  // GX mode, hardware field
        GXClearVtxDesc();
        GXSetVtxDesc(9, 1);
        GXSetVtxAttrFmt(0, 9, 1, 4, 0);
        for (int i = 0; i < 4; i++) {
            GXSetCullMode(modes[i][0]);
            GXBegin(0x90, 0, 3);
            GXPosition3f32(0.0f, 0.0f, 0.0f);
            GXPosition3f32(1.0f, 0.0f, 0.0f);
            GXPosition3f32(0.0f, 1.0f, 0.0f);
            if (((re4_gx_bp(0x00) >> 14) & 3) != (unsigned) modes[i][1]) {
                failed |= 0x200;
            }
        }
        GXSetCullMode(0);
    }
    return failed;
}

// --- G2: the decoder and the chunks for the renderer ----------------------------

#define RE4_GX_FRAME_IO_WRITE_ONLY  // game flags: no STL here
#include "../runtime/re4_gx_frame_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

extern "C" {
void GXLoadPosMtxIndx(u16 index, u32 id);
void re4_gx_put_u8(unsigned char v);
void re4_gx_put_u16(unsigned short v);
unsigned re4_gx_xf(unsigned address);
unsigned int re4_gx_physical(const void* pointer);
void re4_gx_host_order_list(const void* list, unsigned int size);
void VISetNextFrameBuffer(void* fb);
void VIFlush(void);
void VISetBlack(int black);
void re4_dof_autofocus_mark(void* w);
}

namespace {

// What a chunk holds, in op order.
extern "C" void re4_log(const char* format, ...);

struct Parsed {
    const Re4GxDraw* draws[16];
    int drawCount;
    const Re4GxTexture* textures[16];
    int textureBefore[16];  // index of the draw the texture op precedes
    int textureCount;
    const uint32_t* tluts[8];
    int tlutCount;
    const uint32_t* xf[64];  // payloads: address, words
    uint32_t xfWords[64];
    int xfCount;
    bool bpStateFirst;
    int unknown;
    const uint32_t* owner;  // the first owner tag (patch 0137) and the draw it precedes
    int ownerBefore;
};

void parse(const Re4GxChunk* c, Parsed& p)
{
    memset(&p, 0, sizeof(p));
    uint32_t at = 0;
    bool first = true;
    while (at < c->opWords) {
        uint32_t head = c->ops[at];
        uint32_t op = head & 0xFF;
        uint32_t n = head >> 8;
        const uint32_t* payload = c->ops + at + 1;
        if (first) {
            p.bpStateFirst = op == RE4_GX_OP_BP_STATE && n == 0x100;
            first = false;
        }
        if (op == RE4_GX_OP_DRAW && p.drawCount < 16) {
            p.draws[p.drawCount++] = (const Re4GxDraw*) payload;
        } else if (op == RE4_GX_OP_TEXTURE && p.textureCount < 16) {
            p.textureBefore[p.textureCount] = p.drawCount;
            p.textures[p.textureCount++] = (const Re4GxTexture*) payload;
        } else if (op == RE4_GX_OP_TLUT && p.tlutCount < 8) {
            p.tluts[p.tlutCount++] = payload;
        } else if (op == RE4_GX_OP_XF && p.xfCount < 64) {
            p.xfWords[p.xfCount] = n - 1;
            p.xf[p.xfCount++] = payload;
        } else if (op == RE4_GX_OP_OWNER && n == 4) {
            if (!p.owner) {
                p.owner = payload;
                p.ownerBefore = p.drawCount;
            }
        } else if (op < RE4_GX_OP_BP || op > RE4_GX_OP_BP_STATE) {
            p.unknown++;
        }
        at += 1 + n;
    }
}

uint32_t bits(f32 f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

const f32* vertexFloats(const Re4GxChunk* c, const Re4GxDraw* d, uint32_t vertex, uint32_t offset)
{
    return (const f32*) (c->vertices + d->vertexOffset + vertex * d->stride + offset);
}

bool near(f32 a, f32 b)
{
    return a - b < 0.0001f && b - a < 0.0001f;
}

// 256 entries: a TLUT load reads the whole region of its TLUT name (GXLoadTlut writes the
// region's loadTlut1, GX_TLUT_256 for names 0-15), not the object's entries, on the GameCube as in
// gx_decode.cpp tlutLoad; with 16 entries the load read 480 bytes past the array (the sanitizer
// build, 22.10.2026). Only the first 16 are set and checked.
u16 sStaticPalette[256] __attribute__((aligned(32)));

// A frame's end as the game's Render_done / Render_swap: the display copy,
// then the VI switched to it (unless the game holds the picture, as with
// SYS_SCREEN_STOP) and VIFlush.
void copyDisp(void* xfb, bool shown = true)
{
    GXSetDispCopySrc(0, 0, 640, 448);
    GXSetDispCopyDst(640, 448);
    GXCopyDisp(xfb, 1);
    if (shown) {
        VISetNextFrameBuffer(xfb);
    }
    VIFlush();
}

void triangle()
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);
    GXBegin(0x90, 0, 3);
    GXPosition3f32(0.0f, 0.0f, 0.0f);
    GXPosition3f32(1.0f, 0.0f, 0.0f);
    GXPosition3f32(0.0f, 1.0f, 0.0f);
}

}  // namespace

// `mem`: 64 KB of 32-byte aligned MEM1 scratch memory, `fifo`: 64 KB for the
// GX FIFO. A synthetic frame through the SDK's GX library; returns a bit mask
// of failed checks (0 = passed).
extern "C" int re4probe_gx_frame_test(void* fifo, void* mem)
{
    int failed = 0;
    u8* m = (u8*) mem;
    s16* posArr = (s16*) (m + 0x000);
    u8* clrArr = m + 0x040;
    f32* mtxArr = (f32*) (m + 0x080);
    u8* texRgb = m + 0x100;
    u8* texC4 = m + 0x120;
    u8* palette = m + 0x140;
    u8* hostList = m + 0x200;
    u8* copyDest = m + 0x1000;
    u8* xfb = m + 0x2000;
    memset(m, 0, 0x3000);

    GXInit(fifo, 0x10000);
    if (re4_core_gx_acquire() != 0) {  // attaches; nothing built yet
        failed |= 0x1;
    }
    copyDisp(xfb);  // the chunk starts at this frame boundary
    if (re4_core_gx_acquire() != 0) {
        failed |= 0x1;
    }

    // a position matrix, loaded directly
    f32 mtx[3][4] = {{1, 0, 0, 5}, {0, 1, 0, 6}, {0, 0, 1, 7}};
    GXLoadPosMtxImm(mtx, 0);

    // a textured quad: s16 positions (index8, host order, 2 fraction bits),
    // RGBA8 colours (index16), float texture coordinates (direct)
    for (int i = 0; i < 4; i++) {
        posArr[i * 3 + 0] = (s16) (4 * i);
        posArr[i * 3 + 1] = (s16) (-8 * i);
        posArr[i * 3 + 2] = 12;
        clrArr[i * 4 + 0] = (u8) (10 + i);
        clrArr[i * 4 + 1] = 20;
        clrArr[i * 4 + 2] = 30;
        clrArr[i * 4 + 3] = 255;
    }
    for (int i = 0; i < 32; i++) {
        texRgb[i] = (u8) (i * 7 + 1);
    }
    GXTexObj tex;
    GXInitTexObj(&tex, texRgb, 4, 4, 5, 0, 0, 0);  // RGB5A3, clamp
    GXLoadTexObj(&tex, 0);
    GXSetNumTevStages(1);
    GXSetTevOrder(0, 0, 0, 4);  // stage 0: coord 0, map 0, colour 0
    GXClearVtxDesc();
    GXSetVtxDesc(9, 2);   // position index8
    GXSetVtxDesc(11, 3);  // colour 0 index16
    GXSetVtxDesc(13, 1);  // texture coordinate 0 direct
    GXSetVtxAttrFmt(1, 9, 1, 3, 2);   // XYZ s16, 2 fraction bits
    GXSetVtxAttrFmt(1, 11, 1, 5, 0);  // RGBA8
    GXSetVtxAttrFmt(1, 13, 1, 4, 0);  // ST f32
    GXSetArray(9, posArr, 6);
    GXSetArray(11, clrArr, 4);
    GXBegin(0x80, 1, 4);
    for (int i = 0; i < 4; i++) {
        re4_gx_put_u8((u8) i);
        re4_gx_put_u16((u16) (3 - i));
        GXTexCoord2f32(0.25f * i, 0.5f);
    }

    // the owner of the draws that follow (ExecOt, patch 0137): before the strip
    re4_gx_put_owner(0x80123450u, 0x81000010u, 7, 0x00050003u);

    // a triangle strip with the same texture: no new texture op
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);
    GXBegin(0x98, 0, 5);
    for (int i = 0; i < 5; i++) {
        GXPosition3f32((f32) i, (f32) (i & 1), 0.0f);
    }

    // a display list the CPU built in host order (like eff_sys.cpp EspInit)
    {
        memset(hostList, 0, 64);
        hostList[0] = 0x90;
        u16 count = 3;
        memcpy(hostList + 1, &count, 2);
        const f32 v[9] = {1.5f, 2.5f, 3.5f, -1.0f, 0.0f, 2.0f, 8.0f, 9.0f, 10.0f};
        memcpy(hostList + 3, v, sizeof(v));
        re4_gx_host_order_list(hostList, 64);
        GXCallDisplayList(hostList, 64);
    }

    // a position matrix loaded by index from a host-order array
    for (int k = 0; k < 24; k++) {
        mtxArr[k] = 0.5f * k;
    }
    GXSetArray(21, mtxArr, 48);  // GX_POS_MTX_ARRAY
    GXLoadPosMtxIndx(1, 3);      // matrix 1 to PNMTX1 (XF 12)

    // a palette and a C4 texture using it
    for (int i = 0; i < 32; i++) {
        palette[i] = (u8) (0x80 + i);
        texC4[i] = (u8) (i * 3);
    }
    GXTlutObj tlut;
    GXInitTlutObj(&tlut, palette, 2, 16);  // RGB5A3, 16 entries
    GXLoadTlut(&tlut, 0);
    GXTexObj ci;
    GXInitTexObjCI(&ci, texC4, 8, 8, 8, 0, 0, 0, 0);
    GXLoadTexObj(&ci, 0);
    triangle();

    // a palette in port memory outside MEM1
    for (int i = 0; i < 16; i++) {
        sStaticPalette[i] = (u16) (0x1234 + i);
    }
    GXTlutObj tlut2;
    GXInitTlutObj(&tlut2, sStaticPalette, 2, 16);
    GXLoadTlut(&tlut2, 1);

    // an EFB copy to a texture, then bound
    GXSetTexCopySrc(0, 0, 8, 8);
    GXSetTexCopyDst(8, 8, 6, 0);
    GXCopyTex(copyDest, 0);
    GXTexObj copied;
    GXInitTexObj(&copied, copyDest, 8, 8, 6, 0, 0, 0);
    GXLoadTexObj(&copied, 0);
    triangle();

    copyDisp(xfb);
    const Re4GxChunk* c1 = re4_core_gx_acquire();
    if (!c1 || c1->version != RE4_GX_FRAME_VERSION || c1->flags != (RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB) ||
        c1->xfbAddress != re4_gx_physical(xfb) || c1->xfbWidth != 640 || c1->xfbHeight != 448) {
        return failed | 0x2;
    }
    if (const char* dump = getenv("RE4_GX_TEST_DUMP")) {  // the renderer's host tools can draw it
        if (FILE* f = fopen(dump, "wb")) {
            re4gxio::Write(f, *c1, 0);
            fclose(f);
        }
    }
    Parsed p;
    parse(c1, p);
    // five GXBegins, four draws: the display list joins the strip before it
    if (!p.bpStateFirst || p.unknown != 0 || p.drawCount != 4) {
        failed |= 0x2;
    }
    // the owner tag in the chunk, in its place: after the quad, before the strip
    if (!p.owner || p.ownerBefore != 1 || p.owner[0] != 0x80123450u || p.owner[1] != 0x81000010u || p.owner[2] != 7 ||
        p.owner[3] != 0x00050003u) {
        failed |= 0x2;
        re4_log("  gx: the owner tag is missing or out of place (before draw %d)", p.owner ? p.ownerBefore : -1);
    }
    // the matrix
    bool matrix = false;
    for (int i = 0; i < p.xfCount; i++) {
        if (p.xf[i][0] == 0 && p.xfWords[i] == 12 && p.xf[i][1 + 3] == bits(5.0f) && p.xf[i][1 + 7] == bits(6.0f) &&
            p.xf[i][1 + 11] == bits(7.0f) && p.xf[i][1] == bits(1.0f)) {
            matrix = true;
        }
    }
    if (!matrix) {
        failed |= 0x4;
    }
    // the quad
    if (p.drawCount >= 1) {
        const Re4GxDraw* d = p.draws[0];
        bool ok = (d->primitive & 0xFF) == RE4_GX_TRIANGLES && ((d->primitive >> 8) & 0xF8) == 0x80 &&
                  ((d->primitive >> 16) & 7) == 1 && d->layout == (RE4_GX_V_COLOR0 | RE4_GX_V_TEX0) &&
                  d->stride == 24 && d->vertexCount == 4 && d->indexCount == 6;
        static const u16 kQuad[6] = {0, 1, 2, 0, 2, 3};
        for (int i = 0; ok && i < 6; i++) {
            ok = c1->indices[d->indexOffset + i] == kQuad[i];
        }
        for (uint32_t v = 0; ok && v < 4; v++) {
            const f32* pos = vertexFloats(c1, d, v, 0);
            const u8* rgba = c1->vertices + d->vertexOffset + v * d->stride + 12;
            const f32* st = vertexFloats(c1, d, v, 16);
            ok = near(pos[0], (f32) v) && near(pos[1], -2.0f * v) && near(pos[2], 3.0f) && rgba[0] == 13 - v &&
                 rgba[1] == 20 && rgba[3] == 255 && near(st[0], 0.25f * v) && near(st[1], 0.5f);
        }
        if (!ok) {
            failed |= 0x8;
        }
    }
    // the RGB5A3 texture: once, before the quad, with its texels
    {
        bool ok = p.textureCount >= 1 && p.textureBefore[0] == 0;
        const Re4GxTexture* t = ok ? p.textures[0] : 0;
        ok = ok && t->map == 0 && t->format == 5 && t->width == 4 && t->height == 4 && t->levels == 1 &&
             t->dataBytes == 32 && t->flags == 0 && t->address == re4_gx_physical(texRgb) &&
             memcmp(c1->blob + t->dataOffset, texRgb, 32) == 0;
        // the strip and the list draw with it bound: no texture op until the C4 one
        ok = ok && p.textureCount >= 2 && p.textureBefore[1] == 2;
        if (!ok) {
            failed |= 0x10;
        }
    }
    // the strip: every second triangle flipped back; the host-order display
    // list (same layout, nothing between them) joined to it, its indices
    // counted on from the strip's five vertices
    if (p.drawCount >= 2) {
        const Re4GxDraw* d = p.draws[1];
        static const u16 kStrip[12] = {0, 1, 2, 2, 1, 3, 2, 3, 4, 5, 6, 7};
        bool ok = d->indexCount == 12 && d->layout == 0 && d->stride == 12 && d->vertexCount == 8 &&
                  ((d->primitive >> 8) & 0xF8) == 0x98;
        for (int i = 0; ok && i < 12; i++) {
            ok = c1->indices[d->indexOffset + i] == kStrip[i];
        }
        if (!ok) {
            failed |= 0x20;
        }
        const f32* a = vertexFloats(c1, d, 5, 0);
        const f32* c = vertexFloats(c1, d, 7, 0);
        if (!near(a[0], 1.5f) || !near(a[1], 2.5f) || !near(a[2], 3.5f) || !near(c[0], 8.0f) || !near(c[2], 10.0f)) {
            failed |= 0x40;
        }
    }
    // the indexed matrix load
    for (int k = 0; k < 12; k++) {
        if (re4_gx_xf(12 + k) != bits(0.5f * (12 + k))) {
            failed |= 0x80;
        }
    }
    // palettes: the MEM1 one and the one outside MEM1
    {
        bool mem1 = false, outside = false;
        for (int i = 0; i < p.tlutCount; i++) {
            const uint32_t* t = p.tluts[i];
            if (t[1] >= 32 && memcmp(c1->blob + t[2], palette, 32) == 0) {  // the region size is loaded
                mem1 = true;
            }
            if (t[1] >= 32 && memcmp(c1->blob + t[2], sStaticPalette, 32) == 0) {
                outside = true;
            }
        }
        const Re4GxTexture* t = p.textureCount >= 2 ? p.textures[1] : 0;
        if (!mem1 || !t || t->format != 8 || t->width != 8 || t->dataBytes != 32 || t->tlut == 0 ||
            (t->tlut >> 10) != 2) {
            failed |= 0x100;
        }
        if (!outside) {
            failed |= 0x400;
        }
    }
    // the EFB copy bound as a texture
    {
        const Re4GxTexture* t = p.textureCount >= 3 ? p.textures[2] : 0;
        if (!t || p.textureBefore[2] != 3 || !(t->flags & RE4_GX_TEX_COPY) || t->dataBytes != 0 ||
            t->address != re4_gx_physical(copyDest)) {
            failed |= 0x200;
        }
    }

    // the next frame: the RGB5A3 texture again, sent before (no data)
    GXLoadTexObj(&tex, 0);
    triangle();
    copyDisp(xfb);
    const Re4GxChunk* c2 = re4_core_gx_acquire();
    if (!c2 || c2->flags != RE4_GX_CHUNK_XFB || c2->sequence != c1->sequence + 1) {
        failed |= 0x800;
    } else {
        Parsed q;
        parse(c2, q);
        const Re4GxTexture* t = q.textureCount == 1 ? q.textures[0] : 0;
        const Re4GxTexture* t1 = p.textures[0];
        if (!t || t->dataBytes != 0 || t->keyLo != t1->keyLo || t->keyHi != t1->keyHi || q.drawCount != 1) {
            failed |= 0x800;
        }
    }
    re4_core_gx_release(c1);
    if (c2) {
        re4_core_gx_release(c2);
    }
    // a picture the VI does not switch to (Render_swap under SYS_SCREEN_STOP)
    // is run but held; the next one shown is not
    triangle();
    copyDisp(xfb + 0x100, false);
    const Re4GxChunk* c3 = re4_core_gx_acquire();
    if (!c3 || c3->flags != (RE4_GX_CHUNK_XFB | RE4_GX_CHUNK_HOLD)) {
        failed |= 0x2000;
    }
    if (c3) {
        re4_core_gx_release(c3);
    }
    triangle();
    copyDisp(xfb + 0x100);
    const Re4GxChunk* c4 = re4_core_gx_acquire();
    if (!c4 || c4->flags != RE4_GX_CHUNK_XFB) {
        failed |= 0x2000;
    }
    if (c4) {
        re4_core_gx_release(c4);
    }
    // VISetBlack (applied with VIFlush): the picture is run but shown black,
    // until the black is lifted
    VISetBlack(1);
    triangle();
    copyDisp(xfb);
    const Re4GxChunk* c5 = re4_core_gx_acquire();
    if (!c5 || c5->flags != (RE4_GX_CHUNK_XFB | RE4_GX_CHUNK_BLACK)) {
        failed |= 0x4000;
    }
    if (c5) {
        re4_core_gx_release(c5);
    }
    VISetBlack(0);
    triangle();
    copyDisp(xfb);
    const Re4GxChunk* c6 = re4_core_gx_acquire();
    if (!c6 || c6->flags != RE4_GX_CHUNK_XFB) {
        failed |= 0x4000;
    }
    if (c6) {
        re4_core_gx_release(c6);
    }
    // the marker of the app's gameplay autofocus (runtime/dof.cpp, the OT
    // callback of patch 0065): BP 0x5A, not a GameCube register, in the chunk
    // each time, also with the value it already has
    re4_dof_autofocus_mark(0);
    triangle();
    re4_dof_autofocus_mark(0);
    copyDisp(xfb);
    const Re4GxChunk* c7 = re4_core_gx_acquire();
    {
        int marks = 0;
        for (uint32_t at = 0; c7 && at < c7->opWords; at += 1 + (c7->ops[at] >> 8)) {
            marks += (c7->ops[at] & 0xFF) == RE4_GX_OP_BP && c7->ops[at + 1] == (0x5Au << 24 | 1);
        }
        if (marks != 2) {
            failed |= 0x8000;
        }
    }
    if (c7) {
        re4_core_gx_release(c7);
    }
    re4_core_gx_detach();
    copyDisp(xfb);  // no renderer: nothing is built
    if (re4_core_gx_acquire() != 0) {
        failed |= 0x1000;
    }
    re4_core_gx_detach();
    return failed;
}

// `frames` frames of one triangle each, ended by a display copy (the queue
// test of runtime_tests.cpp runs the renderer side in another thread).
extern "C" void re4probe_gx_produce(void* fifo, void* xfb, int frames, int init)
{
    if (init) {
        GXInit(fifo, 0x10000);
    }
    for (int i = 0; i < frames; i++) {
        triangle();
        copyDisp(xfb);
    }
}

// --- decoder fingerprint ----------------------------------------------------------
//
// A deterministic, randomised command stream through the decoder: every vertex
// format the GP knows (direct and indexed attributes, matrix indices, all
// component and colour formats, normals with binormal and tangent), every
// primitive, immediate and in display lists, between register writes, texture
// binds, palettes, EFB copies and indexed matrix loads. The hash of all chunks
// it makes stays the same as long as the decoder's output does: a rewrite of
// the decoder for speed is checked against the value the previous one gave.

extern "C" void re4_gx_put_u32(unsigned int v);
extern "C" void re4_log(const char* format, ...);
extern "C" void re4_gx_wait_jobs(void);

namespace {

struct Rng {
    uint32_t s;
    uint32_t next()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
    // a random byte that is never the exponent byte of a NaN or infinity, in
    // either byte order: 32-bit hosts pass floats through the x87 stack, which
    // turns a signalling NaN into a quiet one, and the hash would then depend
    // on how the decoder moves its floats around
    u8 byte()
    {
        u8 v = (u8) next();
        return (v & 0x7F) == 0x7F ? (u8) (v ^ 1) : v;
    }
    bool chance(uint32_t percent) { return below(100) < percent; }
};

struct Stream {
    u8* out;
    size_t n;
    void u8_(uint32_t v) { out[n++] = (u8) v; }
    void u16_(uint32_t v)
    {
        u8_(v >> 8);
        u8_(v);
    }
    void u32_(uint32_t v)
    {
        u16_(v >> 16);
        u16_(v);
    }
};

uint64_t fnv(uint64_t h, const void* data, size_t n)
{
    const u8* p = (const u8*) data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

const uint32_t kCompBytes[8] = {1, 1, 2, 2, 4, 4, 4, 4};
const uint32_t kColorBytes[8] = {2, 3, 4, 2, 3, 4, 4, 4};

// One random vertex format (VCD and VAT of format `vat`) and the arrays it
// reads, written as CP registers; the bytes of `count` vertices into `s`.
struct Fmt {
    uint32_t vcdLo, vcdHi, a, b, c;
    uint32_t elem[12];  // bytes of an array element (indexed attributes)
};

void cp(Stream& s, uint32_t reg, uint32_t value)
{
    s.u8_(0x08);
    s.u8_(reg);
    s.u32_(value);
}

void bp(Stream& s, uint32_t reg, uint32_t value)
{
    s.u8_(0x61);
    s.u32_(reg << 24 | (value & 0xFFFFFF));
}

Fmt randomFormat(Rng& r)
{
    Fmt f;
    memset(&f, 0, sizeof(f));
    const uint32_t mode[4] = {0, 1, 2, 3};
    f.vcdLo = r.chance(40) ? 1 : 0;  // PNMTXIDX
    for (int i = 0; i < 8; i++) {
        if (r.chance(10)) {
            f.vcdLo |= 1u << (1 + i);
        }
    }
    f.vcdLo |= (1 + r.below(3)) << 9;  // position: direct, index8, index16
    if (r.chance(60)) {
        f.vcdLo |= mode[1 + r.below(3)] << 11;
    }
    if (r.chance(60)) {
        f.vcdLo |= mode[1 + r.below(3)] << 13;
    }
    if (r.chance(20)) {
        f.vcdLo |= mode[1 + r.below(3)] << 15;
    }
    for (int i = 0; i < 8; i++) {
        if (r.chance(i == 0 ? 60 : 12)) {
            f.vcdHi |= mode[1 + r.below(3)] << (2 * i);
        }
    }
    const uint32_t nrmFormats[3] = {1, 3, 4};
    uint32_t posComps = r.below(2), posFmt = r.below(5), posShift = r.below(16);
    uint32_t nbt = r.chance(20), nrmFmt = nrmFormats[r.below(3)], nbt3 = nbt && r.chance(50);
    uint32_t c0c = r.below(2), c0f = r.below(6), c1c = r.below(2), c1f = r.below(6);
    f.a = posComps | posFmt << 1 | posShift << 4 | nbt << 9 | nrmFmt << 10 | c0c << 13 | c0f << 14 | c1c << 17 |
          c1f << 18 | nbt3 << 31;
    uint32_t tc[8], tf[8], ts[8];
    for (int i = 0; i < 8; i++) {
        tc[i] = r.below(2);
        tf[i] = r.below(5);
        ts[i] = r.below(16);
    }
    f.a |= tc[0] << 21 | tf[0] << 22 | ts[0] << 25;
    f.b = tc[1] | tf[1] << 1 | ts[1] << 4 | tc[2] << 9 | tf[2] << 10 | ts[2] << 13 | tc[3] << 18 | tf[3] << 19 |
          ts[3] << 22 | tc[4] << 27 | tf[4] << 28;
    f.c = ts[4] | tc[5] << 5 | tf[5] << 6 | ts[5] << 9 | tc[6] << 14 | tf[6] << 15 | ts[6] << 18 | tc[7] << 23 |
          tf[7] << 24 | ts[7] << 27;
    f.elem[0] = (posComps ? 3 : 2) * kCompBytes[posFmt];
    f.elem[1] = (nbt ? 9 : 3) * kCompBytes[nrmFmt];
    f.elem[2] = kColorBytes[c0f];
    f.elem[3] = kColorBytes[c1f];
    for (int i = 0; i < 8; i++) {
        f.elem[4 + i] = (tc[i] ? 2 : 1) * kCompBytes[tf[i]];
    }
    return f;
}

void attribute(Stream& s, Rng& r, uint32_t m, uint32_t direct, uint32_t entries)
{
    if (m == 1) {
        for (uint32_t i = 0; i < direct; i++) {
            s.u8_(r.byte());
        }
    } else if (m == 2) {
        s.u8_(r.chance(3) ? 0xFF : r.below(entries));  // 0xFF: GX skips such a vertex, the decoder reads it
    } else if (m == 3) {
        // beyond the array, but inside the scratch memory (its bytes are known)
        s.u16_(r.chance(3) ? entries + r.below(8000) : r.below(entries));
    }
}

void vertices(Stream& s, Rng& r, const Fmt& f, uint32_t count, uint32_t entries)
{
    uint32_t nbt = (f.a >> 9) & 1, nbt3 = nbt && (f.a >> 31);
    for (uint32_t v = 0; v < count; v++) {
        for (int i = 0; i < 9; i++) {
            if ((f.vcdLo >> i) & 1) {
                s.u8_(r.below(64) | (r.chance(5) ? 0xC0 : 0));
            }
        }
        attribute(s, r, (f.vcdLo >> 9) & 3, f.elem[0], entries);
        uint32_t nm = (f.vcdLo >> 11) & 3;
        if (nm >= 2 && nbt3) {
            for (int k = 0; k < 3; k++) {
                attribute(s, r, nm, 0, entries);
            }
        } else {
            attribute(s, r, nm, f.elem[1], entries);
        }
        attribute(s, r, (f.vcdLo >> 13) & 3, f.elem[2], entries);
        attribute(s, r, (f.vcdLo >> 15) & 3, f.elem[3], entries);
        for (int i = 0; i < 8; i++) {
            attribute(s, r, (f.vcdHi >> (2 * i)) & 3, f.elem[4 + i], entries);
        }
    }
}

}  // namespace

// `fifo`: 64 KB for the GX FIFO, `mem`: 1 MB of 32-byte aligned MEM1 scratch
// memory, `xfb`: a display copy target. Returns the hash of `frames` chunks.
extern "C" unsigned long long re4probe_gx_fingerprint(void* fifo, void* mem, void* xfb, int frames)
{
    static u8 bytes[0x40000];
    GXInit(fifo, 0x10000);
    {
        // GXInit points these at the SDK's default texture and palette in port
        // memory, whose physical window address follows the library's load
        // address (ASLR): fixed values, or the first chunk's state would differ
        // from run to run
        Stream s = {bytes, 0};
        bp(s, 0x64, 0);
        bp(s, 0x0F, 0);  // left by the tests before, which differ between the hosts
        bp(s, 0x5A, 0);  // the autofocus marker of the frame test
        for (uint32_t reg = 0x94; reg < 0x98; reg++) {
            bp(s, reg, 0);
            bp(s, reg + 0x20, 0);
        }
        for (size_t i = 0; i < s.n; i++) {
            re4_gx_put_u8(bytes[i]);
        }
    }
    re4_core_gx_acquire();  // attach
    copyDisp(xfb);
    if (const Re4GxChunk* c = re4_core_gx_acquire()) {
        re4_core_gx_release(c);
    }
    u8* m = (u8*) mem;
    const uint32_t phys = re4_gx_physical(mem);
    Rng r = {0x5EED1234u};
    // random data for the arrays, textures and palettes (0x00000-0x7FFFF)
    for (uint32_t i = 0; i < 0x80000; i++) {
        m[i] = r.byte();
    }
    // matrices for the indexed loads: host-order floats (array 12 at 0x7F000)
    for (uint32_t i = 0; i < 0x400; i++) {
        f32 v = (f32) ((int) r.below(2000) - 1000) / 64.0f;
        memcpy(m + 0x7F000 + 4 * i, &v, 4);
    }
    const uint32_t listArea = 0x80000;  // display lists: 0x80000-0xFFFFF
    const uint32_t entries = 48;
    uint64_t hash = 0xCBF29CE484222325ull;
    for (int frame = 0; frame < frames; frame++) {
        uint32_t listAt = listArea;
        int draws = 20 + (int) r.below(40);
        for (int d = 0; d < draws; d++) {
            Stream s = {bytes, 0};
            uint32_t vat = r.below(8);
            Fmt f = randomFormat(r);
            cp(s, 0x50, f.vcdLo);
            cp(s, 0x60, f.vcdHi);
            cp(s, 0x70 + vat, f.a);
            cp(s, 0x80 + vat, f.b);
            cp(s, 0x90 + vat, f.c);
            if (r.chance(30)) {
                cp(s, 0x30, r.next() & 0x3FFFFFFF);
                cp(s, 0x40, r.next() & 0xFFFFFF);
            }
            for (uint32_t k = 0; k < 12; k++) {
                uint32_t stride = f.elem[k] + r.below(5);
                uint32_t base = phys + r.below(0x70000 - entries * 40);
                if (r.chance(2)) {
                    // the last 8 bytes of MEM1 for every index: larger elements
                    // are not memory (beyond MEM1 begin the windows of port
                    // memory, which a stray index must not reach here)
                    base = 0x02FFFFF8;
                    stride = 0;
                }
                cp(s, 0xA0 + k, base);
                cp(s, 0xB0 + k, stride);
            }
            if (r.chance(15)) {  // indexed matrix load (array 12)
                cp(s, 0xAC, phys + 0x7F000);
                cp(s, 0xBC, 48);
                s.u8_(0x20);
                s.u32_(r.below(20) << 16 | 11u << 12 | (r.below(5) * 12));
            }
            if (r.chance(20)) {  // XF memory: a matrix row, or a register
                uint32_t count = 1 + r.below(4);
                s.u8_(0x10);
                s.u32_((count - 1) << 16 | (r.chance(70) ? r.below(0x60) : 0x1000 + r.below(0x58)));
                for (uint32_t i = 0; i < count; i++) {
                    s.u32_(r.next());
                }
            }
            if (r.chance(35)) {  // textures: two maps, TEV orders, genmode
                for (uint32_t map = 0; map < 2; map++) {
                    uint32_t w = 1 + r.below(32), h = 1 + r.below(32);
                    const uint32_t formats[11] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
                    uint32_t fmt = formats[r.below(11)];
                    bp(s, 0x80 + map, r.below(0x200));
                    bp(s, 0x84 + map, r.below(0x10000));
                    bp(s, 0x88 + map, (w - 1) | (h - 1) << 10 | fmt << 20);
                    bp(s, 0x94 + map, (phys + 32 * r.below(0x1800)) >> 5);
                    bp(s, 0x98 + map, r.below(0x400) | r.below(4) << 10);
                }
                bp(s, 0x00, r.below(2) << 10 | (r.chance(20) ? 1u << 16 : 0));
                bp(s, 0x28, r.below(2) | 1u << 6 | (r.below(2) | (r.chance(50) ? 1u << 6 : 0)) << 12);
                bp(s, 0x27, r.below(2));
            }
            if (r.chance(8)) {  // a palette load
                bp(s, 0x64, (phys + 32 * r.below(0x1000)) >> 5);
                bp(s, 0x65, r.below(0x400) | (1 + r.below(16)) << 10);
            }
            if (r.chance(5)) {  // an EFB copy to a texture
                bp(s, 0x49, 0);
                bp(s, 0x4A, 7 | 7u << 10);
                bp(s, 0x4B, (phys + 32 * r.below(0x1800)) >> 5);
                bp(s, 0x52, 0x0230);
            }
            if (r.chance(25)) {  // a state register between draws (no joining)
                bp(s, 0x41, r.below(0x1000));
            }
            const uint32_t prims[7] = {0x80, 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xB8};
            int runs = 1 + (int) r.below(4);
            bool list = r.chance(40);
            size_t listStart = s.n;
            for (int k = 0; k < runs; k++) {
                uint32_t count = 1 + r.below(r.chance(10) ? 300 : 40);
                s.u8_(prims[r.below(7)] | vat);
                s.u16_(count);
                vertices(s, r, f, count, entries);
            }
            if (list && listAt + (s.n - listStart) + 64 < 0x100000) {
                // the draws as a display list in memory, called from the FIFO. The indices beyond
                // an array reach up to here: a draw before may still read this memory (the vertex
                // jobs of gx_decode.cpp convert a little after the command, as the GameCube's GP
                // read); like a game after GXDrawDone, the test writes it once the GP is done
                re4_gx_wait_jobs();
                size_t size = (s.n - listStart + 31) & ~(size_t) 31;
                memcpy(m + listAt, bytes + listStart, s.n - listStart);
                memset(m + listAt + (s.n - listStart), 0, size - (s.n - listStart));
                s.n = listStart;
                s.u8_(0x40);
                s.u32_(phys + listAt);
                s.u32_((uint32_t) size);
                listAt += (uint32_t) size;
            }
            for (size_t i = 0; i < s.n; i++) {
                re4_gx_put_u8(bytes[i]);
            }
        }
        copyDisp(xfb);
        if (getenv("RE4_GX_FINGERPRINT_PARTS")) {
            unsigned st[11];
            re4_gx_stats(st);
            re4_log("  gx fingerprint frame %d: %u vertices so far", frame, st[kVertices]);
        }
        const Re4GxChunk* c = re4_core_gx_acquire();
        if (!c) {
            hash = fnv(hash, "none", 4);
            continue;
        }
        const uint32_t head[11] = {c->flags, c->opWords, c->vertexBytes, c->indexCount, c->blobBytes,
                                   c->draws, c->vertexCount, c->textureBytes, c->xfbAddress, c->xfbWidth,
                                   c->xfbHeight};
        hash = fnv(hash, head, sizeof(head));
        hash = fnv(hash, c->ops, (size_t) c->opWords * 4);
        hash = fnv(hash, c->vertices, c->vertexBytes);
        hash = fnv(hash, c->indices, (size_t) c->indexCount * 2);
        hash = fnv(hash, c->blob, c->blobBytes);
        if (getenv("RE4_GX_FINGERPRINT_PARTS")) {
            re4_log("  gx fingerprint frame %d: head %08x ops %08x vertices %08x indices %08x blob %08x", frame,
                    (unsigned) fnv(0, head, sizeof(head)), (unsigned) fnv(0, c->ops, (size_t) c->opWords * 4),
                    (unsigned) fnv(0, c->vertices, c->vertexBytes),
                    (unsigned) fnv(0, c->indices, (size_t) c->indexCount * 2), (unsigned) fnv(0, c->blob, c->blobBytes));
            if (const char* dir = getenv("RE4_GX_FINGERPRINT_DUMP")) {
                char path[512];
                snprintf(path, sizeof(path), "%s/fp-%02d.bin", dir, frame);
                if (FILE* f = fopen(path, "wb")) {
                    re4gxio::Write(f, *c, (uint32_t) frame);
                    fclose(f);
                }
            }
        }
        re4_core_gx_release(c);
    }
    re4_core_gx_detach();
    return hash;
}

// A frame like the village's (r101: about 1 MB of display lists and 120 000
// vertices per frame): model display lists with a matrix load before each
// strip, the usual model format (position index16 s16, normal index16 s8,
// texture coordinate index16 s16, position matrix index), called from the
// FIFO. Returns the decoder's time per frame in microseconds (RE4_GX_BENCH,
// runtime test "gx": the speed of the decoder on this host).
extern "C" unsigned re4probe_gx_bench(void* fifo, void* mem, void* xfb, int frames)
{
    static u8 bytes[64];
    GXInit(fifo, 0x10000);
    re4_core_gx_acquire();  // attach
    copyDisp(xfb);
    if (const Re4GxChunk* c = re4_core_gx_acquire()) {
        re4_core_gx_release(c);
    }
    u8* m = (u8*) mem;
    const uint32_t phys = re4_gx_physical(mem);
    Rng r = {0xBE7C4u};
    for (uint32_t i = 0; i < 0x20000; i++) {  // arrays: positions, normals, texture coordinates, matrices
        m[i] = r.byte();
    }
    // the list: 1500 strips of 20 vertices, a matrix load before each (RE4_GX_BENCH_STRIP=N:
    // strips of N vertices, as many as make 30 000; the forest of r100 has about 8 per GXBegin)
    const char* stripEnv = getenv("RE4_GX_BENCH_STRIP");
    const int strip = stripEnv && atoi(stripEnv) >= 3 && atoi(stripEnv) <= 1000 ? atoi(stripEnv) : 20;
    const int strips = 30000 / strip;
    const uint32_t listAt = 0x20000;
    Stream l = {m + listAt, 0};
    for (int k = 0; k < strips; k++) {
        l.u8_(0x20);
        l.u32_(r.below(64) << 16 | 11u << 12 | (r.below(8) * 12));
        l.u8_(0x98);
        l.u16_((u16) strip);
        for (int v = 0; v < strip; v++) {
            l.u8_(r.below(8) * 3);
            l.u16_(r.below(2000));
            l.u16_(r.below(2000));
            l.u16_(r.below(2000));
        }
    }
    const uint32_t listSize = (uint32_t) ((l.n + 31) & ~(size_t) 31);
    memset(m + listAt + l.n, 0, listSize - l.n);
    double total = 0;
    for (int frame = 0; frame < frames; frame++) {
        Stream s = {bytes, 0};
        cp(s, 0x50, 1 | 3u << 9 | 3u << 11);       // PNMTXIDX, position and normal index16
        cp(s, 0x60, 3);                             // texture coordinate 0 index16
        cp(s, 0x70, 1 | 3u << 1 | 6u << 4 | 1u << 10 | 1u << 21 | 3u << 22 | 8u << 25);
        cp(s, 0xA0, phys);
        cp(s, 0xB0, 6);
        cp(s, 0xA1, phys + 0x4000);
        cp(s, 0xB1, 3);
        cp(s, 0xA4, phys + 0x8000);
        cp(s, 0xB4, 4);
        cp(s, 0xAC, phys + 0x10000);
        cp(s, 0xBC, 48);
        for (size_t i = 0; i < s.n; i++) {
            re4_gx_put_u8(bytes[i]);
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int call = 0; call < 4; call++) {  // 4 x 30 000 vertices
            Stream c = {bytes, 0};
            c.u8_(0x40);
            c.u32_(phys + listAt);
            c.u32_(listSize);
            for (size_t i = 0; i < c.n; i++) {
                re4_gx_put_u8(bytes[i]);
            }
        }
        // the display copy ends the chunk: with the vertex jobs (RE4_GX_THREADS) it waits for them
        copyDisp(xfb);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        total += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
        if (const Re4GxChunk* c = re4_core_gx_acquire()) {
            re4_core_gx_release(c);
        }
    }
    re4_core_gx_detach();
    return frames ? (unsigned) (total / frames) : 0;
}
