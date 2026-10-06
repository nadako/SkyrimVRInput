#include "imgui_helper.h"
#include "extern/ImGuiVRHelperAPI.h"

namespace {
	ImGuiVRHelperPluginAPI::IImGuiVRHelperInterface001 *imguiHelper = nullptr;
}

void AcquireImgui() {
	imguiHelper = ImGuiVRHelperPluginAPI::GetImGuiVRHelperInterface001();
	if (imguiHelper) logger::info("ImGui VR Helper interface acquired (build {})", imguiHelper->GetBuildNumber());
}

bool IsImguiActive() {
	return imguiHelper && imguiHelper->IsOverlayVisible();
}
