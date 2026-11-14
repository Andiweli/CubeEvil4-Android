// The window version's texts (gui.cpp): English and the ten other languages of the Android packer app
// (port/android-decomp/packer/src/main/res/values-*/strings_packer.xml), chosen by the Windows display language, English
// as the fall-back (as the Android app and CubeEvil4's in-game menu). Texts the Android app has too are taken word for
// word from it. Placeholders are %1 ... %9 (any order in a translation), %% is a percent sign. texpack_tests checks that
// every language has every text with the same placeholders.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

namespace guitext {

enum Lang { kEn, kDe, kFr, kEs, kIt, kPt, kRu, kAr, kHi, kId, kZh, kLangCount };

enum Id {
  kVersion, kIntro, kLegal, kDropTitle, kDropSub, kDropRelease, kChooseZip, kChooseFolder, kReading, kReadingFiles,
  kReadingSizes, kNotFound, kZipFailed, kNoImages, kOneAtATime, kNotZip, kUnpackFirst, kImagesSize, kKindZip,
  kKindFolder, kLeftOut, kOtherPack, kChoices, kAsMain, kSameAsMain, kAdd, kSize, kSize256, kSize512, kSize1024,
  kNeverSmaller, kEstimate, kEstimating, kSeconds, kMinutes, kMinSec, kHours, kStart, kSaveTitle, kFileType, kNoSpace,
  kChooseOther, kSmallerFits, kInsidePack, kCannotWrite, kConverting, kStepPrepare, kStepPrepareSub, kStepConvert,
  kStepConvertSub, kStepFinish, kStepFinishSub, kStatConverted, kStatWritten, kStatSkipped, kStatRefused, kOfImages,
  kNow, kElapsed, kTimeLeft, kCancel, kCancelling, kCancelAsk, kCancelAskText, kStop, kKeepGoing, kDone, kSaved,
  kInFolder, kConvertedIn, kKinds, kSkippedDone, kRefusedDone, kMore, kNext, kOpenFolder, kShowReport, kClose, kBack,
  kFailed, kReason, kNoFileLeft, kCancelled, kDiskFull, kNoImageConverted, kWriteFailed, kConsoleHint, kPercent, kMB,
  kGB,
  kIdCount
};

// the text in UTF-8
const char *Text(Lang lang, Id id);
// "de", "de-AT", "in" (Android's old code for Indonesian), "zh-Hans-CN" ...; anything else is English
Lang FromCode(const std::string &code);
const char *Code(Lang lang);
bool RightToLeft(Lang lang);
// %1 ... %9 replaced by args, %% by %
std::string Format(const std::string &pattern, std::initializer_list<std::string> args);
// 5916 -> "5,916" / "5.916" / "5 916" as the language writes it
std::string Count(Lang lang, std::uint64_t v);
// a size as the core's report writes it (1000-based: "716 MB", "2.34 GB"), with the language's decimal sign and units
std::string Bytes(Lang lang, std::uint64_t b);
// "45 s", "4 min 22 s", "1 h 5 min"
std::string Duration(Lang lang, double seconds);
// about how long: minutes, from 10 minutes on in steps of 5 (as the Android app), seconds below 1 minute in steps of 10
std::string About(Lang lang, double seconds);

}  // namespace guitext
