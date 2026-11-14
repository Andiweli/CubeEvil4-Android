// The minimap's view (wish of the project owner, 04.11.2026): the current room's outline (the core's
// re4_core_room_outline, world units) around the player, turned so that the way he faces points up, as segments
// in the map's own square (-1..1, the circle of radius 1 is the map). Shared by the app (core_mode.cpp draws it)
// and the PC check (tools/minimap_check.cpp), so the check sees the same picture.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace re4mm {

// The player: position and the way he faces (the player model's matrix: its z axis, the game's forward).
struct Pose {
  float x = 0, y = 0, z = 0;
  float fx = 0, fz = 1;
};

// The player's pose from a model matrix (row-major 3x4, Re4CoreModel::matrix); false without a direction.
inline bool PoseFromMatrix(const float *m, Pose *p) {
  p->x = m[3];
  p->y = m[7];
  p->z = m[11];
  const float fx = m[2], fz = m[10], len = std::sqrt(fx * fx + fz * fz);
  if (!(len > 1e-4f))
    return false;
  p->fx = fx / len;
  p->fz = fz / len;
  return true;
}

// Edges (x0 z0 x1 z1 height, 5 floats each) to segments x0 y0 x1 y1 alpha in the map's square: `radius` world units
// to the map's edge, up = the way the player faces, right = his right (forward x up in the game's right-handed
// space, y up). Clipped to the circle; floors far above or below him fade (another storey: 25 % at 3000 units).
inline void Project(const float *edges, int n, const Pose &p, float radius, std::vector<float> &out) {
  out.clear();
  const float rx = -p.fz, rz = p.fx;  // the player's right
  const float inv = 1.0f / radius;
  for (int i = 0; i < n; i++) {
    const float *e = edges + i * 5;
    float ax = ((e[0] - p.x) * rx + (e[1] - p.z) * rz) * inv, ay = ((e[0] - p.x) * p.fx + (e[1] - p.z) * p.fz) * inv;
    float bx = ((e[2] - p.x) * rx + (e[3] - p.z) * rz) * inv, by = ((e[2] - p.x) * p.fx + (e[3] - p.z) * p.fz) * inv;
    // the part inside the unit circle: |a + t (b - a)| <= 1
    const float dx = bx - ax, dy = by - ay;
    const float A = dx * dx + dy * dy, B = 2.0f * (ax * dx + ay * dy), C = ax * ax + ay * ay - 1.0f;
    float t0 = 0.0f, t1 = 1.0f;
    if (A < 1e-12f) {
      if (C > 0.0f)
        continue;
    } else {
      const float disc = B * B - 4.0f * A * C;
      if (disc <= 0.0f)
        continue;
      const float s = std::sqrt(disc);
      t0 = std::fmax(0.0f, (-B - s) / (2.0f * A));
      t1 = std::fmin(1.0f, (-B + s) / (2.0f * A));
      if (t1 <= t0)
        continue;
    }
    const float dh = std::fabs(e[4] - p.y);
    const float alpha = dh <= 600.0f ? 1.0f : std::fmax(0.25f, 1.0f - (dh - 600.0f) / 3200.0f);
    out.push_back(ax + dx * t0);
    out.push_back(ay + dy * t0);
    out.push_back(ax + dx * t1);
    out.push_back(ay + dy * t1);
    out.push_back(alpha);
  }
}

// How far the map reaches (world units to its edge) for a room: 30 % of its longer side, at least 10 m (a
// corridor, a hut), at most 24 m (the village, the halls of the castle: 12 m showed one wall there).
inline float RoomRadius(const float *edges, int n) {
  if (n <= 0)
    return 10000.0f;
  float x0 = edges[0], x1 = edges[0], z0 = edges[1], z1 = edges[1];
  for (int i = 0; i < n; i++)
    for (int k = 0; k < 2; k++) {
      const float x = edges[i * 5 + k * 2], z = edges[i * 5 + k * 2 + 1];
      x0 = std::fmin(x0, x);
      x1 = std::fmax(x1, x);
      z0 = std::fmin(z0, z);
      z1 = std::fmax(z1, z);
    }
  return std::fmin(24000.0f, std::fmax(10000.0f, 0.3f * std::fmax(x1 - x0, z1 - z0)));
}

// --- where the map goes (owner's report 05.11.2026: right with "16:9" and "Stretched", too far left and over the
// picture's edge with "4:3", hanging into the bar below the picture with "4:3 (GC)") ---------------------------
// Rectangles on the screen in pixels, from its bottom left (OpenGL: y up).
struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

// "Native" (format 5, wish of the project owner, 07.11.2026): the screen's own aspect, measured from the surface.
// Wider than 16:9 the game's 3D is drawn wider while the player plays (patch 0214; NativeAspect3d below is what the
// core is told): such a picture (RE4_GX_CHUNK_WIDE, GxRenderer::XfbWide) shows the rows 56..392 of 448 over the whole
// screen, its HUD keeps the 16:9 shape in the middle; every other picture (the game's menus, the scope, movies)
// goes into the 16:9 box, as "16:9". Up to 16:9 (a 16:10 tablet, a 4:3 handheld) the game draws without its
// letterbox in play (as "4:3", re4_core_set_letterbox) and the screen shows the middle 336 x (16/9) / aspect rows
// (all 448 at 4:3) over its whole width: more above and below, nothing cut at the sides, the HUD and the menus in
// their 16:9 shape; narrower than 4:3 a 4:3 box. Exactly 16:9 (within half a percent) is "16:9".
struct NativeView {
  int mode = 0;      // 1 wider than 16:9, -1 narrower (no letterbox in play), 0 as "16:9"
  int top = 56, rows = 336;  // the EFB rows shown (of 448)
  Rect pic, hud;     // the picture's box and the HUD in it, from the screen's bottom left
};

inline int NativeMode(int width, int height) {
  if (width <= 0 || height <= 0)
    return 0;
  const double a = static_cast<double>(width) / height, wide = 16.0 / 9.0;
  return a > wide * 1.005 ? 1 : a < wide / 1.005 ? -1 : 0;
}

// The aspect of the game's 3D the core is told (x 10000, re4_core_set_view_aspect): 4/3 x aspect / (16/9) wider
// than 16:9, else 0 (the game's 4:3).
inline int NativeAspect3d(int width, int height) {
  return NativeMode(width, height) == 1 ? static_cast<int>(0.75 * width / height * 10000.0 + 0.5) : 0;
}

inline NativeView Native(int width, int height, bool wideChunk) {
  NativeView v;
  v.mode = NativeMode(width, height);
  if (v.mode == 1 && wideChunk) {
    v.pic = {0, 0, width, height};
    const int hudW = std::min(width, static_cast<int>(height * 16.0 / 9.0 + 0.5));
    v.hud = {(width - hudW) / 2, 0, hudW, height};
    return v;
  }
  if (v.mode == -1) {
    // the rows that fill the width at the screen's aspect (the 336 of the letterbox are 16:9), at most all 448
    const double a = static_cast<double>(width) / height;
    v.rows = std::min(448, static_cast<int>(336.0 * 16.0 / 9.0 / a + 0.5) & ~1);
    v.top = (448 - v.rows) / 2;
    int boxW = width, boxH = height;
    if (v.rows == 448) {
      boxH = std::min(height, static_cast<int>(width * 3.0 / 4.0 + 0.5));
      boxW = std::min(width, static_cast<int>(boxH * 4.0 / 3.0 + 0.5));
    }
    v.pic = {(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
    v.hud = {v.pic.x, v.pic.y + boxH * (56 - v.top) / v.rows, boxW, boxH * 336 / v.rows};
    return v;
  }
  // as "16:9": the 16:9 box in the middle
  int boxW = width, boxH = width * 9 / 16;
  if (boxH > height) {
    boxH = height;
    boxW = height * 16 / 9;
  }
  v.pic = {(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
  v.hud = v.pic;
  return v;
}

// The picture's box and the game's own HUD on the screen for the menu's picture format, as drawGx (core_mode.cpp)
// presents the XFB (`xh` lines): 0 "16:9" and 2 "Stretched" show the lines 56..391 of 448 (the game's letterboxed
// part) in the box; 1 "4:3 (GC)" all lines in a 4:3 frame of 480, the HUD in the lines 56..391 of it; 3 "4:3" all
// 448 lines in an exact 4:3 box (until 0.99.9 with the flat draws 15/14 wider than the box; since 0.99.10 as wide). The game draws its gauge
// (life, ammo) in those lines in each format (it sits at line 384 in all of them), so the map, placed in the
// HUD's rectangle, mirrors the gauge at the bottom right in every format. The boxes must stay those of drawGx.
inline void PictureRects(int format, int width, int height, int xh, Rect *hud, Rect *pic, bool wideChunk = false) {
  if (xh <= 0)
    xh = 448;
  if (format == 5) {
    // "Native" (the rows the XFB holds of the 448 do not matter: the shown part is in the game's 448)
    const NativeView v = Native(width, height, wideChunk);
    *pic = v.pic;
    *hud = v.hud;
    return;
  }
  if (format == 3) {
    int boxW = width, boxH = static_cast<int>(width * 3.0 / 4.0 + 0.5);
    if (boxH > height) {
      boxH = height;
      boxW = static_cast<int>(height * 4.0 / 3.0 + 0.5);
    }
    *pic = {(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
    *hud = {pic->x, pic->y + boxH * 56 / 448, boxW, boxH * 336 / 448};
  } else if (format == 1) {
    int boxW = width, boxH = width * 3 / 4;
    if (boxH > height) {
      boxH = height;
      boxW = height * 4 / 3;
    }
    const int picH = boxH * xh / 480;
    const int top = xh * 56 / 448, rows = xh * 336 / 448;
    *pic = {(width - boxW) / 2, (height - picH) / 2, boxW, picH};
    *hud = {pic->x, pic->y + picH * (xh - top - rows) / xh, boxW, picH * rows / xh};
  } else {
    int boxW = width, boxH = width * 9 / 16;
    if (format == 2) {
      boxH = height;
    } else if (boxH > height) {
      boxH = height;
      boxW = height * 16 / 9;
    }
    *pic = {(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
    *hud = *pic;
  }
}

// The part of the picture the screen shows: the box, but with "4:3 (GC)" (1) the rows 56..392 in it (the game's
// letterbox covers the others). The core puts the game's life gauge in its bottom right corner (patch 0215,
// re4_core_set_hud_layout), the map goes into its bottom left corner.
inline Rect Shown(int format, const Rect &hud, const Rect &pic) { return format == 1 ? hud : pic; }

// The map's square: a little over a quarter of the HUD's height, in the bottom left corner of the shown part, as
// far from its side as from its bottom: 4 % of its height (wish of the project owner, 07.11.2026: map and life gauge
// in the corners with the same margin, the gauge mirrors it at the bottom right; with "16:9" the place it had).
inline Rect Place(const Rect &hud, const Rect &shown) {
  const int size = std::max(48, std::min(hud.h * 27 / 100, hud.w * 3 / 10));
  const int m = std::max(6, shown.h * 4 / 100);
  Rect r;
  r.x = shown.x + m;
  r.y = shown.y + m;
  r.w = r.h = size;
  return r;
}

// What the core is told for its life gauge (re4_core_set_hud_layout): the game's rows the shown part has, how much
// wider one unit of the game's HUD shows across than down (x 10000) and whether the ring is drawn that much narrower
// to keep its shape (flags bit 0, "Stretched"). `native` as Native() gave it for format 5.
struct HudLayout {
  int top = 56, bottom = 392, stretch = 10000, flags = 0;
};
inline HudLayout Hud(int format, int width, int height, const NativeView &native) {
  HudLayout h;
  if (format == 3) {
    h.top = 0;  // "4:3": all 448 rows in an exact 4:3 box
    h.bottom = 448;
  } else if (format == 1) {
    h.stretch = 10714;  // "4:3 (GC)": the 448 rows in a 4:3 frame of 480, 15/14 as wide as exact
  } else if (format == 2 && height > 0) {
    // "Stretched": the 16:9 rows over the whole screen; the ring keeps its shape
    h.stretch = static_cast<int>(static_cast<double>(width) / height * 9.0 / 16.0 * 10000.0 + 0.5);
    h.flags = 1;
  } else if (format == 5 && native.mode == -1) {
    h.top = native.top;  // "Native" narrower than 16:9: the middle rows
    h.bottom = native.top + native.rows;
  }
  return h;
}

}  // namespace re4mm
