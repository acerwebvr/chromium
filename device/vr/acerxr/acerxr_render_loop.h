// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_VR_ACERXR_RENDER_LOOP_H
#define DEVICE_VR_ACERXR_RENDER_LOOP_H

#include "base/memory/scoped_refptr.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "device/vr/acerxr/Acer_Socket.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "device/vr/vr_device.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "third_party/openvr/src/headers/openvr.h"
#include "ui/gfx/geometry/rect_f.h"


#if defined(OS_WIN)
#include "device/vr/windows/d3d11_texture_helper.h"
#endif

namespace device {

class AcerXRWrapper;
struct OpenVRGamepadState;  //[Leo] need check

struct HMDSensorData {
  float rot_x = 0.0f;
  float rot_y = 0.0f;
  float rot_z = 0.0f;
  float rot_w = 0.0f;
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float pos_z = 0.0f;
};

#define CONTROLLER_COUNT 2

#define LEFT_CONTROLLER 0
#define RIGHT_CONTROLLER 1

#define CONTROLLER_NONE 0x00
#define CONTROLLER_GRASPED 0x01
#define CONTROLLER_MENU 0x02
#define CONTROLLER_SELECTED 0x04
#define CONTROLLER_THUMBSTICK 0x08
#define CONTROLLER_TOUCHPAD 0x10
#define CONTROLLER_MASK                                         \
  (CONTROLLER_GRASPED | CONTROLLER_MENU | CONTROLLER_SELECTED | \
   CONTROLLER_THUMBSTICK | CONTROLLER_TOUCHPAD)

struct ControllerData {
  int id = 0;
  int handedness = -1;

  HMDSensorData pose;

  float vel[6] = {0.0f};  // 0~2: Velocity, 3~5: angular velocity

  int buttonPressed = CONTROLLER_NONE;
  int buttonTouched = CONTROLLER_NONE;

  double selectPressedValue = 0.0;
  double Thumbstick[2] = {0.0};
  double Touchpad[2] = {0.0};
};

class AcerXRRenderLoop : public base::Thread,
                         mojom::XRPresentationProvider,
                         mojom::XRFrameDataProvider,
                         mojom::IsolatedXRGamepadProvider {
 public:
  using RequestSessionCallback =
      base::OnceCallback<void(bool result, mojom::XRSessionPtr)>;

  AcerXRRenderLoop();
  ~AcerXRRenderLoop() override;

  void RequestSession(base::OnceCallback<void()> on_presentation_ended,
                      mojom::XRRuntimeSessionOptionsPtr options,
                      RequestSessionCallback callback);
  void ExitPresent();
  base::WeakPtr<AcerXRRenderLoop> GetWeakPtr();

  // XRPresentationProvider overrides:
  void SubmitFrameMissing(int16_t frame_index, const gpu::SyncToken&) override;
  void SubmitFrame(int16_t frame_index,
                   const gpu::MailboxHolder& mailbox,
                   base::TimeDelta time_waited) override;
  void SubmitFrameDrawnIntoTexture(int16_t frame_index,
                                   const gpu::SyncToken&,
                                   base::TimeDelta time_waited) override;
  void SubmitFrameWithTextureHandle(int16_t frame_index,
                                    mojo::ScopedHandle texture_handle) override;
  void UpdateLayerBounds(int16_t frame_id,
                         const gfx::RectF& left_bounds,
                         const gfx::RectF& right_bounds,
                         const gfx::Size& source_size) override;

  void UpdateAcerXRUrl(mojom::AcerXRUrlInfoPtr acerxrurlinfo) override;

  void GetFrameData(
      XRFrameDataProvider::GetFrameDataCallback callback) override;

  void RequestGamepadProvider(mojom::IsolatedXRGamepadProviderRequest request);
  bool isDisableControllerList(std::string str);
  bool isModifyPosition(std::string str);
  bool isDisableControllerForMatterport();

  void RunAcerSocketThread();
  void RunWin_Y_MonitorThread();
  UINT Draw_Texture_Width = 1593 * 2;  // 2160; // 3490;// 2160;
  UINT Draw_Texture_Height = 1593;

  UINT SENSOR_FIFO_SIZE = 2;
  UINT SLEEP_TIME = 0;

  bool IsDisableController = false;
  bool IsModifyPosition = false;

  AcerSocket* socket = NULL;

 private:
  bool m_IsExit = false;
  bool m_IsMonitorExit = false;
  bool m_IsUWPLaunched = false;
  SOCKET m_ClientSocket;
  bool m_IsConnected;
  bool m_TargetHandleChanged = false;
  bool m_ScreenScaleChanged = false;
  float m_ScreenScaleValue = 5.0f;
  HANDLE m_TerminateThreadsEvent = nullptr;

  float rot_x = 0.0f;
  float rot_y = 0.0f;
  float rot_z = 0.0f;
  float rot_w = 0.1f;

  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float pos_z = 0.0f;

  long m_timePosData = 0;
  HANDLE ghMutex_sensor;
  std::list<HMDSensorData> g_sensorListPtr;

  HMDSensorData HMDSensorData_;

  HANDLE ghMutex_controller;
  std::list<ControllerData> g_controllerDataList;

  // base::Thread overrides:
  void Init() override;
  HRESULT LaunchUWP();
  //unsigned char* readBMP( const TCHAR* filename, unsigned int& rowBytes);
  void WMRUtil_SendKeysToSwitchFocus();
  bool WMRUtil_HasFocus();
  void Process6Dof(const char* rcvbuf);
  void ProcessController(const char* rcvbuf);
  void ProcessPredictTime(const char* rcvbuf);
  void CleanUp() override;

  void ClearPendingFrame();
  void UpdateControllerState();

  // IsolatedXRGamepadProvider
  void RequestUpdate(mojom::IsolatedXRGamepadProvider::RequestUpdateCallback
                         callback) override;

  mojom::VRPosePtr GetPose();
  std::vector<mojom::XRInputSourceStatePtr> GetInputState(
      vr::TrackedDevicePose_t* poses,
      uint32_t count);

  struct InputActiveState {
    bool active;
    bool primary_input_pressed;
    vr::ETrackedDeviceClass device_class;
    vr::ETrackedControllerRole controller_role;
  };

#if defined(OS_WIN)
  D3D11TextureHelper texture_helper_;
#endif

  base::OnceCallback<void()> delayed_get_frame_data_callback_;
  bool has_outstanding_frame_ = false;

  int16_t next_frame_id_ = 0;
  bool is_presenting_ = false;
  // InputActiveState input_active_states_[vr::k_unMaxTrackedDeviceCount];
  gfx::RectF left_bounds_;
  gfx::RectF right_bounds_;
  gfx::Size source_size_;
  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner_;
  mojom::XRPresentationClientPtr submit_client_;
  base::RepeatingCallback<void(OpenVRGamepadState)> update_gamepad_;
  base::OnceCallback<void()> on_presentation_ended_;
  mojom::IsolatedXRGamepadProvider::RequestUpdateCallback gamepad_callback_;
  std::unique_ptr<AcerXRWrapper> acerxr_;
  mojo::Binding<mojom::XRPresentationProvider> presentation_binding_;
  mojo::Binding<mojom::XRFrameDataProvider> frame_data_binding_;
  mojo::Binding<mojom::IsolatedXRGamepadProvider> gamepad_provider_;

  base::WeakPtrFactory<AcerXRRenderLoop> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AcerXRRenderLoop);
};

}  // namespace device

#endif  // DEVICE_VR_OPENVR_RENDER_LOOP_H
