// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/acerxr/acerxr_api_wrapper.h"

#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "device/vr/acerxr/test/test_hook.h"

namespace device {

AcerXRWrapper::AcerXRWrapper(bool for_rendering) {
  initialized_ = Initialize(for_rendering);
}

AcerXRWrapper::~AcerXRWrapper() {
  if (initialized_)
    Uninitialize();
}

vr::IVRCompositor* AcerXRWrapper::GetCompositor() {
  // DCHECK(current_task_runner_->BelongsToCurrentThread());
  compositor_ = nullptr;
  return compositor_;
}

vr::IVRSystem* AcerXRWrapper::GetSystem() {
  // DCHECK(current_task_runner_->BelongsToCurrentThread());
  system_ = nullptr;
  return system_;
}

void AcerXRWrapper::SetTestHook(AcerXRTestHook* hook) {
  // This may be called from any thread - tests are responsible for
  // maintaining thread safety, typically by not changing the test hook
  // while presenting.
  test_hook_ = hook;
  if (test_hook_registration_) {
    test_hook_registration_->SetTestHook(test_hook_);
  }
}

bool AcerXRWrapper::Initialize(bool for_rendering) {
  DCHECK(!any_initialized_);
  any_initialized_ = true;

  // device can only be used on this thread once initailized
  system_ = nullptr;

  // current_task_runner_ = base::ThreadTaskRunnerHandle::Get();

  if (for_rendering) {
    // compositor_ = vr::VRCompositor();
  }

  if (test_hook_) {
    // Allow our mock implementation of OpenVR to be controlled by tests.
    // Note that SetTestHook must be called before CreateDevice, or
    // test_hook_registration_s will remain null.  This is a good pattern for
    // tests anyway, since the alternative is we start mocking part-way through
    // using the device, and end up with race conditions for when we started
    // controlling things.
    vr::EVRInitError eError;
    test_hook_registration_ = reinterpret_cast<TestHookRegistration*>(
        vr::VR_GetGenericInterface(kChromeAcerXRTestHookAPI, &eError));
    if (test_hook_registration_) {
      test_hook_registration_->SetTestHook(test_hook_);
    }
  }

  return true;
}

void AcerXRWrapper::Uninitialize() {
  DCHECK(initialized_);
  initialized_ = false;
  system_ = nullptr;
  compositor_ = nullptr;
  test_hook_registration_ = nullptr;
  current_task_runner_ = nullptr;
  vr::VR_Shutdown();

  any_initialized_ = false;
}

AcerXRTestHook* AcerXRWrapper::test_hook_ = nullptr;
bool AcerXRWrapper::any_initialized_ = false;
TestHookRegistration* AcerXRWrapper::test_hook_registration_ = nullptr;

std::string GetAcerXRString(vr::IVRSystem* vr_system,
                            vr::TrackedDeviceProperty prop) {
  std::string out;

  vr::TrackedPropertyError error = vr::TrackedProp_Success;
  char acerxr_string[vr::k_unMaxPropertyStringSize];
  vr_system->GetStringTrackedDeviceProperty(
      vr::k_unTrackedDeviceIndex_Hmd, prop, acerxr_string,
      vr::k_unMaxPropertyStringSize, &error);

  if (error == vr::TrackedProp_Success)
    out = acerxr_string;

  return out;
}

}  // namespace device