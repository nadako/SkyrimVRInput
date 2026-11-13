#pragma once

#include "actions.h"

// TODO: this is here temporary until I figure out how to move this into a separate plugin
bool IsVRIKFingerTrackingEnabled();
void UpdateVRIKFingers(vrnew::IVRInput *vrInput, Hand hand, vrnew::VRActionHandle_t handSkeleton, vrnew::VRControllerState_t *controllerState);
