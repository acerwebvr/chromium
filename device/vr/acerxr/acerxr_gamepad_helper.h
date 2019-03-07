// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_ACERXR_ACERXR_GAMEPAD_HELPER_H_
#define DEVICE_VR_ACERXR_ACERXR_GAMEPAD_HELPER_H_

#include "device/vr/public/mojom/isolated_xr_service.mojom.h"
#include "third_party/openvr/src/headers/openvr.h"

#include "device/vr/acerxr/acerxr_render_loop.h"
namespace device {

class AcerXRGamepadHelper {
 public:
  static mojom::XRGamepadDataPtr GetGamepadData(std::list<ControllerData> controllerDataList);
};

}  // namespace device
#endif  // DEVICE_VR_OPENVR_OPENVR_GAMEPAD_HELPER_H_
