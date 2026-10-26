// The game's language with the European discs (port/docs/retail-eu-plan.md, R7). Compiled with
// game flags; the game's functions run on the game thread.
//
// The European release has every language on its discs: the six-language message tables (core,
// rooms, memory card, codec) and the folders ss/Eng, ss/Ger, ss/Fra, ss/Esp, ss/Ita. Its setLanguage
// (European main.dol 0x80125354) starts from the console's system language (OSGetLanguage + 2:
// 2 English, 3 German, 4 French, 5 Spanish, 6 Italian; Dutch is English), its title menu has a
// "LANGUAGE SELECT". The debug build fixes language 1 (the US release: no event subtitles, two
// difficulties). Decisions of the project owner: the language is chosen in the app's in-game menu,
// the phone's language until then, "LANGUAGE SELECT" leaves the title menu (patch 0121; 26.10.2026);
// a change restarts the game after a question (27.10.2026; switching while the game ran left the
// title and the loaded screens in the old language).
//
// The app's choice comes as re4_core_language_wanted (runtime/core_api.cpp: RE4_GAME_LANGUAGE at the
// start, re4_core_restart_game later). Patch 0121 asks here at boot (setLanguage), at every start
// and soft reset (systemWorkInit) and after the system file of the memory card was read (it holds
// the language it was saved with).
#include "types.h"
#include "global.h"
#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern "C" int re4_disc_retail_eu(void);       // runtime/dvd.cpp
extern "C" int re4_core_language_wanted(void);  // runtime/core_api.cpp: 2..6, or -1
extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" int re4_game_language_value(void);
extern "C" void re4_core_restart_game(int language);  // runtime/core_api.cpp

namespace {

int gRestartHooked;
int gRestartAsked;  // the app asked for a restart (re4_game_restart)

const char* name(int language)
{
    static const char* const names[] = {"Japanese", "English (US)", "English", "German", "French", "Spanish", "Italian"};
    return language >= 0 && language <= 6 ? names[language] : "?";
}

void setFields(int language)
{
    pSys->language = (u8) language;
    pSys->eff_country = (u8) language;
    pG->game_country = (u8) language;
}

void restartHook(unsigned frame)
{
    if (__atomic_exchange_n(&gRestartAsked, 0, __ATOMIC_ACQ_REL)) {
        re4_log("language: the game restarts (soft reset at frame %u, language %s asked)", frame,
                name(re4_game_language_value()));
        SysFlagOn(pG, SYS_SOFT_RESET);
    }
}

// RE4_TEST_LANGUAGE_RESTART="N:LANG" (the PC check): at game frame N what the app's "Restart" asks
// for, with LANG (eng, ger, fra, esp, ita or 2..6).
long gTestFrame = -1;
int gTestLanguage;

void testHook(unsigned frame)
{
    if (gTestFrame >= 0 && (long) frame >= gTestFrame) {
        gTestFrame = -1;
        re4_log("language: restart with language %d (RE4_TEST_LANGUAGE_RESTART) at frame %u", gTestLanguage, frame);
        re4_core_restart_game(gTestLanguage);
    }
}

void testSetUp()
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    const char* v = getenv("RE4_TEST_LANGUAGE_RESTART");
    const char* colon = v ? strchr(v, ':') : 0;
    if (!colon) {
        return;
    }
    static const char* const names[] = {"eng", "ger", "fra", "esp", "ita"};
    gTestLanguage = atoi(colon + 1);
    for (int i = 0; i < 5; i++) {
        if (strcasecmp(colon + 1, names[i]) == 0) {
            gTestLanguage = i + 2;
        }
    }
    gTestFrame = atol(v);
    re4_add_frame_hook(testHook);
}

}  // namespace

// The language the game gets with the European discs (2..6), -1 with the debug build's discs.
extern "C" int re4_game_language_value(void)
{
    testSetUp();
    if (!re4_disc_retail_eu()) {
        return -1;
    }
    int wanted = re4_core_language_wanted();
    return wanted >= 2 && wanted <= 6 ? wanted : 2;
}

// After the system file was read into pSys (card.cpp): the app's language, not the saved one. With
// the debug build's discs the build's own language 1 (setLanguage): they hold the sub screens in
// English only (ss/Eng), and a system file saved with the European discs holds their language
// (3 German ...): the title then looked for ss/ger/title.dat and stopped (26.10.2026, a European save
// loaded with the debug discs).
extern "C" void re4_game_language_after_card(void)
{
    int language = re4_game_language_value();
    const char* why = "the app's choice";
    if (language < 0) {
        language = 1;
        why = "the debug build's discs";
    }
    if (pSys->language != language) {
        re4_log("language: the system file holds %s, the game keeps %s (%s)", name(pSys->language), name(language),
                why);
        setFields(language);
    }
}

// The app's "Restart" after a new language (re4_core_restart_game, any thread): the game's own soft
// reset, asked for at the next game frame (a frame hook, called in PADRead on the game thread).
// systemResetCheck runs it at the end of that frame unless a memory card access holds it (then
// later), as the reset combo does; the title then starts with setLanguage's new choice.
extern "C" void re4_game_restart(void)
{
    if (!__atomic_exchange_n(&gRestartHooked, 1, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(restartHook);
    }
    __atomic_store_n(&gRestartAsked, 1, __ATOMIC_RELEASE);
}
