#include "Test.h"

#include "../src/ComboModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <array>
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

    ShowWindow(parent, SW_SHOW);
    ShowWindow(combo, SW_SHOW);
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
    DestroyWindow(combo);
    CHECK(FindPopup() == nullptr);

    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
