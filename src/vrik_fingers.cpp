#include "vrik_fingers.h"
#include "mod_interfaces.h"
#include "settings.h"
#include "vrinput.h"

namespace {
	bool areHandFingersDriven[HandCount];

	bool GetSkeletalData(vrnew::IVRInput *vrInput, Hand hand, vrnew::VRActionHandle_t handSkeleton, vrnew::VRSkeletalSummaryData_t *pOutData) {
		vrnew::InputSkeletalActionData_t actionData;
		if (auto error = vrInput->GetSkeletalActionData(handSkeleton, &actionData, sizeof(actionData)); error != vrnew::VRInputError_None) {
			logger::error("IVRInput::GetSkeletalActionData({}) failed: {}", hand == HandLeft ? "<left hand>" : "<right hand>", (int) error);
			return false;
		}

		if (!actionData.bActive) {
			return false;
		}

		if (auto error = vrInput->GetSkeletalSummaryData(handSkeleton, vrnew::VRSummaryType_FromAnimation, pOutData); error != vrnew::VRInputError_None) {
			logger::error("IVRInput::GetSkeletalSummaryData({}) failed: {}", hand == HandLeft ? "<left hand>" : "<right hand>", (int) error);
			return false;
		}

		return true;
	}

	bool IsCasting(RE::Actor *actor, bool isLeftHand) {
		// stolen from HIGGS
		static RE::BSFixedString IsCastingDual("IsCastingDual");
		static RE::BSFixedString IsCastingRight("IsCastingRight");
		static RE::BSFixedString IsCastingLeft("IsCastingLeft");
		bool result = false;
		actor->GetGraphVariableBool(IsCastingDual, result);
		if (!result) {
			if (isLeftHand) actor->GetGraphVariableBool(IsCastingLeft, result);
			else actor->GetGraphVariableBool(IsCastingRight, result);
		}
		return result;
	}

	bool CheckFingerGameConditions(bool isLeftHand) {
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) return false;

		// refuse if we have an equipped object, hand pose would be controlled by the game
		// TODO: disabled for now, not sure if it's something we want, because VRIK doesn't prevent finger tracking on equip too
		// bool hasEquippedObject = RE::PlayerCharacter::GetSingleton()->GetEquippedObject(isLeftHand) != nullptr;
		// if (hasEquippedObject) return false;

		// refuse if we have a HIGGS-grabbed object, because HIGGS will animate the fingers
		auto higgs = ModInterfaces::higgs;
		if (higgs && higgs->IsHoldingObject(isLeftHand)) return false;

		// refuse if the hand is casting a spell
		if (IsCasting(player, isLeftHand)) return false;

		return true;
	}
}

bool IsVRIKFingerTrackingEnabled() {
	return ModInterfaces::vrik != nullptr && gSettings.enableVrikFingerTracking;
}

void UpdateVRIKFingers(vrnew::IVRInput *vrInput, Hand hand, vrnew::VRActionHandle_t handSkeleton, vrnew::VRControllerState_t *controllerState) {
	if (handSkeleton == vrnew::k_ulInvalidActionHandle) return;

	auto vrik = ModInterfaces::vrik;
	if (!vrik) return;

	vrnew::VRSkeletalSummaryData_t skeletalData{};
	bool shouldDriveFingers = GetSkeletalData(vrInput, hand, handSkeleton, &skeletalData);

	bool isLeftHand = hand == HandLeft;

	if (shouldDriveFingers) {
		shouldDriveFingers = CheckFingerGameConditions(isLeftHand);
	}

	if (!shouldDriveFingers) {
		if (areHandFingersDriven[hand]) {
			areHandFingersDriven[hand] = false;
			vrik->restoreFingers(isLeftHand);
		}
		return;
	}

	// finally we are sure we want to drive the fingers \o/
	areHandFingersDriven[hand] = true;

	// convert the ranges (in SteamVR 0 is straight, 1 is curved, in VRIK it's inverted)
	float positions[vrnew::VRFinger_Count];
	for (int fingerIndex = 0; fingerIndex < vrnew::VRFinger_Count; ++fingerIndex) {
		positions[fingerIndex] = 1.0f - skeletalData.flFingerCurl[fingerIndex];
	}

	// TODO: this is a rather simple "fix" that will work for now, but ideally we should have configurable min/max remapping
	// which would help with both "curved without pressing" and "fully open, but not detected as such". check there's something on the SteamVR level
	if (controllerState->ulButtonTouched & GetButtonCodeMask(KnownButtons::Joystick)) positions[0] = 0.0f;
	if (controllerState->ulButtonTouched & GetButtonCodeMask(KnownButtons::Trigger)) positions[1] = 0.0f;
	if (controllerState->ulButtonTouched & GetButtonCodeMask(KnownButtons::Grip)) positions[2] = 0.0f;

	vrik->setFingerRange(isLeftHand, positions[0], positions[0], positions[1], positions[1], positions[2], positions[2], positions[3], positions[3], positions[4], positions[4]);
}
