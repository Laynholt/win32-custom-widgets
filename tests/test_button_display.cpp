#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <array>

namespace {

int clicks;

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) ++clicks;
    return DefWindowProcW(window, message, wParam, lParam);
}

void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwButtonDisplayTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 300, 200, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::ControlOptions base{.parent = parent,
                             .id = 10,
                             .bounds = {0, 0, 100, 30},
                             .text = L"Button",
                             .appearance = {.cornerRadiusDip = 0.0f}};
    wcw::ButtonOptions buttonOptions;
    static_cast<wcw::ControlOptions&>(buttonOptions) = base;

    const auto button = wcw::CreateButton(buttonOptions);
    buttonOptions.id = 11;
    buttonOptions.text = L"Icon";
    const auto iconButton = wcw::CreateIconButton(buttonOptions);
    base.id = 12;
    base.text = L"Label";
    const auto label = wcw::CreateLabel(base);
    base.id = 13;
    base.text = L"Image";
    const auto image = wcw::CreateImageView(base, {}, wcw::ImageMode::Contain);
    base.id = 14;
    base.text = L"Separator";
    const auto separator = wcw::CreateSeparator(base);
    base.id = 15;
    base.text = L"Panel";
    const auto panel = wcw::CreatePanel(base);

    const std::array controls{button, iconButton, label, image, separator, panel};
    for (size_t i = 0; i < controls.size(); ++i) {
        CHECK(controls[i] != nullptr);
        CHECK(GetDlgCtrlID(controls[i]) == 10 + static_cast<int>(i));
        CHECK(wcw::SetStyleOverride(controls[i], {.cornerRadiusDip = 0.0f}));
    }
    wchar_t text[32]{};
    CHECK(GetWindowTextW(button, text, 32) == 6);
    CHECK(std::wstring_view(text) == L"Button");
    CHECK((GetWindowLongPtrW(label, GWL_STYLE) & WS_TABSTOP) == 0);
    CHECK((GetWindowLongPtrW(image, GWL_STYLE) & WS_TABSTOP) == 0);

    SetFocus(button);
    SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
    PumpMessages();
    CHECK(clicks == 1);

    EnableWindow(button, FALSE);
    SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
    PumpMessages();
    CHECK(clicks == 1);

    for (const auto control : controls) DestroyWindow(control);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
