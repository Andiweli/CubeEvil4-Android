// Per-frame hook of the game (PADRead calls it once per frame), compiled with
// the game's flags into libre4_main because it reads the game's own state.
//
// Zustands-Trace: one JSON line per game frame (RE4_TRACE=FILE):
//
//   {"frame":N,"Rno0":3,"room":"0x100","pl":[x,y,z],"rsf":["0x80000000"],
//    "str":[{"w":0,"id":"0x..","be":"0x0013","rno":2,"req":"0x0","vol":..,"dvd":[busy,reqs,status],"dma":0,"err":0}],
//    "em":[{"slot":1,"id":"0x12","r":[1,11,0,0],"hp":500,"pos":[x,y,z]}, ...],
//    "rnd":N,"plr":[r0,r1,r2,r3,hp],"n":[objects,events,effects,models,parts,lights,
//    dmg,sat,eat],"evt":[frame,...],
//    "gx":[12 counts of this frame],"heap":[[no,free,size],...],"hf":N}  (traceMore)
//
// Rno0 is the game task step (2 room init, 3 main loop), "rsf" the room save
// flag word of the current room (flag_rsf.h), "em" every live enemy work of
// EmMgr except the player (id 0). Every pointer is checked against MEM1 first,
// so the trace can run from the first frame on; "pl", "em" (empty), "plr", "n"
// and "evt" only from step 1 on.
//
// Run control of the local checks (port/tools/whole_r100_check.sh):
//
//   RE4_PAD_UNTIL_ROOM=1   the test script (RE4_PAD_SCRIPT) ends when the game
//                          starts loading its first room (step 2): its presses
//                          drive the title screen only
//   RE4_STOP_AFTER=N       the run ends (exit code 0) after N frames counted
//                          from the first frame of the game's main loop
//                          (step 3), with a summary line in the log
//   RE4_TEST_PLACE="F:x,y,z,a"  F frames after the room started loading (the
//                          frame RE4_PAD_UNTIL_ROOM ends the title script), the
//                          player is put at x,y,z facing a (radians), as a room
//                          script does (setPos / setAng): the checks reach a
//                          place without steering there (r100: the first
//                          house, where the s03 event starts);
//                          "free+F:x,y,z,a" counts from the moment the player
//                          is free (below)
//   RE4_TEST_SOFT_RESET="free+N"  N frames after the player is free, the
//                          soft reset of "game over" -> "no"; the log follows
//                          the game steps afterwards ("run: after the soft
//                          reset"), the title should come back
//   RE4_TEST_STREAM="free+N:B:S"  N frames after the player is free, stream S
//                          of stream block B starts as a room script starts it
//                          (SndStrReq(B, S, 0x80000003): r226's statue camera
//                          plays 0:0x16), for the sound checks (RE4_AUDIO_WAV)
//   RE4_TEST_CHEATS="free+N:A[,A...]"  N frames after the player is free, the
//                          cheat menu's buttons A (1 Killer7, 2 case L, 3 money)
//                          one a frame, as the app's menu presses them while the
//                          game runs (runtime/cheats.cpp; the switches: RE4_CHEATS)
//   RE4_PAD_FREE_SCRIPT    pad.cpp: a test script started once the player is
//                          free in the room: main loop, no event and no sub
//                          screen for 300 frames in a row ("run: player free"
//                          in the log)
//   RE4_TEST_SAVE="free+N[,B][,stop]"  N frames after the player is free,
//                          the save screen of a typewriter opens (CardSave
//                          of sce_at.cpp: card task in slot 1, mode 1), first
//                          setting the brightness option to B (the PC check
//                          finds it again after a restart: the system file);
//                          "stop": the run ends (exit code 0) when the screen
//                          has closed again
//   RE4_TEST_CARD_YES=1    in the memory card screens every choice stands on
//                          its first option (yes: make the system file, save
//                          to this file, load it), so that A alone confirms;
//                          the game itself puts the cursor on the second one
//                          (card.cpp: cMes.SetCursor(0, 1), mostly "no")
//   RE4_TEST_CONTINUE="free+N"  N frames after the player is free, "retry
//                          from a continue point" (option.cpp: GameContinue(1),
//                          also the end of a load from the pause menu); the
//                          log says when the room runs again ("run: after the
//                          retry")
//   RE4_STOP_AT_FRAME=N    the run ends (exit code 0) at frame N whatever the
//                          game is doing, with its step, room and whether a
//                          card screen is open (the card runs of the PC check
//                          must end even when a screen waits for a key)
//   RE4_TEST_WEAPON="free+N:W[,T][,stop=S]"  N frames after the player is
//                          free, weapon W (type T) is equipped the way the
//                          debug option tool does it (t_option.cpp:
//                          weaponRelease, weaponLoad, weaponInit,
//                          ItemMgr.debugWeapon; in a task of its own, as the
//                          tool menu runs, since the load waits with
//                          TaskSleep), with the debug flag for endless ammo
//                          (a reload is always possible). From then on one
//                          "wep:" line per frame: the player's routine
//                          (r_no 0..3), his motion (archive entry, frame,
//                          sequence length), the aim key, the aim pitch, the
//                          weapon object's routine and motion, and how far
//                          his left hand is from the right one (the gun).
//                          stop=S ends the run S frames after the player is
//                          free (the reload check of the PC check, M11:
//                          the Super Magnum's reload looked wrong)
//   RE4_TRACE_WEAPON=1     the same "wep:" lines in a normal game: every frame
//                          of a reload (weapon routine r_no_1 6, state
//                          r_no_2 4) and 40 frames after it, else one line
//                          when the player's routine changes while he holds
//                          a weapon: the reload as played on the device
//                          (test-core-app.ps1 -WeaponTrace)
//
// Always logged (the memory card, M10): the end of the boot card check with
// the options then in force, and a save loaded from the card. Since
// 21.10.2026 also the enemies and the menu cursor, one line per change (the
// testers' error log, core-log.txt; enemyLog / menuLog below):
//
//   em: + s12 0x1a/3 list 5 set 0x24 f 0x00020000 hp 300 at (x y z) in r209    a work comes alive
//       [its entry of the enemy list (emset_no), cEm::set and cEm::flag from the list: for
//        Ganados f bit 0x20000 = dynamite in the hand, 0x2000 = at the waist, 0x10000 = endless]
//   em: - s12 0x1a/3 hp 0 r 3/1/0/0 at (x y z) after 812 frames
//   em: hit s12 0x1a/3 hp 300 -> 100 part 5 wep 0x10 r 1/16/0/0
//       [Ganados, ids 0x10..0x1f: Be_flg, weapon kind, class, parasite core, cEm::flag]
//   em: s12 r 2/4/0/0                       its routine, for 90 frames after a hit
//   em: s12 head lost                       a Ganado's Be_flg bit 0x80 (em10LostHead)
//   em: s12 at / leaves the dynamite post (list 247)   em10 R1 0x50 FixBomber (thrower)
//   em: s12 lights a dynamite stick (2 so far)         its fuse (r_no_2 4), the first 5
//   em: s12 hidden / shown                  be_flag bit1 (drawn)
//   em: s12 jumps (x y z) -> (x y z)        more than 3000 in one frame
//   em: s12 position is not a number, last at (x y z)
//   em: s79 dynamite thrown by s27 at (x y z)       a weapon work (0x42) starts flying as dynamite
//   em: s79 dynamite exploded at (x y z)           (emwep.cpp routine 9 BombThrow; setLost after
//       [or "fell into the water", "stopped (routine N)"]    the blast, setFall in water)
//   pad: menu frame N rep 0x.. stick (x y) c (x y)   the cursor moves in a sub screen
//
// Since 22.10.2026 Leon's side of a death (playerLog below) and the sub screen and hit lines
// in the same budget per minute (before: the first 40 of a session):
//
//   room: r21d runs (frame N), Leon at (x y z) facing a, life 1200/1200, weapon 0x02[, continue after a death]
//   die: r21d after 812 frames, life 1200, Leon at (x y z); scene areas hit: 0x05 0x13
//   die: area 0x05 id 0 on 1 trg 1: floor .. (corners), for 1    each area hit, at most 8
//   pl: Leon's position is not a number in r21d (frame N, M frames after the room started)
#include "types.h"
#include "em.h"
#include "em10.h"
#include "emwep.h"
#include "global.h"
#include "player.h"
#include "cam_ctrl.h"
#include "room_data.h"
#include "sce_at.h"
#include "snd_drv.h"
#include "snd.h"
#include "id_sys.h"
#include "ss_main.h"
#include "card.h"
#include "main.h"
#include "scheduler.h"
#include "mes.h"
#include "game.h"
#include "item.h"
#include "joy.h"
#include "pl_wep.h"
#include "obj.h"
#include "event.h"
#include "espgen.h"
#include "main_mem.h"
#include "model.h"
#include "light.h"
#include "dmg.h"
#include "atari.h"
#include <dolphin/os/OSAlloc.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;
extern cEmMgr EmMgr;

// An id system ran full (runtime/log.cpp, "IDSystem::set() work full"): how many units of each class
// IdSys, IdSub and IdNum hold, so a check sees which screen part took the room.
extern "C" void re4_idsys_report(void)
{
    struct {
        const char* name;
        IDSystem* sys;
    } all[] = {{"IdSys", &IdSys}, {"IdSub", &IdSub}, {"IdNum", &IdNum}};
    for (auto& s : all) {
        int max = *(const s32*) s.sys;  // m_maxId (private, the first word)
        if (!s.sys->m_IdUnit || max <= 0 || max > 0x1000) {
            continue;
        }
        unsigned count[256] = {};
        int used = 0;
        for (int i = 0; i < max; i++) {
            if (s.sys->m_IdUnit[i].be_flag != 0xFF) {
                count[s.sys->m_IdUnit[i].classNo]++;
                used++;
            }
        }
        char line[1024];
        int n = snprintf(line, sizeof(line), "check: %s holds %d of %d units:", s.name, used, max);
        for (int c = 0; c < 256 && n < (int) sizeof(line) - 16; c++) {
            if (count[c]) {
                n += snprintf(line + n, sizeof(line) - n, " class %d: %u", c, count[c]);
            }
        }
        re4_log("%s", line);
    }
}

extern "C" void re4_cheats_install(void);  // runtime/cheats.cpp
extern "C" int re4_cheat_queue(int what);

extern "C" void re4_set_frame_hook(void (*hook)(unsigned));
extern "C" void re4_pad_script_stop(void);
extern "C" void re4_pad_script_free(void);
extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_title_room_menu_open(void);
extern "C" void re4_gx_mark(void);
extern "C" void re4_memcard_flush(int force);
extern "C" unsigned re4_rnd_state(void);          // game/rnd.cpp (patch 0081)
extern "C" unsigned re4_heap_fail_count(void);    // runtime/log.cpp (patch 0081)
extern "C" void re4_gx_stats(unsigned out[11]);   // runtime/gx_decode.cpp
extern "C" void re4_ai_set_block_hook(void (*hook)(void));  // runtime/ai.cpp
extern u32 proc_tick[32];         // game/debug.cpp (patch 0108): ProcessTickGet of the last frame
extern const char* proc_name[32];
extern int proc_tick_idx_bak;
extern "C" void re4_gx_stats2(unsigned out[9]);

namespace {

FILE* g_trace;

bool live(const void* p)
{
    unsigned long a = (unsigned long) p;
    // the managers and the player can live in the libraries (above MEM1) or in MEM1
    return a >= 0x80000000ul;
}

void vec(FILE* f, const Vec& v)
{
    fprintf(f, "[%.3f,%.3f,%.3f]", (double) v.x, (double) v.y, (double) v.z);
}

// One scene area (AEV, sce_at.cpp) as text: number, kind, enabled, trigger bits, its shape and
// who may trigger it (1 the player, 2 enemies, 8 the partner).
void areaText(const SCE_AT_DATA* w, char* out, size_t size)
{
    const AREA_HIT_DATA& a = w->area;
    int k = snprintf(out, size, "0x%02x id %u on %u trg %u: ", (unsigned) w->no, (unsigned) w->id,
                     (unsigned) (w->be_flg & 1), (unsigned) w->trg_type);
    if (k < 0 || (size_t) k >= size) {
        return;
    }
    if (a.type == AREA_TYPE_XZ4) {
        const AREA_XZ4& q = a.xz4;
        k += snprintf(out + k, size - k, "floor %.0f height %.0f, (%.0f %.0f) (%.0f %.0f) (%.0f %.0f) (%.0f %.0f)",
                      (double) q.floor, (double) q.height, (double) q.p[0].x, (double) q.p[0].z, (double) q.p[1].x,
                      (double) q.p[1].z, (double) q.p[2].x, (double) q.p[2].z, (double) q.p[3].x, (double) q.p[3].z);
    } else if (a.type == AREA_TYPE_CYLINDER) {
        const AREA_CYLINDER& c = a.cylinder;
        k += snprintf(out + k, size - k, "floor %.0f height %.0f, circle (%.0f %.0f) radius %.0f", (double) c.floor,
                      (double) c.height, (double) c.x, (double) c.z, (double) c.radius);
    } else {
        const AREA_EYE_TRIGGER& e = a.eye_trigger;
        k += snprintf(out + k, size - k, "type %u floor %.0f height %.0f, at (%.0f %.0f) radius %.0f, view %.2f %.2f "
                      "open %.2f", (unsigned) a.type, (double) e.floor, (double) e.height, (double) e.xz, (double) e.z,
                      (double) e.radius, (double) e.ang_x, (double) e.ang_y, (double) e.open_ang);
    }
    if (k > 0 && (size_t) k < size) {
        snprintf(out + k, size - k, ", for %u%s", (unsigned) w->target_type, w->pParent != 0 ? ", moves with a model" : "");
    }
}

// The room's scene areas once its main loop runs: where the room scripts'
// triggers are (r100: 0x0A starts the s03 event at the first house), for
// placing the player in the checks (RE4_TEST_PLACE).
void logAreas()
{
    unsigned count = 0;
    char text[256];
    for (SCE_AT_DATA* w = sceAtSetOtStart(); (w = sceAtGetOtAddr(w)) != 0 && count < 256; count++) {
        areaText(w, text, sizeof text);
        re4_log("run: area %s", text);
    }
}

// A work of the manager's own array (cManager::create hands out only those): the walks below
// follow cUnit::next only that far. After a soft reset the title runs with the managers of the
// last room as they were, their arrays given back with the heaps; what loads there next (the
// title's demo movie) turned the lists into garbage, and the walk read a next pointer at a wild
// address (PC check 18.10.2026: crash at the start of demo0.sfd after the soft reset, i686 and
// x86_64, fault address = register + 8, the offset of cUnit::next).
template <class T> bool inArray(const cManager<T>& m, const cUnit* u)
{
    unsigned long a = (unsigned long) u, base = (unsigned long) m.pArray;
    return live(m.pArray) && m.size != 0 && a >= base && a < base + (unsigned long) m.nArray * m.size &&
           (a - base) % m.size == 0;
}

// Live works of a manager (its alive list, cUnit::next), at most `limit`.
template <class T> int aliveCount(cManager<T>& m, int limit = 4096)
{
    int count = 0;
    for (cUnit* u = (cUnit*) m.pAlive; inArray(m, u) && count < limit; u = u->getNext()) {
        count++;
    }
    return count;
}

// More of the state for the 32/64-bit comparison of the PC check (port/tools/trace_compare.py):
// "rnd" the game's random generator (it parts first when two runs call Rnd differently), "plr"
// the player's routine and hit points, "n" the live works (objects, events, effect slots in
// use, model infos, model parts, lights, damage, collision and hit works), "evt" the total frame of each running event, "gx" what the GP got since the last frame
// (commands, draws, vertices, display list calls, XF and BP writes, textures, texture bytes,
// copies to the picture and to textures, TLUT loads, unknown commands), every 60 frames
// "heap": [number, free bytes, size] of each active heap (OSCheckHeap, which also reports a
// broken heap), and "hf" the allocations that found no room so far (patch 0081). "plr", "n" and
// "evt" only from step 1 on: in step 0 (boot, title, also after a soft reset) the player and the
// managers are what the last room left, in memory the title uses again.
unsigned g_gxLast[20];

void traceMore(FILE* f, unsigned n, GLOBAL_WK* g)
{
    fprintf(f, ",\"rnd\":%u", re4_rnd_state());
    cPlayer* pl = pPL;
    bool works = live(g) && g->Rno0 != 0;
    if (works && live(pl)) {
        fprintf(f, ",\"plr\":[%u,%u,%u,%u,%d]", (unsigned) pl->r_no_0, (unsigned) pl->r_no_1,
                (unsigned) pl->r_no_2, (unsigned) pl->r_no_3, (int) pl->hp);
    }
    if (works) {
        int esp = live(g_pEspSys) ? (int) g_pEspSys->ActiveEspNum : -1;
        fprintf(f, ",\"n\":[%d,%d,%d,%d,%d,%d,%d,%d,%d]", aliveCount(ObjMgr), aliveCount(EvtMgr), esp,
                aliveCount(ModInfoMgr), aliveCount(PartsMgr), aliveCount(LightMgr), aliveCount(DmgMgr),
                aliveCount(SatMgr), aliveCount(EatMgr));
        fprintf(f, ",\"evt\":[");
        int k = 0;
        for (cUnit* u = (cUnit*) EvtMgr.pAlive; inArray(EvtMgr, u) && k < 16; u = u->getNext(), k++) {
            fprintf(f, "%s%d", k ? "," : "", ((Event*) u)->GetNowTotalFrame());
        }
        fprintf(f, "]");
    }
    unsigned a[11], b[9];
    re4_gx_stats(a);
    re4_gx_stats2(b);
    // commands, draws, vertices, calls, xf, bp, textures, texture bytes, xfb copies, texture
    // copies, tlut loads, unknown
    const unsigned now[12] = {a[1], a[6], a[7], a[8], a[4], a[2], b[3], b[4], b[0], b[1], b[2], a[9]};
    fprintf(f, ",\"gx\":[");
    for (int i = 0; i < 12; i++) {
        fprintf(f, "%s%u", i ? "," : "", now[i] - g_gxLast[i]);
        g_gxLast[i] = now[i];
    }
    fprintf(f, "]");
    if (n % 60 == 0) {
        fprintf(f, ",\"heap\":[");
        bool first = true;
        for (int h = 0; h < MEM_HEAP_NUM; h++) {
            if (Heap[h].handle < 0 || Heap[h].status != 0) {
                continue;
            }
            long size = (long) ((u8*) Heap[h].end - (u8*) Heap[h].start);
            fprintf(f, "%s[%d,%ld,%ld]", first ? "" : ",", h, (long) OSCheckHeap(Heap[h].handle), size);
            first = false;
        }
        fprintf(f, "],\"hf\":%u", re4_heap_fail_count());
    }
    (void) g;
}

void trace(unsigned n)
{
    FILE* f = g_trace;
    if (!f) {
        return;
    }
    GLOBAL_WK* g = pG;
    if (!live(g)) {
        return;
    }
    u16 room = g->room_id;
    fprintf(f, "{\"frame\":%u,\"Rno0\":%u,\"room\":\"0x%03x\"", n, (unsigned) g->Rno0, (unsigned) room);
    // the sound streams (radio voice, music): status, state, requests, volume,
    // DVD and DMA work in flight (a stream that never fades out holds the
    // game in SndStrStopBlock)
    {
        bool any = false;
        for (int i = 0; i < SND_STR_MAX; i++) {
            const SND_STR& st = Snd_str_work[i];
            if (st.be_flag == 0) {
                continue;
            }
            fprintf(f, "%s{\"w\":%d,\"id\":\"0x%lx\",\"be\":\"0x%04x\",\"rno\":%d,\"req\":\"0x%lx\",\"vol\":%d,"
                       "\"dvd\":[%d,%d,%lu],\"dma\":%d,\"err\":%u}",
                    any ? "," : ",\"str\":[", i, (unsigned long) st.snd_id, (unsigned) st.be_flag, (int) st.rno,
                    (unsigned long) st.req_flag, (int) st.now_vol, (int) st.dvd_busy, (int) st.dvd_req_num,
                    (unsigned long) st.dvd_status, (int) st.dma_busy, (unsigned) st.err_flag);
            any = true;
        }
        if (any) {
            fprintf(f, "]");
        }
    }
    // the player and the enemies only from step 1 on (as "plr", "n", "evt" in traceMore): in step 0
    // (boot, title, also after a soft reset) they are what the last room left, in memory the title
    // uses again (compare run 18.10.2026: Leon "moved" to 3297810 in the title on one side)
    bool inGame = g->Rno0 != 0;
    cPlayer* pl = pPL;
    if (inGame && live(pl)) {
        fprintf(f, ",\"pl\":");
        vec(f, pl->pos);
    }
    u8* save = g->Rno0 >= 2 ? (u8*) RoomData.getRoomSavePtr(room) : 0;
    if (live(save)) {
        fprintf(f, ",\"rsf\":[\"0x%08lx\"]", (unsigned long) *(u32*) (save + 4));
    }
    // the camera the game draws with (pG->Camera, which CameraMove fills from
    // CamCtrl.camera when STA_CAMERA is on and the debug camera is off) and
    // the switches that decide it: C STA_CAMERA, D DBG_DBG_CAM, S SPF_CAMERA,
    // E STA_EVENT, U STA_SUB_SCRN, X extra (boss/event) camera
    {
        const CAMERA_POINT& c = g->Camera.param;
        fprintf(f, ",\"cam\":{\"eye\":");
        vec(f, c.Campos);
        fprintf(f, ",\"at\":");
        vec(f, c.Target);
        fprintf(f, ",\"fov\":%.2f,\"sw\":\"%s%s%s%s%s%s\",\"r0\":%u,\"ctrl\":", (double) c.Fovy,
                StaFlagChk(g, STA_CAMERA) ? "C" : "", DbgFlagChk(g, DBG_DBG_CAM) ? "D" : "",
                SpfFlagChk(g, SPF_CAMERA) ? "S" : "", StaFlagChk(g, STA_EVENT) ? "E" : "",
                StaFlagChk(g, STA_SUB_SCRN) ? "U" : "", CamCtrl.m_pExtraCamera ? "X" : "", (unsigned) CamCtrl.r0);
        vec(f, CamCtrl.camera.param.Campos);
        fprintf(f, "}");
    }
    fprintf(f, ",\"em\":[");
    cEm* arr = EmMgr.pArray;
    u32 n2 = EmMgr.nArray;
    u32 stride = EmMgr.size;
    bool first = true;
    if (inGame && live(arr) && stride != 0 && n2 < 4096) {
        for (u32 i = 0; i < n2; i++) {
            cEm* e = (cEm*) ((u8*) arr + i * stride);
            if (!(e->be_flag & 1) || e->id == 0) {
                continue;
            }
            fprintf(f, "%s{\"slot\":%lu,\"id\":\"0x%02x\",\"r\":[%u,%u,%u,%u],\"hp\":%d,\"pos\":", first ? "" : ",",
                    (unsigned long) i, (unsigned) e->id, (unsigned) e->r_no_0, (unsigned) e->r_no_1,
                    (unsigned) e->r_no_2, (unsigned) e->r_no_3, (int) e->hp);
            vec(f, e->pos);
            fprintf(f, "}");
            first = false;
        }
    }
    fprintf(f, "]");
    traceMore(f, n, g);
    fprintf(f, "}\n");
    fflush(f);  // a crash must not lose the last frames
}

bool g_padUntilRoom;
// RE4_TEST_PLACE: frames from the room start, or with "free+" from the moment
// the player is free
bool g_place;
bool g_placeFree;
unsigned long g_placeFrame;
float g_placePos[3], g_placeAng;
long g_roomStart = -1;  // frame the room started loading
// The player is free: main loop, no event (STA_EVENT), no sub screen
// (STA_SUB_SCRN) for kFreeFrames frames in a row (the opening event and the
// radio call follow each other within a few frames).
const unsigned kFreeFrames = 300;
unsigned g_freeRun;
long g_freeAt = -1;
unsigned long g_stopAfter;
// RE4_DEBUG_HUD=0: the debug build's own displays off (debug_mode 0, as the
// publicity build sets it in main.cpp): the event and score lines, the
// process bars, the heap numbers, the debug text (EprintfDrawing) and the
// green far fog of DBG_FOG_FAR_GREEN (game.cpp). The app sets it by default.
// While the debug start menu (patch 0043) or the tool menu (SELECT held,
// db_menu.cpp) is open, debug_mode is left on: their lines are debug text.
// The bars that come with it (DebugControl: process time bars at the left
// edge, primitive buffer and heap bars, the pad monitor; ConfigSet switches
// DBG_PROC_BAR and DBG_PAD_INFO on at boot) are kept off, every frame (wish
// of the project owner, 17.10.2026: only the text; 26.10.2026: the flickering
// bars in the tool menu again, whatever switched them on).
bool g_hideDebugHud;
unsigned g_debugBarsOff;  // times they were found on and switched off
unsigned long g_roomFrames;  // frames since the first main loop frame
unsigned long g_loopFrames;  // of them in step 3
// RE4_TEST_SOFT_RESET="free+N": N frames after the player is free, the soft
// reset the game makes after "game over" -> "no" (game.cpp, System_flg
// SYS_SOFT_RESET); the log then follows the game steps to the title
long g_resetAfter = -1;
long g_resetAt = -1;
unsigned g_resetSteps;
u8 g_resetStep;
// RE4_TEST_SAVE="free+N[,B]": the save screen N frames after the player is free
long g_saveAfter = -1;
int g_saveBrightness = -1;
bool g_saveOpened, g_saveStop, g_saveRunning;
bool g_cardYes;              // RE4_TEST_CARD_YES
long g_cheatAfter = -1;      // RE4_TEST_CHEATS
int g_cheatActions[8], g_cheatCount;
long g_streamAfter = -1;     // RE4_TEST_STREAM
int g_streamBlock, g_streamNo;
long g_continueAfter = -1;   // RE4_TEST_CONTINUE
long g_continueAt = -1;
bool g_continueLeft;         // the room stopped running after the retry
unsigned long g_stopAtFrame;  // RE4_STOP_AT_FRAME
// RE4_TEST_WEAPON="free+N:W[,T][,stop=S]"
long g_weaponAfter = -1;
int g_weaponNo, g_weaponType;
long g_weaponStop = -1;
long g_weaponAt = -1;   // the frame it was asked for
unsigned g_weaponLines;
bool g_traceWeapon;     // RE4_TRACE_WEAPON
unsigned g_weaponAfterReload;  // frames left to log after a reload
u32 g_weaponRoutine;           // r_no 0..3 of the last frame

// The weapon change of the debug option tool (t_option.cpp), in a task of
// its own: ReadWepData waits for the read with TaskSleep.
void weaponTask()
{
    cPlayer* pl = pPL;
    pl->weaponRelease();
    pl->weaponLoad(g_weaponNo, g_weaponType);
    pl->weaponInit();
    pl->r_no_0 = 0;
    pl->r_no_1 = 0;
    pl->r_no_2 = 0;
    pl->r_no_3 = 0;
    ItemMgr.debugWeapon((ITEM_ID) WeaponNo2WeaponId((u8) g_weaponNo, (u8) g_weaponType));
    re4_log("run: weapon %d (type %d) equipped (RE4_TEST_WEAPON), archive %p", g_weaponNo, g_weaponType,
            (void*) pG->pWep);
    TaskExit();
}

// Which archive entry a motion is: "wNN" (weapon archive), "pNN" (player
// archive), else the address.
const char* entryName(const void* data, char* buf, size_t size)
{
    if (!data) {
        return "-";
    }
    PlArc* arcs[2] = { live(pG->pWep) ? pG->pWep : 0, live(pG->pPlayer) ? pG->pPlayer : 0 };
    for (int k = 0; k < 2; k++) {
        PlArc* a = arcs[k];
        if (!a) {
            continue;
        }
        u32 count = a->ofs[0] < 0xFC ? a->ofs[0] : 0xFC;
        for (u32 i = 4; i < 4 + count; i++) {
            if (PL_ARC_PTR(a, i) == data) {
                snprintf(buf, size, "%c%02X", k == 0 ? 'w' : 'p', (unsigned) i);
                return buf;
            }
        }
    }
    snprintf(buf, size, "%p", data);
    return buf;
}

void weaponLine(unsigned n)
{
    cPlayer* pl = pPL;
    char m[24], om[24];
    float hand = -1.0f;
    if (live(pl->pList) && pl->nParts > 0x10) {
        Vec l = pl->getPartsPtr(0x10)->world, r = pl->getPartsPtr(0xA)->world;
        float dx = l.x - r.x, dy = l.y - r.y, dz = l.z - r.z;
        hand = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    cObjWep* obj = live(pl->Wep) ? WEP_OBJ(pl) : 0;
    bool o = live(obj);
    cItem* item = ItemMgr.weapon();
    re4_log("wep: %u pl %u/%u/%u/%u mot %s %.1f/%u kamae %d m3r %.2f | obj %u/%u mot %s %.1f | hand %.0f | "
            "ammo %d wep %u/%u | joy 0x%04x R %u", n,
            (unsigned) pl->r_no_0, (unsigned) pl->r_no_1, (unsigned) pl->r_no_2, (unsigned) pl->r_no_3,
            entryName(pl->Motion.pMot, m, sizeof(m)), (double) pl->Motion.Mot_frame,
            (unsigned) pl->Motion.Seq_frame_num, joyKamae(), (double) (f32) m3r, o ? (unsigned) obj->r_no_0 : 0u,
            o ? (unsigned) obj->r_no_1 : 0u, o ? entryName(obj->Motion.pMot, om, sizeof(om)) : "-",
            o ? (double) obj->Motion.Mot_frame : 0.0, (double) hand, live(item) ? (int) (item->bullet & 0x1FFF) : -1,
            (unsigned) pG->weapon_no, (unsigned) pG->weapon_lv_reload, (unsigned) (Joy[0].on & 0xFFFF),
            (unsigned) Joy[0].triggerRight);
}
std::atomic<unsigned> g_playerHits(0);  // re4_core_player_hits

// --- the enemies and the menu cursor for the error log (21.10.2026) ----------------------------
// A tester's reports of enemies that keep coming or vanish (castle hall r208), a head that does
// not burst (ritual room r209) and a menu cursor that moves by itself (the merchant's tune-up)
// had no word of it in the log. One line per change; at most kWindowLines lines in kWindowFrames
// frames (about a minute), so that a flood cannot push everything else out of core-log.txt
// (2 x 4 MB): the late part of a long session counts as much as the start.
const unsigned kSlots = 512, kFollowFrames = 90, kWindowFrames = 1800, kWindowLines = 1500, kMenuWindowLines = 600;
// a Ganado's "stays back" line at most every 10 s, "follows again" after 3 s and more of it
const unsigned kStayLineFrames = 300, kStayFollowFrames = 90;

struct EnemySeen {
    bool alive;
    bool shown;
    bool headLost;
    bool lost;          // its position is not a number
    u8 id, type;
    s16 hp;
    u8 r[4];
    Vec pos;
    unsigned born;
    unsigned follow;    // frames left to log routine changes (after a hit)
    unsigned visLines;  // hidden / shown lines of this life
    bool post;          // a Ganado at its dynamite thrower post (R1 0x50 FixBomber)
    u8 fuses;           // fuses it lit (lines for the first 5)
    unsigned stayFrom;  // a Ganado holding back (R1 0x1B Stay): the frame it began, and of its last line
    unsigned stayLine;
};

EnemySeen g_seen[kSlots];
const cEm* g_seenArray;
u32 g_seenCount;
u16 g_seenRoom;

bool isGanado(const cEm* e)
{
    return e->id >= 0x10 && e->id <= 0x1F;
}

// A position that is a number in all three parts (NaN or infinite: the difference is not 0). A
// work at NaN is inside every quadrilateral scene area (area.cpp areaHitCheck_xz4 only rejects).
bool finite(const Vec& v)
{
    return v.x - v.x == 0.0f && v.y - v.y == 0.0f && v.z - v.z == 0.0f;
}

// A line within the window's budget (true), else counted and left out, said once per window.
struct LineWindow {
    unsigned start, lines, skipped;
    bool take(unsigned n, unsigned limit, const char* what)
    {
        if (n - start >= kWindowFrames) {
            if (skipped) {
                re4_log("%s: %u lines left out in the last %u frames (more than %u)", what, skipped,
                        kWindowFrames, limit);
            }
            start = n;
            lines = skipped = 0;
        }
        if (lines < limit) {
            lines++;
            return true;
        }
        skipped++;
        return false;
    }
};

LineWindow g_enemyWindow, g_menuWindow;
// the sub screen and player hit lines (before: the first 40 of a session each)
LineWindow g_runWindow;
const unsigned kRunWindowLines = 120;
unsigned g_enemyFrame;  // the frame enemyLine counts for

bool enemyLine()
{
    return g_enemyWindow.take(g_enemyFrame, kWindowLines, "em");
}

void enemyHit(unsigned slot, const cEm* e, const EnemySeen& was)
{
    const YARARE_INFO* part = e->dmg.m_pDamageYarare;
    char ganado[96] = "";
    if (isGanado(e)) {
        const FREE_EM10* w = EM10_WK(e);
        snprintf(ganado, sizeof ganado, " flg 0x%08x wt %u cls %u core %u f 0x%08x", (unsigned) w->Be_flg,
                 (unsigned) w->Wep_type, (unsigned) w->Ganado, w->pCore ? 1u : 0u, (unsigned) e->flag);
    }
    re4_log("em: hit s%u 0x%02x/%u hp %d -> %d part %d wep 0x%02x r %u/%u/%u/%u%s", slot, (unsigned) e->id,
            (unsigned) e->type, (int) was.hp, (int) e->hp, live(part) ? (int) part->parts_no : -1,
            (unsigned) e->dmg.m_Wep, (unsigned) e->r_no_0, (unsigned) e->r_no_1, (unsigned) e->r_no_2,
            (unsigned) e->r_no_3, ganado);
}

// A weapon work (0x42) starts or ends its flight as dynamite (cEmWep::setBombThrow sets routine 9,
// the blast setLost routine 2, water setFall routine 4); the thrower's slot from pEm_old. Testers
// missed the dynamite Ganados in r104 (22.10.2026): these lines show whether they throw.
void dynamiteLine(unsigned slot, const cEm* e, const cEm* arr, u32 count, u32 stride)
{
    if (e->r_no_1 == 9) {
        const u8* by = (const u8*) EMWEP_WK(e)->pEm_old;
        char who[24] = "";
        if (by >= (const u8*) arr && by < (const u8*) arr + count * stride &&
            (u32) (by - (const u8*) arr) % stride == 0) {
            snprintf(who, sizeof who, " by s%u", (unsigned) ((by - (const u8*) arr) / stride));
        }
        re4_log("em: s%u dynamite thrown%s at (%.0f %.0f %.0f)", slot, who, (double) e->pos.x, (double) e->pos.y,
                (double) e->pos.z);
        return;
    }
    char what[32];
    if (e->r_no_1 == 2) {
        snprintf(what, sizeof what, "exploded");
    } else if (e->r_no_1 == 4) {
        snprintf(what, sizeof what, "fell into the water");
    } else {
        snprintf(what, sizeof what, "stopped (routine %u)", (unsigned) e->r_no_1);
    }
    re4_log("em: s%u dynamite %s at (%.0f %.0f %.0f)", slot, what, (double) e->pos.x, (double) e->pos.y,
            (double) e->pos.z);
}

// A Ganado begins to hold back (Stay): its distance to Leon, along the route, the Ganados nearer
// to him on the route (em10StayCk's count), and Leon's distance from its guard post with the
// guard radius.
void stayLine(unsigned slot, const cEm* e, const cEm* arr, u32 count, u32 stride)
{
    const FREE_EM10* w = EM10_WK(e);
    unsigned nearer = 0;
    for (u32 k = 0; k < count; k++) {
        cEm* o = (cEm*) ((const u8*) arr + k * stride);
        if (o != e && o->isAlive() && o->id > 0xF && o->id <= 0x20 && o->hp > 0 && o->checkStatus(EM_STATUS_ACTIVE) &&
            EM10_WK(o)->L_pl_route < w->L_pl_route) {
            nearer++;
        }
    }
    re4_log("em: s%u stays back (Stay): %.1f m from Leon, %.1f m on the route, %u Ganados nearer on the route; "
            "Leon %.1f m from its post, guard radius %.1f m",
            slot, sqrt((double) e->l_pl) / 1000.0, (double) w->L_pl_route / 1000.0, nearer,
            (double) w->L_pl_guard / 1000.0, (double) e->Guard_r / 1000.0);
}

void enemyLog(unsigned n, GLOBAL_WK* g)
{
    const cEm* arr = EmMgr.pArray;
    u32 count = EmMgr.nArray, stride = EmMgr.size;
    if (g->Rno0 != 3 || !live(arr) || stride == 0 || count > kSlots) {
        return;
    }
    g_enemyFrame = n;
    // a new room (or a new array): the works start over without a line each
    if (arr != g_seenArray || count != g_seenCount || g->room_id != g_seenRoom) {
        memset(g_seen, 0, sizeof g_seen);
        g_seenArray = arr;
        g_seenCount = count;
        g_seenRoom = g->room_id;
    }
    for (u32 i = 0; i < count; i++) {
        const cEm* e = (const cEm*) ((const u8*) arr + i * stride);
        EnemySeen& s = g_seen[i];
        bool alive = (e->be_flag & 1) && e->id != 0;
        if (s.alive && (!alive || e->id != s.id)) {
            if (enemyLine()) {
                re4_log("em: - s%u 0x%02x/%u hp %d r %u/%u/%u/%u at (%.0f %.0f %.0f) after %u frames", (unsigned) i,
                        (unsigned) s.id, (unsigned) s.type, (int) s.hp, (unsigned) s.r[0], (unsigned) s.r[1],
                        (unsigned) s.r[2], (unsigned) s.r[3], (double) s.pos.x, (double) s.pos.y, (double) s.pos.z,
                        n - s.born);
            }
            s.alive = false;
        }
        if (!alive) {
            continue;
        }
        bool shown = (e->be_flag & 2) != 0;
        bool headLost = isGanado(e) && (EM10_WK(e)->Be_flg & 0x80);
        if (!s.alive) {
            if (enemyLine()) {
                re4_log("em: + s%u 0x%02x/%u list %u set 0x%02x f 0x%08x hp %d at (%.0f %.0f %.0f) in r%03x%s",
                        (unsigned) i, (unsigned) e->id, (unsigned) e->type, (unsigned) e->emset_no, (unsigned) e->set,
                        (unsigned) e->flag, (int) e->hp, (double) e->pos.x, (double) e->pos.y, (double) e->pos.z,
                        (unsigned) g->room_id, shown ? "" : ", hidden");
            }
            memset(&s, 0, sizeof s);
            s.alive = true;
            s.born = n;
        } else {
            // the throw sets hp 1, the blast hp 0: one dynamite line instead of a hit line
            if (e->id == 0x42 && (e->r_no_1 == 9) != (s.r[1] == 9)) {
                if (enemyLine()) {
                    dynamiteLine(i, e, arr, count, stride);
                }
            } else if (e->hp != s.hp) {
                if (enemyLine()) {
                    enemyHit(i, e, s);
                }
                s.follow = kFollowFrames;
            } else if (s.follow && (e->r_no_0 != s.r[0] || e->r_no_1 != s.r[1] || e->r_no_2 != s.r[2] ||
                                    e->r_no_3 != s.r[3]) && enemyLine()) {
                re4_log("em: s%u r %u/%u/%u/%u", (unsigned) i, (unsigned) e->r_no_0, (unsigned) e->r_no_1,
                        (unsigned) e->r_no_2, (unsigned) e->r_no_3);
            }
            if (headLost && !s.headLost && enemyLine()) {
                re4_log("em: s%u head lost", (unsigned) i);
            }
            // the dynamite throwers (em10 R1 0x50 FixBomber, sets 0x24/0x25/0x2B): at the post,
            // each fuse lit (r_no_2 4 before em10BombThrow), when they leave it (Guard_r)
            const bool post = isGanado(e) && e->r_no_0 == 1 && e->r_no_1 == 0x50;
            if (post != s.post && enemyLine()) {
                re4_log("em: s%u %s the dynamite post (list %u)", (unsigned) i, post ? "at" : "leaves",
                        (unsigned) e->emset_no);
            }
            // a Ganado holding back (em10 R1 0x1B Stay; em10StayCk lets the nearest few on the route
            // attack and keeps the others waiting, and those whose guard area Leon left): when it
            // begins, with what the rule weighs, and after how long it follows again (tester report
            // 22.10.2026: "people stopped following, I stayed there, then I moved and they started
            // following me again")
            const bool stay = isGanado(e) && e->r_no_0 == 1 && e->r_no_1 == 0x1B;
            const bool stayed = s.r[0] == 1 && s.r[1] == 0x1B;
            if (stay && !stayed) {
                s.stayFrom = n;
                if ((!s.stayLine || n - s.stayLine >= kStayLineFrames) && enemyLine()) {
                    s.stayLine = n;
                    stayLine(i, e, arr, count, stride);
                }
            } else if (!stay && stayed && s.stayFrom && n - s.stayFrom >= kStayFollowFrames && enemyLine()) {
                re4_log("em: s%u follows again after %.1f s (r %u/%u)", (unsigned) i, (n - s.stayFrom) / 30.0,
                        (unsigned) e->r_no_0, (unsigned) e->r_no_1);
            }
            if (post && e->r_no_2 == 4 && s.r[2] != 4) {
                s.fuses++;
                if (s.fuses <= 5 && enemyLine()) {
                    re4_log("em: s%u lights a dynamite stick (%u so far)", (unsigned) i, (unsigned) s.fuses);
                }
            }
            s.post = post;
            if (shown != s.shown && s.visLines < 20 && enemyLine()) {
                s.visLines++;
                re4_log("em: s%u %s", (unsigned) i, shown ? "shown" : "hidden");
            }
            if (!finite(e->pos) && !s.lost && enemyLine()) {
                re4_log("em: s%u position is not a number, last at (%.0f %.0f %.0f)", (unsigned) i, (double) s.pos.x,
                        (double) s.pos.y, (double) s.pos.z);
            }
            float dx = e->pos.x - s.pos.x, dy = e->pos.y - s.pos.y, dz = e->pos.z - s.pos.z;
            if (dx * dx + dy * dy + dz * dz > 3000.0f * 3000.0f && enemyLine()) {
                re4_log("em: s%u jumps (%.0f %.0f %.0f) -> (%.0f %.0f %.0f)", (unsigned) i, (double) s.pos.x,
                        (double) s.pos.y, (double) s.pos.z, (double) e->pos.x, (double) e->pos.y, (double) e->pos.z);
            }
            if (s.follow) {
                s.follow--;
            }
        }
        s.id = e->id;
        s.type = e->type;
        s.hp = e->hp;
        s.r[0] = e->r_no_0;
        s.r[1] = e->r_no_1;
        s.r[2] = e->r_no_2;
        s.r[3] = e->r_no_3;
        s.pos = e->pos;
        s.shown = shown;
        s.headLost = headLost;
        s.lost = !finite(e->pos);
    }
}

// The cursor of a sub screen (inventory, merchant, map) moves on the repeat bits of the pad, the
// cross (0x0000000f), the main stick from 30 units (0x000f0000) or the C stick from 10 units
// (0x00f00000): which one, with both sticks' values.
void menuLog(unsigned n, GLOBAL_WK* g)
{
    if (!StaFlagChk(g, STA_SUB_SCRN)) {
        return;
    }
    const JOY& j = Joy[0];
    u32 dir = j.rep & 0x00FF000Fu;
    if (!dir) {
        return;
    }
    if (g_menuWindow.take(n, kMenuWindowLines, "pad: menu")) {
        re4_log("pad: menu frame %u rep 0x%08x stick (%d %d) c (%d %d) buttons 0x%04x", n, (unsigned) dir,
                (int) j.stickX, (int) j.stickY, (int) j.substickX, (int) j.substickY, (unsigned) (j.on & 0xFFFF));
    }
}

// Leon's side of a death (tester report 22.10.2026: in r21d Leon died again at once after every
// continue, the log said only "--DIEDEMO START!!"): a line when a room starts running (where
// Leon stands, life, weapon; "after a death" for the continue), when the death demo starts (life,
// place, the scene areas hit in that frame: room scripts kill through them, r21d's pistons) and
// when his position stops being a number.
struct PlayerSeen {
    bool running;    // the room's main loop (step 3) ran in the last frame
    bool dying;      // STA_DIEDEMO
    bool died;       // a death since the room started
    bool lost;       // the position is not a number
    unsigned start;  // the frame the room started running
};
PlayerSeen g_player;

void playerLog(unsigned n, GLOBAL_WK* g)
{
    if (g->Rno0 != 3 || !live(pPL)) {
        g_player.running = false;
        return;
    }
    const Vec& p = pPL->pos;
    if (!g_player.running) {
        g_player.running = true;
        g_player.dying = false;
        g_player.lost = false;
        g_player.start = n;
        re4_log("room: r%03x runs (frame %u), Leon at (%.0f %.0f %.0f) facing %.2f, life %d/%d, weapon 0x%02x%s",
                (unsigned) g->room_id, n, (double) p.x, (double) p.y, (double) p.z, (double) pPL->ang.y,
                (int) (s16) g->pl_life, (int) (s16) g->pl_life_max, (unsigned) g->weapon_no,
                g_player.died ? ", continue after a death" : "");
        g_player.died = false;
    }
    bool dying = StaFlagChk(g, STA_DIEDEMO) != 0;
    if (dying && !g_player.dying) {
        // the hit bits stay until the next scene check, which the death demo stops (SPF_SCE_AT);
        // the scene system exists once a room was set up (SceAtInit, else its table is at 0)
        bool areas = live(sceAtSetOtStart());
        char hit[160] = "";
        unsigned count = 0;
        int k = 0;
        for (u32 a = 0; areas && a < 256; a++) {
            if (SceAtHitCheck(a) && k >= 0 && (size_t) k < sizeof hit - 8) {
                k += snprintf(hit + k, sizeof hit - k, " 0x%02x", (unsigned) a);
                count++;
            }
        }
        char partner[48] = "";
        if (live(pSUB)) {
            snprintf(partner, sizeof partner, ", partner life %d", (int) (s16) g->ashley_life);
        }
        re4_log("die: r%03x after %u frames, life %d%s, Leon at (%.0f %.0f %.0f); scene areas hit:%s", (unsigned) g->room_id,
                n - g_player.start, (int) (s16) g->pl_life, partner, (double) p.x, (double) p.y, (double) p.z,
                count ? hit : " none");
        unsigned shown = 0, seen = 0;
        char text[256];
        for (SCE_AT_DATA* w = sceAtSetOtStart(); areas && (w = sceAtGetOtAddr(w)) != 0 && seen < 512 && shown < 8;
             seen++) {
            if (SceAtHitCheck(w->no)) {
                shown++;
                areaText(w, text, sizeof text);
                re4_log("die: area %s", text);
            }
        }
        g_player.died = true;
    }
    g_player.dying = dying;
    bool lost = !finite(p);
    if (lost && !g_player.lost) {
        re4_log("pl: Leon's position is not a number in r%03x (frame %u, %u frames after the room started)",
                (unsigned) g->room_id, n, n - g_player.start);
    }
    g_player.lost = lost;
}

// --- the sound driver's requests (patch 0107) ---
//
//   sound: up to N requests for one audio frame (bank of 256, the GameCube's 64): M audio frames
//       over 64, K full, in r105
//
// Before each audio frame (ai.cpp re4_ai_set_block_hook) the requests pushed since the last one
// wait in Snd_req_work[req_push_idx]. More than 64 would have filled the GameCube's bank, whose
// full bank refuses a new sound without a word (suspected cause of the silent death cries in
// busy fights, device test 22.10.2026 23:41). A line every 10 s while it happens.
unsigned g_sndPeak, g_sndOver, g_sndFull, g_sndOverLogged, g_sndFullLogged;

void sndBankLook()
{
    const int bank = Snd_ctrl_work.req_push_idx & 1;
    unsigned used = 0;
    for (int i = 0; i < SND_REQ_MAX; i++) {
        used += Snd_req_work[bank][i].be_flag != 0;
    }
    g_sndPeak = used > g_sndPeak ? used : g_sndPeak;
    g_sndOver += used > 64;
    g_sndFull += used >= SND_REQ_MAX;
}

void sndLog(unsigned n, GLOBAL_WK* g)
{
    static bool hooked;
    if (!hooked) {
        hooked = true;
        re4_ai_set_block_hook(sndBankLook);
    }
    if (n % 300 != 0) {
        return;
    }
    if ((g_sndOver != g_sndOverLogged || g_sndFull != g_sndFullLogged) &&
        g_runWindow.take(n, kRunWindowLines, "run")) {
        re4_log("sound: up to %u requests for one audio frame (bank of %u, the GameCube's 64): %u audio frames over "
                "64, %u full, in r%03x",
                g_sndPeak, (unsigned) SND_REQ_MAX, g_sndOver, g_sndFull, (unsigned) g->room_id);
        g_sndOverLogged = g_sndOver;
        g_sndFullLogged = g_sndFull;
    }
    g_sndPeak = 0;
}

// --- the sections of a frame (patch 0108) ---
//
//   frame: 600 frames in r105: logic 6.1 ms, drawing 9.8 ms, rest 2.0 ms, frame end 1.2 ms,
//       waiting 14.2 ms (averages; the longest frame 41.0 ms)
//
// The game's own timing (main.cpp ProcessTickGet, wall time from the frame's start): logic = the
// task scheduler (Leon, enemies, effects, cameras, the room), drawing = IdSys, Trans (the models'
// GX commands, which the port's graphics decoder converts at once) up to SndWatcher, rest = text,
// the cinema bars and the ordering table (more GX commands), frame end = Render_done and the
// checks, waiting = for the retrace (the frame's pace). Every 600 frames of play (wishes of the
// project owner, 22.10.2026: where a weak device's time goes).
struct FrameSections {
    double logic, drawing, rest, end, waiting, longest;
    unsigned frames;
};
FrameSections g_sections;

void sectionLog(unsigned n, GLOBAL_WK* g)
{
    // the last frame's marks: 4 setup, 5 TaskScheduler, 6 SndWatcher, 2 CPU, 3 draw remain, 0 total
    if (proc_tick_idx_bak != 2 || !proc_name[5] || !proc_name[6] || strcmp(proc_name[5], "TaskScheduler") != 0 ||
        strcmp(proc_name[6], "SndWatcher") != 0 || g->Rno0 != 3) {
        return;
    }
    const double ms = 1000.0 / (162000000.0 / 4);  // OSGetTick: the bus clock / 4
    const u32* t = proc_tick;
    if (!(t[4] <= t[5] && t[5] <= t[6] && t[6] <= t[2] && t[2] <= t[3] && t[3] <= t[0])) {
        return;
    }
    FrameSections& s = g_sections;
    s.logic += (t[5] - t[4]) * ms;
    s.drawing += (t[6] - t[5]) * ms;
    s.rest += (t[2] - t[6]) * ms;
    s.end += (t[3] - t[2]) * ms;
    s.waiting += (t[0] - t[3]) * ms;
    s.longest = t[0] * ms > s.longest ? t[0] * ms : s.longest;
    if (++s.frames < 600) {
        return;
    }
    if (g_runWindow.take(n, kRunWindowLines, "run")) {
        const double f = s.frames;
        re4_log("frame: %u frames in r%03x: logic %.1f ms, drawing %.1f ms, rest %.1f ms, frame end %.1f ms, "
                "waiting %.1f ms (averages; the longest frame %.1f ms)",
                s.frames, (unsigned) g->room_id, s.logic / f, s.drawing / f, s.rest / f, s.end / f, s.waiting / f,
                s.longest);
    }
    s = FrameSections();
}

void frame(unsigned n)
{
    trace(n);
    GLOBAL_WK* g = pG;
    if (g_stopAtFrame && n >= g_stopAtFrame) {
        bool ok = live(g);
        re4_log("run: stopped at frame %u (RE4_STOP_AT_FRAME): step %u, room r%03x, card screen %s", n,
                ok ? (unsigned) g->Rno0 : 0u, ok ? (unsigned) g->room_id : 0u,
                ok && SysFlagChk(g, SYS_TYPEWRITER) ? "open" : "closed");
        re4_memcard_flush(1);
        if (g_trace) {
            fflush(g_trace);
        }
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
    if (!live(g)) {
        return;
    }
    if (g_cardYes && SysFlagChk(g, SYS_TYPEWRITER)) {
        cMes.SetCursor(0, 0);
    }
    // the debug start menu (patch 0043) draws its lines with the debug text;
    // the tool menu (SELECT held in the app, patch 0061; db_menu.cpp DbMenuExec) sets its
    // own page and puts the old one back when it closes (DBG_TEST_MODE)
    if (g_hideDebugHud && (DbgFlagChk(g, DBG_PROC_BAR) || DbgFlagChk(g, DBG_PAD_INFO))) {
        DbgFlagOff(g, DBG_PROC_BAR);
        DbgFlagOff(g, DBG_PAD_INFO);
        if (g_debugBarsOff++ == 0) {
            re4_log("run: the debug bars (process time, pad monitor) switched off (RE4_DEBUG_HUD=0)");
        } else if (g_debugBarsOff <= 10) {
            re4_log("run: the debug bars were on again, switched off (r%03x, tool menu %s)", g->room_id,
                    DbgFlagChk(g, DBG_TEST_MODE) ? "open" : "closed");
        }
    }
    if (g_hideDebugHud && !DbgFlagChk(g, DBG_TEST_MODE)) {
        int page = re4_title_room_menu_open() ? 1 : 0;
        if ((g->debug_mode != 0) != (page != 0)) {
            g->debug_mode = page;
        }
    }
    if (g_roomStart < 0 && g->Rno0 >= 2) {
        g_roomStart = (long) n;
    }
    // the room camera once the first room runs: a fall camera CameraControl::roomInit
    // built must survive the fill of its storage (patch 0044; before, its vtable
    // pointer read 0x09090909 and the next camera cut crashed)
    static bool cameraLogged;
    if (!cameraLogged && g->Rno0 == 3) {
        cameraLogged = true;
        const void* proc = CamCtrl.m_pProc;
        re4_log("run: camera after the room start: routine %u, camera object %s", (unsigned) CamCtrl.r0,
                !proc ? "none" : *(const u32*) proc == 0x09090909u ? "DESTROYED (storage filled over it)" : "intact");
    }
    // sub screens (inventory, map, radio): when they open and close (the map
    // stayed black on the Retroid and could not be closed)
    static bool subOpen;
    bool sub = StaFlagChk(g, STA_SUB_SCRN) != 0;
    if (sub != subOpen && g_runWindow.take(n, kRunWindowLines, "run")) {
        re4_log("run: sub screen %s at frame %u", sub ? "open" : "closed", n);
    }
    subOpen = sub;
    sndLog(n, g);
    sectionLog(n, g);
    enemyLog(n, g);
    menuLog(n, g);
    playerLog(n, g);
    // hits on the player (life goes down in the game): a dark bar showed at the
    // bottom of the picture only then; the app records the pictures after a hit
    // (re4_core_player_hits, core_mode.cpp RE4_GX_DUMP_ON_HIT)
    static int lastLife = -1;
    static long lastHit = -1000;
    static int marks = -1;
    if (marks < 0) {
        const char* v = getenv("RE4_GX_SHOTS_ON_HIT");
        marks = v ? atoi(v) : 0;
    }
    if (g->Rno0 == 3) {
        int life = (int) g->pl_life;
        if (lastLife >= 0 && life < lastLife) {
            g_playerHits++;
            if (g_runWindow.take(n, kRunWindowLines, "run")) {
                re4_log("run: player hit, life %d -> %d at frame %u", lastLife, life, n);
            }
            // a new hit (not the next frame of the same grab or burn): the GX chunk
            // of this moment for the app (RE4_GX_SHOTS_ON_HIT, core_mode.cpp)
            if (marks > 0 && (long) n - lastHit > 60) {
                marks--;
                re4_gx_mark();
                re4_log("run: hit marked for the pictures (frame %u)", n);
            }
            lastHit = (long) n;
        }
        lastLife = life;
    } else {
        lastLife = -1;
    }
    if (g_roomStart >= 0 && g_freeAt < 0) {
        bool free = g->Rno0 == 3 && !StaFlagChk(g, STA_EVENT) && !StaFlagChk(g, STA_SUB_SCRN);
        g_freeRun = free ? g_freeRun + 1 : 0;
        if (g_freeRun == kFreeFrames) {
            g_freeAt = (long) n;
            re4_log("run: player free (no event, no sub screen for %u frames) at frame %u, %ld after the room start",
                    kFreeFrames, n, (long) n - g_roomStart);
            re4_pad_script_free();
        }
    }
    long placeBase = g_placeFree ? g_freeAt : g_roomStart;
    if (g_place && placeBase >= 0 && n == placeBase + g_placeFrame && live(pPL) && g->Rno0 == 3) {
        g_place = false;
        Vec pos = { g_placePos[0], g_placePos[1], g_placePos[2] };
        Vec ang = { 0.0f, g_placeAng, 0.0f };
        pPL->setPos(&pos);
        pPL->setAng(&ang);
        re4_log("run: player placed at (%.0f %.0f %.0f), facing %.2f, frame %u", (double) pos.x, (double) pos.y,
                (double) pos.z, (double) g_placeAng, n);
    }
    if (g_weaponAfter >= 0 && g_freeAt >= 0 && g_weaponAt < 0 && (long) n >= g_freeAt + g_weaponAfter &&
        g->Rno0 == 3 && live(pPL) && TaskStatus(2) == 0) {
        g_weaponAt = (long) n;
        DbgFlagOn(g, DBG_INF_BULLET2);
        TaskExec(2, (TaskFunc) weaponTask, 0);
        re4_log("run: weapon %d asked for (RE4_TEST_WEAPON) at frame %u, endless ammo on", g_weaponNo, n);
    }
    if (g_weaponAt >= 0 && (long) n > g_weaponAt && g_weaponLines < 3000 && live(pPL) && g->Rno0 == 3) {
        g_weaponLines++;
        weaponLine(n);
    } else if (g_traceWeapon && live(pPL) && g->Rno0 == 3 && g_weaponLines < 20000) {
        cPlayer* pl = pPL;
        u32 routine = (u32) pl->r_no_0 << 24 | (u32) pl->r_no_1 << 16 | (u32) pl->r_no_2 << 8 | pl->r_no_3;
        bool reloading = pl->r_no_1 == 6 && pl->r_no_2 == 4;
        bool changed = routine != g_weaponRoutine && (pl->r_no_1 == 6 || (g_weaponRoutine >> 16 & 0xFF) == 6);
        if (reloading) {
            g_weaponAfterReload = 40;
        }
        if (reloading || changed || g_weaponAfterReload) {
            g_weaponAfterReload -= !reloading && g_weaponAfterReload;
            g_weaponLines++;
            weaponLine(n);
        }
        g_weaponRoutine = routine;
    }
    // A reload level past the three columns of PlReloadEndTbl (pl_class.cpp;
    // the debug menu's weapon page goes up to RELOAD Lv.5): the handgun,
    // shotgun, rifle and machine gun routines then read the end frame from
    // the next weapon's row, often 1, so letting go of R cuts the reload
    // short and the weapon's own reload motion plays on alone. The GameCube
    // reads the same; the hint tells a tester's log what it was.
    {
        static u32 lastLevel = ~0u;
        const u32 level = (u32) g->weapon_no << 8 | g->weapon_lv_reload;
        if (g->Rno0 == 3 && level != lastLevel) {
            lastLevel = level;
            const u32 at = (u32) g->weapon_no * 3 + g->weapon_lv_reload;
            if (g->weapon_lv_reload > 2 && at < 45 * 3) {
                re4_log("check: weapon %u has reload level %u (Lv.%u, the debug menu's weapon page): the reload "
                        "table has 3 levels, the end frame comes from the next weapon's row (%.0f), so letting go "
                        "of R cuts the reload short (as on the GameCube)",
                        (unsigned) g->weapon_no, (unsigned) g->weapon_lv_reload, (unsigned) g->weapon_lv_reload + 1,
                        (double) ((const f32*) PlReloadEndTbl)[at]);
            }
        }
    }
    if (g_weaponStop >= 0 && g_freeAt >= 0 && (long) n >= g_freeAt + g_weaponStop) {
        re4_log("run: stopped %ld frames after the player was free (RE4_TEST_WEAPON stop)", g_weaponStop);
        if (g_trace) {
            fflush(g_trace);
        }
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
    if (g_resetAfter >= 0 && g_freeAt >= 0 && g_resetAt < 0 && (long) n == g_freeAt + g_resetAfter) {
        g_resetAt = (long) n;
        g_resetStep = g->Rno0;
        SysFlagOn(g, SYS_SOFT_RESET);
        re4_log("run: soft reset requested (RE4_TEST_SOFT_RESET) at frame %u, step %u", n, (unsigned) g->Rno0);
    }
    if (g_cheatAfter >= 0 && g_freeAt >= 0 && (long) n >= g_freeAt + g_cheatAfter &&
        (long) n < g_freeAt + g_cheatAfter + g_cheatCount) {
        int what = g_cheatActions[n - (g_freeAt + g_cheatAfter)];
        re4_log("run: cheat button %d (RE4_TEST_CHEATS) at frame %u", what, n);
        re4_cheat_queue(what);
    }
    if (g_streamAfter >= 0 && g_freeAt >= 0 && (long) n == g_freeAt + g_streamAfter) {
        u32 id = SndStrReq(g_streamBlock, g_streamNo, (int) 0x80000003, 0, 0, 0.0f);
        re4_log("run: stream %d:0x%x started (RE4_TEST_STREAM) at frame %u: id %u", g_streamBlock, g_streamNo, n,
                (unsigned) id);
    }
    if (g_resetAt >= 0 && g->Rno0 != g_resetStep && g_resetSteps < 16) {
        g_resetStep = g->Rno0;
        g_resetSteps++;
        re4_log("run: after the soft reset: step %u at frame %u (%ld frames later)", (unsigned) g->Rno0, n,
                (long) n - g_resetAt);
    }
    // the memory card (M10): the boot check's end with the options of the
    // system file, a save loaded from the card
    static bool checkDone;
    if (!checkDone && (g->CardStatus & 0x80000000u) && live(pSys)) {
        checkDone = true;
        re4_log("run: memory card check done at frame %u (CardStatus 0x%08x): options language %u, brightness %u, "
                "controller layout %u, sound mode %u, config 0x%08x",
                n, (unsigned) g->CardStatus, (unsigned) pSys->language, (unsigned) pSys->brightness,
                (unsigned) pSys->pad_type, (unsigned) pSys->SndMode, (unsigned) pSys->Config_flg);
    }
    static bool loaded;
    if ((g->CardStatus & 4) && !loaded) {
        re4_log("run: save loaded from the memory card at frame %u: room r%03x, saved %u times, brightness %u", n,
                (unsigned) g->room_id, (unsigned) g->save_cnt, live(pSys) ? (unsigned) pSys->brightness : 0u);
    }
    loaded = (g->CardStatus & 4) != 0;
    if (g_saveOpened && TaskStatus(1) != 0) {
        g_saveRunning = true;
    } else if (g_saveRunning) {
        g_saveRunning = false;
        re4_log("run: save screen closed at frame %u (CardStatus 0x%08x, saved %u times)", n, (unsigned) g->CardStatus,
                (unsigned) g->save_cnt);
        if (g_saveStop) {
            re4_log("run: stopped after the save (RE4_TEST_SAVE)");
            re4_memcard_flush(1);
            if (g_trace) {
                fflush(g_trace);
            }
            fflush(stdout);
            fflush(stderr);
            _exit(0);
        }
    }
    if (g_saveAfter >= 0 && g_freeAt >= 0 && !g_saveOpened && (long) n >= g_freeAt + g_saveAfter && g->Rno0 == 3) {
        if (TaskStatus(1) == 0) {
            g_saveOpened = true;
            if (g_saveBrightness >= 0 && live(pSys)) {
                pSys->brightness = (u8) g_saveBrightness;
            }
            // CardSave(0, 1) without its TaskSleep (this hook runs in PADRead)
            g->terminal_no = 0;
            TaskExec(1, (TaskFunc) CardMainTask, (void*) 1);
            re4_log("run: save screen opened (RE4_TEST_SAVE) at frame %u in room r%03x, player at (%.0f %.0f %.0f), "
                    "brightness %u, saved %u times so far",
                    n, (unsigned) g->room_id, live(pPL) ? (double) pPL->pos.x : 0.0, live(pPL) ? (double) pPL->pos.y : 0.0,
                    live(pPL) ? (double) pPL->pos.z : 0.0, live(pSys) ? (unsigned) pSys->brightness : 0u,
                    (unsigned) g->save_cnt);
        }
    }
    if (g_continueAfter >= 0 && g_freeAt >= 0 && g_continueAt < 0 && (long) n >= g_freeAt + g_continueAfter &&
        g->Rno0 == 3 && !StaFlagChk(g, STA_SUB_SCRN) && !StaFlagChk(g, STA_EVENT)) {
        g_continueAt = (long) n;
        re4_log("run: retry from the continue point (RE4_TEST_CONTINUE) at frame %u in room r%03x", n,
                (unsigned) g->room_id);
        GameContinue(1);
    } else if (g_continueAt >= 0 && !g_continueLeft && g->Rno0 != 3) {
        g_continueLeft = true;
    } else if (g_continueLeft && g->Rno0 == 3) {
        g_continueLeft = false;
        g_continueAfter = -1;
        re4_log("run: after the retry: room r%03x runs again at frame %u (%ld frames later)", (unsigned) g->room_id,
                n, (long) n - g_continueAt);
    }
    if (g_padUntilRoom && g->Rno0 >= 2) {
        re4_pad_script_stop();
        g_padUntilRoom = false;
    }
    if (g_stopAfter == 0 || (g_roomFrames == 0 && g->Rno0 != 3)) {
        return;
    }
    if (g_roomFrames == 0) {
        re4_log("run: main loop reached at frame %u in room r%03x", n, (unsigned) g->room_id);
        logAreas();
    }
    g_roomFrames++;
    g_loopFrames += g->Rno0 == 3;
    if (g_roomFrames >= g_stopAfter) {
        re4_log("run: stopped after %lu frames from the main loop on (%lu in step 3), room r%03x, step %u "
                "(RE4_STOP_AFTER)",
                g_roomFrames, g_loopFrames, (unsigned) g->room_id, (unsigned) g->Rno0);
        re4_memcard_flush(1);
        if (g_trace) {
            fflush(g_trace);
        }
        fflush(stdout);
        fflush(stderr);
        _exit(0);
    }
}

void configure()
{
    const char* path = getenv("RE4_TRACE");
    if (path && *path && !g_trace) {
        g_trace = fopen(path, "w");
    }
    const char* until = getenv("RE4_PAD_UNTIL_ROOM");
    g_padUntilRoom = until && *until && *until != '0';
    const char* place = getenv("RE4_TEST_PLACE");
    if (place && *place) {
        g_placeFree = strncmp(place, "free+", 5) == 0;
        g_place = sscanf(place + (g_placeFree ? 5 : 0), "%lu:%f,%f,%f,%f", &g_placeFrame, &g_placePos[0],
                         &g_placePos[1], &g_placePos[2], &g_placeAng) == 5;
        if (!g_place) {
            re4_log("run: RE4_TEST_PLACE \"%s\" not understood ([free+]F:x,y,z,a)", place);
        }
    }
    const char* reset = getenv("RE4_TEST_SOFT_RESET");
    if (reset && strncmp(reset, "free+", 5) == 0) {
        g_resetAfter = strtol(reset + 5, 0, 10);
    }
    const char* save = getenv("RE4_TEST_SAVE");
    if (save && strncmp(save, "free+", 5) == 0) {
        char* end = 0;
        g_saveAfter = strtol(save + 5, &end, 10);
        if (end && *end == ',' && end[1] != 's') {
            g_saveBrightness = (int) strtol(end + 1, &end, 0);
        }
        g_saveStop = end && strstr(end, ",stop") != 0;
    }
    // the cheat menu's switches and buttons in every game frame (runtime/cheats.cpp)
    re4_cheats_install();
    const char* cheats = getenv("RE4_TEST_CHEATS");
    if (cheats && strncmp(cheats, "free+", 5) == 0) {
        char* end = 0;
        g_cheatAfter = strtol(cheats + 5, &end, 10);
        g_cheatCount = 0;
        while (end && *end && g_cheatCount < 8) {
            g_cheatActions[g_cheatCount++] = (int) strtol(end + 1, &end, 10);
        }
    }
    const char* stream = getenv("RE4_TEST_STREAM");
    if (stream && strncmp(stream, "free+", 5) == 0 &&
        sscanf(stream + 5, "%ld:%i:%i", &g_streamAfter, &g_streamBlock, &g_streamNo) != 3) {
        re4_log("run: RE4_TEST_STREAM \"%s\" not understood (free+N:B:S)", stream);
        g_streamAfter = -1;
    }
    const char* retry = getenv("RE4_TEST_CONTINUE");
    if (retry && strncmp(retry, "free+", 5) == 0) {
        g_continueAfter = strtol(retry + 5, 0, 10);
    }
    const char* traceWeapon = getenv("RE4_TRACE_WEAPON");
    g_traceWeapon = traceWeapon && *traceWeapon && *traceWeapon != '0';
    const char* weapon = getenv("RE4_TEST_WEAPON");
    if (weapon && strncmp(weapon, "free+", 5) == 0) {
        char* end = 0;
        g_weaponAfter = strtol(weapon + 5, &end, 10);
        g_weaponNo = end && *end == ':' ? (int) strtol(end + 1, &end, 0) : -1;
        g_weaponType = 0;
        if (end && *end == ',' && end[1] != 's') {
            g_weaponType = (int) strtol(end + 1, &end, 0);
        }
        const char* stopAt = end ? strstr(end, "stop=") : 0;
        g_weaponStop = stopAt ? strtol(stopAt + 5, 0, 10) : -1;
        if (g_weaponNo < 0) {
            g_weaponAfter = -1;
            re4_log("run: RE4_TEST_WEAPON \"%s\" not understood (free+N:W[,T][,stop=S])", weapon);
        }
    }
    const char* yes = getenv("RE4_TEST_CARD_YES");
    g_cardYes = yes && *yes && *yes != '0';
    const char* stopAt = getenv("RE4_STOP_AT_FRAME");
    g_stopAtFrame = stopAt ? strtoul(stopAt, 0, 10) : 0;
    const char* stop = getenv("RE4_STOP_AFTER");
    g_stopAfter = stop ? strtoul(stop, 0, 10) : 0;
    const char* freeScript = getenv("RE4_PAD_FREE_SCRIPT");
    const char* hud = getenv("RE4_DEBUG_HUD");
    g_hideDebugHud = hud && *hud == '0';
    // (always: the memory card lines)
    (void) freeScript;
    re4_set_frame_hook(frame);
}

struct Install {
    Install() { configure(); }
} g_install;

}  // namespace

// The frame the player became free (-1: not yet), for runtime/core_api.cpp
// (RE4_GX_DUMP_FREE).
extern "C" unsigned re4_core_player_hits(void)
{
    return g_playerHits.load();
}

// The debug build's own displays are shown (RE4_DEBUG_HUD not 0): the sub screens' debug editors
// open only then (patch 0093; with the displays hidden they were invisible editors on the cross).
extern "C" int re4_debug_hud_shown(void)
{
    return !g_hideDebugHud;
}

extern "C" long re4_trace_free_at(void)
{
    return g_freeAt;
}

// One frame's sections into the averages (runtime test "sections", game_probe.cpp).
extern "C" void re4_trace_sections(unsigned n)
{
    if (live(pG)) {
        sectionLog(n, pG);
    }
}

// The sound request count before an audio frame and its counters (peak, over 64, full), for the
// runtime test "sndbank" (game_probe.cpp re4probe_snd_bank); `reset` clears them first.
extern "C" void re4_trace_snd_look(int reset, unsigned out[3])
{
    if (reset) {
        g_sndPeak = g_sndOver = g_sndFull = 0;
    }
    sndBankLook();
    out[0] = g_sndPeak;
    out[1] = g_sndOver;
    out[2] = g_sndFull;
}

// The enemy and menu lines of one frame on their own (runtime test "enemylog", game_probe.cpp).
extern "C" void re4_state_trace_enemy_frame(unsigned n)
{
    if (live(pG)) {
        enemyLog(n, pG);
        menuLog(n, pG);
        playerLog(n, pG);
    }
}

// Reads the switches again (the app sets them after the library is loaded,
// runtime/core_api.cpp re4_core_start).
extern "C" void re4_state_trace_reconfigure(void)
{
    configure();
}

// Starts (path) or ends (0) the trace at run time (runtime tests).
extern "C" int re4_state_trace_open(const char* path)
{
    if (g_trace) {
        fclose(g_trace);
        g_trace = 0;
    }
    if (path) {
        g_trace = fopen(path, "w");
    }
    re4_set_frame_hook(frame);  // (the hook stays: the memory card lines)
    return g_trace != 0;
}
