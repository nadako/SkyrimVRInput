#include "mod_interfaces.h"

namespace ModInterfaces {
	vrikPluginApi::IVrikInterface001 *vrik = nullptr;
	HiggsPluginAPI::IHiggsInterface001 *higgs = nullptr;

	void Acquire() {
		const auto messaging = SKSE::GetMessagingInterface();

		vrik = vrikPluginApi::getVrikInterface001(messaging);
		if (vrik) logger::info("VRIK interface acquired (build {})", vrik->getBuildNumber());

		higgs = HiggsPluginAPI::GetHiggsInterface001(messaging);
		if (higgs) logger::info("HIGGS interface acquired (build {})", higgs->GetBuildNumber());
	}
}
