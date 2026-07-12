#pragma once

#include <wcw/Controls.h>

#include <windows.h>

#include <optional>

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
};

void RegisterAccessibility(HWND window, AccessibleKind kind, const ControlOptions& options,
                           bool readOnly = false, bool password = false,
                           std::optional<std::wstring> fallbackName = std::nullopt);
bool HandleAccessibilityMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT& result);
void DestroyAccessibility(HWND window);
void ShutdownAccessibility();
void NotifyAccessibility(HWND window, DWORD event);
void NotifyAccessibilityFocus(HWND window, bool fromChild = false);

} // namespace wcw::internal
