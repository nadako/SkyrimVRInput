#include "utils.h"

void *GetRealGenericVRInterface(const char *pchInterfaceVersion) {
	constexpr auto functionName = "VR_GetGenericInterface";

	typedef void * (*pfnVRGetGenericInterface)(const char *pchInterfaceVersion, vrnew::EVRInitError *peError);

	static pfnVRGetGenericInterface VR_GetGenericInterface = nullptr;
	if (!VR_GetGenericInterface) {
		constexpr auto moduleName = "openvr_api.dll";

		auto module = GetModuleHandleA(moduleName);
		if (!module) {
			logger::error("Module {} is not loaded", moduleName);
			return nullptr;
		}

		VR_GetGenericInterface = (pfnVRGetGenericInterface) GetProcAddress(module, functionName);
		if (!VR_GetGenericInterface) {
			logger::error("Function {} is not found in module {}", functionName, moduleName);
			return nullptr;
		}
	}

	vrnew::EVRInitError error;
	auto result = VR_GetGenericInterface(pchInterfaceVersion, &error);
	if (error != vrnew::VRInitError_None) {
		logger::error("{}({}) failed with error {}", functionName, pchInterfaceVersion, (int) error);
		return nullptr;
	}
	return result;
}

// NOTE: the return value is only valid until next call
const char *GetModuleNameForAddress(uintptr_t address) {
	HMODULE module;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) address, &module)) {
		return "<failed-to-get-module-handle>";
	}

	static char path[MAX_PATH];
	if (!GetModuleFileNameA(module, path, std::size(path))) {
		return "<failed-to-get-module-name>";
	}

	const char *name = strrchr(path, '\\');
	return name ? name + 1 : path;
}

uintptr_t InstallHook_(REL::Relocation<uintptr_t> vtable, size_t funcIndex, void *newFunc, const char *label) {
	auto slot = (uintptr_t *) (vtable.address() + funcIndex * sizeof(void *));
	logger::info("Hooking {} (previously in {})", label, GetModuleNameForAddress(*slot));
	return vtable.write_vfunc(funcIndex, newFunc);
}
