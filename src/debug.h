#pragma once

#include "actions.h"

void AddInputEventLogger();
void LogControllerDeviceButtons();
std::string InputContextIdToString(GameInputContext);
void LogInputContextPriorityStack();
