// The language pack import for the app (LanguagePacks.java): a library of its own, like the RVZ reader, so the import
// does not load the game. The app walks the player's folder or ZIP and hands each file over (as an open file); the
// checks and the copying are language_pack.cpp's.
#include "language_pack.h"

#include <jni.h>
#include <unistd.h>

#include <memory>
#include <string>

namespace {

struct Session {
  re4lang::Importer importer;
  re4lang::Result result;
  std::vector<re4lang::ZipEntry> zip;
  int zipFd = -1;
  bool begun = false, finished = false;
  Session(std::string game, std::string dest) : importer(std::move(game), std::move(dest)) {}
};

std::string text(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string out = c ? c : "";
  if (c) env->ReleaseStringUTFChars(s, c);
  return out;
}

jstring answer(JNIEnv* env, const re4lang::Result& r, bool ok) {
  return env->NewStringUTF(ok ? "" : re4lang::Describe(r).c_str());
}

// The whole file behind `fd` (the app keeps it open and closes it).
bool readAll(int fd, re4lang::Bytes& out) {
  out.clear();
  uint8_t buf[1 << 16];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) return false;
    if (n == 0) return true;
    if (out.size() + size_t(n) > (512u << 20)) return false;
    out.insert(out.end(), buf, buf + n);
  }
}

re4lang::ZipRead reader(int fd) {
  return [fd](uint64_t at, void* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
      ssize_t k = pread(fd, static_cast<char*>(buf) + got, n - got, off_t(at + got));
      if (k <= 0) return false;
      got += size_t(k);
    }
    return true;
  };
}

Session* session(jlong h) { return reinterpret_cast<Session*>(h); }

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeOpen(JNIEnv* env, jclass, jstring game, jstring dest) {
  return reinterpret_cast<jlong>(new Session(text(env, game), text(env, dest)));
}

// "" when the discs fit (European, imported) and the work folder is made, else the refusal (Describe)
JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeBegin(JNIEnv* env, jclass, jlong h) {
  Session* s = session(h);
  s->begun = s->importer.begin(s->result);
  return answer(env, s->result, s->begun);
}

// 0 skip (not a game file), 1 read it (nativeAdd), 2 stop (nativeResult says why)
JNIEXPORT jint JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeWant(JNIEnv* env, jclass, jlong h, jstring path) {
  Session* s = session(h);
  return s->importer.want(text(env, path), s->result);
}

JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeAdd(JNIEnv* env, jclass, jlong h, jstring path,
                                                                          jint fd) {
  Session* s = session(h);
  std::string p = text(env, path);
  re4lang::Bytes data;
  if (!readAll(fd, data)) {
    s->result.reason = re4lang::kReadFailed;
    s->result.file = p;
    s->result.detail = "cannot read " + p;
    return answer(env, s->result, false);
  }
  return answer(env, s->result, s->importer.add(p, data, s->result));
}

// The ZIP's directory; its entries then by number (nativeZipCount/Name/Size, nativeZipAdd). The fd stays the app's.
JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeZipList(JNIEnv* env, jclass, jlong h, jint fd,
                                                                              jlong size) {
  Session* s = session(h);
  s->zipFd = fd;
  std::string why;
  bool ok = re4lang::ZipList(reader(fd), uint64_t(size), s->zip, why);
  if (!ok) {
    s->result.reason = re4lang::kBadZip;
    s->result.detail = why;
  }
  return answer(env, s->result, ok);
}

JNIEXPORT jint JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeZipCount(JNIEnv*, jclass, jlong h) {
  return jint(session(h)->zip.size());
}

JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeZipName(JNIEnv* env, jclass, jlong h, jint i) {
  Session* s = session(h);
  return env->NewStringUTF(i >= 0 && size_t(i) < s->zip.size() ? s->zip[size_t(i)].name.c_str() : "");
}

JNIEXPORT jlong JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeZipSize(JNIEnv*, jclass, jlong h, jint i) {
  Session* s = session(h);
  return i >= 0 && size_t(i) < s->zip.size() ? jlong(s->zip[size_t(i)].size) : 0;
}

JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeZipAdd(JNIEnv* env, jclass, jlong h, jint i) {
  Session* s = session(h);
  if (i < 0 || size_t(i) >= s->zip.size() || s->zipFd < 0) {
    s->result.reason = re4lang::kBadZip;
    s->result.detail = "no such entry";
    return answer(env, s->result, false);
  }
  const re4lang::ZipEntry& e = s->zip[size_t(i)];
  if (e.size > (512u << 20)) {
    s->result.reason = re4lang::kReadFailed;
    s->result.file = e.name;
    s->result.detail = e.name + " is too large for a game file";
    return answer(env, s->result, false);
  }
  re4lang::Bytes data;
  std::string why;
  if (!re4lang::ZipExtract(reader(s->zipFd), e, data, why)) {
    s->result.reason = re4lang::kBadZip;
    s->result.file = e.name;
    s->result.detail = why;
    return answer(env, s->result, false);
  }
  return answer(env, s->result, s->importer.add(e.name, data, s->result));
}

// The pack in place: Describe with reason 0 and the counts; else the refusal (the work folder is gone).
JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeFinish(JNIEnv* env, jclass, jlong h, jstring name,
                                                                             jstring source) {
  Session* s = session(h);
  s->finished = s->importer.finish(text(env, name), text(env, source), s->result);
  return env->NewStringUTF(re4lang::Describe(s->result).c_str());
}

JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeResult(JNIEnv* env, jclass, jlong h) {
  return env->NewStringUTF(re4lang::Describe(session(h)->result).c_str());
}

// The end of an import: a pack that was not finished leaves nothing behind.
JNIEXPORT void JNICALL Java_com_ast_re4decomp_LanguagePacks_nativeClose(JNIEnv*, jclass, jlong h) {
  Session* s = session(h);
  if (s->begun && !s->finished) s->importer.abort();
  delete s;
}

}  // extern "C"
