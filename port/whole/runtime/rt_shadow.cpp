// The app's real-time shadows of the figures and objects (port/docs/echtzeit-schatten-studie.md; until
// 16.11.2026 debug builds only, since then every build, decisions of the project owner: "Die Schatten kannst du auch
// in den Hauptbuild übernehmen"). Compiled with the game's flags (re4_gamehooks_obj).
//
// The renderer draws the shadows (gx_core.cpp, GxCore::ShadowPass); the core says where and from which lights:
//  - re4_rt_shadow_frame (patch 0240, at the start of Render): the app switched them on (bit 5 of the Potato mask,
//    core_api.cpp re4_core_set_potato -> re4_potato_set), Potato's "Shadows & light" is on and the game is in play
//    (main loop, no event or cut scene, no sub screen, no death demo or continue screen, no movie, no thermal
//    scope). Since stage 5 (16.11.2026, the project owner: "Derzeit wird der Echtzeitschatten immer in die gleiche
//    Richtung angezeigt, mit der gleichen Intensität egal ob hell, dunkel oder in Räumen") the lights are the ones
//    that really light the player this frame: the game's own list of them (cLightInfo::pLight, cLightMgr::setModel2
//    in Trans), each worked out as trans_lit.cpp's LightSetModel does (DispCol and its alpha, Intensity, the type's
//    fall-off with the distance, a spot's cone). Since stage 6 (16.11.2026: a standing figure's shadow turned when
//    the player walked) the lights of every figure in view count, each by the most it gives one of them; up to three
//    of the brightest that sit in the world cast (parallel ones by their direction, the others from their place:
//    away from a torch, a lamp, a candle, for each figure from where it stands); a light that turns with the camera,
//    or one a figure carries, only lights. The renderer works out at each place how much of the light there a shadow
//    takes away (the light's fall-off against the ambient cLightEnv::AmbientEm, the lights that turn with the camera
//    and the casting ones), and less in the dark: nothing of it depends on where the player stands. The choice is
//    kept steady (a light that cast keeps its place unless another is much brighter). Since stage 7 (18.11.2026, the
//    owner's device test: shadows too dark in the castle) the ambient is the most any figure in view gets (its local
//    ambient lights, ambientAt), and lamps within 1 m of each other (two lights of one lamp) cast as one with their
//    strength together. The lightning of the storm rooms follows: its flash is a light of an effect (ESP11, r118) or
//    a light whose Intensity the flash raises (r119), often one that turns with the camera; while the game's flash
//    state is on (EffGetToolState bits 0..1, the rooms' ThunderFlagOn) such a light casts too. During a flash it is
//    the brightest light, between the flashes it is gone and the night's lamps cast faintly.
//  - re4_rt_shadow_blobs_off (patch 0240, DrawFootShadow): the game's foot shadow blobs are left out while the shadows
//    are on in play, also where no light casts (the project owner: shadows only where there is light).
//  - re4_rt_shadow_mark (patch 0240, in Render after the ordering table 14: the room and the figures (table 11) and
//    the game's own shadow receivers are drawn, the effects not yet): the port command re4_gx_put_shadow_lights2
//    with each light's kind, place or direction in view space, fall-off and softness, and the ambient. The renderer
//    draws the figures it saw so far from each light (a point light from its place) and darkens the scene behind them.
// The game reads nothing of this: the blobs are only drawn, never read. Off (the app's switch), nothing changes:
// no command, the blobs as before. RE4_SHADOW_LIGHTS=N (PC checks): every N frames the player's lights and the choice.
#include "types.h"
#include "global.h"
#include "light.h"
#include "model.h"
#include "em.h"
#include "player.h"
#include "camera.h"
#include "math_sub.h"
#include "esp.h"
#include "espgen.h"

#include "re4_gx_fifo.h"
#include "re4_runtime.h"

#include <math.h>
#include <stdlib.h>

namespace {

volatile int gOn;          // the app's switch (bit 5 of the Potato mask)
volatile int gObjects;     // "Characters and objects" (bit 6 of the Potato mask)
bool gActive;              // this frame casts real-time shadows (re4_rt_shadow_frame)
bool gBlobsOff;            // this frame leaves the game's foot shadow blobs out (on, in play)
unsigned gFrameNo;         // Render's frames (re4_rt_shadow_frame; gx_owner.cpp tells its frames apart)
unsigned gRoom = ~0u;      // the room told last
int gWhy = -1;             // the reason logged last (Why)
unsigned gMarks;           // frames marked since the last log line

// the lights of this frame (re4_rt_shadow_frame -> re4_rt_shadow_mark)
const int kMaxCast = 3;
// Stage 8 (18.11.2026): the most a parallel light that casts may travel upwards or sideways (the world's y of the way
// it travels, unit): it has to come from at least about 14 degrees above the horizon. The game's parallel lights from
// below or the side are fill lights for the figures, no sun or moon: in r10e two of them travel upwards (y +0.5 to
// +0.65) and threw Leon's shadow up the walls and onto the wall beside the ladder (the owner's device test).
const f32 kFromAbove = -0.25f;
struct Cast {
    cLight* light;  // the game's light
    Vec dir;        // a parallel light: the way it travels (world, unit)
    f32 best;       // the most it gives one figure (the order, the log)
    f32 soft;       // the shadow's softness (the renderer's filter radius in texels)
    bool castable;  // it may cast
    f32 merged;     // the strength of the lights within 1 m that cast with it, over its own (0: none)
};
Cast gCast[kMaxCast];
int gCasts;
f32 gAmbient;              // the light the same everywhere (the ambient, the camera's parallel lights), luminance

enum Why { kOff, kPotato, kNotPlaying, kNoLight, kOn };

const char* whyText(int why)
{
    switch (why) {
    case kOff: return "off (the app's switch)";
    case kPotato: return "off (Potato: shadows and light effects off)";
    case kNotPlaying: return "paused (event, sub screen, death or continue screen, movie)";
    case kNoLight: return "none (no light that sits in the world reaches the player here; no round blobs either)";
    case kOn:
        return gObjects ? "on (characters and objects, from the lights that reach the player; no round blobs)"
                        : "on (characters, from the lights that reach the player; no round blobs)";
    }
    return "?";
}

void note(int why)
{
    if (why == gWhy) {
        return;
    }
    gWhy = why;
    re4_log("real-time shadows: %s, r%03x (%u frames marked before)", whyText(why), pG ? (unsigned) pG->room_id : 0u,
            gMarks);
    gMarks = 0;
}

f32 luma(f32 r, f32 g, f32 b)
{
    return (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
}

// a light's colour as LightSetModel loads it (trans_lit.cpp lightSetColor: DispCol x its alpha / 128, clamped), as
// luminance
f32 lightLuma(const cLight* l)
{
    const f32 a = (f32) (int) l->DispCol.a * 0.0078125f;
    f32 c[3] = {(f32) l->DispCol.r * a, (f32) l->DispCol.g * a, (f32) l->DispCol.b * a};
    for (f32& x : c) {
        x = x < 0.0f ? 0.0f : x > 255.0f ? 255.0f : x;
    }
    return luma(c[0], c[1], c[2]);
}

// trans_lit.cpp's edge fade (types 0, 2, 5, spots): full within the radius plus the model's size, down to 0 over the
// last `edge` units
f32 edgeFade(const cLight* l, f32 d, f32 size, f32 edge, f32 beyond)
{
    const f32 range = l->Radius + size;
    if (l->Radius == 0.0f || d < range - edge) {
        return l->Intensity;
    }
    if (d < range && edge > 0.0f) {
        return l->Intensity * (range - d) / edge;
    }
    return beyond;
}

// a spot's cone (GX_SP_COS as GXInitLightSpot makes it): 1 on its axis, 0 at its cut-off angle
f32 spotCone(cLight* l, const Vec& toObj)
{
    Vec axis;
    l->getNormal(&l->cone.Normal, &axis);
    const f32 len = PSVECMag(&axis);
    if (!(len > 0.0f)) {
        return 1.0f;
    }
    const f32 cosA = PSVECDotProduct(&axis, &toObj) / len;
    const f32 cosC = cosf(l->cone.CutOff * (3.14159265f / 180.0f));
    if (!(cosC < 0.9999f)) {
        return cosA >= cosC ? 1.0f : 0.0f;
    }
    const f32 a1 = 1.0f / (1.0f - cosC), a0 = -cosC * a1;
    const f32 v = a0 + a1 * cosA;
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

// How much one light of the player's list lights the player (LightSetModel: the GX light's colour times its
// attenuation; the angle to the surface left out), 0 for none; the way its light travels into `dir` (world, unit), and
// whether it can cast (it sits in the world: not a parallel light that turns with the camera, unless a lightning
// flash lights it (`flash`), not at the player).
f32 lightAt(cLight* l, const Vec& obj, f32 size, bool flash, Vec* dir, bool* casts)
{
    *casts = false;
    Vec pos;
    l->getPos(&pos);
    Vec to;
    PSVECSubtract(&obj, &pos, &to);
    const f32 d = PSVECMag(&to);
    Vec toN = to;
    if (d > 1.0f) {
        PSVECScale(&to, &toN, 1.0f / d);
    }
    f32 att = 0.0f;
    switch (l->Type) {
    case 0:  // constant
        att = edgeFade(l, d, size, l->normal.x, 0.0f);
        break;
    case 1:  // linear
        att = l->Radius != 0.0f ? l->Intensity * (l->Radius - d) / l->Radius : l->Intensity;
        break;
    case 2: {  // quadratic: 0.1 at the radius
        const f32 br = edgeFade(l, d, size, l->normal.x, 0.001f);
        const f32 k2 = l->Radius != 0.0f ? (l->Intensity - 0.1f) / 0.1f / (l->Radius * l->Radius) : 0.0f;
        att = br / (1.0f + (k2 > 0.0f ? k2 : 0.0f) * d * d);
        break;
    }
    case 3: {  // spot: the cone, GXInitLightDistAttn(5000, br, medium) (none from br 1 on)
        const f32 br = edgeFade(l, d, size, l->cone.Edge, 0.001f);
        f32 dist = 1.0f;
        if (br > 0.0f && br < 1.0f) {
            const f32 k1 = 0.5f * (1.0f - br) / (br * 5000.0f), k2 = 0.5f * (1.0f - br) / (br * 5000.0f * 5000.0f);
            dist = 1.0f / (1.0f + k1 * d + k2 * d * d);
        }
        att = spotCone(l, toN) * dist;
        break;
    }
    case 4: {  // custom: the raw GX coefficients
        Vec axis;
        l->getNormal(&l->spot.Normal, &axis);
        const f32 len = PSVECMag(&axis);
        const f32 c = len > 0.0f ? PSVECDotProduct(&axis, &toN) / len : 1.0f;
        const f32 a = l->spot.A0 + l->spot.A1 * c + l->spot.A2 * c * c;
        const f32 k = l->spot.K0 + l->spot.K1 * d + l->spot.K2 * d * d;
        att = a > 0.0f && k > 0.0f ? a / k : 0.0f;
        break;
    }
    case 5: {  // parallel: the direction, in the world or turning with the camera (Flag bit 0)
        att = edgeFade(l, d, size, l->dir.Edge, 0.001f);
        Vec p = l->dir.Pos;
        const bool camera = (l->dir.Flag & 1) != 0;
        if (camera) {
            Mtx inv;
            PSMTXInverse(pG->Camera.v_mat, inv);
            PSMTXMultVecSR(inv, &l->dir.Pos, &p);
        }
        const f32 len = PSVECMag(&p);
        if (len > 0.0f) {
            PSVECScale(&p, dir, -1.0f / len);
            *casts = !camera || flash;
        }
        break;
    }
    case 6: {  // spot with a quadratic fall-off scaled by the brightness
        const f32 br = edgeFade(l, d, size, l->cone.Edge, 0.001f);
        const f32 k2 = l->Radius != 0.0f && br > 0.0f ? (l->Intensity - 0.1f) / 0.1f / (l->Radius * l->Radius) : 0.0f;
        att = spotCone(l, toN) * br / (1.0f + (k2 > 0.0f ? k2 : 0.0f) * d * d);
        break;
    }
    default:  // 7: a local ambient light (lightAmbient), no GX light
        return 0.0f;
    }
    if (!(att > 0.0f)) {
        return 0.0f;
    }
    if (att > 2.0f) {
        att = 2.0f;
    }
    if (l->Type != 5) {
        // from its place to the player; not when it sits at the player (a light it carries: the muzzle flash) or is
        // the player's own
        if (d > 400.0f && l->pParent != (cModel*) pPL) {
            *dir = toN;
            *casts = true;
        }
    }
    return lightLuma(l) * att;
}

// a figure: the player, the partner or a work of the character manager whose id is below 0x40 (as gx_owner.cpp)
bool isFigureModel(const cModel* p)
{
    if (!p) {
        return false;
    }
    if (p == (const cModel*) pPL || p == (const cModel*) pSUB) {
        return true;
    }
    const u8* a = (const u8*) EmMgr.pArray;
    const size_t size = EmMgr.size;
    if (a && size && (const u8*) p >= a && (const u8*) p < a + (size_t) EmMgr.nArray * size) {
        return ((const cEm*) (a + ((const u8*) p - a) / size * size))->id < 0x40;
    }
    return false;
}

// The ambient a figure gets (luminance): the brightest of the room's local ambient lights (Type 7, lightAmbient of
// trans_lit.cpp: full within the radius plus the model's size, fading over normal.x) and cLightEnv::AmbientEm, as
// the game takes it (the brighter of each colour). Stage 5 had it at the player; in the castle halls AmbientEm is
// black and these lights are all the ambient there is (stage 6 left them out: every lamp's shadow there was as dark
// as it gets, the owner's device test of 17.11.2026).
f32 ambientAt(const cModel* m, const Vec& obj, f32 size)
{
    const GXColor env = LightMgr.getEnvPtr()->AmbientEm;
    f32 r = env.r, g = env.g, b = env.b;
    for (int i = 0; i < 8; i++) {
        cLight* l = m->LightInfo.pLight[i];
        if (!l || l->Type != 7) {
            continue;
        }
        Vec pos;
        l->getPos(&pos);
        const f32 d = GetDistance3((Vec*) &obj, &pos);
        const f32 range = l->Radius + size;
        f32 f = 0.0f;
        if (l->Radius == 0.0f || d < range - l->normal.x) {
            f = 1.0f;
        } else if (d < range && l->normal.x > 0.0f) {
            f = (range - d) / l->normal.x;
        }
        r = r > f * l->DispCol.r ? r : f * l->DispCol.r;
        g = g > f * l->DispCol.g ? g : f * l->DispCol.g;
        b = b > f * l->DispCol.b ? b : f * l->DispCol.b;
    }
    return luma(r, g, b);
}

// This frame's lights for the shadows (stage 6, 16.11.2026: until then the player's lights alone, worked out at his
// place, so that a standing figure's shadow turned and changed when he walked; the project owner: "die Schatten der
// anderen Personen 'wandern' bzw. 'bewegen' sich um die Person mit, wenn sich Leon bewegt"): the lights of every
// figure in view (the player, the partner, the character manager's figures drawn this frame within 20 m of the
// camera), each light by the most it gives one of them; the three brightest that sit in the world cast. Where a shadow
// falls and how dark it is the renderer works out from the light's place and fall-off (re4_rt_shadow_mark), never from
// where the player stands.
int chooseLights()
{
    static const int logEvery = getenv("RE4_SHADOW_LIGHTS") ? atoi(getenv("RE4_SHADOW_LIGHTS")) : 0;
    const bool log = logEvery > 0 && gFrameNo % (unsigned) logEvery == 0;
    // a lightning flash this frame (the storm rooms' tool state, eff_sys.cpp EffCallToolStateCallBack)
    const bool flash = g_pEspSys && (EffGetToolState() & 3) != 0;
    cModel* figs[48];
    int nf = 0;
    if (pPL) {
        figs[nf++] = pPL;
    }
    const u8* a = (const u8*) EmMgr.pArray;
    for (int i = 0; a && EmMgr.size && i < EmMgr.nArray && nf < 48; i++) {
        cEm* em = (cEm*) (a + (size_t) i * EmMgr.size);
        if ((cModel*) em == pPL || (em->be_flag & 3) != 3 || em->id >= 0x40) {
            continue;
        }
        const Vec obj = em->pList ? em->pList->world : em->pos;
        Vec v;
        PSMTXMultVec(pG->Camera.v_mat, (Vec*) &obj, &v);
        if (PSVECMag(&v) > 20000.0f) {
            continue;
        }
        figs[nf++] = em;
    }
    Cast found[24];
    int n = 0;
    f32 localAmbient = 0.0f;
    for (int f = 0; f < nf; f++) {
        cModel* m = figs[f];
        const Vec obj = m->pList ? m->pList->world : m->pos;
        const f32 size = m->LightInfo.Size.x > m->LightInfo.Size.y ? m->LightInfo.Size.x : m->LightInfo.Size.y;
        const f32 ambient = ambientAt(m, obj, size);
        localAmbient = ambient > localAmbient ? ambient : localAmbient;
        for (int i = 0; i < 8; i++) {
            cLight* l = m->LightInfo.pLight[i];
            if (!l || l->Type > 6) {
                continue;
            }
            Vec dir = {0.0f, -1.0f, 0.0f};
            bool casts = false;
            const f32 light = lightAt(l, obj, size, flash, &dir, &casts);
            // a parallel light casts when it sits in the world (or a flash lights it) and comes from above
            // (kFromAbove); any other from its place, unless a figure carries it (a Ganado's torch, the muzzle
            // flash): it would throw its bearer's shadow
            const bool castable = l->Type == 5 ? casts && dir.y < kFromAbove : !isFigureModel(l->pParent);
            int k = 0;
            while (k < n && found[k].light != l) {
                k++;
            }
            if (k == n) {
                if (n == 24) {
                    continue;
                }
                found[n].light = l;
                found[n].dir = dir;
                found[n].best = 0.0f;
                found[n].castable = castable;
                found[n].merged = 0.0f;
                n++;
            }
            if (light > found[k].best) {
                found[k].best = light;
            }
            if (log && f == 0) {
                Vec pos;
                l->getPos(&pos);
                re4_log("real-time shadows: light %d: id %u type %u be 0x%x %s, %.0f mm away, colour %u %u %u a %u, "
                        "intensity %.2f, radius %.0f -> %.3f%s%s",
                        i, (unsigned) l->Id, (unsigned) l->Type, (unsigned) l->be_flag,
                        (l->be_flag & 0x40) ? "(an effect's)" : "", (double) GetDistance3((Vec*) &obj, &pos),
                        (unsigned) l->DispCol.r, (unsigned) l->DispCol.g, (unsigned) l->DispCol.b,
                        (unsigned) l->DispCol.a, (double) l->Intensity, (double) l->Radius, (double) light,
                        castable ? ", casts" : "",
                        l->Type != 5 ? ""
                        : casts && dir.y >= kFromAbove ? ", from below or the side: no shadow"
                        : "");
                if (l->Type == 5) {
                    re4_log("real-time shadows: light %d travels (%.2f %.2f %.2f) in the world", i, (double) dir.x,
                            (double) dir.y, (double) dir.z);
                }
            }
        }
    }
    // the brightest first
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && found[j].best > found[j - 1].best; j--) {
            const Cast t = found[j];
            found[j] = found[j - 1];
            found[j - 1] = t;
        }
    }
    // Keep the choice steady: the values above follow the player too, and the first light is also the one the objects
    // cast from (the renderer's tiles 0 and 1). A light that cast last frame counts half again as bright when the three
    // are chosen, and it stays first until another gives a figure twice as much (a lightning flash does). So a light
    // that comes into the player's reach turns no object's shadow and, while no more than three reach the figures,
    // takes no other light's place.
    static cLight* last[kMaxCast];
    static int lastN;
    auto wasCasting = [&](const cLight* l) {
        for (int s = 0; s < lastN; s++) {
            if (last[s] == l) {
                return s;
            }
        }
        return -1;
    };
    // A lamp within 1 m of a chosen one casts with it (r202: two lights of one lamp 0.5 m apart drew two shadows on
    // top of each other, as dark as they get, and took a place from another light): it adds its strength to it.
    bool joined[24] = {};
    auto lampNear = [&](int a, int b) {
        const cLight* la = found[a].light;
        const cLight* lb = found[b].light;
        if (la->Type == 5 || lb->Type == 5 || (la->Type == 3 || la->Type == 6) != (lb->Type == 3 || lb->Type == 6)) {
            return false;
        }
        Vec pa, pb;
        found[a].light->getPos(&pa);
        found[b].light->getPos(&pb);
        return GetDistance3(&pa, &pb) < 1000.0f;
    };
    int pick[kMaxCast];
    int casts = 0;
    for (; casts < kMaxCast; casts++) {
        int top = -1;
        f32 topValue = 0.0f;
        for (int i = 0; i < n; i++) {
            bool taken = joined[i];
            for (int c = 0; c < casts; c++) {
                taken = taken || pick[c] == i;
            }
            if (taken || !found[i].castable || found[i].best < 0.02f) {
                continue;
            }
            const f32 value = found[i].best * (wasCasting(found[i].light) >= 0 ? 1.5f : 1.0f);
            if (top < 0 || value > topValue) {
                top = i;
                topValue = value;
            }
        }
        if (top < 0) {
            break;
        }
        pick[casts] = top;
        const f32 own = lightLuma(found[top].light) * found[top].light->Intensity;
        for (int i = 0; i < n; i++) {
            bool taken = joined[i] || i == top;
            for (int c = 0; c < casts; c++) {
                taken = taken || pick[c] == i;
            }
            if (!taken && found[i].castable && lampNear(top, i) && own > 0.0f) {
                joined[i] = true;
                found[top].merged += lightLuma(found[i].light) * found[i].light->Intensity / own;
            }
        }
    }
    // the order: the light first last frame stays first unless another is twice as bright, the rest the brightest
    // first (pick holds indices into found, which is sorted by best)
    for (int i = 1; i < casts; i++) {
        for (int j = i; j > 0 && pick[j] < pick[j - 1]; j--) {
            const int t = pick[j];
            pick[j] = pick[j - 1];
            pick[j - 1] = t;
        }
    }
    for (int i = 1; i < casts && lastN > 0; i++) {
        if (found[pick[i]].light == last[0] && found[pick[0]].best <= 2.0f * found[pick[i]].best) {
            const int t = pick[i];
            for (int j = i; j > 0; j--) {
                pick[j] = pick[j - 1];
            }
            pick[0] = t;
            break;
        }
    }
    for (int i = 0; i < casts; i++) {
        gCast[i] = found[pick[i]];
        // sharp from the sky and a flash, a little softer from a lamp
        gCast[i].soft = gCast[i].light->Type == 5 || (gCast[i].light->be_flag & 0x40) || flash ? 1.5f : 2.0f;
        last[i] = gCast[i].light;
    }
    lastN = casts;
    // the light that is the same everywhere: the ambient and the parallel lights that turn with the camera (they never
    // cast between the flashes, but light every figure alike). The renderer weighs each casting light against all the
    // light at a place, as stage 5 did at the player; without these the night's lamps were lost against too small a
    // total (r118 between the flashes: 0.13 instead of the player's 0.44, no shadow at all)
    // Since stage 7 the ambient is the most any figure in view gets (its local ambient lights or AmbientEm,
    // ambientAt), so that the castle halls' lamps do not darken a shadow to the full.
    gAmbient = localAmbient;
    for (int i = 0; i < n; i++) {
        bool cast = false;
        for (int c = 0; c < casts; c++) {
            cast = cast || gCast[c].light == found[i].light;
        }
        if (!cast && !joined[i] && found[i].light->Type == 5 && found[i].light->Radius == 0.0f) {
            gAmbient += found[i].best;
        }
    }
    if (log) {
        re4_log("real-time shadows: r%03x frame %u: %d figures, %d lights, ambient %.3f%s, %d cast: %.3f %.3f %.3f",
                pG ? (unsigned) pG->room_id : 0u, gFrameNo, nf, n, (double) gAmbient, flash ? " (flash)" : "", casts,
                (double) (casts > 0 ? gCast[0].best : 0.0f), (double) (casts > 1 ? gCast[1].best : 0.0f),
                (double) (casts > 2 ? gCast[2].best : 0.0f));
    }
    return casts;
}

// One light for the renderer (15 floats, view space): kind (0 parallel, 1 point, 2 spot), the way a parallel light
// travels or the place of any other, its colour's luminance (lightLuma), its strength, the distance up to which it is
// full and where it is gone (the edge fade of trans_lit.cpp, with a figure's size 1 m), the GX distance attenuation k1,
// k2, a spot's cut-off cosine (-2 none) and axis, the shadow's softness. The renderer works out the light at each place
// from these as LightSetModel does at a figure.
void lightParams(const Cast& c, const Mtx v, f32* out)
{
    cLight* l = c.light;
    const f32 size = 1000.0f, far = 1.0e9f;
    f32 kind = 1.0f, I = l->Intensity, dFull = far, dZero = far, k1 = 0.0f, k2 = 0.0f, cosCut = -2.0f;
    Vec pos, axis = {0.0f, 0.0f, 0.0f};
    l->getPos(&pos);
    const f32 range = l->Radius + size;
    switch (l->Type) {
    case 0:  // constant, faded over normal.x at the edge
    case 2:  // quadratic: 0.1 at the radius
        if (l->Radius != 0.0f) {
            dFull = range - l->normal.x;
            dZero = range;
        }
        if (l->Type == 2 && l->Radius != 0.0f) {
            k2 = (l->Intensity - 0.1f) / 0.1f / (l->Radius * l->Radius);
        }
        break;
    case 1:  // linear to 0 at the radius
        if (l->Radius != 0.0f) {
            dFull = 0.0f;
            dZero = l->Radius;
        }
        break;
    case 3:    // spot: the cone, GXInitLightDistAttn(5000, Intensity, medium); the colour not scaled
    case 6: {  // spot with a quadratic fall-off scaled by the brightness
        kind = 2.0f;
        if (l->Radius != 0.0f) {
            dFull = range - l->cone.Edge;
            dZero = range;
        }
        if (l->Type == 3) {
            if (I > 0.0f && I < 1.0f) {
                k1 = 0.5f * (1.0f - I) / (I * 5000.0f);
                k2 = 0.5f * (1.0f - I) / (I * 5000.0f * 5000.0f);
            }
            I = 1.0f;
        } else if (l->Radius != 0.0f) {
            k2 = (l->Intensity - 0.1f) / 0.1f / (l->Radius * l->Radius);
        }
        cosCut = cosf(l->cone.CutOff * (3.14159265f / 180.0f));
        Vec w;
        l->getNormal(&l->cone.Normal, &w);
        PSMTXMultVecSR((MtxPtr) v, &w, &axis);
        const f32 len = PSVECMag(&axis);
        if (len > 0.0f) {
            PSVECScale(&axis, &axis, 1.0f / len);
        } else {
            cosCut = -2.0f;
        }
        break;
    }
    case 4:  // custom: (A0 + A1 + A2) / (K0 + K1 d + K2 d^2), the angle left out
        I = l->spot.K0 > 0.0f ? (l->spot.A0 + l->spot.A1 + l->spot.A2) / l->spot.K0 : 0.0f;
        k1 = l->spot.K0 > 0.0f ? l->spot.K1 / l->spot.K0 : 0.0f;
        k2 = l->spot.K0 > 0.0f ? l->spot.K2 / l->spot.K0 : 0.0f;
        break;
    case 5:  // parallel
        kind = 0.0f;
        break;
    }
    // a soft end: many of the game's lamps stop at their reach at once (normal.x 0), which would cut a shadow off in a
    // line; the shadow fades out over another 30 % of the reach instead
    if (dZero < far && dZero - dFull < 0.3f * dZero) {
        dZero = dFull + 0.3f * dZero;
    }
    Vec p;
    if (kind == 0.0f) {
        PSMTXMultVecSR((MtxPtr) v, (Vec*) &c.dir, &p);
    } else {
        PSMTXMultVec((MtxPtr) v, &pos, &p);
    }
    I *= 1.0f + c.merged;
    const f32 values[15] = {kind, p.x, p.y, p.z, lightLuma(l), I, dFull, dZero, k1 > 0.0f ? k1 : 0.0f,
                            k2 > 0.0f ? k2 : 0.0f, cosCut, axis.x, axis.y, axis.z, c.soft};
    for (int i = 0; i < 15; i++) {
        out[i] = values[i];
    }
}

}  // namespace

extern "C" {

// bits 5 (on) and 6 (also the objects) of the Potato mask (potato.cpp re4_potato_set). Each change is logged as it
// arrives (16.11.2026: a device test showed nothing, and nothing in the log told whether the core had the setting;
// the app had cut the bit off); never in a release build, which never sets the bits.
void re4_rt_shadow_set(int bits)
{
    const int on = (bits & 0x20) != 0, objects = on && (bits & 0x40) != 0;
    if (gOn != on || gObjects != objects) {
        gOn = on;
        gObjects = objects;
        re4_log("real-time shadows: the app's setting arrived: %s",
                on ? (objects ? "characters and objects" : "characters") : "off");
        gWhy = on ? -1 : kOff;  // on: the next frame tells where they stand
    }
}

// gx_owner.cpp: the classes the owner tags carry in this picture: 0 none (off, or this frame casts none: the tags as
// before), 1 the figures' parts, 2 also the objects
int re4_rt_shadow_classes(void)
{
    return gActive ? (gObjects ? 2 : 1) : 0;
}

// gx_owner.cpp: the number of this frame (Render's)
unsigned re4_rt_shadow_frame_no(void)
{
    return gFrameNo;
}

int re4_potato_no_shadows(void);

// Patch 0240, Render's start: does this frame cast real-time shadows, and from which lights?
void re4_rt_shadow_frame(void)
{
    gActive = false;
    gBlobsOff = false;
    gCasts = 0;
    gFrameNo++;
    if (!gOn) {
        return;
    }
    // each room tells anew where they stand (on, or paused and why)
    if (pG && pG->room_id != gRoom) {
        gRoom = pG->room_id;
        gWhy = -1;
    }
    if (re4_potato_no_shadows()) {
        note(kPotato);
        return;
    }
    if (!pG || !pPL || pG->Rno0 != 3 || DpfFlagChk(pG, DPF_SHADOW) || StaFlagChk(pG, STA_EVENT) ||
        StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_DIEDEMO) || StaFlagChk(pG, STA_MOVIE_ON) ||
        StaFlagChk(pG, STA_MOVIE2_ON) || StaFlagChk(pG, STA_THERMO_GRAPH)) {
        note(kNotPlaying);
        return;
    }
    gBlobsOff = true;
    gCasts = chooseLights();
    if (gCasts == 0) {
        note(kNoLight);
        return;
    }
    gActive = true;
    note(kOn);
}

// Patch 0240, DrawFootShadow: no blobs while the shadows are on in play (also where no light casts).
int re4_rt_shadow_blobs_off(void)
{
    return gBlobsOff;
}

// Patch 0240, Render after the ordering table 14: the shadows go on the scene here.
void re4_rt_shadow_mark(void)
{
    if (!gActive || gCasts <= 0) {
        return;
    }
    f32 lights[1 + kMaxCast * 15];
    lights[0] = gAmbient;
    for (int i = 0; i < gCasts; i++) {
        lightParams(gCast[i], pG->Camera.v_mat, &lights[1 + 15 * i]);
    }
    re4_gx_put_shadow_lights2(gCasts, lights);
    gMarks++;
}

}  // extern "C"
