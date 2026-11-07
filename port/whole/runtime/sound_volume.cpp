// The app menu's AUDIO page (wish of the project owner, 30.10.2026): the volume of the music and of the
// sound effects, 0 .. 100 % each. Compiled with the game's flags into libre4_main; the app sends both at
// its start and at every change (re4_core_set_sound_volume).
//
// The sound driver works out each sound's output volume from its system volumes (Snd_ctrl_work) and the
// sound's own volume: Snd_axv_work_calc_ax_vol (the sound effects of the I.S.S.), Snd_seq_work_calc_ax_vol
// (sequences: type 2 effects, else music) and Snd_str_work_calc_ax_vol (streams: type 2 effects, else
// music, the room BGM). Patch 0153 scales the result by re4_sound_volume(): the game's own volumes stay as
// they are (its mute and fades work as before). After a change the driver recomputes every playing sound at
// the next game frame (Snd_reset_vol_all, a frame hook on the game thread). The movies' ADX sound is not
// part of the driver and keeps its volume.
//
// For tests on the PC: RE4_SOUND_VOLUME=music,effects (percent) at the start.
#include "types.h"
#include "snd_drv.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

namespace {

std::atomic<int> gMusic(100);       // percent
std::atomic<int> gEffects(100);     // percent
std::atomic<bool> gPending(false);  // recompute the playing sounds at the next game frame
std::atomic<bool> gHooked(false);

void hook(unsigned)
{
    if (gPending.exchange(false)) {
        Snd_reset_vol_all();
    }
}

int clampPercent(int v)
{
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

// RE4_SOUND_VOLUME at the start (the frame hook is not needed: nothing plays yet)
struct StartSetting {
    StartSetting()
    {
        int music = 100, effects = 100;
        if (const char* v = getenv("RE4_SOUND_VOLUME")) {
            if (sscanf(v, "%d,%d", &music, &effects) == 2) {
                gMusic = clampPercent(music);
                gEffects = clampPercent(effects);
            }
        }
    }
} gStartSetting;

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_set_sound_volume). Any thread.
void re4_sound_volume_set(int music, int effects)
{
    music = clampPercent(music);
    effects = clampPercent(effects);
    const bool changed = gMusic.exchange(music) != music;
    const bool changed2 = gEffects.exchange(effects) != effects;
    if (changed || changed2) {
        re4_log("sound: music %d %%, sound effects %d %%", music, effects);
    }
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(hook);
    }
    gPending.store(true);
}

// Patch 0153: the factor for a sound's output volume, music (1) or a sound effect (0), as percent 0..100.
int re4_sound_volume(int music)
{
    return (music ? gMusic : gEffects).load(std::memory_order_relaxed);
}

}  // extern "C"
