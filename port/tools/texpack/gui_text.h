// The window version's texts (gui.cpp), in English only (decision of the project owner, 14.11.2026: "die windows app
// braucht nur englisch sein"). Texts the Android packer app has too are the same words. Placeholders are %1 ... %9, %%
// is a percent sign; texpack_tests checks the table.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

namespace guitext {

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
const char *Text(Id id);
// the id of each row of the table (the tests check the order)
Id RowId(int row);
// %1 ... %9 replaced by args, %% by %; "1 files" / "1 images" become "1 file" / "1 image"
std::string Format(const std::string &pattern, std::initializer_list<std::string> args);
// 5916 -> "5,916"
std::string Count(std::uint64_t v);
// a size as the core's report writes it (1000-based): "716 MB", "2.34 GB", "11.9 GB"
std::string Bytes(std::uint64_t b);
// "45 s", "4 min 22 s", "1 h 5 min"
std::string Duration(double seconds);
// about how long: minutes, from 10 minutes on in steps of 5 (as the Android app), seconds below 1 minute in steps of 10
std::string About(double seconds);

}  // namespace guitext
