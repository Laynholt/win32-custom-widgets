#include "../src/MenuModel.h"
#include "../src/Internal.h"
#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <vector>

namespace {

int commandId{};
LPARAM commandSource{};
HWND menuBar{};
HWND originalFocus{};
HWND inactiveTarget{};
RECT previousPopupBounds{};

int PopupCount() {
    int count{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) {
        wchar_t name[64]{};
        GetClassNameW(window, name, static_cast<int>(std::size(name)));
        if (wcscmp(name, L"WcwMenuPopup") == 0) ++*reinterpret_cast<int*>(value);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count;
}

HWND Popup() {
    return FindWindowW(L"WcwMenuPopup", nullptr);
}

INT_PTR CALLBACK DialogProc(HWND, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        commandId = LOWORD(wParam);
        commandSource = lParam;
    }
    return FALSE;
}

HWND TestDialog(HINSTANCE instance) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog{WS_POPUP | WS_CAPTION | DS_CONTROL, 0, 0, 0, 0, 400, 100};
        WORD menu{};
        WORD windowClass{};
        WORD title{};
    } dialogTemplate;
    return CreateDialogIndirectParamW(instance, &dialogTemplate.dialog, nullptr, DialogProc, 0);
}

void CALLBACK EscapePopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (popup) SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

void CALLBACK DisableBar(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(menuBar != nullptr);
    if (menuBar) EnableWindow(menuBar, FALSE);
}

void CALLBACK DestroyDialog(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    DestroyWindow(owner);
}

RECT BarItemScreenRect(int target) {
    RECT result{};
    int left = -1;
    int right = -1;
    for (int x = 0; x < 320; ++x) {
        POINT point{x, 16};
        if (SendMessageW(menuBar, wcw::internal::MenuBarHitTestMessage, 0,
                         reinterpret_cast<LPARAM>(&point)) == target) {
            if (left < 0) left = x;
            right = x + 1;
        }
    }
    if (left < 0) return result;
    result = {left, 0, right, 32};
    MapWindowPoints(menuBar, nullptr, reinterpret_cast<POINT*>(&result), 2);
    return result;
}

void CALLBACK CheckViewPopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    const auto view = BarItemScreenRect(2);
    RECT popupBounds{};
    CHECK(GetWindowRect(popup, &popupBounds));
    CHECK(popupBounds.top == view.bottom || popupBounds.bottom == view.top);
    CHECK(previousPopupBounds.left != popupBounds.left ||
          previousPopupBounds.right != popupBounds.right);
    POINT viewCenter{(view.left + view.right) / 2, (view.top + view.bottom) / 2};
    CHECK(SendMessageW(menuBar, wcw::internal::MenuBarNextItemMessage, 0,
                       reinterpret_cast<LPARAM>(&viewCenter)) == 2);
    if (popup) SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

void CALLBACK SwitchPopupRight(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (!popup) return;
    CHECK(GetWindowRect(popup, &previousPopupBounds));
    SendMessageW(popup, WM_KEYDOWN, VK_RIGHT, 0);
    SetTimer(owner, timer + 100, 1, CheckViewPopup);
}

void CALLBACK HoverSwitchPopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (!popup) return;
    auto view = BarItemScreenRect(2);
    POINT point{(view.left + view.right) / 2, (view.top + view.bottom) / 2};
    ScreenToClient(popup, &point);
    SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
    SetTimer(owner, timer + 100, 1, CheckViewPopup);
}

void CALLBACK ClickCurrentPopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (!popup) return;
    auto file = BarItemScreenRect(0);
    POINT point{(file.left + file.right) / 2, (file.top + file.bottom) / 2};
    ScreenToClient(popup, &point);
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}

void CALLBACK CancelWithAlt(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (popup) SendMessageW(popup, WM_SYSKEYDOWN, VK_MENU, 1L << 29);
}

void CALLBACK DeactivateParent(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    if (popup && inactiveTarget)
        SendMessageW(owner, WM_ACTIVATE, WA_INACTIVE,
                     reinterpret_cast<LPARAM>(inactiveTarget));
}

void CALLBACK NestedLeftBeforeSwitch(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    auto popup = Popup();
    CHECK(popup != nullptr);
    if (!popup) return;
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(PopupCount() == 2);
    popup = GetCapture();
    CHECK(popup != nullptr);
    if (!popup) return;
    SendMessageW(popup, WM_KEYDOWN, VK_LEFT, 0);
    CHECK(PopupCount() == 1);
    popup = GetCapture();
    CHECK(popup != nullptr);
    if (popup) {
        SendMessageW(popup, WM_KEYDOWN, VK_RIGHT, 0);
        SetTimer(owner, timer + 100, 1, CheckViewPopup);
    }
}

} // namespace

int main() {
    using wcw::MenuItem;
    using wcw::internal::ParseMenuLabel;
    using wcw::internal::ValidMenuBarItems;

    const auto file = ParseMenuLabel(L"&File");
    CHECK(file.text == L"File");
    CHECK(file.mnemonic == L'f');

    const auto escaped = ParseMenuLabel(L"Save && Close");
    CHECK(escaped.text == L"Save & Close");
    CHECK(escaped.mnemonic == 0);

    const auto trailing = ParseMenuLabel(L"Help&");
    CHECK(trailing.text == L"Help&");
    CHECK(trailing.mnemonic == 0);

    const std::vector<MenuItem> valid{
        {.text = L"&File", .children = {{.id = 101, .text = L"Open"}}},
        {.id = 102, .text = L"&Help"},
    };
    CHECK(ValidMenuBarItems(valid));
    const std::vector<MenuItem> empty;
    const std::vector<MenuItem> separator{{.separator = true}};
    const std::vector<MenuItem> emptyLabel{{.id = 1, .text = L"&"}};
    CHECK(!ValidMenuBarItems(empty));
    CHECK(!ValidMenuBarItems(separator));
    CHECK(!ValidMenuBarItems(emptyLabel));

    const std::vector<MenuItem> navigation{
        {.text = L"Disabled", .children = {{.id = 1, .text = L"One"}}, .enabled = false},
        {.text = L"File", .children = {{.id = 2, .text = L"Two"}}},
        {.text = L"Edit", .children = {{.id = 3, .text = L"Three"}}},
    };
    CHECK(wcw::internal::NextMenuIndex(navigation, -1, 1) == 1);
    CHECK(wcw::internal::NextMenuIndex(navigation, 1, -1) == 2);

    wcw::MenuBarOptions options;
    options.items = valid;

    const auto instance = GetModuleHandleW(nullptr);
    CHECK(wcw::Initialize(instance));

    const auto dialog = TestDialog(instance);
    CHECK(dialog != nullptr);

    const std::vector<MenuItem> firstItems{
        {.text = L"&File", .children = {{.id = 201, .text = L"Open"}}},
        {.id = 202, .text = L"&Help"},
    };
    const std::vector<MenuItem> replacementItems{
        {.id = 203, .text = L"&About"},
    };
    options.parent = dialog;
    options.id = 77;
    options.bounds = {0, 0, 320, 32};
    options.style = WS_VISIBLE;
    options.items = firstItems;

    SetLastError(ERROR_SUCCESS);
    auto invalidParent = options;
    invalidParent.parent = nullptr;
    CHECK(wcw::CreateMenuBar(invalidParent) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

    SetLastError(ERROR_SUCCESS);
    auto invalidItems = options;
    invalidItems.items.clear();
    CHECK(wcw::CreateMenuBar(invalidItems) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    menuBar = wcw::CreateMenuBar(options);
    CHECK(menuBar != nullptr);
    CHECK(GetClassLongPtrW(menuBar, GCW_ATOM) != 0);

    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 16));
    CHECK(GetCapture() == menuBar);
    SendMessageW(menuBar, WM_CANCELMODE, 0, 0);
    CHECK(GetCapture() != menuBar);

    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 16));
    CHECK(GetCapture() == menuBar);
    EnableWindow(menuBar, FALSE);
    CHECK(GetCapture() != menuBar);
    EnableWindow(menuBar, TRUE);

    const auto other = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                       0, 40, 40, 20, dialog, nullptr, instance, nullptr);
    CHECK(other != nullptr);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetMenuBarItems(other, replacementItems));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

    POINT firstPoint{8, 16};
    CHECK(SendMessageW(menuBar, wcw::internal::MenuBarHitTestMessage, 0,
                       reinterpret_cast<LPARAM>(&firstPoint)) == 0);
    int secondX = -1;
    for (int x = 0; x < 320; ++x) {
        POINT point{x, 16};
        if (SendMessageW(menuBar, wcw::internal::MenuBarHitTestMessage, 0,
                         reinterpret_cast<LPARAM>(&point)) == 1) {
            secondX = x;
            break;
        }
    }
    CHECK(secondX >= 0);
    commandId = 0;
    commandSource = 0;
    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(secondX, 16));
    SendMessageW(menuBar, WM_LBUTTONUP, 0, MAKELPARAM(secondX, 16));
    CHECK(commandId == 202);
    CHECK(commandSource == reinterpret_cast<LPARAM>(menuBar));

    SetTimer(dialog, 101, 1, EscapePopup);
    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 16));
    SendMessageW(menuBar, WM_LBUTTONUP, 0, MAKELPARAM(8, 16));
    CHECK(Popup() == nullptr);
    CHECK(commandId == 202);

    const std::vector<MenuItem> navigationItems{
        {.text = L"&File", .children = {{.text = L"More", .children = {{.id = 301, .text = L"Open"}}}}},
        {.text = L"&Edit", .children = {{.id = 302, .text = L"Cut"}}, .enabled = false},
        {.text = L"&View", .children = {{.id = 303, .text = L"Zoom"}}},
    };
    CHECK(wcw::SetMenuBarItems(menuBar, navigationItems));
    originalFocus = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE,
                                    0, 40, 100, 20, dialog, nullptr, instance, nullptr);
    CHECK(originalFocus != nullptr);
    ShowWindow(dialog, SW_SHOW);
    CHECK(SetFocus(originalFocus) != nullptr || GetFocus() == originalFocus);

    SetTimer(dialog, 201, 1, EscapePopup);
    SendMessageW(originalFocus, WM_KEYDOWN, VK_F10, 0);
    CHECK(GetFocus() == menuBar);
    SendMessageW(menuBar, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(menuBar, WM_KEYDOWN, VK_DOWN, 0);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SetTimer(dialog, 202, 1, SwitchPopupRight);
    SendMessageW(originalFocus, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SetTimer(dialog, 203, 1, HoverSwitchPopup);
    SendMessageW(originalFocus, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SetTimer(dialog, 204, 1, ClickCurrentPopup);
    SendMessageW(originalFocus, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SetTimer(dialog, 205, 1, CancelWithAlt);
    SendMessageW(originalFocus, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SetTimer(dialog, 206, 1, NestedLeftBeforeSwitch);
    SendMessageW(originalFocus, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    inactiveTarget = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 40, 20,
                                     nullptr, nullptr, instance, nullptr);
    CHECK(inactiveTarget != nullptr);
    SetTimer(dialog, 207, 1, DeactivateParent);
    SendMessageW(dialog, WM_SYSCHAR, L'f', 1L << 29);
    CHECK(Popup() == nullptr);
    CHECK(GetFocus() == originalFocus);

    SendMessageW(dialog, WM_SYSKEYDOWN, VK_MENU, 1L << 29);
    SendMessageW(dialog, WM_SYSKEYDOWN, L'x', 1L << 29);
    SendMessageW(dialog, WM_SYSKEYUP, VK_MENU, 1L << 29);
    CHECK(GetFocus() == originalFocus);
    SendMessageW(originalFocus, WM_SYSKEYDOWN, VK_MENU, 1L << 29);
    SendMessageW(originalFocus, WM_SYSKEYUP, VK_MENU, 1L << 29);
    CHECK(GetFocus() == menuBar);
    SendMessageW(menuBar, WM_SYSKEYDOWN, VK_MENU, 1L << 29);
    SendMessageW(menuBar, WM_SYSKEYUP, VK_MENU, 1L << 29);
    CHECK(GetFocus() == originalFocus);

    CHECK(wcw::SetMenuBarItems(menuBar, firstItems));

    CHECK(!wcw::SetMenuBarItems(menuBar, {}));
    CHECK(wcw::SetMenuBarItems(menuBar, replacementItems));
    InvalidateRect(menuBar, nullptr, FALSE);
    CHECK(UpdateWindow(menuBar) != FALSE);
    EnableWindow(menuBar, FALSE);
    CHECK(!IsWindowEnabled(menuBar));
    EnableWindow(menuBar, TRUE);

    CHECK(wcw::SetMenuBarItems(menuBar, firstItems));
    SetTimer(dialog, 102, 1, DisableBar);
    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 16));
    SendMessageW(menuBar, WM_LBUTTONUP, 0, MAKELPARAM(8, 16));
    CHECK(!IsWindowEnabled(menuBar));
    CHECK(Popup() == nullptr);
    EnableWindow(menuBar, TRUE);

    SetTimer(dialog, 103, 1, DestroyDialog);
    SendMessageW(menuBar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 16));
    SendMessageW(menuBar, WM_LBUTTONUP, 0, MAKELPARAM(8, 16));
    CHECK(!IsWindow(dialog));
    CHECK(Popup() == nullptr);

    if (inactiveTarget) DestroyWindow(inactiveTarget);
    menuBar = nullptr;
    wcw::Shutdown();
    return testFailures ? 1 : 0;
}
