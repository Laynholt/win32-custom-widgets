#include "Test.h"

#include "../src/NumericModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <cmath>
#include <optional>
#include <string>

namespace {

int notifications;
double notifiedValue;
int textChanges;

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE) ++textChanges;
    if (message == WM_NOTIFY) {
        const auto notification = reinterpret_cast<const wcw::ValueChangedNotification*>(lParam);
        if (notification && notification->header.code == wcw::WCN_VALUE_CHANGED) {
            ++notifications;
            notifiedValue = notification->value;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

HWND ChildEdit(HWND outer) { return FindWindowExW(outer, nullptr, L"Edit", nullptr); }

struct ThreadCheck {
    HWND window;
    bool result{};
    DWORD error{};
};

DWORD WINAPI CheckWrongThread(void* value) {
    auto& check = *static_cast<ThreadCheck*>(value);
    SetLastError(ERROR_SUCCESS);
    check.result = wcw::SetValidationError(check.window, true);
    check.error = GetLastError();
    return 0;
}

} // namespace

int main() {
    using wcw::internal::NumericModel;
    using wcw::internal::NumericTextState;

    NumericModel integer(wcw::NumericMode::Integer, -10, 10, 2, 0);
    CHECK(integer.SetText(L"-12") == NumericTextState::Valid);
    CHECK(integer.Value() == -10);
    CHECK(integer.Text() == L"-10");
    CHECK(integer.SetText(L"") == NumericTextState::Intermediate);
    CHECK(!integer.Value().has_value());
    CHECK(integer.SetText(L"-") == NumericTextState::Intermediate);
    CHECK(integer.SetText(L".") == NumericTextState::Invalid);
    CHECK(integer.SetText(L"1x") == NumericTextState::Invalid);
    CHECK(integer.SetText(L"3.5") == NumericTextState::Invalid);

    NumericModel floating(wcw::NumericMode::Floating, -100, 100, .5, 0);
    CHECK(floating.SetText(L"3.5") == NumericTextState::Valid);
    CHECK(floating.Value() == 3.5);
    CHECK(floating.SetText(L"+") == NumericTextState::Intermediate);
    CHECK(floating.SetText(L".") == NumericTextState::Intermediate);
    CHECK(floating.SetText(L"-.") == NumericTextState::Intermediate);
    CHECK(floating.SetText(L"3,5") == NumericTextState::Invalid);
    CHECK(floating.SetText(L"9.75") == NumericTextState::Valid);
    CHECK(floating.Step(1));
    CHECK(floating.Value() == 10.25);
    CHECK(floating.Text() == L"10.25");
    CHECK(floating.Step(-1));
    CHECK(floating.Value() == 9.75);

    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwTextNumericTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 300, 180, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::TextBoxOptions textOptions;
    textOptions.parent = parent;
    textOptions.id = 10;
    textOptions.bounds = {0, 0, 160, 30};
    textOptions.text = L"entered";
    textOptions.placeholder = L"placeholder";
    const auto textBox = wcw::CreateTextBox(textOptions);
    CHECK(textBox != nullptr);
    const auto edit = ChildEdit(textBox);
    CHECK(edit != nullptr);
    CHECK((GetWindowLongPtrW(edit, GWL_STYLE) & WS_BORDER) == 0);
    CHECK((GetWindowLongPtrW(edit, GWL_EXSTYLE) & WS_EX_CLIENTEDGE) == 0);
    CHECK(wcw::GetTextBoxText(textBox) == L"entered");

    ShowWindow(parent, SW_SHOWNOACTIVATE);
    ShowWindow(textBox, SW_SHOWNOACTIVATE);
    ValidateRect(textBox, nullptr);
    CHECK(wcw::SetValidationError(textBox, true));
    CHECK(GetUpdateRect(textBox, nullptr, FALSE) != FALSE);
    CHECK(wcw::GetTextBoxText(textBox) == L"entered");
    ValidateRect(textBox, nullptr);
    CHECK(wcw::SetTextBoxText(textBox, L"changed"));
    CHECK(GetUpdateRect(textBox, nullptr, FALSE) != FALSE);
    CHECK(wcw::GetTextBoxText(textBox) == L"changed");
    SetFocus(textBox);
    CHECK(GetFocus() == edit);

    wcw::TextBoxOptions protectedOptions = textOptions;
    protectedOptions.id = 11;
    protectedOptions.readOnly = true;
    protectedOptions.password = true;
    const auto protectedBox = wcw::CreateTextBox(protectedOptions);
    const auto protectedEdit = ChildEdit(protectedBox);
    CHECK((GetWindowLongPtrW(protectedEdit, GWL_STYLE) & ES_READONLY) != 0);
    CHECK(SendMessageW(protectedEdit, EM_GETPASSWORDCHAR, 0, 0) != 0);

    wcw::NumericBoxOptions numericOptions;
    numericOptions.parent = parent;
    numericOptions.id = 12;
    numericOptions.bounds = {0, 70, 160, 30};
    numericOptions.mode = wcw::NumericMode::Floating;
    numericOptions.minimum = 0;
    numericOptions.maximum = 10;
    numericOptions.step = .5;
    numericOptions.value = 1;
    const auto numericBox = wcw::CreateNumericBox(numericOptions);
    CHECK(numericBox != nullptr);
    const auto numericEdit = ChildEdit(numericBox);
    CHECK(numericEdit != nullptr);
    CHECK(wcw::GetNumericValue(numericBox) == std::optional<double>(1));

    SetWindowTextW(numericEdit, L"bad");
    PumpMessages();
    CHECK(!wcw::GetNumericValue(numericBox).has_value());
    CHECK(notifications == 0);
    CHECK(textChanges != 0);
    SetWindowTextW(numericEdit, L"2.5");
    PumpMessages();
    CHECK(wcw::GetNumericValue(numericBox) == std::optional<double>(2.5));
    CHECK(notifications == 1);
    CHECK(notifiedValue == 2.5);
    SendMessageW(numericEdit, WM_KEYDOWN, VK_UP, 0);
    CHECK(wcw::GetNumericValue(numericBox) == std::optional<double>(3));
    CHECK(notifications == 2);
    SendMessageW(numericEdit, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    CHECK(wcw::GetNumericValue(numericBox) == std::optional<double>(3.5));
    CHECK(notifications == 3);
    CHECK(wcw::SetNumericValue(numericBox, 100));
    CHECK(wcw::GetNumericValue(numericBox) == std::optional<double>(10));
    CHECK(notifications == 4);

    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetTextBoxText(parent, L"wrong type"));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

    ThreadCheck threadCheck{textBox};
    const auto thread = CreateThread(nullptr, 0, CheckWrongThread, &threadCheck, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!threadCheck.result);
    CHECK(threadCheck.error == ERROR_INVALID_WINDOW_HANDLE);

    auto invalidOptions = numericOptions;
    invalidOptions.step = 0;
    SetLastError(ERROR_SUCCESS);
    CHECK(wcw::CreateNumericBox(invalidOptions) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::GetNumericValue(textBox).has_value());
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

    DestroyWindow(numericBox);
    DestroyWindow(protectedBox);
    DestroyWindow(textBox);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
