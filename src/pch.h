#pragma once

#define WIN32_LEAN_AND_MEAN

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <atomic>
#include <mutex>
#include <cstring>
#include <filesystem>
#include <string>

#define OPENVR_INTERFACE_INTERNAL
#include "extern/openvr_1_7_15.h"
namespace vrnew = vr_1_7_15;

namespace logger = SKSE::log;
using namespace std::literals;
