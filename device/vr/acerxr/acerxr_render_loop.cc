// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/acerxr/acerxr_render_loop.h"

#include <ShlObj.h>
#include <atlbase.h>
#include <process.h>
#include <chrono>
#include "base/metrics/histogram_functions.h"
#include "device/vr/acerxr/acerxr_api_wrapper.h"
#include "device/vr/acerxr/acerxr_gamepad_helper.h"
#include "device/vr/acerxr/acerxr_type_converters.h"
#include "ui/gfx/geometry/angle_conversions.h"
#include "ui/gfx/transform.h"
// [Leo] write file sample ++
#include <fstream>
#include <iostream>
// [Leo] write file sample ++

#if defined(OS_WIN)
#include "device/vr/windows/d3d11_texture_helper.h"
#endif

namespace device {
// std::ofstream Debug_file;

#define DISABLE_CONTROLLER_LIST_FILE "..\\settings\\WebVR.disablecontrollerlist"
#define MODIFY_POSITION_LIST_FILE "..\\settings\\WebVR.modifypositionlist"

unsigned int static WINAPI ProcessThread_Socket(void* p) {
  // AtomCopyFrameGenerator *pp = (AtomCopyFrameGenerator*) p;
  AcerXRRenderLoop* pAcerXRRenderLoop = static_cast<AcerXRRenderLoop*>(p);
  if (pAcerXRRenderLoop)
    pAcerXRRenderLoop->RunAcerSocketThread();
  // _endthreadex(0);
  return 0;
}

unsigned int static WINAPI ProcessThread_Win_Y_Monitor(void* p) {
  // AtomCopyFrameGenerator *pp = (AtomCopyFrameGenerator*) p;
  AcerXRRenderLoop* pAcerXRRenderLoop = static_cast<AcerXRRenderLoop*>(p);
  if (pAcerXRRenderLoop)
    pAcerXRRenderLoop->RunWin_Y_MonitorThread();
  // _endthreadex(0);
  return 0;
}

namespace {

// OpenVR reports the controllers pose of the controller's tip, while WebXR
// needs to report the pose of the controller's grip (centered on the user's
// palm.) This experimentally determined value is how far back along the Z axis
// in meters OpenVR's pose needs to be translated to align with WebXR's
// coordinate system.
const float kGripOffsetZMeters = 0.08f;

// WebXR reports a pointer pose separate from the grip pose, which represents a
// pointer ray emerging from the tip of the controller. OpenVR does not report
// anything like that, and most pointers are assumed to come straight from the
// controller's tip. For consistency with other WebXR backends we'll synthesize
// a pointer ray that's angled down slightly from the controller's handle,
// defined by this angle. Experimentally determined, should roughly point in the
// same direction as a user's outstretched index finger while holding a
// controller.
const float kPointerErgoAngleDegrees = -40.0f;
static wchar_t AUMID[128] =
    L"AcerIncorporated.AcerChromiumBrowserforWebVR_gj281b0z0r0br!App";
}  // namespace

void AcerXRRenderLoop::RunWin_Y_MonitorThread() {
  m_IsMonitorExit = false;
  while (!m_IsMonitorExit) {
    if (m_IsUWPLaunched) {
      if (WMRUtil_HasFocus()) {
        WMRUtil_SendKeysToSwitchFocus();
      }
      Sleep(500);
    }
    Sleep(500);
  }
}

void AcerXRRenderLoop::RunAcerSocketThread() {
  fd_set rd, wr;
  timeval timeout;
  // AcerSocket* socket = NULL;

  OutputDebugStringW(L"Bind server socket\n");

  while (1) {
    socket = new AcerSocket(true, "127.0.0.1", 54238, AF_INET, SOCK_STREAM,
                            IPPROTO_TCP);
    if (socket->socket_server_Init() != true) {
      delete socket;
      socket = NULL;
    } else {
      break;
    }
    Sleep(100);
  }
  char buf[128];
  // int buf_count = 0;
  ZeroMemory(buf, 128);

  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  FD_ZERO(&rd);
  FD_ZERO(&wr);
  FD_SET(socket->get_client_socket(), &wr);
  FD_SET(socket->get_client_socket(), &rd);
  // while (m_IsConnected)
  bool IsAccept = false;
  // bool IsAlive = false;

  char recvBuf[128];
  m_IsExit = false;
  while (!m_IsExit) {
    while (!m_IsExit && socket->get_client_socket() == INVALID_SOCKET) {
      
      socket->socket_accept();
      Sleep(100);
      IsAccept = true;
      // m_IsConnected = false;
      m_ClientSocket = INVALID_SOCKET;
      m_IsConnected = false;
    }

    if (IsAccept) {
      OutputDebugStringW(L"Accept Client socket\n");
      // send init data to cleint after accept
      // m_TargetHandleChanged = true;

      IsAccept = false;
      m_IsConnected = true;
      
      m_ClientSocket = socket->get_client_socket();
      socket->GreateTimeOut();      
    }
    if ((texture_helper_.m_SyncTexture_Target != NULL &&
         m_TargetHandleChanged) ||
        (m_ScreenScaleChanged && m_ScreenScaleValue > 0.0f)) {
      
      sprintf_s(buf, "H%lld,%d,%d,%f,%f",
                (UINT64)texture_helper_.m_SyncTexture_Target,
                Draw_Texture_Width, Draw_Texture_Height, m_ScreenScaleValue,
                0.0f);
      socket->socket_send(buf, 128);
      m_TargetHandleChanged = false;
      m_ScreenScaleChanged = false;
    }

    int recvlen = 0;
    recvlen = socket->socket_receive(recvBuf, sizeof(recvBuf));
    if (recvlen > 0) {
      // check client alive
      std::string str = "Keep-Alive";
      
      if (str.compare(recvBuf) == 0) {
        printf("received Keep-Alive\n");
        
        socket->SetKeepAlive(true);
        sprintf_s(buf, "H%lld,%d,%d,%f,%f",
                  (UINT64)texture_helper_.m_SyncTexture_Target,
                  Draw_Texture_Width, Draw_Texture_Height, m_ScreenScaleValue,
                  0.0f);
        socket->socket_send(buf, 128);
      } else {
        char type;
        memcpy(&type, recvBuf, 1);
        
        switch (type) {
          case 'P':
            if (recvlen > 0) {
              Process6Dof(&recvBuf[1]);
            }
            break;
          case 'C':
            if (recvlen > 0) {
              ProcessController(&recvBuf[1]);
            }
            break;
          case 'T':
            if (recvlen > 0) {
              ProcessPredictTime(&recvBuf[1]);
            }
            break;
        }        
      }
    }
  }
  OutputDebugString(L"End thread\n");

  if (m_IsExit) {
    sprintf_s(buf, "exit");
    socket->socket_send(buf, 128);
    Sleep(100);
    socket->close_client_socket();
    socket->socket_close();
  }
  SetEvent(m_TerminateThreadsEvent);
  WSACleanup();
}
AcerXRRenderLoop::AcerXRRenderLoop()
    : base::Thread("AcerXRRenderLoop"),
      main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      presentation_binding_(this),
      frame_data_binding_(this),
      gamepad_provider_(this),
      weak_ptr_factory_(this) {
  DCHECK(main_thread_task_runner_);
  
  ghMutex_sensor = CreateMutex(NULL,   // default security attributes
                               FALSE,  // initially not owned
                               NULL);

  ghMutex_controller = CreateMutex(NULL,   // default security attributes
                                   FALSE,  // initially not owned
                                   NULL);  
}

AcerXRRenderLoop::~AcerXRRenderLoop() {
  m_IsExit = true;
  m_IsMonitorExit = true;
  Sleep(500);
  Stop();
}

void AcerXRRenderLoop::ClearPendingFrame() {
  has_outstanding_frame_ = false;
  if (delayed_get_frame_data_callback_) {
    base::ResetAndReturn(&delayed_get_frame_data_callback_).Run();
  }
}

void AcerXRRenderLoop::SubmitFrameMissing(int16_t frame_index,
                                          const gpu::SyncToken& sync_token) {
  // Nothing to do. It's OK to start the next frame even if the current
  // one didn't get sent to OpenVR.
  ClearPendingFrame();
}

void AcerXRRenderLoop::SubmitFrame(int16_t frame_index,
                                   const gpu::MailboxHolder& mailbox,
                                   base::TimeDelta time_waited) {
  NOTREACHED();
}

void AcerXRRenderLoop::SubmitFrameDrawnIntoTexture(
    int16_t frame_index,
    const gpu::SyncToken& sync_token,
    base::TimeDelta time_waited) {
  // Not currently implemented for Windows.
  NOTREACHED();
}

void AcerXRRenderLoop::SubmitFrameWithTextureHandle(
    int16_t frame_index,
    mojo::ScopedHandle texture_handle) {
  // DCHECK(acerxr_);
  // vr::IVRCompositor* vr_compositor = acerxr_->GetCompositor();
  // DCHECK(vr_compositor);

  TRACE_EVENT1("gpu", "SubmitFrameWithTextureHandle", "frameIndex",
               frame_index);
#if defined(OS_WIN)
  MojoPlatformHandle platform_handle;
  platform_handle.struct_size = sizeof(platform_handle);
  MojoResult result = MojoUnwrapPlatformHandle(texture_handle.release().value(),
                                               nullptr, &platform_handle);
  if (result != MOJO_RESULT_OK) {
    ClearPendingFrame();
    return;
  }

  texture_helper_.SetSourceTexture(
      base::win::ScopedHandle(reinterpret_cast<HANDLE>(platform_handle.value)));
  texture_helper_.AllocateBackBuffer();
  // static int delay_count = 0;

  
  if (!m_IsUWPLaunched) {
    if (texture_helper_.Is_SyncTexture_Target_Created()) {
      m_TargetHandleChanged = true;
      if (WMRUtil_HasFocus()) {
        LaunchUWP();
        m_IsUWPLaunched = true;
      } else {
        WMRUtil_SendKeysToSwitchFocus();
        LaunchUWP();
        m_IsUWPLaunched = true;
      }
    }
  }

  bool copy_successful = texture_helper_.CopyTextureToBackBuffer(true);
  if (copy_successful) {
    vr::Texture_t texture;
    texture.handle = texture_helper_.GetBackbuffer().Get();

    texture.eType = vr::TextureType_DirectX;
    texture.eColorSpace = vr::ColorSpace_Auto;

    vr::VRTextureBounds_t bounds[2];
    bounds[0] = {left_bounds_.x(), left_bounds_.y(),
                 left_bounds_.width() + left_bounds_.x(),
                 left_bounds_.height() + left_bounds_.y()};
    bounds[1] = {right_bounds_.x(), right_bounds_.y(),
                 right_bounds_.width() + right_bounds_.x(),
                 right_bounds_.height() + right_bounds_.y()};    
  }

  // Tell WebVR that we are done with the texture.
  submit_client_->OnSubmitFrameTransferred(copy_successful);
  submit_client_->OnSubmitFrameRendered();
#endif

  ClearPendingFrame();
}

void AcerXRRenderLoop::CleanUp() {
  submit_client_ = nullptr;
  presentation_binding_.Close();
  frame_data_binding_.Close();
  gamepad_provider_.Close();
}

void AcerXRRenderLoop::RequestGamepadProvider(
    mojom::IsolatedXRGamepadProviderRequest request) {
  gamepad_provider_.Close();
  gamepad_provider_.Bind(std::move(request));
}

void AcerXRRenderLoop::UpdateLayerBounds(int16_t frame_id,
                                         const gfx::RectF& left_bounds,
                                         const gfx::RectF& right_bounds,
                                         const gfx::Size& source_size) {
  // Bounds are updated instantly, rather than waiting for frame_id.  This works
  // since blink always passes the current frame_id when updating the bounds.
  // Ignoring the frame_id keeps the logic simpler, so this can more easily
  // merge with vr_shell_gl eventually.
  left_bounds_ = left_bounds;
  right_bounds_ = right_bounds;
  source_size_ = source_size;
};

void AcerXRRenderLoop::UpdateAcerXRUrl(mojom::AcerXRUrlInfoPtr acerxrurlinfo) {
  /*std::ofstream file;
  file.open("D:\\AcerXRRenderLoop_UpdateAcerXRUrl.txt");
  file << acerxrurlinfo->uul << "\n";
  file.close();*/

  IsDisableController = isDisableControllerList(acerxrurlinfo->uul);
  IsModifyPosition = isModifyPosition(acerxrurlinfo->uul);
};

bool AcerXRRenderLoop::isDisableControllerList(std::string str) {
  if (isDisableControllerForMatterport()) {
    // std::string matterport_str = "https://matterport.com";
    std::string matterport_str = "matterport";
    if (str.find(matterport_str) != std::string::npos) {
      return true;
    }
  }

  std::ifstream infile(DISABLE_CONTROLLER_LIST_FILE);
  std::string line;
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    if (!strcmp(line.c_str(), str.c_str()))
      return true;
  }
  return false;
}

bool AcerXRRenderLoop::isDisableControllerForMatterport() {
  std::ifstream infile(DISABLE_CONTROLLER_LIST_FILE);
  std::string line;
  std::string matterport_str = "https://matterport.com";
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    if (!strcmp(line.c_str(), matterport_str.c_str()))
      return true;
  }
  return false;
}

bool AcerXRRenderLoop::isModifyPosition(std::string str) {
  std::string sketchfab_str = "https://sketchfab.com";
  if (str.find(sketchfab_str) != std::string::npos)
    return true;

  std::ifstream infile(MODIFY_POSITION_LIST_FILE);
  std::string line;
  while (std::getline(infile, line)) {
    std::istringstream iss(line);
    if (!strcmp(line.c_str(), str.c_str()))
      return true;
  }
  return false;
}

void AcerXRRenderLoop::RequestSession(
    base::OnceCallback<void()> on_presentation_ended,
    mojom::XRRuntimeSessionOptionsPtr options,
    RequestSessionCallback callback) {
  DCHECK(options->immersive);
  presentation_binding_.Close();
  frame_data_binding_.Close();

  if (!acerxr_) {
    acerxr_ = std::make_unique<AcerXRWrapper>(true);
    if (!acerxr_->IsInitialized()) {
      acerxr_ = nullptr;
      main_thread_task_runner_->PostTask(
          FROM_HERE, base::BindOnce(std::move(callback), false, nullptr));
      return;
    }
  }

#if defined(OS_WIN)
  texture_helper_.SetAdapterIndex(0);

#endif
  DCHECK(!on_presentation_ended_);
  on_presentation_ended_ = std::move(on_presentation_ended);

  device::mojom::XRPresentationProviderPtr presentation_provider;
  device::mojom::XRFrameDataProviderPtr frame_data_provider;
  presentation_binding_.Bind(mojo::MakeRequest(&presentation_provider));
  frame_data_binding_.Bind(mojo::MakeRequest(&frame_data_provider));

  device::mojom::XRPresentationTransportOptionsPtr transport_options =
      device::mojom::XRPresentationTransportOptions::New();
  transport_options->transport_method =
      device::mojom::XRPresentationTransportMethod::SUBMIT_AS_TEXTURE_HANDLE;
  // Only set boolean options that we need. Default is false, and we should be
  // able to safely ignore ones that our implementation doesn't care about.
  transport_options->wait_for_transfer_notification = true;

  // Reset the active states for all the controllers.
  

  auto submit_frame_sink = device::mojom::XRPresentationConnection::New();
  submit_frame_sink->provider = presentation_provider.PassInterface();
  submit_frame_sink->client_request = mojo::MakeRequest(&submit_client_);
  submit_frame_sink->transport_options = std::move(transport_options);

  auto session = device::mojom::XRSession::New();
  session->data_provider = frame_data_provider.PassInterface();
  session->submit_frame_sink = std::move(submit_frame_sink);

  main_thread_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(std::move(callback), true, std::move(session)));
  is_presenting_ = true;
  // acerxr_->GetCompositor()->SuspendRendering(false);

  // Measure the VrViewerType we are presenting with.
  using ViewerMap = std::map<std::string, VrViewerType>;
  CR_DEFINE_STATIC_LOCAL(ViewerMap, viewer_types,
                         ({
                             {"Oculus Rift CV1", VrViewerType::OPENVR_RIFT_CV1},
                             {"Vive MV", VrViewerType::OPENVR_VIVE},
                         }));
  VrViewerType type = VrViewerType::OPENVR_UNKNOWN;
  // std::string model =
  //     GetAcerXRString(acerxr_->GetSystem(), vr::Prop_ModelNumber_String);

  std::string model = "Acer XR HMD";
  auto it = viewer_types.find(model);
  if (it != viewer_types.end())
    type = it->second;

  base::UmaHistogramSparse("VRViewerType", static_cast<int>(type));

  // Create the thread
  ////
  HANDLE m_hThread = reinterpret_cast<HANDLE>(
      _beginthreadex(nullptr, 0, ProcessThread_Socket, this, 0, nullptr));
  if (m_hThread) {
    OutputDebugStringW(L"ProcessThread_Socket is ready \n");
  } else {
    OutputDebugStringW(L"ProcessThread_Socket is NOT ready \n");
  }
  HANDLE m_hThread_winy = reinterpret_cast<HANDLE>(_beginthreadex(
      nullptr, 0, ProcessThread_Win_Y_Monitor, this, 0, nullptr));
  if (m_hThread_winy) {
    OutputDebugStringW(L"ProcessThread_Win_Y_Monitor is ready \n");
  } else {
    OutputDebugStringW(L"ProcessThread_Win_Y_Monitor is NOT ready \n");
  }
  ////
}

void AcerXRRenderLoop::ExitPresent() {
  is_presenting_ = false;
  m_IsExit = true;
  m_IsMonitorExit = true;
  Sleep(500);

  m_IsUWPLaunched = false;
  presentation_binding_.Close();
  frame_data_binding_.Close();
  submit_client_ = nullptr;
  
  acerxr_ = nullptr;

  has_outstanding_frame_ = false;
  delayed_get_frame_data_callback_.Reset();

  // Push out one more controller update so we don't have stale controllers.
  if (!IsDisableController)
    UpdateControllerState();

  if (on_presentation_ended_) {
    main_thread_task_runner_->PostTask(FROM_HERE,
                                       std::move(on_presentation_ended_));
  }
}

void AcerXRRenderLoop::UpdateControllerState() {
  if (!gamepad_callback_) {
    // Nobody is listening to updates, so bail early.
    return;
  }

  if (!acerxr_) {
    std::move(gamepad_callback_).Run(nullptr);
    return;
  }

  if (WaitForSingleObject(ghMutex_controller, 5) == WAIT_OBJECT_0)  // INFINITE
  {
    std::move(gamepad_callback_)
        .Run(AcerXRGamepadHelper::GetGamepadData(g_controllerDataList));
  }
  ReleaseMutex(ghMutex_controller);
}

void AcerXRRenderLoop::RequestUpdate(
    mojom::IsolatedXRGamepadProvider::RequestUpdateCallback callback) {
  // Save the callback to resolve next time we update (typically on vsync).
  gamepad_callback_ = std::move(callback);

  // If we aren't presenting, reply now saying that we have no controllers.
  if (!is_presenting_) {
    if (!IsDisableController)
      UpdateControllerState();
  }
}



mojom::VRPosePtr AcerXRRenderLoop::GetPose() {
  vr::TrackedDevicePose_t rendering_poses[vr::k_unMaxTrackedDeviceCount];

  rendering_poses->bPoseIsValid = true;
  rendering_poses->eTrackingResult = vr::TrackingResult_Running_OK;
  rendering_poses->bDeviceIsConnected = true;
  rendering_poses->vAngularVelocity.v[0] = 0;
  rendering_poses->vAngularVelocity.v[1] = 0;
  rendering_poses->vAngularVelocity.v[2] = 0;
  rendering_poses->vVelocity.v[0] = 0;
  rendering_poses->vVelocity.v[1] = 0;
  rendering_poses->vVelocity.v[2] = 0;

  rendering_poses[0].mDeviceToAbsoluteTracking.m[0][0] = 1.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[1][0] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[2][0] = 0.0f;

  rendering_poses[0].mDeviceToAbsoluteTracking.m[0][1] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[1][1] = 1.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[2][1] = 0.0f;

  rendering_poses[0].mDeviceToAbsoluteTracking.m[0][2] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[1][2] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[2][2] = 1.0f;

  rendering_poses[0].mDeviceToAbsoluteTracking.m[0][3] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[1][3] = 0.0f;
  rendering_poses[0].mDeviceToAbsoluteTracking.m[2][3] = 0.0f;

  
  TRACE_EVENT0("gpu", "WaitGetPoses");
  
  Sleep(SLEEP_TIME);

  mojom::VRPosePtr pose = mojo::ConvertTo<mojom::VRPosePtr>(
      rendering_poses[vr::k_unTrackedDeviceIndex_Hmd]);
  
  if (WaitForSingleObject(ghMutex_sensor, 5) == WAIT_OBJECT_0)  // INFINITE
  {
    // if (WaitForSingleObject(ghMutex_sensor, 5) == WAIT_OBJECT_0)   //
    // INFINITE
    if (!g_sensorListPtr.empty()) {
      HMDSensorData_ = g_sensorListPtr.front();

      rot_x = HMDSensorData_.rot_x;
      rot_y = HMDSensorData_.rot_y;
      rot_z = HMDSensorData_.rot_z;
      rot_w = HMDSensorData_.rot_w;

      pos_x = HMDSensorData_.pos_x;
      pos_y = HMDSensorData_.pos_y;
      pos_z = HMDSensorData_.pos_z;

      if (!g_sensorListPtr.empty())
        g_sensorListPtr.pop_front();     
      
    }
    ReleaseMutex(ghMutex_sensor);
  }
  
  pose->orientation = std::vector<float>({rot_x, rot_y, rot_z, rot_w});

  if (IsModifyPosition)
    pose->position = std::vector<float>({pos_x, pos_y + 1, pos_z});
  else
    pose->position = std::vector<float>({pos_x, pos_y, pos_z});
  // Update WebXR input sources.
  DCHECK(pose);

  if (WaitForSingleObject(ghMutex_controller, 5) == WAIT_OBJECT_0)  // INFINITE
  {
    pose->input_state = GetInputState(rendering_poses, 1);
  }
  ReleaseMutex(ghMutex_controller);
  return pose;
}

void AcerXRRenderLoop::Init() {}
HRESULT AcerXRRenderLoop::LaunchUWP() {
  // IApplicationActivationManager* AppActivationMgr = nullptr;
  CComPtr<IApplicationActivationManager> AppActivationMgr = nullptr;
  // HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr,
                        CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&AppActivationMgr));

  if (FAILED(hr)) {
    TCHAR buffer[300];
    swprintf(buffer,
             L"LaunchUWP %s: Failed to create Application Activation "
             L"Manager.hr = 0x%08lx \n",
             AUMID, hr);
    OutputDebugStringW(buffer);
  }

  if (SUCCEEDED(hr)) {
    DWORD pid = 0;
    hr = AppActivationMgr->ActivateApplication(AUMID, nullptr, AO_NONE, &pid);

    if (FAILED(hr)) {
      TCHAR buffer[300];
      swprintf(buffer, L"LaunchUWP %s: Failed to Activate App. hr = 0x%08lx \n",
               AUMID, hr);
      OutputDebugStringW(buffer);
    }
  }
  // AppActivationMgr->Release();
  CoUninitialize();
  return hr;
}

bool AcerXRRenderLoop::WMRUtil_HasFocus() {
  HWND hWnd = ::GetTopWindow(nullptr);

  int i;
  const int maxWin = 5;  // max number of windows to search
  for (i = 0; i < maxWin; i++) {
    WCHAR wndClassName[MAX_PATH];
    ::memset(wndClassName, 0, sizeof(wndClassName));
    if (::GetClassName(hWnd, wndClassName, MAX_PATH) > 0) {
      // WMR banner class name: "HolographicInputBannerWndClass"
      // search for the window that has Holographic and Banner.
      const WCHAR wmrBannerWndClassName_Holographic[] = L"Holographic";
      const WCHAR wmrBannerWndClassName_Banner[] = L"Banner";

      // if either "Holographic" and "Banner" is in the top window class name,
      // then WMR has the focus.
      if (::wcsstr(wndClassName, wmrBannerWndClassName_Holographic) &&
          ::wcsstr(wndClassName, wmrBannerWndClassName_Banner))
        return true;
    }

    // continue to the next window
    hWnd = ::GetNextWindow(hWnd, GW_HWNDNEXT);
    if (hWnd == nullptr)
      break;
  }

  return false;
}

void AcerXRRenderLoop::WMRUtil_SendKeysToSwitchFocus() {
  // press/release Win+Y key combination
  INPUT keyInput[4] = {};

  // Win + y key down
  keyInput[0].type = INPUT_KEYBOARD;
  keyInput[0].ki.wVk = VK_LWIN;

  keyInput[1].type = INPUT_KEYBOARD;
  keyInput[1].ki.wVk = 0x59;

  // Win + y key up (y key is up first)
  keyInput[3].type = INPUT_KEYBOARD;
  keyInput[3].ki.wVk = 0x59;
  keyInput[3].ki.dwFlags = KEYEVENTF_KEYUP;

  keyInput[2].type = INPUT_KEYBOARD;
  keyInput[2].ki.wVk = VK_LWIN;
  keyInput[2].ki.dwFlags = KEYEVENTF_KEYUP;

  SendInput(4, keyInput, sizeof(INPUT));
}

void AcerXRRenderLoop::ProcessPredictTime(const char* rcvbuf) {
  int ptr;
  int idx = 0;
  int PredictTime;

  // long UWP_sensor_timestamp = 0;
  for (int i = 0; i < 1; i++) {
    memcpy(&ptr, rcvbuf + idx, 4);
    idx += 4;
    if (i == 0)
      PredictTime = ptr;
  }
  SLEEP_TIME = PredictTime;
}

void AcerXRRenderLoop::Process6Dof(const char* rcvbuf) {
  long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  /*if ((timestamp - m_timePosData) <= 0) {
     return;
  }*/

  // It is position data
  float ptr;
  int idx = 0;
  float rotX = 0.0f, rotY = 0.0f, rotZ = 0.0f, rotW = 0.0f, posX = 0.0f,
        posY = 0.0f, posZ = 0.0f;
  // long UWP_sensor_timestamp = 0;
  for (int i = 0; i < 7; i++) {
    memcpy(&ptr, rcvbuf + idx, 4);
    idx += 4;
    if (i == 0)
      rotX = ptr;
    else if (i == 1)
      rotY = ptr;
    else if (i == 2)
      rotZ = ptr;
    else if (i == 3)
      rotW = ptr;
    else if (i == 4)
      posX = ptr;
    else if (i == 5)
      posY = ptr;
    else if (i == 6)
      posZ = ptr;
  }

  //============
  HMDSensorData new_sensorData;
  /*rot_x = rotX;
  rot_y = rotY;
  rot_z = rotZ;
  rot_w = rotW;*/
  new_sensorData.rot_x = rotX;
  new_sensorData.rot_y = rotY;
  new_sensorData.rot_z = rotZ;
  new_sensorData.rot_w = rotW;
  new_sensorData.pos_x = posX;
  new_sensorData.pos_y = posY;
  new_sensorData.pos_z = posZ;
      
  if (WaitForSingleObject(ghMutex_sensor, 1) == WAIT_OBJECT_0)  // INFINITE
  {
    if (g_sensorListPtr.size() >= SENSOR_FIFO_SIZE) {
      g_sensorListPtr.push_back(new_sensorData);
      g_sensorListPtr.pop_front();
    } else {
      g_sensorListPtr.push_back(new_sensorData);
    }
    
    ReleaseMutex(ghMutex_sensor);
  } 
  
  // Change sensor FIFO architecture
  m_timePosData = timestamp;
}

void AcerXRRenderLoop::ProcessController(const char* rcvbuf) {
  int idx = 0;
  int id, handedness;
  int ptrInt;
  for (int i = 0; i < 2; i++) {
    memcpy(&ptrInt, rcvbuf + idx, 4);
    idx += 4;
    if (i == 0)
      id = ptrInt;
    else if (i == 1)
      handedness = ptrInt;
  }
  if (id == 0 ||
      (handedness != LEFT_CONTROLLER && handedness != RIGHT_CONTROLLER)) {
    return;
  }

  float rotX, rotY, rotZ, rotW, posX, posY, posZ, velX, velY, velZ, angVelX,
      angVelY, angVelZ;
  float ptrFloat;
  for (int i = 0; i < 13; i++) {
    memcpy(&ptrFloat, rcvbuf + idx, 4);
    idx += 4;
    if (i == 0)
      rotX = ptrFloat;
    else if (i == 1)
      rotY = ptrFloat;
    else if (i == 2)
      rotZ = ptrFloat;
    else if (i == 3)
      rotW = ptrFloat;
    else if (i == 4)
      posX = ptrFloat;
    else if (i == 5)
      posY = ptrFloat;
    else if (i == 6)
      posZ = ptrFloat;
    else if (i == 7)
      velX = ptrFloat;
    else if (i == 8)
      velY = ptrFloat;
    else if (i == 9)
      velZ = ptrFloat;
    else if (i == 10)
      angVelX = ptrFloat;
    else if (i == 11)
      angVelY = ptrFloat;
    else if (i == 12)
      angVelZ = ptrFloat;
  }

  bool isPressed, isGrasped, isMenuPressed, isSelectPressed,
      isThumbstickPressed, isTouchpadPressed, isTouchpadTouched;
  bool ptrBool;
  for (int i = 0; i < 7; i++) {
    memcpy(&ptrBool, rcvbuf + idx, 1);
    idx += 1;
    if (i == 0)
      isPressed = ptrBool;
    else if (i == 1)
      isGrasped = ptrBool;
    else if (i == 2)
      isMenuPressed = ptrBool;
    else if (i == 3)
      isSelectPressed = ptrBool;
    else if (i == 4)
      isThumbstickPressed = ptrBool;
    else if (i == 5)
      isTouchpadPressed = ptrBool;
    else if (i == 6)
      isTouchpadTouched = ptrBool;
  }

  double selectPressedValue, ThumbstickX, ThumbstickY, TouchpadX, TouchpadY;
  double ptrDouble;
  for (int i = 0; i < 5; i++) {
    memcpy(&ptrDouble, rcvbuf + idx, 8);
    idx += 8;
    if (i == 0)
      selectPressedValue = ptrDouble;
    else if (i == 1)
      ThumbstickX = ptrDouble;
    else if (i == 2)
      ThumbstickY = ptrDouble;
    else if (i == 3)
      TouchpadX = ptrDouble;
    else if (i == 4)
      TouchpadY = ptrDouble;
  }

  ControllerData controllerData;
  controllerData.id = id;
  controllerData.handedness = handedness;
  controllerData.pose.rot_x = rotX;
  controllerData.pose.rot_y = rotY;
  controllerData.pose.rot_z = rotZ;
  controllerData.pose.rot_w = rotW;
  controllerData.pose.pos_x = posX;
  if (IsModifyPosition)
    controllerData.pose.pos_y = posY + 1;
  else
    controllerData.pose.pos_y = posY;
  controllerData.pose.pos_z = posZ;
  controllerData.vel[0] = velX;
  controllerData.vel[1] = velY;
  controllerData.vel[2] = velZ;
  controllerData.vel[3] = angVelX;
  controllerData.vel[4] = angVelY;
  controllerData.vel[5] = angVelZ;

  if (isGrasped) {
    controllerData.buttonPressed |= CONTROLLER_GRASPED;
    // controllerData.buttonTouched |= CONTROLLER_GRASPED;
  }
  if (isMenuPressed) {
    controllerData.buttonPressed |= CONTROLLER_MENU;
    // controllerData.buttonTouched |= CONTROLLER_MENU;
  }
  if (isSelectPressed) {
    controllerData.buttonPressed |= CONTROLLER_SELECTED;
    // controllerData.buttonTouched |= CONTROLLER_SELECTED;
  }
  if (isThumbstickPressed) {
    controllerData.buttonPressed |= CONTROLLER_THUMBSTICK;
  }
  if (isTouchpadPressed) {
    controllerData.buttonPressed |= CONTROLLER_TOUCHPAD;
  }

  controllerData.selectPressedValue = selectPressedValue;
  controllerData.Thumbstick[0] = ThumbstickX;
  controllerData.Thumbstick[1] = ThumbstickY;
  // if (ThumbstickX != 0 || ThumbstickY != 0) {
  //  controllerData.buttonTouched |= CONTROLLER_THUMBSTICK;
  //}

  controllerData.Touchpad[0] = TouchpadX;
  controllerData.Touchpad[1] = TouchpadY;
  if (isTouchpadTouched || TouchpadX != 0 || TouchpadY != 0) {
    controllerData.buttonTouched |= CONTROLLER_TOUCHPAD;
  }

  if (controllerData.handedness == LEFT_CONTROLLER ||
      controllerData.handedness == RIGHT_CONTROLLER) {
    if (WaitForSingleObject(ghMutex_controller, 5) ==
        WAIT_OBJECT_0)  // INFINITE
    {
      int count = g_controllerDataList.size();
      if (count <= 0) {
        g_controllerDataList.push_back(controllerData);
      } else if (count <= CONTROLLER_COUNT) {
        bool existed = false;
        std::list<ControllerData>::iterator it;
        for (it = g_controllerDataList.begin();
             it != g_controllerDataList.end(); it++) {
          if (it->id == controllerData.id) {
            existed = true;
            it->pose = controllerData.pose;
            for (int i = 0; i < 6; i++) {
              it->vel[i] = controllerData.vel[i];
            }
            it->buttonPressed = controllerData.buttonPressed;
            it->buttonTouched = controllerData.buttonTouched;
            it->selectPressedValue = controllerData.selectPressedValue;
            for (int i = 0; i < 2; i++) {
              it->Thumbstick[i] = controllerData.Thumbstick[i];
              it->Touchpad[i] = controllerData.Touchpad[i];
            }
            break;
          }
        }
        if (!existed && count < CONTROLLER_COUNT) {
          g_controllerDataList.push_back(controllerData);
        }
      } else {
        printf("Should not run here, there are too many controllers added");
      }
    }
    ReleaseMutex(ghMutex_controller);
  }
}

base::WeakPtr<AcerXRRenderLoop> AcerXRRenderLoop::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void AcerXRRenderLoop::GetFrameData(
    mojom::XRFrameDataProvider::GetFrameDataCallback callback) {
  DCHECK(is_presenting_);

  if (has_outstanding_frame_) {
    DCHECK(!delayed_get_frame_data_callback_);
    delayed_get_frame_data_callback_ =
        base::BindOnce(&AcerXRRenderLoop::GetFrameData, base::Unretained(this),
                       std::move(callback));
    return;
  }

  has_outstanding_frame_ = true;

  mojom::XRFrameDataPtr frame_data = mojom::XRFrameData::New();

  frame_data->frame_id = next_frame_id_;
  next_frame_id_ += 1;
  if (next_frame_id_ < 0) {
    next_frame_id_ = 0;
  }

  if (acerxr_) {
    frame_data->pose = GetPose();
    vr::Compositor_FrameTiming timing;
    timing.m_nSize = sizeof(vr::Compositor_FrameTiming);    
  }

  // Update gamepad controllers.
  if (!IsDisableController)
    UpdateControllerState();

  std::move(callback).Run(std::move(frame_data));
}

std::vector<mojom::XRInputSourceStatePtr> AcerXRRenderLoop::GetInputState(
    vr::TrackedDevicePose_t* poses,
    uint32_t count) {
  std::vector<mojom::XRInputSourceStatePtr> input_states;

  if (!acerxr_ || g_controllerDataList.size() <= 0)
    return input_states;

  ControllerData controllerData = g_controllerDataList.front();

  device::mojom::XRInputSourceStatePtr state =
      device::mojom::XRInputSourceState::New();
  state->source_id = controllerData.id;
  state->primary_input_pressed =
      (controllerData.buttonPressed & CONTROLLER_MASK) != 0;
  state->primary_input_clicked = false;

  state->grip = gfx::Transform(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
  // Scoot the grip matrix back a bit so that it actually lines up with the
  // user's palm.
  state->grip->Translate3d(0, 0, kGripOffsetZMeters);

  device::mojom::XRInputSourceDescriptionPtr desc =
      device::mojom::XRInputSourceDescription::New();

  // It's a handheld pointing device.
  desc->target_ray_mode = device::mojom::XRTargetRayMode::POINTING;

  // Set handedness.
  switch (controllerData.handedness) {
    case LEFT_CONTROLLER:
      desc->handedness = device::mojom::XRHandedness::LEFT;
      break;
    case RIGHT_CONTROLLER:
      desc->handedness = device::mojom::XRHandedness::RIGHT;
      break;
    default:
      desc->handedness = device::mojom::XRHandedness::NONE;
      break;
  }

  // WMR controller are fully 6DoF.
  desc->emulated_position = false;

  // Tweak the pointer transform so that it's angled down from the
  // grip. This should be a bit more ergonomic.
  desc->pointer_offset = gfx::Transform();
  desc->pointer_offset->RotateAboutXAxis(kPointerErgoAngleDegrees);

  state->description = std::move(desc);
  input_states.push_back(std::move(state));

  return input_states; 
}

}  // namespace device
