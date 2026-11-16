// The app's real-time shadows of the figures and objects (port/docs/echtzeit-schatten-studie.md; debug builds only
// until after 1.0, decision of the project owner, 15.11.2026: "nur in der Debug Version die ich nutze aktiviert, nicht
// wenn man Release baut"). Compiled with the game's flags (re4_gamehooks_obj).
//
// The renderer draws the shadows (gx_core.cpp, GxCore::ShadowPass); the core says where and from which lights:
//  - re4_rt_shadow_frame (patch 0240, at the start of Render): the app switched them on (bit 5 of the Potato mask,
//    core_api.cpp re4_core_set_potato -> re4_potato_set), Potato's "Shadows & light" is on and the game is in play
//    (main loop, no event or cut scene, no sub screen, no death demo or continue screen, no movie, no thermal scope).
//    Since stage 5 (16.11.2026, the project owner: "Derzeit wird der Echtzeitschatten immer in die gleiche Richtung
//    angezeigt, mit der gleichen Intensität egal ob hell, dunkel oder in Räumen") the lights are the ones that really
//    light the player this frame: the game's own list of them (cLightInfo::pLight, cLightMgr::setModel2 in Trans), each
//    worked out as trans_lit.cpp's LightSetModel does (DispCol and its alpha, Intensity, the type's fall-off with the
//    distance, a spot's cone), and the ambient the figure gets (cLightEnv::AmbientEm, the local ambient lights). Up to
//    three of the brightest lights that sit in the world cast (parallel ones by their direction, the others from their
//    place: away from a torch, a lamp, a candle); a light that turns with the camera, or sits at the player, only
//    lights. Each light's weight is its share of all the light at the player (ambient included), and less in the dark:
//    a shadow takes away the light it blocks. The lightning of the storm rooms follows: its flash is a light of an
//    effect (ESP11, r118) or a light whose Intensity the flash raises (r119), often one that turns with the camera;
//    while the game's flash state is on (EffGetToolState bits 0..1, the rooms' ThunderFlagOn) such a light casts too.
//    During a flash it is the brightest light, between the flashes it is gone and the night's lamps cast faintly.
//  - re4_rt_shadow_blobs_off (patch 0240, DrawFootShadow): the game's foot shadow blobs are left out while the shadows
//    are on in play, also where no light casts (the project owner: shadows only where there is light).
//  - re4_rt_shadow_mark (patch 0240, in Render after the ordering table 14: the room and the figures (table 11) and
//    the game's own shadow receivers are drawn, the effects not yet): the port command re4_gx_put_shadow_lights with
//    the lights' directions in view space, their weights and softness. The renderer draws the figures it saw so far
//    from each light and darkens the scene behind them.
// The game reads nothing of this: the blobs are only drawn, never read. Off (every release build), nothing changes:
// no command, the blobs as before. RE4_SHADOW_LIGHTS=N (PC checks): every N frames a line with the player's lights.
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
struct Cast {
    Vec dir;    // the way its light travels at the player (world, unit)
    f32 weight; // how much of the light there a shadow takes away
    f32 soft;   // the shadow's softness (the renderer's filter radius in texels)
    f32 light;  // its brightness at the player (for the order and the log)
};
Cast gCast[kMaxCast];
int gCasts;
f32 gTotal;                // all the light at the player (luminance, 1 a white light at full strength)

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

// the ambient the figure gets (LightSetModel: the local ambient lights, at least cLightEnv::AmbientEm, the model's
// added ambient), as luminance
f32 ambientAt(cModel* m, const Vec& obj, f32 size)
{
    f32 r = 0.0f, g = 0.0f, b = 0.0f;
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
    const GXColor env = LightMgr.getEnvPtr()->AmbientEm;
    r = r > env.r ? r : env.r;
    g = g > env.g ? g : env.g;
    b = b > env.b ? b : env.b;
    if (m->be_flag & 8) {
        r += m->AddAmb_r;
        g += m->AddAmb_g;
        b += m->AddAmb_b;
    }
    return luma(r, g, b);
}

// This frame's lights for the shadows: the player's (as his model is lit), the brightest three that sit in the world
int chooseLights(cModel* m)
{
    static const int logEvery = getenv("RE4_SHADOW_LIGHTS") ? atoi(getenv("RE4_SHADOW_LIGHTS")) : 0;
    const bool log = logEvery > 0 && gFrameNo % (unsigned) logEvery == 0;
    const Vec obj = m->pList ? m->pList->world : m->pos;
    const f32 size = m->LightInfo.Size.x > m->LightInfo.Size.y ? m->LightInfo.Size.x : m->LightInfo.Size.y;
    const f32 ambient = ambientAt(m, obj, size);
    // a lightning flash this frame (the storm rooms' tool state, eff_sys.cpp EffCallToolStateCallBack)
    const bool flash = g_pEspSys && (EffGetToolState() & 3) != 0;
    f32 total = ambient;
    Cast found[8];
    int n = 0;
    for (int i = 0; i < 8; i++) {
        cLight* l = m->LightInfo.pLight[i];
        if (!l) {
            continue;
        }
        Vec dir = {0.0f, -1.0f, 0.0f};
        bool casts = false;
        const f32 light = lightAt(l, obj, size, flash, &dir, &casts);
        total += light;
        if (log) {
            Vec pos;
            l->getPos(&pos);
            re4_log("real-time shadows: light %d: id %u type %u be 0x%x %s, %.0f mm away, colour %u %u %u a %u, "
                    "intensity %.2f, radius %.0f -> %.3f%s",
                    i, (unsigned) l->Id, (unsigned) l->Type, (unsigned) l->be_flag,
                    (l->be_flag & 0x40) ? "(an effect's)" : "", (double) GetDistance3((Vec*) &obj, &pos),
                    (unsigned) l->DispCol.r, (unsigned) l->DispCol.g, (unsigned) l->DispCol.b, (unsigned) l->DispCol.a,
                    (double) l->Intensity, (double) l->Radius, (double) light, casts ? ", casts" : "");
        }
        if (casts && light > 0.0f) {
            found[n].dir = dir;
            found[n].light = light;
            // softer from a light close by (a torch beside the figure), sharp from the sky and a flash
            Vec pos;
            l->getPos(&pos);
            const f32 d = GetDistance3((Vec*) &obj, &pos);
            f32 soft = l->Type == 5 || (l->be_flag & 0x40) || flash ? 1.5f : 1.5f + 3000.0f / (d > 1.0f ? d : 1.0f);
            found[n].soft = soft > 4.0f ? 4.0f : soft;
            n++;
        }
    }
    gTotal = total;
    // the strongest first
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && found[j].light > found[j - 1].light; j--) {
            const Cast t = found[j];
            found[j] = found[j - 1];
            found[j - 1] = t;
        }
    }
    // each one's share of the light at the player, less in the dark: full from 0.9 of a white light at full strength
    // on (daylight r100 1.5, the church r117 1.35), about a third at night between the flashes (r118 0.44), none
    // below 0.15
    f32 dark = (total - 0.15f) / 0.75f;
    dark = dark < 0.0f ? 0.0f : dark > 1.0f ? 1.0f : dark;
    int casts = 0;
    for (int i = 0; i < n && casts < kMaxCast; i++) {
        f32 w = total > 0.0f ? found[i].light / total : 0.0f;
        w = (w > 0.75f ? 0.75f : w) * dark;
        if (w < 0.03f) {
            continue;
        }
        gCast[casts] = found[i];
        gCast[casts].weight = w;
        casts++;
    }
    if (log) {
        for (int i = 0; i < casts; i++) {
            Vec v;
            PSMTXMultVecSR(pG->Camera.v_mat, &gCast[i].dir, &v);
            re4_log("real-time shadows: cast %d: weight %.2f, soft %.1f, world (%.2f %.2f %.2f), view (%.2f %.2f %.2f)",
                    i, (double) gCast[i].weight, (double) gCast[i].soft, (double) gCast[i].dir.x,
                    (double) gCast[i].dir.y, (double) gCast[i].dir.z, (double) v.x, (double) v.y, (double) v.z);
        }
        re4_log("real-time shadows: r%03x frame %u: ambient %.3f, all the light %.3f%s, %d cast: %.2f %.2f %.2f",
                pG ? (unsigned) pG->room_id : 0u, gFrameNo, (double) ambient, (double) total, flash ? " (flash)" : "",
                casts,
                (double) (casts > 0 ? gCast[0].weight : 0.0f), (double) (casts > 1 ? gCast[1].weight : 0.0f),
                (double) (casts > 2 ? gCast[2].weight : 0.0f));
    }
    return casts;
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
    gCasts = chooseLights(pPL);
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
    f32 lights[kMaxCast * 5];
    for (int i = 0; i < gCasts; i++) {
        Vec v;
        PSMTXMultVecSR(pG->Camera.v_mat, &gCast[i].dir, &v);
        lights[5 * i + 0] = v.x;
        lights[5 * i + 1] = v.y;
        lights[5 * i + 2] = v.z;
        lights[5 * i + 3] = gCast[i].weight;
        lights[5 * i + 4] = gCast[i].soft;
    }
    re4_gx_put_shadow_lights(gCasts, lights);
    gMarks++;
}

}  // extern "C"
