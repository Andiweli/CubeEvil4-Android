// The Texture Packer app's native side (PackerNative.java): a pack from a ZIP (a file descriptor) or a folder (Java opens
// each file through the storage access framework) prepared, estimated and converted with the PC tool's core
// (port/tools/texpack/packer.h) into a file descriptor.
#include <jni.h>

#include <android/log.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cerrno>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "packer.h"
#include "zip.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "TexPacker", __VA_ARGS__)

namespace {

JavaVM *gVm = nullptr;

// a JNIEnv for this thread: the pool's threads are attached once and detached when they end
struct Attachment {
  JNIEnv *env = nullptr;
  bool attached = false;
  ~Attachment() {
    if (attached) gVm->DetachCurrentThread();
  }
};

JNIEnv *Env() {
  thread_local Attachment a;
  if (a.env) return a.env;
  JNIEnv *env = nullptr;
  if (gVm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
    a.env = env;
    return env;
  }
  if (gVm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
  a.env = env;
  a.attached = true;
  return env;
}

bool ReadFd(int fd, std::uint64_t offset, void *data, std::size_t n) {
  auto *p = static_cast<std::uint8_t *>(data);
  while (n) {
    const ssize_t got = pread64(fd, p, n, static_cast<off64_t>(offset));
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) return false;
    p += got;
    n -= static_cast<std::size_t>(got);
    offset += static_cast<std::uint64_t>(got);
  }
  return true;
}

class FdAccess : public zipr::RandomAccess {
 public:
  explicit FdAccess(int fd) : fd_(fd) {
    struct stat64 st;
    size_ = fstat64(fd, &st) == 0 ? static_cast<std::uint64_t>(st.st_size) : 0;
  }
  ~FdAccess() override { close(fd_); }
  bool ReadAt(std::uint64_t offset, void *data, std::size_t n) override { return ReadFd(fd_, offset, data, n); }
  std::uint64_t Size() override { return size_; }

 private:
  int fd_;
  std::uint64_t size_ = 0;
};

// a file of a folder the player chose: Java opens it (FolderOpener.open), we read and close it
class FolderReader : public pack::Reader {
 public:
  FolderReader(jobject opener, jmethodID open, int index) : opener_(opener), open_(open), index_(index) {}
  bool ReadAll(std::vector<std::uint8_t> &out, std::string &error) override {
    const int fd = Open();
    if (fd < 0) {
      error = "cannot open the file";
      return false;
    }
    struct stat64 st;
    const bool ok = fstat64(fd, &st) == 0 && st.st_size >= 0 && st.st_size < (1ll << 31);
    if (ok) out.resize(static_cast<std::size_t>(st.st_size));
    const bool read = ok && ReadFd(fd, 0, out.data(), out.size());
    close(fd);
    if (!read) error = "cannot read the file";
    return read;
  }
  // the image's header is asked for several times (sizes, memory, the conversion): kept after the first time
  bool ReadHead(std::size_t n, std::vector<std::uint8_t> &out) override {
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (headRead_ && (n <= head_.size() || headWhole_)) {
        out.assign(head_.begin(), head_.begin() + static_cast<std::ptrdiff_t>(std::min(n, head_.size())));
        return true;
      }
    }
    const int fd = Open();
    if (fd < 0) return false;
    struct stat64 st;
    bool ok = fstat64(fd, &st) == 0;
    if (ok) {
      out.resize(static_cast<std::size_t>(std::min<std::uint64_t>(n, static_cast<std::uint64_t>(st.st_size))));
      ok = ReadFd(fd, 0, out.data(), out.size());
    }
    close(fd);
    if (ok && n <= 4096) {
      std::lock_guard<std::mutex> lock(mu_);
      head_ = out;
      headWhole_ = out.size() < n;
      headRead_ = true;
    }
    return ok;
  }

 private:
  int Open() {
    JNIEnv *env = Env();
    if (!env) return -1;
    const int fd = env->CallIntMethod(opener_, open_, index_);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return -1;
    }
    return fd;
  }
  jobject opener_;  // a global reference the session holds
  jmethodID open_;
  int index_;
  std::mutex mu_;
  std::vector<std::uint8_t> head_;
  bool headRead_ = false, headWhole_ = false;
};

class FdSink : public pack::Sink {
 public:
  explicit FdSink(int fd) : fd_(fd) {}
  bool Write(const void *data, std::size_t n) override {
    const auto *p = static_cast<const std::uint8_t *>(data);
    while (n) {
      const ssize_t put = write(fd_, p, n);
      if (put < 0 && errno == EINTR) continue;
      if (put <= 0) return false;
      p += put;
      n -= static_cast<std::size_t>(put);
    }
    return true;
  }
  bool WriteAt(std::uint64_t offset, const void *data, std::size_t n) override {
    const auto *p = static_cast<const std::uint8_t *>(data);
    while (n) {
      const ssize_t put = pwrite64(fd_, p, n, static_cast<off64_t>(offset));
      if (put < 0 && errno == EINTR) continue;
      if (put <= 0) return false;
      p += put;
      n -= static_cast<std::size_t>(put);
      offset += static_cast<std::uint64_t>(put);
    }
    return true;
  }
  std::uint64_t Free() override {
    struct statvfs64 st;
    return fstatvfs64(fd_, &st) == 0 ? static_cast<std::uint64_t>(st.f_bavail) * st.f_frsize : ~0ull;
  }

 private:
  int fd_;
};

struct Session {
  pack::Pack pack;
  jobject opener = nullptr;  // global reference (folders)
  std::atomic<bool> cancel{false};
  std::string report;
  ~Session() {
    if (opener) {
      if (JNIEnv *env = Env()) env->DeleteGlobalRef(opener);
    }
  }
};

std::string gLastError;

// A Java string as UTF-8 (proper UTF-8, also for characters outside the BMP; GetStringUTFChars gives modified UTF-8)
std::string Str(JNIEnv *env, jstring s) {
  if (!s) return std::string();
  const jsize n = env->GetStringLength(s);
  std::vector<jchar> u(static_cast<std::size_t>(n));
  if (n) env->GetStringRegion(s, 0, n, u.data());
  std::string out;
  out.reserve(u.size());
  for (std::size_t i = 0; i < u.size(); i++) {
    unsigned cp = u[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;
    }
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  return out;
}

// A Java string from UTF-8 text. Not NewStringUTF: names from a ZIP need not be valid UTF-8 (old archives keep them in
// the PC's code page) or can hold 4-byte characters, which NewStringUTF does not take (CheckJNI ends the app); a
// broken sequence becomes U+FFFD.
jstring JStr(JNIEnv *env, const std::string &s) {
  std::vector<jchar> out;
  out.reserve(s.size());
  const auto *p = reinterpret_cast<const unsigned char *>(s.data());
  const std::size_t n = s.size();
  for (std::size_t i = 0; i < n;) {
    const unsigned c = p[i];
    unsigned cp = 0xFFFD, len = 1;
    if (c < 0x80) {
      cp = c;
    } else if (c >= 0xC2 && c <= 0xF4) {
      const unsigned need = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
      bool ok = true;
      unsigned v = c & (0x3F >> need);
      for (unsigned k = 1; ok && k <= need; k++) {
        if (i + k >= n || (p[i + k] & 0xC0) != 0x80) ok = false;
        else v = (v << 6) | (p[i + k] & 0x3F);
      }
      const unsigned min = need == 1 ? 0x80 : need == 2 ? 0x800 : 0x10000;
      if (ok && v >= min && v <= 0x10FFFF && !(v >= 0xD800 && v <= 0xDFFF)) {
        cp = v;
        len = need + 1;
      }
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<jchar>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<jchar>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<jchar>(cp));
    }
    i += len;
  }
  return env->NewString(out.data(), static_cast<jsize>(out.size()));
}

Session *S(jlong h) { return reinterpret_cast<Session *>(h); }

void ApplyChoices(JNIEnv *env, Session *s, jintArray choices) {
  const jsize n = choices ? env->GetArrayLength(choices) : 0;
  std::vector<jint> c(static_cast<std::size_t>(n));
  if (n) env->GetIntArrayRegion(choices, 0, n, c.data());
  for (std::size_t g = 0; g < s->pack.groups.size(); g++) {
    const int v = g < c.size() ? c[g] : -1;
    s->pack.groups[g].chosen = v >= 0 && v < static_cast<int>(s->pack.groups[g].variants.size()) ? v : -1;
  }
}

}  // namespace

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
  gVm = vm;
  return JNI_VERSION_1_6;
}

#define JNI(name) extern "C" JNIEXPORT
#define FN(name) Java_com_ast_cubeevil4_texturepacker_PackerNative_##name

// a ZIP archive (fd: detached, closed by the session); 0 and lastError() when it cannot be read
JNI() jlong JNICALL FN(openZip)(JNIEnv *env, jclass, jint fd, jstring name) {
  auto access = std::make_shared<FdAccess>(fd);
  std::vector<pack::InputFile> files;
  std::string packName, error;
  if (!pack::ListZip(access, Str(env, name), files, packName, error)) {
    gLastError = error;
    return 0;
  }
  auto *s = new Session();
  pack::Prepare(std::move(files), packName, s->pack);
  LOG("ZIP %s: %zu images", packName.c_str(), s->pack.byKey.size());
  return reinterpret_cast<jlong>(s);
}

// a folder: its files' paths from the folder, their sizes, and the object that opens file i (int open(int))
JNI() jlong JNICALL FN(openFolder)(JNIEnv *env, jclass, jstring name, jobjectArray rels, jlongArray sizes, jobject opener) {
  auto *s = new Session();
  s->opener = env->NewGlobalRef(opener);
  jmethodID open = env->GetMethodID(env->GetObjectClass(opener), "open", "(I)I");
  const jsize n = env->GetArrayLength(rels);
  std::vector<jlong> sz(static_cast<std::size_t>(n));
  if (n) env->GetLongArrayRegion(sizes, 0, n, sz.data());
  std::vector<pack::InputFile> files;
  for (jsize i = 0; i < n; i++) {
    auto rel = static_cast<jstring>(env->GetObjectArrayElement(rels, i));
    pack::InputFile f;
    f.rel = Str(env, rel);
    env->DeleteLocalRef(rel);
    f.bytes = static_cast<std::uint64_t>(sz[static_cast<std::size_t>(i)]);
    f.reader = std::make_shared<FolderReader>(s->opener, open, static_cast<int>(i));
    files.push_back(std::move(f));
  }
  pack::Prepare(std::move(files), Str(env, name), s->pack);
  LOG("folder %s: %zu images", s->pack.name.c_str(), s->pack.byKey.size());
  return reinterpret_cast<jlong>(s);
}

JNI() jstring JNICALL FN(lastError)(JNIEnv *env, jclass) { return JStr(env, gLastError); }

// what the pack holds, one line each: "name\t<name>", "images\t<n>", "bytes\t<n>", "skipped\t<n>", and per group
// "group\t<name>\t<1 add-on>" followed by its "variant\t<name>\t<1 the same as the main folder>"
JNI() jstring JNICALL FN(summary)(JNIEnv *env, jclass, jlong h) {
  const pack::Pack &p = S(h)->pack;
  std::string out = "name\t" + p.name + "\nimages\t" + std::to_string(p.byKey.size()) + "\nbytes\t" +
                    std::to_string(p.bytes) + "\nskipped\t" + std::to_string(p.skipped.size() + p.duplicates.size()) + "\n";
  for (const pack::Group &g : p.groups) {
    out += "group\t" + g.name + "\t" + (g.addOn ? "1" : "0") + "\n";
    for (const pack::Variant &v : g.variants) out += "variant\t" + v.name + "\t" + (v.sameAsMain ? "1" : "0") + "\n";
  }
  return JStr(env, out);
}

// the pack file's size at most for 256, 512 and 1024 with these choices (variant per group, -1 as in the main folder)
JNI() jlongArray JNICALL FN(estimate)(JNIEnv *env, jclass, jlong h, jintArray choices) {
  Session *s = S(h);
  ApplyChoices(env, s, choices);
  pack::Settings settings;
  std::string text;
  const std::vector<const pack::Source *> todo = pack::Todo(s->pack, settings, text);
  const std::vector<std::uint64_t> e = pack::EstimateSizes(todo, {256, 512, 1024});
  jlongArray out = env->NewLongArray(3);
  const jlong v[3] = {static_cast<jlong>(e[0]), static_cast<jlong>(e[1]), static_cast<jlong>(e[2])};
  env->SetLongArrayRegion(out, 0, 3, v);
  return out;
}

// converts into fd (opened "rw" by Java, not closed here); progress.progress(done, total, written) on this thread.
// "" when it worked, else why not ("cancelled" after cancel()); report() then has the text for the .txt
JNI() jstring JNICALL FN(convert)(JNIEnv *env, jclass, jlong h, jint size, jintArray choices, jint fd, jstring date,
                                  jstring outName, jlong memoryBudget, jint threads, jobject progress) {
  Session *s = S(h);
  s->cancel = false;
  ApplyChoices(env, s, choices);
  pack::Settings settings;
  settings.size = static_cast<unsigned>(size);
  settings.date = Str(env, date);
  settings.outName = Str(env, outName);
  settings.memoryBudget = static_cast<std::uint64_t>(memoryBudget);
  settings.threads = threads > 0 ? static_cast<unsigned>(threads) : 0;
  std::string choiceText;
  const std::vector<const pack::Source *> todo = pack::Todo(s->pack, settings, choiceText);
  jmethodID call = env->GetMethodID(env->GetObjectClass(progress), "progress", "(IIJ)V");
  FdSink sink(fd);
  const pack::Outcome result = pack::Convert(s->pack, todo, choiceText, settings, sink, [&](const pack::Progress &p) {
    env->CallVoidMethod(progress, call, static_cast<jint>(p.done), static_cast<jint>(p.total), static_cast<jlong>(p.written));
    if (env->ExceptionCheck()) env->ExceptionClear();
  }, &s->cancel);
  s->report = result.report;
  LOG("convert %u: %s, %zu images, %llu bytes, %.0f s", settings.size, result.ok ? "done" : result.error.c_str(),
      result.entries, static_cast<unsigned long long>(result.fileBytes), result.seconds);
  return JStr(env, result.ok ? std::string() : result.error);
}

JNI() void JNICALL FN(cancel)(JNIEnv *, jclass, jlong h) { S(h)->cancel = true; }

JNI() jstring JNICALL FN(report)(JNIEnv *env, jclass, jlong h) { return JStr(env, S(h)->report); }

JNI() void JNICALL FN(close)(JNIEnv *, jclass, jlong h) { delete S(h); }
