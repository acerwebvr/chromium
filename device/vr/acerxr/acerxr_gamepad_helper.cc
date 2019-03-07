// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device/vr/acerxr/acerxr_gamepad_helper.h"

#include <memory>

#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "device/gamepad/public/cpp/gamepads.h"
#include "device/vr/vr_device.h"
#include "third_party/openvr/src/headers/openvr.h"
#include "ui/gfx/transform.h"
#include "ui/gfx/transform_util.h"


namespace device {

namespace {

mojom::XRGamepadButtonPtr GetGamepadButton(
	ControllerData& controllerData,
    int button_id) {
  uint64_t button_mask = CONTROLLER_MASK;
  if ((button_id & button_mask) != 0) {
    auto ret = mojom::XRGamepadButton::New();
    bool button_pressed = (controllerData.buttonPressed & button_id) != 0;
    bool button_touched = (controllerData.buttonTouched & button_id) != 0;
    ret->touched = button_touched;
    ret->pressed = button_pressed;
    if (button_id == CONTROLLER_SELECTED) {
      ret->value = button_pressed ? controllerData.selectPressedValue : 0.0;
	} else {
      ret->value = button_pressed ? 1.0 : 0.0;
    }
    return ret;
  }

  return nullptr;
}

}  // namespace

mojom::XRGamepadDataPtr AcerXRGamepadHelper::GetGamepadData(std::list<ControllerData> controllerDataList) {
  mojom::XRGamepadDataPtr ret = mojom::XRGamepadData::New();

  std::list<ControllerData>::iterator it;
  for (it = controllerDataList.begin(); it != controllerDataList.end(); it++) {
    ControllerData& controllerData = *it;
	auto gamepad = mojom::XRGamepad::New();
    gamepad->controller_id = controllerData.id;

	switch (controllerData.handedness) {
      case LEFT_CONTROLLER:
        gamepad->hand = device::mojom::XRHandedness::LEFT;
        break;
      case RIGHT_CONTROLLER:
        gamepad->hand = device::mojom::XRHandedness::RIGHT;
        break;
      default:
        gamepad->hand = device::mojom::XRHandedness::NONE;
        break;
    }


    gamepad->axes.push_back(controllerData.Touchpad[0]);
    gamepad->axes.push_back(controllerData.Touchpad[1]);
    auto button = GetGamepadButton(controllerData, CONTROLLER_TOUCHPAD);
    if (button) {
      gamepad->buttons.push_back(std::move(button));
    }

    gamepad->axes.push_back(controllerData.selectPressedValue);
    button = GetGamepadButton(controllerData, CONTROLLER_SELECTED);
    if (button) {
      gamepad->buttons.push_back(std::move(button));
    }	

    //gamepad->axes.push_back(controllerData.Thumbstick[0]);
    //gamepad->axes.push_back(controllerData.Thumbstick[1]);
    //button = GetGamepadButton(controllerData, CONTROLLER_THUMBSTICK);
    //if (button) {
    //  gamepad->buttons.push_back(std::move(button));
    //}

    button = GetGamepadButton(controllerData, CONTROLLER_GRASPED);
    if (button) {
      gamepad->buttons.push_back(std::move(button));
    }

	button = GetGamepadButton(controllerData, CONTROLLER_MENU);
    if (button) {
      gamepad->buttons.push_back(std::move(button));
    }

	auto dst_pose = mojom::VRPose::New();
    dst_pose->orientation = std::vector<float>(
		{controllerData.pose.rot_x, controllerData.pose.rot_y, controllerData.pose.rot_z, controllerData.pose.rot_w});
    dst_pose->position = std::vector<float>(
		{controllerData.pose.pos_x, controllerData.pose.pos_y, controllerData.pose.pos_z});
    dst_pose->angularVelocity =
        std::vector<float>({controllerData.vel[3], controllerData.vel[4], controllerData.vel[5]});
    dst_pose->linearVelocity = std::vector<float>(
        {controllerData.vel[0], controllerData.vel[1], controllerData.vel[2]});

    gamepad->pose = std::move(dst_pose);

	ret->gamepads.push_back(std::move(gamepad));
  }

  return ret;
}

}  // namespace device
