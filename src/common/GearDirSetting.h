#pragma once

#include <windows.h>
#include <string>

namespace BuzzVst {

// Per-user remembered Gear directory, stored at
// HKCU\Software\BuzzBridge\GearDir. Written by the installer (when the user
// picks where to extract the machine database) and by the plugin whenever a
// Gear folder is chosen in the UI, so new plugin instances default to it —
// including folders on drives other than C:. HKCU\Software is not subject
// to WOW64 redirection, so the 32-bit and 64-bit plugins share the value.
static const char* const kGearDirRegKey = "Software\\BuzzBridge";
static const char* const kGearDirRegValue = "GearDir";

inline std::string ReadSavedGearDir()
{
	HKEY hKey = nullptr;
	if (RegOpenKeyExA(HKEY_CURRENT_USER, kGearDirRegKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
		return "";
	char buf[MAX_PATH] = {};
	DWORD size = sizeof(buf) - 1;
	DWORD type = 0;
	LONG rc = RegQueryValueExA(hKey, kGearDirRegValue, nullptr, &type, (BYTE*)buf, &size);
	RegCloseKey(hKey);
	if (rc != ERROR_SUCCESS || type != REG_SZ) return "";
	return std::string(buf);
}

inline void WriteSavedGearDir(const std::string& dir)
{
	if (dir.empty()) return;
	HKEY hKey = nullptr;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, kGearDirRegKey, 0, nullptr, 0,
			KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
		return;
	RegSetValueExA(hKey, kGearDirRegValue, 0, REG_SZ,
		(const BYTE*)dir.c_str(), (DWORD)dir.size() + 1);
	RegCloseKey(hKey);
}

} // namespace BuzzVst
