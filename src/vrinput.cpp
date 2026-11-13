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
	uint64_t controllerSupportedButtons[ConCount]{};
	vrnew::VRActionHandle_t handHapticActions[HandCount]{};
	vrnew::VRActionHandle_t handSkeletonActions[HandCount]{};

	std::vector<uint32_t> actionsPerContext[kContextCount];

	uint32_t lastActionPressedContextStackGeneration[actionCount];
	constexpr uint32_t NotPressedGeneration = 0;

	vrnew::VRControllerState_t controllerStates[ConCount];
	std::mutex controllerStatesMutex; // reads can happen in other threads

	// we track "orphan" button releases, when a button action is still physically pressed,
	// but stops contributing to ulButtonPressed due to input context changes.
	// we use this data in the MenuControls::ProcessEvent hook to zero the "up" events for
	// such buttons so they don't have any effect (i.e. Map Mode "Click" release being handled as mouse hover)
	uint64_t orphanControllerButtonReleases[ConCount]{};
	bool lastEffectivePressedButtonActions[actionCount]{};

	uint64_t preventedMenuUserEventControllerButtons[ConCount]{};

	bool isDMEEnabled = false;

	// CommonLibSSE-NG uses the right/left terminology, but really it's primary/secondary, so I encapsulate this
	RE::BSTrackedControllerDevice* GetPrimaryController() { return RE::BSInputDeviceManager::GetSingleton()->GetVRControllerRight(); }
	RE::BSTrackedControllerDevice* GetSecondaryController() { return RE::BSInputDeviceManager::GetSingleton()->GetVRControllerLeft(); }

	void RegisterControllerDeviceButton(RE::BSTrackedControllerDevice *controller, uint8_t code) {
		auto &data = static_cast<RE::BSInputDevice *>(controller)->GetRuntimeData();
		if (data.deviceButtons.contains(code)) return;

		auto button = std::construct_at(RE::malloc<RE::BSInputDevice::InputButton>());
		button->name = std::format("Button-{}", code);
		button->keycode = code;
		data.deviceButtons.insert({code, button});
		data.buttonNameIDMap.insert({button->name, code});
	}

	// The game only accumulates held time for the button codes registered in deviceButtons.
	// Without this, every frame would count a held button as a separate press.
	// TODO: move this code into the InitVRInput actionDefs loop (next to controllerSupportedButtons setup)
	//  if it's well-tested that we never need to setup again after init (this is only in a separate function
	//  because there was a bug, now should be fixed by ForceOculusControllerDevices)
	void SetupControllerDeviceButtons() {
		auto primaryController = GetPrimaryController();
		auto secondaryController = GetSecondaryController();

		primaryController->ResetButtonMaps();
		secondaryController->ResetButtonMaps();

		for (const auto &actionDef: actionDefs) {
			if (actionDef.type != ActionTypeButton) continue;
			auto &control = actionDef.control;

			bool bothControllers = actionDef.flags & ActionFlagSwapInLeftHandedMode;
			if (bothControllers || control.column == ConPrimary) RegisterControllerDeviceButton(primaryController, control.code);
			if (bothControllers || control.column == ConSecondary) RegisterControllerDeviceButton(secondaryController, control.code);
		}
	}

	// TODO: https://github.com/alandtse/CommonLibSSE-NG/pull/508
	void RefreshVRControllerDevices(RE::BSInputDeviceManager *manager) {
		using func_t = void (*)(RE::BSInputDeviceManager *);
		static REL::Relocation<func_t> func{ REL::Offset(0xC51D30) };
		func(manager);
	}

	void ForceOculusControllerDevices() {
		auto openVr = RE::BSOpenVR::GetSingleton();
		logger::info("Forcing HMD Device Type to Oculus ({}) (currently {})", (int) RE::BSVRInterface::HMDDeviceType::kOculus, (int) openVr->hmdDeviceType);
		openVr->hmdDeviceType = RE::BSVRInterface::HMDDeviceType::kOculus;
		RefreshVRControllerDevices(RE::BSInputDeviceManager::GetSingleton());
	}

	// TODO: this is here for debugging. we force hmd device type early so it shouldn't really change, but in case it does - i would like to know about it...
	void CheckHmdDeviceTypeIsOculus() {
		static auto lastHmdDeviceType = RE::BSVRInterface::HMDDeviceType::kOculus;

		auto hmdDeviceType = RE::BSOpenVR::GetSingleton()->GetHMDDeviceType();
		if (hmdDeviceType != lastHmdDeviceType) {
			logger::warn("HMD Device Type changed from {} to {}, should be Oculus ({})", (int) lastHmdDeviceType, (int) hmdDeviceType, (int) RE::BSVRInterface::HMDDeviceType::kOculus);
			lastHmdDeviceType = hmdDeviceType;
		}
	}

	bool IsLeftHandedMode() {
		// CommonLib's RE::BSTrackedControllerDevice runtime data is wrong: it's put at 0x78 but there's a bogus uint64_t unk70 that shifts the data by 8 bytes
		// TODO: contribute the fix to CommonlibSSE-NG
		struct TrackedControllerDevice {
			uint8_t _pad[0x78];
			RE::ControllerDeviceHand hand;
		};

		// the RefreshVRControllerDevices method sets the "hand" field to left/right according to the game setting,
		// and once that happens the handedness comes into effect (the game will poll tracked devices with respective roles)
		auto primaryController = (TrackedControllerDevice*)GetPrimaryController();
		return primaryController->hand == RE::ControllerDeviceHand::kLeft;
	}

	ControllerColumn GetActionControllerColumn(const ActionDef &actionDef, bool leftHandedMode) {
		bool swap = leftHandedMode && (actionDef.flags & ActionFlagSwapInLeftHandedMode);
		if (!swap) return actionDef.control.column;
		else return actionDef.control.column == ConPrimary ? ConSecondary : ConPrimary;
	}

	RE::INPUT_DEVICE GetControllerInputDevice(RE::BSTrackedControllerDevice *controller) {
		return static_cast<RE::BSInputDevice *>(controller)->GetRuntimeData().device;
	}

	bool StoreAction(const char *name, vrnew::VRActionHandle_t *handle) {
		if (auto error = vrInput->GetActionHandle(name, handle); error != vrnew::VRInputError_None) {
			logger::error("IVRInput::GetActionHandle({}) failed: {}", name, (int) error);
			return false;
		}
		return true;
	}

	bool GetAnalogActionData(uint32_t actionIndex, vrnew::InputAnalogActionData_t *data) {
		if (auto error = vrInput->GetAnalogActionData(actionHandles[actionIndex], data, sizeof(*data), vrnew::k_ulInvalidInputValueHandle); error != vrnew::VRInputError_None) {
			logger::error("Error getting axis action data for action {}: {}", actionData::defs[actionIndex].name, (int) error);
			return false;
		}
		return true;
	}

	bool GetOriginInfo(vrnew::VRInputValueHandle_t origin, vrnew::InputOriginInfo_t *info) {
		if (auto error = vrInput->GetOriginTrackedDeviceInfo(origin, info, sizeof(*info)); error != vrnew::VRInputError_None) {
			logger::error("IVRInput::GetOriginTrackedDeviceInfo({}) failed: {}", origin, (int) error);
			return false;
		}
		return true;
	}

	bool IsMovementActionOrigin(vrnew::VRInputValueHandle_t origin, const vrnew::InputDigitalActionData_t *digitalActionData) {
		vrnew::InputOriginInfo_t originInfo;
		if (!GetOriginInfo(origin, &originInfo)) return false;

		for (auto actionIndex : actionData::movementActionIndices) {
			bool isActive = false;
			vrnew::VRInputValueHandle_t movementOrigin = vrnew::k_ulInvalidInputValueHandle;

			switch (actionData::defs[actionIndex].type) {
				case ActionTypeButton:
				case ActionTypeTouch: {
					isActive = digitalActionData[actionIndex].bActive;
					movementOrigin = digitalActionData[actionIndex].activeOrigin;
					break;
				}
				case ActionTypeAxis1:
				case ActionTypeAxis2: {
					vrnew::InputAnalogActionData_t data;
					if (GetAnalogActionData(actionIndex, &data)) {
						isActive = data.bActive;
						movementOrigin = data.activeOrigin;
					}
					break;
				}
			}

			vrnew::InputOriginInfo_t movementOriginInfo;
			if (isActive && GetOriginInfo(movementOrigin, &movementOriginInfo)) {
				// i didn't find a better way to compare origins, because SteamVR reports different origin handles for the same physical button/stick...
				auto &a = originInfo, &b = movementOriginInfo;
				if (a.devicePath == b.devicePath && strcmp(a.rchRenderModelComponentName, b.rchRenderModelComponentName) == 0) {
					return true;
				}
			}
		}

		return false;
	}

	bool IsInheritedContext(GameInputContext context, GameInputContext parent) {
		for (auto &inheritance: actionData::inheritedContexts) {
			if (inheritance.context == context && inheritance.parent == parent) return true;
		}
		return false;
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
			if (!currentStack.empty() && IsInheritedContext(contextId, currentStack.back())) continue;
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

ControllerColumn GetControllerColumnForInputDevice(RE::INPUT_DEVICE device) {
	if (GetControllerInputDevice(GetPrimaryController()) == device) return ConPrimary;
	if (GetControllerInputDevice(GetSecondaryController()) == device) return ConSecondary;
	return ConNone;
}

bool InitVRInput() {
	vrInput = (vrnew::IVRInput *) GetRealGenericVRInterface(vrnew::IVRInput_Version);
	if (!vrInput) return false;

	// SteamVR opens the manifest file and the referenced bindings, so it needs a physical path, not MO2 virtualized one
	// that's why we must deploy them into the game folder (with Root Builder or manually)
	constexpr char actionManifestRelativePath[] = "\\skyrim_vrinput\\actions.json";
	char actionManifestPath[MAX_PATH + sizeof(actionManifestRelativePath)];
	GetCurrentDirectory(MAX_PATH, actionManifestPath);
	strcat_s(actionManifestPath, actionManifestRelativePath);

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
	for (uint32_t i = 0; i < actionSetCount; ++i) {
		vrnew::VRActionSetHandle_t actionSetHandle;
		if (auto error = vrInput->GetActionSetHandle(actionSetNames[i], &actionSetHandle); error != vrnew::VRInputError_None) {
			logger::error("Could not get action set handle {}: error {}", actionSetNames[i], (int) error);
			return false;
		}
		frameActionSets[i] = {.ulActionSet = actionSetHandle};
	}

	constexpr auto DMEPluginModuleName = "DialogueMovementEnabler.dll";
	isDMEEnabled = GetModuleHandleA(DMEPluginModuleName) != nullptr;
	if (isDMEEnabled) logger::info("{} detected, enabling movement actions in dialogue", DMEPluginModuleName);

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

		if (actionDef.context.topOnly) {
			for (auto &inheritance: actionData::inheritedContexts) {
				if (inheritance.parent == actionDef.context.contextId) {
					actionsPerContext[inheritance.context].push_back(actionIndex);
				}
			}
		}

		if (isDMEEnabled && actionDef.flags & ActionFlagMovement) {
			auto &additionalContextActions = actionsPerContext[kContext_DialogueMenu];
			additionalContextActions.push_back(actionIndex);
		}

		lastActionPressedContextStackGeneration[actionIndex] = NotPressedGeneration;

		if (actionDef.type == ActionTypeButton || actionDef.type == ActionTypeTouch) {
			auto mask = GetButtonCodeMask(actionDef.control.code);
			// since the action can appear in either controller at run-time (depending on left-handed mode), we simply register the button for both
			if (actionDef.flags & ActionFlagSwapInLeftHandedMode) {
				controllerSupportedButtons[ConPrimary] |= mask;
				controllerSupportedButtons[ConSecondary] |= mask;
			} else {
				controllerSupportedButtons[actionDef.control.column] |= mask;
			}
		}
	}

	StoreAction("/actions/gameplay/out/LeftHaptic", &handHapticActions[HandLeft]);
	StoreAction("/actions/gameplay/out/RightHaptic", &handHapticActions[HandRight]);
	StoreAction("/actions/gameplay/in/RightSkeleton", &handSkeletonActions[HandRight]);
	StoreAction("/actions/gameplay/in/LeftSkeleton", &handSkeletonActions[HandLeft]);

	ForceOculusControllerDevices();
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

	CheckHmdDeviceTypeIsOculus();

	bool leftHandedMode = IsLeftHandedMode();

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
	for (uint32_t actionIndex = 0; actionIndex < actionCount; actionIndex++) {
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
	vrnew::VRControllerState_t nextControllerStates[ConCount]{}; // note the {} to zero the memory
	for (auto &state: nextControllerStates) state.unPacketNum = packetNum;

	bool effectivePressedButtonActions[actionCount]{};
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

			if (actionDef.context.topOnly && contextIndex != topContextIndex) {
				continue; // action is only handled when its context is the top
			}

			// stacked ids start with one, zero means no stacking, so id=1 takes the first bit
			uint64_t handledActionMask = (actionDef.context.stackedId > 0) ? 1ull << (actionDef.context.stackedId - 1) : 0;

			if (handledStackedActions & handledActionMask) {
				continue; // overriding action was handled in an upper context
			}

			auto &actionControl = actionDef.control;
			auto &nextControllerState = nextControllerStates[GetActionControllerColumn(actionDef, leftHandedMode)];

			switch (actionDef.type) {
				case ActionTypeButton: {
					auto &data = digitalActionData[actionIndex];
					if (!data.bActive)
						continue;

					if (data.bState) {
						// when Dialogue Movement Enabler VR is enabled we allow move/turn actions in the dialogue menu,
						// so if the menu nav button actions are bound to the same physical component as movement (e.g. sticks),
						// we need to ignore them to prevent active dialogue item switching when moving/turning
						if (isDMEEnabled) {
							bool isMenuNavAction = actionDef.flags & ActionFlagMenuNav;
							if (isMenuNavAction && contextId == kContext_DialogueMenu && IsMovementActionOrigin(data.activeOrigin, digitalActionData)) {
								continue;
							}
						}

						bool wasPressedInThisContext = lastActionPressedContextStackGeneration[actionIndex] == contextStackGeneration;
						if (wasPressedInThisContext) {
							nextControllerState.ulButtonPressed |= GetButtonCodeMask(actionControl.code);
							handledStackedActions |= handledActionMask;
							effectivePressedButtonActions[actionIndex] = true;
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
						logger::trace("{} {}", actionDef.name, data.bState ? "touched" : "lifted");
					}
					break;
				}
				case ActionTypeAxis1:
				case ActionTypeAxis2: {
					vrnew::InputAnalogActionData_t data;
					if (!GetAnalogActionData(actionIndex, &data)) continue;

					if (data.bActive) {
						nextControllerState.rAxis[actionControl.axis] = {data.x, actionDef.type == ActionTypeAxis2 ? data.y : 0.0f};
						handledStackedActions |= handledActionMask;
					}
					break;
				}
			}
		}
	}

	uint64_t nextOrphanControllerButtonReleases[ConCount]{};
	uint64_t nextPreventedMenuUserEventControllerButtons[ConCount]{};
	for (uint32_t actionIndex = 0; actionIndex < actionCount; actionIndex++) {
		auto &actionDef = actionDefs[actionIndex];
		if (actionDef.type == ActionTypeButton) {
			bool buttonWasPressed = lastEffectivePressedButtonActions[actionIndex];
			bool buttonIsPressed = effectivePressedButtonActions[actionIndex];
			lastEffectivePressedButtonActions[actionIndex] = effectivePressedButtonActions[actionIndex];

			if ((actionDef.flags & ActionFlagPreventMenuUserEvents) && (buttonWasPressed || buttonIsPressed)) {
				nextPreventedMenuUserEventControllerButtons[GetActionControllerColumn(actionDef, leftHandedMode)] |= GetButtonCodeMask(actionDef.control.code);
			}

			// if the steamvr action is still pressed, but its controller state button is not - mark it as an "orphan" release
			bool actionIsPressed = digitalActionData[actionIndex].bActive && digitalActionData[actionIndex].bState;
			if (buttonWasPressed && !buttonIsPressed && actionIsPressed) {
				nextOrphanControllerButtonReleases[GetActionControllerColumn(actionDef, leftHandedMode)] |= GetButtonCodeMask(actionDef.control.code);
			}
		}
	}

	{
		std::lock_guard lock(controllerStatesMutex);
		std::memcpy(controllerStates, nextControllerStates, sizeof(controllerStates));
		std::memcpy(orphanControllerButtonReleases, nextOrphanControllerButtonReleases, sizeof(orphanControllerButtonReleases));
		std::memcpy(preventedMenuUserEventControllerButtons, nextPreventedMenuUserEventControllerButtons, sizeof(preventedMenuUserEventControllerButtons));
	}

	if (IsVRIKFingerTrackingEnabled()) {
		// controller states hold "button touched" for primary/secondary. whether those are left or right depend on the left-handed mode
		auto rightControllerState = &nextControllerStates[GetControllerColumnForHand(HandRight, leftHandedMode)];
		auto leftControllerState = &nextControllerStates[GetControllerColumnForHand(HandLeft, leftHandedMode)];
		UpdateVRIKFingers(vrInput, HandRight, handSkeletonActions[HandRight], rightControllerState);
		UpdateVRIKFingers(vrInput, HandLeft, handSkeletonActions[HandLeft], leftControllerState);
	}
}

vrnew::VRControllerState_t GetControllerState(Hand hand) {
	// this can be called from any thread, so there's a lock
	std::lock_guard lock(controllerStatesMutex);
	auto column = GetControllerColumnForHand(hand, IsLeftHandedMode());
	return controllerStates[column];
}

bool IsOrphanControllerButtonRelease(ControllerColumn column, uint32_t code) {
	// this is likely never called from a different thread than UpdateControllerStates one, unless some mod
	// does crazy things with MenuControls::ProcessEvent, but the lock should be harmless, so let's keep it
	std::lock_guard lock(controllerStatesMutex);
	return (orphanControllerButtonReleases[column] & GetButtonCodeMask(code)) != 0;
}

bool IsPreventedMenuUserEventControllerButton(ControllerColumn column, uint32_t code) {
	// similarly to IsOrphanControllerButtonRelease, there's likely no thread safety issues, but the lock should be harmless nevertheless
	std::lock_guard lock(controllerStatesMutex);
	return (preventedMenuUserEventControllerButtons[column] & GetButtonCodeMask(code)) != 0;
}

uint64_t GetHandSupportedButtons(Hand hand) {
	auto column = GetControllerColumnForHand(hand, IsLeftHandedMode());
	return controllerSupportedButtons[column];
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
