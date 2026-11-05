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

// The picture's box and the game's own HUD on the screen for the menu's picture format, as drawGx (core_mode.cpp)
// presents the XFB (`xh` lines): 0 "16:9" and 2 "Stretched" show the lines 56..391 of 448 (the game's letterboxed
// part) in the box; 1 "4:3 (GC)" all lines in a 4:3 frame of 480, the HUD in the lines 56..391 of it; 3 "4:3" all
// 448 lines in an exact 4:3 box with the flat draws (the HUD) 15/14 wider than the box. The game draws its gauge
// (life, ammo) in those lines in each format (it sits at line 384 in all of them), so the map, placed in the
// HUD's rectangle, mirrors the gauge at the bottom right in every format. The boxes must stay those of drawGx.
inline void PictureRects(int format, int width, int height, int xh, Rect *hud, Rect *pic) {
  if (xh <= 0)
    xh = 448;
  if (format == 3) {
    int boxW = width, boxH = static_cast<int>(width * 3.0 / 4.0 + 0.5);
    if (boxH > height) {
      boxH = height;
      boxW = static_cast<int>(height * 4.0 / 3.0 + 0.5);
    }
    const int layerW = static_cast<int>(boxW * 15.0 / 14.0 + 0.5);
    *pic = {(width - boxW) / 2, (height - boxH) / 2, boxW, boxH};
    *hud = {(width - layerW) / 2, pic->y + boxH * 56 / 448, layerW, boxH * 336 / 448};
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

// The map's square: a little over a quarter of the HUD's height, 2.25 % of its width from its left and 4 % of its
// height from its bottom (with "16:9" both are the 4 % of the height that was right before), and never left of
// the picture's box (with "4:3" the HUD's layer is wider than the box) nor below it.
inline Rect Place(const Rect &hud, const Rect &pic) {
  const int size = std::max(48, std::min(hud.h * 27 / 100, hud.w * 3 / 10));
  const int mx = std::max(6, hud.w * 9 / 400), my = std::max(6, hud.h * 4 / 100);
  Rect r;
  r.x = std::max(hud.x + mx, pic.x + std::max(6, pic.w * 9 / 400));
  r.y = std::max(hud.y + my, pic.y + 6);
  r.w = r.h = size;
  return r;
}

}  // namespace re4mm
