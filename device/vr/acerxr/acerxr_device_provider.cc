// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/acerxr/acerxr_device_provider.h"

#include "base/metrics/histogram_macros.h"
#include "device/gamepad/gamepad_data_fetcher_manager.h"
#include "device/vr/isolated_gamepad_data_fetcher.h"
#include "device/vr/acerxr/acerxr_api_wrapper.h"
#include "device/vr/acerxr/acerxr_device.h"
#include "device/vr/acerxr/test/test_hook.h"
#include "third_party/openvr/src/headers/openvr.h"

namespace device {

void AcerXRDeviceProvider::RecordRuntimeAvailability() {
  XrRuntimeAvailable runtime = XrRuntimeAvailable::NONE;
  if (vr::VR_IsRuntimeInstalled())
    runtime = XrRuntimeAvailable::OPENVR;
  UMA_HISTOGRAM_ENUMERATION("XR.RuntimeAvailable", runtime,
                            XrRuntimeAvailable::COUNT);
}

AcerXRDeviceProvider::AcerXRDeviceProvider() = default;

AcerXRDeviceProvider::~AcerXRDeviceProvider() {
  device::GamepadDataFetcherManager::GetInstance()->RemoveSourceFactory(
      device::GAMEPAD_SOURCE_OPENVR);

  if (device_) {
    device_->Shutdown();
    device_ = nullptr;
  }

  AcerXRWrapper::SetTestHook(nullptr);
}

void AcerXRDeviceProvider::Initialize(
    base::RepeatingCallback<void(device::mojom::XRDeviceId,
                                 mojom::VRDisplayInfoPtr,
                                 mojom::XRRuntimePtr)> add_device_callback,
    base::RepeatingCallback<void(device::mojom::XRDeviceId)>
        remove_device_callback,
    base::OnceClosure initialization_complete) {
  CreateDevice();
  if (device_) {
    add_device_callback.Run(device_->GetId(), device_->GetVRDisplayInfo(),
                            device_->BindXRRuntimePtr());
  }
  initialized_ = true;
  std::move(initialization_complete).Run();
}

void AcerXRDeviceProvider::SetTestHook(AcerXRTestHook* test_hook) {
  AcerXRWrapper::SetTestHook(test_hook);
}

void AcerXRDeviceProvider::CreateDevice() {
//  if (!vr::VR_IsRuntimeInstalled() || !vr::VR_IsHmdPresent())
//    return;

  device_ = std::make_unique<AcerXRDevice>();
  if (device_->IsInitialized()) {
    GamepadDataFetcherManager::GetInstance()->AddFactory(
        new IsolatedGamepadDataFetcher::Factory(
            device::mojom::XRDeviceId::OPENVR_DEVICE_ID,
            device_->BindGamepadFactory()));
  } else {
    device_ = nullptr;
  }
}

bool AcerXRDeviceProvider::Initialized() {
  return initialized_;
}

}  // namespace device
