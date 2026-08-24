#pragma once

#include <wcw/Controls.h>

#include <windows.h>

#include <new>
#include <vector>

namespace wcw::internal {

inline constexpr UINT ThemeChangedMessage = WM_APP + 0x570;

struct MenuColors {
    ResolvedStyle style;
    Color selectedText;
};

MenuColors ResolveMenuColors(const StyleOverride& appearance);

HINSTANCE Instance();
bool RegisterControlClass(const wchar_t* name, WNDPROC procedure,
                          UINT style = CS_HREDRAW | CS_VREDRAW);

void RegisterWindow(HWND window);
void UnregisterWindow(HWND window);
bool IsLibraryWindow(HWND window, const wchar_t* className = nullptr);
StyleOverride WindowStyleOverride(HWND window);
bool WindowRegionContainsScreenPoint(HWND window, POINT screen);

inline StyleOverride OverlayStyle(StyleOverride result, const StyleOverride& local) {
#define WCW_OVERLAY(member) if (local.member) result.member = local.member
    WCW_OVERLAY(background); WCW_OVERLAY(foreground); WCW_OVERLAY(mutedForeground);
    WCW_OVERLAY(border); WCW_OVERLAY(hover); WCW_OVERLAY(pressed); WCW_OVERLAY(selected);
    WCW_OVERLAY(disabledSurface); WCW_OVERLAY(disabledText); WCW_OVERLAY(focus);
    WCW_OVERLAY(accent); WCW_OVERLAY(danger); WCW_OVERLAY(font); WCW_OVERLAY(borderWidthDip);
    WCW_OVERLAY(focusWidthDip); WCW_OVERLAY(paddingXDip); WCW_OVERLAY(paddingYDip);
    WCW_OVERLAY(spacingDip); WCW_OVERLAY(controlHeightDip); WCW_OVERLAY(cornerRadiusDip);
    WCW_OVERLAY(trackThicknessDip); WCW_OVERLAY(thumbSizeDip); WCW_OVERLAY(indicatorSizeDip);
#undef WCW_OVERLAY
    return result;
}

template <LRESULT (*Procedure)(HWND, UINT, WPARAM, LPARAM)>
LRESULT CALLBACK SafeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return Procedure(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

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
bool RegisterMenuClass();
bool RegisterMenuBarClass();
bool ShowPopupMenu(HWND commandTarget, HWND source, RECT anchor,
                   std::vector<MenuItem> items, const StyleOverride& appearance);
void CancelPopupMenu(HWND source);

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
inline constexpr UINT ButtonSetMenuItemsMessage = WM_APP + 0x589;
inline constexpr UINT ButtonGetMenuStateMessage = WM_APP + 0x58A;
inline constexpr UINT MenuBarSetItemsMessage = WM_APP + 0x58B;
inline constexpr UINT MenuBarHitTestMessage = WM_APP + 0x58C;

HWND CreateNumericBoxWindow(const NumericBoxOptions& options);
bool IsTextBoxWindow(HWND window, bool numericOnly = false);

} // namespace wcw::internal
