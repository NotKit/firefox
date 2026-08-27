/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Hal.h"
#include "mozilla/java/GeckoSensorWrappers.h"
#include "mozilla/jni/Utils.h"

using namespace mozilla::hal;

namespace mozilla::hal_impl {

// GeckoView 153, which ATL runs this libxul against, has no GeckoSensor class,
// and looking it up is fatal. Report no sensors instead, as hal's fallback does.
static bool HasGeckoSensor() {
  return mozilla::jni::HasClass(mozilla::java::GeckoSensor::name);
}

void EnableSensorNotifications(SensorType aSensor) {
  if (!HasGeckoSensor()) {
    return;
  }
  java::GeckoSensor::EnableSensor(aSensor);
}

void DisableSensorNotifications(SensorType aSensor) {
  if (!HasGeckoSensor()) {
    return;
  }
  java::GeckoSensor::DisableSensor(aSensor);
}

}  // namespace mozilla::hal_impl
