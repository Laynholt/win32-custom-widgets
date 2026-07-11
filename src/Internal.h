#pragma once

#include <wcw/Style.h>

#include <windows.h>

namespace wcw::internal {

inline constexpr UINT ThemeChangedMessage = WM_APP + 0x570;

HINSTANCE Instance();
bool RegisterControlClass(const wchar_t* name, WNDPROC procedure,
                          UINT style = CS_HREDRAW | CS_VREDRAW);

void RegisterWindow(HWND window);
void UnregisterWindow(HWND window);
StyleOverride WindowStyleOverride(HWND window);
FontSpec ResolveLabelFont(const Theme& theme, const StyleOverride& local);

// Handles messages shared by every fully buffer-painted library control.
bool HandleControlMessage(HWND window, UINT message, LRESULT& result);
bool RegisterButtonClasses();
bool RegisterDisplayClasses();

} // namespace wcw::internal
