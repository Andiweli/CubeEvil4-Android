// The language pack for the app's own readers of disc files (port/docs/sprachpakete.md): the core reads the pack's
// files instead of the disc's itself (port/whole/runtime/dvd.cpp); the app's readers that open disc files directly
// (save artwork, start-up pictures, core_scene's sources) ask here.
#pragma once

#include <string>

namespace re4lang {

// The pack in use (its folder with overlay.tsv), "" none (CoreBridge.setLanguagePack).
void SetActivePack(const std::string& packDir);

// discFiles: a disc's ".../discN/files" folder; rel: the path on the disc. The pack's file when the pack in use
// replaces it, else discFiles + "/" + rel.
std::string DiscFile(const std::string& discFiles, const std::string& rel);

}  // namespace re4lang
