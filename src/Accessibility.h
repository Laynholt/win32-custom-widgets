#pragma once

#include <wcw/Controls.h>

#include <windows.h>

#include <optional>
#include <vector>

namespace wcw::internal {

enum class AccessibleKind {
    Button,
    Checkbox,
    Toggle,
    TextBox,
    NumericBox,
    Slider,
    ProgressBar,
    ComboBox,
    ScrollView,
    Label,
    Image,
    Separator,
    Panel,
    Tooltip,
    Menu,
};

struct AccessibleMenuItem {
    std::wstring text;
    RECT screenBounds{};
    int row{};
    bool enabled{};
    bool checked{};
    bool hasPopup{};
};

inline constexpr UINT MenuActivateAccessibleMessage = WM_APP + 0x58B;

void RegisterAccessibility(HWND window, AccessibleKind kind, const ControlOptions& options,
                           bool readOnly = false, bool password = false,
                           std::optional<std::wstring> fallbackName = std::nullopt);
void RegisterMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items);
void UpdateMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items,
                             int focusedChild);
bool HandleAccessibilityMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT& result);
void DestroyAccessibility(HWND window);
void ShutdownAccessibility();
void NotifyAccessibility(HWND window, DWORD event, LONG child = CHILDID_SELF);
void NotifyAccessibilityFocus(HWND window, bool fromChild = false);

} // namespace wcw::internal
