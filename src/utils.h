#pragma once

void *GetRealGenericVRInterface(const char *pchInterfaceVersion);

const char *GetModuleNameForAddress(uintptr_t address);

uintptr_t InstallHook_(REL::Relocation<uintptr_t> vtable, size_t funcIndex, void *newFunc, const char *label);

template<typename T>
T InstallHook(REL::Relocation<uintptr_t> vtable, size_t funcIndex, T newFunc, const char *label) {
	return (T) InstallHook_(vtable, funcIndex, newFunc, label);
}
