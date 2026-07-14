#include "Test.h"

#include "../src/Internal.h"
#include "../src/MenuModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <algorithm>
#include <vector>

namespace {

int commandId;
int commandCode;
LPARAM commandSource;
bool popupAliveWhenCommand;
int popupCountAtCommand;
RECT capturedPopup{};
HWND menuButton{};
int menuButtonId{};
int buttonIdCommands{};
const std::vector<wcw::MenuItem> replacementMenu{{.id = 302, .text = L"Replacement"}};

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
    popupCountAtCommand = -1;
}

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        commandId = LOWORD(wParam);
        commandCode = HIWORD(wParam);
        commandSource = lParam;
        if (commandId == menuButtonId) ++buttonIdCommands;
        popupAliveWhenCommand = Popup() != nullptr;
        popupCountAtCommand = PopupCount();
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

INT_PTR CALLBACK DialogProc(HWND, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        commandId = LOWORD(wParam);
        commandCode = HIWORD(wParam);
        commandSource = lParam;
        if (commandId == menuButtonId) ++buttonIdCommands;
        popupAliveWhenCommand = Popup() != nullptr;
        popupCountAtCommand = PopupCount();
    }
    return FALSE;
}

HWND TestDialog(HINSTANCE instance) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog{WS_POPUP | WS_CAPTION | DS_CONTROL, 0, 0, 0, 0, 200, 100};
        WORD menu{};
        WORD windowClass{};
        WORD title{};
    } dialogTemplate;
    return CreateDialogIndirectParamW(instance, &dialogTemplate.dialog, nullptr, DialogProc, 0);
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

void CALLBACK SelectThenDisable(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
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

void CALLBACK SelectThenDestroy(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
    DestroyWindow(owner);
}

void CALLBACK QuitMenu(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    PostQuitMessage(73);
}

void CALLBACK SelectNestedPointer(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto root = Popup();
    CHECK(root != nullptr);
    SendMessageW(root, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
    CHECK(PopupCount() == 2);
    HWND child{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) {
        wchar_t name[64]{};
        GetClassNameW(window, name, static_cast<int>(std::size(name)));
        if (wcscmp(name, L"WcwMenuPopup") == 0 && window != GetCapture()) {
            *reinterpret_cast<HWND*>(value) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&child));
    CHECK(child != nullptr);
    RECT childBounds{};
    CHECK(GetWindowRect(child, &childBounds));
    POINT point{childBounds.left + 10, childBounds.top + 10};
    ScreenToClient(root, &point);
    SendMessageW(root, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}

void CALLBACK SelectHiddenLast(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    CHECK(GetWindowRect(popup, &capturedPopup));
    SendMessageW(popup, WM_KEYDOWN, VK_END, 0);
    RECT client{};
    CHECK(GetClientRect(popup, &client));
    SendMessageW(popup, WM_LBUTTONUP, 0,
                 MAKELPARAM(20, (std::max)(0L, client.bottom - 10)));
}

void CALLBACK WheelThenSelect(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    SendMessageW(popup, WM_LBUTTONUP, 0, MAKELPARAM(20, 10));
}

void CALLBACK CapturePopup(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    CHECK(GetWindowRect(popup, &capturedPopup));
    SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
}

void CALLBACK SelectFirst(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = Popup();
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

void CALLBACK ReplaceMenu(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    CHECK(Popup() != nullptr);
    CHECK(SendMessageW(menuButton, wcw::internal::ButtonGetMenuStateMessage, 0, 0) == 3);
    CHECK(wcw::SetMenuItems(menuButton, replacementMenu));
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
    CHECK(SameRect(wcw::internal::PlaceRootMenu({150, 150, 150, 150}, {500, 400},
                                                {100, 100, 300, 250}),
                   {100, 100, 300, 250}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({150, 120, 180, 152}, {500, 400},
                                               {100, 100, 300, 250}),
                   {100, 100, 300, 250}));

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

    menuButtonId = 300;
    wcw::MenuButtonOptions menuButtonOptions;
    menuButtonOptions.parent = parent;
    menuButtonOptions.id = menuButtonId;
    menuButtonOptions.bounds = {10, 10, 140, 36};
    menuButtonOptions.text = L"Actions";
    menuButtonOptions.items = {{.id = 301, .text = L"First"},
                               {.id = 303, .text = L"Second"}};
    menuButton = wcw::CreateMenuButton(menuButtonOptions);
    CHECK(menuButton != nullptr);
    CHECK(SendMessageW(menuButton, wcw::internal::ButtonGetMenuStateMessage, 0, 0) == 1);

    ResetCommand();
    buttonIdCommands = 0;
    SetTimer(parent, 18, 1, SelectFirst);
    SendMessageW(menuButton, BM_CLICK, 0, 0);
    CHECK(commandId == 301);
    CHECK(commandSource == reinterpret_cast<LPARAM>(menuButton));
    CHECK(!popupAliveWhenCommand);
    CHECK(buttonIdCommands == 0);
    CHECK(SendMessageW(menuButton, wcw::internal::ButtonGetMenuStateMessage, 0, 0) == 1);

    ResetCommand();
    SetTimer(parent, 19, 1, ReplaceMenu);
    SendMessageW(menuButton, BM_CLICK, 0, 0);
    CHECK(commandId == 0);
    CHECK(buttonIdCommands == 0);
    CHECK(Popup() == nullptr);
    CHECK(SendMessageW(menuButton, wcw::internal::ButtonGetMenuStateMessage, 0, 0) == 1);

    SetTimer(parent, 20, 1, SelectFirst);
    SendMessageW(menuButton, BM_CLICK, 0, 0);
    CHECK(commandId == 302);
    CHECK(commandSource == reinterpret_cast<LPARAM>(menuButton));
    CHECK(!popupAliveWhenCommand);
    CHECK(buttonIdCommands == 0);

    ResetCommand();
    SetTimer(parent, 21, 1, SelectFirst);
    SendMessageW(menuButton, WM_KEYDOWN, VK_DOWN, 0);
    CHECK(commandId == 302);
    CHECK(commandSource == reinterpret_cast<LPARAM>(menuButton));
    CHECK(!popupAliveWhenCommand);
    CHECK(buttonIdCommands == 0);

    ResetCommand();
    SetTimer(parent, 22, 1, SelectFirst);
    SendMessageW(menuButton, WM_SYSKEYDOWN, VK_DOWN, 1 << 29);
    CHECK(commandId == 302);
    CHECK(commandSource == reinterpret_cast<LPARAM>(menuButton));
    CHECK(!popupAliveWhenCommand);
    CHECK(buttonIdCommands == 0);

    wcw::ButtonOptions normalButtonOptions;
    normalButtonOptions.parent = parent;
    normalButtonOptions.id = 304;
    normalButtonOptions.bounds = {10, 50, 140, 36};
    normalButtonOptions.text = L"Normal";
    const auto normalButton = wcw::CreateButton(normalButtonOptions);
    CHECK(normalButton != nullptr);
    CHECK((SendMessageW(normalButton, WM_GETDLGCODE, 0, 0) & DLGC_WANTARROWS) == 0);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetMenuItems(normalButton, replacementMenu));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetMenuItems(menuButton, zeroId));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    auto invalidMenuButtonOptions = menuButtonOptions;
    invalidMenuButtonOptions.items = zeroId;
    SetLastError(ERROR_SUCCESS);
    CHECK(wcw::CreateMenuButton(invalidMenuButtonOptions) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    const auto dialog = TestDialog(instance);
    CHECK(dialog != nullptr);
    auto dialogMenuButtonOptions = menuButtonOptions;
    dialogMenuButtonOptions.parent = dialog;
    dialogMenuButtonOptions.id = 305;
    dialogMenuButtonOptions.items = replacementMenu;
    const auto dialogMenuButton = wcw::CreateMenuButton(dialogMenuButtonOptions);
    CHECK(dialogMenuButton != nullptr);
    CHECK((SendMessageW(dialogMenuButton, WM_GETDLGCODE, 0, 0) & DLGC_WANTARROWS) != 0);
    ShowWindow(dialog, SW_SHOW);
    ShowWindow(dialogMenuButton, SW_SHOW);
    CHECK(SetFocus(dialogMenuButton) != nullptr || GetFocus() == dialogMenuButton);
    menuButtonId = dialogMenuButtonOptions.id;
    ResetCommand();
    buttonIdCommands = 0;
    SetTimer(dialog, 23, 1, SelectFirst);
    MSG down{dialogMenuButton, WM_KEYDOWN, VK_DOWN, 0};
    CHECK(IsDialogMessageW(dialog, &down));
    KillTimer(dialog, 23);
    CHECK(commandId == 302);
    CHECK(commandSource == reinterpret_cast<LPARAM>(dialogMenuButton));
    CHECK(!popupAliveWhenCommand);
    CHECK(buttonIdCommands == 0);

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
    CHECK(popupCountAtCommand == 0);
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
    SetTimer(parent, 11, 1, SelectNestedPointer);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, nested));
    CHECK(commandId == 103);
    CHECK(popupCountAtCommand == 0);
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
    SetTimer(parent, 12, 1, SelectThenDisable);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);
    EnableWindow(parent, TRUE);

    ResetCommand();
    SetTimer(parent, 8, 1, StealCapture);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(GetCapture() == parent);
    ReleaseCapture();

    ResetCommand();
    SetTimer(parent, 9, 1, DeactivateMenu);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    const auto focusWindow = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE,
                                             0, 0, 80, 24, parent, nullptr, instance, nullptr);
    CHECK(focusWindow != nullptr);
    ShowWindow(parent, SW_SHOW);
    CHECK(SetFocus(focusWindow) != nullptr || GetFocus() == focusWindow);
    SetTimer(parent, 13, 1, EscapeMenu);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(GetFocus() == focusWindow);

    ResetCommand();
    SetTimer(parent, 14, 1, QuitMenu);
    CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
    CHECK(commandId == 0);
    MSG quit{};
    bool foundQuit{};
    for (int checked = 0; checked < 100 && PeekMessageW(&quit, nullptr, 0, 0, PM_REMOVE); ++checked) {
        if (quit.message == WM_QUIT) {
            foundQuit = true;
            break;
        }
        TranslateMessage(&quit);
        DispatchMessageW(&quit);
    }
    CHECK(foundQuit);
    CHECK(quit.wParam == 73);

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

    wcw::ContextMenuOptions overflow;
    for (int index = 0; index < 100; ++index)
        overflow.items.push_back({.id = 1000 + index,
                                  .text = index == 0 ? std::wstring(2000, L'W')
                                                    : L"Item " + std::to_wstring(index)});
    capturedPopup = {};
    ResetCommand();
    SetTimer(parent, 15, 1, SelectHiddenLast);
    CHECK(wcw::ShowContextMenu(parent, edge, overflow));
    CHECK(capturedPopup.left >= monitor.rcWork.left);
    CHECK(capturedPopup.top >= monitor.rcWork.top);
    CHECK(capturedPopup.right <= monitor.rcWork.right);
    CHECK(capturedPopup.bottom <= monitor.rcWork.bottom);
    CHECK(commandId == 1099);
    CHECK(popupCountAtCommand == 0);

    ResetCommand();
    SetTimer(parent, 17, 1, WheelThenSelect);
    CHECK(wcw::ShowContextMenu(parent, edge, overflow));
    CHECK(commandId == 1003);
    CHECK(popupCountAtCommand == 0);

    const auto temporary = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                           0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    CHECK(temporary != nullptr);
    ResetCommand();
    SetTimer(temporary, 7, 1, DestroyOwner);
    CHECK(wcw::ShowContextMenu(temporary, {20, 20}, menu));
    CHECK(!IsWindow(temporary));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    const auto selectedTemporary = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                                   0, 0, 320, 240, nullptr, nullptr, instance, nullptr);
    CHECK(selectedTemporary != nullptr);
    ResetCommand();
    SetTimer(selectedTemporary, 16, 1, SelectThenDestroy);
    CHECK(wcw::ShowContextMenu(selectedTemporary, {20, 20}, menu));
    CHECK(!IsWindow(selectedTemporary));
    CHECK(commandId == 0);
    CHECK(Popup() == nullptr);

    DestroyWindow(normalButton);
    DestroyWindow(menuButton);
    DestroyWindow(dialogMenuButton);
    DestroyWindow(dialog);
    DestroyWindow(focusWindow);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
