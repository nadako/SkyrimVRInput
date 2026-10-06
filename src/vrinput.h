#pragma once

#include "actions.h"

bool InitVRInput();
void UpdateControllerStates();
vrnew::VRControllerState_t GetControllerState(Hand hand);
uint64_t GetHandSupportedButtons(Hand hand);
Hand GetHandForTrackedDevice(vrnew::IVRSystem *system, vrnew::TrackedDeviceIndex_t trackedDeviceIndex);
void TriggerHapticVibration(Hand hand, std::uint16_t durationMicroSec);
