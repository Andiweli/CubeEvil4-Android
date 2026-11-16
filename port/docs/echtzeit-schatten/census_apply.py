#!/usr/bin/env python3
# realtime-shadows stage 4, a measurement only (never committed): what the owner tags' objects are.
#  - core (runtime/gx_owner.cpp): RE4_OWNER_CENSUS=1 logs every object of an ordering-table entry once: the room, the
#    table, the pointer and its class (figure, EmMgr prop with its name, ObjMgr work with construct id / kind / type /
#    motion / be_flag, effect, other with the function's symbol)
#  - renderer (gx_core.cpp Draw): RE4_GX_OWNER_STATS=1 logs per picture, per owner object: triangles of solid
#    (depth-written) and other draws, the view-space centre and radius
#   census_apply.py TREE
import sys

tree = sys.argv[1]


def patch(path, anchor, insert, before=False):
    p = tree + "/" + path
    s = open(p, encoding="utf-8").read()
    if "OWNER_CENSUS" in s or "OWNER_STATS" in s:
        if insert.strip()[:40] in s:
            print("already:", path)
            return
    assert s.count(anchor) == 1, (path, anchor[:50], s.count(anchor))
    s = s.replace(anchor, insert + anchor if before else anchor + insert)
    open(p, "w", encoding="utf-8").write(s)
    print("patched:", path)


census = r'''
// --- realtime-shadows stage 4 census (measurement only) ---
#include "obj.h"
#include "global.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <set>
namespace {
void censusNote(void* data, void* func, int table)
{
    static const bool on = getenv("RE4_OWNER_CENSUS") && *getenv("RE4_OWNER_CENSUS") == '1';
    static std::set<const void*> seen;
    static unsigned room = 0xFFFF;
    if (!on || !data)
        return;
    if (pG && pG->room_id != room) {
        room = pG->room_id;
        seen.clear();
    }
    if (seen.size() > 6000 || !seen.insert(data).second)
        return;
    char what[200];
    const u8* a = (const u8*) EmMgr.pArray;
    const u8* o = (const u8*) ObjMgr.pArray;
    if (a && (const u8*) data >= a && (const u8*) data < a + (size_t) EmMgr.nArray * EmMgr.size) {
        const cEm* em = (const cEm*) data;
        snprintf(what, sizeof(what), "EM id 0x%02x %s be 0x%08x motion %d shd %u", em->id,
                 em->id < 96 ? cEmMgr::idName[em->id] : "?", (unsigned) em->be_flag, em->Motion.pMot != 0,
                 (unsigned) em->Shd_color);
    } else if (o && (const u8*) data >= o && (const u8*) data < o + (size_t) ObjMgr.nArray * ObjMgr.size) {
        const cObj* ob = (const cObj*) data;
        snprintf(what, sizeof(what), "OBJ id %u kind %u type %u be 0x%08x motion %d ot %u blk %d pos %.0f %.0f %.0f scale %.2f",
                 ob->id, ob->kindid, ob->type, (unsigned) ob->be_flag, ob->Motion.pMot != 0, ob->ot_type, (int) ob->blk,
                 (double) ob->pos.x, (double) ob->pos.y, (double) ob->pos.z, (double) ob->scale.x);
    } else {
        Dl_info info;
        const char* sym = dladdr(func, &info) && info.dli_sname ? info.dli_sname : "?";
        snprintf(what, sizeof(what), "OTHER func %s", sym);
    }
    re4_log("census: r%03x t%02d %08x %s", pG ? (unsigned) pG->room_id : 0u, table, (unsigned) (size_t) data, what);
}
}  // namespace
'''
patch("port/whole/runtime/gx_owner.cpp", 'extern "C" void re4_gx_owner(void* data, void* func, int table)\n', census, True)
patch("port/whole/runtime/gx_owner.cpp", 'extern "C" void re4_gx_owner(void* data, void* func, int table)\n{\n',
      "    censusNote(data, func, table);\n")

stats_decl = r'''  // realtime-shadows stage 4 owner stats (measurement only)
  struct OwnerStat { double tris = 0, solid = 0, cx = 0, cy = 0, cz = 0, n = 0, r2 = 0; unsigned table = 0; };
  std::unordered_map<std::uint32_t, OwnerStat> ownerStats;
  void OwnerStatNote(const Re4GxDraw &d, const Re4GxChunk &chunk);
'''
patch("port/android-decomp/app/src/main/cpp/gx_core.h", "  void SetShadows(int level);\n", stats_decl, True)
stats_impl = r'''
// realtime-shadows stage 4 owner stats (measurement only)
void GxCore::OwnerStatNote(const Re4GxDraw &d, const Re4GxChunk &chunk) {
  if ((d.primitive & 0xFF) != RE4_GX_TRIANGLES || (xfReg[0x26] & 1) || ownerWords[2] == 0xFF || d.stride < 12)
    return;
  OwnerStat &s = ownerStats[ownerWords[0]];
  s.table = ownerWords[2];
  const std::uint32_t z = bp[0x40];
  s.tris += d.indexCount / 3;
  if ((z & 1) && ((z >> 4) & 1))
    s.solid += d.indexCount / 3;
  unsigned mtxAt = 12;
  if (d.layout & RE4_GX_V_NORMAL)
    mtxAt += (d.layout & RE4_GX_V_BINORMAL_TANGENT) ? 36 : 12;
  mtxAt += ((d.layout & RE4_GX_V_COLOR0) ? 4 : 0) + ((d.layout & RE4_GX_V_COLOR1) ? 4 : 0);
  for (int t = 0; t < 8; t++)
    mtxAt += (d.layout & (RE4_GX_V_TEX0 << t)) ? 8 : 0;
  for (std::uint32_t v = 0; v < d.vertexCount; v += 4) {
    const std::uint8_t *at = chunk.vertices + d.vertexOffset + static_cast<std::size_t>(v) * d.stride;
    float pos[3];
    std::memcpy(pos, at, 12);
    unsigned row = d.matrices[0] & 63;
    if ((d.layout & RE4_GX_V_MATRIX) && mtxAt < d.stride)
      row = at[mtxAt] & 63;
    if (row > 61)
      row = 0;
    double e[3];
    for (int r = 0; r < 3; r++)
      e[r] = xf.posTex[row + r][0] * pos[0] + xf.posTex[row + r][1] * pos[1] + xf.posTex[row + r][2] * pos[2] +
             xf.posTex[row + r][3];
    s.cx += e[0];
    s.cy += e[1];
    s.cz += e[2];
    s.r2 += e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    s.n += 1;
  }
}
'''
patch("port/android-decomp/app/src/main/cpp/gx_core.cpp", "void GxCore::SetShadows(int level)", stats_impl, True)
patch("port/android-decomp/app/src/main/cpp/gx_core.cpp",
      "  // a figure's triangles for the real-time shadows (debug builds; with the in-between picture's blended matrices)\n",
      "  {\n    static const bool ownerStatsOn = std::getenv(\"RE4_GX_OWNER_STATS\") && *std::getenv(\"RE4_GX_OWNER_STATS\") == '1';\n"
      "    if (ownerStatsOn && !merge)\n      OwnerStatNote(d, chunk);\n  }\n", True)
patch("port/android-decomp/app/src/main/cpp/gx_core.cpp", "  m.RunEnd();  // the GPU timer, the fence after the chunk (GxStats::behind*)\n",
      "  if (!m.ownerStats.empty()) {\n"
      "    for (auto &kv : m.ownerStats) {\n"
      "      const OwnerStat &s = kv.second;\n"
      "      const double cx = s.cx / s.n, cy = s.cy / s.n, cz = s.cz / s.n;\n"
      "      const double r = std::sqrt(std::max(0.0, s.r2 / s.n - (cx * cx + cy * cy + cz * cz)));\n"
      "      GXLOG(\"ownerstat: %08x table %03x tris %.0f solid %.0f dist %.0f radius %.0f\", kv.first, s.table, s.tris, s.solid,\n"
      "            std::sqrt(cx * cx + cy * cy + cz * cz), r);\n"
      "    }\n"
      "    m.ownerStats.clear();\n"
      "  }\n", True)
