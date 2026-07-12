#include "Test.h"

#include "../src/ComboModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <array>
#include <limits>
#include <vector>

namespace {

int notifications;
wcw::SelectionChangedNotification lastNotification{};

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const wcw::SelectionChangedNotification*>(lParam);
        if (notification && notification->header.code == wcw::WCN_SELECTION_CHANGED) {
            ++notifications;
            lastNotification = *notification;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND FindPopup() {
    HWND found{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) {
        wchar_t name[64]{};
        GetClassNameW(window, name, static_cast<int>(std::size(name)));
        if (wcscmp(name, L"WcwComboPopup") == 0) {
            *reinterpret_cast<HWND*>(value) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

bool HasNativeListChild(HWND popup) {
    bool found{};
    EnumChildWindows(popup, [](HWND child, LPARAM value) {
        wchar_t name[64]{};
        GetClassNameW(child, name, static_cast<int>(std::size(name)));
        if (_wcsicmp(name, L"ListBox") == 0 || _wcsicmp(name, L"ComboBox") == 0) {
            *reinterpret_cast<bool*>(value) = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&found));
    return found;
}

struct ThreadCheck {
    HWND combo{};
    bool result{};
    DWORD error{};
};

DWORD WINAPI CheckWrongThread(void* value) {
    auto& check = *static_cast<ThreadCheck*>(value);
    SetLastError(ERROR_SUCCESS);
    check.result = wcw::SetComboSelection(check.combo, 0);
    check.error = GetLastError();
    return 0;
}

} // namespace

int main() {
    using wcw::ComboItem;
    using wcw::internal::ComboModel;

    ComboModel model;
    CHECK(model.Navigate(1) == -1);
    model.SetItems({{L"Alpha", 10}, {L"Beta", 20}, {L"Alpine", 30}});
    CHECK(model.SetSelection(1));
    model.Open();
    CHECK(model.Navigate(1) == 2);
    CHECK(model.Navigate(1) == 2);
    CHECK(model.Navigate(-1) == 1);
    CHECK(model.Home() == 0);
    CHECK(model.End() == 2);
    model.Cancel();
    CHECK(model.Selection() == 1);

    model.Open();
    CHECK(model.PrefixSearch(L'a', 100) == 0);
    CHECK(model.PrefixSearch(L'A', 200) == 2);
    CHECK(model.PrefixSearch(L'a', 1201) == 0);
    CHECK(model.Commit());
    CHECK(model.Selection() == 0);
    model.SetItems({{L"Other", 40}, {L"Alpha moved", 10}});
    CHECK(model.Selection() == 1);
    model.SetItems({{L"\u0401\u0436", 50}, {L"\u0451\u0436\u0438\u043a", 60},
                    {L"\u042f\u0431\u043b\u043e\u043a\u043e", 70}});
    model.SetSelection(-1);
    model.Open();
    CHECK(model.PrefixSearch(L'\u0451', 2000) == 0);
    CHECK(model.PrefixSearch(L'\u0401', 2100) == 1);
    model.Cancel();

    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwComboTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::ComboBoxOptions options;
    options.parent = parent;
    options.id = 71;
    options.bounds = {10, 10, 180, 36};
    options.items = {{L"First", 101}, {L"Second", 202}, {L"Third", 303}};
    options.selectedIndex = 0;
    const auto combo = wcw::CreateComboBox(options);
    CHECK(combo != nullptr);
    CHECK(wcw::GetComboSelection(combo) == 0);
    wcw::ButtonOptions nextOptions;
    nextOptions.parent = parent;
    nextOptions.id = 74;
    nextOptions.bounds = {210, 10, 80, 36};
    nextOptions.text = L"Next";
    const auto nextControl = wcw::CreateButton(nextOptions);

    ShowWindow(parent, SW_SHOW);
    ShowWindow(combo, SW_SHOW);
    ShowWindow(nextControl, SW_SHOW);
    SetFocus(combo);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    auto popup = FindPopup();
    CHECK(popup != nullptr);
    CHECK((GetWindowLongPtrW(popup, GWL_STYLE) & WS_POPUP) != 0);
    CHECK(!HasNativeListChild(popup));
    CHECK(GetFocus() == popup);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK(FindPopup() == nullptr);
    CHECK(GetFocus() == combo);
    CHECK(wcw::GetComboSelection(combo) == 0);

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    SendMessageW(popup, WM_KEYDOWN, VK_TAB, 0);
    CHECK(FindPopup() == nullptr);
    CHECK(GetFocus() == nextControl);
    SetFocus(combo);

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_DPICHANGED, 144, 0);
    CHECK(FindPopup() == nullptr);
    CHECK(GetFocus() == combo);

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    CHECK(popup != nullptr);
    SetFocus(parent);
    CHECK(FindPopup() == nullptr);
    CHECK(GetFocus() != combo);

    SetFocus(combo);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    RECT beforeMove{};
    GetWindowRect(popup, &beforeMove);
    SetWindowPos(combo, nullptr, 30, 10, 180, 36, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT afterMove{};
    GetWindowRect(popup, &afterMove);
    CHECK(afterMove.left - beforeMove.left == 20);
    RECT parentBefore{};
    GetWindowRect(parent, &parentBefore);
    SetWindowPos(parent, nullptr, parentBefore.left + 15, parentBefore.top + 10, 320, 240,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT afterParentMove{};
    GetWindowRect(popup, &afterParentMove);
    CHECK(afterParentMove.left - afterMove.left == 15);
    ShowWindow(combo, SW_HIDE);
    CHECK(FindPopup() == nullptr);
    ShowWindow(combo, SW_SHOW);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    CHECK(FindPopup() != nullptr);
    ShowWindow(parent, SW_HIDE);
    CHECK(FindPopup() == nullptr);
    ShowWindow(parent, SW_SHOW);
    ShowWindow(combo, SW_SHOW);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    CHECK(FindPopup() != nullptr);
    EnableWindow(parent, FALSE);
    CHECK(FindPopup() == nullptr);
    EnableWindow(parent, TRUE);

    SetFocus(combo);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    CHECK(popup != nullptr);
    SetCapture(parent);
    CHECK(FindPopup() == nullptr);
    ReleaseCapture();
    CHECK(wcw::GetComboSelection(combo) == 0);
    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    SendMessageW(popup, WM_CANCELMODE, 0, 0);
    CHECK(FindPopup() == nullptr);
    CHECK(wcw::GetComboSelection(combo) == 0);

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(FindPopup() == nullptr);
    CHECK(wcw::GetComboSelection(combo) == 1);
    CHECK(notifications == 1);
    CHECK(lastNotification.oldIndex == 0);
    CHECK(lastNotification.newIndex == 1);
    CHECK(lastNotification.oldId == 101);
    CHECK(lastNotification.newId == 202);

    CHECK(wcw::SetComboItems(combo, {{L"Second moved", 202}, {L"Fourth", 404}}));
    CHECK(wcw::GetComboSelection(combo) == 0);
    CHECK(wcw::SetComboSelection(combo, 1));
    CHECK(wcw::GetComboSelection(combo) == 1);
    CHECK(notifications == 2);
    const int beforePreservedItems = notifications;
    CHECK(wcw::SetComboItems(combo, {{L"Fourth moved", 404}, {L"Other", 999}}));
    CHECK(wcw::GetComboSelection(combo) == 0);
    CHECK(notifications == beforePreservedItems);
    CHECK(wcw::SetComboItems(combo, {{L"Other", 999}}));
    CHECK(wcw::GetComboSelection(combo) == -1);
    CHECK(notifications == beforePreservedItems + 1);
    CHECK(lastNotification.oldId == 404);
    CHECK(lastNotification.newIndex == -1);
    CHECK(wcw::SetComboSelection(combo, 0));

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(-5, -5));
    CHECK(FindPopup() == nullptr);
    CHECK(GetFocus() == combo);

    auto scrollingOptions = options;
    scrollingOptions.id = 72;
    scrollingOptions.bounds.y = 60;
    scrollingOptions.popupHeightDip = 72;
    scrollingOptions.items.clear();
    for (int index = 0; index < 10; ++index)
        scrollingOptions.items.push_back({L"Item " + std::to_wstring(index), 500 + index});
    scrollingOptions.selectedIndex = 0;
    const auto scrollingCombo = wcw::CreateComboBox(scrollingOptions);
    ShowWindow(scrollingCombo, SW_SHOW);
    SendMessageW(scrollingCombo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    RECT popupBounds{};
    GetClientRect(popup, &popupBounds);
    const int scrollbarX = popupBounds.right - 2;
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scrollbarX, 2));
    SendMessageW(popup, WM_MOUSEMOVE, MK_LBUTTON,
                 MAKELPARAM(scrollbarX, popupBounds.bottom - 2));
    SendMessageW(popup, WM_LBUTTONUP, 0,
                 MAKELPARAM(scrollbarX, popupBounds.bottom - 2));
    CHECK(FindPopup() == popup);
    const int beforeScrollCommit = notifications;
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(wcw::GetComboSelection(scrollingCombo) >= 7);
    CHECK(notifications == beforeScrollCommit + 1);
    DestroyWindow(scrollingCombo);

    auto wheelOptions = scrollingOptions;
    wheelOptions.id = 75;
    wheelOptions.bounds.y = 155;
    wheelOptions.selectedIndex = 0;
    const auto wheelCombo = wcw::CreateComboBox(wheelOptions);
    ShowWindow(wheelCombo, SW_SHOW);
    const auto wheelAndCommitFirst = [&](int firstDelta, int secondDelta) {
        wcw::SetComboSelection(wheelCombo, 0);
        SendMessageW(wheelCombo, WM_KEYDOWN, VK_F4, 0);
        const auto wheelPopup = FindPopup();
        SendMessageW(wheelPopup, WM_MOUSEWHEEL, MAKEWPARAM(0, firstDelta), 0);
        if (secondDelta)
            SendMessageW(wheelPopup, WM_MOUSEWHEEL, MAKEWPARAM(0, secondDelta), 0);
        SendMessageW(wheelPopup, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        SendMessageW(wheelPopup, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        return wcw::GetComboSelection(wheelCombo);
    };
    CHECK(wheelAndCommitFirst(-WHEEL_DELTA / 2, -WHEEL_DELTA / 2) == 3);
    CHECK(wheelAndCommitFirst(-WHEEL_DELTA * 2, 0) == 6);
    CHECK(wheelAndCommitFirst(0, 0) == 0);

    wcw::SetComboSelection(wheelCombo, 0);
    SendMessageW(wheelCombo, WM_KEYDOWN, VK_NEXT, 0);
    CHECK(wcw::GetComboSelection(wheelCombo) == 2);
    SendMessageW(wheelCombo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    SendMessageW(popup, WM_KEYDOWN, VK_NEXT, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
    CHECK(wcw::GetComboSelection(wheelCombo) == 4);
    DestroyWindow(wheelCombo);

    auto unicodeOptions = options;
    unicodeOptions.id = 76;
    unicodeOptions.bounds.y = 200;
    unicodeOptions.items = {{L"\u0401\u0436", 800}, {L"\u0451\u0436\u0438\u043a", 801},
                            {L"\u042f\u0431\u043b\u043e\u043a\u043e", 802}};
    unicodeOptions.selectedIndex = -1;
    const auto unicodeCombo = wcw::CreateComboBox(unicodeOptions);
    ShowWindow(unicodeCombo, SW_SHOW);
    const int beforeClosedPrefix = notifications;
    SendMessageW(unicodeCombo, WM_CHAR, L'\u0451', 0);
    CHECK(wcw::GetComboSelection(unicodeCombo) == 0);
    CHECK(notifications == beforeClosedPrefix + 1);
    SendMessageW(unicodeCombo, WM_CHAR, L'\u0401', 0);
    CHECK(wcw::GetComboSelection(unicodeCombo) == 1);
    CHECK(notifications == beforeClosedPrefix + 2);
    DestroyWindow(unicodeCombo);

    auto partialRowOptions = options;
    partialRowOptions.id = 73;
    partialRowOptions.bounds.y = 110;
    partialRowOptions.popupHeightDip = 50;
    const auto partialRowCombo = wcw::CreateComboBox(partialRowOptions);
    ShowWindow(partialRowCombo, SW_SHOW);
    SendMessageW(partialRowCombo, WM_KEYDOWN, VK_F4, 0);
    popup = FindPopup();
    GetClientRect(popup, &popupBounds);
    SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON,
                 MAKELPARAM(10, popupBounds.bottom - 2));
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(10, popupBounds.bottom - 2));
    CHECK(FindPopup() == popup);
    CHECK(wcw::GetComboSelection(partialRowCombo) == 0);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
    DestroyWindow(partialRowCombo);

    SendMessageW(combo, WM_KEYDOWN, VK_F4, 0);
    CHECK(FindPopup() != nullptr);

    ThreadCheck threadCheck{combo};
    const auto thread = CreateThread(nullptr, 0, CheckWrongThread, &threadCheck, 0, nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!threadCheck.result);
    CHECK(threadCheck.error == ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetComboSelection(parent, 0));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!wcw::SetComboSelection(combo, 99));
    auto invalidOptions = options;
    invalidOptions.popupHeightDip = std::numeric_limits<float>::quiet_NaN();
    SetLastError(ERROR_SUCCESS);
    CHECK(wcw::CreateComboBox(invalidOptions) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    invalidOptions.popupHeightDip = std::numeric_limits<float>::infinity();
    CHECK(wcw::CreateComboBox(invalidOptions) == nullptr);

    DestroyWindow(combo);
    CHECK(FindPopup() == nullptr);

    DestroyWindow(nextControl);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
