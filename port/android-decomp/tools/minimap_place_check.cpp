// Where the app puts the minimap in each picture format (owner's report 05.11.2026: right with "16:9", too far
// left with "4:3", below the picture with "4:3 (GC)"), with the app's own rule (app/src/main/cpp/minimap_view.h
// re4mm::PictureRects / Place, as core_mode.cpp minimapDraw uses it).
//
//   g++ -O2 -std=c++17 -I app/src/main/cpp tools/minimap_place_check.cpp -o minimap_place_check
//   minimap_place_check                         the table: formats x screens, checks
//   minimap_place_check IN.ppm FORMAT OUT.ppm   the map's ring drawn into a screenshot (binary PPM, P6)
//
// Checks per format and screen: the map inside the picture's box, its bottom at the HUD's bottom plus the
// margin, and with "16:9" exactly the place the app had before (the owner: right there).
#include "minimap_view.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

const char *Name(int format) {
  switch (format) {
  case 0:
    return "16:9";
  case 1:
    return "4:3 (GC)";
  case 2:
    return "Stretched";
  case 3:
    return "4:3";
  }
  return "?";
}

int Table() {
  const int screens[][2] = {{1920, 1080}, {2400, 1080}, {2340, 1080}, {2000, 923}, {1280, 720}, {1024, 768}, {1600, 720}};
  int bad = 0;
  for (const auto &s : screens) {
    for (int format : {1, 3, 0, 2}) {
      re4mm::Rect hud, pic;
      re4mm::PictureRects(format, s[0], s[1], 448, &hud, &pic);
      const re4mm::Rect at = re4mm::Place(hud, pic);
      const bool inside = at.x >= pic.x && at.y >= pic.y && at.x + at.w <= pic.x + pic.w && at.y + at.h <= pic.y + pic.h;
      const bool onHud = at.y >= hud.y && at.y + at.h <= hud.y + hud.h;
      bool same = true;
      if (format == 0) {
        // the place before 05.11.2026: margin 4 % of the box's height on both sides
        const int size = std::max(48, std::min(pic.h * 27 / 100, pic.w * 3 / 10)), margin = std::max(6, pic.h * 4 / 100);
        same = std::abs(at.x - (pic.x + margin)) <= 1 && at.y == pic.y + margin && at.w == size;
      }
      // the gauge's line 384 of 448 (its bottom): how far the map's bottom is from the HUD's bottom, as the
      // gauge's in lines of the game
      const double mapLine = 56.0 + 336.0 * (1.0 - double(at.y - hud.y) / hud.h);
      std::printf("%4dx%-4d %-9s  picture %4d,%4d %4dx%4d  HUD %4d,%4d %4dx%4d  map %4d,%4d size %3d  bottom at line %.0f  %s%s%s\n",
                  s[0], s[1], Name(format), pic.x, pic.y, pic.w, pic.h, hud.x, hud.y, hud.w, hud.h, at.x, at.y, at.w,
                  mapLine, inside ? "inside" : "OUTSIDE", onHud ? "" : " OFF-HUD", same ? "" : " CHANGED");
      bad += !inside || !onHud || !same;
    }
  }
  std::printf("%d problems\n", bad);
  return bad ? 1 : 0;
}

int Draw(const char *in, int format, const char *out) {
  std::FILE *f = std::fopen(in, "rb");
  if (!f)
    return 1;
  int w = 0, h = 0, mx = 0;
  if (std::fscanf(f, "P6 %d %d %d", &w, &h, &mx) != 3 || mx != 255)
    return 1;
  std::fgetc(f);
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 3);
  if (std::fread(px.data(), 1, px.size(), f) != px.size())
    return 1;
  std::fclose(f);
  re4mm::Rect hud, pic;
  re4mm::PictureRects(format, w, h, 448, &hud, &pic);
  const re4mm::Rect at = re4mm::Place(hud, pic);
  const double cx = at.x + at.w / 2.0, cy = at.y + at.h / 2.0, r = at.w / 2.0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int gy = h - 1 - y;  // the rule's y is from the bottom
      const double d = std::hypot(x + 0.5 - cx, gy + 0.5 - cy);
      unsigned char *p = &px[(static_cast<size_t>(y) * w + x) * 3];
      if (std::fabs(d - r) < 2.0) {
        p[0] = 255, p[1] = 40, p[2] = 200;  // the new ring, magenta
      } else if ((gy == hud.y || gy == hud.y + hud.h - 1) && x >= hud.x && x < hud.x + hud.w && (x / 6) % 2 == 0) {
        p[0] = 40, p[1] = 220, p[2] = 255;  // the HUD's lines, dashed cyan
      }
    }
  f = std::fopen(out, "wb");
  if (!f)
    return 1;
  std::fprintf(f, "P6 %d %d 255\n", w, h);
  std::fwrite(px.data(), 1, px.size(), f);
  std::fclose(f);
  std::printf("%s: %s, map %d,%d size %d\n", out, Name(format), at.x, at.y, at.w);
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 1)
    return Table();
  if (argc == 4)
    return Draw(argv[1], std::atoi(argv[2]), argv[3]);
  std::fprintf(stderr, "usage: minimap_place_check [IN.ppm FORMAT OUT.ppm]\n");
  return 2;
}
