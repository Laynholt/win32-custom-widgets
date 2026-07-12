#pragma once

#include <wcw/Controls.h>

#include <windows.h>

namespace wcw::internal {

inline constexpr UINT ThemeChangedMessage = WM_APP + 0x570;

HINSTANCE Instance();
bool RegisterControlClass(const wchar_t* name, WNDPROC procedure,
                          UINT style = CS_HREDRAW | CS_VREDRAW);

void RegisterWindow(HWND window);
void UnregisterWindow(HWND window);
bool IsLibraryWindow(HWND window);
StyleOverride WindowStyleOverride(HWND window);
FontSpec ResolveLabelFont(const Theme& theme, const StyleOverride& local);

// Handles messages shared by every fully buffer-painted library control.
bool HandleControlMessage(HWND window, UINT message, LRESULT& result);
bool RegisterButtonClasses();
bool RegisterDisplayClasses();
bool RegisterTextBoxClass();
bool RegisterBooleanControlClasses();
bool RegisterSliderClass();
bool RegisterProgressBarClass();

inline constexpr UINT TextBoxSetTextMessage = WM_APP + 0x571;
inline constexpr UINT TextBoxGetTextMessage = WM_APP + 0x572;
inline constexpr UINT TextBoxSetErrorMessage = WM_APP + 0x573;
inline constexpr UINT NumericSetValueMessage = WM_APP + 0x574;
inline constexpr UINT NumericGetValueMessage = WM_APP + 0x575;
inline constexpr UINT NumericStepMessage = WM_APP + 0x576;
inline constexpr UINT BooleanSetMessage = WM_APP + 0x577;
inline constexpr UINT BooleanGetMessage = WM_APP + 0x578;
inline constexpr UINT SliderSetValueMessage = WM_APP + 0x579;
inline constexpr UINT SliderGetValueMessage = WM_APP + 0x57A;
inline constexpr UINT ProgressSetValueMessage = WM_APP + 0x57B;
inline constexpr UINT ProgressGetValueMessage = WM_APP + 0x57C;
inline constexpr UINT ProgressSetIndeterminateMessage = WM_APP + 0x57D;

HWND CreateNumericBoxWindow(const NumericBoxOptions& options);
bool IsTextBoxWindow(HWND window, bool numericOnly = false);

} // namespace wcw::internal
