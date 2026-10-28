// What a movie does on the device, for the log (M9): game/sofdec.cpp calls
// re4_movie_status after every playback step (cSofdec::appMain, patch 0054)
// and re4_movie_end when the movie is finished (finishMovie). Every 5 s of
// playback and at the end one line "movie: ..." with the player's time, the
// frames handed out, and the frames the CRI player skipped because decoding
// or the display fell behind (mwPlyGetNumSkipDec / GetNumSkipDisp: on a fast
// enough device both stay 0, as on the GameCube). Compiled with the game's
// flags (re4_gamehooks_obj).
#include "mwply.h"

#include <string.h>

extern "C" void re4_log(const char* fmt, ...);

namespace {

int gSteps;
int gFrames;
int gLastFno = -1;
char gName[0x40];
int gLastSkipDec, gLastSkipDisp;

void report(MWPLY hn, const char* what, int stat)
{
    int count = 0, scale = 1;
    mwPlyGetTime(hn, &count, &scale);
    gLastSkipDec = mwPlyGetNumSkipDec(hn);
    gLastSkipDisp = mwPlyGetNumSkipDisp(hn);
    re4_log("movie: %s %s, %d steps, %d frames, time %d.%02d s, skipped %d decode / %d display, status %d", gName,
            what, gSteps, gFrames, scale > 0 ? count / scale : 0, scale > 0 ? (int) ((long long) count * 100 / scale % 100) : 0,
            gLastSkipDec, gLastSkipDisp, stat);
}

}  // namespace

extern "C" void re4_movie_status(MWPLY hn, const char* fname, const MWS_FRM* frm, int stat)
{
    if (gSteps == 0) {
        strncpy(gName, fname ? fname : "?", sizeof(gName) - 1);
        gFrames = 0;
        gLastFno = -1;
        re4_log("movie: %s started", gName);
    }
    gSteps++;
    if (frm && frm->bufadr != NULL && frm->fno != gLastFno) {
        gLastFno = frm->fno;
        gFrames++;
    }
    if (gSteps % 300 == 0) {  // 5 s at 60 steps a second
        report(hn, "playing", stat);
    }
}

extern "C" void re4_movie_end(MWPLY hn, int skipped)
{
    if (gSteps > 0 && hn) {
        report(hn, skipped ? "skipped (START)" : "ended", mwPlyGetStat(hn));
    }
    gSteps = 0;
}

// The opening movie's subtitles with the European discs (patch 0136, game/sofdec.cpp, as their
// cSofdec does it). Their movie/opening.sfd carries a table in the header's private stream (0xBF):
// tags of 8 bytes, a length of 7 hex digits and the value ("CRITAGS" / "USRINFS" TYPE SUBTITLE,
// MAXCH, per channel "CHDAT" with CHNO, TUNIT and "TIMEDAT" INTIME / DURTIME / CONTENT, the number
// of the text in etc/movie.das). Their CRI library looks the time up in the playing movie; this
// one predates that, so the table is read from the header the game reads anyway (initApp) and
// looked up against the player's time. The channel is the game's language (3 German .. 6 Italian;
// English, 2, has none, as on the European GameCube).
namespace {

struct Subtitle {
    long long in, dur;
    int content;
};
Subtitle gSubs[64];
int gSubCount;
long long gSubUnit = 1;
int gSubLast = -1;

// a tag's value as a number (decimal text)
long long tagNumber(const unsigned char* v, int len)
{
    long long n = 0;
    for (int i = 0; i < len && v[i] >= '0' && v[i] <= '9'; i++) {
        n = n * 10 + (v[i] - '0');
    }
    return n;
}

// walks the tags in [pos, end): containers are entered; returns false on a malformed tag
bool walkTags(const unsigned char* b, int pos, int end, int channel, int* chno, Subtitle* cur)
{
    while (pos + 16 <= end) {
        char name[9];
        memcpy(name, b + pos, 8);
        name[8] = 0;
        int len = 0;
        for (int i = 8; i < 15; i++) {
            char c = (char) b[pos + i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) {
                return false;
            }
            len = len * 16 + d;
        }
        int val = pos + 16;
        if (len < 0 || val + len > end) {
            return false;
        }
        const unsigned char* v = b + val;
        if (!strcmp(name, "CRITAGE")) {
            return true;
        } else if (!strcmp(name, "CRITAGS") || !strcmp(name, "USRINFS")) {
            if (!walkTags(b, val, val + len, channel, chno, cur)) {
                return false;
            }
        } else if (!strcmp(name, "CHDAT")) {
            *chno = -1;
            if (!walkTags(b, val, val + len, channel, chno, cur)) {
                return false;
            }
        } else if (!strcmp(name, "TIMEDAT")) {
            Subtitle s = {0, 0, -1};
            if (!walkTags(b, val, val + len, channel, chno, &s)) {
                return false;
            }
            if (*chno == channel && s.content >= 0 && gSubCount < 64) {
                gSubs[gSubCount++] = s;
            }
        } else if (!strcmp(name, "TYPE") && strncmp((const char*) v, "SUBTITLE", 8)) {
            return true;  // user data of another kind
        } else if (!strcmp(name, "CHNO")) {
            *chno = (int) tagNumber(v, len);
        } else if (!strcmp(name, "TUNIT") && *chno == channel) {
            gSubUnit = tagNumber(v, len);
        } else if (!strcmp(name, "INTIME") && cur) {
            cur->in = tagNumber(v, len);
        } else if (!strcmp(name, "DURTIME") && cur) {
            cur->dur = tagNumber(v, len);
        } else if (!strcmp(name, "CONTENT") && cur) {
            cur->content = (int) tagNumber(v, len);
        }
        pos = val + len;
    }
    return true;
}

}  // namespace

// The subtitles of channel `channel` from a movie's header (its first `size` bytes, an MPEG
// program stream): the number found (0: none for this channel or no table).
extern "C" int re4_movie_subtitles(const void* head, int size, int channel)
{
    static unsigned char tags[0x4000];
    const unsigned char* d = (const unsigned char*) head;
    int n = 0;
    gSubCount = 0;
    gSubUnit = 1;
    gSubLast = -1;
    // the private stream's payload in order (MPEG-1 packs of 12 bytes, PES packets with a length)
    for (int i = 0; i + 6 <= size;) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            int sid = d[i + 3];
            if (sid == 0xBA) {
                i += (d[i + 4] >> 4) == 2 ? 12 : 14 + (i + 13 < size ? d[i + 13] & 7 : 0);
                continue;
            }
            if (sid >= 0xBB) {
                int len = (d[i + 4] << 8) | d[i + 5];
                if (sid == 0xBF) {
                    int take = len;
                    if (i + 6 + take > size) {
                        take = size - i - 6;
                    }
                    if (n + take > (int) sizeof(tags)) {
                        take = (int) sizeof(tags) - n;
                    }
                    memcpy(tags + n, d + i + 6, take);
                    n += take;
                }
                i += 6 + len;
                continue;
            }
        }
        i++;
    }
    int at = -1;
    for (int i = 0; i + 8 <= n; i++) {
        if (!memcmp(tags + i, "CRITAGS", 8)) {
            at = i;
            break;
        }
    }
    int chno = -1;
    if (at < 0 || !walkTags(tags, at, n, channel, &chno, 0) || gSubUnit <= 0) {
        gSubCount = 0;
    }
    if (gSubCount) {
        re4_log("movie: %d subtitles for language %d (time unit %lld), the first at %lld", gSubCount, channel, gSubUnit,
                gSubs[0].in);
    } else if (at >= 0) {
        re4_log("movie: a subtitle table, none for language %d", channel);
    }
    return gSubCount;
}

// The text number of the subtitle shown now (the player's time), or -1.
extern "C" int re4_movie_subtitle(MWPLY hn)
{
    if (!gSubCount || !hn) {
        return -1;
    }
    int count = 0, scale = 1;
    mwPlyGetTime(hn, &count, &scale);
    if (scale <= 0) {
        return -1;
    }
    long long t = (long long) count * gSubUnit / scale;
    int now = -1;
    for (int i = 0; i < gSubCount; i++) {
        if (t >= gSubs[i].in && t < gSubs[i].in + gSubs[i].dur) {
            now = gSubs[i].content;
        }
    }
    if (now != gSubLast) {
        if (now >= 0) {
            re4_log("movie: subtitle %d from %d.%02d s", now, count / scale, (int) ((long long) count * 100 / scale % 100));
        }
        gSubLast = now;
    }
    return now;
}
