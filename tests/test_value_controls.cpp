#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <cmath>

namespace {

int checkNotifications;
int valueNotifications;
bool notifiedCheck;
double notifiedValue;

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NOTIFY) {
        const auto header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->code == wcw::WCN_CHECK_CHANGED) {
            const auto notification = reinterpret_cast<const wcw::CheckChangedNotification*>(lParam);
            ++checkNotifications;
            notifiedCheck = notification->checked;
        } else if (header && header->code == wcw::WCN_VALUE_CHANGED) {
            const auto notification = reinterpret_cast<const wcw::ValueChangedNotification*>(lParam);
            ++valueNotifications;
            notifiedValue = notification->value;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Key(HWND window, WPARAM key) {
    SendMessageW(window, WM_KEYDOWN, key, 0);
    SendMessageW(window, WM_KEYUP, key, 0);
}

void PumpTimersFor(HWND window, DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, window, WM_TIMER, WM_TIMER, PM_REMOVE)) {
            DispatchMessageW(&message);
        }
        Sleep(1);
    } while (GetTickCount64() < end);
}

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwValueControlsTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 400, 260, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::CheckableOptions checkOptions;
    checkOptions.parent = parent;
    checkOptions.id = 10;
    checkOptions.bounds = {10, 10, 140, 30};
    checkOptions.text = L"Check";
    const auto checkbox = wcw::CreateCheckbox(checkOptions);
    checkOptions.id = 11;
    checkOptions.bounds.y = 50;
    const auto toggle = wcw::CreateToggle(checkOptions);
    CHECK(checkbox != nullptr);
    CHECK(toggle != nullptr);

    SendMessageW(checkbox, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(checkbox, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(checkbox, WM_KEYUP, VK_SPACE, 0);
    CHECK(wcw::GetChecked(checkbox));
    CHECK(checkNotifications == 1);
    CHECK(notifiedCheck);

    Key(toggle, VK_SPACE);
    CHECK(wcw::GetChecked(toggle));
    CHECK(checkNotifications == 2);
    EnableWindow(toggle, FALSE);
    Key(toggle, VK_SPACE);
    CHECK(wcw::GetChecked(toggle));
    CHECK(checkNotifications == 2);
    CHECK(wcw::SetChecked(toggle, false));
    CHECK(!wcw::GetChecked(toggle));
    CHECK(checkNotifications == 2);

    wcw::SliderOptions sliderOptions;
    sliderOptions.parent = parent;
    sliderOptions.id = 20;
    sliderOptions.bounds = {10, 90, 200, 30};
    sliderOptions.minimum = 0;
    sliderOptions.maximum = 100;
    sliderOptions.step = 2;
    sliderOptions.value = 50;
    sliderOptions.trackAppearance.background = wcw::Color::FromRgb(1, 2, 3);
    sliderOptions.trackAppearance.trackThicknessDip = 5.0f;
    sliderOptions.thumbAppearance.accent = wcw::Color::FromRgb(4, 5, 6);
    sliderOptions.thumbAppearance.thumbSizeDip = 16.0f;
    const auto slider = wcw::CreateSlider(sliderOptions);
    CHECK(slider != nullptr);
    Key(slider, VK_RIGHT);
    CHECK(wcw::GetSliderValue(slider) == 52);
    Key(slider, VK_PRIOR);
    CHECK(wcw::GetSliderValue(slider) == 72);
    Key(slider, VK_HOME);
    CHECK(wcw::GetSliderValue(slider) == 0);
    Key(slider, VK_END);
    CHECK(wcw::GetSliderValue(slider) == 100);
    const auto notificationsAtMaximum = valueNotifications;
    Key(slider, VK_END);
    CHECK(valueNotifications == notificationsAtMaximum);
    CHECK(notifiedValue == 100);

    SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(25, 15));
    CHECK(GetCapture() == slider);
    SendMessageW(slider, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(100, 15));
    SendMessageW(slider, WM_LBUTTONUP, 0, MAKELPARAM(100, 15));
    CHECK(GetCapture() != slider);
    CHECK(std::abs(*wcw::GetSliderValue(slider) - 50) <= 2);
    EnableWindow(slider, FALSE);
    const auto disabledValue = wcw::GetSliderValue(slider);
    Key(slider, VK_LEFT);
    CHECK(wcw::GetSliderValue(slider) == disabledValue);

    sliderOptions.id = 21;
    sliderOptions.step = 30;
    sliderOptions.value = 0;
    const auto endpointSlider = wcw::CreateSlider(sliderOptions);
    CHECK(endpointSlider != nullptr);
    Key(endpointSlider, VK_END);
    CHECK(wcw::GetSliderValue(endpointSlider) == 100);
    Key(endpointSlider, VK_LEFT);
    CHECK(wcw::GetSliderValue(endpointSlider) == 70);

    CHECK(wcw::SetSliderValue(endpointSlider, 0));
    SendMessageW(endpointSlider, WM_MOUSEWHEEL, MAKEWPARAM(0, 240), 0);
    CHECK(wcw::GetSliderValue(endpointSlider) == 60);
    SendMessageW(endpointSlider, WM_MOUSEWHEEL, MAKEWPARAM(0, 60), 0);
    CHECK(wcw::GetSliderValue(endpointSlider) == 60);
    SendMessageW(endpointSlider, WM_MOUSEWHEEL, MAKEWPARAM(0, 60), 0);
    CHECK(wcw::GetSliderValue(endpointSlider) == 90);
    SendMessageW(endpointSlider, WM_MOUSEWHEEL, MAKEWPARAM(0, 0), 0);
    CHECK(wcw::GetSliderValue(endpointSlider) == 90);

    sliderOptions.id = 22;
    sliderOptions.bounds.height = 2;
    sliderOptions.step = 1;
    sliderOptions.value = 0;
    const auto lowSlider = wcw::CreateSlider(sliderOptions);
    CHECK(lowSlider != nullptr);
    SendMessageW(lowSlider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 1));
    SendMessageW(lowSlider, WM_LBUTTONUP, 0, MAKELPARAM(100, 1));
    CHECK(std::abs(*wcw::GetSliderValue(lowSlider) - 50) <= 1);

    wcw::ProgressBarOptions progressOptions;
    progressOptions.parent = parent;
    progressOptions.id = 30;
    progressOptions.bounds = {10, 130, 200, 20};
    progressOptions.minimum = 0;
    progressOptions.maximum = 100;
    progressOptions.value = 150;
    const auto progress = wcw::CreateProgressBar(progressOptions);
    CHECK(progress != nullptr);
    CHECK(wcw::GetProgressValue(progress) == 100);
    CHECK(wcw::SetProgressValue(progress, -10));
    CHECK(wcw::GetProgressValue(progress) == 0);

    ShowWindow(parent, SW_SHOWNOACTIVATE);
    ShowWindow(progress, SW_SHOWNOACTIVATE);
    CHECK(wcw::SetProgressIndeterminate(progress, true));
    ValidateRect(progress, nullptr);
    PumpTimersFor(progress, 40);
    CHECK(GetUpdateRect(progress, nullptr, FALSE) != FALSE);
    UpdateWindow(progress);
    ShowWindow(parent, SW_HIDE);
    ValidateRect(progress, nullptr);
    PumpTimersFor(progress, 40);
    CHECK(GetUpdateRect(progress, nullptr, FALSE) == FALSE);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    ValidateRect(progress, nullptr);
    PumpTimersFor(progress, 40);
    CHECK(GetUpdateRect(progress, nullptr, FALSE) != FALSE);
    UpdateWindow(progress);
    ShowWindow(progress, SW_HIDE);
    ValidateRect(progress, nullptr);
    PumpTimersFor(progress, 40);
    CHECK(GetUpdateRect(progress, nullptr, FALSE) == FALSE);
    ShowWindow(progress, SW_SHOWNOACTIVATE);
    EnableWindow(progress, FALSE);
    ValidateRect(progress, nullptr);
    PumpTimersFor(progress, 40);
    CHECK(GetUpdateRect(progress, nullptr, FALSE) == FALSE);
    CHECK(wcw::SetProgressIndeterminate(progress, false));

    DestroyWindow(progress);
    DestroyWindow(lowSlider);
    DestroyWindow(endpointSlider);
    DestroyWindow(slider);
    DestroyWindow(toggle);
    DestroyWindow(checkbox);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
