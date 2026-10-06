#pragma once

#include "extern/vrikinterface001.h"
#include "extern/higgsinterface001.h"

namespace ModInterfaces {
	extern vrikPluginApi::IVrikInterface001 *vrik;
	extern HiggsPluginAPI::IHiggsInterface001 *higgs;

	void Acquire();
}
