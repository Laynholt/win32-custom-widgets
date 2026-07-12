#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include <windows.h>

namespace {

constexpr wchar_t ParentClass[] = L"WcwTooltipTestParent";
constexpr wchar_t PopupClass[] = L"WcwTooltipPopup";

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
    CHECK(parent != nullptr);
    CHECK(target != nullptr);

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

    wcw::TooltipOptions options{
        .text = L"A deliberately long tooltip line that must wrap at the configured maximum width.",
        .initialDelayMs = 0,
        .reshowDelayMs = 0,
        .autopopDelayMs = 40,
        .maxWidthDip = 110,
        .appearance = {.cornerRadiusDip = 3.0f}};
    CHECK(wcw::AttachTooltip(target, options));
    ShowWindow(parent, SW_SHOWNOACTIVATE);
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

    CHECK(wcw::AttachTooltip(target, options));
    SendMessageW(target, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    wcw::HideAllTooltips();
    CHECK(!IsWindowVisible(Popup()));
    DestroyWindow(target);
    Pump();
    CHECK(!IsWindow(Popup()));

    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
