#pragma once

#include "actions.h"

// TODO: this is here temporary until I figure out how to move this into a separate plugin
// TODO: note in the description that we need version 0.8.8+
bool IsVRIKFingerTrackingEnabled();
void UpdateVRIKFingers(vrnew::IVRInput *vrInput, Hand hand, vrnew::VRActionHandle_t handSkeleton, vrnew::VRControllerState_t *controllerState);
