#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>
#include <wcw/Geometry.h>

#include <commctrl.h>
#include <oleacc.h>

namespace {

HWND pages[2]{};
int notifications{};
wcw::SelectionChangedNotification last{};
bool destroyOnChange{};

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NOTIFY) {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header && header->code == wcw::WCN_SELECTION_CHANGED) {
            last = *reinterpret_cast<const wcw::SelectionChangedNotification*>(lParam);
            ++notifications;
            for (int i = 0; i < 2; ++i)
                ShowWindow(pages[i], i == last.newIndex ? SW_SHOWNA : SW_HIDE);
            if (destroyOnChange) DestroyWindow(header->hwndFrom);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct ThreadCheck { HWND tab; bool changed{}; DWORD error{}; };
DWORD WINAPI WrongThread(void* value) {
    auto& check = *static_cast<ThreadCheck*>(value);
    check.changed = wcw::SetTabSelection(check.tab, 0);
    check.error = GetLastError();
    return 0;
}

void ClickTab(HWND tab, int index) {
    RECT rect{};
    CHECK(TabCtrl_GetItemRect(tab, index, &rect));
    const auto point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    SendMessageW(tab, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(tab, WM_LBUTTONUP, 0, point);
}

void CheckPaint(HWND tab, wcw::Color selected, int focusedIndex = -1) {
    RECT rect{};
    CHECK(GetClientRect(tab, &rect));
    const auto screen = GetDC(tab);
    const auto dc = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
    const auto old = SelectObject(dc, bitmap);
    SendMessageW(tab, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    RECT item{};
    CHECK(TabCtrl_GetItemRect(tab, wcw::GetTabSelection(tab), &item));
    // Sample inside the fill, away from text, border and rounded corners.
    const auto dpi = GetDpiForWindow(tab);
    CHECK(GetPixel(dc, (item.left + item.right) / 2, item.top + wcw::DipToPx(7, dpi)) ==
          RGB(selected.r, selected.g, selected.b));
    if (focusedIndex >= 0) {
        CHECK(TabCtrl_GetItemRect(tab, focusedIndex, &item));
        CHECK(GetPixel(dc, (item.left + item.right) / 2, item.top + wcw::DipToPx(5, dpi)) ==
              RGB(255, 0, 255));
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(tab, screen);
}

} // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    CHECK(wcw::Initialize(instance));
    const WNDCLASSW cls{.lpfnWndProc = ParentProc, .hInstance = instance,
                       .lpszClassName = L"WcwTabsTestParent"};
    CHECK(RegisterClassW(&cls));
    const auto parent = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"",
        WS_OVERLAPPEDWINDOW, 0, 0, 700, 400, nullptr, nullptr, instance, nullptr);
    wcw::TabControlOptions options;
    options.parent = parent;
    options.id = 71;
    options.bounds = {10, 10, 420, 44};
    options.style = WS_VISIBLE;
    options.accessibleName = L"Views";
    options.items = {{L"Overview", 101}, {L"Settings", 202}};
    const auto tab = wcw::CreateTabControl(options);
    CHECK(tab);
    if (!tab) return 1;
    CHECK(wcw::GetTabSelection(tab) == 0);
    CHECK(notifications == 0);

    wcw::TextBoxOptions page;
    page.parent = parent;
    page.bounds = {10, 70, 300, 40};
    page.style = WS_VISIBLE;
    page.text = L"Page one keeps its text";
    pages[0] = wcw::CreateTextBox(page);
    page.style = 0;
    page.text = L"Page two";
    pages[1] = wcw::CreateTextBox(page);
    CHECK(pages[0] && pages[1]);
    ShowWindow(parent, SW_SHOWNOACTIVATE);

    CHECK(wcw::SetTabSelection(tab, 1));
    CHECK(notifications == 1);
    CHECK(last.header.hwndFrom == tab && last.header.idFrom == 71);
    CHECK(last.oldIndex == 0 && last.newIndex == 1);
    CHECK(last.oldId == 101 && last.newId == 202);
    CHECK(!IsWindowVisible(pages[0]) && IsWindowVisible(pages[1]));
    CHECK(wcw::SetTabSelection(tab, 1));
    CHECK(notifications == 1);
    ClickTab(tab, 0);
    CHECK(wcw::GetTabSelection(tab) == 0);
    CHECK(notifications == 2);
    CHECK(IsWindowVisible(pages[0]) && !IsWindowVisible(pages[1]));
    CHECK(wcw::GetTextBoxText(pages[0]) == L"Page one keeps its text");
    SetFocus(tab);
    CHECK(wcw::SetStyleOverride(tab, {.focus = wcw::Color::FromRgb(255, 0, 255),
                                     .focusWidthDip = 4.0f, .cornerRadiusDip = 0.0f}));
    SendMessageW(tab, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessageW(tab, WM_KEYUP, VK_RIGHT, 0);
    CHECK(TabCtrl_GetCurFocus(tab) == 1);
    CheckPaint(tab, wcw::DarkTheme().palette.selected, 1);
    CHECK(wcw::ClearStyleOverride(tab));
    // Native button-style tabs move focus with arrows and activate with Space.
    SendMessageW(tab, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(tab, WM_KEYUP, VK_SPACE, 0);
    CHECK(wcw::GetTabSelection(tab) == 1);
    CHECK(notifications == 3);

    IAccessible* accessible{};
    CHECK(SUCCEEDED(AccessibleObjectFromWindow(tab, OBJID_CLIENT, IID_IAccessible,
                                             reinterpret_cast<void**>(&accessible))));
    if (accessible) {
        VARIANT self{};
        self.vt = VT_I4;
        VARIANT role{};
        CHECK(SUCCEEDED(accessible->get_accRole(self, &role)));
        CHECK(role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PAGETABLIST);
        BSTR name{};
        CHECK(SUCCEEDED(accessible->get_accName(self, &name)));
        CHECK(name && std::wstring(name) == L"Views");
        SysFreeString(name);
        self.lVal = 2;
        CHECK(SUCCEEDED(accessible->get_accRole(self, &role)));
        CHECK(role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_PAGETAB);
        CHECK(SUCCEEDED(accessible->get_accName(self, &name)));
        CHECK(name && std::wstring(name) == L"Settings");
        SysFreeString(name);
        accessible->Release();
    }

    SetFocus(parent);
    CheckPaint(tab, wcw::DarkTheme().palette.selected);
    wcw::SetTheme(wcw::LightTheme());
    CheckPaint(tab, wcw::LightTheme().palette.selected);
    CHECK(wcw::SetStyleOverride(tab, {.selected = wcw::Color::FromRgb(220, 40, 90)}));
    CheckPaint(tab, wcw::Color::FromRgb(220, 40, 90));
    MoveWindow(tab, 10, 10, 300, 50, TRUE);
    RECT first{}, second{};
    CHECK(TabCtrl_GetItemRect(tab, 0, &first));
    CHECK(TabCtrl_GetItemRect(tab, 1, &second));
    CHECK(first.top == second.top && second.right <= 300);
    ClickTab(tab, 0);
    CHECK(wcw::GetTabSelection(tab) == 0);

    CHECK(!wcw::SetTabSelection(tab, -1));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(!wcw::SetTabSelection(tab, 2));
    CHECK(!wcw::SetTabSelection(parent, 0));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    ThreadCheck threadCheck{tab};
    const auto thread = CreateThread(nullptr, 0, WrongThread, &threadCheck, 0, nullptr);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!threadCheck.changed && threadCheck.error == ERROR_INVALID_WINDOW_HANDLE);
    options.items.clear();
    CHECK(!wcw::CreateTabControl(options));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    destroyOnChange = true;
    CHECK(wcw::SetTabSelection(tab, 1));
    CHECK(!IsWindow(tab));
    CHECK(wcw::GetTabSelection(tab) == -1);
    DestroyWindow(parent);
    wcw::Shutdown();
    return testFailures;
}
