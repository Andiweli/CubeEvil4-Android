// The app's minimap on the PC (04.11.2026): a dump of RE4_TEST_MINIMAP (state_trace.cpp: the room's outline from
// the core and the player's pose) drawn with the app's own projection (app/src/main/cpp/minimap_view.h) into a
// PPM, as core_mode.cpp draws it: the dark disc, the outline (other storeys fainter), the ring, the arrow.
//
//   g++ -O2 -std=c++17 -I app/src/main/cpp tools/minimap_check.cpp -o minimap_check
//   minimap_check DUMP OUT.ppm [radius]
//
// Prints the edges, the segments inside the map and how many end within 2 % of the ring (clipped there).
#include "minimap_view.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv) {
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
  std::fclose(f);
  re4mm::Pose pose;
  if (!re4mm::PoseFromMatrix(m, &pose))
    return 1;
  std::vector<float> lines;
  const float radius = argc > 3 ? static_cast<float>(std::atof(argv[3]))
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
