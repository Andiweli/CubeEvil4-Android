// RvzImage.java: the RVZ reader (disc_rvz.cpp) in a library of its own (libre4rvz.so), so the
// import does not load the game's (libre4decomp.so loads the core and reserves its memory).
#include <jni.h>

#include <string>

#include "disc_rvz.h"

namespace {

std::string Utf(JNIEnv *env, jstring s) {
  const char *c = env->GetStringUTFChars(s, nullptr);
  std::string out = c ? c : "";
  if (c)
    env->ReleaseStringUTFChars(s, c);
  return out;
}

} // namespace

// {disc size, file size} into sizes; null when it can be unpacked, else the reason.
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_disc_RvzImage_check(JNIEnv *env, jclass, jstring path,
                                                                               jlongArray sizes) {
  re4::RvzInfo info;
  std::string error;
  if (!re4::RvzInfoOf(Utf(env, path).c_str(), &info, &error))
    return env->NewStringUTF(error.c_str());
  const jlong values[2] = {static_cast<jlong>(info.discSize), static_cast<jlong>(info.fileSize)};
  env->SetLongArrayRegion(sizes, 0, 2, values);
  return nullptr;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_disc_RvzImage_unpack(JNIEnv *env, jclass, jstring in,
                                                                                jstring out, jobject progress) {
  jclass type = env->GetObjectClass(progress);
  jmethodID step = env->GetMethodID(type, "step", "(JJ)Z");
  std::string error;
  const bool ok = re4::RvzToIso(
      Utf(env, in).c_str(), Utf(env, out).c_str(),
      [&](std::uint64_t done, std::uint64_t total) {
        const jboolean go = env->CallBooleanMethod(progress, step, static_cast<jlong>(done), static_cast<jlong>(total));
        if (env->ExceptionCheck()) {
          env->ExceptionClear();
          return false;
        }
        return go == JNI_TRUE;
      },
      &error);
  return ok ? nullptr : env->NewStringUTF(error.c_str());
}
