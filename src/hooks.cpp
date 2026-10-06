#include "hooks.h"

#include "utils.h"
#include "vrinput.h"
#include "vrik_fingers.h"

namespace {
	constexpr size_t slotIdx_BSOpenVR_ProcessVREvents = 11;
	constexpr size_t slotIdx_IVRSystem_GetInt32TrackedDeviceProperty = 24;
	constexpr size_t slotIdx_IVRSystem_GetUint64TrackedDeviceProperty = 25;
	constexpr size_t slotIdx_IVRSystem_GetStringTrackedDeviceProperty = 28;
	constexpr size_t slotIdx_IVRSystem_GetControllerState = 34;
	constexpr size_t slotIdx_IVRSystem_TriggerHapticPulse = 36;

	typedef void (*pfnProcessVREvents)(RE::BSOpenVR *);
	typedef bool (*pfnGetControllerState)(vrnew::IVRSystem *, vrnew::TrackedDeviceIndex_t, vrnew::VRControllerState_t *, uint32_t);
	typedef void (*pfnTriggerHapticPulse)(vrnew::IVRSystem *, vrnew::TrackedDeviceIndex_t, uint32_t, unsigned short);
	typedef int32_t (*pfnGetInt32TrackedDeviceProperty)(vrnew::IVRSystem *, vrnew::TrackedDeviceIndex_t, vrnew::ETrackedDeviceProperty, vrnew::ETrackedPropertyError *);
	typedef uint64_t (*pfnGetUint64TrackedDeviceProperty)(vrnew::IVRSystem *, vrnew::TrackedDeviceIndex_t, vrnew::ETrackedDeviceProperty, vrnew::ETrackedPropertyError *);
	typedef uint32_t (*pfnGetStringTrackedDeviceProperty)(vrnew::IVRSystem *, vrnew::TrackedDeviceIndex_t, vrnew::ETrackedDeviceProperty, char *, uint32_t, vrnew::ETrackedPropertyError *);

	pfnProcessVREvents origProcessVREvents;
	pfnGetControllerState origGetControllerState;
	pfnTriggerHapticPulse origTriggerHapticPulse;
	pfnGetInt32TrackedDeviceProperty origGetInt32TrackedDeviceProperty;
	pfnGetUint64TrackedDeviceProperty origGetUint64TrackedDeviceProperty;
	pfnGetStringTrackedDeviceProperty origGetStringTrackedDeviceProperty;

	void Hook_ProcessVREvents(RE::BSOpenVR *self) {
		UpdateControllerStates();
		origProcessVREvents(self);
	}

	// NOTE: there is GetControllerStateWithPose, but it doesn't seem to be used by the game, and the mods I checked (SKSE, HIGGS, PLANCK, SkyrimVRTools don't use it or simply forward it), so let's pretend it doesn't exist
	// we also don't hook WaitGetPoses/GetDeviceToAbsoluteTrackingPose/GetLastPoses/etc. as they seem to work just fine with action manifests too, and I don't see any other use for "pose" actions
	bool Hook_GetControllerState(vrnew::IVRSystem *self, vrnew::TrackedDeviceIndex_t unControllerDeviceIndex, vrnew::VRControllerState_t *pControllerState, uint32_t unControllerStateSize) {
		auto hand = GetHandForTrackedDevice(self, unControllerDeviceIndex);
		if (hand != HandNone) {
			*pControllerState = GetControllerState(hand);
			return true;
		}
		return origGetControllerState(self, unControllerDeviceIndex, pControllerState, unControllerStateSize);
	}

	void Hook_TriggerHapticPulse(vrnew::IVRSystem *self, vrnew::TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t axisId, unsigned short usDurationMicroSec) {
		auto hand = GetHandForTrackedDevice(self, unControllerDeviceIndex);
		if (hand != HandNone) {
			// we ignore axisId as I don't think anything other than 0 is ever used. if we find a situation with multiple haptic outputs, we'd need to map axis ids to haptic actions
			TriggerHapticVibration(hand, usDurationMicroSec);
		} else {
			// other device haptics?
			origTriggerHapticPulse(self, unControllerDeviceIndex, axisId, usDurationMicroSec);
		}
	}

	uint64_t Hook_GetUint64TrackedDeviceProperty(vrnew::IVRSystem *self, vrnew::TrackedDeviceIndex_t unDeviceIndex, vrnew::ETrackedDeviceProperty prop, vrnew::ETrackedPropertyError *pError) {
		// the game only fires button events for controller state bits that are reported here
		if (prop == vrnew::Prop_SupportedButtons_Uint64) {
			if (auto hand = GetHandForTrackedDevice(self, unDeviceIndex); hand != HandNone) {
				if (pError) *pError = vrnew::TrackedProp_Success;
				return GetHandSupportedButtons(hand);
			}
		}
		return origGetUint64TrackedDeviceProperty(self, unDeviceIndex, prop, pError);
	}

	int32_t Hook_GetInt32TrackedDeviceProperty(vrnew::IVRSystem *self, vrnew::TrackedDeviceIndex_t unDeviceIndex, vrnew::ETrackedDeviceProperty prop, vrnew::ETrackedPropertyError *pError) {
		if (prop >= vrnew::Prop_Axis0Type_Int32 && prop <= vrnew::Prop_Axis4Type_Int32 && GetHandForTrackedDevice(self, unDeviceIndex) != HandNone) {
			auto axisIndex = prop - vrnew::Prop_Axis0Type_Int32;
			auto value = GetControllerAxisType(axisIndex);
			if (pError) *pError = vrnew::TrackedProp_Success;
			return value;
		}
		return origGetInt32TrackedDeviceProperty(self, unDeviceIndex, prop, pError);
	}

	uint32_t Hook_GetStringTrackedDeviceProperty(vrnew::IVRSystem *self, vrnew::TrackedDeviceIndex_t unDeviceIndex, vrnew::ETrackedDeviceProperty prop, char *pchValue, uint32_t unBufferSize, vrnew::ETrackedPropertyError *pError) {
		if (prop == vrnew::Prop_TrackingSystemName_String && GetHandForTrackedDevice(self, unDeviceIndex) != HandNone) {
			// our fake controllers pretend to be oculus ones, which is the closest Skyrim knows to the modern hardware
			constexpr char value[] = "oculus";
			if (spdlog::should_log(spdlog::level::debug)) {
				char buf[256]{};
				origGetStringTrackedDeviceProperty(self, unDeviceIndex, prop, buf, sizeof(buf), nullptr);
				logger::debug("GetStringTrackedDeviceProperty(TrackingSystemName) requested, original API returned `{}`, we override with `{}`", buf, value);
			}
			if (pchValue && unBufferSize >= sizeof(value)) {
				memcpy(pchValue, value, sizeof(value));
				if (pError) *pError = vrnew::TrackedProp_Success;
			} else {
				if (pError) *pError = vrnew::TrackedProp_BufferTooSmall;
			}
			return sizeof(value);
		}
		return origGetStringTrackedDeviceProperty(self, unDeviceIndex, prop, pchValue, unBufferSize, pError);
	}
}

bool InstallHooks() {
	// Skyrim VR works with IVRSystem_019, however with SKSEVR, what it actually gets is a wrapper over IVRSystem_020.
	// This means that every SKSE plugin down the line actually calls that wrapper and never the real thing (well, unless
	// of course it explicitly bypasses that, of which I'm unaware of). Now, if we hook the vtable of the IVRSystem_020,
	// we make everyone, including SKSEVR wrapper use our hooks, which is perfect for the use case.
	auto vrSystem = (vrnew::IVRSystem *) GetRealGenericVRInterface(vrnew::IVRSystem_Version);
	if (!vrSystem) return false;

	// hook state update method
	REL::Relocation<uintptr_t> vtable_BSOpenVR{RE::VTABLE_BSOpenVR[0]};
	origProcessVREvents = InstallHook(vtable_BSOpenVR, slotIdx_BSOpenVR_ProcessVREvents, &Hook_ProcessVREvents, "BSOpenVR::ProcessVREvents");

	// hook legacy API getters
	REL::Relocation<uintptr_t> vtable_vrSystem{*((uintptr_t *) vrSystem)};
	origGetControllerState = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_GetControllerState, &Hook_GetControllerState, "IVRSystem::GetControllerState");
	origTriggerHapticPulse = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_TriggerHapticPulse, &Hook_TriggerHapticPulse, "IVRSystem::TriggerHapticPulse");
	origGetInt32TrackedDeviceProperty = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_GetInt32TrackedDeviceProperty, &Hook_GetInt32TrackedDeviceProperty, "IVRSystem::GetInt32TrackedDeviceProperty");
	origGetUint64TrackedDeviceProperty = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_GetUint64TrackedDeviceProperty, &Hook_GetUint64TrackedDeviceProperty, "IVRSystem::GetUint64TrackedDeviceProperty");
	origGetStringTrackedDeviceProperty = InstallHook(vtable_vrSystem, slotIdx_IVRSystem_GetStringTrackedDeviceProperty, &Hook_GetStringTrackedDeviceProperty, "IVRSystem::GetStringTrackedDeviceProperty");

	return true;
}
