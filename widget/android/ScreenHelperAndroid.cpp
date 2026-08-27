/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScreenHelperAndroid.h"

#include <mozilla/jni/Refs.h>
#include <mozilla/jni/Utils.h>

#include "AndroidRect.h"
#include "AndroidVsync.h"
#include "mozilla/java/GeckoAppShellWrappers.h"
#include "mozilla/java/ScreenManagerHelperNatives.h"
#include "mozilla/widget/ScreenManager.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

using namespace mozilla;
using namespace mozilla::widget;

static ScreenHelperAndroid* gHelper = nullptr;

class ScreenHelperAndroid::ScreenHelperSupport final
    : public java::ScreenManagerHelper::Natives<ScreenHelperSupport> {
 public:
  typedef java::ScreenManagerHelper::Natives<ScreenHelperSupport> Base;

  static void RefreshScreenInfo() { gHelper->Refresh(); }
};

static already_AddRefed<Screen> MakePrimaryScreen() {
  MOZ_ASSERT(XRE_IsParentProcess());
  java::sdk::Rect::LocalRef rect = java::GeckoAppShell::GetScreenSize();
  LayoutDeviceIntRect bounds = LayoutDeviceIntRect(
      rect->Left(), rect->Top(), rect->Width(), rect->Height());
  uint32_t depth = java::GeckoAppShell::GetScreenDepth();
  float density = java::GeckoAppShell::GetDensity();
  float dpi = java::GeckoAppShell::GetDpi();
  bool isHDR = java::GeckoAppShell::HasHDRScreen();
  auto orientation =
      hal::ScreenOrientation(java::GeckoAppShell::GetScreenOrientation());
  uint16_t angle = java::GeckoAppShell::GetScreenAngle();
  // Note that sdrContentBrightness can vary in real time in response to varying
  // ambient light level.
  // Both were added after GeckoView 153, which ATL runs this libxul against;
  // an absent method is fatal in the lookup, so ask first. 80.0f is what the
  // Java side itself returns when the display cannot say.
  float sdrContentBrightness = 80.0f;
  if (jni::HasMember<java::GeckoAppShell::GetSDRContentBrightness_t>()) {
    sdrContentBrightness = java::GeckoAppShell::GetSDRContentBrightness();
  }
  float hdrPeakBrightness = 80.0f;
  if (jni::HasMember<java::GeckoAppShell::GetHDRPeakBrightness_t>()) {
    hdrPeakBrightness = java::GeckoAppShell::GetHDRPeakBrightness();
  }
  float refreshRate = java::GeckoAppShell::GetScreenRefreshRate();
  return MakeAndAddRef<Screen>(
      bounds, bounds, depth, depth, refreshRate,
      DesktopToLayoutDeviceScale(density), CSSToLayoutDeviceScale(1.0f), dpi,
      Screen::IsPseudoDisplay::No, Screen::IsHDR(isHDR), sdrContentBrightness,
      hdrPeakBrightness, orientation, angle);
}

ScreenHelperAndroid::ScreenHelperAndroid() {
  MOZ_ASSERT(!gHelper);
  gHelper = this;

  ScreenHelperSupport::Base::Init();

  Refresh();
}

ScreenHelperAndroid::~ScreenHelperAndroid() { gHelper = nullptr; }

void ScreenHelperAndroid::Refresh() {
  AutoTArray<RefPtr<Screen>, 1> screens;
  screens.AppendElement(MakePrimaryScreen());
  ScreenManager::Refresh(std::move(screens));

  if (RefPtr<AndroidVsync> vsync = AndroidVsync::GetInstance()) {
    vsync->OnMaybeUpdateRefreshRate();
  }
}
