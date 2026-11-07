// Where the app puts the minimap in each picture format (owner's report 05.11.2026: right with "16:9", too far
// left with "4:3", below the picture with "4:3 (GC)"), with the app's own rule (app/src/main/cpp/minimap_view.h
// re4mm::PictureRects / Place, as core_mode.cpp minimapDraw uses it).
//
//   g++ -O2 -std=c++17 -I app/src/main/cpp tools/minimap_place_check.cpp -o minimap_place_check
//   minimap_place_check                         the table: formats x screens, checks
//   minimap_place_check IN.ppm FORMAT OUT.ppm   the map's ring drawn into a screenshot (binary PPM, P6;
//                                               FORMAT 6: "Native" with a wide picture)
//
// Checks per format and screen: the map inside the shown part of the picture (re4mm::Shown), in its bottom left
// corner as far from the side as from the bottom (since 07.11.2026, the life gauge mirrors it at the bottom right),
// and with "16:9" exactly the place the app had before (the owner: right there). The life gauge's layout the core
// is told (re4mm::Hud): its rows the shown part's, a ring of the same shape. "Native" (0.99.0,
// re4mm::Native) twice, with a picture the core drew wide (RE4_GX_CHUNK_WIDE) and with any other: on a 16:9 screen
// both, and on every screen the other, exactly as "16:9"; a wide picture over the whole screen wider than 16:9 with
// the HUD in a 16:9 box in its middle; narrower than 16:9 the picture over the whole width, its HUD 16:9 in it.
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
  case 5:
    return "Native";
  case 6:
    return "Native/W";
  }
  return "?";
}

// format 6 here: "Native" (5) with a wide picture
void Rects(int format, int w, int h, re4mm::Rect *hud, re4mm::Rect *pic) {
  re4mm::PictureRects(format == 6 ? 5 : format, w, h, 448, hud, pic, format == 6);
}

// The checks of "Native" on one screen: false with the reason printed
bool NativeRight(int w, int h, int format, const re4mm::Rect &hud, const re4mm::Rect &pic) {
  re4mm::Rect hud0, pic0;
  re4mm::PictureRects(0, w, h, 448, &hud0, &pic0);
  const auto same = [](const re4mm::Rect &a, const re4mm::Rect &b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
  };
  const int mode = re4mm::NativeMode(w, h);
  const re4mm::NativeView v = re4mm::Native(w, h, format == 6);
  const char *why = nullptr;
  if (mode == 0 || (mode == 1 && format == 5)) {
    if (!same(pic, pic0) || !same(hud, hud0))
      why = "not as 16:9";
    if (v.top != 56 || v.rows != 336)
      why = "rows not 56..392";
  } else if (mode == 1) {
    const int hudW = static_cast<int>(h * 16.0 / 9.0 + 0.5);
    if (pic.x != 0 || pic.y != 0 || pic.w != w || pic.h != h)
      why = "wide picture not over the whole screen";
    else if (hud.h != h || std::abs(hud.w - hudW) > 1 || std::abs(hud.x - (w - hud.w) / 2) > 1)
      why = "HUD not the 16:9 middle";
    else if (std::abs(re4mm::NativeAspect3d(w, h) - 0.75 * w / h * 10000.0) > 1.0)
      why = "3D aspect";
  } else {
    // the shown rows at the screen's aspect: (4/3) x 448 / rows = the box's aspect, rows symmetric
    const double shown = 4.0 / 3.0 * 448.0 / v.rows, box = double(pic.w) / pic.h;
    if (v.top * 2 + v.rows != 448 || v.rows < 336 || v.rows > 448 || std::fabs(shown - box) > 0.01)
      why = "rows";
    else if (v.rows < 448 && (pic.w != w || pic.h != h))
      why = "not over the whole screen";
    else if (std::fabs(double(hud.w) / hud.h - 16.0 / 9.0) > 0.01 || hud.y < pic.y || hud.y + hud.h > pic.y + pic.h)
      why = "HUD not 16:9 in the picture";
    else if (re4mm::NativeAspect3d(w, h) != 0)
      why = "3D aspect not the game's";
  }
  if (why)
    std::printf("  %dx%d %s: %s\n", w, h, Name(format), why);
  return !why;
}

int Table() {
  const int screens[][2] = {{1920, 1080}, {2400, 1080}, {2340, 1080}, {2000, 923}, {1280, 720}, {1024, 768}, {1600, 720},
                            {2560, 1600}, {1920, 1200}, {2560, 1080}, {3200, 1440}, {1280, 1024}, {1919, 1080}};
  int bad = 0;
  for (const auto &s : screens) {
    for (int format : {1, 3, 0, 2, 5, 6}) {
      re4mm::Rect hud, pic;
      Rects(format, s[0], s[1], &hud, &pic);
      const re4mm::Rect shown = re4mm::Shown(format == 6 ? 5 : format, hud, pic);
      const re4mm::Rect at = re4mm::Place(hud, shown);
      const bool inside = at.x >= shown.x && at.y >= shown.y && at.x + at.w <= shown.x + shown.w &&
                          at.y + at.h <= shown.y + shown.h && at.x >= pic.x && at.y >= pic.y;
      // the same margin to the side and to the bottom; the gauge's rows the shown part's rows
      bool onHud = at.x - shown.x == at.y - shown.y;
      const re4mm::HudLayout layout = re4mm::Hud(format == 6 ? 5 : format, s[0], s[1], re4mm::Native(s[0], s[1], format == 6));
      const double rowsShown = layout.bottom - layout.top, boxRows = format == 1 ? 336.0 : rowsShown;
      onHud = onHud && std::abs(rowsShown - boxRows) < 0.5 && layout.top >= 0 && layout.bottom <= 448;
      if (format == 2)
        onHud = onHud && layout.flags == 1 && std::abs(layout.stretch / 10000.0 - double(s[0]) / s[1] * 9.0 / 16.0) < 1e-3;
      else
        onHud = onHud && layout.flags == 0 && layout.stretch == (format == 1 ? 10714 : 10000);
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
                  mapLine, inside ? "inside" : "OUTSIDE", onHud ? "" : " NOT-IN-CORNER", same ? "" : " CHANGED");
      bad += !inside || !onHud || !same;
      if (format >= 5)
        bad += !NativeRight(s[0], s[1], format, hud, pic);
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
  Rects(format, w, h, &hud, &pic);
  const re4mm::Rect at = re4mm::Place(hud, re4mm::Shown(format == 6 ? 5 : format, hud, pic));
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
