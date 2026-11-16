#!/usr/bin/env python3
# realtime-shadows stage 4, a measurement only (never committed): RE4_SHADOW_WHERE=1 logs once per object where the
# props, cloth and moving room models of a room are ("where: rROOM WHAT x y z"), to place Leon beside them.
#   where_apply.py TREE
import sys

p = sys.argv[1] + "/port/whole/runtime/gx_owner.cpp"
s = open(p, encoding="utf-8").read()
if "RE4_SHADOW_WHERE" in s:
    print("already")
    sys.exit(0)
code = r'''
#include "global.h"
#include <stdio.h>
#include <stdlib.h>
namespace {
void whereNote(void* data, void* func)
{
    static const bool on = getenv("RE4_SHADOW_WHERE") && *getenv("RE4_SHADOW_WHERE") == '1';
    static const void* seen[4096];
    static int n;
    static unsigned room = 0xFFFF;
    if (!on || !data || !pG)
        return;
    if (pG->room_id != room) {
        room = pG->room_id;
        n = 0;
    }
    for (int i = 0; i < n; i++)
        if (seen[i] == data)
            return;
    if (n < 4096)
        seen[n++] = data;
    if (func == (void*) clothTrans) {
        const Cloth* c = (const Cloth*) data;
        fprintf(stderr, "where: r%x CLOTH %.0f %.0f %.0f\n", room, c->center.x, c->center.y, c->center.z);
        return;
    }
    const u8* a = (const u8*) EmMgr.pArray;
    if (a && (const u8*) data >= a && (const u8*) data < a + (size_t) EmMgr.nArray * EmMgr.size) {
        const cEm* em = (const cEm*) data;
        if (em->id >= 0x41 && em->id <= 0x50)
            fprintf(stderr, "where: r%x EM_%s %.0f %.0f %.0f\n", room, cEmMgr::idName[em->id], em->pos.x, em->pos.y,
                    em->pos.z);
        return;
    }
    const u8* o = (const u8*) ObjMgr.pArray;
    if (o && (const u8*) data >= o && (const u8*) data < o + (size_t) ObjMgr.nArray * ObjMgr.size) {
        const cObj* ob = (const cObj*) data;
        if ((ob->kindid == 2 && (ob->type == 1 || ob->type == 2 || (ob->be_flag & 0x20))) || ob->id == 24 ||
            (ob->id == 2 && ob->kindid == 1))
            fprintf(stderr, "where: r%x OBJ_%u_%u_%u %.0f %.0f %.0f\n", room, ob->id, ob->kindid, ob->type, ob->pos.x,
                    ob->pos.y, ob->pos.z);
    }
}
}  // namespace
'''
anchor = 'extern "C" void re4_gx_owner(void* data, void* func, int table)\n{\n'
assert s.count(anchor) == 1
s = s.replace(anchor, code + anchor + "    whereNote(data, func);\n")
open(p, "w", encoding="utf-8").write(s)
print("patched")
