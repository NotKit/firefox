/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_FFVPX_CONFIG_H
#define MOZ_FFVPX_CONFIG_H

#if defined(MOZ_FFVPX_AUDIOONLY)
// __ANDROID__, not MOZ_WIDGET_ANDROID: an android-toolkit build against glibc
// has the toolkit but no bionic and no MediaCodec, and the config_android*
// headers set CONFIG_MEDIACODEC 1 while this build excludes the file that
// defines ff_hwcontext_type_mediacodec (libavutil/moz.build only adds
// hwcontext_mediacodec.c when the build is not audio-only).
#  if defined(MOZ_WIDGET_ANDROID) && defined(__ANDROID__) && defined(__arm__)
#    include "config_android32.h"
#  elif defined(MOZ_WIDGET_ANDROID) && defined(__ANDROID__) && defined(__aarch64__)
#    include "config_android64.h"
#  elif defined(MOZ_WIDGET_ANDROID) && defined(__ANDROID__) && defined(HAVE_64BIT_BUILD)
#    include "config_android_x86_64.h"
#  elif defined(__aarch64__)
#    include "config_unix_aarch64.h"
#  else
      // no assembly
#    include "config_generic.h"
#  endif
#else  // MOZ_FFVPX_AUDIOONLY
#  if defined(XP_WIN)
// Avoid conflicts with mozilla-config.h
#    if !defined(_MSC_VER)
#      undef HAVE_DIRENT_H
#      undef HAVE_UNISTD_H
#    endif
#    if defined(_ARM64_)
#      include "config_win64_aarch64.h"
#    else
#      if defined(HAVE_64BIT_BUILD)
#        include "config_win64.h"
#      else
#        include "config_win32.h"
#      endif
#    endif
// Adjust configure defines for GCC
#    if !defined(_MSC_VER)
#      if !defined(HAVE_64BIT_BUILD)
#        undef HAVE_MM_EMPTY
#        define HAVE_MM_EMPTY 0
#      endif
#      undef HAVE_LIBC_MSVCRT
#      define HAVE_LIBC_MSVCRT 0
#    endif
#  elif defined(XP_DARWIN)
#    if defined(__aarch64__)
#      include "config_darwin_aarch64.h"
#    else
#      include "config_darwin64.h"
#    endif
// Again __ANDROID__: aarch64 with the android toolkit but glibc has no
// MediaCodec, so take the unix config, which has CONFIG_MEDIACODEC 0.
#  elif defined(MOZ_WIDGET_ANDROID) && !defined(__ANDROID__) && \
      defined(__aarch64__)
#    include "config_unix_aarch64.h"
#  elif defined(MOZ_WIDGET_ANDROID) && defined(__aarch64__)
#    include "config_android64.h"
#  elif defined(MOZ_WIDGET_ANDROID) && defined(__arm__)
#    include "config_android32.h"
#  elif defined(MOZ_WIDGET_ANDROID) && defined(HAVE_64BIT_BUILD)
#    include "config_android_x86_64.h"
#  elif defined(XP_UNIX)
#    if defined(__aarch64__)
#      include "config_unix_aarch64.h"
#    elif defined(HAVE_64BIT_BUILD)
#      include "config_unix64.h"
#    else
#      include "config_unix32.h"
#    endif
#  endif
#endif  // else MOZ_FFVPX_AUDIOONLY
        //
#include "config_override.h"

#endif  // MOZ_FFVPX_CONFIG_H
