// The app menu's "Potato mode" (decision of the project owner, 30.10.2026; two switches on its wish of
// the same day; re4_core_set_potato): expensive surroundings effects are left out, everything that tells
// the player something stays. The game's effect sprites (cEsp, game/esp.cpp) are drawn by EspTrans;
// patch 0149 asks re4_esp_skip for each live sprite there, before its trans function draws it. A sprite
// left out is only not drawn: its life, sounds and anything the game reads from it go on as before.
//
// Effects of events and cut scenes (Core_flg 0x1000, and everything while an event runs) always stay.
//   weather effects (bit 0): fog banks, rain, water spray: rain / dust sheets over the screen (esp 0x04),
//            rain / snow particles (0x15), and the room tables' covering sprites that do not rise (owner
//            EFF_SST, blended over the picture: blend type 0, 4 or 5, or a scrolling sheet esp 0x08)
//   special effects (bit 1): steam, heat shimmer, water: the picture distortions that copy the picture
//            and draw it warped (esp 0x0F, 0x18, any sprite with a shimmer type) and the room tables'
//            covering sprites that rise (steam)
//
// The room code's effects (owner EFF_ROOM) stay: the smoke of exploding barrels and the dust of falling
// rocks are such effects, and the liquid nitrogen of the Verdugo fight (r221 "bonbe") is drawn additive
// and marks where he freezes (checked on the Retroid, 30.10.2026).
//
// Kept by these two: muzzle flashes, blood, explosions, fire and glows (additive blends), dynamite, hits, the
// hints, and the distance fog (GX fog, not an effect sprite).
//
// Since 0.99.3 (a user's wish through the project owner, 08.11.2026; port/docs/whole-program-status.md, version
// 0.99.3, item 2) two more settings:
//   effects (bits 2..3: 0 normal, 1 less, 2 none): the effects of fights, weapons and objects. Only sprites whose
//            trans function draws and does nothing else (drawsOnly) count. "Less" leaves every second one out (by
//            its place in the effect pool, so a sprite is drawn or not for its whole life, never flickers): blood,
//            dust, smoke, splashes and debris, the flash and smoke at the player's gun, and glowing particles that
//            fly free (fire, sparks, explosions). "None" leaves out all of the first kind and still draws every
//            second glowing particle: fire and explosions stay visible as a danger. Always kept: the effects at
//            the point of a hit (owner EFF_CORE: the blood burst that shows a hit, the dust of a bullet in a wall),
//            the room code's effects (EFF_ROOM), glows on a model other than the player's gun (a dynamite fuse, a
//            Ganado's torch), the laser (esp 0x19), and every effect that is no plain sprite (lights, screen
//            filters, shakes, sounds, ropes, cloth).
//   shadows and light effects (bit 4): no shadows (patch 0219: ShadowTrans and the foot shadows; with them the
//            light pictures that a shadow light projects onto figures), no lens glows (esp 0x0E), light shafts
//            (0x14) or light blooms (0x45, the Filter00 spread). The rooms' lights themselves stay as they are, so
//            nothing gets darker. The shadows also go in events and cut scenes: the game draws its shadow textures
//            almost only there (debug starts of 90 rooms: r11b, r206, r215, r227, r228, r22b, r300, r316, every
//            frame with a shadow texture in an event), and they cost the most there (12 to 25 % of the GPU time on
//            the PC); the effect sprites of events stay.
// Esp 0x0B's trans draws a jitter from the game's own random numbers (fRand1_1, Core_flg 0x8000): a left out 0x0B
// still takes its two numbers, so the game's random numbers and with them everything that follows stay the same.
//
// RE4_POTATO=mask switches it on without the app (1 weather, 2 special, 4 effects less, 8 effects none, 16 shadows
// and light effects); RE4_ESP_CENSUS=N logs every N frames the live sprites by owner, id, blend and shimmer with an
// estimate of the screen they cover and what the switches do with them.
#include "types.h"
#include "global.h"
#include "esp.h"
#include "camera.h"
#include "rnd.h"

#include "re4_runtime.h"

#include <stdlib.h>
#include <string.h>

void EspCommonTrans(cEsp* esp);
extern EspTransFunc EspTransTbl[0xFF];  // game/esp.cpp

namespace {

enum { kWeather = 1, kSpecial = 2, kFxShift = 2, kFxBits = 3 << kFxShift, kLights = 16, kAll = 0x1F };
enum { kFxLess = 1, kFxNone = 2 };    // the effects level (bits 2..3)
enum { kFxKeep, kFxThin, kFxDrop };    // what the effects level does with a sprite (fx)

volatile int gMask = -1;  // -1: not read yet (RE4_POTATO), else the switches (kWeather | kSpecial | level | kLights)
unsigned gCensusEvery;
unsigned gSkipped[4];  // weather, special, effects, light effects
unsigned gLastTick;

struct CensusKey {
    u8 owner, id, blend, shimmer, kind, rises, attached;
    u16 core;
};
struct CensusRow {
    CensusKey key;
    unsigned count;
    float cover;  // sum of size^2 / distance^2 (relative screen cover)
};
CensusRow gRows[256];
unsigned gRowCount;
unsigned gCensusFrame, gCensusFrames;

const char* levelName(int mask)
{
    const int level = (mask & kFxBits) >> kFxShift;
    return level == kFxLess ? "less" : level >= kFxNone ? "none" : "normal";
}

void logMask(int mask, const char* why)
{
    re4_log("potato: weather effects %s, special effects %s, effects %s, shadows and light effects %s%s",
            mask & kWeather ? "off" : "on", mask & kSpecial ? "off" : "on", levelName(mask), mask & kLights ? "off" : "on",
            why);
}

void readEnv()
{
    if (gMask >= 0) {
        return;
    }
    const char* on = getenv("RE4_POTATO");
    gMask = on && *on ? (int) (strtoul(on, 0, 0) & kAll) : 0;
    if (const char* census = getenv("RE4_ESP_CENSUS")) {
        gCensusEvery = (unsigned) strtoul(census, 0, 0);
    }
    if (gMask) {
        logMask(gMask, " (RE4_POTATO)");
    }
}

// The common sprite layout (EspCommonTrans) carries blend and shimmer type; other ids keep their own
// data in that union.
bool common(const cEsp* esp)
{
    return EspTransTbl[esp->m_Id] == EspCommonTrans;
}

bool rises(const cEsp* esp)
{
    return esp->m_Speed.y > 0.0f || esp->m_Speed_plus.y > 0.0f;
}

int kind(const cEsp* esp)
{
    const u8 id = esp->m_Id;
    // effects of events and cut scenes stay (r11b: the veins on Leon's face are such room effects)
    if (esp->info.Core_flg & 0x1000) {
        return 0;
    }
    // and effects on a model or a figure
    if (esp->m_pMod != 0) {
        return 0;
    }
    if (id == 0x0F || id == 0x18 || (common(esp) && esp->m_Shimmer_type != 0)) {
        return kSpecial;
    }
    if (id == 0x04 || id == 0x15) {
        return kWeather;
    }
    const u8 blend = common(esp) ? esp->m_Blend_type : 0xFF;
    const bool covers = blend == 0 || blend == 4 || blend == 5 || id == 0x08;
    if (!covers) {
        return 0;
    }
    // the room tables' permanent ambient effects (SstSet: Core_flg 0x4001)
    if (esp->info.owner == EFF_SST && (esp->info.Core_flg & 0x4000)) {
        return rises(esp) ? kSpecial : kWeather;
    }
    return 0;
}

// Ids whose trans function only draws (a sprite, strip, trail or decal): the effects level may leave them out.
// The others do more than draw or are kept for what they show: weather and screen sheets (0x04, 0x15, 0x47), the
// distortions (0x0F, 0x18), the est spawner (0x0C), glows and lights (0x0E, 0x11, 0x14, 0x45), the retired 0x13,
// ropes and chains (0x16), the laser (0x19), vector buffers (0x3F), the delayed spawner (0x43), sound (0x44),
// the screen filter (0x46), the shake (0x4A), the weather control (0x4C), water ripples (0x4D) and cloth (0x4E).
bool drawsOnly(u8 id)
{
    switch (id) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09: case 0x0A:
    case 0x0B: case 0x0D: case 0x10: case 0x12: case 0x17: case 0x1A: case 0x1B: case 0x40: case 0x41: case 0x42:
    case 0x48: case 0x49: case 0x4B: case 0x4F:
        return true;
    }
    return false;
}

// What the effects level does with a sprite the weather and special switches keep.
int fx(const cEsp* esp)
{
    if (esp->info.Core_flg & 0x1000) {
        return kFxKeep;
    }
    if (!drawsOnly(esp->m_Id)) {
        return kFxKeep;
    }
    const unsigned owner = esp->info.owner;
    // the hit at its point (the blood burst, a bullet's dust in a wall) and the room code's effects
    if (owner == EFF_CORE || owner == EFF_ROOM) {
        return kFxKeep;
    }
    // added to the picture (blend dst ONE): fire, explosions, sparks, fuses, flashes, glows
    const bool glows = esp->m_Dst_factor == 1;
    const bool gun = owner >= EFF_WEP00 && owner <= EFF_WEP33;
    if (esp->m_pMod != 0) {
        // on a model: the flash and smoke at the player's gun go, a fuse or a torch stays
        return gun ? kFxDrop : kFxKeep;
    }
    if (glows) {
        return kFxThin;
    }
    // the room tables' own sprites belong to the weather and special switches
    if (owner == EFF_SST) {
        return kFxKeep;
    }
    return kFxDrop;
}

// The light effects that "shadows and light effects" leaves out: lens glows, light shafts, light blooms.
bool lightEffect(const cEsp* esp)
{
    const u8 id = esp->m_Id;
    return (id == 0x0E || id == 0x14 || id == 0x45) && !(esp->info.Core_flg & 0x1000);
}

// Every second sprite by its place in the effect pool: one sprite is drawn or not for its whole life.
bool second(const cEsp* esp)
{
    return ((unsigned long) esp / sizeof(cEsp)) & 1;
}

void census(const cEsp* esp, unsigned frame)
{
    if (frame != gCensusFrame) {
        if (gRowCount && ++gCensusFrames % gCensusEvery == 0) {
            re4_log("esp census (frame %u, room r%03x, potato %d): %u kinds", gCensusFrame, (unsigned) pG->room_id,
                    gMask, gRowCount);
            for (unsigned i = 0; i < gRowCount; i++) {
                const CensusRow& r = gRows[i];
                re4_log("  owner %3u id 0x%02x core 0x%04x blend %3u shimmer %3u rises %u on a model %u: %3u sprites, "
                        "cover %8.2f, %s",
                        (unsigned) r.key.owner, (unsigned) r.key.id, (unsigned) r.key.core, (unsigned) r.key.blend,
                        (unsigned) r.key.shimmer, (unsigned) r.key.rises, (unsigned) r.key.attached, r.count, (double) r.cover,
                        r.key.kind == kWeather ? "weather" : r.key.kind == kSpecial ? "special" : "kept");
            }
        }
        gRowCount = 0;
        gCensusFrame = frame;
    }
    CensusKey k = {esp->info.owner, esp->m_Id, common(esp) ? esp->m_Blend_type : (u8) 0xFF,
                   common(esp) ? esp->m_Shimmer_type : (u8) 0xFF, (u8) kind(esp), (u8) rises(esp),
                   (u8) (esp->m_pMod != 0), esp->info.Core_flg};
    const Vec& c = pG->Camera.param.Campos;
    const float dx = esp->m_Pos.x - c.x, dy = esp->m_Pos.y - c.y, dz = esp->m_Pos.z - c.z;
    const float d2 = dx * dx + dy * dy + dz * dz + 1.0f;
    const float sx = esp->m_Size_base_x * esp->m_Size_mul, sy = esp->m_Size_base_y * esp->m_Size_mul;
    const float area = sx * sy;
    const float cover = (area < 0.0f ? -area : area) / d2;
    for (unsigned i = 0; i < gRowCount; i++) {
        CensusRow& r = gRows[i];
        if (!memcmp(&r.key, &k, sizeof(k))) {
            r.count++;
            r.cover += cover;
            return;
        }
    }
    if (gRowCount < 256) {
        gRows[gRowCount++] = {k, 1, cover};
    }
}

}  // namespace

extern "C" {

// EspTrans (patch 0149): 1 = do not draw this live sprite this frame.
int re4_esp_skip(cEsp* esp)
{
    readEnv();
    if (gCensusEvery) {
        census(esp, pG->Frame_cnt);
    }
    // events and cut scenes keep every effect (r11b: the veins on Leon's face are room table sprites)
    if (!gMask || StaFlagChk(pG, STA_EVENT)) {
        return 0;
    }
    const int mask = gMask;
    const int k = kind(esp);
    int why = -1;
    if (k & mask) {
        why = k == kWeather ? 0 : 1;
    } else if ((mask & kLights) && lightEffect(esp)) {
        why = 3;
    } else if (mask & kFxBits) {
        const int f = fx(esp);
        const int level = (mask & kFxBits) >> kFxShift;
        if ((f == kFxDrop && level >= kFxNone) || (f != kFxKeep && second(esp))) {
            why = 2;
        }
    }
    if (why < 0) {
        return 0;
    }
    // the jitter's two random numbers (Esp0b_Trans), so that the game's random numbers stay as they were
    if (esp->m_Id == 0x0B && (esp->info.Core_flg & 0x8000)) {
        fRand1_1();
        fRand1_1();
    }
    gSkipped[why]++;
    const unsigned tick = pG->Frame_cnt / 1800;  // about once a minute
    if (tick != gLastTick) {
        gLastTick = tick;
        re4_log("potato: sprites left out so far: %u weather, %u special, %u effects, %u light effects (r%03x)",
                gSkipped[0], gSkipped[1], gSkipped[2], gSkipped[3], (unsigned) pG->room_id);
    }
    return 1;
}

// Patch 0219 (ShadowTrans, DrawFootShadow): 1 = no shadows this frame ("shadows and light effects" off).
int re4_potato_no_shadows(void)
{
    readEnv();
    return (gMask & kLights) != 0;
}

// The app's switches (core_api.cpp re4_core_set_potato): bit 0 weather effects off, bit 1 special effects off,
// bits 2..3 the effects level (1 less, 2 none), bit 4 shadows and light effects off.
void re4_potato_set(int mask)
{
    readEnv();
    mask &= kAll;
    if (gMask != mask) {
        logMask(mask, "");
    }
    gMask = mask;
}

int re4_potato_mask(void)
{
    readEnv();
    return gMask;
}

}  // extern "C"
