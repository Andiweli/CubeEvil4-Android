// The game's language with the European discs (port/docs/retail-eu-plan.md, R7). Compiled with
// game flags; all calls run on the game thread.
//
// The European release has every language on its discs: the six-language message tables (core,
// rooms, memory card, codec) and the folders ss/Eng, ss/Ger, ss/Fra, ss/Esp, ss/Ita. Its setLanguage
// (European main.dol 0x80125354) starts from the console's system language (OSGetLanguage + 2:
// 2 English, 3 German, 4 French, 5 Spanish, 6 Italian; Dutch is English), its title menu has a
// "LANGUAGE SELECT". The debug build fixes language 1 (the US release: no event subtitles, two
// difficulties). Decisions of the project owner (26.10.2026): the language is chosen in the app's
// in-game menu, the phone's language until then, "LANGUAGE SELECT" leaves the title menu
// (patch 0121), a change applies at once.
//
// The app's choice comes as re4_core_language_wanted (runtime/core_api.cpp: RE4_GAME_LANGUAGE at the
// start, re4_core_set_language while the game runs). Patch 0121 asks here at boot (setLanguage),
// after the system file of the memory card was read (it holds the language it was saved with) and
// every frame of the title and of play (re4_game_language_tick). A change switches the message
// language at once; the sub screens read their language folder when they open, the title its
// title.dat when it starts; the option menu's data (SS/<lang>/option.dat) is read again in play,
// where no option screen is open (gameMainLoop runs in Rno0 3 only).
#include "types.h"
#include "global.h"
#include "main.h"
#include "mes.h"
#include "read.h"

extern "C" int re4_disc_retail_eu(void);       // runtime/dvd.cpp
extern "C" int re4_core_language_wanted(void);  // runtime/core_api.cpp: 2..6, or -1
extern "C" void re4_log(const char* fmt, ...);

namespace {

bool gOptionStale;  // option.dat is of another language than pSys->language

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

}  // namespace

// The language the game gets with the European discs (2..6), -1 with the debug build's discs.
extern "C" int re4_game_language_value(void)
{
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

// Every frame of the title (inPlay 0) and of play (inPlay 1): a changed choice of the app.
extern "C" void re4_game_language_tick(int inPlay)
{
    int language = re4_game_language_value();
    if (language < 0) {
        return;
    }
    if (pSys->language != language) {
        re4_log("language: %s -> %s (%s)", name(pSys->language), name(language), inPlay ? "in play" : "title");
        setFields(language);
        cMes.setLanguage(language);
        gOptionStale = true;
    }
    if (gOptionStale && inPlay) {
        gOptionStale = false;
        OptionDataRead();
    }
}
