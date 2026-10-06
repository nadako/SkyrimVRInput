#include "debug.h"

#include "actions.h"

namespace {
	class ButtonEventLogger : public RE::BSTEventSink<RE::InputEvent *> {
	public:
		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent *const*firstEvent, RE::BSTEventSource<RE::InputEvent *> *) override {
			for (auto event = firstEvent ? *firstEvent : nullptr; event; event = event->next) {
				if (auto button = event->AsButtonEvent(); button) {
					if (button->IsDown() || button->IsUp()) {
						logger::debug("ButtonEvent ({}): device={} code={:#x} userEvent='{}'",
						             button->IsDown() ? "down" : "up", (int) event->GetDevice(), button->GetIDCode(), button->GetUserEvent().c_str());
						LogInputContextPriorityStack();
					}
				} else if (auto stick = event->AsThumbstickEvent(); stick) {
					logger::debug("ThumbstickEvent: device={} code={:#x} userEvent='{}' x={:.2f} y={:.2f}",
					             (int) event->GetDevice(), stick->GetIDCode(), stick->QUserEvent().c_str(), stick->xValue, stick->yValue);
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	void LogDeviceButtons(RE::BSInputDevice *controller) {
		logger::debug("--- controller {}", (int) controller->device);
		auto &data = controller->GetRuntimeData();
		for (auto &[code, button]: data.deviceButtons) {
			logger::debug("button(code={}, keycode={}, name={})", code, button->keycode, button->name.c_str());
		}
	}
}

void AddInputEventLogger() {
	static ButtonEventLogger logger;
	RE::BSInputDeviceManager::GetSingleton()->AddEventSink(&logger);
}

void LogControllerDeviceButtons() {
	auto manager = RE::BSInputDeviceManager::GetSingleton();
	auto leftController = manager->GetVRControllerLeft();
	auto rightController = manager->GetVRControllerRight();
	LogDeviceButtons(leftController);
	LogDeviceButtons(rightController);
}

std::string InputContextIdToString(GameInputContext contextId) {
	switch (contextId) {
		case kContext_Gameplay: return "Gameplay";
		case kContext_MenuMode: return "MenuMode";
		case kContext_Console: return "Console";
		case kContext_ItemMenu: return "ItemMenu";
		case kContext_Inventory: return "Inventory";
		case kContext_DebugText: return "DebugText";
		case kContext_Favorites: return "Favorites";
		case kContext_Map: return "Map";
		case kContext_Stats: return "Stats";
		case kContext_Cursor: return "Cursor";
		case kContext_Book: return "Book";
		case kContext_DebugOverlay: return "DebugOverlay";
		case kContext_Journal: return "Journal";
		case kContext_TFCMode: return "TFCMode";
		case kContext_MapDebug: return "MapDebug";
		case kContext_Lockpicking: return "Lockpicking";
		case kContext_Favor: return "Favor";
		case kContext_CraftingMenu: return "CraftingMenu";
		case kContext_BarterMenu: return "BarterMenu";
		case kContext_RaceSexMenu: return "RaceSexMenu";
		case kContext_DialogueMenu: return "DialogueMenu";
		default: return std::format("<unknown {}>", (uint32_t) contextId);
	}
}

void LogInputContextPriorityStack() {
	auto &contexts = GetInputContextPriorityStack();
	logger::debug("Input Context Priority Stack:");
	for (auto &ctx: contexts) {
		logger::debug(" - {}", InputContextIdToString(ctx));
	}
}
