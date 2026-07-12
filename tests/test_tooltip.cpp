#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include <windows.h>

namespace {

constexpr wchar_t ParentClass[] = L"WcwTooltipTestParent";
constexpr wchar_t PopupClass[] = L"WcwTooltipPopup";
constexpr UINT_PTR ConsumerTimerId = 0x57435453;
constexpr UINT_PTR TooltipAutopopTimerId = 0x57435441;

int consumerTimerTicks{};

void CALLBACK ConsumerTimerProc(HWND, UINT, UINT_PTR, DWORD) { ++consumerTimerTicks; }

void Pump(DWORD milliseconds = 0) {
    const auto until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount64() >= until) break;
        Sleep(1);
    } while (true);
}

HWND Popup() { return FindWindowW(PopupClass, nullptr); }

struct ThreadCheck {
    HWND target{};
    bool result{true};
    DWORD error{};
};

DWORD WINAPI AttachOnWrongThread(void* parameter) {
    auto& check = *static_cast<ThreadCheck*>(parameter);
    SetLastError(ERROR_SUCCESS);
    check.result = wcw::AttachTooltip(check.target, {.text = L"wrong thread"});
    check.error = GetLastError();
    return 0;
}

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass{.lpfnWndProc = DefWindowProcW,
                                .hInstance = instance,
                                .lpszClassName = ParentClass};
    CHECK(RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    CHECK(wcw::Initialize(instance));

    const auto parent = CreateWindowExW(0, ParentClass, L"", WS_OVERLAPPEDWINDOW,
                                        20, 20, 320, 200, nullptr, nullptr, instance, nullptr);
    const auto target = CreateWindowExW(0, L"STATIC", L"target", WS_CHILD | WS_VISIBLE,
                                        10, 10, 120, 30, parent, nullptr, instance, nullptr);
    const auto sibling = CreateWindowExW(0, L"EDIT", L"sibling", WS_CHILD | WS_VISIBLE,
                                         10, 50, 120, 30, parent, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(target != nullptr);
    CHECK(sibling != nullptr);

    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::AttachTooltip(nullptr, {.text = L"invalid"}));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::AttachTooltip(target, {.text = L"", .maxWidthDip = 0}));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    ThreadCheck threadCheck{target};
    const auto thread = CreateThread(nullptr, 0, AttachOnWrongThread, &threadCheck, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!threadCheck.result);
    CHECK(threadCheck.error == ERROR_INVALID_WINDOW_HANDLE);

    wcw::TooltipOptions delayed{.text = L"delayed",
                                .initialDelayMs = 1000,
                                .reshowDelayMs = 1000,
                                .autopopDelayMs = 0,
                                .maxWidthDip = 120};
    CHECK(wcw::AttachTooltip(target, delayed));
    CHECK(SetTimer(target, ConsumerTimerId, 10, ConsumerTimerProc) == ConsumerTimerId);
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    Pump(40);
    CHECK(consumerTimerTicks > 0);
    CHECK(KillTimer(target, ConsumerTimerId));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));

    auto immediate = delayed;
    immediate.text = L"short";
    immediate.initialDelayMs = 0;
    immediate.reshowDelayMs = 0;
    immediate.autopopDelayMs = 30;
    CHECK(wcw::AttachTooltip(target, immediate));
    CHECK(IsWindowVisible(Popup()));
    RECT shortBounds{};
    GetWindowRect(Popup(), &shortBounds);

    auto updated = immediate;
    updated.text = L"Updated tooltip text that is substantially longer and must be remeasured.";
    updated.maxWidthDip = 180;
    updated.autopopDelayMs = 0;
    CHECK(wcw::AttachTooltip(target, updated));
    RECT updatedBounds{};
    GetWindowRect(Popup(), &updatedBounds);
    CHECK(updatedBounds.right - updatedBounds.left != shortBounds.right - shortBounds.left ||
          updatedBounds.bottom - updatedBounds.top != shortBounds.bottom - shortBounds.top);
    SendMessageW(Popup(), WM_TIMER, TooltipAutopopTimerId, 0);
    CHECK(IsWindowVisible(Popup()));

    updated.autopopDelayMs = 20;
    CHECK(wcw::AttachTooltip(target, updated));
    SendMessageW(Popup(), WM_TIMER, TooltipAutopopTimerId, 0);
    CHECK(!IsWindowVisible(Popup()));
    CHECK(wcw::DetachTooltip(target));

    wcw::TooltipOptions options{
        .text = L"A deliberately long tooltip line that must wrap at the configured maximum width.",
        .initialDelayMs = 0,
        .reshowDelayMs = 0,
        .autopopDelayMs = 40,
        .maxWidthDip = 110,
        .appearance = {.cornerRadiusDip = 3.0f}};
    CHECK(wcw::AttachTooltip(target, options));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));

    const auto popup = Popup();
    CHECK(popup != nullptr);
    CHECK(IsWindowVisible(popup));
    const auto extended = static_cast<DWORD>(GetWindowLongPtrW(popup, GWL_EXSTYLE));
    CHECK((extended & WS_EX_NOACTIVATE) != 0);
    CHECK((extended & WS_EX_TOPMOST) != 0);
    CHECK(GetFocus() != popup);
    RECT popupBounds{};
    GetWindowRect(popup, &popupBounds);
    CHECK(popupBounds.right - popupBounds.left <=
          wcw::DipToPx(options.maxWidthDip, GetDpiForWindow(target)) +
              2 * wcw::DipToPx(12.0f, GetDpiForWindow(target)));
    CHECK(popupBounds.bottom - popupBounds.top > wcw::DipToPx(24.0f, GetDpiForWindow(target)));
    MONITORINFO monitor{sizeof(monitor)};
    CHECK(GetMonitorInfoW(MonitorFromWindow(popup, MONITOR_DEFAULTTONEAREST), &monitor));
    CHECK(popupBounds.left >= monitor.rcWork.left);
    CHECK(popupBounds.top >= monitor.rcWork.top);
    CHECK(popupBounds.right <= monitor.rcWork.right);
    CHECK(popupBounds.bottom <= monitor.rcWork.bottom);

    Pump(80);
    CHECK(!IsWindowVisible(popup));

    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    MoveWindow(target, 20, 20, 120, 30, TRUE);
    Pump();
    CHECK(!IsWindowVisible(Popup()));

    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    ShowWindow(parent, SW_HIDE);
    Pump();
    CHECK(!IsWindowVisible(Popup()));

    ShowWindow(parent, SW_SHOWNOACTIVATE);
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(sibling, WM_KEYDOWN, VK_SPACE, 0);
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    CHECK(PostMessageW(sibling, WM_KEYDOWN, VK_SPACE, 0));
    Pump();
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(sibling, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(parent, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(2, 2));
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(parent, WM_ACTIVATEAPP, FALSE, 0);
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    EnableWindow(target, FALSE);
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(!IsWindowVisible(Popup()));
    EnableWindow(target, TRUE);
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    CHECK(!IsWindowVisible(Popup()));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    CHECK(IsWindowVisible(Popup()));
    SendMessageW(target, WM_KEYDOWN, VK_SPACE, 0);
    CHECK(!IsWindowVisible(Popup()));

    CHECK(wcw::DetachTooltip(target));
    CHECK(!IsWindow(Popup()));
    CHECK(!wcw::DetachTooltip(target));

    CHECK(wcw::AttachTooltip(target, delayed));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    SendMessageW(sibling, WM_KEYDOWN, VK_SPACE, 0);
    CHECK(wcw::AttachTooltip(target, immediate));
    CHECK(!IsWindowVisible(Popup()));
    CHECK(wcw::AttachTooltip(target, delayed));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    SendMessageW(parent, WM_ACTIVATEAPP, FALSE, 0);
    CHECK(wcw::AttachTooltip(target, immediate));
    CHECK(!IsWindowVisible(Popup()));
    CHECK(wcw::DetachTooltip(target));

    CHECK(wcw::AttachTooltip(target, options));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    wcw::HideAllTooltips();
    CHECK(!IsWindowVisible(Popup()));
    DestroyWindow(target);
    Pump();
    CHECK(!IsWindow(Popup()));

    wcw::Shutdown();
    DestroyWindow(sibling);
    DestroyWindow(parent);
    return testFailures;
}
