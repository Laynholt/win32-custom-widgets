#pragma once

#include <wcw/Controls.h>

#include <windows.h>

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
                           bool readOnly = false, bool password = false);
bool HandleAccessibilityMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT& result);
void DestroyAccessibility(HWND window);
void ShutdownAccessibility();
void NotifyAccessibility(HWND window, DWORD event);

} // namespace wcw::internal
