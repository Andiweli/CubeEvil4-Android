// The GX emulation both graphics APIs share (gx_core.h; port/docs/vulkan-bauplan.md, E1): registers, shader keys,
// uniform block contents, in-between pictures (60 FPS), merged draw calls, the lens flares' logic.
#include "gx_core.h"

#include "gx_texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <sys/syscall.h>
#include <unistd.h>

namespace re4gx {

std::string TempName(const std::string &path) {
  char tail[32];
  std::snprintf(tail, sizeof(tail), ".part%ld", static_cast<long>(syscall(SYS_gettid)));
  return path + tail;
}

bool ReadKeyList(const std::string &path, std::vector<ShaderKey> *keys) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  KeyListHead head{};
  bool ok = std::fread(&head, sizeof(head), 1, f) == 1 && std::memcmp(head.magic, "RE4L", 4) == 0 &&
            head.keyBytes == sizeof(ShaderKey) && head.count < 1000000;
  if (ok) {
    const std::size_t at = keys->size();
    keys->resize(at + head.count);
    ok = std::fread(keys->data() + at, sizeof(ShaderKey), head.count, f) == head.count;
    if (!ok)
      keys->resize(at);
  }
  std::fclose(f);
  return ok;
}

bool WriteKeyList(const std::string &path, const std::vector<ShaderKey> &keys) {
  const std::string temp = TempName(path);
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  const KeyListHead head{{'R', 'E', '4', 'L'}, static_cast<std::uint32_t>(sizeof(ShaderKey)),
                         static_cast<std::uint32_t>(keys.size())};
  bool ok = std::fwrite(&head, sizeof(head), 1, f) == 1 &&
            std::fwrite(keys.data(), sizeof(ShaderKey), keys.size(), f) == keys.size();
  ok = std::fclose(f) == 0 && ok;
  if (ok)
    std::rename(temp.c_str(), path.c_str());
  else
    std::remove(temp.c_str());
  return ok;
}

void GxCore::Split(int kind) {
  const auto now = std::chrono::steady_clock::now();
  stats.splitNs[kind] +=
      static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - splitAt).count());
  splitAt = now;
}

std::uint64_t GxCore::DrawSig(const Re4GxDraw &d, const ShaderKey &k, bool textures) const {
  std::uint64_t h = 1469598103934665603ull;
  auto mix = [&h](std::uint64_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  mix(ProgramTag());
  mix(d.primitive);
  mix(d.layout);
  mix(d.stride);
  mix(d.vertexCount);
  mix(d.indexCount);
  mix(d.matrices[0]);
  mix(d.matrices[1]);
  mix(d.matrices[2]);
  for (int m = 0; textures && m < 8; m++)
    if (k.texMaps & (1u << m))
      mix(boundKey[m]);
  return h;
}

void GxCore::DrawRows(const Re4GxDraw &d, std::uint32_t *first, std::uint32_t *count) {
  if (d.layout & RE4_GX_V_MATRIX) {
    *first = 0;
    *count = 30;
    return;
  }
  const std::uint32_t pi = d.matrices[0] & 63;
  *first = pi;
  *count = pi + 2 < 30 ? 3 : 0;
}

void GxCore::RecordDraw(const Re4GxDraw &d, const ShaderKey &k) {
  DrawRec r;
  r.sig = DrawSig(d, k);
  DrawRows(d, &r.posRow, &r.rows);
  r.data = static_cast<std::uint32_t>(recFloatsNew.size());
  r.vertexOffset = d.vertexOffset;
  r.vertexCount = d.vertexCount;
  r.stride = d.stride;
  r.layout = d.layout;
  r.perspective = !k.orthographic;
  if (r.rows == 3 && d.vertexCount <= 16)
    r.look = DrawSig(d, k, false);
  if (r.rows) {
    for (int c = 0; c < 3; c++)
      r.t[c] = xf.posTex[r.posRow + c][3];
  }
  for (std::uint32_t i = 0; i < r.rows; i++)
    recFloatsNew.insert(recFloatsNew.end(), xf.posTex[r.posRow + i], xf.posTex[r.posRow + i] + 4);
  for (std::uint32_t i = 0; i < r.rows; i++)
    recFloatsNew.insert(recFloatsNew.end(), xf.normal[(r.posRow + i) & 31], xf.normal[(r.posRow + i) & 31] + 3);
  for (int i = 0; i < 6; i++)
    recFloatsNew.push_back(xf.proj[i / 4][i % 4]);
  recFloatsNew.insert(recFloatsNew.end(), &xf.matAmb[0][0], &xf.matAmb[0][0] + 16);
  r.owner = curOwner;
  r.base = curOwner ? ownerBase : 0;
  r.life = curLife;
  r.shape = DrawSig(d, k, false);
  if (curOwner) {
    r.extra = static_cast<std::uint32_t>(recFloatsNew.size());
    for (int n = 0; n < 8; n++) {
      const std::uint32_t row = TexRow(d, n);
      for (std::uint32_t i = 0; i < 3; i++) {
        if (row < 60 && row + i < 64)
          recFloatsNew.insert(recFloatsNew.end(), xf.posTex[row + i], xf.posTex[row + i] + 4);
        else
          recFloatsNew.insert(recFloatsNew.end(), 4, 0.0f);
      }
    }
    float tev[32];
    std::memcpy(tev, tevColor, sizeof(tevColor));
    std::memcpy(tev + 16, tevKonst, sizeof(tevKonst));
    recFloatsNew.insert(recFloatsNew.end(), tev, tev + 32);
    for (int l = 0; l < 8; l++)
      recFloatsNew.insert(recFloatsNew.end(), xf.light[l][0], xf.light[l][0] + 4);
    // the post-transform matrices of the coordinates (dual texture: water and fog scroll there)
    for (int n = 0; n < 8; n++) {
      const std::uint32_t row = k.dualTex ? k.postGen[n] & 63 : 64;
      for (std::uint32_t i = 0; i < 3; i++) {
        if (row + i < 64)
          recFloatsNew.insert(recFloatsNew.end(), xf.post[row + i], xf.post[row + i] + 4);
        else
          recFloatsNew.insert(recFloatsNew.end(), 4, 0.0f);
      }
    }
    // the indirect matrices (BP 0x06..0x0E: the distortion of water and heat haze)
    for (int i = 0; i < 9; i++) {
      float w;
      std::memcpy(&w, &bp[0x06 + i], 4);
      recFloatsNew.push_back(w);
    }
  }
  recNew.push_back(r);
}

std::uint32_t GxCore::TexRow(const Re4GxDraw &d, int n) {
  if (d.layout & RE4_GX_V_MATRIX)
    return 60;
  const std::uint32_t w = n < 3 ? d.matrices[0] >> (8 * (n + 1)) : n < 7 ? d.matrices[1] >> (8 * (n - 3)) : d.matrices[2];
  return w & 63;
}

std::uint64_t GxCore::OwnerKey(const std::uint32_t *p) {
  if (!p[0] || (p[2] & 0xFF) == 0xFF)
    return 0;
  std::uint64_t h = 1469598103934665603ull;
  for (int i = 0; i < 3; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h | 1;
}

void GxCore::SetOwner(const std::uint32_t *p) {
  ownerBase = OwnerKey(p);
  ownerLife = p[3];
  for (int i = 0; i < 4; i++)
    ownerWords[i] = p[i];
  ownerDraws = 0;
}

const GxCore::DrawRec *GxCore::MatchDraw(const std::vector<std::uint32_t> &candidates, const float *t, std::uint32_t rows,
                           std::uint32_t vertexCount, bool flat) {
  const float reach = rows ? std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) : 0.0f;
  const bool particle = !flat && rows == 3 && vertexCount <= 16, moved = particle && camValid;
  const float limit = moved ? 0.05f * reach + 300.0f : 0.25f * reach + 400.0f;
  const float close = 0.02f * reach + 50.0f;
  int first = -1, best = -1;
  float firstDist = 0.0f, bestDist = 0.0f;
  unsigned looked = 0;
  for (std::uint32_t i : candidates) {
    if (recUsed[i] || recReserved[i])
      continue;
    const DrawRec &r = rec[i];
    float p[3] = {r.t[0], r.t[1], r.t[2]};
    if (moved)
      CameraApply(cam, r.t, p);
    const float dx = p[0] - t[0], dy = p[1] - t[1], dz = p[2] - t[2];
    const float dist = rows ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
    if (first < 0) {
      first = static_cast<int>(i);
      firstDist = dist;
      if (dist <= (particle ? close : limit))
        break;  // near enough (a particle: where it was): no need to look further
    }
    if (dist <= limit && (best < 0 || dist < bestDist)) {
      best = static_cast<int>(i);
      bestDist = dist;
    }
    if (++looked >= (particle ? 32u : 8u))
      break;
  }
  if (first >= 0 && firstDist <= limit && (!particle || best < 0 || firstDist <= bestDist + close))
    best = first;
  if (best < 0)
    return nullptr;
  recUsed[static_cast<std::size_t>(best)] = 1;
  return &rec[static_cast<std::size_t>(best)];
}

bool GxCore::Jumped(const float *before, const float *t) const {
  // where the particle was, moved by the camera's move (when known), against where it is now; the tolerance of
  // MatchDraw without the camera's move (a quarter of the distance and 400), so that fast sparks still count as
  // moving, and the wraps of the weather particles (1.2 times the box's half size, thousands of units) do not
  float p[3] = {before[0], before[1], before[2]};
  if (camValid)
    CameraApply(cam, before, p);
  const float reach = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
  const float dx = p[0] - t[0], dy = p[1] - t[1], dz = p[2] - t[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz) > 0.25f * reach + 400.0f;
}

void GxCore::CameraApply(const float (&c)[3][4], const float *q, float *out) {
  for (int r = 0; r < 3; r++)
    out[r] = c[r][0] * q[0] + c[r][1] * q[1] + c[r][2] * q[2] + c[r][3];
}

std::uint32_t GxCore::BlendByCamera(std::uint32_t first, std::uint32_t rows) {
  if (!camValid || rows != 3 || first + 3 > 64)
    return 0;
  const float(&c)[3][4] = cam;
  const float det = c[0][0] * (c[1][1] * c[2][2] - c[1][2] * c[2][1]) - c[0][1] * (c[1][0] * c[2][2] - c[1][2] * c[2][0]) +
                    c[0][2] * (c[1][0] * c[2][1] - c[1][1] * c[2][0]);
  if (!(std::fabs(det) > 1e-12f))
    return 0;
  const float inv[3][3] = {{(c[1][1] * c[2][2] - c[1][2] * c[2][1]) / det, (c[0][2] * c[2][1] - c[0][1] * c[2][2]) / det,
                            (c[0][1] * c[1][2] - c[0][2] * c[1][1]) / det},
                           {(c[1][2] * c[2][0] - c[1][0] * c[2][2]) / det, (c[0][0] * c[2][2] - c[0][2] * c[2][0]) / det,
                            (c[0][2] * c[1][0] - c[0][0] * c[1][2]) / det},
                           {(c[1][0] * c[2][1] - c[1][1] * c[2][0]) / det, (c[0][1] * c[2][0] - c[0][0] * c[2][1]) / det,
                            (c[0][0] * c[1][1] - c[0][1] * c[1][0]) / det}};
  float now[3][4];
  std::memcpy(now, xf.posTex[first], sizeof(now));
  const float s = interpT;
  // before = cam^-1 * now: the turn undone, the shift taken back
  for (int r = 0; r < 3; r++) {
    float before[4];
    for (int k = 0; k < 3; k++)
      before[k] = inv[r][0] * now[0][k] + inv[r][1] * now[1][k] + inv[r][2] * now[2][k];
    before[3] = inv[r][0] * (now[0][3] - c[0][3]) + inv[r][1] * (now[1][3] - c[1][3]) + inv[r][2] * (now[2][3] - c[2][3]);
    for (int k = 0; k < 4; k++)
      xf.posTex[first + r][k] = before[k] + (now[r][k] - before[k]) * s;
  }
  const bool lights = BlendLights(now, xf.posTex[first]);
  Dirty(static_cast<int>(first), static_cast<int>(first + 3));
  Dirty(64, 96);
  stats.interpCameraMoved++;
  return first << 8 | rows | (lights ? 0x200000u : 0u);
}

bool GxCore::CameraMove(const float *p, const float (&n)[3][4], float (&c)[3][4]) {
  const float det = p[0] * (p[5] * p[10] - p[6] * p[9]) - p[1] * (p[4] * p[10] - p[6] * p[8]) +
                    p[2] * (p[4] * p[9] - p[5] * p[8]);
  if (!(std::fabs(det) > 1e-12f))
    return false;
  const float inv[3][3] = {{(p[5] * p[10] - p[6] * p[9]) / det, (p[2] * p[9] - p[1] * p[10]) / det,
                            (p[1] * p[6] - p[2] * p[5]) / det},
                           {(p[6] * p[8] - p[4] * p[10]) / det, (p[0] * p[10] - p[2] * p[8]) / det,
                            (p[2] * p[4] - p[0] * p[6]) / det},
                           {(p[4] * p[9] - p[5] * p[8]) / det, (p[1] * p[8] - p[0] * p[9]) / det,
                            (p[0] * p[5] - p[1] * p[4]) / det}};
  for (int r = 0; r < 3; r++) {
    for (int k = 0; k < 3; k++)
      c[r][k] = n[r][0] * inv[0][k] + n[r][1] * inv[1][k] + n[r][2] * inv[2][k];
    c[r][3] = n[r][3] - (c[r][0] * p[3] + c[r][1] * p[7] + c[r][2] * p[11]);
  }
  return true;
}

void GxCore::NoteCamera(std::uint64_t sig, const float *before, const float (&now)[3][4]) {
  auto it = recBySig.find(sig);
  if (it == recBySig.end() || it->second.size() != 1)
    return;
  const float beforeT[3] = {before[3], before[7], before[11]}, nowT[3] = {now[0][3], now[1][3], now[2][3]};
  const float reach = std::sqrt(nowT[0] * nowT[0] + nowT[1] * nowT[1] + nowT[2] * nowT[2]);
  for (unsigned g = 0; g < camGuesses; g++) {
    CamGuess &q = camGuess[g];
    float p[3];
    CameraApply(q.c, beforeT, p);
    const float dx = p[0] - nowT[0], dy = p[1] - nowT[1], dz = p[2] - nowT[2];
    if (std::sqrt(dx * dx + dy * dy + dz * dz) > 0.02f * reach + 50.0f)
      continue;
    const float sx = nowT[0] - q.t[0], sy = nowT[1] - q.t[1], sz = nowT[2] - q.t[2];
    if (sx * sx + sy * sy + sz * sz >= 1.0f && ++q.votes > camVotes) {
      std::memcpy(cam, q.c, sizeof(cam));
      camVotes = q.votes;
      camValid = true;
    }
    return;
  }
  if (camGuesses < 4) {
    CamGuess &q = camGuess[camGuesses];
    if (!CameraMove(before, now, q.c))
      return;
    std::memcpy(q.t, nowT, sizeof(q.t));
    q.votes = 1;
    camGuesses++;
  }
}

bool GxCore::BlendLights(const float (&now)[3][4], const float *mid) {
  const float(&n)[3][4] = now;
  const float det = n[0][0] * (n[1][1] * n[2][2] - n[1][2] * n[2][1]) -
                    n[0][1] * (n[1][0] * n[2][2] - n[1][2] * n[2][0]) +
                    n[0][2] * (n[1][0] * n[2][1] - n[1][1] * n[2][0]);
  if (!(std::fabs(det) > 1e-12f))
    return false;
  float inv[3][3];
  inv[0][0] = (n[1][1] * n[2][2] - n[1][2] * n[2][1]) / det;
  inv[0][1] = (n[0][2] * n[2][1] - n[0][1] * n[2][2]) / det;
  inv[0][2] = (n[0][1] * n[1][2] - n[0][2] * n[1][1]) / det;
  inv[1][0] = (n[1][2] * n[2][0] - n[1][0] * n[2][2]) / det;
  inv[1][1] = (n[0][0] * n[2][2] - n[0][2] * n[2][0]) / det;
  inv[1][2] = (n[0][2] * n[1][0] - n[0][0] * n[1][2]) / det;
  inv[2][0] = (n[1][0] * n[2][1] - n[1][1] * n[2][0]) / det;
  inv[2][1] = (n[0][1] * n[2][0] - n[0][0] * n[2][1]) / det;
  inv[2][2] = (n[0][0] * n[1][1] - n[0][1] * n[1][0]) / det;
  // C's turn: mid's 3x3 times inv; C's shift: mid's shift minus C's turn of now's shift
  float rot[3][3], shift[3];
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++)
      rot[r][c] = mid[r * 4 + 0] * inv[0][c] + mid[r * 4 + 1] * inv[1][c] + mid[r * 4 + 2] * inv[2][c];
  }
  for (int r = 0; r < 3; r++)
    shift[r] = mid[r * 4 + 3] - (rot[r][0] * n[0][3] + rot[r][1] * n[1][3] + rot[r][2] * n[2][3]);
  for (int l = 0; l < 8; l++) {
    float p[3], dir[3];
    std::memcpy(p, xf.light[l][3], sizeof(p));
    std::memcpy(dir, xf.light[l][4], sizeof(dir));
    for (int r = 0; r < 3; r++) {
      xf.light[l][3][r] = rot[r][0] * p[0] + rot[r][1] * p[1] + rot[r][2] * p[2] + shift[r];
      xf.light[l][4][r] = rot[r][0] * dir[0] + rot[r][1] * dir[1] + rot[r][2] * dir[2];
    }
  }
  Dirty(160, 200);
  return true;
}

void GxCore::BlendRestore(std::uint32_t blended) {
  const std::uint32_t first = (blended >> 8) & 0xFF, rows = blended & 0xFF;
  for (std::uint32_t r = 0; r < rows; r++)
    for (std::uint32_t c = 0; c < 4; c++)
      XfMemToBlock((first + r) * 4 + c);
  for (std::uint32_t r = 0; r < rows; r++)
    for (std::uint32_t c = 0; c < 3; c++)
      XfMemToBlock(0x400 + ((first + r) & 31) * 3 + c);
  if (rows) {
    Dirty(static_cast<int>(first), static_cast<int>(first + rows));
    Dirty(64, 96);
  }
  if (blended & 0x200000) {
    for (std::uint32_t a = 0x600; a < 0x680; a++)
      XfMemToBlock(a);
    Dirty(160, 200);
  }
  if (blended & 0x400000)
    Dirty(200, 204);
  if (blended & 0x800000) {
    for (std::uint32_t row : texRowsBlended) {
      if (row == 0x100) {
        std::memcpy(tevColor, tevSave[0], sizeof(tevColor));
        std::memcpy(tevKonst, tevSave[1], sizeof(tevKonst));
        pxDirty = true;
        continue;
      }
      if (row == 0x300) {
        std::memcpy(&bp[0x06], indSave, sizeof(indSave));
        pxDirty = true;
        continue;
      }
      if (row >= 0x200) {
        const std::uint32_t pr = row - 0x200;
        for (std::uint32_t r = 0; r < 3; r++)
          for (std::uint32_t c = 0; c < 4; c++)
            XfMemToBlock(0x500 + (pr + r) * 4 + c);
        Dirty(static_cast<int>(96 + pr), static_cast<int>(96 + pr + 3));
        continue;
      }
      for (std::uint32_t r = 0; r < 3; r++)
        for (std::uint32_t c = 0; c < 4; c++)
          XfMemToBlock((row + r) * 4 + c);
      Dirty(static_cast<int>(row), static_cast<int>(row + 3));
    }
    texRowsBlended.clear();
  }
  xfRegsDirty = true; // FillXfRegs puts the projection (and the colours) back
  Dirty(206, 208);
}

float GxCore::CopyFilterGain() const {
  const std::uint32_t c0 = bp[0x53], c1 = bp[0x54];
  unsigned sum = 0;
  for (unsigned i = 0; i < 4; i++)
    sum += (c0 >> (6 * i)) & 63;
  for (unsigned i = 0; i < 3; i++)
    sum += (c1 >> (6 * i)) & 63;
  return sum == 0 ? 1.0f : static_cast<float>(sum) / 64.0f;
}

void GxCore::CopyFilterTaps(float *above, float *at, float *below) const {
  const std::uint32_t c0 = bp[0x53], c1 = bp[0x54];
  unsigned t[7];
  for (unsigned i = 0; i < 4; i++)
    t[i] = (c0 >> (6 * i)) & 63;
  for (unsigned i = 0; i < 3; i++)
    t[4 + i] = (c1 >> (6 * i)) & 63;
  if (t[0] + t[1] + t[2] + t[3] + t[4] + t[5] + t[6] == 0) {
    *above = *below = 0.0f;
    *at = 1.0f;
    return;
  }
  *above = static_cast<float>(t[0] + t[1]) / 64.0f;
  *at = static_cast<float>(t[2] + t[3] + t[4]) / 64.0f;
  *below = static_cast<float>(t[5] + t[6]) / 64.0f;
}

void GxCore::Dirty(int lo, int hi) {
  for (int i = 0; i < kXfParts; i++) {
    const int a = std::max(lo, kXfPartRow[i]), b = std::min(hi, kXfPartRow[i + 1]);
    if (a < b) {
      dirtyLo[i] = std::min(dirtyLo[i], a);
      dirtyHi[i] = std::max(dirtyHi[i], b);
    }
  }
}

void GxCore::DirtyAddress(std::uint32_t a) {
  constexpr int kNormal = 64, kPost = 96, kLight = 160, kRegs = 200; // row offsets in XfBlock
  if (a < 0x100)
    Dirty(static_cast<int>(a / 4), static_cast<int>(a / 4) + 1);
  else if (a >= 0x400 && a < 0x460)
    Dirty(kNormal + static_cast<int>((a - 0x400) / 3), kNormal + static_cast<int>((a - 0x400) / 3) + 1);
  else if (a >= 0x500 && a < 0x600)
    Dirty(kPost + static_cast<int>((a - 0x500) / 4), kPost + static_cast<int>((a - 0x500) / 4) + 1);
  else if (a >= 0x600 && a < 0x680)
    Dirty(kLight + static_cast<int>((a - 0x600) / 16) * 5, kLight + static_cast<int>((a - 0x600) / 16) * 5 + 5);
  else if (a >= 0x1000)
    Dirty(kRegs, static_cast<int>(sizeof(XfBlock) / 16));
}

void GxCore::XfWrite(std::uint32_t address, const std::uint32_t *words, std::uint32_t n) {
  bool projection = false;
  for (std::uint32_t i = 0; i < n; i++) {
    const std::uint32_t a = address + i;
    if (a < 0x800) {
      xfMem[a] = words[i];
      XfMemToBlock(a);
    } else if (a >= 0x1000 && a < 0x1058) {
      const std::uint32_t r = a - 0x1000;
      xfReg[r] = words[i];
      if (r == 0x09 || (r >= 0x0E && r <= 0x12) || r == 0x26 || r >= 0x3F)
        keyDirty = true;
      if (r >= 0x20 && r <= 0x26)
        projection = true;
      xfRegsDirty = true;
    } else {
      continue;
    }
    DirtyAddress(a);
  }
  if (projection && xfReg[0x26] == 0) {
    perspA = AsFloat(xfReg[0x24]);
    perspB = AsFloat(xfReg[0x25]);
    havePerspective = true;
  }
}

void GxCore::XfMemToBlock(std::uint32_t a) {
  if (a < 0x100) {
    xf.posTex[a / 4][a % 4] = AsFloat(xfMem[a]);
  } else if (a >= 0x400 && a < 0x460) {
    xf.normal[(a - 0x400) / 3][(a - 0x400) % 3] = AsFloat(xfMem[a]);
  } else if (a >= 0x500 && a < 0x600) {
    xf.post[(a - 0x500) / 4][(a - 0x500) % 4] = AsFloat(xfMem[a]);
  } else if (a >= 0x600 && a < 0x680) {
    const unsigned l = (a - 0x600) / 16, w = (a - 0x600) % 16;
    if (w == 3) {
      for (int c = 0; c < 4; c++)
        xf.light[l][0][c] = static_cast<float>((xfMem[a] >> (24 - 8 * c)) & 255) / 255.0f;
    } else if (w >= 4) {
      xf.light[l][1 + (w - 4) / 3][(w - 4) % 3] = AsFloat(xfMem[a]);
    }
  }
}

void GxCore::FillXfRegs() {
  const std::uint32_t colorRegs[4] = {xfReg[0x0C], xfReg[0x0D], xfReg[0x0A], xfReg[0x0B]};
  for (int i = 0; i < 4; i++)
    for (int c = 0; c < 4; c++)
      xf.matAmb[i][c] = static_cast<float>((colorRegs[i] >> (24 - 8 * c)) & 255) / 255.0f;
  xf.viewport[0][0] = AsFloat(xfReg[0x1A]);
  xf.viewport[0][1] = AsFloat(xfReg[0x1B]);
  xf.viewport[0][2] = AsFloat(xfReg[0x1C]);
  xf.viewport[0][3] = AsFloat(xfReg[0x1D]);
  xf.viewport[1][0] = AsFloat(xfReg[0x1E]);
  xf.viewport[1][1] = AsFloat(xfReg[0x1F]);
  xf.viewport[1][2] = kEfbWidth;
  xf.viewport[1][3] = kEfbHeight;
  for (int i = 0; i < 6; i++)
    xf.proj[i / 4][i % 4] = AsFloat(xfReg[0x20 + i]);
  xf.proj[1][2] = static_cast<float>(xfReg[0x26]);
  xfRegsDirty = false;
}

ShaderKey GxCore::MakeKey(const Re4GxDraw &d) {
  ShaderKey k;
  std::memset(&k, 0, sizeof(k));
  k.centroid = options.centroid && samples > 1 ? 1 : 0;
  if (psxOn && !(xfReg[0x26] & 1))
    k.centroid |= 2;  // PSone demake: the 3D draws only (ShaderKey::centroid bit 1)
  k.layout = d.layout;
  k.numChans = xfReg[0x09] & 3;
  k.numTexGens = std::min(8u, xfReg[0x3F] & 15);
  for (int i = 0; i < 4; i++)
    k.chan[i] = i % 2 < static_cast<int>(k.numChans) ? xfReg[0x0E + i] & 0x7FFF : 0;
  k.dualTex = xfReg[0x12] & 1;
  for (unsigned i = 0; i < k.numTexGens; i++) {
    k.texGen[i] = xfReg[0x40 + i] & 0x3FFFF;
    k.postGen[i] = k.dualTex ? xfReg[0x50 + i] & 0x13F : 0;
  }
  k.orthographic = xfReg[0x26] & 1;
  const std::uint32_t gen = bp[0x00];
  k.numStages = ((gen >> 10) & 15) + 1;
  k.numIndStages = (gen >> 16) & 7;
  for (unsigned s = 0; s < k.numStages; s++) {
    k.colorEnv[s] = bp[0xC0 + 2 * s] & 0xFFFFFF;
    k.alphaEnv[s] = bp[0xC1 + 2 * s] & 0xFFFFFF;
    const std::uint32_t o = bp[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
    k.order[s] = (o & 7) | ((o >> 3) & 7) << 3 | ((o >> 6) & 1) << 6 | ((o >> 7) & 7) << 7;
    const std::uint32_t ks = bp[0xF6 + s / 2];
    k.konst[s] = ((s & 1) ? (ks >> 14) & 31 : (ks >> 4) & 31) | ((s & 1) ? (ks >> 19) & 31 : (ks >> 9) & 31) << 8;
    k.indCmd[s] = k.numIndStages ? bp[0x10 + s] & 0x1FFFFF : 0;
    if ((o >> 6) & 1)
      k.texMaps |= 1u << (o & 7);
  }
  for (int t = 0; t < 4; t++)
    k.swap[t] = (bp[0xF6 + 2 * t] & 3) | ((bp[0xF6 + 2 * t] >> 2) & 3) << 2 | (bp[0xF7 + 2 * t] & 3) << 4 |
                ((bp[0xF7 + 2 * t] >> 2) & 3) << 6;
  if (k.numIndStages) {
    k.indRef = bp[0x27] & 0xFFFFFF;
    for (unsigned s = 0; s < k.numIndStages && s < 4; s++)
      k.texMaps |= 1u << ((k.indRef >> (6 * s)) & 7);
  }
  k.alphaTest = (bp[0xF3] >> 16) & 0xFF;
  k.fog = (bp[0xF1] >> 20) & 15;
  return k;
}

std::string GxCore::RoomList(unsigned r) const {
  char name[32];
  std::snprintf(name, sizeof(name), "/room-%03x.keys", r);
  return programCache + name;
}

void GxCore::ReadAhead() {
  aheadRead = true;
  ahead.clear();
  aheadNext = 0;
  aheadLoaded = aheadLinked = aheadListed = 0;
  aheadMs = 0.0;
  std::vector<ShaderKey> keys;
  if (programCache.empty() || room == ~0u || !ReadKeyList(RoomList(room), &keys))
    return;
  for (const ShaderKey &k : keys) {
    roomUsed.insert(k);
    if (ahead.size() < maxPrograms / 2) {
      AheadKey a;
      a.key = k;
      ahead.push_back(a);
    }
  }
}

void GxCore::SaveRoom() {
  roomDirty = false;
  roomSaved = stats.chunks;
  if (programCache.empty() || room == ~0u || roomUsed.empty())
    return;
  const std::vector<ShaderKey> keys(roomUsed.begin(), roomUsed.end());
  WriteKeyList(RoomList(room), keys);
}

void GxCore::NoteKey(const ShaderKey &k) {
  if (room != ~0u && !programCache.empty() && roomUsed.insert(k).second)
    roomDirty = true;
}

void GxCore::FillPixel(const ShaderKey &k) {
  std::memcpy(px.colors, tevColor, sizeof(tevColor));
  std::memcpy(px.konst, tevKonst, sizeof(tevKonst));
  px.alphaRef[0] = static_cast<std::int32_t>(bp[0xF3] & 0xFF);
  px.alphaRef[1] = static_cast<std::int32_t>((bp[0xF3] >> 8) & 0xFF);
  const auto fogFloat = [](std::uint32_t v) {
    const std::uint32_t bits = ((v >> 19) & 1) << 31 | ((v >> 11) & 0xFF) << 23 | (v & 0x7FF) << 12;
    return AsFloat(bits);
  };
  px.fog[0][0] = fogFloat(bp[0xEE]);
  px.fog[0][1] = fogFloat(bp[0xF1]);
  px.fog[0][2] = static_cast<float>(bp[0xEF] & 0xFFFFFF);
  px.fog[0][3] = static_cast<float>(bp[0xF0] & 31);
  px.fog[1][0] = static_cast<float>((bp[0xF2] >> 16) & 255);
  px.fog[1][1] = static_cast<float>((bp[0xF2] >> 8) & 255);
  px.fog[1][2] = static_cast<float>(bp[0xF2] & 255);
  px.fog[1][3] = 0.0f;
  for (int i = 0; i < 8; i++) {
    px.coordSize[i][0] = static_cast<float>((bp[0x30 + 2 * i] & 0xFFFF) + 1);
    px.coordSize[i][1] = static_cast<float>((bp[0x31 + 2 * i] & 0xFFFF) + 1);
    px.coordSize[i][2] = px.coordSize[i][3] = 0.0f;
    const unsigned mode0 = bp[(i < 4 ? 0x80 : 0xA0) + (i & 3)];
    int bias = static_cast<int>((mode0 >> 9) & 0xFF);
    if (bias & 0x80)
      bias -= 256;
    px.mapSize[i][0] = static_cast<float>(bound[i].width);
    px.mapSize[i][1] = static_cast<float>(bound[i].height);
    px.mapSize[i][2] = static_cast<float>(bias) / 32.0f;
    // PSone demake: the 3D textures no finer than the grid shows them (a cell is `halves` screen pixels
    // wide): sharper mip levels flickered as the camera moved (fine planks, the owner's report 03.11.2026)
    if (psxOn && !(xfReg[0x26] & 1))
      px.mapSize[i][2] += std::log2(static_cast<float>(std::max(halves, 2)));
    px.mapSize[i][3] = 0.0f;
  }
  for (int s = 0; s < 4; s++) {
    const std::uint32_t r = bp[0x25 + s / 2] >> ((s & 1) ? 8 : 0);
    px.indScale[s][0] = static_cast<std::int32_t>(r & 15);
    px.indScale[s][1] = static_cast<std::int32_t>((r >> 4) & 15);
    px.indScale[s][2] = px.indScale[s][3] = 0;
  }
  for (int m = 0; m < 3; m++) {
    const std::uint32_t a = bp[0x06 + 3 * m], b = bp[0x07 + 3 * m], c = bp[0x08 + 3 * m];
    const int scale = static_cast<int>(((a >> 22) & 3) | ((b >> 22) & 3) << 2 | ((c >> 22) & 3) << 4);
    px.indMtx[2 * m][0] = Signed11(a);
    px.indMtx[2 * m][1] = Signed11(b);
    px.indMtx[2 * m][2] = Signed11(c);
    px.indMtx[2 * m][3] = 17 - scale;
    px.indMtx[2 * m + 1][0] = Signed11(a >> 11);
    px.indMtx[2 * m + 1][1] = Signed11(b >> 11);
    px.indMtx[2 * m + 1][2] = Signed11(c >> 11);
    px.indMtx[2 * m + 1][3] = 17 - scale;
  }
  (void)k;
}

void GxCore::MergePlan(const Re4GxChunk &chunk) {
  mergeMarks.clear();
  mergeIndices.clear();
  mergeNext = 0;
  Re4GxDraw last{};
  std::size_t chainMark = 0;   // the chain's first mark
  bool open = false, gap = false;  // a chain is open; only ops a run may pass since its last draw
  int gapWhy = GxStats::kPlanOtherOp;  // why gap went false (GxStats::mergePlanBreak)
  const auto close = [&]() {
    if (open && mergeMarks.size() - chainMark < 2) {  // a single draw: no chain
      mergeIndices.resize(mergeMarks[chainMark].index - chunk.indexCount);
      mergeMarks.resize(chainMark);
    }
    open = false;
  };
  for (std::uint32_t at = 0; at < chunk.opWords;) {
    const std::uint32_t head = chunk.ops[at], op = head & 0xFF, n = head >> 8;
    const std::uint32_t *p = chunk.ops + at + 1;
    if (at + 1 + n > chunk.opWords)
      break;
    if (op == RE4_GX_OP_DRAW && n * 4 >= sizeof(Re4GxDraw)) {
      Re4GxDraw d;
      std::memcpy(&d, p, sizeof(d));
      const bool valid = d.indexCount && d.stride &&
                         d.vertexOffset + static_cast<std::uint64_t>(d.vertexCount) * d.stride <= chunk.vertexBytes &&
                         d.indexOffset + static_cast<std::uint64_t>(d.indexCount) <= chunk.indexCount;
      const std::uint32_t first = valid ? d.vertexOffset / d.stride : 0;
      const std::uint16_t *idx = chunk.indices + d.indexOffset;
      std::uint32_t most = 0;
      if (valid)
        for (std::uint32_t i = 0; i < d.indexCount; i++)
          most = std::max<std::uint32_t>(most, idx[i]);
      const bool joins = open && gap && valid && d.layout == last.layout && d.stride == last.stride &&
                         (d.primitive & 0xFF) == (last.primitive & 0xFF) &&
                         d.vertexOffset % d.stride == last.vertexOffset % last.stride &&
                         first >= last.vertexOffset / last.stride + last.vertexCount &&
                         first - mergeMarks[chainMark].base + most < 0xFFFF;
      if (!joins && open && valid) {
        const int why = !gap ? gapWhy
                        : d.layout != last.layout ? GxStats::kPlanLayout
                        : d.stride != last.stride ? GxStats::kPlanStride
                        : (d.primitive & 0xFF) != (last.primitive & 0xFF) ? GxStats::kPlanPrimitive
                        : d.vertexOffset % d.stride != last.vertexOffset % last.stride ||
                                  first < last.vertexOffset / last.stride + last.vertexCount
                            ? GxStats::kPlanVertices
                            : GxStats::kPlanIndices;
        stats.mergePlanBreak[why]++;
      }
      if (!joins) {
        close();
        if (valid) {
          open = true;
          chainMark = mergeMarks.size();
        }
      }
      if (open) {
        const bool starts = chainMark == mergeMarks.size();
        MergeMark m;
        m.op = at;
        m.chain = starts ? at : mergeMarks[chainMark].chain;
        m.index = chunk.indexCount + static_cast<std::uint32_t>(mergeIndices.size());
        m.first = first;
        m.base = starts ? first : mergeMarks[chainMark].base;
        mergeMarks.push_back(m);
        const std::uint32_t move = first - m.base;
        for (std::uint32_t i = 0; i < d.indexCount; i++)
          mergeIndices.push_back(static_cast<std::uint16_t>(idx[i] + move));
        last = d;
        gap = true;
      }
    } else if (op == RE4_GX_OP_BP) {
      const unsigned reg = n >= 1 ? p[0] >> 24 : 0;
      if (n < 1 || reg == 0x52 || reg == 0x5A || reg == 0x5B) {
        gap = false;
        gapWhy = GxStats::kPlanCopyMark;
      }
    } else if (op == RE4_GX_OP_TEXTURE) {
      Re4GxTexture t;
      if (n * 4 < sizeof(t)) {
        gap = false;
        gapWhy = GxStats::kPlanTexture;
      } else {
        std::memcpy(&t, p, sizeof(t));
        if ((t.flags & RE4_GX_TEX_COPY) || t.dataBytes) {
          gap = false;
          gapWhy = (t.flags & RE4_GX_TEX_COPY) ? GxStats::kPlanTextureCopy : GxStats::kPlanTexture;
        }
      }
    } else if (op != RE4_GX_OP_XF && op != RE4_GX_OP_OWNER && op != RE4_GX_OP_TLUT) {
      gap = false;
      gapWhy = GxStats::kPlanOtherOp;
    }
    at += 1 + n;
  }
  close();
}

bool GxCore::MergeQuiet(const Re4GxChunk &chunk, std::uint32_t from, std::uint32_t to, int *why) const {
  std::uint32_t touched[8] = {}, last[256];
  *why = GxStats::kRunOtherOp;
  for (std::uint32_t at = from; at < to;) {
    const std::uint32_t head = chunk.ops[at], op = head & 0xFF, n = head >> 8;
    const std::uint32_t *p = chunk.ops + at + 1;
    switch (op) {
    case RE4_GX_OP_OWNER:
    case RE4_GX_OP_TLUT:
      break;
    case RE4_GX_OP_BP: {
      const unsigned reg = p[0] >> 24;
      const std::uint32_t value = p[0] & 0xFFFFFF;
      if (reg == 0x52 || reg == 0x5A || reg == 0x5B) {
        *why = GxStats::kRunBpMark;
        return false;
      }
      if (reg >= 0xE0 && reg <= 0xE7) {
        // (one address for two registers: the colour or the konstant, by bit 23)
        const std::int32_t *dst = (value >> 23) & 1 ? tevKonst[(reg - 0xE0) / 2] : tevColor[(reg - 0xE0) / 2];
        const bool same = (reg & 1) == 0 ? dst[0] == Signed11(value) && dst[3] == Signed11(value >> 12)
                                         : dst[2] == Signed11(value) && dst[1] == Signed11(value >> 12);
        if (!same) {
          *why = GxStats::kRunTevColour;
          return false;
        }
        break;
      }
      touched[reg / 32] |= 1u << (reg % 32);
      last[reg] = value;
      break;
    }
    case RE4_GX_OP_XF:
      *why = GxStats::kRunOtherOp;
      for (std::uint32_t i = 1; i < n; i++) {
        const std::uint32_t a = p[0] + i - 1, v = p[i];
        if (a < 0x100 || (a >= 0x400 && a < 0x460) || (a >= 0x100A && a <= 0x100D) || a == 0x1018 || a == 0x1019)
          continue;
        if (a < 0x800 ? xfMem[a] != v : a >= 0x1000 && a < 0x1058 && xfReg[a - 0x1000] != v) {
          *why = a >= 0x1000 ? GxStats::kRunXfReg
                 : a >= 0x600 ? GxStats::kRunXfLight
                 : a >= 0x500 ? GxStats::kRunXfPost : GxStats::kRunXf;
          return false;
        }
      }
      break;
    case RE4_GX_OP_TEXTURE: {
      Re4GxTexture t;
      std::memcpy(&t, p, sizeof(t));
      const unsigned map = t.map & 7;
      const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
      *why = GxStats::kRunTexture;
      if (key == 0 || boundKey[map] != key || bound[map].width != (t.width ? t.width : 1) ||
          bound[map].height != (t.height ? t.height : 1))
        return false;
      const auto it = textures.find(key);
      if (it == textures.end() || it->second.id != bound[map].id || it->second.levels != bound[map].levels)
        return false;
      break;
    }
    default:
      *why = GxStats::kRunOtherOp;
      return false;
    }
    at += 1 + n;
  }
  for (unsigned reg = 0; reg < 256; reg++)
    if (((touched[reg / 32] >> (reg % 32)) & 1) && last[reg] != bp[reg]) {
      *why = GxStats::kRunBpValue;
      return false;
    }
  return true;
}

float GxCore::WidenFor(const ShaderKey &k) const {
  const float f = widen2d.load(std::memory_order_relaxed);
  if (f == 1.0f || !k.orthographic)
    return 1.0f;
  const float half = AsFloat(xfReg[0x1A]), centre = AsFloat(xfReg[0x1D]) - 342.0f;
  if (half * 2.0f < 500.0f || std::fabs(centre - half) > 1.0f)
    return 1.0f;
  for (int m = 0; m < 8; m++)
    if ((k.texMaps & (1u << m)) && (boundKey[m] >> 63))
      return 1.0f;
  return f;
}

void GxCore::CopyMode(std::uint32_t control, bool depth, int *mode, unsigned *kind) {
  const unsigned format = ((control >> 4) & 7) | ((control >> 3) & 1) << 3;
  const bool intensity = (control >> 15) & 1;
  *kind = 0;
  if (depth) {
    switch (format) {
    case 0x9: *mode = 13; *kind = 1; return;  // Z8M
    case 0xA: *mode = 14; *kind = 1; return;  // Z8L
    case 0xB:                                 // Z16
    case 0x3:
    case 0xC: *mode = 15; *kind = 2; return;  // Z16L (approx.)
    case 0x6: *mode = 16; *kind = 0; return;  // Z24X8
    default: *mode = 12; *kind = 1; return;   // Z4, Z8
    }
  }
  if (intensity) {
    if (format == 2 || format == 3) {
      *mode = 4;
      *kind = 2;
    } else {
      *mode = 3;
      *kind = 1;
    }
    return;
  }
  switch (format) {
  case 0x0:
  case 0x1:
  case 0x8: *mode = 5; *kind = 1; return; // R4, R8
  case 0x2:
  case 0x3: *mode = 11; *kind = 2; return; // RA4, RA8
  case 0x4: *mode = 1; return;
  case 0x5: *mode = 2; return;
  case 0x7: *mode = 8; *kind = 1; return; // A8
  case 0x9: *mode = 6; *kind = 1; return; // G8
  case 0xA: *mode = 7; *kind = 1; return; // B8
  case 0xB: *mode = 9; *kind = 2; return; // RG8
  case 0xC: *mode = 10; *kind = 2; return; // GB8
  default: *mode = 0; return;             // RGBA8 (and YUVA8)
  }
}

std::uint32_t GxCore::FlareScan(const Re4GxChunk &chunk) const {
  std::uint32_t proj = xfReg[0x26], half = xfReg[0x1A], after = 0;
  for (std::uint32_t at = 0; at < chunk.opWords;) {
    const std::uint32_t head = chunk.ops[at], op = head & 0xFF, n = head >> 8;
    const std::uint32_t *p = chunk.ops + at + 1;
    if (at + 1 + n > chunk.opWords)
      break;
    if (op == RE4_GX_OP_XF && n >= 1) {
      const std::uint32_t a = p[0];
      if (a <= 0x1026 && a + n - 1 > 0x1026)
        proj = p[1 + 0x1026 - a];
      if (a <= 0x101A && a + n - 1 > 0x101A)
        half = p[1 + 0x101A - a];
    } else if (op == RE4_GX_OP_DRAW && !(proj & 1) && AsFloat(half) * 2.0f >= 500.0f) {
      after = at + 1 + n;
    }
    at += 1 + n;
  }
  return after;
}

void GxCore::FlareNote(const Re4GxDraw &d, const Re4GxChunk &chunk, std::uint32_t opAt) {
  if (opAt >= flareAfter || (xfReg[0x26] & 1) || !(AsFloat(xfReg[0x1A]) * 2.0f >= 500.0f))
    return;
  const std::uint32_t c = bp[0x41];
  if (!(c & 1) || ((c >> 11) & 1) || ((c >> 5) & 7) != 1 || !((c >> 3) & 1) || !(bp[0x40] & 1))
    return;
  if (d.vertexCount == 0 || d.vertexCount > 512 || d.stride < 12)
    return;
  // nothing on a figure (wish of the project owner, 09.11.2026: halos only at the room's lights and glowing points,
  // never on a figure that moves): a draw of a figure or of a particle attached to one (the merchant's glowing eyes
  // in r11c, an enemy's torch); the core marks them in the owner tag (runtime/gx_owner.cpp)
  // (tests: RE4_GX_FLARE_FIGURES=1 takes them as before 0.99.6)
  static const bool figures = std::getenv("RE4_GX_FLARE_FIGURES") && *std::getenv("RE4_GX_FLARE_FIGURES") == '1';
  if (!figures && ownerWords[2] != 0xFF && (ownerWords[2] & RE4_GX_OWNER_FIGURE))
    return;
  // lines and points are no light (the laser sight's beam is a GX line, its dot elsewhere)
  if ((d.primitive & 0xFF) == RE4_GX_LINES || (d.primitive & 0xFF) == RE4_GX_POINTS)
    return;
  // where a per-vertex matrix index sits (Re4GxLayout)
  unsigned mtxAt = 12;
  if (d.layout & RE4_GX_V_NORMAL)
    mtxAt += (d.layout & RE4_GX_V_BINORMAL_TANGENT) ? 36 : 12;
  mtxAt += ((d.layout & RE4_GX_V_COLOR0) ? 4 : 0) + ((d.layout & RE4_GX_V_COLOR1) ? 4 : 0);
  for (int t = 0; t < 8; t++)
    mtxAt += (d.layout & (RE4_GX_V_TEX0 << t)) ? 8 : 0;
  const float *vp0 = xf.viewport[0], *vp1 = xf.viewport[1], *pr0 = xf.proj[0], *pr1 = xf.proj[1];
  FlareCand f{1e9f, 1e9f, -1e9f, -1e9f, 1.0f, 1};
  f.normals = (d.layout & RE4_GX_V_NORMAL) ? 1 : 0;
  f.verts = d.vertexCount;
  f.zmode = bp[0x40];
  f.layout = d.layout;
  f.blend = c;
  f.tevc = bp[0xC0];
  for (int i = 0; i < 4; i++)
    f.owner[i] = ownerWords[i];
  float zSum = 0.0f;
  unsigned counted = 0;
  const unsigned step = d.vertexCount > 64 ? d.vertexCount / 64 : 1;
  for (std::uint32_t v = 0; v < d.vertexCount; v += step) {
    const std::uint8_t *at = chunk.vertices + d.vertexOffset + static_cast<std::size_t>(v) * d.stride;
    float pos[3];
    std::memcpy(pos, at, 12);
    unsigned row = d.matrices[0] & 63;
    if ((d.layout & RE4_GX_V_MATRIX) && mtxAt < d.stride)
      row = at[mtxAt] & 63;
    if (row > 61)
      continue;
    float e[3];
    for (int r = 0; r < 3; r++)
      e[r] = xf.posTex[row + r][0] * pos[0] + xf.posTex[row + r][1] * pos[1] + xf.posTex[row + r][2] * pos[2] +
             xf.posTex[row + r][3];
    const float w = -e[2];
    if (!(w > 1.0f))
      continue;  // behind the camera or at it
    const float cx = pr0[0] * e[0] + pr0[1] * e[2], cy = pr0[2] * e[1] + pr0[3] * e[2], cz = pr1[0] * e[2] + pr1[1];
    const float sx = (vp0[3] - 342.0f) + vp0[0] * cx / w, sy = (vp1[0] - 342.0f) + vp0[1] * cy / w;
    const float sz = (vp0[2] * cz / w + vp1[1]) / 16777215.0f;
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz))
      continue;
    f.x0 = std::min(f.x0, sx);
    f.x1 = std::max(f.x1, sx);
    f.y0 = std::min(f.y0, sy);
    f.y1 = std::max(f.y1, sy);
    zSum += sz;
    counted++;
  }
  if (!counted)
    return;
  f.z = zSum / static_cast<float>(counted);
  const float cx = (f.x0 + f.x1) * 0.5f, cy = (f.y0 + f.y1) * 0.5f, size = std::max(f.x1 - f.x0, f.y1 - f.y0);
  // on the picture, not a sheet over half of it (a fog plane, a beam), not behind the far plane
  if (cx < 0.0f || cy < 0.0f || cx > 640.0f || cy > 480.0f || size > 160.0f || !(f.z > 0.0f && f.z < 1.0f))
    return;
  flareCands.push_back(f);
}

// A small 2D draw's place on the EFB. The rifle scope's blur (0.99.7, report of a Xiaomi with a Mali-G52 MC2: 12-15
// pictures a second in the scope with Vulkan) copies a 128x112 corner of the EFB and draws it back 24 times a picture:
// every copy ends the EFB's render pass, and on a tiling GPU the next pass loads the whole EFB (colour and depth) and
// stores it again at its end. With the draw's place known, Vulkan begins that pass on the corner alone.
bool GxCore::DrawArea(const Re4GxDraw &d, const Re4GxChunk &chunk, int area[4]) const {
  const unsigned primitive = d.primitive & 0xFF;
  if (!(xfReg[0x26] & 1) || primitive == RE4_GX_LINES || primitive == RE4_GX_POINTS || d.vertexCount == 0 ||
      d.vertexCount > 64 || d.stride < 12 || WidenFor(key) != 1.0f)
    return false;
  unsigned mtxAt = 12;
  if (d.layout & RE4_GX_V_NORMAL)
    mtxAt += (d.layout & RE4_GX_V_BINORMAL_TANGENT) ? 36 : 12;
  mtxAt += ((d.layout & RE4_GX_V_COLOR0) ? 4 : 0) + ((d.layout & RE4_GX_V_COLOR1) ? 4 : 0);
  for (int t = 0; t < 8; t++)
    mtxAt += (d.layout & (RE4_GX_V_TEX0 << t)) ? 8 : 0;
  // (the registers themselves: Draw asks before FillXfRegs brings xf.viewport and xf.proj up to date)
  const float vp0[4] = {AsFloat(xfReg[0x1A]), AsFloat(xfReg[0x1B]), 0.0f, AsFloat(xfReg[0x1D])};
  const float vp1[1] = {AsFloat(xfReg[0x1E])};
  const float pr0[4] = {AsFloat(xfReg[0x20]), AsFloat(xfReg[0x21]), AsFloat(xfReg[0x22]), AsFloat(xfReg[0x23])};
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (std::uint32_t v = 0; v < d.vertexCount; v++) {
    const std::uint8_t *at = chunk.vertices + d.vertexOffset + static_cast<std::size_t>(v) * d.stride;
    float pos[3];
    std::memcpy(pos, at, 12);
    unsigned row = d.matrices[0] & 63;
    if ((d.layout & RE4_GX_V_MATRIX) && mtxAt < d.stride)
      row = at[mtxAt] & 63;
    if (row > 61)
      return false;
    float e[2];
    for (int r = 0; r < 2; r++)
      e[r] = xf.posTex[row + r][0] * pos[0] + xf.posTex[row + r][1] * pos[1] + xf.posTex[row + r][2] * pos[2] +
             xf.posTex[row + r][3];
    // orthographic: x' = p0 x + p1, y' = p2 y + p3; the viewport as FlareNote's
    const float sx = (vp0[3] - 342.0f) + vp0[0] * (pr0[0] * e[0] + pr0[1]);
    const float sy = (vp1[0] - 342.0f) + vp0[1] * (pr0[2] * e[1] + pr0[3]);
    if (!std::isfinite(sx) || !std::isfinite(sy))
      return false;
    x0 = std::min(x0, sx);
    x1 = std::max(x1, sx);
    y0 = std::min(y0, sy);
    y1 = std::max(y1, sy);
  }
  // GX pixels with 2 to spare, within the scissor (ApplyState's), then the renderer's pixels (rounded outwards)
  const int xoff = static_cast<int>(bp[0x59] & 0x3FF) * 2, yoff = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
  const int sx0 = std::max(0, static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xoff);
  const int sy0 = std::max(0, static_cast<int>(bp[0x20] & 0x7FF) - yoff);
  const int sx1 = std::min(kEfbWidth, static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xoff + 1);
  const int sy1 = std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yoff + 1);
  const int gx0 = std::max(sx0, static_cast<int>(std::floor(std::max(x0, -1e6f))) - 2);
  const int gy0 = std::max(sy0, static_cast<int>(std::floor(std::max(y0, -1e6f))) - 2);
  const int gx1 = std::min(sx1, static_cast<int>(std::ceil(std::min(x1, 1e6f))) + 2);
  const int gy1 = std::min(sy1, static_cast<int>(std::ceil(std::min(y1, 1e6f))) + 2);
  if (gx1 <= gx0 || gy1 <= gy0)
    return false;  // (nothing on the EFB: as before)
  area[0] = Lo(gx0);
  area[1] = Lo(gy0);
  area[2] = Hi(gx1);
  area[3] = Hi(gy1);
  return true;
}

void GxCore::FlareTrack() {
  std::vector<FlareCand> merged;
  for (const FlareCand &f : flareCands) {
    bool joined = false;
    for (FlareCand &m : merged) {
      const float gap = std::max({f.x0 - m.x1, m.x0 - f.x1, f.y0 - m.y1, m.y0 - f.y1});
      const float joinedSize = std::max(std::max(m.x1, f.x1) - std::min(m.x0, f.x0),
                                        std::max(m.y1, f.y1) - std::min(m.y0, f.y0));
      if (gap < 6.0f && joinedSize <= 48.0f) {
        m.x0 = std::min(m.x0, f.x0);
        m.y0 = std::min(m.y0, f.y0);
        m.x1 = std::max(m.x1, f.x1);
        m.y1 = std::max(m.y1, f.y1);
        m.z = std::min(m.z, f.z);
        m.draws++;
        m.normals += f.normals;
        m.verts = std::max(m.verts, f.verts);
        joined = true;
        break;
      }
    }
    if (!joined)
      merged.push_back(f);
  }
  std::sort(merged.begin(), merged.end(), [](const FlareCand &a, const FlareCand &b) {
    return std::max(a.x1 - a.x0, a.y1 - a.y0) > std::max(b.x1 - b.x0, b.y1 - b.y0);
  });
  {
    std::vector<FlareCand> kept;
    for (const FlareCand &m : merged) {
      const float cx = (m.x0 + m.x1) * 0.5f, cy = (m.y0 + m.y1) * 0.5f;
      bool inside = false;
      for (const FlareCand &k : kept)
        inside = inside || std::hypot(cx - (k.x0 + k.x1) * 0.5f, cy - (k.y0 + k.y1) * 0.5f) <
                               0.35f * std::max(k.x1 - k.x0, k.y1 - k.y0);
      if (!inside)
        kept.push_back(m);
    }
    merged.swap(kept);
  }
  for (FlareSlot &s : flareSlot)
    if (s.lost >= 0)
      s.lost++;
  for (const FlareCand &m : merged) {
    const float cx = (m.x0 + m.x1) * 0.5f, cy = (m.y0 + m.y1) * 0.5f;
    const float size = std::max(m.x1 - m.x0, m.y1 - m.y0), thin = std::min(m.x1 - m.x0, m.y1 - m.y0);
    // not a light: dust and embers, a shaft of light, a cloud of fire or smoke
    if (size < 4.0f || (thin < 3.0f && m.draws < 3) || (size > 3.0f * thin && size > 24.0f) ||
        size > (m.draws > 1 ? 64.0f : 96.0f))
      continue;
    // the flare: about the source's size, from a candle (5) to a torch's fire (18 GameCube pixels); a big fire
    // keeps the torch's size at its middle
    const float r = std::min(18.0f, std::max(5.0f, size * 0.5f));
    int best = -1, freeSlot = -1;
    float bestD = 1e9f;
    for (int i = 0; i < kFlareSlots; i++) {
      const FlareSlot &s = flareSlot[i];
      if (s.lost < 0) {
        if (freeSlot < 0)
          freeSlot = i;
        continue;
      }
      if (s.lost == 0)
        continue;  // taken by another source of this picture
      // (0.99.8: within 64 GameCube pixels, three radii for a big fire; with 24 a light jumped out of its slot
      // while the camera turned fast, its star stayed behind where the light had been and faded for some ten
      // pictures while a new one faded in at the light: stars wandering and flickering along, project owner's
      // dumps of 10.11.2026)
      const float d = std::hypot(s.x - cx, s.y - cy);
      if (d < std::max(64.0f, 3.0f * s.r) && d < bestD) {
        best = i;
        bestD = d;
      }
    }
    if (best < 0 && freeSlot < 0)
      continue;
    FlareSlot &s = flareSlot[best >= 0 ? best : freeSlot];
    s.fresh = best < 0;
    if (s.fresh) {
      s.x = cx;
      s.y = cy;
      s.r = r;
      s.age = 0;
      s.ghost = 0.0f;
    } else {
      // at the light in every picture (followed, it lagged behind the light while the camera turned: project
      // owner's test 04.11.2026, "wandert beim Drehen mit")
      s.x = cx;
      s.y = cy;
      s.r += (r - s.r) * 0.15f;
    }
    s.z = m.z;
    s.size = s.fresh ? size : s.size + (size - s.size) * 0.3f;
    s.lost = 0;
    s.age = std::min(s.age + 1, 1000);
  }
  for (FlareSlot &s : flareSlot) {
    if (s.lost > 16)
      s.lost = -1;
    if (s.lost > 0)
      s.age = 0;
  }
  // the ghosts: the three largest lights that flare, by size; each slot's share followed smoothly
  int order[kFlareSlots], ranked = 0;
  for (int i = 0; i < kFlareSlots; i++)
    if (flareSlot[i].lost == 0 && flareSlot[i].age >= 4)
      order[ranked++] = i;
  std::sort(order, order + ranked, [this](int a, int b) { return flareSlot[a].r > flareSlot[b].r; });
  static const float kShare[3] = {1.0f, 0.6f, 0.4f};
  for (int i = 0; i < kFlareSlots; i++) {
    float want = 0.0f;
    for (int j = 0; j < ranked && j < 3; j++)
      if (order[j] == i)
        want = kShare[j];
    flareSlot[i].ghost += (want - flareSlot[i].ghost) * 0.12f;
    if (flareSlot[i].lost < 0)
      flareSlot[i].ghost = 0.0f;
  }
  // lights close together (the candles of one holder) share one glow's brightness, so they do not add up to a
  // white blot
  for (int i = 0; i < kFlareSlots; i++) {
    FlareSlot &s = flareSlot[i];
    if (s.lost < 0) {
      s.crowd = 1.0f;
      continue;
    }
    int near = 0;
    for (const FlareSlot &o : flareSlot)
      if (o.lost >= 0 && o.age >= 4 && std::hypot(o.x - s.x, o.y - s.y) < 2.5f * std::max(s.r, o.r))
        near++;
    // and a big lamp (a box over 60 pixels: a floodlight) at most 40 % of a candle's star
    const float big = std::min(1.0f, std::max(0.0f, (s.size - 60.0f) / 60.0f));
    const float want = (1.0f - 0.6f * big) / std::sqrt(static_cast<float>(std::max(1, near)));
    s.crowd += (want - s.crowd) * 0.2f;
  }
  if (flareLog && flarePictures < 400) {
    int used = 0;
    for (const FlareSlot &s : flareSlot)
      used += s.lost >= 0;
    GXLOG("lens flares: picture %u, %zu additive 3D draws, %zu sources, %d slots", flarePictures, flareCands.size(),
          merged.size(), used);
    for (const FlareCand &m : merged)
      GXLOG("lens flares:   source (%.0f %.0f)-(%.0f %.0f) depth %.5f, %d draws, %d with normals, up to %u vertices, "
            "z %06x blend %06x tev %06x layout %08x, owner %08x %08x table %u life %08x", m.x0, m.y0, m.x1, m.y1, m.z,
            m.draws, m.normals, m.verts, m.zmode & 0xFFFFFF, m.blend & 0xFFFFFF, m.tevc & 0xFFFFFF, m.layout, m.owner[0],
            m.owner[1], m.owner[2], m.owner[3]);
  }
  flarePictures++;
}

bool GxCore::CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const {
  const GxCore &m = *this;
  const std::uint32_t refuse = RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_HOLD | RE4_GX_CHUNK_BLACK | RE4_GX_CHUNK_MARK;
  if (m.interpPass || a.version != RE4_GX_FRAME_VERSION || b.version != RE4_GX_FRAME_VERSION ||
      (a.flags & refuse) || !(a.flags & RE4_GX_CHUNK_XFB) || m.viBlack)
    return false;
  // the EFB copies for textures in `a`: their targets (the BP state at that moment decides them); and
  // which copy each texture map holds (0: none), from the chunk run last on
  std::vector<std::uint32_t> bp(m.bp, m.bp + 256), pending;
  std::uint32_t mapCopy[8];
  for (int i = 0; i < 8; i++)
    mapCopy[i] = (m.boundKey[i] >> 63) ? static_cast<std::uint32_t>(m.boundKey[i]) : 0;
  auto texture = [&](const std::uint32_t *p, std::uint32_t n, Re4GxTexture *t) {
    if (n * 4 < sizeof(Re4GxTexture))
      return false;
    std::memcpy(t, p, sizeof(*t));
    mapCopy[t->map & 7] = (t->flags & RE4_GX_TEX_COPY) ? t->address : 0;
    return true;
  };
  for (std::uint32_t at = 0; at < a.opWords;) {
    const std::uint32_t head = a.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > a.opWords)
      return false;
    const std::uint32_t *p = a.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1))
        pending.push_back((bp[0x4B] & 0x1FFFFF) << 5);
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    } else if (op == RE4_GX_OP_TEXTURE) {
      texture(p, n, &t);
    }
    at += 1 + n;
  }
  if (pending.empty())
    return true;
  auto isPending = [&](std::uint32_t address) {
    return address && std::find(pending.begin(), pending.end(), address) != pending.end();
  };
  // `b` must copy to each of them again (else a later chunk reads a copy `a` never made), and must not
  // read one before: a texture op for it, or a draw whose TEV stages sample a map still holding it
  // (until 26.10.2026 any draw counted: almost every picture of a room makes a copy, shadows and the
  // depth of field, and draws before it copies, so Frame skip almost never skipped)
  for (std::uint32_t at = 0; at < b.opWords && !pending.empty();) {
    const std::uint32_t head = b.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > b.opWords)
      return false;
    const std::uint32_t *p = b.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_DRAW) {
      const std::uint32_t gen = bp[0x00];
      const unsigned stages = ((gen >> 10) & 15) + 1, indStages = (gen >> 16) & 7;
      for (unsigned st = 0; st < stages; st++) {
        const std::uint32_t o = bp[0x28 + st / 2] >> ((st & 1) ? 12 : 0);
        if (((o >> 6) & 1) && isPending(mapCopy[o & 7]))
          return false;
      }
      for (unsigned st = 0; st < indStages && st < 4; st++)
        if (isPending(mapCopy[(bp[0x27] >> (6 * st)) & 7]))
          return false;
    } else if (op == RE4_GX_OP_TEXTURE) {
      if (!texture(p, n, &t))
        return false;
      if ((t.flags & RE4_GX_TEX_COPY) && isPending(t.address))
        return false;
    } else if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1)) {
        const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
        pending.erase(std::remove(pending.begin(), pending.end(), dest), pending.end());
      }
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    }
    at += 1 + n;
  }
  return pending.empty();
}
void GxCore::Skip(const Re4GxChunk &a) {
  this->skipDraws = true;
  Run(a);
  this->skipDraws = false;
  this->recValid = false;  // 60 FPS: no in-between picture across a chunk left out
}
void GxCore::SetMerge(bool on) { this->mergeOn = on; }
bool GxCore::Merging() const { return this->mergeOn && this->BaseVertex() && !this->recordDraws; }
void GxCore::SetRecording(bool on) {
  GxCore &m = *this;
  m.recordDraws = on;
  if (!on) {
    m.recValid = false;
    m.rec.clear();
    m.recFloats.clear();
  }
}
bool GxCore::XfbWide() const { return this->haveXfb && this->xfbWide; }
void GxCore::SetOptions(const GxOptions &options) { this->options = options; }
void GxCore::SetDepthSink(DepthSink sink) { this->depthSink = std::move(sink); }
void GxCore::SetFxaa(bool on) { this->fxaaOn = on; }
void GxCore::SetSharpen(int level) { this->sharpLevel = level < 0 ? 0 : level > 2 ? 2 : level; }
void GxCore::SetPsx(bool on) { this->psxOn = on; }
void GxCore::SetCrt(int level) { this->crtLevel = level < 0 ? 0 : level > 2 ? 2 : level; }
void GxCore::SetColour(int mode, int level) {
  this->colourMode = mode < 0 || mode > 2 ? 0 : mode;
  this->colourStrength = level >= 2 ? 1.0f : 0.5f;
}
void GxCore::SetFlares(bool on) { this->flaresOn = on; }
void GxCore::SetFlareHalos(bool on) { this->flareHalos = on; }
void GxCore::SetDofStrong(bool on) { this->dofStrong = on; }
void GxCore::SetTvFilter(bool on) { this->tvFilter = on; }
void GxCore::SetBandwidthSave(bool on) { this->bwSave = on; }
void GxCore::SetWiden(float factor) {
  this->widen2d.store(factor > 0.5f && factor < 2.0f ? factor : 1.0f, std::memory_order_relaxed);
}
void GxCore::SetShownRows(int top, int bottom) {
  this->shownTop.store(std::max(0, std::min(top, kEfbHeight)), std::memory_order_relaxed);
  this->shownBottom.store(std::max(0, std::min(bottom, kEfbHeight)), std::memory_order_relaxed);
}
int GxCore::Samples() const { return this->samples; }
float GxCore::Anisotropy() const { return this->anisotropy; }
bool GxCore::Invalidates() const { return this->options.invalidate; }
bool GxCore::HalfDof() const { return this->options.halfDof; }
int GxCore::Scale() const { return this->halves / 2; }
int GxCore::Halves() const { return this->halves; }
const GxStats &GxCore::Stats() const { return this->stats; }
void GxCore::SetProgramList(const std::string &path) { this->programList = path; }
void GxCore::SetRoom(unsigned room) {
  GxCore &m = *this;
  if (room == m.room)
    return;
  if (m.roomDirty)
    m.SaveRoom();
  m.room = room;
  m.roomUsed.clear();
  m.aheadRead = false;  // Prewarm reads the new room's list
}
std::uint32_t GxCore::Op(const Re4GxChunk &chunk, std::uint32_t at) {
  const std::uint32_t head = chunk.ops[at];
  const std::uint32_t op = head & 0xFF, n = head >> 8;
  const std::uint32_t *p = chunk.ops + at + 1;
  if (at + 1 + n > chunk.opWords)
    return chunk.opWords;
  switch (op) {
  case RE4_GX_OP_BP:
    if (n >= 1)
      BpWrite(p[0], true);
    break;
  case RE4_GX_OP_BP_STATE:
    for (std::uint32_t r = 0; r < n && r < 256; r++)
      BpWrite(r << 24 | (p[r] & 0xFFFFFF), false);
    break;
  case RE4_GX_OP_XF:
    if (n >= 1)
      XfWrite(p[0], p + 1, n - 1);
    break;
  case RE4_GX_OP_TLUT:
    if (n >= 3 && p[0] < kTlutBytes && p[2] + p[1] <= chunk.blobBytes)
      std::memcpy(tlut.data() + p[0], chunk.blob + p[2], std::min<std::uint32_t>(p[1], kTlutBytes - p[0]));
    break;
  case RE4_GX_OP_TEXTURE:
    if (n * 4 >= sizeof(Re4GxTexture)) {
      Re4GxTexture t;
      std::memcpy(&t, p, sizeof(t));
      BindTexture(t, chunk);
    }
    break;
  case RE4_GX_OP_OWNER:
    if (n >= 4)
      SetOwner(p);
    break;
  case RE4_GX_OP_FORGET:
    // the real Run forgets (the in-between pass runs before it). A texture the core sends again in
    // this chunk (it forgot it for its budget) the in-between pass uploaded already: after the forget
    // it is uploaded again, else it stays gone and draws white (itel P661N, 28.10.2026: Ganados with
    // white weapons after some 20 minutes with 60 FPS)
    if (n >= 2 && !interpPass) {
      const std::uint64_t key = static_cast<std::uint64_t>(p[1]) << 32 | p[0];
      Forget(key);
      interpUploads.erase(key);
    }
    break;
  case RE4_GX_OP_DRAW:
    if (!skipDraws && n * 4 >= sizeof(Re4GxDraw)) {
      Re4GxDraw d;
      std::memcpy(&d, p, sizeof(d));
      Draw(d, chunk);
    }
    break;
  default:
    break;
  }
  Split(op == RE4_GX_OP_BP || op == RE4_GX_OP_BP_STATE ? GxStats::kSplitBp
        : op == RE4_GX_OP_XF                          ? GxStats::kSplitXf
        : op == RE4_GX_OP_TEXTURE                     ? GxStats::kSplitTexture
                                                      : GxStats::kSplitRest);
  return at + 1 + n;
}

void GxCore::Run(const Re4GxChunk &chunk) {
  GxCore &m = *this;
  if (chunk.version != RE4_GX_FRAME_VERSION)
    return;
  if (!m.interpPass)
    m.stats.chunks++;
  if (m.roomDirty && m.stats.chunks - m.roomSaved >= 900)  // every 30 s while new keys come
    m.SaveRoom();
  m.mergeValid = false;
  m.splitAt = std::chrono::steady_clock::now();
  // a real Run notes its draws for the in-between picture after it
  const bool record = m.recordDraws && !m.interpPass;
  if (record) {
    m.recNew.clear();
    m.recFloatsNew.clear();
    m.recOwnerDrawsNew.clear();
  }
  // the API's state before the chunk (GL: its caches forgotten, the depth maps the GPU finished since the
  // last chunk to the core, the uniform stream's next place, its vertex array)
  m.RunBegin();
  m.holdXfb = (chunk.flags & RE4_GX_CHUNK_HOLD) != 0;
  m.chunkWide = (chunk.flags & RE4_GX_CHUNK_WIDE) != 0;
  m.chunkMovie = (chunk.flags & RE4_GX_CHUNK_MOVIE) != 0;
  if (chunk.flags & RE4_GX_CHUNK_XFB)
    m.viBlack = (chunk.flags & RE4_GX_CHUNK_BLACK) != 0;
  if ((chunk.flags & RE4_GX_CHUNK_RESET) && !m.interpPass) {
    m.stats.resets++;
    m.DropTextures();
  }
  const bool uploaded = !m.interpPass && m.uploadedBy == &chunk && m.uploadedSequence == chunk.sequence;
  m.uploadedBy = nullptr;
  // merged draw calls: the chains and their indices (after the chunk's in ibo)
  m.mergeMarks.clear();
  m.mergeNext = 0;
  if (m.mergeOn && m.BaseVertex() && !m.interpPass && !m.recordDraws && !m.skipDraws && !uploaded)
    m.MergePlan(chunk);
  if (!m.skipDraws && !uploaded) {
    // the vertices and indices (and the merged chains' indices) to the GPU
    m.UploadChunk(chunk);
    if (m.interpPass) {
      m.uploadedBy = &chunk;
      m.uploadedSequence = chunk.sequence;
    }
  }
  m.Split(GxStats::kSplitVertices);
  // a new pass, the state other code of the app may have left, the uniform blocks, the GPU timer
  m.RunState();
  m.ownerBase = 0;
  m.ownerLife = ~0u;
  m.ownerWords[0] = m.ownerWords[1] = 0;
  m.ownerWords[2] = 0xFF;
  m.ownerWords[3] = ~0u;
  m.Split(GxStats::kSplitState);
  // lens flares: after the scene's last 3D draw (FlareScan); its light sources noted on the way (FlareNote)
  // none on a menu screen of the game (RE4_GX_CHUNK_STILL: the option screen, the retry / load menu, sub screens,
  // card screens, results; project owner 04.11.2026: stars on the "NEUER VERSUCH oder LADEN" menu's letters)
  m.flareAfter = m.flaresOn && !m.skipDraws && !(chunk.flags & RE4_GX_CHUNK_STILL) ? m.FlareScan(chunk) : 0;
  m.flareDone = false;
  m.flareCands.clear();
  std::uint32_t at = 0;
  while (at < chunk.opWords) {
    m.curOpAt = at;
    // a draw that starts a run of a chain (MergePlan): the run as one draw call
    while (m.mergeNext < m.mergeMarks.size() && m.mergeMarks[m.mergeNext].op < at)
      m.mergeNext++;
    if (m.mergeNext < m.mergeMarks.size() && m.mergeMarks[m.mergeNext].op == at)
      at = m.DrawMerged(chunk, at);
    else
      at = m.Op(chunk, at);
    if (m.flareAfter && !m.flareDone && at >= m.flareAfter)
      m.FlarePass();
  }
  m.RunEnd();  // the GPU timer, the fence after the chunk (GxStats::behind*)
  if (record) {
    m.rec.swap(m.recNew);
    m.recOwnerDraws.swap(m.recOwnerDrawsNew);
    m.recFloats.swap(m.recFloatsNew);
    m.recSequence = chunk.sequence;
    m.recValid = true;
  } else if (!m.interpPass) {
    m.recValid = false;
  }
  if (!m.interpPass) {
    // the in-between pass's uploads served this Run
    m.skipInterpUploads = false;
    m.interpUploads.clear();
  }
  // the API left as the app's other renderers expect it
  m.RunLeave();
  m.Split(GxStats::kSplitRest);
}


void GxCore::Draw(const Re4GxDraw &d, const Re4GxChunk &chunk, const MergeCall *merge) {
  // (every draw op counts for "n-th draw of its object", as Interpolate counts them ahead)
  curOwner = ownerBase ? (ownerBase ^ (static_cast<std::uint64_t>(ownerDraws++) + 1) * 0x9E3779B97F4A7C15ull) | 1 : 0;
  curLife = ownerLife;
  if (ownerBase && recordDraws && !interpPass)
    recOwnerDrawsNew[ownerBase]++;
  Split(GxStats::kSplitRest);
  if (d.indexCount == 0 || d.vertexOffset + static_cast<std::uint64_t>(d.vertexCount) * d.stride > chunk.vertexBytes ||
      d.indexOffset + d.indexCount > chunk.indexCount)
    return;
  // the shader: the one of the last draw while no register that shapes it changed
  if (keyDirty || d.layout != keyLayout) {
    key = MakeKey(d);
    if (KeyProgram())
      NoteKey(key);
    keyLayout = d.layout;
    keyDirty = false;
  }
  const void *p = DrawProgram(merge != nullptr);
  Split(GxStats::kSplitProgram);
  if (!p)
    return;
  const ShaderKey &k = key;
  const unsigned primitive = d.primitive & 0xFF;
  int area[4];
  if (!merge && !interpPass && DrawArea(d, chunk, area))
    UseEfbArea(area[0], area[1], area[2], area[3]);
  else
    UseEfb();
  ApplyState(primitive);
  const float pointSize = static_cast<float>((bp[0x22] >> 8) & 0xFF) / 6.0f * Sf(1);
  if (xf.proj[1][3] != pointSize) {
    xf.proj[1][3] = pointSize;
    Dirty(207, 208);
  }
  if (xfRegsDirty)
    FillXfRegs();
  Split(GxStats::kSplitState);
  std::uint32_t blended = 0;
  if (interpPass)
    blended = BlendDraw(d, k, chunk);
  else if (recordDraws)
    RecordDraw(d, k);
  // a light source for the lens flares (with the in-between picture's blended matrices)
  if (flaresOn && flareAfter && !merge)
    FlareNote(d, chunk, curOpAt);
  Split(GxStats::kSplitRest);
  // (the merged block first: a move of the stream it causes sends the other parts again)
  if (merge)
    UploadMerged(merge->draws);
  UploadXf(merge != nullptr);
  if (pxDirty || !pxValid) {
    FillPixel(k);
    pxDirty = false;
    if (!pxValid || std::memcmp(&px, &pxSent, sizeof(px)) != 0) {
      UploadUniform(kPixelBinding, &px, 0, sizeof(PixelBlock), sizeof(PixelBlock));
      mergePixel = true;
      pxSent = px;
      pxValid = true;
    }
  }
  Split(GxStats::kSplitUniform);
  // the program, its constants, the textures, the vertices and the draw call
  SubmitDraw(d, p, merge);
  if (!interpPass) {
    const MergeSnap now{stats.uniformUploads, stats.programSwitches, stats.textureBinds, stats.stateChanges,
                        stats.attributeSetups, stats.textureCopies, stats.displayCopies, primitive};
    if (mergeValid && !blended && now.programs == mergeLast.programs && now.binds == mergeLast.binds &&
        now.states == mergeLast.states && now.setups == mergeLast.setups && now.copies == mergeLast.copies &&
        now.displays == mergeLast.displays && now.mode == mergeLast.mode) {
      if (now.uniforms == mergeLast.uniforms && !mergeMatrix)
        stats.mergeSame++;
      else {
        stats.mergeUniform++;
        if (!mergePixel)
          stats.mergeMatrixOnly++;
      }
    }
    mergeLast = now;
    mergeValid = !blended;
    mergeMatrix = mergePixel = false;
  }
  stats.draws += merge ? merge->draws : 1;
  if (merge) {
    stats.mergeBatches++;
    stats.mergeTaken += merge->draws;
  }
  if (primitive != RE4_GX_LINES && primitive != RE4_GX_POINTS)
    stats.triangles += (merge ? merge->count : d.indexCount) / 3;
  if (blended)
    BlendRestore(blended);
}

void GxCore::ApplyState(unsigned primitive) {
  // the picture's extent: the full-width scissors the draws of this picture ran under
  {
    const int xo = static_cast<int>(bp[0x59] & 0x3FF) * 2, yo = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
    const int sx0 = static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xo, sx1 = static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xo + 1;
    if (sx1 - sx0 >= 500)
      sceneBottom = std::max(sceneBottom, std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yo + 1));
  }
  // the registers below, as last applied: nothing to do while they hold
  const std::uint32_t sig[10] = {bp[0x20],     bp[0x21],     bp[0x59],       bp[0x40] & 0x1F, bp[0x41] & 0xFFFF,
                                 bp[0x43] & 7, (bp[0x00] >> 14) & 3, bp[0x22] & 0xFF, primitive, bp[0x42] & 0x1FF};
  if (stateValid && std::memcmp(sig, stateSig, sizeof(sig)) == 0)
    return;
  std::memcpy(stateSig, sig, sizeof(sig));
  stateValid = true;
  stats.stateChanges++;
  DrawState s;
  // scissor: registers in EFB pixels + 342, minus the scissor offset (BP 0x59, in 2 pixels)
  const int xoff = static_cast<int>(bp[0x59] & 0x3FF) * 2, yoff = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
  const int x0 = std::max(0, static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xoff);
  const int y0 = std::max(0, static_cast<int>(bp[0x20] & 0x7FF) - yoff);
  const int x1 = std::min(kEfbWidth, static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xoff + 1);
  const int y1 = std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yoff + 1);
  s.scissor[0] = Lo(x0);
  s.scissor[1] = Lo(y0);
  s.scissor[2] = std::max(0, Lo(x1) - Lo(x0));
  s.scissor[3] = std::max(0, Lo(y1) - Lo(y0));
  // depth
  const std::uint32_t z = bp[0x40];
  s.depthTest = (z & 1) != 0;
  s.depthFunc = (z >> 1) & 7;
  s.depthWrite = s.depthTest && ((z >> 4) & 1);
  // blending, colour and alpha update
  const std::uint32_t c = bp[0x41];
  const bool alphaFormat = (bp[0x43] & 7) == 1; // RGBA6_Z24 keeps alpha
  const bool alphaWrite = ((c >> 4) & 1) && alphaFormat, colorWrite = (c >> 3) & 1;
  s.colorWrite = colorWrite;
  s.alphaWrite = alphaWrite;
  // destination alpha (GXSetDstAlpha, BP 0x42): the EFB alpha gets this
  // constant instead of the TEV alpha, which still drives the blending; the
  // shader writes it (PX alphaRef.z) unless the colour blending needs the
  // TEV alpha (colour written with SRC_ALPHA factors), then the alpha blend
  // factors make it (0 only: other values would need dual-source blending;
  // counted in Stats().dstAlphaApprox)
  const bool dstAlpha = alphaWrite && ((bp[0x42] >> 8) & 1);
  const int dstValue = static_cast<int>(bp[0x42] & 0xFF);
  int shaderAlpha = -1;
  using F = DrawState;
  if ((c >> 11) & 1) { // subtract: dst - src
    s.blend = true;
    s.eqColor = F::kReverseSubtract;
    s.eqAlpha = dstAlpha ? F::kAdd : F::kReverseSubtract;
    s.srcColor = s.dstColor = s.srcAlpha = F::kOne;
    s.dstAlpha = dstAlpha ? F::kZero : F::kOne;
    if (dstAlpha)
      shaderAlpha = dstValue;
  } else if (c & 1) {
    static const DrawState::Factor src[8] = {F::kZero,     F::kOne,           F::kDstColor, F::kOneMinusDstColor,
                                             F::kSrcAlpha, F::kOneMinusSrcAlpha, F::kDstAlpha, F::kOneMinusDstAlpha};
    static const DrawState::Factor dst[8] = {F::kZero,     F::kOne,           F::kSrcColor, F::kOneMinusSrcColor,
                                             F::kSrcAlpha, F::kOneMinusSrcAlpha, F::kDstAlpha, F::kOneMinusDstAlpha};
    const unsigned sf = (c >> 8) & 7, df = (c >> 5) & 7;
    s.blend = true;
    s.eqColor = s.eqAlpha = F::kAdd;
    s.srcColor = src[sf];
    s.dstColor = dst[df];
    if (!dstAlpha) {
      s.srcAlpha = src[sf];
      s.dstAlpha = dst[df];
    } else if (!colorWrite || (sf != 4 && sf != 5 && df != 4 && df != 5)) {
      s.srcAlpha = F::kOne; // TEV alpha not needed: the shader writes it
      s.dstAlpha = F::kZero;
      shaderAlpha = dstValue;
    } else {
      s.srcAlpha = s.dstAlpha = F::kZero;
      if (dstValue != 0)
        stats.dstAlphaApprox++;
    }
  } else if ((c >> 1) & 1) { // logic operations: the common ones
    const unsigned op = (c >> 12) & 15;
    s.blend = true;
    s.eqColor = s.eqAlpha = F::kAdd;
    if (op == 0) { // clear
      s.srcColor = F::kZero;
      s.dstColor = F::kZero;
    } else if (op == 5) { // noop
      s.srcColor = F::kZero;
      s.dstColor = F::kOne;
    } else if (op == 15) { // set: approximated
      s.srcColor = F::kOne;
      s.dstColor = F::kOne;
    } else { // copy and the rest
      s.srcColor = F::kOne;
      s.dstColor = F::kZero;
    }
    s.srcAlpha = s.srcColor;
    s.dstAlpha = s.dstColor;
    if (dstAlpha && op != 0 && op != 5 && op != 15)
      shaderAlpha = dstValue;
  } else {
    s.blend = false;
    if (dstAlpha)
      shaderAlpha = dstValue;
  }
  if (px.alphaRef[2] != shaderAlpha) {
    px.alphaRef[2] = shaderAlpha;
    pxDirty = true;
  }
  // culling: only triangles (hardware 1 = back, 2 = front, 3 = all)
  const unsigned cull = (bp[0x00] >> 14) & 3;
  s.cull = primitive != RE4_GX_TRIANGLES ? 0 : cull;
  // lines and points: width in 1/6 pixel
  s.lineWidth = static_cast<float>(bp[0x22] & 0xFF) / 6.0f * Sf(1);
  ApplyDrawState(s);
}

void GxCore::UploadXf(bool merged) {
  for (int round = 0; round < 2; round++) {
    if (streamMoved) {
      streamMoved = false;
      Dirty(0, kXfPartRow[kXfParts]);
      pxValid = false;
      for (bool &v : xfSentValid)
        v = false;
    }
    for (int i = merged ? 1 : 0; i < kXfParts; i++) {
      if (dirtyLo[i] >= dirtyHi[i])
        continue;
      const std::size_t first = static_cast<std::size_t>(kXfPartRow[i]) * 16;
      const std::size_t partBytes = static_cast<std::size_t>(kXfPartRow[i + 1] - kXfPartRow[i]) * 16;
      const auto *now = reinterpret_cast<const std::uint8_t *>(&xf) + first;
      auto *sent = reinterpret_cast<std::uint8_t *>(&xfSent) + first;
      // the rows that changed are as sent: the part bound now still holds them (in the stream
      // a part is never written again in place; the small buffers keep what they got)
      const std::size_t lo = static_cast<std::size_t>(dirtyLo[i]) * 16, hi = static_cast<std::size_t>(dirtyHi[i]) * 16;
      // RE4_GX_UNIFORM_SAME=0: every changed part sent (the counter-check)
      static const bool sendSame = std::getenv("RE4_GX_UNIFORM_SAME") && *std::getenv("RE4_GX_UNIFORM_SAME") == '0';
      if (!sendSame && xfSentValid[i] && std::memcmp(now + (lo - first), sent + (lo - first), hi - lo) == 0) {
        stats.uniformSame++;
        dirtyLo[i] = kXfPartRow[kXfParts];
        dirtyHi[i] = 0;
        continue;
      }
      std::memcpy(sent, now, partBytes);
      xfSentValid[i] = true;
      UploadUniform(kXfPartBinding[i], reinterpret_cast<const std::uint8_t *>(&xf) + first,
                  static_cast<std::size_t>(dirtyLo[i]) * 16 - first,
                  static_cast<std::size_t>(dirtyHi[i] - dirtyLo[i]) * 16,
                  static_cast<std::size_t>(kXfPartRow[i + 1] - kXfPartRow[i]) * 16);
      dirtyLo[i] = kXfPartRow[kXfParts];
      dirtyHi[i] = 0;
    }
    if (!streamMoved)
      break;
  }
}

void GxCore::UploadMerged(unsigned draws) {
  const std::size_t bytes = offsetof(MergeBlock, xf) + static_cast<std::size_t>(draws) * sizeof(mergeBlock.xf[0]);
  UploadUniform(kMergeBinding, &mergeBlock, 0, bytes, sizeof(MergeBlock), bytes);
}

std::uint32_t GxCore::BlendDraw(const Re4GxDraw &d, const ShaderKey &k, const Re4GxChunk &chunk) {
  const bool flat = k.orthographic;
  std::uint32_t first = 0, rows = 0;
  bool lights = false;
  DrawRows(d, &first, &rows);
  if (flat && rows != 3)
    return 0;
  if (!flat)
    ipDraws++;
  float t[3] = {};
  if (rows)
    for (int c = 0; c < 3; c++)
      t[c] = xf.posTex[first + c][3];
  const std::uint64_t sig = DrawSig(d, k);
  // its kind for the counts (60fps-plan.md S0): 0 a particle, 1 a figure (matrices per vertex),
  // 2 the room and other 3D, 3 2D
  const int kind = flat ? 3 : rows == 30 ? 1 : rows == 3 && d.vertexCount <= 16 ? 0 : 2;
  stats.interpKind[kind]++;
  // the object's draw of the picture before (S2): the same object, the same shape; a particle's
  // slot used anew (another effect, fewer life frames) is a new one, drawn as in the next picture
  const DrawRec *a = nullptr;
  bool owned = false;
  bool sameCount = false;
  if (curOwner) {
    auto pc = recOwnerDraws.find(ownerBase), nc = nextOwnerDraws.find(ownerBase);
    sameCount = pc != recOwnerDraws.end() && nc != nextOwnerDraws.end() && pc->second == nc->second;
    if (!sameCount)
      stats.interpOwnedRecount++;
  }
  if (curOwner && sameCount) {
    auto o = recByOwner.find(curOwner);
    // not a particle, the same vertices and position rows, only another shader or texture matrix index:
    // still that draw. Its position is blended, its texture matrices and colours (S3) are not. Retroid
    // series 01.11.2026, r101: a house door switched a texture matrix index between identity and a row
    // from one game picture to the next; unmatched, its lower half was drawn where it is in the next
    // picture, a step against the wall around it in each in-between picture (flickering walls)
    const bool sameShape = o != recByOwner.end() && rec[o->second].shape == DrawSig(d, k, false);
    const bool samePlace = o != recByOwner.end() && kind != 0 && rec[o->second].rows == rows &&
                           rec[o->second].posRow == first && rec[o->second].vertexCount == d.vertexCount &&
                           rec[o->second].stride == d.stride && rec[o->second].layout == d.layout;
    if (o != recByOwner.end() && !recUsed[o->second] && (sameShape || samePlace)) {
      const DrawRec &c = rec[o->second];
      recUsed[o->second] = 1;
      if (c.life != ~0u && curLife != ~0u &&
          ((c.life >> 16) != (curLife >> 16) || (curLife & 0xFFFF) < (c.life & 0xFFFF))) {
        stats.interpOwnedNew++;
        stats.interpKindUnmatched[kind]++;
        return flat ? 0 : BlendByCamera(first, rows);
      }
      // a particle that jumped: the weather particles of esp15.cpp (rain, snow, fog) wrap around in a box around the
      // camera, the path sprites of esp06.cpp start their path again. Halfway it stood where it is in neither picture:
      // drawn as a new one (the project owner's GX series 08.11.2026, r227 in fog: a sprite 7 m to the side, a
      // brighter fog bank in one in-between picture now and then, 60 FPS only)
      if (kind == 0 && c.rows == 3 && c.posRow == first && Jumped(c.t, t)) {
        stats.interpOwnedJumps++;
        stats.interpKindUnmatched[kind]++;
        return BlendByCamera(first, rows);
      }
      a = &c;
      owned = sameShape;
      stats.interpOwned++;
    }
  }
  // a one-frame effect the game makes anew every picture (the laser sight: EspDrawLaserLine and drawPoint select
  // a new cEsp each picture, life 1, mostly in another slot): not new, the same effect as a one-frame one of its
  // id and look in the picture before (by MatchDraw among those; draws of objects that go on are reserved), its
  // rows and ends blended. Taken for new, the beam was moved by the camera's guessed move instead and its far end
  // lay 9-10 pixels beside both pictures' in every in-between picture (users 06.11.2026: the laser briefly
  // doubled while it moved; PC series r101, aiming and turning)
  bool oneFrame = false;
  if (!a && curOwner && curLife != ~0u && (curLife & 0xFFFF) <= 1) {
    auto same = recBySig.find(sig);
    if (same != recBySig.end()) {
      oneFrameCands.clear();
      for (std::uint32_t i : same->second)
        if (rec[i].life != ~0u && (rec[i].life >> 16) == (curLife >> 16) && (rec[i].life & 0xFFFF) <= 1)
          oneFrameCands.push_back(i);
      if (!oneFrameCands.empty() && (a = MatchDraw(oneFrameCands, t, rows, d.vertexCount, flat)) != nullptr) {
        oneFrame = true;
        stats.interpOneFrame++;
      }
    }
  }
  // a particle whose slot drew nothing in the picture before: a new one (a turn of the r307 puzzle
  // starts sparkles), drawn as in the next picture; guessed, it took another's place
  if (!a && curOwner && curLife != ~0u && !recOwnerDraws.count(ownerBase)) {
    stats.interpOwnedNew++;
    stats.interpKindUnmatched[kind]++;
    return flat ? 0 : BlendByCamera(first, rows);
  }
  auto it = recBySig.find(sig);
  if (!a)
    a = it != recBySig.end() ? MatchDraw(it->second, t, rows, d.vertexCount, flat) : nullptr;
  // a particle with none of its look: by the look without its textures. Flames show a new picture
  // of their animation, a texture of its own, every game picture (Retroid report 26.10.2026, the
  // merchant's brazier: unmatched, the flames were drawn where they are in the next picture and
  // lagged behind the brazier while the camera turned). The texture stays the next picture's.
  if (!a && !flat && rows == 3 && d.vertexCount <= 16) {
    auto look = recByLook.find(DrawSig(d, k, false));
    if (look != recByLook.end())
      a = MatchDraw(look->second, t, rows, d.vertexCount);
  }
  if (a && !flat && rows == 3 && a->rows == 3 && a->posRow == first && d.vertexCount >= 64) {
    float now[3][4];
    std::memcpy(now, xf.posTex[first], sizeof(now));
    NoteCamera(sig, recFloats.data() + a->data, now);
  }
  if (!a) {
    stats.interpKindUnmatched[kind]++;
    return flat ? 0 : BlendByCamera(first, rows);
  }
  const float s = interpT;
  const float *src = recFloats.data() + a->data;
  if (flat) {
    const float dx = xf.posTex[first][3] - src[3], dy = xf.posTex[first + 1][3] - src[7];
    if (a->rows != 3 || a->posRow != first || !(dx * dx + dy * dy <= kFlatMove * kFlatMove))
      return 0;
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        if (!(std::fabs(xf.posTex[first + r][c] - src[r * 4 + c]) <= 1e-3f * std::max(1.0f, std::fabs(src[r * 4 + c]))))
          return 0;
  } else {
    ipMatched++;
  }
  if (rows && a->rows == rows && a->posRow == first) {
    // turned more than 20 degrees: a cut of the camera, most likely (Interpolate counts them)
    if (rows == 3 && !flat) {
      float lowest = 1.0f;
      for (int r = 0; r < 3; r++) {
        const float *pa = src + r * 4, *pb = xf.posTex[first + r];
        const float la = std::sqrt(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]);
        const float lb = std::sqrt(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
        if (la > 0.0f && lb > 0.0f)
          lowest = std::min(lowest, (pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2]) / (la * lb));
      }
      if (lowest < 0.94f)
        ipTurned++;
    }
    float now[3][4] = {};
    if (rows == 3)
      std::memcpy(now, xf.posTex[first], sizeof(now));
    for (std::uint32_t r = 0; r < rows; r++)
      for (int c = 0; c < 4; c++)
        xf.posTex[first + r][c] = src[r * 4 + c] + (xf.posTex[first + r][c] - src[r * 4 + c]) * s;
    // the lights (in view space, as the game loaded them for the next picture) moved with the
    // camera between the two: tester's report 24.10.2026, light pools that swam in 60 FPS
    if (rows == 3 && !flat && BlendLights(now, xf.posTex[first]))
      lights = true;
    const float *nrm = src + rows * 4;
    for (std::uint32_t r = 0; r < rows; r++)
      for (int c = 0; c < 3; c++)
        xf.normal[(first + r) & 31][c] = nrm[r * 3 + c] + (xf.normal[(first + r) & 31][c] - nrm[r * 3 + c]) * s;
    Dirty(static_cast<int>(first), static_cast<int>(first + rows));
    Dirty(64, 96);
  }
  // the projection (a zoom): between the two
  const float *proj = src + rows * 4 + rows * 3;
  if (a->perspective && (a->rows == rows)) {
    for (int i = 0; i < 6; i++)
      xf.proj[i / 4][i % 4] = proj[i] + (xf.proj[i / 4][i % 4] - proj[i]) * s;
    Dirty(206, 208);
  }
  // the material and ambient colours: between the two (particles fade in and out by their
  // material colour; Retroid report 26.10.2026: the flames changed only with the game's 30
  // pictures a second). FillXfRegs puts them back after the draw.
  bool colors = false;
  const float *mat = src + a->rows * 4 + a->rows * 3 + 6;
  if (std::memcmp(mat, &xf.matAmb[0][0], sizeof(xf.matAmb)) != 0) {
    for (int i = 0; i < 16; i++)
      xf.matAmb[i / 4][i % 4] = mat[i] + (xf.matAmb[i / 4][i % 4] - mat[i]) * s;
    Dirty(200, 204);
    colors = true;
  }
  // with the object sure (an owned draw, S3): its texture matrices (flowing water, lava, drifting
  // fog) unless a coordinate jumped by more than half the texture (it ran over its edge and
  // starts again), the TEV colours (a glow) and the light colours (torchlight) between the two
  bool extras = false;
  if (owned && a->extra != ~0u) {
    const float *ex = recFloats.data() + a->extra;
    texRowsBlended.clear();
    for (int n = 0; n < 8; n++) {
      const std::uint32_t row = TexRow(d, n);
      if (row >= 60 || row + 3 > 64)
        continue;
      const float *was = ex + n * 12;
      bool same = true, jump = false;
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
          const float diff = xf.posTex[row + r][c] - was[r * 4 + c];
          same = same && diff == 0.0f;
          if (c == 3 && r < 2 && std::fabs(diff) > 0.5f)
            jump = true;
        }
      }
      if (same || std::find(texRowsBlended.begin(), texRowsBlended.end(), row) != texRowsBlended.end())
        continue;
      if (jump) {
        stats.interpTexWraps++;
        continue;
      }
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
          xf.posTex[row + r][c] = was[r * 4 + c] + (xf.posTex[row + r][c] - was[r * 4 + c]) * s;
      texRowsBlended.push_back(row);
      Dirty(static_cast<int>(row), static_cast<int>(row + 3));
      stats.interpTexBlends++;
      extras = true;
    }
    std::int32_t tev[2][4][4];
    std::memcpy(tev, ex + 96, sizeof(tev));
    if (std::memcmp(tev[0], tevColor, sizeof(tevColor)) != 0 || std::memcmp(tev[1], tevKonst, sizeof(tevKonst)) != 0) {
      std::memcpy(tevSave[0], tevColor, sizeof(tevColor));
      std::memcpy(tevSave[1], tevKonst, sizeof(tevKonst));
      for (int i = 0; i < 4; i++) {
        for (int c = 0; c < 4; c++) {
          tevColor[i][c] = static_cast<std::int32_t>(std::lround(tev[0][i][c] + (tevColor[i][c] - tev[0][i][c]) * s));
          tevKonst[i][c] = static_cast<std::int32_t>(std::lround(tev[1][i][c] + (tevKonst[i][c] - tev[1][i][c]) * s));
        }
      }
      pxDirty = true;
      stats.interpTevBlends++;
      extras = true;
      texRowsBlended.push_back(0x100);
    }
    // post-transform matrices, as the texture matrices
    for (int n = 0; k.dualTex && n < 8; n++) {
      const std::uint32_t row = k.postGen[n] & 63;
      if (row + 3 > 64 ||
          std::find(texRowsBlended.begin(), texRowsBlended.end(), 0x200 + row) != texRowsBlended.end())
        continue;
      const float *was = ex + 160 + n * 12;
      bool same = true, jump = false;
      for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
          const float diff = xf.post[row + r][c] - was[r * 4 + c];
          same = same && diff == 0.0f;
          if (c == 3 && r < 2 && std::fabs(diff) > 0.5f)
            jump = true;
        }
      }
      if (same)
        continue;
      if (jump) {
        stats.interpTexWraps++;
        continue;
      }
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
          xf.post[row + r][c] = was[r * 4 + c] + (xf.post[row + r][c] - was[r * 4 + c]) * s;
      texRowsBlended.push_back(0x200 + row);
      Dirty(static_cast<int>(96 + row), static_cast<int>(96 + row + 3));
      stats.interpTexBlends++;
      extras = true;
    }
    // the indirect matrices, each element between the two when the scale stayed (it is shared by
    // the matrix's three words; a new scale is a new matrix: as in the next picture)
    {
      std::uint32_t was[9];
      std::memcpy(was, ex + 256, sizeof(was));
      bool changed = false;
      for (int i = 0; i < 9; i++)
        changed = changed || (was[i] & 0xFFFFFF) != (bp[0x06 + i] & 0xFFFFFF);
      if (changed) {
        std::memcpy(indSave, &bp[0x06], sizeof(indSave));
        for (int m = 0; m < 3; m++) {
          bool sameScale = true;
          for (int w = 0; w < 3; w++)
            sameScale = sameScale && ((was[3 * m + w] >> 22) & 3) == ((bp[0x06 + 3 * m + w] >> 22) & 3);
          if (!sameScale)
            continue;
          for (int w = 0; w < 3; w++) {
            const std::uint32_t a = was[3 * m + w], b = bp[0x06 + 3 * m + w];
            std::uint32_t v = b & ~0x3FFFFFu;
            for (int half = 0; half < 2; half++) {
              const int ea = Signed11(a >> (11 * half)), eb = Signed11(b >> (11 * half));
              const int e = static_cast<int>(std::lround(ea + (eb - ea) * s));
              v |= (static_cast<std::uint32_t>(e) & 0x7FFu) << (11 * half);
            }
            bp[0x06 + 3 * m + w] = v;
          }
        }
        pxDirty = true;
        texRowsBlended.push_back(0x300);
        stats.interpIndBlends++;
        extras = true;
      }
    }
    const float *lc = ex + 128;
    bool lightsDiffer = false;
    for (int l = 0; l < 8 && !lightsDiffer; l++)
      lightsDiffer = std::memcmp(lc + l * 4, xf.light[l][0], 4 * sizeof(float)) != 0;
    if (lightsDiffer) {
      for (int l = 0; l < 8; l++)
        for (int c = 0; c < 4; c++)
          xf.light[l][0][c] = lc[l * 4 + c] + (xf.light[l][0][c] - lc[l * 4 + c]) * s;
      Dirty(160, 200);
      lights = true;
      stats.interpLightBlends++;
    }
  }
  // vertices the game moved itself (skinning): positions and normals between the two
  if (interpPrev && a->vertexCount == d.vertexCount && a->stride == d.stride && a->layout == d.layout &&
      d.stride >= 12 && a->vertexOffset + static_cast<std::uint64_t>(a->vertexCount) * a->stride <=
                            interpPrev->vertexBytes) {
    const std::uint8_t *pa = interpPrev->vertices + a->vertexOffset, *pb = chunk.vertices + d.vertexOffset;
    const std::size_t bytes = static_cast<std::size_t>(d.vertexCount) * d.stride;
    if (std::memcmp(pa, pb, bytes) != 0) {
      blendVertices.assign(pb, pb + bytes);
      const bool normals = (d.layout & RE4_GX_V_NORMAL) != 0 && d.stride >= 24;
      float far = 0.0f;
      for (std::uint32_t v = 0; v < d.vertexCount; v++) {
        float fa[6], fb[6];
        std::memcpy(fa, pa + v * d.stride, normals ? 24 : 12);
        std::memcpy(fb, pb + v * d.stride, normals ? 24 : 12);
        for (int c = 0; c < 3; c++)
          far = std::max(far, std::fabs(fb[c] - fa[c]));
        for (int c = 0; c < (normals ? 6 : 3); c++)
          fb[c] = fa[c] + (fb[c] - fa[c]) * s;
        std::memcpy(blendVertices.data() + v * d.stride, fb, normals ? 24 : 12);
      }
      // more than 1500 units in one game picture (2D: kFlatMove): not the same figure. A one-frame line
      // matched by its id is the same at any distance (the laser's far end jumps from a near wall to a far one).
      // The blended vertices go to a place of blendVbo no draw has used in this pass: no waiting, no copy
      // of the buffer
      if (far <= (flat ? kFlatMove : 1500.0f) || (oneFrame && (d.primitive & 0xFF) == RE4_GX_LINES)) {
        if (PutBlendedVertices(blendVertices.data(), bytes, d.stride))
          stats.interpVertexBlends++;
      }
    }
  }
  // 0x100000: only the projection; 0x200000: the lights too; 0x400000: the colours; 0x800000:
  // texture matrices or TEV colours (texRowsBlended)
  return (rows ? first << 8 | rows : 0x100000) | (lights ? 0x200000u : 0u) | (colors ? 0x400000u : 0u) |
         (extras ? 0x800000u : 0u);
}

void GxCore::BpWrite(std::uint32_t word, bool action) {
  const unsigned reg = word >> 24;
  const std::uint32_t value = word & 0xFFFFFF;
  if (bp[reg] != value || !action) {
    // registers the generated shader depends on (MakeKey)
    if (reg == 0x00 || (reg >= 0x10 && reg <= 0x1F) || (reg >= 0x27 && reg <= 0x2F) ||
        (reg >= 0xC0 && reg <= 0xDF) || reg == 0xF1 || reg == 0xF3 || (reg >= 0xF6 && reg <= 0xFD))
      keyDirty = true;
    // registers of the PX block (FillPixel)
    if ((reg >= 0x06 && reg <= 0x0E) || reg == 0x25 || reg == 0x26 || (reg >= 0x30 && reg <= 0x3F) ||
        (reg >= 0x80 && reg <= 0x87) || (reg >= 0xA0 && reg <= 0xA7) || (reg >= 0xE0 && reg <= 0xE7) ||
        (reg >= 0xEE && reg <= 0xF3))
      pxDirty = true;
  }
  bp[reg] = value;
  if (reg >= 0xE0 && reg <= 0xE7) {
    const unsigned i = (reg - 0xE0) / 2;
    std::int32_t *dst = (value >> 23) & 1 ? tevKonst[i] : tevColor[i];
    if ((reg & 1) == 0) { // RA
      dst[0] = Signed11(value);
      dst[3] = Signed11(value >> 12);
    } else { // BG
      dst[2] = Signed11(value);
      dst[1] = Signed11(value >> 12);
    }
  }
  if (action && reg == 0x52) {
    if (!skipDraws) {
      EfbCopy(value);
    } else if ((value >> 14) & 1) {
      // a skipped picture: nothing to show, only the clear of the EFB that the next picture needs
      stats.skippedChunks++;
      ClearAfterCopyPlain(value, static_cast<int>(bp[0x49] & 0x3FF), static_cast<int>((bp[0x49] >> 10) & 0x3FF),
                     static_cast<int>(bp[0x4A] & 0x3FF) + 1, static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1);
      sceneBottom = 0;
    } // a copy for a texture: CanSkip said the next chunk copies there first, nobody reads this one
  }
  if (action && reg == 0x5A && !skipDraws) {
    const bool focusTime = std::getenv("RE4_GX_COPY_TIME") && *std::getenv("RE4_GX_COPY_TIME") == '1';
    std::chrono::steady_clock::time_point focusStart;
    if (focusTime) {
      GpuFinish();
      focusStart = std::chrono::steady_clock::now();
    }
    Autofocus(value);
    stats.copyCount[GxStats::kCopyFocus]++;
    if (focusTime) {
      GpuFinish();
      stats.copyNs[GxStats::kCopyFocus] += static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - focusStart).count());
    }
  }
  if (action && reg == 0x5B && !interpPass && !skipDraws) {
    peekWanted = true;  // the game's GXPeekZ is used in this picture: its depth goes to the core
    peekPicture = value & 0xFFFFFF;  // with the picture's number (the core answers by the map's age)
  }
}

void GxCore::BindTexture(const Re4GxTexture &t, const Re4GxChunk &chunk) {
  const unsigned map = t.map & 7;
  pxDirty = true; // the map's size is in the PX block
  Bound &b = bound[map];
  b.id = whiteTex;
  b.width = t.width ? t.width : 1;
  b.height = t.height ? t.height : 1;
  b.levels = 1;
  if (t.flags & RE4_GX_TEX_COPY) {
    boundKey[map] = 1ull << 63 | t.address;
    // the in-between pass reads its own copies, and the real ones of the picture before
    const bool own = interpPass && interpCopied.count(t.address);
    auto &from = own ? interpCopies : copies;
    auto it = from.find(t.address);
    if (it == from.end()) {
      stats.missingTextures++;
      return;
    }
    if (!it->second.read) {
      it->second.read = true;
      stats.copyRead++;
    }
    b.id = GxIsPaletted(t.format) ? PalettedCopy(it->second, t) : it->second.id;
    // a texture of the copy's width but another height reads the copy's
    // rows one to one, as the texels lie in memory (the heat haze over the
    // fires of r101 copies rows 56-447 and reads 336 of them as 256x168):
    // texel coordinates go by the copy's size, not by the texture's; scaled
    // to it, the rows below the picture (black) showed as a dark band
    if (it->second.width == b.width && it->second.height) {
      b.height = it->second.height;
    }
    return;
  }
  const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
  boundKey[map] = key;
  if (key == 0)
    return;
  // a texture the in-between pass uploaded already (the same key: the same texels)
  if (t.dataBytes && !(skipInterpUploads && interpUploads.count(key)))
    Upload(t, chunk);
  if (interpPass && t.dataBytes)
    interpUploads.insert(key);
  auto it = textures.find(key);
  if (it == textures.end()) {
    stats.missingTextures++;
    return;
  }
  b.id = it->second.id;
  b.levels = it->second.levels;
}

void GxCore::Forget(std::uint64_t key) {
  Touched(); // a deleted texture is unbound
  auto it = textures.find(key);
  if (it != textures.end()) {
    DeleteTexture(it->second.id);
    textures.erase(it);
  }
}

void GxCore::DropTextures() {
  Touched();
  for (auto &t : textures)
    DeleteTexture(t.second.id);
  textures.clear();
  for (auto &t : paletted)
    DeleteTexture(t.second.id);
  paletted.clear();
}

void GxCore::EfbCopy(std::uint32_t control) {
  const bool copyTime = std::getenv("RE4_GX_COPY_TIME") && *std::getenv("RE4_GX_COPY_TIME") == '1';
  std::chrono::steady_clock::time_point copyStart;
  if (copyTime) {
    GpuFinish();
    copyStart = std::chrono::steady_clock::now();
  }
  Touched(); // its own pass
  const int x = static_cast<int>(bp[0x49] & 0x3FF), y = static_cast<int>((bp[0x49] >> 10) & 0x3FF);
  const int w = static_cast<int>(bp[0x4A] & 0x3FF) + 1, h = static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1;
  const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
  const bool toXfb = (control >> 14) & 1;
  const bool half = !toXfb && ((control >> 9) & 1);
  const bool depth = !toXfb && (bp[0x43] & 7) == 3;
  int mode = 0;
  unsigned kind = 0;
  if (!toXfb)
    CopyMode(control, depth, &mode, &kind);
  const int ow = half ? std::max(1, w / 2) : w, oh = half ? std::max(1, h / 2) : h;
  if (toXfb && holdXfb) {
    // the VI stays on the picture before (RE4_GX_CHUNK_HOLD, SYS_SCREEN_STOP):
    // nothing to show, only the clear of the EFB
    stats.heldCopies++;
    PlainEfb();
    if (peekWanted)
      SnapshotDepth();
    ClearAfterCopy(control, x, y, w, h);
    sceneBottom = 0;
    return;
  }
  CopyParams p;
  if (toXfb) {
    p.target = DisplayTarget(w, h);
    haveXfb = true;
    xfbWide = chunkWide;
    stats.displayCopies++;
  } else {
    // the in-between pass copies into its own textures: the next real Run still reads what the
    // picture before copied (effects that blend the last picture in)
    if (interpPass)
      interpCopied.insert(dest);
    Copy &c = interpPass ? interpCopies[dest] : copies[dest];
    if (c.id && !c.read && !interpPass)
      stats.copyOverwrittenUnread++;
    c.read = false;
    CopyTarget(c, ow, oh);
    c.serial = ++copySerial;
    c.kind = kind;
    p.target = c.id;
    stats.textureCopies++;
    // copies read through a palette are made anew from this copy (their key holds the copy's
    // serial: those of the in-between pass's own copies stay apart)
    for (auto it = paletted.begin(); !interpPass && it != paletted.end();) {
      DeleteTexture(it->second.id);
      it = paletted.erase(it);
    }
  }
  // render the EFB region into the target
  p.toXfb = toXfb;
  p.depth = depth;
  p.half = half;
  p.mode = toXfb ? 0 : mode;
  p.x = x;
  p.y = y;
  p.w = w;
  p.h = h;
  p.tw = toXfb ? w : ow;
  p.th = toXfb ? h : oh;
  p.gain = depth ? 1.0f : CopyFilterGain();
  // the TV filter: a display copy blends the lines above and below with the copy filter's taps (they
  // hold the gain as well), one original line (the scale) apart
  if (toXfb && !depth && tvFilter) {
    float above = 0.0f, at = 1.0f, below = 0.0f;
    CopyFilterTaps(&above, &at, &below);
    p.taps[0] = above;
    p.taps[1] = at;
    p.taps[2] = below;
    p.step = Sf(1);
    if (!tvLogged) {
      tvLogged = true;
      GXLOG("TV filter: the line above %.3f, the line %.3f, the line below %.3f", above, at, below);
    }
  }
  // The game's depth of field (filter01.cpp, Filter01CopyEFB) copies the picture at half size with a
  // copy filter of only the lines above and below ({32, 0, 0, 0, 0, 0, 32}) and draws that copy back
  // over the scene, shifted by 0.6 to 6.6 pixels at 50 %. On the GameCube the copy is soft (2x2 of its
  // pixels, the lines around). Until 0.99.7 the app left the filter out of texture copies and took the
  // 2x2 box from its own texels, a fraction of a pixel at a higher scale: the copy stayed sharp and
  // showed as a second, shifted picture over the background ("ghosting", Xiaomi report of 0.99.7 at 4x,
  // 10.11.2026). A copy whose filter leaves the line itself out takes the filter (a GameCube line
  // apart) and the GameCube's box at the scale. Other texture copies keep their line (no filter, or the
  // render mode's deflicker filter) and stay as they were.
  if (!toXfb && !depth) {
    float above = 0.0f, at = 1.0f, below = 0.0f;
    CopyFilterTaps(&above, &at, &below);
    if (at == 0.0f && above + below > 0.0f) {
      p.taps[0] = above;
      p.taps[1] = at;
      p.taps[2] = below;
      p.step = Sf(1);
      p.box = half ? Sf(1) : 0.0f;
      static bool logged = false;
      if (!logged) {
        logged = true;
        GXLOG("blur copy (the game's depth of field): the line above %.3f, below %.3f, a box of %.1f texels",
              above, below, p.box);
      }
    }
  }
  // The game reads some copies (water, heat haze: EFB rows 56..447 at half size) through a texture
  // object that is shorter than the copy, so on the GameCube a lookup below the drawn picture
  // clamps to the last drawn row. The copy here is read whole: rows the letterbox never drew
  // (black) would show as dark waves at the bottom edge. Repeat the last drawn row below it.
  // In the 16:9 formats the app shows rows 56..391 only: there a copy of the whole picture (the
  // shimmer of the logos at the start, IdShimmerTrans: rows 0..447, a movie drawn into 56..391
  // under a full scissor; the depth of field at the edge) repeats the edge rows of what is shown
  // instead of reading the unseen bars (black waves at the bottom of the intro, 0.91.0 test).
  int top = 0, bottom = kEfbHeight;
  if (!toXfb && !depth && x == 0 && w >= 500) {
    if (half && sceneBottom > y + h / 2 && sceneBottom < y + h)
      bottom = sceneBottom;
    const int shownT = shownTop.load(std::memory_order_relaxed), shownB = shownBottom.load(std::memory_order_relaxed);
    if (shownB > y + h / 2 && shownB < y + h)
      bottom = std::min(bottom, shownB);
    if (shownT > y && shownT < y + h / 2)
      top = shownT;
  }
  p.limited = top > 0 || bottom < kEfbHeight;
  p.limit[0] = p.limited ? Sf(top) : -1e9f;
  p.limit[1] = p.limited && bottom < kEfbHeight ? Sf(bottom) : 1e9f;
  if (p.limited)
    stats.limitedCopies++;
  CopyPass(p);
  if (toXfb)
    DisplayEffects(Hi(w), Hi(h), y, h, w);
  if (toXfb && peekWanted)
    SnapshotDepth();  // the picture is complete, the clear comes next
  ClearAfterCopy(control, x, y, w, h);
  if (toXfb)
    sceneBottom = 0;
  {
    const int k = toXfb ? GxStats::kCopyDisplay
                  : depth ? GxStats::kCopyDepth
                  : half ? GxStats::kCopyHalf : x == 0 && w >= 500 ? GxStats::kCopyFull : GxStats::kCopySmall;
    stats.copyCount[k]++;
    stats.copyTexels[k] += static_cast<unsigned long long>(Hi(ow)) * Hi(oh);
    if (copyTime) {
      GpuFinish();
      stats.copyNs[k] += static_cast<unsigned long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - copyStart).count());
    }
  }
}

bool GxCore::Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t) {
  GxCore &m = *this;
  // (STILL: a menu screen of the game, its scrolling text lists; also when prev was one: the cut out of it)
  const std::uint32_t refuse =
      RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_HOLD | RE4_GX_CHUNK_BLACK | RE4_GX_CHUNK_MARK | RE4_GX_CHUNK_STILL;
  if (prev.flags & RE4_GX_CHUNK_STILL)
    return false;
  // "Native": from a wide picture to one in the 16:9 box or back (a menu screen, the scope, a movie) the app's box
  // changes; nothing lies between them
  if ((prev.flags ^ next.flags) & RE4_GX_CHUNK_WIDE)
    return false;
  if (!m.recordDraws || !m.recValid || m.recSequence != prev.sequence || next.sequence != prev.sequence + 1 ||
      (next.flags & refuse) || !(next.flags & RE4_GX_CHUNK_XFB) || next.version != RE4_GX_FRAME_VERSION ||
      m.viBlack || m.rec.empty())
    return false;
  // nothing in perspective noted (title, movies, 2D menus): nothing to place between
  bool any3d = false;
  for (const DrawRec &r : m.rec)
    any3d = any3d || r.perspective;
  if (!any3d)
    return false;
  // what the pass changes and Run(next) expects as after prev
  std::vector<std::uint32_t> bp(m.bp, m.bp + 256), xfMem(m.xfMem, m.xfMem + 0x800), xfReg(m.xfReg, m.xfReg + 0x58);
  std::int32_t tevColor[4][4], tevKonst[4][4];
  std::memcpy(tevColor, m.tevColor, sizeof(tevColor));
  std::memcpy(tevKonst, m.tevKonst, sizeof(tevKonst));
  Bound bound[8];
  std::uint64_t boundKey[8];
  for (int i = 0; i < 8; i++) {
    bound[i] = m.bound[i];
    boundKey[i] = m.boundKey[i];
  }
  const float perspA = m.perspA, perspB = m.perspB;
  const bool havePerspective = m.havePerspective, holdXfb = m.holdXfb, viBlack = m.viBlack;
  const int focusCur = m.focusCur;
  const unsigned focusChunk = m.focusChunk;
  // palette loads of next (TMEM): only then the palettes
  bool tlutLoads = false;
  for (std::uint32_t at = 0; at < next.opWords;) {
    const std::uint32_t head = next.ops[at];
    tlutLoads = tlutLoads || (head & 0xFF) == RE4_GX_OP_TLUT;
    at += 1 + (head >> 8);
  }
  std::vector<std::uint8_t> tlut;
  if (tlutLoads)
    tlut = m.tlut;
  // the noted draws by signature, in their order
  m.recBySig.clear();
  m.recByLook.clear();
  m.recByOwner.clear();
  m.nextOwnerDraws.clear();
  {
    std::uint64_t owner = 0;
    for (std::uint32_t at = 0; at < next.opWords;) {
      const std::uint32_t head = next.ops[at], op = head & 0xFF, n = head >> 8;
      if (at + 1 + n > next.opWords)
        break;
      if (op == RE4_GX_OP_OWNER && n >= 4)
        owner = OwnerKey(next.ops + at + 1);
      else if (op == RE4_GX_OP_DRAW && owner)
        m.nextOwnerDraws[owner]++;
      at += 1 + n;
    }
  }
  for (std::uint32_t i = 0; i < m.rec.size(); i++) {
    if (m.rec[i].owner)
      m.recByOwner.emplace(m.rec[i].owner, i);
    m.recBySig[m.rec[i].sig].push_back(i);
    if (m.rec[i].look)
      m.recByLook[m.rec[i].look].push_back(i);
  }
  m.recUsed.assign(m.rec.size(), 0);
  m.recReserved.assign(m.rec.size(), 0);
  for (std::uint32_t i = 0; i < m.rec.size(); i++) {
    if (!m.rec[i].base)
      continue;
    auto pc = m.recOwnerDraws.find(m.rec[i].base), nc = m.nextOwnerDraws.find(m.rec[i].base);
    m.recReserved[i] = pc != m.recOwnerDraws.end() && nc != m.nextOwnerDraws.end() && pc->second == nc->second;
  }
  m.ipDraws = m.ipMatched = m.ipTurned = 0;
  m.camValid = false;
  m.camGuesses = m.camVotes = 0;
  m.interpCopied.clear();
  m.interpUploads.clear();
  // the store for the pass's blended vertices (PutBlendedVertices)
  m.InterpBegin(next);
  m.interpPass = true;
  m.interpT = t;
  m.interpPrev = &prev;
  Run(next);
  m.InterpEnd();
  m.interpPass = false;
  m.interpPrev = nullptr;
  m.skipInterpUploads = true;
  // back to the state after prev
  std::memcpy(m.bp, bp.data(), sizeof(m.bp));
  std::memcpy(m.xfMem, xfMem.data(), sizeof(m.xfMem));
  std::memcpy(m.xfReg, xfReg.data(), sizeof(m.xfReg));
  std::memcpy(m.tevColor, tevColor, sizeof(tevColor));
  std::memcpy(m.tevKonst, tevKonst, sizeof(tevKonst));
  for (int i = 0; i < 8; i++) {
    m.bound[i] = bound[i];
    m.boundKey[i] = boundKey[i];
  }
  if (tlutLoads)
    m.tlut.swap(tlut);
  m.perspA = perspA;
  m.perspB = perspB;
  m.havePerspective = havePerspective;
  m.holdXfb = holdXfb;
  m.viBlack = viBlack;
  m.focusCur = focusCur;
  m.focusChunk = focusChunk;
  for (std::uint32_t a = 0; a < 0x800; a++)
    m.XfMemToBlock(a);
  m.FillXfRegs();
  m.Dirty(0, static_cast<int>(sizeof(XfBlock) / 16));
  m.keyDirty = true;
  m.keyLayout = ~0u;
  m.pxDirty = true;
  m.pxValid = false;
  m.Touched();
  // a cut: few draws found again, or most of them turned (the camera jumped)
  m.stats.interpDraws += m.ipDraws;
  m.stats.interpMatched += m.ipMatched;
  const bool cut = m.ipDraws == 0 || m.ipMatched * 2 < m.ipDraws || m.ipTurned * 10 > m.ipMatched * 3;
  if (cut) {
    m.stats.interpCuts++;
    return false;
  }
  m.stats.interpPictures++;
  return true;
}

std::uint32_t GxCore::DrawMerged(const Re4GxChunk &chunk, std::uint32_t at) {
  std::size_t i = mergeNext;
  const MergeMark &start = mergeMarks[i];
  Re4GxDraw first;
  std::memcpy(&first, chunk.ops + at + 1, sizeof(first));
  const auto end = [&](std::uint32_t op) { return op + 1 + (chunk.ops[op] >> 8); };
  // the program of the run (its draws have the shader of the first: nothing between them changes it)
  if (keyDirty || first.layout != keyLayout) {
    key = MakeKey(first);
    if (KeyProgram())
      NoteKey(key);
    keyLayout = first.layout;
    keyDirty = false;
  }
  unsigned draws = 0;
  std::uint32_t count = 0, next = end(at);
  if (!KeyProgramReady() || !MergedProgramReady())
    stats.mergeRunBreak[GxStats::kRunNoProgram]++;
  else {
    for (;;) {
      const MergeMark &m = mergeMarks[i];
      Re4GxDraw d;
      std::memcpy(&d, chunk.ops + m.op + 1, sizeof(d));
      if (xfRegsDirty)
        FillXfRegs();
      std::memcpy(mergeBlock.xf[draws], xf.posTex, sizeof(xf.posTex) + sizeof(xf.normal));
      std::memcpy(mergeBlock.xf[draws][96], xf.matAmb, sizeof(xf.matAmb));
      std::memcpy(mergeBlock.slot[draws], d.matrices, sizeof(d.matrices));
      mergeBlock.slot[draws][3] = m.first;
      draws++;
      count += d.indexCount;
      next = end(m.op);
      int why = GxStats::kRunOtherOp;
      if (draws == kMergeSlots || i + 1 >= mergeMarks.size() || mergeMarks[i + 1].chain != start.chain ||
          !MergeQuiet(chunk, next, mergeMarks[i + 1].op, &why)) {
        stats.mergeRunBreak[draws == kMergeSlots ? GxStats::kRunSlots
                            : i + 1 >= mergeMarks.size() || mergeMarks[i + 1].chain != start.chain
                                ? GxStats::kRunChainEnd
                                : why]++;
        break;
      }
      // the ops between (the owner tags only count for the recording, which is off while merging)
      for (std::uint32_t op = next; op < mergeMarks[i + 1].op;)
        op = Op(chunk, op);
      i++;
    }
  }
  mergeNext = i + 1;
  if (draws < 2) {
    Draw(first, chunk);
    return end(at);
  }
  for (int s = static_cast<int>(draws); s < kMergeSlots; s++)
    mergeBlock.slot[s][3] = ~0u;
  MergeCall call;
  call.draws = draws;
  call.index = start.index;
  call.count = count;
  call.base = start.base;
  Draw(first, chunk, &call);
  return next;
}

void GxCore::Upload(const Re4GxTexture &t, const Re4GxChunk &chunk) {
  const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
  if (t.dataBytes == 0 || t.dataOffset + t.dataBytes > chunk.blobBytes)
    return;
  Texture tex;
  tex.width = t.width;
  tex.height = t.height;
  tex.levels = t.levels ? t.levels : 1;
  tex.id = BeginTexture(t.width, t.height, tex.levels);
  const std::uint8_t *data = chunk.blob + t.dataOffset;
  std::size_t left = t.dataBytes;
  const std::uint8_t *palette = nullptr;
  unsigned entries = 0;
  if (GxIsPaletted(t.format)) {
    const unsigned offset = (t.tlut & 0x3FF) << 9;
    palette = tlut.data() + offset;
    entries = std::min<unsigned>(GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
  }
  unsigned w = t.width, h = t.height, levels = 0;
  std::vector<std::uint8_t> &rgba = decoded;
  for (unsigned l = 0; l < tex.levels; l++) {
    const std::size_t bytes = GxTextureLevelBytes(t.format, w, h);
    if (bytes > left)
      break;
    GxDecodeTexture(data, bytes, t.format, w, h, palette, entries, (t.tlut >> 10) & 3, rgba);
    TextureLevel(tex.id, l, w, h, rgba.data());
    levels++;
    data += bytes;
    left -= bytes;
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  tex.levels = std::max(1u, levels);
  EndTexture(tex.id, tex.levels);
  auto old = textures.find(key);
  if (old != textures.end())
    DeleteTexture(old->second.id);
  textures[key] = tex;
  stats.textures++;
}

GxSamplerDesc GxCore::SamplerFor(std::uint32_t mode0, std::uint32_t mode1, unsigned levels, bool point) const {
  GxSamplerDesc s;
  static const std::uint8_t wrap[4] = {0, 1, 2, 1};  // clamp, repeat, mirror, (repeat)
  s.wrapS = wrap[mode0 & 3];
  s.wrapT = wrap[(mode0 >> 2) & 3];
  s.magLinear = (mode0 >> 4) & 1;
  const unsigned minFilter = (mode0 >> 5) & 7;
  // GX's minification filters: near, near mip near, near mip lin, -, lin, lin mip near, lin mip lin, -
  static const std::uint8_t minLinear[8] = {0, 0, 0, 0, 1, 1, 1, 1};
  static const std::uint8_t mip[8] = {0, 1, 2, 0, 0, 1, 2, 0};
  s.minLinear = minLinear[minFilter];
  s.mip = mip[minFilter];
  if (levels <= 1) {
    s.minLinear = minFilter >= 4;
    s.mip = 0;
  }
  if (point) {
    s.magLinear = false;
    s.minLinear = false;
    s.mip = levels > 1 ? 1 : 0;
  }
  s.minLod = static_cast<float>(mode1 & 0xFF) / 16.0f;
  s.maxLod = static_cast<float>((mode1 >> 8) & 0xFF) / 16.0f;
  // anisotropic filtering (the menu's option): only where the game filters
  // linearly through mipmaps; point-sampled effects keep their look
  s.anisotropy = anisotropy > 1.0f && levels > 1 && ((mode0 >> 4) & 1) && minFilter >= 5 && minFilter <= 6 && !point
                     ? anisotropy
                     : 1.0f;
  return s;
}

void GxCore::ClearAfterCopy(std::uint32_t control, int x, int y, int w, int h) {
  if (!((control >> 11) & 1))
    return;
  const std::uint32_t c = bp[0x41];
  GxClearRect r;
  r.color = (c >> 3) & 1;
  r.alpha = (c >> 4) & 1;
  r.depth = (bp[0x40] >> 4) & 1;
  r.rect[0] = Lo(x);
  r.rect[1] = Lo(y);
  r.rect[2] = Lo(x + w) - Lo(x);
  r.rect[3] = Lo(y + h) - Lo(y);
  r.rgba[0] = static_cast<float>(bp[0x4F] & 255) / 255.0f;
  r.rgba[3] = static_cast<float>((bp[0x4F] >> 8) & 255) / 255.0f;
  r.rgba[2] = static_cast<float>(bp[0x50] & 255) / 255.0f;
  r.rgba[1] = static_cast<float>((bp[0x50] >> 8) & 255) / 255.0f;
  r.z = static_cast<float>(bp[0x51] & 0xFFFFFF) / 16777215.0f;
  if (!(r.color || r.alpha || r.depth))
    return;
  // "Save bandwidth (Vulkan)": the clear goes with the EFB's next pass (its load), not a pass of its own
  if (DeferClear(r))
    return;
  UseEfb();  // MSAA on the tiles: the EFB back first, the clear is only the copy's region
  ClearRect(r);
}

unsigned GxCore::PalettedCopy(const Copy &copy, const Re4GxTexture &t) {
  Touched(); // its own pass
  const unsigned offset = (t.tlut & 0x3FF) << 9;
  const unsigned entries = std::min<unsigned>(GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
  const std::uint64_t key =
      Fnv(tlut.data() + offset, entries * 2, (static_cast<std::uint64_t>(copy.serial) << 32) ^ t.address ^ t.tlut);
  auto it = paletted.find(key);
  if (it != paletted.end())
    return it->second.id;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(entries) * 4);
  for (unsigned i = 0; i < entries; i++) {
    std::vector<std::uint8_t> one;
    std::uint8_t tile[32] = {};
    tile[0] = 0;
    GxDecodeTexture(tile, 32, kTexC8, 1, 1, tlut.data() + offset + i * 2, 1, (t.tlut >> 10) & 3, one);
    std::memcpy(rgba.data() + i * 4, one.data(), 4);
  }
  const unsigned pw = std::min(entries, 256u), ph = std::max(1u, entries / 256);
  Texture out;
  out.width = copy.width;
  out.height = copy.height;
  out.id = DrawPaletted(copy, t, rgba.data(), pw, ph);
  paletted[key] = out;
  return out.id;
}

// ---- end of the part moved from GlRenderer ----

} // namespace re4gx
