#include "settings.h"

namespace {
	REX::INI::Str<> sLogLevel{"Debug", "sLogLevel", "info"};

	REX::INI::Bool<> sEnableVrikFingerTracking{"Fingers", "bEnableVrikFingerTracking", false};

	REX::INI::F32<> sHapticFrequency{"Haptics", "fFrequency", 200.0f};
	REX::INI::F32<> sHapticMinAmplitude{"Haptics", "fMinAmplitude", 0.0f};
	REX::INI::F32<> sHapticAddedDurationAmplitudeScale{"Haptics", "fAddedDurationAmplitudeScale", 1.0f};
}

Settings gSettings;

void LoadSettings() {
	constexpr auto path = "Data/SKSE/Plugins/skyrim_vrinput.ini";

	auto store = REX::INI::SettingStore::GetSingleton();
	store->Init(path, "");
	store->Load();

	gSettings.enableVrikFingerTracking = sEnableVrikFingerTracking.GetValue();
	gSettings.hapticFrequency = sHapticFrequency.GetValue();
	gSettings.hapticMinAmplitude = sHapticMinAmplitude.GetValue();
	gSettings.hapticAddedDurationAmplitudeScale = sHapticAddedDurationAmplitudeScale.GetValue();

	auto logLevel = spdlog::level::from_str(sLogLevel.GetValue());
	spdlog::set_level(logLevel);
	spdlog::flush_on(logLevel);
	bool debugLogging = spdlog::should_log(spdlog::level::debug);
	gSettings.logInputEvents = debugLogging;
	gSettings.logActions = debugLogging;

	logger::info("Loaded settings from {}", path);
}
