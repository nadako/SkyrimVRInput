#pragma once

#include "actions.h"

bool InitVRInput();
void UpdateControllerStates();
vrnew::VRControllerState_t GetControllerState(Hand hand);
bool IsOrphanControllerButtonRelease(ControllerColumn column, uint32_t code);
bool IsPreventedMenuUserEventControllerButton(ControllerColumn column, uint32_t code);
uint64_t GetHandSupportedButtons(Hand hand);
Hand GetHandForTrackedDevice(vrnew::IVRSystem *system, vrnew::TrackedDeviceIndex_t trackedDeviceIndex);
ControllerColumn GetControllerColumnForInputDevice(RE::INPUT_DEVICE device);
void TriggerHapticVibration(Hand hand, std::uint16_t durationMicroSec);
