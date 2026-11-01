#include "types.h"
#include "vec.h"
#include "gx.h"
#include "t_prim.h"
#include "camera.h"

// Debug primitive drawing for the tool modules (D:/Bio4/Prog/t_prim.cpp, the same object in every
// t_*/Tools REL that draws). The DOL's game/t_prim.cpp is the dead-stripped version of this file: the
// linker kept only the functions the game calls, so this is where TprimDraw2D/TprimDrawPolyFn/
// TprimDrawCursor/TprimDrawMtxDirection come from (their constant pools survived in the DOL as the
// 0x60 anonymous .rodata words in front of t_prim's data).


void set_attr_common();
void set_attr_f32();
void set_vtx_flat_f32(Vec* v, GXColor* col, u16 n);
#ifdef TPRIM_FULL
// The Tools REL carries the full file (18 functions, src/Tools/t_prim.cpp defines TPRIM_FULL and includes
// this one); t_emlist/t_camera have the build without the 2D/s16/Htr helpers.
void set_attr_s16();
void set_vtx_flat_s16(S16Vec* v, GXColor* col, u16 n);
#endif

static f32 Vrect[6] = {0.0f, 0.0f, 512.0f, 448.0f, 0.0f, 1.0f};
static f32 Orect[4];
static MtxPtr ProjMtx;
static MtxPtr ViewMtx;
static int FlipMode = 0;
u8 ToolBuffer[0x100] __attribute__((aligned(32)));

// Sets the primitive drawing environment: the 2D ortho rect and viewport, the 3D projection and
// view matrices (normally the game camera's), z test off.
void TprimInitEnv2D3D(f32* view, MtxPtr proj, MtxPtr view_mtx)
{
    memcpy(Orect, view, sizeof(Orect));
    memcpy(Vrect, view, sizeof(Vrect));
    ProjMtx = proj;
    ViewMtx = view_mtx;
    FlipMode = 0;
}

#ifdef TPRIM_FULL
// Changes the 2D ortho rectangle only.
void TprimInitEnv2D(f32* rect)
{
    memcpy(Orect, rect, sizeof(Orect));
}
#endif

// Begins 2D drawing: ortho projection over the 2D rect, identity model matrix, blend mode
// (0 opaque, 1 alpha, 2 additive) and the flat colour vertex format.
void TprimDraw2D(u32 mode)
{
    Mtx44 proj;
    Mtx pos;

    C_MTXOrtho(proj, Orect[1], Orect[3], Orect[0], Orect[2], 0.0f, -100.0f);
    GXSetProjection(proj, 1);
    PSMTXIdentity(pos);
    GXSetCurrentMtx(0);
    GXLoadPosMtxImm(pos, 0);
    TprimSetBlend(mode);
    set_attr_common();
}

// Begins 3D drawing: the current camera projection, the view matrix as model matrix, blend mode
// and the flat colour vertex format.
void TprimDraw3D(u32 mode)
{
    CameraCurrentProjection();
    GXSetCurrentMtx(0);
    GXLoadPosMtxImm(ViewMtx, 0);
    TprimSetBlend(mode);
    set_attr_common();
}

// GX blend mode for `blend` 0 none / 1 src alpha / 2 additive; other values leave it unchanged.
void TprimSetBlend(u32 mode)
{
    static u32 bl[3][4] = {
        {0, 1, 0, 0},
        {1, 1, 1, 0},
        {1, 0, 2, 0},
    };

    if (mode <= 2) {
        GXSetBlendMode(bl[mode][0], bl[mode][1], bl[mode][2], bl[mode][3]);
        GXSetColorUpdate(1);
    }
}

// TEV / channel setup for flat vertex-coloured primitives: no textures, one colour channel, no
// culling, z test only in FlipMode bit 0, line width 6, f32 positions.
void set_attr_common()
{
    GXSetCullMode(0);
    if (FlipMode & 1) {
        GXSetZMode(1, 3, 1);
    } else {
        GXSetZMode(0, 3, 0);
    }
    GXSetNumTexGens(0);
    GXSetNumTevStages(1);
    GXSetTevOp(0, 4);
    GXSetTevOrder(0, 0xFF, 0xFF, 4);
    GXSetNumChans(1);
    GXSetChanCtrl(0, 0, 0, 1, 1, 0, 2);
    GXSetLineWidth(6, 0);
    set_attr_f32();
}

// Vertex format: f32 position + RGBA8 colour, direct.
void set_attr_f32()
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(11, 1);
    GXSetVtxAttrFmt(0, 9, 1, 4, 0);
    GXSetVtxAttrFmt(0, 11, 1, 5, 0);
}

#ifdef TPRIM_FULL
// Vertex format: s16 position + RGBA8 colour, direct.
void set_attr_s16()
{
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);
    GXSetVtxDesc(11, 1);
    GXSetVtxAttrFmt(0, 9, 1, 3, 0);
    GXSetVtxAttrFmt(0, 11, 1, 5, 0);
}

// Line strip through `n` points in one colour.
void TprimDrawLineFn(Vec* v, GXColor* c, u16 n)
{
    GXBegin(0xB0, 0, n);
    set_vtx_flat_f32(v, c, n);
}
#endif

// Filled polygon (quad primitive) over `n` points in one colour.
void TprimDrawPolyFn(Vec* v, GXColor* c, u16 n)
{
    GXBegin(0x80, 0, n);
    set_vtx_flat_f32(v, c, n);
}

#ifdef TPRIM_FULL
// Filled 2D rectangle at depth z.
void TprimDrawTile2D(f32* v, f32 z, GXColor* c)
{
    GXBegin(0x80, 0, 4);
    GXPosition3f32(v[0], v[1], z);
    GXColor4u8(c->r, c->g, c->b, c->a);
    GXPosition3f32(v[0] + v[2], v[1], z);
    GXColor4u8(c->r, c->g, c->b, c->a);
    GXPosition3f32(v[0] + v[2], v[1] + v[3], z);
    GXColor4u8(c->r, c->g, c->b, c->a);
    GXPosition3f32(v[0], v[1] + v[3], z);
    GXColor4u8(c->r, c->g, c->b, c->a);
}
#endif

// Cross-hair of four triangles around `pos` (the last one's tip has z 0 in the original).
void TprimDrawCursor(Vec* pos, f32 z, GXColor* c)
{
    Vec v[3];

    v[0].x = pos->x;
    v[0].y = pos->y - 2.0f;
    v[0].z = z;
    v[1].x = pos->x - 4.0f;
    v[1].y = v[0].y - 8.0f;
    v[1].z = z;
    v[2].x = pos->x + 4.0f;
    v[2].y = v[1].y;
    v[2].z = z;
    TprimDrawPolyFn(v, c, 3);

    v[0].x = pos->x;
    v[0].y = pos->y + 2.0f;
    v[0].z = z;
    v[1].x = pos->x + 4.0f;
    v[1].y = v[0].y + 8.0f;
    v[1].z = z;
    v[2].x = pos->x - 4.0f;
    v[2].y = v[1].y;
    v[2].z = z;
    TprimDrawPolyFn(v, c, 3);

    v[0].x = pos->x - 2.0f;
    v[0].y = pos->y;
    v[0].z = z;
    v[1].x = v[0].x - 8.0f;
    v[1].y = pos->y + 4.0f;
    v[1].z = z;
    v[2].x = v[1].x;
    v[2].y = pos->y - 4.0f;
    v[2].z = z;
    TprimDrawPolyFn(v, c, 3);

    v[0].x = pos->x + 2.0f;
    v[0].y = pos->y;
    v[0].z = 0.0f;
    v[1].x = v[0].x + 8.0f;
    v[1].y = pos->y - 4.0f;
    v[1].z = z;
    v[2].x = v[1].x;
    v[2].y = pos->y + 4.0f;
    v[2].z = z;
    TprimDrawPolyFn(v, c, 3);
}

#ifdef TPRIM_FULL
// The 10.0f word between TprimDrawCursor's and TprimDrawHtr's constant pools: a public const object
// (emitted at its definition; nothing references it).
extern const f32 TprimHtrSize;
const f32 TprimHtrSize = 10.0f;

// Hit marker: a horizontal square around `pos`.
void TprimDrawHtr(Vec* pos, GXColor* col)
{
    Vec v[4];

    GXBegin(0x80, 0, 4);
    v[0].x = pos->x;
    v[0].y = pos->y;
    v[0].z = pos->z - 300.0f;
    v[1].x = pos->x - 300.0f;
    v[1].y = pos->y;
    v[1].z = pos->z;
    v[2].x = pos->x;
    v[2].y = pos->y;
    v[2].z = pos->z + 300.0f;
    v[3].x = pos->x + 300.0f;
    v[3].y = pos->y;
    v[3].z = pos->z;
    set_vtx_flat_f32(v, col, 4);
}

// Hit marker cone: four triangles from the apex 1200 above `pos`, the last three in a darker colour.
void TprimDrawHtrCone(Vec* pos, GXColor* col)
{
    Vec v[3];
    GXColor c;

    v[0].x = pos->x;
    v[0].y = pos->y + 1200.0f;
    v[0].z = pos->z;
    v[1].x = pos->x;
    v[1].y = pos->y;
    v[1].z = pos->z - 300.0f;
    v[2].x = pos->x + 300.0f;
    v[2].y = pos->y;
    v[2].z = pos->z;
    TprimDrawPolyFn(v, col, 3);

    v[0].x = pos->x;
    v[0].y = pos->y + 1200.0f;
    v[0].z = pos->z;
    v[1].x = pos->x + 300.0f;
    v[1].y = pos->y;
    v[1].z = pos->z;
    v[2].x = pos->x;
    v[2].y = pos->y;
    v[2].z = pos->z + 300.0f;
    c.r = col->r >> 1;
    c.g = col->g >> 1;
    c.b = col->b >> 1;
    c.a = col->a;
    TprimDrawPolyFn(v, &c, 3);

    v[0].x = pos->x;
    v[0].y = pos->y + 1200.0f;
    v[0].z = pos->z;
    v[1].x = pos->x;
    v[1].y = pos->y;
    v[1].z = pos->z + 300.0f;
    v[2].x = pos->x - 300.0f;
    v[2].y = pos->y;
    v[2].z = pos->z;
    c.r = col->r >> 1;
    c.g = col->g >> 1;
    c.b = col->b >> 1;
    c.a = col->a;
    TprimDrawPolyFn(v, &c, 3);

    v[0].x = pos->x;
    v[0].y = pos->y + 1200.0f;
    v[0].z = pos->z;
    v[1].x = pos->x - 300.0f;
    v[1].y = pos->y;
    v[1].z = pos->z;
    v[2].x = pos->x;
    v[2].y = pos->y;
    v[2].z = pos->z - 300.0f;
    c.r = col->r >> 1;
    c.g = col->g >> 1;
    c.b = col->b >> 1;
    c.a = col->a;
    TprimDrawPolyFn(v, &c, 3);
}
#endif

// Never called. GCC 2.95 emits the initializer templates of local aggregates in inline functions at
// parse time, and the original object carries these 9 words between TprimDrawCursor's constant pool
// and TprimDrawMtxDirection's template. The values are the original's; the grouping and the body are a
// guess that reproduces them.
// Likewise the 0x20 bytes of .bss behind ToolBuffer (unreferenced, so the DOL link dropped them).
#ifndef TPRIM_FULL
static f32 default_view[6];
static f32 default_clip[2];

// Never called: keeps the default view / clip / x-axis constants of the original object.
static inline void tprim_default_view(Vec* axis)
{
    f32 v[6] = {10.0f, 300.0f, 1200.0f, 300.0f, 1.0f, 0.0f};
    Vec x = {1.0f, 0.0f, 0.0f};

    memcpy(default_view, v, sizeof(v));
    default_clip[0] = v[4];
    default_clip[1] = v[5];
    *axis = x;
}
#else
// The full build has TprimDrawHtr/HtrCone (the 10/300 and 1200/300 words are their pools); only the
// {1, 0} and {1, 0, 0} templates remain unexplained.
static f32 default_clip[2];

// Never called: keeps the clip / x-axis constants of the original object.
static inline void tprim_default_view(Vec* axis)
{
    f32 clip[2] = {1.0f, 0.0f};
    Vec x = {1.0f, 0.0f, 0.0f};

    default_clip[0] = clip[0];
    default_clip[1] = clip[1];
    *axis = x;
}
#endif

// Arrow head along the matrix' z axis: a filled triangle and its outline. Nothing in the Tools REL calls
// it and only its template survives there (an unused inline).
#ifdef TPRIM_FULL
inline
#endif
void TprimDrawMtxDirection(Mtx mat, GXColor* c0, GXColor* c1)
{
    Vec v[3] = {{0.0f, 0.0f, 900.0f}, {300.0f, 0.0f, -300.0f}, {-300.0f, 0.0f, -300.0f}};

    PSMTXMultVec(mat, &v[0], &v[0]);
    PSMTXMultVec(mat, &v[1], &v[1]);
    PSMTXMultVec(mat, &v[2], &v[2]);
    GXBegin(0x80, 0, 3);
    GXPosition3f32(v[0].x, v[0].y, v[0].z);
    GXColor4u8(c0->r, c0->g, c0->b, c0->a);
    GXPosition3f32(v[1].x, v[1].y, v[1].z);
    GXColor4u8(c0->r, c0->g, c0->b, c0->a);
    GXPosition3f32(v[2].x, v[2].y, v[2].z);
    GXColor4u8(c0->r, c0->g, c0->b, c0->a);
    GXBegin(0xB0, 0, 4);
    GXPosition3f32(v[0].x, v[0].y, v[0].z);
    GXColor4u8(c1->r, c1->g, c1->b, c1->a);
    GXPosition3f32(v[1].x, v[1].y, v[1].z);
    GXColor4u8(c1->r, c1->g, c1->b, c1->a);
    GXPosition3f32(v[2].x, v[2].y, v[2].z);
    GXColor4u8(c1->r, c1->g, c1->b, c1->a);
    GXPosition3f32(v[0].x, v[0].y, v[0].z);
    GXColor4u8(c1->r, c1->g, c1->b, c1->a);
}

#ifdef TPRIM_FULL
// Closed outline: the strip plus the first vertex again.
void TprimDrawFrameFn_s16(S16Vec* v, GXColor* c, u16 n)
{
    set_attr_s16();
    GXBegin(0xB0, 0, n + 1);
    set_vtx_flat_s16(v, c, n);
    GXPosition3s16(v[0].x, v[0].y, v[0].z);
    GXColor4u8(c->r, c->g, c->b, c->a);
    set_attr_f32();
}

// Filled polygon over `n` s16 points in one colour (switches the vertex format and back).
void TprimDrawPolyFn_s16(S16Vec* v, GXColor* c, u16 n)
{
    set_attr_s16();
    GXBegin(0x80, 0, n);
    set_vtx_flat_s16(v, c, n);
    set_attr_f32();
}
#endif

// Emits `n` f32 vertices with the same colour.
void set_vtx_flat_f32(Vec* v, GXColor* col, u16 n)
{
    u16 i = 0;

    do {
        GXPosition3f32(v[i].x, v[i].y, v[i].z);
        GXColor4u8(col->r, col->g, col->b, col->a);
    } while (++i < n);
}

#ifdef TPRIM_FULL
// Emits `n` s16 vertices with the same colour.
void set_vtx_flat_s16(S16Vec* v, GXColor* col, u16 n)
{
    u16 i = 0;

    do {
        GXPosition3s16(v[i].x, v[i].y, v[i].z);
        GXColor4u8(col->r, col->g, col->b, col->a);
    } while (++i < n);
}

// the next unit's .data is 8-aligned
asm(".section .data; .balign 8");
#endif
