#include "debug.h"
#include "hooks.h"
#include "imgui_helper.h"
#include "settings.h"
#include "vrinput.h"
#include "mod_interfaces.h"

namespace {
	bool IsSKSEActionBindingsEnabled() {
		char skseIniPath[MAX_PATH];
		if (GetFullPathNameA("Data/SKSE/skse.ini", MAX_PATH, skseIniPath, nullptr) == 0) return false;
		logger::debug("Checking {}", skseIniPath);
		auto bActionBindingsValue = GetPrivateProfileIntA("Input", "bActionBindings", 0, skseIniPath);
		return bActionBindingsValue != 0;
	}

	bool Init() {
		if (IsSKSEActionBindingsEnabled()) {
			logger::error("Incompatible `[Input] bActionBindings` setting is enabled in skse.ini.");
			return false;
		}

		if (!InitVRInput()) {
			logger::error("Failed to initialize SteamVR input.");
			return false;
		}

		// TODO: figure out what to hook for better UI compat
		if (!InstallHooks()) {
			logger::error("Failed to install engine function hooks.");
			return false;
		}

		if (gSettings.logInputEvents) AddInputEventLogger();

		return true;
	}

	bool initSuccess = false;

	void OnSKSEMessage(SKSE::MessagingInterface::Message *message) {
		switch (message->type) {
			case SKSE::MessagingInterface::kPostPostLoad: {
				AcquireImgui();
				break;
			}
			case SKSE::MessagingInterface::kDataLoaded: {
				// HIGGS cannot be used until it initializes its own state, which happens only on kDataLoaded,
				// so we acquire here instead of the usual kPostPostLoad
				if (initSuccess) {
					ModInterfaces::Acquire();
				}
				break;
			}
			case SKSE::MessagingInterface::kInputLoaded: {
				initSuccess = Init();
				if (initSuccess) logger::info("Initialization complete.");
				else logger::error("Initialization aborted.");
				break;
			}
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
	SKSE::Init(skse); // NOTE: CommonLibNG initializes default logging too
	LoadSettings();
	auto plugin = SKSE::PluginDeclaration::GetSingleton();
	logger::info("Initializing version: {}, log level: {}", plugin->GetVersion(), spdlog::level::to_string_view(spdlog::get_level()));
	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
