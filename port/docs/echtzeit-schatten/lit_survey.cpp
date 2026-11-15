// realtime-shadows study: the lights of every room's LIT (all cuts), counted by what a shadow system could use.
//   lit_survey ROOM.das...   one line per room, then totals
#include "archive.h"
#include "room_lighting.h"
#include "ports/yz2.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <map>
#include <string>

int main(int argc, char **argv) {
  unsigned rooms = 0, withFoot = 0, withCast = 0, withShadowTex = 0, withWorldDir = 0, withCamDir = 0, withLocal = 0,
           withAnyWorld = 0, failed = 0;
  std::map<std::string, unsigned> footDirs;
  for (int i = 1; i < argc; i++) {
    std::string name = argv[i];
    const auto slash = name.find_last_of('/');
    if (slash != std::string::npos)
      name = name.substr(slash + 1);
    try {
      const re4::RoomArchive archive(re4::LoadYz2(argv[i]).bytes);
      // the LIT alone (ReadRoomLighting also reads the CAM areas, which some rooms exceed the app's budget with)
      const auto *litEntry = archive.find("LIT");
      if (!litEntry)
        throw std::runtime_error("no LIT");
      const auto lit = std::make_shared<re4::RoomLighting>(archive.copy(litEntry - archive.entries().data()));
      unsigned cuts = 0, foot = 0, cast = 0, shadowTex = 0, worldDir = 0, camDir = 0, point = 0, spot = 0;
      std::string footText;
      for (std::size_t c = 0; c < lit->cuts.size(); c++) {
        if (!lit->present[c])
          continue;
        cuts++;
        for (const re4::RoomLight &l : lit->cuts[c].lights) {
          if ((l.flags & 3) != 3)
            continue;
          if (l.id == 4) {
            if (l.shadow.kind == 5) {
              foot++;
              char b[96];
              std::snprintf(b, sizeof(b), " t%u(%d,%d)c%u", l.type, l.shadow.angleX, l.shadow.angleY, l.color[0]);
              if (footText.size() < 120)
                footText += b;
              char k[48];
              std::snprintf(k, sizeof(k), "type %u ax %d", l.type, l.shadow.angleX);
              footDirs[k]++;
            } else if (l.shadow.kind >= 1 && l.shadow.kind <= 4) {
              cast++;
            } else {
              shadowTex++;
            }
            continue;
          }
          if (l.type == 5) {
            if (l.directionFlags & 1)
              camDir++;
            else
              worldDir++;
          } else if (l.type == 3 || l.type == 6) {
            spot++;
          } else if (l.type <= 2 || l.type == 4) {
            point++;
          }
        }
      }
      rooms++;
      withFoot += foot > 0;
      withCast += cast > 0;
      withShadowTex += shadowTex > 0;
      withWorldDir += worldDir > 0;
      withCamDir += camDir > 0;
      withLocal += point + spot > 0;
      withAnyWorld += (foot + worldDir + point + spot) > 0;
      std::printf("%s cuts %u | foot-shadow lights %u%s | cast %u | shadow-texture lights %u | parallel world %u camera %u "
                  "| point %u spot %u\n",
                  name.c_str(), cuts, foot, footText.c_str(), cast, shadowTex, worldDir, camDir, point, spot);
    } catch (const std::exception &e) {
      failed++;
      std::printf("%s: %s\n", name.c_str(), e.what());
    }
  }
  std::printf("rooms %u (failed %u): foot-shadow lights in %u, cast lights in %u, shadow-texture lights in %u, world "
              "parallel lights in %u, camera parallel lights in %u, point/spot lights in %u, any world source in %u\n",
              rooms, failed, withFoot, withCast, withShadowTex, withWorldDir, withCamDir, withLocal, withAnyWorld);
  for (const auto &f : footDirs)
    std::printf("foot light %s: %u\n", f.first.c_str(), f.second);
  return 0;
}
