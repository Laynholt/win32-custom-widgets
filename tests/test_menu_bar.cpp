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

    menuBar = nullptr;
    wcw::Shutdown();
    return testFailures ? 1 : 0;
}
