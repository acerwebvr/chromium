// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_ACERXR_DEVICE_PROVIDER_H
#define DEVICE_VR_ACERXR_DEVICE_PROVIDER_H

#include <memory>

#include "base/callback.h"
#include "base/macros.h"
#include "device/vr/vr_device_provider.h"
#include "device/vr/vr_export.h"

namespace device {

class AcerXRDevice;
class AcerXRTestHook;

class DEVICE_VR_EXPORT AcerXRDeviceProvider : public VRDeviceProvider {
 public:
  AcerXRDeviceProvider();
  ~AcerXRDeviceProvider() override;

  void Initialize(
      base::RepeatingCallback<void(device::mojom::XRDeviceId,
                                   mojom::VRDisplayInfoPtr,
                                   mojom::XRRuntimePtr)> add_device_callback,
      base::RepeatingCallback<void(device::mojom::XRDeviceId)>
          remove_device_callback,
      base::OnceClosure initialization_complete) override;

  bool Initialized() override;

  static void RecordRuntimeAvailability();

  static void SetTestHook(AcerXRTestHook*);

 private:
  void CreateDevice();

  std::unique_ptr<AcerXRDevice> device_;
  bool initialized_ = false;

  DISALLOW_COPY_AND_ASSIGN(AcerXRDeviceProvider);
};

}  // namespace device

#endif  // DEVICE_VR_OPENVR_DEVICE_PROVIDER_H
