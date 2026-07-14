#include "Test.h"

#include "../src/MenuModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <vector>

namespace {

int commandId;
int commandCode;
LPARAM commandSource;
bool popupAliveWhenCommand;
RECT capturedPopup{};

bool SameRect(RECT left, RECT right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

HWND Popup() {
    return FindWindowW(L"WcwMenuPopup", nullptr);
}

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

void ResetCommand() {
    commandId = commandCode = 0;
    commandSource = 0;
    popupAliveWhenCommand = false;
}

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        commandId = LOWORD(wParam);
        commandCode = HIWORD(wParam);
        commandSource = lParam;
        popupAliveWhenCommand = Popup() != nullptr;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CALLBACK SelectSecond(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

void CALLBACK SelectNested(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RIGHT, 0);
    popup = GetCapture();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

void CALLBACK SelectEnabled(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

void CALLBACK EscapeMenu(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

void CALLBACK DisableOwner(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    EnableWindow(owner, FALSE);
}

void CALLBACK StealCapture(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    SetCapture(owner);
    CHECK(GetCapture() == owner);
}

void CALLBACK DeactivateMenu(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_ACTIVATEAPP, FALSE, 0);
}

void CALLBACK LeaveBeforeChildTimer(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
    SendMessageW(popup, WM_MOUSEMOVE, 0, MAKELPARAM(-10, -10));
    SendMessageW(popup, WM_TIMER, 1, 0);
    CHECK(PopupCount() == 1);
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

void CALLBACK DestroyOwner(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    DestroyWindow(owner);
}

void CALLBACK CapturePopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    CHECK(GetWindowRect(popup, &capturedPopup));
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

} // namespace

int main() {
    const std::vector<wcw::MenuItem> valid{
        {.id = 1, .text = L"Open"},
        {.separator = true},
        {.text = L"More", .children = {{.id = 2, .text = L"Nested"}}},
        {.id = 0, .text = L"Unavailable", .enabled = false},
    };
    const std::vector<wcw::MenuItem> empty;
    const std::vector<wcw::MenuItem> zeroId{{.id = 0, .text = L"Bad"}};
    const std::vector<wcw::MenuItem> largeId{{.id = 65536, .text = L"Bad"}};
    const std::vector<wcw::MenuItem> emptyText{{.enabled = false}};
    const std::vector<wcw::MenuItem> invalidNested{
        {.text = L"Parent", .children = {{.id = 0, .text = L"Bad"}}},
    };
    const std::vector<wcw::MenuItem> ignoredSeparator{
        {.id = 999999, .children = {{.id = 0}}, .separator = true},
    };
    CHECK(wcw::internal::ValidMenuItems(valid));
    CHECK(!wcw::internal::ValidMenuItems(empty));
    CHECK(!wcw::internal::ValidMenuItems(zeroId));
    CHECK(!wcw::internal::ValidMenuItems(largeId));
    CHECK(!wcw::internal::ValidMenuItems(emptyText));
    CHECK(!wcw::internal::ValidMenuItems(invalidNested));
    CHECK(wcw::internal::ValidMenuItems(ignoredSeparator));

    CHECK(wcw::internal::NextMenuIndex(valid, -1, 1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, 0, 1) == 2);
    CHECK(wcw::internal::NextMenuIndex(valid, 2, 1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, -1, -1) == 2);
    CHECK(wcw::internal::NextMenuIndex(valid, 2, -1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, 0, -1) == 2);
    CHECK(wcw::internal::EdgeMenuIndex(valid, false) == 0);
    CHECK(wcw::internal::EdgeMenuIndex(valid, true) == 2);
    const std::vector<wcw::MenuItem> unavailable{
        {.separator = true},
        {.text = L"Disabled", .enabled = false},
    };
    CHECK(wcw::internal::NextMenuIndex(unavailable, -1, 1) == -1);
    CHECK(wcw::internal::EdgeMenuIndex(unavailable, false) == -1);

    const RECT work{0, 0, 800, 600};
    CHECK(SameRect(wcw::internal::PlaceRootMenu({100, 100, 100, 100}, {120, 100}, work),
                   {100, 100, 220, 200}));
    CHECK(SameRect(wcw::internal::PlaceRootMenu({760, 580, 760, 580}, {120, 100}, work),
                   {680, 480, 800, 580}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({100, 100, 180, 132}, {160, 120}, work),
                   {180, 100, 340, 220}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({700, 400, 780, 432}, {160, 120}, work),
                   {540, 400, 700, 520}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({100, 500, 180, 532}, {160, 120}, work),
                   {180, 480, 340, 600}));

    const RECT scaledWork{0, 0, 1200, 900};
    CHECK(SameRect(wcw::internal::PlaceRootMenu({1140, 870, 1140, 870}, {180, 150}, scaledWork),
                   {1020, 720, 1200, 870}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({1050, 600, 1170, 648}, {240, 180}, scaledWork),
                   {810, 600, 1050, 780}));

    SetLastError(ERROR_SUCCESS);
    const wcw::ContextMenuOptions validOptions{{{.id = 1, .text = L"Item"}}};
    CHECK(!wcw::ShowContextMenu(nullptr, {0, 0}, validOptions));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);

    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwMenuTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    SetLastError(ERROR_SUCCESS);
    const wcw::ContextMenuOptions invalidOptions{{{.id = 0, .text = L"Bad"}}};
    CHECK(!wcw::ShowContextMenu(parent, {20, 20}, invalidOptions));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    wcw::ContextMenuOptions menu{{
        {.id = 101, .text = L"First"},
        {.id = 102, .text = L"Second", .shortcut = L"Ctrl+S"},
        {.text = L"More", .children = {{.id = 103, .text = L"Nested"}}},
    }};
    ResetCommand();
    SetTimer(parent, 1, 1, SelectSecond);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 102);
    CHECK(commandCode == 0);
    CHECK(commandSource == 0);
    CHECK(!popupAliveWhenCommand);
    CHECK(Popup() == nullptr);

    wcw::ContextMenuOptions nested{{
        {.text = L"More", .children = {{.id = 103, .text = L"Nested"},
                                        {.id = 104, .text = L"Nested second"}}},
    }};
    ResetCommand();
    SetTimer(parent, 2, 1, SelectNested);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, nested));
    CHECK(commandId == 103);
    CHECK(Popup() == nullptr);

    ResetCommand();
    SetTimer(parent, 10, 1, LeaveBeforeChildTimer);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, nested));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    wcw::ContextMenuOptions disabled{{
        {.id = 201, .text = L"Disabled", .enabled = false},
        {.id = 202, .text = L"Enabled"},
    }};
    ResetCommand();
    SetTimer(parent, 3, 1, SelectEnabled);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, disabled));
    CHECK(commandId == 202);

    ResetCommand();
    SetTimer(parent, 4, 1, EscapeMenu);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    ResetCommand();
    SetTimer(parent, 5, 1, DisableOwner);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);
    EnableWindow(parent, TRUE);

    ResetCommand();
    SetTimer(parent, 8, 1, StealCapture);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(GetCapture() == nullptr);

    ResetCommand();
    SetTimer(parent, 9, 1, DeactivateMenu);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    MONITORINFO monitor{sizeof(monitor)};
    POINT edge{};
    CHECK(GetMonitorInfoW(MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST), &monitor));
    edge = {monitor.rcWork.right - 1, monitor.rcWork.bottom - 1};
    capturedPopup = {};
    SetTimer(parent, 6, 1, CapturePopup);
    CHECK(wcw::ShowContextMenu(parent, edge, menu));
    CHECK(capturedPopup.left >= monitor.rcWork.left);
    CHECK(capturedPopup.top >= monitor.rcWork.top);
    CHECK(capturedPopup.right <= monitor.rcWork.right);
    CHECK(capturedPopup.bottom <= monitor.rcWork.bottom);

    const auto temporary = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                           0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    CHECK(temporary != nullptr);
    ResetCommand();
    SetTimer(temporary, 7, 1, DestroyOwner);
    CHECK(wcw::ShowContextMenu(temporary, {20, 20}, menu));
    CHECK(!IsWindow(temporary));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
