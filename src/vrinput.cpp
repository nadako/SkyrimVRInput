#include "vrinput.h"

#include "debug.h"
#include "imgui_helper.h"
#include "settings.h"
#include "utils.h"
#include "vrik_fingers.h"

namespace {
	auto &actionDefs = actionData::defs;

	vrnew::IVRInput *vrInput;
	vrnew::VRActiveActionSet_t frameActionSets[actionSetCount];

	vrnew::VRActionHandle_t actionHandles[actionCount];
	uint64_t handSupportedButtons[HandCount]{};
	vrnew::VRActionHandle_t handHapticActions[HandCount]{};
	vrnew::VRActionHandle_t handSkeletonActions[HandCount]{};

	std::vector<uint32_t> actionsPerContext[kContextCount];

	uint32_t lastActionPressedContextStackGeneration[actionCount];
	constexpr uint32_t NotPressedGeneration = 0;

	vrnew::VRControllerState_t controllerStates[HandCount];
	std::mutex controllerStatesMutex; // reads can happen in other threads

	// The game only accumulates held time for the button codes registered in deviceButtons.
	// Without this, every frame would count a held button as a separate press.
	void SetupControllerDeviceButtons() {
		auto manager = RE::BSInputDeviceManager::GetSingleton();
		auto leftController = manager->GetVRControllerLeft();
		auto rightController = manager->GetVRControllerRight();

		leftController->ResetButtonMaps();
		rightController->ResetButtonMaps();

		for (const auto &actionDef: actionDefs) {
			if (actionDef.type != ActionTypeButton) continue;
			auto &control = actionDef.control;

			auto controller = (control.hand == HandLeft) ? leftController : rightController;
			auto &data = static_cast<RE::BSInputDevice *>(controller)->GetRuntimeData();

			if (!data.deviceButtons.contains(control.code)) {
				auto button = std::construct_at(RE::malloc<RE::BSInputDevice::InputButton>());
				button->name = std::format("Button-{}", control.code);
				button->keycode = control.code;
				data.deviceButtons.insert({control.code, button});
				data.buttonNameIDMap.insert({button->name, control.code});
			}
		}
	}

	// TODO: something resets the input device buttons after our Init, and sometimes this happens mid-gameplay too
	// i hack it around with a per-frame check for now, but would be nice to know under what conditions this happens
	// TODO (new): i think this stopped happening after I hooked Prop_TrackingSystemName_String property to always be "oculus", disable, play and see if it ever happens now
	bool AreControllerDeviceButtonsBrokenAgain() {
		auto manager = RE::BSInputDeviceManager::GetSingleton();
		auto controller = manager->GetVRControllerLeft();
		auto &data = static_cast<RE::BSInputDevice *>(controller)->GetRuntimeData();
		if (auto findResult = data.deviceButtons.find(0); findResult != data.deviceButtons.end()) {
			if (findResult->second->name == "system") {
				return true;
			}
		}
		return false;
	}

	bool StoreAction(const char *name, vrnew::VRActionHandle_t *handle) {
		if (auto error = vrInput->GetActionHandle(name, handle); error != vrnew::VRInputError_None) {
			logger::error("IVRInput::GetActionHandle({}) failed: {}", name, (int) error);
			return false;
		}
		return true;
	}

	// i guess this is the not most optimized, but we normally should have like up to 5 contexts, so it shouldn't matter
	uint32_t CalculateContextStackGeneration(std::span<GameInputContext> contextStack) {
		static uint32_t contextStackGeneration = 0;
		static std::vector<GameInputContext> lastStack;
		static std::vector<GameInputContext> currentStack;

		constexpr uint32_t contextStackCapacity = 32; // some sensible amount to reserve vectors for and avoid random allocs
		lastStack.reserve(contextStackCapacity);
		currentStack.reserve(contextStackCapacity);

		currentStack.clear();
		for (auto contextId: contextStack) {
			// for some reason the game creates duplicate Gameplay entries sometimes, probably a bug
			// it shouldn't matter though and we can treat them as one for the sake of stack comparison,
			// but we keep any other duplicates as they can be legit (e.g. MenuMode->MenuMode for confirmation menus, like level-up)
			if (contextId == kContext_Gameplay && !currentStack.empty() && currentStack.back() == kContext_Gameplay) continue;
			currentStack.push_back(contextId);
		}

		if (currentStack != lastStack) {
			lastStack = currentStack;
			contextStackGeneration++;
		}

		return contextStackGeneration;
	}
}

Hand GetHandForTrackedDevice(vrnew::IVRSystem *system, vrnew::TrackedDeviceIndex_t trackedDeviceIndex) {
	auto role = system->GetControllerRoleForTrackedDeviceIndex(trackedDeviceIndex);
	if (role == vrnew::TrackedControllerRole_LeftHand) return HandLeft;
	if (role == vrnew::TrackedControllerRole_RightHand) return HandRight;
	return HandNone;
}

bool InitVRInput() {
	vrInput = (vrnew::IVRInput *) GetRealGenericVRInterface(vrnew::IVRInput_Version);
	if (!vrInput) return false;

	// SteamVR opens the manifest file and the referenced bindings, so it needs a physical path, not MO2 virtualized one
	// that's why we must deploy them into the game folder (with Root Builder or manually)
	char actionManifestPath[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, actionManifestPath);
	strcat_s(actionManifestPath, "\\skyrim_vrinput\\actions.json");

	if (!std::filesystem::exists(actionManifestPath)) {
		logger::error("Action manifest path does not exist. Check installation. ({})", actionManifestPath);
		return false;
	}

	if (auto error = vrInput->SetActionManifestPath(actionManifestPath); error != vrnew::VRInputError_None) {
		logger::error("IVRInput::SetActionManifestPath({}) failed: {}.", actionManifestPath, (int) error);
		return false;
	}

	logger::info("Action manifest set: {}", actionManifestPath);

	auto actionSetNames = actionData::sets;
	for (int i = 0; i < actionSetCount; ++i) {
		vrnew::VRActionSetHandle_t actionSetHandle;
		if (auto error = vrInput->GetActionSetHandle(actionSetNames[i], &actionSetHandle); error != vrnew::VRInputError_None) {
			logger::error("Could not get action set handle {}: error {}", actionSetNames[i], (int) error);
			return false;
		}
		frameActionSets[i] = {.ulActionSet = actionSetHandle};
	}

	for (uint32_t actionIndex = 0; actionIndex < actionCount; ++actionIndex) {
		auto &actionDef = actionDefs[actionIndex];

		if (!StoreAction(actionDef.name, &actionHandles[actionIndex]))
			return false;

		auto &contextActions = actionsPerContext[actionDef.context.contextId];
		contextActions.push_back(actionIndex);

		for (auto &additionalActionContext: actionData::additionalActionContexts) {
			if (strcmp(additionalActionContext.name, actionDef.name) == 0) {
				auto &additionalContextActions = actionsPerContext[additionalActionContext.context];
				additionalContextActions.push_back(actionIndex);
			}
		}

		lastActionPressedContextStackGeneration[actionIndex] = NotPressedGeneration;

		if (actionDef.type == ActionTypeButton || actionDef.type == ActionTypeTouch)
			handSupportedButtons[actionDef.control.hand] |= GetButtonCodeMask(actionDef.control.code);
	}

	StoreAction("/actions/gameplay/out/LeftHaptic", &handHapticActions[HandLeft]);
	StoreAction("/actions/gameplay/out/RightHaptic", &handHapticActions[HandRight]);
	StoreAction("/actions/gameplay/in/RightSkeleton", &handSkeletonActions[HandRight]);
	StoreAction("/actions/gameplay/in/LeftSkeleton", &handSkeletonActions[HandLeft]);

	SetupControllerDeviceButtons();

	logger::info("VR Input setup complete.");
	return true;
}

void UpdateControllerStates() {
	auto &gameContextStack = GetInputContextPriorityStack();
	if (gameContextStack.empty()) return;

	std::span<GameInputContext> contextStack(gameContextStack.data(), gameContextStack.size());
	if (IsImguiActive()) {
		static GameInputContext imguiContextStack[] = {kContext_ImGuiHelper};
		contextStack = imguiContextStack;
	}

	if (AreControllerDeviceButtonsBrokenAgain()) {
		logger::warn("Controller device buttons have been reset externally for some reason, setting them up again...");
		SetupControllerDeviceButtons();
	}

	if (auto error = vrInput->UpdateActionState(frameActionSets, sizeof(frameActionSets[0]), actionSetCount); error != vrnew::VRInputError_None) {
		logger::error("UpdateActionState returned error: {}", (int) error);
		return;
	}

	auto contextStackGeneration = CalculateContextStackGeneration(contextStack);

	// get digital data for all button/touch actions before everything, as we want to store the last input context stack generation in which an action was pressed,
	// to prevent leaking "pressed" states when the context stack changes as a result of an action in a previous context (i.e. a button opening some menu should not
	// be considered pressed when the input context changes for that menu)
	// also, it seems like OpenVR reports bChanged=true if the action was pressed/released, but we didn't call GetDigitalActionData for it...
	static vrnew::InputDigitalActionData_t digitalActionData[actionCount]; // most of actions are digital, so let's have a simple array of digital datas, leaving unused slots for analog actions
	for (int actionIndex = 0; actionIndex < actionCount; actionIndex++) {
		auto &actionDef = actionDefs[actionIndex];
		if (!(actionDef.type == ActionTypeButton || actionDef.type == ActionTypeTouch)) continue;

		auto &data = digitalActionData[actionIndex];
		if (auto error = vrInput->GetDigitalActionData(actionHandles[actionIndex], &data, sizeof(data), vrnew::k_ulInvalidInputValueHandle); error != vrnew::VRInputError_None) {
			logger::error("Error getting button action data for action {}: {}", actionDefs[actionIndex].name, (int) error);
			data.bActive = false;
		}

		if (data.bActive && data.bState) {
			if (lastActionPressedContextStackGeneration[actionIndex] == NotPressedGeneration)
				lastActionPressedContextStackGeneration[actionIndex] = contextStackGeneration;
		} else {
			lastActionPressedContextStackGeneration[actionIndex] = NotPressedGeneration;
		}
	}

	// we could probably bookkeep the real changes, but I don't think it matters
	static uint32_t nextPacketNum = 0;
	uint32_t packetNum = ++nextPacketNum;
	vrnew::VRControllerState_t nextControllerStates[HandCount]{}; // note the {} to zero the memory
	for (auto &state: nextControllerStates) state.unPacketNum = packetNum;

	uint64_t handledStackedActions = 0;
	GameInputContext prevContextId = InvalidInputContext; // invalid value for the first check
	int topContextIndex = (int) contextStack.size() - 1;
	for (int contextIndex = topContextIndex; contextIndex >= 0; contextIndex--) {
		auto contextId = contextStack[contextIndex];

		// sometimes the game has duplicate contexts in the stack for some reason (e.g. double Gameplay or Menu entries), we don't need to handle their actions twice
		if (contextId == prevContextId) continue;
		prevContextId = contextId;

		auto &actionIndices = actionsPerContext[contextId];
		for (auto actionIndex: actionIndices) {
			auto &actionDef = actionDefs[actionIndex];

			if (actionDef.context.stackedId == -1 && contextIndex != topContextIndex) {
				continue; // action is only handled when its context is the top
			}

			// stacked ids start with one, zero means no stacking, so id=1 takes the first bit
			uint64_t handledActionMask = (actionDef.context.stackedId > 0) ? 1ull << (actionDef.context.stackedId - 1) : 0;

			if (handledStackedActions & handledActionMask) {
				continue; // overriding action was handled in an upper context
			}

			auto &actionControl = actionDef.control;
			auto &nextControllerState = nextControllerStates[actionControl.hand];
			auto actionHandle = actionHandles[actionIndex];

			switch (actionDef.type) {
				case ActionTypeButton: {
					auto &data = digitalActionData[actionIndex];
					if (!data.bActive)
						continue;

					if (data.bState) {
						bool wasPressedInThisContext = lastActionPressedContextStackGeneration[actionIndex] == contextStackGeneration;
						if (wasPressedInThisContext) {
							nextControllerState.ulButtonPressed |= GetButtonCodeMask(actionControl.code);
							handledStackedActions |= handledActionMask;
						}
					}

					if (gSettings.logActions && data.bChanged) {
						logger::debug("{} {}", actionDef.name, data.bState ? "pressed" : "released");
						LogInputContextPriorityStack();
					}
					break;
				}
				case ActionTypeTouch: {
					// TODO: touches probably should be treated as global context-less actions
					auto &data = digitalActionData[actionIndex];
					if (!data.bActive)
						continue;

					if (data.bState) {
						nextControllerState.ulButtonTouched |= GetButtonCodeMask(actionControl.code);
						handledStackedActions |= handledActionMask;
					}
					if (gSettings.logActions && data.bChanged) {
						// disabled as it's way too spammy and seems to work well otherwise, so no need to debug for now
						// logger::debug("{} {}", actionDef.name, data.bState ? "touched" : "lifted");
					}
					break;
				}
				case ActionTypeAxis1:
				case ActionTypeAxis2: {
					vrnew::InputAnalogActionData_t data;
					if (auto error = vrInput->GetAnalogActionData(actionHandle, &data, sizeof(data), vrnew::k_ulInvalidInputValueHandle); error != vrnew::VRInputError_None) {
						logger::error("Error getting axis action data for action {}: {}", actionDef.name, (int) error);
						continue;
					}

					if (data.bActive) {
						nextControllerState.rAxis[actionControl.axis] = {data.x, actionDef.type == ActionTypeAxis2 ? data.y : 0.0f};
						handledStackedActions |= handledActionMask;
					}
					break;
				}
			}
		}
	}

	{
		std::lock_guard lock(controllerStatesMutex);
		std::memcpy(controllerStates, nextControllerStates, sizeof(controllerStates));
	}

	if (IsVRIKFingerTrackingEnabled()) {
		UpdateVRIKFingers(vrInput, HandLeft, handSkeletonActions[HandLeft], &nextControllerStates[HandLeft]);
		UpdateVRIKFingers(vrInput, HandRight, handSkeletonActions[HandRight], &nextControllerStates[HandRight]);
	}
}

vrnew::VRControllerState_t GetControllerState(Hand hand) {
	// this can be called from any thread, so there's a lock
	std::lock_guard lock(controllerStatesMutex);
	return controllerStates[hand];
}

uint64_t GetHandSupportedButtons(Hand hand) {
	return handSupportedButtons[hand];
}

void TriggerHapticVibration(Hand hand, std::uint16_t durationMicroSec) {
	auto durationSeconds = (float) durationMicroSec / 1'000'000.0f;

	float frequency = gSettings.hapticFrequency;
	float amplitude = gSettings.hapticMinAmplitude;

	float durationRatio = (float) durationMicroSec / 3999.0f; // apparently the legacy duration is capped at 3999us
	amplitude += durationRatio * gSettings.hapticAddedDurationAmplitudeScale;
	amplitude = std::clamp(amplitude, 0.0f, 1.0f);

	if (frequency > 0 && amplitude > 0)
		vrInput->TriggerHapticVibrationAction(handHapticActions[hand], 0.0f, durationSeconds, frequency, amplitude, vrnew::k_ulInvalidInputValueHandle);
}
