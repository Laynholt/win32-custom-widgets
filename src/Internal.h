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
bool HandleControlMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                          LRESULT& result);
bool RegisterButtonClasses();
bool RegisterDisplayClasses();
bool RegisterTextBoxClass();
bool RegisterBooleanControlClasses();
bool RegisterSliderClass();
bool RegisterProgressBarClass();
bool RegisterComboBoxClasses();
bool RegisterScrollViewClasses();
bool RegisterTooltipClass();

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
inline constexpr UINT ComboSetItemsMessage = WM_APP + 0x57E;
inline constexpr UINT ComboSetSelectionMessage = WM_APP + 0x57F;
inline constexpr UINT ComboGetSelectionMessage = WM_APP + 0x580;
inline constexpr UINT ScrollSetExtentMessage = WM_APP + 0x581;
inline constexpr UINT ScrollSetOffsetMessage = WM_APP + 0x582;
inline constexpr UINT ScrollGetOffsetMessage = WM_APP + 0x583;
inline constexpr UINT ScrollGetContentMessage = WM_APP + 0x584;
inline constexpr UINT ComboGetAccessibleValueMessage = WM_APP + 0x585;
inline constexpr UINT ButtonGetPressedMessage = WM_APP + 0x586;
inline constexpr UINT ComboGetOpenMessage = WM_APP + 0x587;
inline constexpr UINT ProgressGetIndeterminateMessage = WM_APP + 0x588;

HWND CreateNumericBoxWindow(const NumericBoxOptions& options);
bool IsTextBoxWindow(HWND window, bool numericOnly = false);

} // namespace wcw::internal
