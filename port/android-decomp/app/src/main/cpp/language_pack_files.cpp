// The language pack for the app's own readers of disc files (language_pack_files.h).
#include "language_pack_files.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <sys/stat.h>

namespace re4lang {

namespace {

std::mutex gLock;
std::map<std::string, std::string> gFiles;  // "<disc 1|2>\t<lower-case path>" -> the pack's file

std::string lower(std::string s) {
  for (auto& c : s) c = char(tolower((unsigned char) c));
  return s;
}

}  // namespace

void SetActivePack(const std::string& packDir) {
  std::map<std::string, std::string> files;
  FILE* f = packDir.empty() ? nullptr : fopen((packDir + "/overlay.tsv").c_str(), "r");
  if (f) {
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (line[0] != 'F' || line[1] != '\t') continue;
      char* field[5];
      int n = 0;
      for (char* p = line; n < 5 && p;) {
        field[n++] = p;
        p = strchr(p, '\t');
        if (p) *p++ = 0;
      }
      if (n == 5 && !strstr(field[4], "..")) files[std::string(field[1]) + "\t" + lower(field[2])] = packDir + "/" + field[4];
    }
    fclose(f);
  }
  std::lock_guard<std::mutex> hold(gLock);
  gFiles.swap(files);
}

std::string DiscFile(const std::string& discFiles, const std::string& rel) {
  std::string plain = discFiles + "/" + rel;
  std::lock_guard<std::mutex> hold(gLock);
  if (gFiles.empty()) return plain;
  // ".../disc<N>/files": the app's folders count from 1 (disc1, disc2), the PC's from 0 when a disc0 folder is there
  size_t at = discFiles.rfind("/disc");
  if (at == std::string::npos || at + 6 >= discFiles.size() || !isdigit((unsigned char) discFiles[at + 5])) return plain;
  int n = discFiles[at + 5] - '0';
  struct stat st;
  if (stat((discFiles.substr(0, at) + "/disc0").c_str(), &st) == 0) n++;
  auto it = gFiles.find(std::to_string(n) + "\t" + lower(rel));
  return it == gFiles.end() ? plain : it->second;
}

}  // namespace re4lang
