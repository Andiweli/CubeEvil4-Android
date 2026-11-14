// The window version's texts (see gui_text.h), English only. Our own code (CC0).
#include "gui_text.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace guitext {

namespace {

struct Row {
  Id id;
  const char *text;
};

// clang-format off
const Row kRows[] = {
    {kVersion, "Version %1"},
    {kIntro,
     "Turns an HD texture pack in the Dolphin format (a ZIP file or a folder) into one .c4tex file for CubeEvil4. The pack is only read, never changed; a ZIP file is read as it is, nothing is unpacked."},
    {kLegal,
     "CubeEvil4 and this program include no textures and link to none. Use only packs you obtained yourself and may use."},
    {kDropTitle, "Drag your texture pack here"},
    {kDropSub, "a ZIP file or the unpacked folder"},
    {kDropRelease, "Release to open the pack"},
    {kChooseZip, "Choose ZIP file…"},
    {kChooseFolder, "Choose folder…"},
    {kReading, "Reading the pack …"},
    {kReadingFiles, "Reading the pack: %1 files …"},
    {kReadingSizes, "Reading the image sizes: %1 of %2 …"},
    {kNotFound, "The folder or ZIP file was not found: %1"},
    {kZipFailed, "The ZIP file cannot be read: %1"},
    {kNoImages, "No Dolphin texture images found (tex1_… files as .dds, .png or .jpg)."},
    {kOneAtATime, "Please drop one ZIP file or one folder."},
    {kNotZip, "Not a ZIP file or a folder: %1"},
    {kUnpackFirst, "A 7z or RAR file: unpack it first, then drag the folder here."},
    {kImagesSize, "%1 images, %2"},
    {kKindZip, "ZIP file"},
    {kKindFolder, "folder"},
    {kLeftOut, "Left out (not images of the pack, or the same image twice): %1 files"},
    {kOtherPack, "Other pack…"},
    {kChoices, "Choices of the pack"},
    {kAsMain, "As in the main folder"},
    {kSameAsMain, "%1 (the same as the main folder)"},
    {kAdd, "add these images"},
    {kSize, "Texture size"},
    {kSize256, "256: smallest, saves memory and space"},
    {kSize512, "512: recommended for every device"},
    {kSize1024, "1024: sharpest, for strong devices with enough space"},
    {kNeverSmaller, "No image gets smaller than the game's own."},
    {kEstimate, "up to %1 · about %2"},
    {kEstimating, "estimating …"},
    {kSeconds, "%1 s"},
    {kMinutes, "%1 min"},
    {kMinSec, "%1 min %2 s"},
    {kHours, "%1 h %2 min"},
    {kStart, "Save and start…"},
    {kSaveTitle, "Where should the texture pack be saved?"},
    {kFileType, "CubeEvil4 texture pack"},
    {kNoSpace, "Not enough free space: about %1 needed, %2 free."},
    {kChooseOther, "Choose another place or a smaller size."},
    {kSmallerFits, "Size %1 fits."},
    {kInsidePack, "The file cannot be saved inside the texture pack's folder. Please choose another folder."},
    {kCannotWrite, "The file cannot be written there: %1"},
    {kConverting, "Converting the texture pack"},
    {kStepPrepare, "Prepare"},
    {kStepPrepareSub, "Enough free space (%1 free, about %2 needed), file created"},
    {kStepConvert, "Convert the images"},
    {kStepConvertSub,
     "Each image is read from the pack, brought to the chosen size, compressed for the phone's graphics chip (ETC2) and written into the file."},
    {kStepFinish, "Finish the file"},
    {kStepFinishSub, "Index and checksums; then the report next to the file"},
    {kStatConverted, "Converted"},
    {kStatWritten, "Written"},
    {kStatSkipped, "Skipped"},
    {kStatRefused, "Refused"},
    {kOfImages, "%1 of %2 images"},
    {kNow, "Now: %1"},
    {kElapsed, "Elapsed: %1"},
    {kTimeLeft, "About %1 left"},
    {kCancel, "Cancel"},
    {kCancelling, "Cancelling … the images in work are finished first."},
    {kCancelAsk, "Stop converting?"},
    {kCancelAskText, "The unfinished file will be deleted. The pack stays as it is."},
    {kStop, "Stop"},
    {kKeepGoing, "Keep converting"},
    {kDone, "Done!"},
    {kSaved, "Saved: %1 (%2)"},
    {kInFolder, "in %1"},
    {kConvertedIn, "%1 images converted in %2."},
    {kKinds, "%1 opaque, %2 with cut-out transparency, %3 with soft transparency."},
    {kSkippedDone, "Left out: %1 files (not images of the pack, or the same image twice). The report lists them."},
    {kRefusedDone, "Refused: %1 images. The game keeps its own textures for them:"},
    {kMore, "… and %1 more (see the report)."},
    {kNext,
     "Next: copy the file to your device. In CubeEvil4: app menu (SELECT) → Cheats & Mods → Mods → Import texture pack."},
    {kOpenFolder, "Open folder"},
    {kShowReport, "Show report"},
    {kClose, "Close"},
    {kBack, "Back"},
    {kFailed, "Converting failed"},
    {kReason, "Reason: %1"},
    {kNoFileLeft, "No file was left behind."},
    {kCancelled, "Cancelled"},
    {kDiskFull, "the disk is full"},
    {kNoImageConverted, "no image could be converted"},
    {kWriteFailed, "writing the file failed"},
    {kConsoleHint,
     "This is the window version. For the command line, use CubeEvil4-TexturePacker-Console.exe (same options)."},
    {kPercent, "%1%%"},
    {kMB, "%1 MB"},
    {kGB, "%1 GB"},
};

// clang-format on

static_assert(sizeof(kRows) / sizeof(kRows[0]) == kIdCount, "one row per text");

// "1 files" -> "1 file", "1 images" -> "1 image" (a 1 that is a number of its own)
void Singular(std::string &s) {
  static const char *const kPlurals[] = {" files", " images"};
  for (const char *plural : kPlurals) {
    const std::string pl = std::string("1") + plural;
    for (std::size_t at = s.find(pl); at != std::string::npos; at = s.find(pl, at + 1)) {
      const bool alone = at == 0 || !(std::isdigit(static_cast<unsigned char>(s[at - 1])) || s[at - 1] == ',' ||
                                      s[at - 1] == '.');
      const std::size_t after = at + pl.size();
      const bool wordEnd = after >= s.size() || !std::isalpha(static_cast<unsigned char>(s[after]));
      if (alone && wordEnd) s.erase(after - 1, 1);
    }
  }
}

}  // namespace

const char *Text(Id id) {
  if (id < 0 || id >= kIdCount) return "";
  return kRows[id].text;
}

Id RowId(int row) { return row >= 0 && row < kIdCount ? kRows[row].id : kIdCount; }

std::string Format(const std::string &pattern, std::initializer_list<std::string> args) {
  std::string out;
  out.reserve(pattern.size() + 32);
  for (std::size_t i = 0; i < pattern.size(); i++) {
    const char c = pattern[i];
    if (c == '%' && i + 1 < pattern.size()) {
      const char n = pattern[i + 1];
      if (n == '%') {
        out.push_back('%');
        i++;
        continue;
      }
      if (n >= '1' && n <= '9') {
        const std::size_t k = static_cast<std::size_t>(n - '1');
        if (k < args.size()) out += *(args.begin() + k);
        i++;
        continue;
      }
    }
    out.push_back(c);
  }
  Singular(out);
  return out;
}

std::string Count(std::uint64_t v) {
  std::string s = std::to_string(v);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
  return s;
}

std::string Bytes(std::uint64_t b) {
  char buf[64];
  if (b >= 1000ull * 1000 * 1000) {
    std::snprintf(buf, sizeof(buf), b >= 10ull * 1000 * 1000 * 1000 ? "%.1f" : "%.2f", b / 1e9);
    return Format(Text(kGB), {buf});
  }
  std::snprintf(buf, sizeof(buf), "%.0f", b / 1e6);
  return Format(Text(kMB), {buf});
}

std::string Duration(double seconds) {
  const long s = static_cast<long>(seconds + 0.5);
  if (s >= 3600) return Format(Text(kHours), {std::to_string(s / 3600), std::to_string((s / 60) % 60)});
  if (s >= 60) return Format(Text(kMinSec), {std::to_string(s / 60), std::to_string(s % 60)});
  return Format(Text(kSeconds), {std::to_string(s)});
}

std::string About(double seconds) {
  if (seconds < 55) {
    const long s = std::max(10L, static_cast<long>(std::lround(seconds / 10.0)) * 10);
    return Format(Text(kSeconds), {std::to_string(s)});
  }
  long minutes = std::max(1L, static_cast<long>(std::lround(seconds / 60.0)));
  if (minutes >= 10) minutes = static_cast<long>(std::lround(minutes / 5.0)) * 5;
  if (minutes >= 60) return Format(Text(kHours), {std::to_string(minutes / 60), std::to_string(minutes % 60)});
  return Format(Text(kMinutes), {std::to_string(minutes)});
}

}  // namespace guitext
