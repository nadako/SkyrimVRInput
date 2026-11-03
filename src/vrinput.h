#pragma once

#include "actions.h"

bool InitVRInput();
void UpdateControllerStates();
vrnew::VRControllerState_t GetControllerState(Hand hand);
bool IsOrphanControllerButtonRelease(Hand hand, uint32_t code);
uint64_t GetHandSupportedButtons(Hand hand);
Hand GetHandForTrackedDevice(vrnew::IVRSystem *system, vrnew::TrackedDeviceIndex_t trackedDeviceIndex);
Hand GetHandForInputDevice(RE::INPUT_DEVICE device);
void TriggerHapticVibration(Hand hand, std::uint16_t durationMicroSec);
