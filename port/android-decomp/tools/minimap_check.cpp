// The app's minimap on the PC (04.11.2026): a dump of RE4_TEST_MINIMAP (state_trace.cpp: the room's outline from
// the core and the player's pose) drawn with the app's own projection (app/src/main/cpp/minimap_view.h) into a
// PPM, as core_mode.cpp draws it: the dark disc, the outline (other storeys fainter), the ring, the arrow.
//
//   g++ -O2 -std=c++17 -I app/src/main/cpp tools/minimap_check.cpp -o minimap_check
//   minimap_check DUMP OUT.ppm [radius] [pickups mode 0..2]
//
// 1.1.6: the dump's "pick x z height kind" lines are the room's treasures and items (re4mm::PickupDots), drawn with
// mode 1 (shown) unless the fifth argument says otherwise.
//
// Prints the edges, the segments inside the map and how many end within 2 % of the ring (clipped there).
#include "minimap_view.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char **argv) {
  // minimap_check --photo: the photo mode rule (1.1.0): the minimap goes with the HUD switch, not with Leon's
  if (argc == 2 && std::string(argv[1]) == "--photo") {
    struct Case { bool photo; int mask; unsigned beFlag; bool shown, player; const char *what; } cases[] = {
        {false, 0, 0x27, true, true, "playing, Leon drawn"},
        {false, 0, 0x25, true, false, "playing, Leon not drawn (the game's own): no pose as before"},
        {true, 0, 0x27, true, true, "photo mode, nothing hidden"},
        {true, 1, 0x27, false, true, "photo mode, HUD hidden: no minimap"},
        {true, 2, 0x25, true, true, "photo mode, Leon hidden (bit 1 cleared): minimap stays"},
        {true, 3, 0x25, false, true, "photo mode, HUD and Leon hidden: no minimap"},
        {true, 14, 0x25, true, true, "photo mode, Leon, Ashley, enemies hidden: minimap stays"},
    };
    int bad = 0;
    for (const Case &c : cases) {
      const bool shown = re4mm::MinimapShown(c.photo, c.mask), player = re4mm::PlayerModel(c.beFlag, c.photo);
      const bool ok = shown == c.shown && player == c.player;
      bad += !ok;
      std::printf("%s %s: minimap %s, player %s\n", ok ? "ok  " : "FAIL", c.what, shown ? "shown" : "hidden",
                  player ? "found" : "not found");
    }
    return bad ? 1 : 0;
  }
  if (argc < 3) {
    std::fprintf(stderr, "usage: minimap_check DUMP OUT.ppm [radius]\n");
    return 2;
  }
  std::FILE *f = std::fopen(argv[1], "r");
  if (!f)
    return 1;
  float m[12] = {};
  unsigned room = 0;
  float px, py, pz, fx, fz;
  if (std::fscanf(f, "pose %f %f %f %f %f %u", &px, &py, &pz, &fx, &fz, &room) != 6)
    return 1;
  m[3] = px;
  m[7] = py;
  m[11] = pz;
  m[2] = fx;
  m[10] = fz;
  std::vector<float> edges;
  float e[5];
  while (std::fscanf(f, "%f %f %f %f %f", &e[0], &e[1], &e[2], &e[3], &e[4]) == 5)
    edges.insert(edges.end(), e, e + 5);
  std::vector<float> picks;
  int kind = 0;
  while (std::fscanf(f, " pick %f %f %f %d", &e[0], &e[1], &e[2], &kind) == 4) {
    picks.insert(picks.end(), e, e + 3);
    picks.push_back(static_cast<float>(kind));
  }
  std::fclose(f);
  re4mm::Pose pose;
  if (!re4mm::PoseFromMatrix(m, &pose))
    return 1;
  std::vector<float> lines;
  // (an empty or 0 radius: the room's own, as the app takes it)
  const float radius = argc > 3 && std::atof(argv[3]) > 0 ? static_cast<float>(std::atof(argv[3]))
                                                          : re4mm::RoomRadius(edges.data(), static_cast<int>(edges.size() / 5));
  re4mm::Project(edges.data(), static_cast<int>(edges.size() / 5), pose, radius, lines);
  const int S = 400;
  std::vector<float> img(S * S * 3, 0.15f);
  auto plot = [&](float x, float y, float r, float g, float b, float a) {
    const int ix = static_cast<int>((x * 0.5f + 0.5f) * (S - 1)), iy = static_cast<int>((0.5f - y * 0.5f) * (S - 1));
    if (ix < 0 || iy < 0 || ix >= S || iy >= S)
      return;
    float *p = &img[(iy * S + ix) * 3];
    p[0] = p[0] * (1 - a) + r * a;
    p[1] = p[1] * (1 - a) + g * a;
    p[2] = p[2] * (1 - a) + b * a;
  };
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) {
      const float u = (x + 0.5f) / S * 2 - 1, v = 1 - (y + 0.5f) / S * 2;
      if (u * u + v * v <= 1)
        plot(u, v, 0, 0, 0, 0.45f);
    }
  int nearRing = 0;
  for (size_t i = 0; i + 4 < lines.size(); i += 5) {
    const float x0 = lines[i], y0 = lines[i + 1], x1 = lines[i + 2], y1 = lines[i + 3], a = 0.85f * lines[i + 4];
    for (int k = 0; k <= 200; k++) {
      const float t = k / 200.0f;
      plot(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 0.86f, 0.92f, 0.86f, a);
    }
    for (float r : {std::hypot(x0, y0), std::hypot(x1, y1)})
      nearRing += r > 0.98f;
  }
  for (int k = 0; k < 720; k++)
    plot(std::cos(k * 0.00872665f) * 0.99f, std::sin(k * 0.00872665f) * 0.99f, 0.86f, 0.92f, 0.86f, 0.5f);
  // 1.1.6: the treasures and items, as the app draws them (filled triangles, then the rings)
  const int pickMode = argc > 4 ? std::atoi(argv[4]) : 1;
  std::vector<float> dotTris, dotLines;
  re4mm::PickupDots(picks.data(), static_cast<int>(picks.size() / 4), pose, radius, pickMode, dotTris, dotLines);
  for (size_t t = 0; t + 17 < dotTris.size(); t += 18) {
    const float *a = &dotTris[t], *b = &dotTris[t + 6], *c = &dotTris[t + 12];
    const float minx = std::fmin(a[0], std::fmin(b[0], c[0])), maxx = std::fmax(a[0], std::fmax(b[0], c[0]));
    const float miny = std::fmin(a[1], std::fmin(b[1], c[1])), maxy = std::fmax(a[1], std::fmax(b[1], c[1]));
    const float area = (b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]);
    if (std::fabs(area) < 1e-9f)
      continue;
    for (int y = static_cast<int>((0.5f - maxy * 0.5f) * (S - 1)) - 1; y <= static_cast<int>((0.5f - miny * 0.5f) * (S - 1)) + 1; y++)
      for (int x = static_cast<int>((minx * 0.5f + 0.5f) * (S - 1)) - 1; x <= static_cast<int>((maxx * 0.5f + 0.5f) * (S - 1)) + 1; x++) {
        const float u = (x + 0.5f) / (S - 1) * 2 - 1, v = 1 - (y + 0.5f) / (S - 1) * 2;
        const float w0 = ((b[0] - u) * (c[1] - v) - (c[0] - u) * (b[1] - v)) / area;
        const float w1 = ((c[0] - u) * (a[1] - v) - (a[0] - u) * (c[1] - v)) / area;
        const float w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0)
          continue;
        plot(u, v, a[2] * w0 + b[2] * w1 + c[2] * w2, a[3] * w0 + b[3] * w1 + c[3] * w2, a[4] * w0 + b[4] * w1 + c[4] * w2,
             a[5] * w0 + b[5] * w1 + c[5] * w2);
      }
  }
  for (size_t l = 0; l + 11 < dotLines.size(); l += 12) {
    const float *a = &dotLines[l], *b = &dotLines[l + 6];
    for (int k = 0; k <= 30; k++) {
      const float t = k / 30.0f;
      plot(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2], a[3], a[4], a[5]);
    }
  }
  int counts[4] = {}, hiddenCount = 0;
  for (size_t i = 3; i < picks.size(); i += 4) {
    counts[(static_cast<int>(picks[i]) & 7) % 4]++;
    hiddenCount += (static_cast<int>(picks[i]) & 8) != 0;
  }
  std::printf("pickups: %d items, %d treasures, %d medallions, %d of them hidden; drawn with mode %d\n", counts[1],
              counts[2], counts[3], hiddenCount, pickMode);
  for (int k = 0; k <= 40; k++) {  // the arrow's outline
    const float t = k / 40.0f;
    plot(-0.08f + 0.08f * t, -0.09f + 0.22f * t, 1, 0.78f, 0.25f, 1);
    plot(0.08f - 0.08f * t, -0.09f + 0.22f * t, 1, 0.78f, 0.25f, 1);
  }
  std::FILE *o = std::fopen(argv[2], "wb");
  std::fprintf(o, "P6\n%d %d\n255\n", S, S);
  for (float v : img)
    std::fputc(static_cast<int>(std::fmin(1.0f, v) * 255.0f + 0.5f), o);
  std::fclose(o);
  std::printf("r%03x: %zu edges, %zu segments in the map, %d ends at the ring, player (%.0f %.0f %.0f) facing (%.2f %.2f)\n",
              room, edges.size() / 5, lines.size() / 5, nearRing, px, py, pz, pose.fx, pose.fz);
  return 0;
}
