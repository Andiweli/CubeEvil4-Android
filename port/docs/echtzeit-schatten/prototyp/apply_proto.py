#!/usr/bin/env python3
# realtime-shadows study: puts the throw-away shadow prototype into a WSL tree's renderer (never into the repo).
#   python3 apply_proto.py TREE
import os
import sys

tree = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
cpp = os.path.join(tree, "port/android-decomp/app/src/main/cpp")


def snippet(name):
    with open(os.path.join(here, name), encoding="utf-8") as f:
        return f.read().replace("\r", "")


def patch(path, pairs):
    full = os.path.join(cpp, path)
    with open(full, encoding="utf-8") as f:
        text = f.read()
    if "realtime-shadows study" in text:
        print("already patched:", path)
        return
    for anchor, insert, before in pairs:
        n = text.count(anchor)
        if n != 1:
            sys.exit("anchor found %d times in %s: %r" % (n, path, anchor[:60]))
        text = text.replace(anchor, insert + anchor if before else anchor + insert)
    with open(full, "w", encoding="utf-8") as f:
        f.write(text)
    print("patched:", path)


if "--again" in sys.argv:
    os.system("git -C %s checkout -- port/android-decomp/app/src/main/cpp/gx_core.h "
              "port/android-decomp/app/src/main/cpp/gx_core.cpp port/android-decomp/app/src/main/cpp/gx_gl.cpp" % tree)

patch("gx_core.h", [("  virtual void BloomPass() = 0;\n", snippet("core_members.txt"), True)])

run_init = (
    "  // realtime-shadows study (prototype)\n"
    "  if (m.shdMode < 0) {\n"
    "    const char *v = std::getenv(\"RE4_SHD_PROTO\");\n"
    "    m.shdMode = v && *v ? std::atoi(v) : 0;\n"
    "  }\n"
    "  m.shdAfter = m.shdMode > 0 && !m.interpPass && !m.skipDraws && !(chunk.flags & RE4_GX_CHUNK_STILL)\n"
    "                  ? m.FlareScan(chunk) : 0;\n"
    "  m.shdDone = false;\n"
    "  m.shdTris.clear();\n"
    "  m.shdLights.clear();\n"
    "  m.shdCenter[0] = m.shdCenter[1] = m.shdCenter[2] = 0.0;\n"
    "  m.shdCenterW = 0.0;\n"
    "  m.shdBlobs = m.shdCasterDraws = 0;\n"
    "  std::memset(m.shdTables, 0, sizeof(m.shdTables));\n"
    "  m.shdFuncs.clear();\n"
    "  m.shdHaveView = false;\n"
)
run_loop = (
    "    if (m.shdAfter && !m.shdDone && at >= m.shdAfter) {\n"
    "      m.shdDone = true;\n"
    "      m.ShadowProtoPass();\n"
    "    }\n"
)
draw_hook = (
    "  // realtime-shadows study (prototype): the figures' triangles, the game's foot shadow blobs left out\n"
    "  if (shdAfter && !merge && curOpAt < shdAfter) {\n"
    "    static const bool keepBlobs = std::getenv(\"RE4_SHD_KEEP_BLOBS\") && *std::getenv(\"RE4_SHD_KEEP_BLOBS\") == '1';\n"
    "    if (ShdBlob(d)) {\n"
    "      // the blob is drawn with the camera's view matrix (foot_shadow.cpp drawShadowParts): keep it\n"
    "      const unsigned row = d.matrices[0] & 63;\n"
    "      if (row <= 61) {\n"
    "        for (int r = 0; r < 3; r++)\n"
    "          for (int c = 0; c < 4; c++)\n"
    "            shdView[r][c] = xf.posTex[row + r][c];\n"
    "        shdHaveView = true;\n"
    "      }\n"
    "      if (!keepBlobs) {\n"
    "        shdBlobs++;\n"
    "        return;\n"
    "      }\n"
    "    }\n"
    "    ShdCollect(d, chunk);\n"
    "  }\n"
)
patch("gx_core.cpp", [
    ("void GxCore::Run(const Re4GxChunk &chunk) {\n", snippet("core_funcs.txt") + "\n", True),
    ("  m.bloomDone = false;\n", run_init, False),
    ("    if (m.bloomAfter && !m.bloomDone && at >= m.bloomAfter) {\n", run_loop, True),
    ("  const void *p = DrawProgram(merge != nullptr);\n  Split(GxStats::kSplitProgram);\n  if (!p)\n    return;\n",
     draw_hook, False),
])
patch("gx_gl.cpp", [("  void BloomRelease() {\n", snippet("gl_pass.txt"), True)])
