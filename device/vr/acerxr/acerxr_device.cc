// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/acerxr/acerxr_device.h"

#include <math.h>
// [Leo] Read json file ++
#include <fstream>
#include <iostream>
#include "base/json/json_reader.h"
#include "base/json/json_parser.h"
#include "base/json/json_writer.h"
#include <Shlobj.h>
#include <string>
#include <atlbase.h>
#include <process.h>
#include <chrono>
// [Leo] Read json file --

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/math_constants.h"
#include "build/build_config.h"
#include "device/vr/acerxr/acerxr_render_loop.h"
#include "device/vr/acerxr/acerxr_type_converters.h"
#include "third_party/openvr/src/headers/openvr.h"
#include "ui/gfx/geometry/angle_conversions.h"

namespace device {
#define CONFIG_FILE_FOLDER L"\\AcerWebVR\\"
#define SETTING_FILE_NAME L"WebVR.vrsettings"
#define TRIGGER_FILE_NAME L"WebVR.vrtrigger"

#define DISABLE_CONTROLLER_LIST_FILE "..\\settings\\WebVR.disablecontrollerlist"
// #define DISABLE_CONTROLLER_LIST_FILE "WebVR.disablecontrollerlist"

#define MODIFY_POSITION_LIST_FILE "..\\settings\\WebVR.modifypositionlist"
// #define MODIFY_POSITION_LIST_FILE "WebVR.modifypositionlist"

static float Acer_HMD_FOV = 50.0f;      // 50.0f
static float Acer_HMD_IPD = 0.062f;
static int Acer_HMD_Resolution_Height = 1593;  // 1593;
static int Acer_HMD_Resolution_Width = 1593;
static int Acer_SENSOR_FIFO_SIZE = 2;
static int Acer_Sleep_time = 0;

static std::string ButtonInfo = "";
static std::string Ori_URL = "";

namespace {

constexpr float kDefaultIPD = 0.06f;  // Default average IPD.
constexpr double kTimeBetweenPollingEventsSeconds = 0.25;

mojom::VRFieldOfViewPtr AcerXRFovToWebVRFov(vr::IVRSystem* vr_system,
                                            vr::Hmd_Eye eye) {
  auto out = mojom::VRFieldOfView::New();
  return out;
}

std::vector<float> HmdMatrix34ToWebVRTransformMatrix(
    const vr::HmdMatrix34_t& mat) {
  std::vector<float> transform;
  transform.resize(16);
  transform[0] = mat.m[0][0];
  transform[1] = mat.m[1][0];
  transform[2] = mat.m[2][0];
  transform[3] = 0.0f;
  transform[4] = mat.m[0][1];
  transform[5] = mat.m[1][1];
  transform[6] = mat.m[2][1];
  transform[7] = 0.0f;
  transform[8] = mat.m[0][2];
  transform[9] = mat.m[1][2];
  transform[10] = mat.m[2][2];
  transform[11] = 0.0f;
  transform[12] = mat.m[0][3];
  transform[13] = mat.m[1][3];
  transform[14] = mat.m[2][3];
  transform[15] = 1.0f;
  return transform;
}

mojom::VRDisplayInfoPtr CreateVRDisplayInfo(vr::IVRSystem* vr_system,
                                            device::mojom::XRDeviceId id) {
  mojom::VRDisplayInfoPtr display_info = mojom::VRDisplayInfo::New();
  display_info->id = id;

  display_info->displayName = "Acer XR HMD";
  
  display_info->capabilities = mojom::VRDisplayCapabilities::New();
  display_info->capabilities->hasPosition = true;
  display_info->capabilities->hasExternalDisplay = true;
  display_info->capabilities->canPresent = true;
  display_info->webvr_default_framebuffer_scale = 1.0;
  display_info->webxr_default_framebuffer_scale = 1.0;


  display_info->leftEye = mojom::VREyeParameters::New();
  display_info->rightEye = mojom::VREyeParameters::New();
  mojom::VREyeParametersPtr& left_eye = display_info->leftEye;
  mojom::VREyeParametersPtr& right_eye = display_info->rightEye;

  left_eye->fieldOfView = AcerXRFovToWebVRFov(vr_system, vr::Eye_Left);
  right_eye->fieldOfView = AcerXRFovToWebVRFov(vr_system, vr::Eye_Right);

  //[Leo] 20180814 - add VRDisplay ++
  left_eye->fieldOfView->leftDegrees = Acer_HMD_FOV;
  left_eye->fieldOfView->rightDegrees = Acer_HMD_FOV;
  left_eye->fieldOfView->upDegrees = Acer_HMD_FOV;
  left_eye->fieldOfView->downDegrees = Acer_HMD_FOV;

  right_eye->fieldOfView->leftDegrees = Acer_HMD_FOV;
  right_eye->fieldOfView->rightDegrees = Acer_HMD_FOV;
  right_eye->fieldOfView->upDegrees = Acer_HMD_FOV;
  right_eye->fieldOfView->downDegrees = Acer_HMD_FOV;
  //[Leo] 20180814 - add VRDisplay --

  vr::TrackedPropertyError error = vr::TrackedProp_Success;
  float ipd = Acer_HMD_IPD;
  
  if (error != vr::TrackedProp_Success)
    ipd = kDefaultIPD;

  left_eye->offset.resize(3);
  left_eye->offset[0] = -ipd * 0.5;
  left_eye->offset[1] = 0.0f;
  left_eye->offset[2] = 0.0f;
  right_eye->offset.resize(3);
  right_eye->offset[0] = ipd * 0.5;
  right_eye->offset[1] = 0.0;
  right_eye->offset[2] = 0.0;

  uint32_t width, height;
  width = Acer_HMD_Resolution_Width;
  height = Acer_HMD_Resolution_Height;
  
  left_eye->renderWidth = width;
  left_eye->renderHeight = height;
  right_eye->renderWidth = left_eye->renderWidth;
  right_eye->renderHeight = left_eye->renderHeight;

  display_info->stageParameters = mojom::VRStageParameters::New();

  vr::HmdMatrix34_t mat;
  mat.m[0][0] = 1.0f;
  mat.m[1][0] = 0.0f;
  mat.m[2][0] = 0.0f;

  mat.m[0][1] = 0.0f;
  mat.m[1][1] = 1.0f;
  mat.m[2][1] = 0.0f;

  mat.m[0][2] = 0.0f;
  mat.m[1][2] = 0.0f;
  mat.m[2][2] = 1.0f;

  mat.m[0][3] = 0.0f;
  mat.m[1][3] = 0.0f;
  mat.m[2][3] = 0.0f;
  
  display_info->stageParameters->standingTransform =
      HmdMatrix34ToWebVRTransformMatrix(mat);

  vr::IVRChaperone* chaperone = vr::VRChaperone();
  if (chaperone) {
    chaperone->GetPlayAreaSize(&display_info->stageParameters->sizeX,
                               &display_info->stageParameters->sizeZ);
  } else {
    display_info->stageParameters->sizeX = 0.0f;
    display_info->stageParameters->sizeZ = 0.0f;
  }
  return display_info;
}

}  // namespace

bool AcerXRDevice::check_if_file_exists(std::wstring path) {
  std::ifstream ff(path.c_str());
  return ff.is_open();
}

void AcerXRDevice::ReadSettingFile() {
  std::string line;
  
  char *line_buf;

  /////////////////////////
  PWSTR folder = NULL;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &folder);
  std::wstring path(folder);
  // CoTaskMemFree(folder);
  path += CONFIG_FILE_FOLDER;
  DWORD ftyp = GetFileAttributes(path.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES || !(ftyp & FILE_ATTRIBUTE_DIRECTORY))
    CreateDirectory(path.c_str(), NULL);

  path += SETTING_FILE_NAME;

  const wchar_t* str = path.c_str();
  size_t path_size = wcslen(str) * 2 + 2;
  char* char_path = new char[path_size];
  size_t c_size;
  wcstombs_s(&c_size, char_path, path_size, str, path_size);
  /////////////////////////
  
  if (!check_if_file_exists(path)) {
    FILE* file_setting = fopen(char_path, "wb");
    char setting_bytes[] = "{\n\"FOV\": 50,\n\"IPD\": 0.062,\n\"ContentSize_Height\": 1080,\n\"ContentSize_Width\": 2160,\n\"gl_renderHeight\": 1593,\n\"gl_renderWidth\": 1593,\n\"UWP_ScreenScale\": 5.0,\n\"Sensor_FIFO_Size\": 2,\n\"Sleep_Time\": 2\n}\n";
    fwrite(setting_bytes, 1, sizeof(setting_bytes)-1, file_setting);
    fclose(file_setting);
  } 
  //
  FILE* file = fopen(char_path, "rb");
  // fgets(line_buf, sizeof(line_buf), file);
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
 
  line_buf = new char[size];
  rewind(file);
  fread(line_buf, 1, size, file);
  fclose(file);

  // std::string str(line_buf);
  base::JSONReader reader;  
  // std::unique_ptr<base::Value> root = reader.Read(base::StringPiece("{}"));
  std::unique_ptr<base::Value> root =
      reader.Read(base::StringPiece(reinterpret_cast<char*>(line_buf), size));
  base::DictionaryValue* dict = NULL;
  root->GetAsDictionary(&dict);
  int fov;
  double ipd = 0.0;
  int sensor_FIFO_size_;
  int resolution_height;
  int resolution_width;
  int sleep_time;
  dict->GetInteger("FOV", &fov);
  dict->GetDouble("IPD", &ipd);
    
  dict->GetInteger("gl_renderHeight", &resolution_height);
  dict->GetInteger("gl_renderWidth", &resolution_width);
  dict->GetInteger("Sensor_FIFO_Size", &sensor_FIFO_size_);
  dict->GetInteger("Sleep_Time", &sleep_time);
  
  Acer_HMD_FOV = fov;  // 50.0f
  Acer_HMD_IPD = ipd;                                        
  Acer_HMD_Resolution_Height = resolution_height;  // 1593;
  Acer_HMD_Resolution_Width = resolution_height;

  Acer_SENSOR_FIFO_SIZE = sensor_FIFO_size_;
  Acer_Sleep_time = sleep_time;
}

void AcerXRDevice::ReadButtonInfoandURLFile() {
  std::string line;
  // char line_buf[32];
  char* line_buf;

  /////////////////////////
  PWSTR folder = NULL;
  SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &folder);
  std::wstring path(folder);
  // CoTaskMemFree(folder);
  path += CONFIG_FILE_FOLDER;
  DWORD ftyp = GetFileAttributes(path.c_str());
  if (ftyp == INVALID_FILE_ATTRIBUTES || !(ftyp & FILE_ATTRIBUTE_DIRECTORY))
    CreateDirectory(path.c_str(), NULL);

  path += TRIGGER_FILE_NAME;

  const wchar_t* str = path.c_str();
  size_t path_size = wcslen(str) * 2 + 2;
  char* char_path = new char[path_size];
  size_t c_size;
  wcstombs_s(&c_size, char_path, path_size, str, path_size);
  /////////////////////////
  if (!check_if_file_exists(path)) {
    FILE* file_URL = fopen(char_path, "wb");
    char URL_bytes[] = "{\n\"ButtonInfo\": \"\",\n\"Ori_URL\": \"\"\n}\n";
    fwrite(URL_bytes, 1, sizeof(URL_bytes) - 1, file_URL);
    fclose(file_URL);
  } 
  
  FILE* file = fopen(char_path, "rb");
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);

  line_buf = new char[size];
  rewind(file);
  fread(line_buf, 1, size, file);
  fclose(file);

  base::JSONReader reader;
  std::unique_ptr<base::Value> root =
      reader.Read(base::StringPiece(reinterpret_cast<char*>(line_buf), size));
  if (!root)
    return;
  base::DictionaryValue* dict = NULL;
  root->GetAsDictionary(&dict);
  
  std::string buttonInfo;
  dict->GetString("ButtonInfo", &buttonInfo);
  ButtonInfo = buttonInfo;

  std::string ori_url;
  dict->GetString("Ori_URL", &ori_url);
  Ori_URL = ori_url;  
}

AcerXRDevice::AcerXRDevice()
    : VRDeviceBase(device::mojom::XRDeviceId::OPENVR_DEVICE_ID),
      main_thread_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      exclusive_controller_binding_(this),
      gamepad_provider_factory_binding_(this),
      weak_ptr_factory_(this) {
  // Initialize OpenVR.
  acerxr_ = std::make_unique<AcerXRWrapper>(false /* presenting */);
  if (!acerxr_->IsInitialized()) {
    acerxr_ = nullptr;
    return;
  }

  ReadSettingFile();
  ReadButtonInfoandURLFile();

  SetVRDisplayInfo(CreateVRDisplayInfo(acerxr_->GetSystem(), GetId()));

  render_loop_ = std::make_unique<AcerXRRenderLoop>();
  render_loop_->Draw_Texture_Width = Acer_HMD_Resolution_Width * 2;
  render_loop_->Draw_Texture_Height = Acer_HMD_Resolution_Height;
  render_loop_->SENSOR_FIFO_SIZE = Acer_SENSOR_FIFO_SIZE;
  render_loop_->SLEEP_TIME = Acer_Sleep_time;

  render_loop_->IsDisableController = isDisableControllerList(Ori_URL);
  render_loop_->IsModifyPosition = isModifyPosition(Ori_URL);

  OnPollingEvents();  
}

bool AcerXRDevice::isDisableControllerForMatterport() {
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

bool AcerXRDevice::isModifyPosition(std::string str) {
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

bool AcerXRDevice::isDisableControllerList(std::string str) {

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

mojom::IsolatedXRGamepadProviderFactoryPtr AcerXRDevice::BindGamepadFactory() {
  mojom::IsolatedXRGamepadProviderFactoryPtr ret;
  gamepad_provider_factory_binding_.Bind(mojo::MakeRequest(&ret));
  return ret;
}

AcerXRDevice::~AcerXRDevice() {
  Shutdown();
}

void AcerXRDevice::Shutdown() {
  // Wait for the render loop to stop before completing destruction. This will
  // ensure that the IVRSystem doesn't get shutdown until the render loop is no
  // longer referencing it.
  if (render_loop_ && render_loop_->IsRunning())
    render_loop_->Stop();
}

void AcerXRDevice::RequestSession(
    mojom::XRRuntimeSessionOptionsPtr options,
    mojom::XRRuntime::RequestSessionCallback callback) {

	
  if (!options->immersive) {
    ReturnNonImmersiveSession(std::move(callback));
    return;
  }

  if (!render_loop_->IsRunning()) {
    render_loop_->Start();

    if (!render_loop_->IsRunning()) {
      std::move(callback).Run(nullptr, nullptr);
      return;
    }

    if (provider_request_) {
      render_loop_->task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&AcerXRRenderLoop::RequestGamepadProvider,
                                    render_loop_->GetWeakPtr(),
                                    std::move(provider_request_)));
    }
  }

  // We are done using OpenVR until the presentation session ends.
  acerxr_ = nullptr;

  auto my_callback =
      base::BindOnce(&AcerXRDevice::OnRequestSessionResult,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback));

  auto on_presentation_ended = base::BindOnce(
      &AcerXRDevice::OnPresentationEnded, weak_ptr_factory_.GetWeakPtr());

  render_loop_->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&AcerXRRenderLoop::RequestSession,
                                render_loop_->GetWeakPtr(),
                                std::move(on_presentation_ended),
                                std::move(options), std::move(my_callback)));
}

void AcerXRDevice::OnPresentationEnded() {
  
}

void AcerXRDevice::OnRequestSessionResult(
    mojom::XRRuntime::RequestSessionCallback callback,
    bool result,
    mojom::XRSessionPtr session) {
  if (!result) {
    OnPresentationEnded();
    std::move(callback).Run(nullptr, nullptr);
    return;
  }

  OnStartPresenting();

  mojom::XRSessionControllerPtr session_controller;
  exclusive_controller_binding_.Bind(mojo::MakeRequest(&session_controller));

  // Use of Unretained is safe because the callback will only occur if the
  // binding is not destroyed.
  exclusive_controller_binding_.set_connection_error_handler(
      base::BindOnce(&AcerXRDevice::OnPresentingControllerMojoConnectionError,
                     base::Unretained(this)));

  std::move(callback).Run(std::move(session), std::move(session_controller));
}

void AcerXRDevice::GetIsolatedXRGamepadProvider(
    mojom::IsolatedXRGamepadProviderRequest provider_request) {
  if (render_loop_->IsRunning()) {
    render_loop_->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&AcerXRRenderLoop::RequestGamepadProvider,
                                  render_loop_->GetWeakPtr(),
                                  std::move(provider_request)));
  } else {
    provider_request_ = std::move(provider_request);
  }
}

// XRSessionController
void AcerXRDevice::SetFrameDataRestricted(bool restricted) {
  // Presentation sessions can not currently be restricted.
  DCHECK(false);
}

void AcerXRDevice::OnPresentingControllerMojoConnectionError() {
  render_loop_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&AcerXRRenderLoop::ExitPresent, render_loop_->GetWeakPtr()));
  render_loop_->Stop();
  OnExitPresent();
  exclusive_controller_binding_.Close();
}

void AcerXRDevice::OnMagicWindowFrameDataRequest(
    mojom::XRFrameDataProvider::GetFrameDataCallback callback) {
  std::move(callback).Run(nullptr);  //[Leo] 20180814 - add VRDisplay
  return;                            //[Leo] 20180814 - add VRDisplay

  if (!acerxr_) {
    std::move(callback).Run(nullptr);
    return;
  }
  const float kPredictionTimeSeconds = 0.03f;
  vr::TrackedDevicePose_t rendering_poses[vr::k_unMaxTrackedDeviceCount];

  acerxr_->GetSystem()->GetDeviceToAbsoluteTrackingPose(
      vr::TrackingUniverseSeated, kPredictionTimeSeconds, rendering_poses,
      vr::k_unMaxTrackedDeviceCount);

  /*vr::TestVRSystem::GetDeviceToAbsoluteTrackingPose(
     vr::TrackingUniverseSeated, kPredictionTimeSeconds, rendering_poses,
     vr::k_unMaxTrackedDeviceCount);*/

  mojom::XRFrameDataPtr data = mojom::XRFrameData::New();
  data->pose = mojo::ConvertTo<mojom::VRPosePtr>(
      rendering_poses[vr::k_unTrackedDeviceIndex_Hmd]);
  std::move(callback).Run(std::move(data));
}

// Only deal with events that will cause displayInfo changes for now.
void AcerXRDevice::OnPollingEvents() {
  return;  //[Leo] 20180814 - add VRDisplay

  if (!acerxr_)
    return;

  vr::VREvent_t event;
  bool is_changed = false;
  while (acerxr_->GetSystem()->PollNextEvent(&event, sizeof(event))) {
    if (event.trackedDeviceIndex != vr::k_unTrackedDeviceIndex_Hmd &&
        event.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid) {
      continue;
    }

    switch (event.eventType) {
      case vr::VREvent_TrackedDeviceUpdated:
      case vr::VREvent_IpdChanged:
      case vr::VREvent_ChaperoneDataHasChanged:
      case vr::VREvent_ChaperoneSettingsHaveChanged:
      case vr::VREvent_ChaperoneUniverseHasChanged:
        is_changed = true;
        break;

      default:
        break;
    }
  }

  if (is_changed)
    SetVRDisplayInfo(CreateVRDisplayInfo(acerxr_->GetSystem(), GetId()));

  main_thread_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::Bind(&AcerXRDevice::OnPollingEvents,
                 weak_ptr_factory_.GetWeakPtr()),
      base::TimeDelta::FromSecondsD(kTimeBetweenPollingEventsSeconds));
}

}  // namespace device
