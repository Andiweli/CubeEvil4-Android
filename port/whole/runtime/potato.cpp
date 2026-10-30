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
// Kept: muzzle flashes, blood, explosions, fire and glows (additive blends), dynamite, hits, the hints,
// and the distance fog (GX fog, not an effect sprite). RE4_POTATO=mask switches it on without the app
// (1 weather, 2 special, 3 both); RE4_ESP_CENSUS=N logs every N frames the live sprites by owner, id,
// blend and shimmer with an estimate of the screen they cover and what the switches do with them.
#include "types.h"
#include "global.h"
#include "esp.h"
#include "camera.h"

#include "re4_runtime.h"

#include <stdlib.h>
#include <string.h>

void EspCommonTrans(cEsp* esp);
extern EspTransFunc EspTransTbl[0xFF];  // game/esp.cpp

namespace {

enum { kWeather = 1, kSpecial = 2 };

volatile int gMask = -1;  // -1: not read yet (RE4_POTATO), else the switches on (kWeather | kSpecial)
unsigned gCensusEvery;
unsigned gSkipped[2];
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

void readEnv()
{
    if (gMask >= 0) {
        return;
    }
    const char* on = getenv("RE4_POTATO");
    gMask = on && *on ? (int) (strtoul(on, 0, 0) & 3) : 0;
    if (const char* census = getenv("RE4_ESP_CENSUS")) {
        gCensusEvery = (unsigned) strtoul(census, 0, 0);
    }
    if (gMask) {
        re4_log("potato: weather effects %s, special effects %s (RE4_POTATO)", gMask & kWeather ? "off" : "on",
                gMask & kSpecial ? "off" : "on");
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
    const int k = kind(esp);
    if (!(k & gMask)) {
        return 0;
    }
    gSkipped[k == kWeather ? 0 : 1]++;
    const unsigned tick = pG->Frame_cnt / 1800;  // about once a minute
    if (tick != gLastTick) {
        gLastTick = tick;
        re4_log("potato: sprites left out so far: %u weather, %u special (r%03x)", gSkipped[0], gSkipped[1],
                (unsigned) pG->room_id);
    }
    return 1;
}

// The app's switches (core_api.cpp re4_core_set_potato): bit 0 weather effects off, bit 1 special effects off.
void re4_potato_set(int mask)
{
    readEnv();
    mask &= 3;
    if (gMask != mask) {
        re4_log("potato: weather effects %s, special effects %s", mask & kWeather ? "off" : "on",
                mask & kSpecial ? "off" : "on");
    }
    gMask = mask;
}

int re4_potato_mask(void)
{
    readEnv();
    return gMask;
}

}  // extern "C"
