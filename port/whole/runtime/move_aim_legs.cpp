// "Move and aim", the legs (wish of the project owner, 27.11.2026: "Leon soll beim Zielen richtig gehen", for Leon, Ada,
// HUNK, Krauser and Wesker): the game has no walking aim motions, so while move_aim.cpp moves the aiming player, his
// legs play his own walk under the aim. Patch 0262 calls re4_move_aim_legs once a frame in cPlayer::move, after his
// routine set the aim's pose (MotionMove: the main motion, the aim's up/down blend in pMotionB, the legs' IK) and
// before the frame's last matrices (partsWorldCalc); the bones above the pelvis stay as the aim has them.
//
// The skeleton (the same numbers for every player character, the footstep table pl_fs_tbl and the Ganados use them):
// 0 the root, 1..5 spine to head (the aim's waist twist turns 1 and 2, cPlBody::waistMove), 0x11 the pelvis (on the
// root), 0x12/0x16 the thighs, 0x13/0x17 the knees, 0x14/0x18 the feet, 0x15/0x19 the toes. The walks keep the legs as
// IK chains (motion kind 0x20 on the thighs: the foot's place in the model's space, the knee found by ik.cpp, the foot
// put onto the floor).
//
// Layers, each a motion of the player's own table (the weapon module's, so every weapon and character walks its own
// way; the damaged ones when the game picks them, dmMotCk):
//  - forward: the walk (m_MotTbl 2/3), the pelvis turned towards a diagonal by up to 45 degrees;
//  - backward: the back step (8/9), the same turn;
//  - sideways: the walk (or the back step past a right angle) with the pelvis turned 45 degrees at most, so pure
//    sideways walks diagonal steps and the feet slide sideways a little (1.1.6, the project owner's choice "variant 3"
//    of the three shown; the 90 degree turn of 27.11.2026 looked unnatural to him, the turn on the spot's steps was the
//    third). The steps' speed follows the part of the move along the turned walk.
// Each layer runs on its own MOTION_INFO (MotionSetCore with Mot_flag 0x20000000: the player's pMotionB stays,
// 0x10000000: IKInit not on the model, 0x40000000: the model's matrix left alone), on the pelvis and the legs only (the
// other bones skipped by motParts 0x20000000, the legs without the shared key history by 0x04000000 meanwhile), then its
// IK chains (IKInit's bits of that motion, the aim's bits put back afterwards) as MotionMove solves them. Its speed: the
// motion's own stride (the root's distance over a cycle, Pos_dist) against the player's speed, so the feet keep their
// place on the ground. The layers fade in and out over 8 frames and are blended with the aim's legs in the model's
// space (rotation by quaternions, place linearly). Nothing changes outside the aim with a move of the stick: the game's
// legs as before.
#include "types.h"
#include "global.h"
#include "em.h"
#include "model.h"
#include "player.h"
#include "motion.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_move_aim_moved(float* x, float* y, float* speed);
extern "C" int re4_move_aim_aiming(void);

namespace {

const int kPelvis = 0x11, kLegLast = 0x19, kLegs = kLegLast - kPelvis + 1;
const int kMaxParts = 128;
const u32 kIkBits = 0x4u | 0x10u | 0x300u | 0xE000u;  // IKInit's bits of a chain's root
const u32 kSkip = 0x20000000u, kNoHist = 0x04000000u;
const float kStep = 1.0f / 8.0f;  // the fade: 8 frames in and out
const float kPi = 3.14159265f;
const float kMaxTurn = kPi / 4.0f;  // the legs' turn against his facing: at most 45 degrees (1.1.6, variant 3)

inline bool isLeg(int i)
{
    return i >= kPelvis && i <= kLegLast;
}

enum { kWalk, kBack, kLayers };
const char* const kName[kLayers] = {"walk", "back step"};

struct Layer {
    MOTION_INFO w;
    void* data;
    u32 ik[kMaxParts];
    float stride;   // the root's distance over one cycle (units)
    float weight;   // its share of the legs now
    float target;
    float speed;    // sequence frames a game frame
    Mtx pose[kLegs];
};
Layer gL[kLayers];
float gYaw;          // the legs' turn about the model's y (radians; minus: to his right)
bool gTrace;         // RE4_TRACE_LEGS=1: one "legs:" line a frame while they walk
float gSpeedK = 1.0f; // RE4_LEGS_SPEED (tests): the legs' speed times this
unsigned gTraceLines;
int gChecked;        // the skeleton: 0 not yet, 1 fits, -1 not (the legs stay the game's)
const void* gCheckedModel;
Vec gLastPos;        // his place at this point of the last frame
float gActual;       // the speed he really moved (units a frame, smoothed): slopes and walls slow him
unsigned gLastFrame;

void trace(cPlayer* pl, const char* what)
{
    if (!gTrace || gTraceLines >= 20000) {
        return;
    }
    gTraceLines++;
    const Vec& r = pl->getPartsPtr(0x14)->world;
    const Vec& l = pl->getPartsPtr(0x18)->world;
    re4_log("legs: frame %u pos %.1f %.1f %.1f feet %.1f %.1f %.1f | %.1f %.1f %.1f w %.2f %.2f %.2f %.2f yaw %.2f %s",
            (unsigned) pG->Frame_cnt, (double) pl->pos.x, (double) pl->pos.y, (double) pl->pos.z, (double) r.x,
            (double) r.y, (double) r.z, (double) l.x, (double) l.y, (double) l.z, (double) gL[0].weight,
            (double) gL[1].weight, 0.0, 0.0, (double) gYaw, what);
}

// The skeleton above: the pelvis on the root, two chains of four under it
bool skeletonFits(cPlayer* pl)
{
    if (pl->nParts <= kLegLast) {
        return false;
    }
    auto parentOf = [pl](int i) -> int {
        cParts* q = pl->getPartsPtr(i);
        for (int j = 0; q && j < pl->nParts && j < kMaxParts; j++) {
            if ((void*) pl->getPartsPtr(j) == (void*) q->pParent) {
                return j;
            }
        }
        return -1;
    };
    static const int want[kLegs][2] = {{0x11, 0x00}, {0x12, 0x11}, {0x13, 0x12}, {0x14, 0x13}, {0x15, 0x14},
                                       {0x16, 0x11}, {0x17, 0x16}, {0x18, 0x17}, {0x19, 0x18}};
    for (const auto& w : want) {
        if (parentOf(w[0]) != w[1]) {
            return false;
        }
    }
    return true;
}

void motionOf(cPlayer* pl, int layer, void** data, void** seq)
{
    void** t = pl->m_MotTbl;
    const bool dm = dmMotCk() != 0;
    static const int tbl[kLayers][4] = {{2, 3, 0x61, 0x62}, {8, 9, 0x63, 0x64}};
    const int* e = tbl[layer];
    *data = dm ? t[e[0]] : t[e[2]];
    *seq = dm ? t[e[1]] : t[e[3]];
    if (!*data) {
        *data = dm ? t[e[2]] : t[e[0]];
        *seq = dm ? t[e[3]] : t[e[1]];
    }
}

// The aim's IK bits of every bone (kept), then `bits` (or none) in their place
void ikSwap(cPlayer* pl, u32* keep, const u32* bits)
{
    for (int i = 0; i < pl->nParts && i < kMaxParts; i++) {
        cParts* q = pl->getPartsPtr(i);
        if (!q) {
            continue;
        }
        if (keep) {
            keep[i] = q->motParts.flags & kIkBits;
        }
        q->motParts.flags = (q->motParts.flags & ~kIkBits) | (bits ? bits[i] : 0u);
    }
}

void setLayer(cPlayer* pl, Layer& L, int no, void* data, void* seq)
{
    memset(&L.w, 0, sizeof(L.w));
    L.w.Mot_flag = 0x70000000;
    MotionSetCore(pl, &L.w, data, seq, 0, 4, 0);
    L.data = data;
    L.stride = sqrtf(L.w.Pos_dist.x * L.w.Pos_dist.x + L.w.Pos_dist.z * L.w.Pos_dist.z);
    // its IK chains on the legs (IKInit marks them on the model; the aim's marks back at once)
    u32 aim[kMaxParts];
    ikSwap(pl, aim, 0);
    IKInit(pl, &L.w);
    int chains = 0;
    for (int i = 0; i < pl->nParts && i < kMaxParts; i++) {
        cParts* q = pl->getPartsPtr(i);
        L.ik[i] = q && isLeg(i) ? q->motParts.flags & kIkBits : 0u;
        chains += (L.ik[i] & 4) ? 1 : 0;
    }
    ikSwap(pl, 0, aim);
    re4_log("move and aim (legs): %s of player type %d, weapon %d: %d frames, stride %.0f, %d IK chains", kName[no],
            (int) pG->pl_type, (int) pG->weapon_no, (int) L.w.Seq_frame_num, (double) L.stride, chains);
}

// Model-space matrix of bone i: inverse(model) * its world matrix
void modelSpace(cPlayer* pl, int i, Mtx out)
{
    Mtx inv;
    PSMTXInverse(pl->mat, inv);
    PSMTXConcat(inv, pl->getPartsPtr(i)->mat, out);
}

// One layer's legs this frame, in the model's space, turned by `yaw` about the pelvis' upright
void evalLayer(cPlayer* pl, Layer& L, float yaw)
{
    const int n = pl->nParts < kMaxParts ? pl->nParts : kMaxParts;
    u32 had[kMaxParts];
    for (int i = 0; i < n; i++) {
        cParts* p = pl->getPartsPtr(i);
        if (!p) {
            continue;
        }
        had[i] = p->motParts.flags & (kSkip | kNoHist);
        p->motParts.flags |= isLeg(i) ? kNoHist : kSkip;
    }
    L.w.Seq_speed = L.speed;
    MotionMoveCore(pl, &L.w, 0);
    MotionSequenceCtrl(&L.w);
    for (int i = 0; i < n; i++) {
        cParts* p = pl->getPartsPtr(i);
        if (!p) {
            continue;
        }
        p->motParts.flags = (p->motParts.flags & ~(kSkip | kNoHist)) | had[i];
        if (isLeg(i)) {
            p->matCalc();
        }
    }
    // its IK chains as MotionMove solves them (the model's scale out of its matrix meanwhile)
    u32 aim[kMaxParts];
    ikSwap(pl, aim, L.ik);
    Mtx save, tmp;
    PSMTXCopy(pl->mat, save);
    if (pl->scale.x != 0.0f && pl->scale.y != 0.0f && pl->scale.z != 0.0f) {
        PSMTXScale(tmp, 1.0f / pl->scale.x, 1.0f / pl->scale.y, 1.0f / pl->scale.z);
    } else {
        PSMTXIdentity(tmp);
    }
    PSMTXConcat(pl->mat, tmp, tmp);
    PSMTXCopy(tmp, pl->mat);
    pl->partsWorldCalc();
    InverseKinematics(pl, 1);
    PSMTXCopy(save, pl->mat);
    pl->partsWorldCalc();
    ikSwap(pl, 0, aim);
    for (int i = kPelvis; i <= kLegLast; i++) {
        modelSpace(pl, i, L.pose[i - kPelvis]);
    }
    if (yaw != 0.0f) {
        // about the upright through the pelvis
        Mtx t0, r, t1, m;
        const f32 px = L.pose[0][0][3], pz = L.pose[0][2][3];
        PSMTXTrans(t0, -px, 0.0f, -pz);
        PSMTXRotRad(r, 'y', yaw);
        PSMTXTrans(t1, px, 0.0f, pz);
        PSMTXConcat(r, t0, m);
        PSMTXConcat(t1, m, m);
        for (int k = 0; k < kLegs; k++) {
            Mtx turned;
            PSMTXConcat(m, L.pose[k], turned);
            PSMTXCopy(turned, L.pose[k]);
        }
    }
}

// a + (b - a) * t for two bone matrices: rotation by quaternions, place and scale linearly
void blend(const Mtx a, const Mtx b, float t, Mtx out)
{
    Vec sa, sb;
    Mtx ra, rb;
    const f32 ta[3] = {a[0][3], a[1][3], a[2][3]}, tb[3] = {b[0][3], b[1][3], b[2][3]};  // `out` may be `a`
    for (int c = 0; c < 3; c++) {
        const f32 la = sqrtf(a[0][c] * a[0][c] + a[1][c] * a[1][c] + a[2][c] * a[2][c]);
        const f32 lb = sqrtf(b[0][c] * b[0][c] + b[1][c] * b[1][c] + b[2][c] * b[2][c]);
        (&sa.x)[c] = la;
        (&sb.x)[c] = lb;
        for (int r = 0; r < 3; r++) {
            ra[r][c] = la > 0.0f ? a[r][c] / la : 0.0f;
            rb[r][c] = lb > 0.0f ? b[r][c] / lb : 0.0f;
        }
        ra[c][3] = rb[c][3] = 0.0f;
    }
    Quaternion qa, qb, q;
    C_QUATMtx(&qa, ra);
    C_QUATMtx(&qb, rb);
    C_QUATSlerp(&qa, &qb, &q, t);
    PSMTXQuat(out, &q);
    for (int c = 0; c < 3; c++) {
        const f32 s = (&sa.x)[c] + ((&sb.x)[c] - (&sa.x)[c]) * t;
        for (int r = 0; r < 3; r++) {
            out[r][c] *= s;
        }
        out[c][3] = ta[c] + (tb[c] - ta[c]) * t;
    }
}

void resetLayers()
{
    for (Layer& L : gL) {
        L.weight = L.target = L.speed = 0.0f;
    }
    gYaw = 0.0f;
}

struct Init {
    Init()
    {
        const char* tr = getenv("RE4_TRACE_LEGS");
        gTrace = tr && *tr == '1';
        const char* k = getenv("RE4_LEGS_SPEED");
        if (k && *k) {
            gSpeedK = strtof(k, 0);
        }
    }
} gInit;

}  // namespace

// Patch 0262 (cPlayer::move, after his routine): the legs while he moves with the aim
void re4_move_aim_legs(cPlayer* pl)
{
    float x = 0.0f, y = 0.0f, v = 0.0f;
    const bool moved = re4_move_aim_moved(&x, &y, &v) != 0;
    if (!pl || pl != pPL || !pl->m_MotTbl) {
        return;
    }
    // the speed he really moves: his place now (this frame's move done, the last frame's walls and slopes in it)
    // against the last frame's
    {
        const unsigned f = (unsigned) pG->Frame_cnt;
        const float dx = pl->pos.x - gLastPos.x, dz = pl->pos.z - gLastPos.z;
        const float d = f == gLastFrame + 1 ? sqrtf(dx * dx + dz * dz) : v;
        gActual = gActual > 0.0f ? gActual * 0.5f + d * 0.5f : d;
        gLastPos = pl->pos;
        gLastFrame = f;
    }
    if (!re4_move_aim_aiming()) {
        resetLayers();  // the aim is over (or never was): the game's legs at once
        if (pl->nParts > kLegLast) {
            trace(pl, "game");
        }
        return;
    }
    if (gCheckedModel != (const void*) pl->pList) {
        gCheckedModel = (const void*) pl->pList;
        gChecked = skeletonFits(pl) ? 1 : -1;
        if (gChecked < 0) {
            re4_log("move and aim (legs): player type %d has another skeleton, the legs stay as the aim has them",
                    (int) pG->pl_type);
        }
    }
    if (gChecked < 0) {
        return;
    }
    // the layers wanted: the stick's direction a (0 forward, plus to his right)
    float want[kLayers] = {0.0f, 0.0f};
    float yaw = 0.0f;   // the legs' direction against his facing (plus: to his right)
    if (moved && v > 0.0f) {
        if (gActual > 0.0f && gActual < v) {
            v = gActual;
        }
        const float a = atan2f(x, y), aa = fabsf(a), sgn = a < 0.0f ? -1.0f : 1.0f;
        // up to a right angle the walk, beyond it the back step; the pelvis turned towards the direction, at most 45
        // degrees (1.1.6, the project owner's choice "variant 3": pure sideways becomes diagonal steps, the feet slide
        // sideways a little; before, up to 90 degrees)
        if (aa <= kPi / 2.0f) {
            want[kWalk] = 1.0f;
            yaw = a;
        } else {
            want[kBack] = 1.0f;
            yaw = a - sgn * kPi;
        }
        yaw = yaw > kMaxTurn ? kMaxTurn : yaw < -kMaxTurn ? -kMaxTurn : yaw;
        // each layer's speed: the part of his move along its legs, against its stride
        for (int k = 0; k < kLayers; k++) {
            Layer& L = gL[k];
            if (want[k] <= 0.0f) {
                continue;
            }
            void *data, *seq;
            motionOf(pl, k, &data, &seq);
            if (!data) {
                want[k] = 0.0f;
                continue;
            }
            if (data != L.data) {
                setLayer(pl, L, k, data, seq);
            }
            const float dir = k == kBack ? yaw + kPi : yaw;
            const float along = fabsf(v * cosf(a - dir));
            if (L.stride < 1.0f) {
                L.speed = 0.8f * v / (0.6f * 52.0f);  // no stride in the data: about the walk's own
            } else {
                L.speed = gSpeedK * along * (float) L.w.Seq_frame_num / L.stride;
            }
        }
    }
    // the fade
    float sum = 0.0f;
    for (int k = 0; k < kLayers; k++) {
        Layer& L = gL[k];
        L.target = want[k];
        const float d = L.target - L.weight;
        L.weight += d > kStep ? kStep : d < -kStep ? -kStep : d;
        if (L.target <= 0.0f) {
            L.speed *= 0.85f;  // fading out: its steps slow down
        }
        if (L.weight <= 0.0f) {
            L.weight = 0.0f;
        }
        sum += L.weight;
    }
    gYaw += (-yaw - gYaw) * 0.25f;  // the model's y turn: minus is to his right
    if (sum <= 0.0f) {
        gYaw = 0.0f;
        return;
    }
    // the aim's legs, then each layer's; blended in the model's space
    Mtx out[kLegs];
    for (int i = kPelvis; i <= kLegLast; i++) {
        modelSpace(pl, i, out[i - kPelvis]);
    }
    float acc = sum < 1.0f ? 1.0f - sum : 0.0f;
    for (int k = 0; k < kLayers; k++) {
        Layer& L = gL[k];
        if (L.weight <= 0.0f || !L.data) {
            continue;
        }
        evalLayer(pl, L, gYaw);
        acc += L.weight;
        const float t = L.weight / acc;
        for (int j = 0; j < kLegs; j++) {
            blend(out[j], L.pose[j], t, out[j]);
        }
    }
    // back onto the bones (parents first: the pelvis, then each chain downwards)
    for (int i = kPelvis; i <= kLegLast; i++) {
        cParts* p = pl->getPartsPtr(i);
        Mtx inv;
        PSMTXConcat(pl->mat, out[i - kPelvis], p->mat);
        PSMTXInverse(p->pParent->mat, inv);
        PSMTXConcat(inv, p->mat, p->l_mat);
        p->world.x = p->mat[0][3];
        p->world.y = p->mat[1][3];
        p->world.z = p->mat[2][3];
    }
    trace(pl, "aim");
}
