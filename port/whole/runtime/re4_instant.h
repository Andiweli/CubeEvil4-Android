#ifndef RE4_INSTANT_H
#define RE4_INSTANT_H

#include <stdint.h>

// One app-owned slot, outside the emulated memory card. No process pointers in files or replies.
enum {
    RE4_INSTANT_STATUS = 0,
    RE4_INSTANT_SAVE = 1,
    RE4_INSTANT_LOAD = 2
};
enum {
    RE4_INSTANT_OK = 0,
    RE4_INSTANT_BUSY = -1,
    RE4_INSTANT_EMPTY = -2,
    RE4_INSTANT_BAD = -3,
    RE4_INSTANT_IO = -4,
    RE4_INSTANT_UNSUPPORTED = -5
};
typedef struct Re4InstantInfo {
    uint32_t present, canSave, canLoad, room, chapter, playTime, savedAt, reserved;
} Re4InstantInfo;

#ifdef __cplusplus
extern "C" {
#endif
// Requires the core to be paused; the operation runs on its game thread, before the next frame.
int re4_core_instant(int operation, Re4InstantInfo* info);
// Game-thread implementation (instant_store.cpp); also used by the host checks.
int re4_instant_perform(int operation, Re4InstantInfo* info);
// Fixed original save-file payloads, host byte order. Capture does not replace the Continue image.
int re4_instant_game_ready(void);
int re4_instant_game_can_load(void);
unsigned re4_instant_game_chapter(unsigned number);
int re4_instant_game_capture(unsigned char* data);
int re4_instant_game_restore(const unsigned char* data);
void re4_instant_game_saved(void);
int re4_instant_game_tick(void);
void re4_instant_title_tick(void* work);
void re4_instant_game_pause(int on);
#ifdef __cplusplus
}
#endif
#endif
