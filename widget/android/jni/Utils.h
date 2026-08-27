/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_jni_Utils_h_
#define mozilla_jni_Utils_h_

#include <jni.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "nsIRunnable.h"

#if defined(DEBUG) || !defined(RELEASE_OR_BETA)
#  define MOZ_CHECK_JNI
#endif

#ifdef MOZ_CHECK_JNI
#  include "APKOpen.h"
#  include "MainThreadUtils.h"
#  include "mozilla/Assertions.h"
#endif

namespace mozilla {
namespace jni {

// How exception during a JNI call should be treated.
enum class ExceptionMode {
  // Abort on unhandled excepion (default).
  ABORT,
  // Ignore the exception and return to caller.
  IGNORE,
  // Catch any exception and return a nsresult.
  NSRESULT,
};

// Thread that a particular JNI call is allowed on.
enum class CallingThread {
  // Can be called from any thread (default).
  ANY,
  // Can be called from the Gecko thread.
  GECKO,
  // Can be called from the Java UI thread.
  UI,
};

// If and where a JNI call will be dispatched.
enum class DispatchTarget {
  // Call happens synchronously on the calling thread (default).
  CURRENT,
  // Call happens synchronously on the calling thread, but the call is
  // wrapped in a function object and is passed thru UsesNativeCallProxy.
  // Method must return void.
  PROXY,
  // Call is dispatched asynchronously on the Gecko thread to the XPCOM
  // (nsThread) event queue. Method must return void.
  GECKO,
  // Call is dispatched asynchronously on the Gecko thread to the widget
  // (nsAppShell) event queue. In most cases, events in the widget event
  // queue (aka native event queue) are favored over events in the XPCOM
  // event queue. Method must return void.
  GECKO_PRIORITY,
};

extern JavaVM* sJavaVM;
extern JNIEnv* sGeckoThreadEnv;

inline bool IsAvailable() { return !!sGeckoThreadEnv; }

inline JavaVM* GetVM() {
#ifdef MOZ_CHECK_JNI
  MOZ_ASSERT(sJavaVM);
#endif
  return sJavaVM;
}

inline JNIEnv* GetGeckoThreadEnv() {
#ifdef MOZ_CHECK_JNI
  MOZ_RELEASE_ASSERT(NS_IsMainThread(), "Must be on Gecko thread");
  MOZ_RELEASE_ASSERT(sGeckoThreadEnv, "Must have a JNIEnv");
#endif
  return sGeckoThreadEnv;
}

void SetGeckoThreadEnv(JNIEnv* aEnv);

JNIEnv* GetEnvForThread();

// bionic declares gettid(); glibc only does from 2.30 and the build sysroot is
// older, so go through the syscall.
inline pid_t GetCurrentThreadId() {
#ifdef __BIONIC__
  return ::gettid();
#else
  return static_cast<pid_t>(::syscall(SYS_gettid));
#endif
}

#ifdef MOZ_CHECK_JNI
#  define MOZ_ASSERT_JNI_THREAD(thread)                            \
    do {                                                           \
      if ((thread) == mozilla::jni::CallingThread::GECKO) {        \
        MOZ_RELEASE_ASSERT(::NS_IsMainThread());                   \
      } else if ((thread) == mozilla::jni::CallingThread::UI) {    \
        const bool isOnUiThread =                                  \
            (GetUIThreadId() == mozilla::jni::GetCurrentThreadId());\
        MOZ_RELEASE_ASSERT(isOnUiThread);                          \
      }                                                            \
    } while (0)
#else
#  define MOZ_ASSERT_JNI_THREAD(thread) \
    do {                                \
    } while (0)
#endif

bool ThrowException(JNIEnv* aEnv, const char* aClass, const char* aMessage);

inline bool ThrowException(JNIEnv* aEnv, const char* aMessage) {
  return ThrowException(aEnv, "java/lang/Exception", aMessage);
}

inline bool ThrowException(const char* aClass, const char* aMessage) {
  return ThrowException(GetEnvForThread(), aClass, aMessage);
}

inline bool ThrowException(const char* aMessage) {
  return ThrowException(GetEnvForThread(), aMessage);
}

bool HandleUncaughtException(JNIEnv* aEnv);

bool ReportException(JNIEnv* aEnv, jthrowable aExc, jstring aStack);

#define MOZ_CATCH_JNI_EXCEPTION(env)                    \
  do {                                                  \
    if (mozilla::jni::HandleUncaughtException((env))) { \
      MOZ_CRASH("JNI exception");                       \
    }                                                   \
  } while (0)

uintptr_t GetNativeHandle(JNIEnv* env, jobject instance);

void SetNativeHandle(JNIEnv* env, jobject instance, uintptr_t handle);

jclass GetClassRef(JNIEnv* aEnv, const char* aClassName);

// The wrappers libxul calls through are generated from this tree's GeckoView.
// Under ATL libxul is loaded by whatever GeckoView the APK ships, which can be
// older, and a class or member that is not there is fatal: GetClassRef
// MOZ_CRASHes and Method<>::BeginAccess aborts on the lookup. These two ask
// first, so a call site can fall back instead.

// True if the Java side has this class. Leaves no pending exception.
bool HasClass(const char* aClassName);

// True if the Java side declares the method this traits class was generated
// from. Leaves no pending exception.
template <class Traits>
bool HasMember() {
  const auto& ctx = typename Traits::Owner::Context();
  JNIEnv* const env = ctx.Env();
  const jmethodID id =
      Traits::isStatic ? env->GetStaticMethodID(ctx.ClassRef(), Traits::name,
                                                Traits::signature)
                       : env->GetMethodID(ctx.ClassRef(), Traits::name,
                                          Traits::signature);
  if (!id) {
    env->ExceptionClear();
  }
  return !!id;
}

void DispatchToGeckoPriorityQueue(already_AddRefed<nsIRunnable> aCall);

int GetAPIVersion();

pid_t GetUIThreadId();

bool IsOOMException(JNIEnv* aEnv);

}  // namespace jni
}  // namespace mozilla

#endif  // mozilla_jni_Utils_h_
