#include <wcw/Controls.h>
#include <wcw/Runtime.h>
#include <wcw/Theme.h>

#ifndef UNICODE
#error Win32CustomWidgets must propagate UNICODE to consumers
#endif
#ifndef _UNICODE
#error Win32CustomWidgets must propagate _UNICODE to consumers
#endif
#ifndef NOMINMAX
#error Win32CustomWidgets must propagate NOMINMAX to consumers
#endif
#ifndef WIN32_LEAN_AND_MEAN
#error Win32CustomWidgets must propagate WIN32_LEAN_AND_MEAN to consumers
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!wcw::Initialize(instance)) return 1;
    wcw::SetTheme(wcw::DarkTheme());

    const WNDCLASSW parentClass{.lpfnWndProc = DefWindowProcW,
                                .hInstance = instance,
                                .lpszClassName = L"WcwConsumerSmokeParent"};
    if (!RegisterClassW(&parentClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        wcw::Shutdown();
        return 2;
    }

    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 100, 100, nullptr, nullptr, instance, nullptr);
    wcw::ButtonOptions options;
    options.parent = parent;
    options.id = 1;
    options.bounds = {0, 0, 80, 24};
    options.text = L"Smoke";
    options.checked = true;
    const auto button = parent ? wcw::CreateButton(options) : nullptr;
    const bool buttonWorks = button && wcw::GetChecked(button) &&
        wcw::SetChecked(button, false) && !wcw::GetChecked(button) &&
        wcw::SetAccessibleName(button, L"Consumer button");
    const auto label = parent ? wcw::CreateLabel(options, DT_CENTER) : nullptr;

    wcw::TabControlOptions tabOptions;
    tabOptions.parent = parent;
    tabOptions.bounds = {0, 28, 200, 40};
    tabOptions.items = {{L"First", 1}, {L"Second", 2}};
    const auto tabs = parent ? wcw::CreateTabControl(tabOptions) : nullptr;
    const bool tabsWork = tabs && wcw::SetTabSelection(tabs, 1) &&
                          wcw::GetTabSelection(tabs) == 1;

    if (button) DestroyWindow(button);
    if (parent) DestroyWindow(parent);
    wcw::Shutdown();
    return buttonWorks && label && tabsWork ? 0 : 3;
}
