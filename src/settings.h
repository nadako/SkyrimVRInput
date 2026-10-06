#pragma once

struct Settings {
	bool logInputEvents = false;
	bool logActions = false;

	bool enableVrikFingerTracking = false;

	float hapticFrequency = 200.0f;
	float hapticMinAmplitude = 0.0f;
	float hapticAddedDurationAmplitudeScale = 1.0f;
};

extern Settings gSettings;

void LoadSettings();
