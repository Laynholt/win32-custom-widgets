#include "Test.h"

#include "../src/ScrollModel.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <cmath>
#include <limits>

namespace {

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

struct ThreadCheck {
    HWND scrollView{};
    bool result{};
    DWORD error{};
};

DWORD WINAPI CheckWrongThread(void* value) {
    auto& check = *static_cast<ThreadCheck*>(value);
    SetLastError(ERROR_SUCCESS);
    check.result = wcw::SetScrollOffset(check.scrollView, {1, 1});
    check.error = GetLastError();
    return 0;
}

bool Near(float left, float right) { return std::abs(left - right) < 0.01f; }

POINT PositionIn(HWND child, HWND ancestor) {
    POINT point{};
    MapWindowPoints(child, ancestor, &point, 1);
    return point;
}

} // namespace

int main() {
    using wcw::internal::ScrollModel;

    ScrollModel model;
    model.SetViewport({100, 80});
    model.SetExtent({90, 70});
    CHECK(model.Maximum() == wcw::ScrollOffsetDip{});
    CHECK(!model.SetOffset({25, 10}));
    model.SetExtent({350, 240});
    CHECK((model.Maximum() == wcw::ScrollOffsetDip{250, 160}));
    CHECK(model.SetOffset({999, 999}));
    CHECK(model.Offset() == model.Maximum());
    CHECK(model.SetOffset({125, 80}));
    model.SetViewport({120, 100});
    CHECK((model.Offset() == wcw::ScrollOffsetDip{125, 80}));
    model.SetViewport({300, 220});
    CHECK((model.Offset() == wcw::ScrollOffsetDip{50, 20}));

    CHECK(model.ConsumeWheelDelta(-WHEEL_DELTA / 2, false) == 0);
    CHECK(model.ConsumeWheelDelta(-WHEEL_DELTA / 2, false) == -1);
    CHECK(model.ConsumeWheelDelta(WHEEL_DELTA * 2, true) == 2);
    CHECK(model.ConsumeWheelDelta(0, true) == 0);

    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwScrollTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 500, 400, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::ScrollViewOptions options;
    options.parent = parent;
    options.id = 81;
    options.bounds = {10, 10, 200, 120};
    options.contentExtent = {600, 400};
    options.appearance.thumbSizeDip = 16.0f;
    const auto scrollView = wcw::CreateScrollView(options);
    CHECK(scrollView != nullptr);
    const auto content = wcw::GetScrollContentWindow(scrollView);
    CHECK(content != nullptr);
    CHECK(IsChild(scrollView, content));
    CHECK(GetDlgCtrlID(scrollView) == 81);

    ShowWindow(parent, SW_SHOW);
    ShowWindow(scrollView, SW_SHOW);
    CHECK(wcw::SetScrollOffset(scrollView, {100, 70}));
    auto offset = wcw::GetScrollOffset(scrollView);
    CHECK(offset.has_value());
    CHECK(Near(offset->x, 100) && Near(offset->y, 70));
    auto position = PositionIn(content, scrollView);
    CHECK(position.x == -100 && position.y == -70);

    SendMessageW(scrollView, WM_KEYDOWN, VK_END, 0);
    const auto end = wcw::GetScrollOffset(scrollView);
    CHECK(end && end->y > 270);
    SendMessageW(scrollView, WM_KEYDOWN, VK_HOME, 0);
    CHECK(wcw::GetScrollOffset(scrollView)->y == 0);
    SendMessageW(scrollView, WM_KEYDOWN, VK_NEXT, 0);
    const auto pageDown = wcw::GetScrollOffset(scrollView)->y;
    CHECK(pageDown > 90);
    SendMessageW(scrollView, WM_KEYDOWN, VK_PRIOR, 0);
    CHECK(wcw::GetScrollOffset(scrollView)->y == 0);

    SendMessageW(scrollView, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    CHECK(wcw::GetScrollOffset(scrollView)->y == 0);
    SendMessageW(scrollView, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), 0);
    CHECK(wcw::GetScrollOffset(scrollView)->y > 0);
    const auto beforeHorizontal = wcw::GetScrollOffset(scrollView)->x;
    SendMessageW(scrollView, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, -WHEEL_DELTA), 0);
    CHECK(wcw::GetScrollOffset(scrollView)->x > beforeHorizontal);

    CHECK(wcw::SetScrollOffset(scrollView, {0, 0}));
    SendMessageW(scrollView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(192, 5));
    CHECK(GetCapture() == scrollView);
    SendMessageW(scrollView, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(192, 110));
    SendMessageW(scrollView, WM_LBUTTONUP, 0, MAKELPARAM(192, 110));
    CHECK(GetCapture() != scrollView);
    CHECK(wcw::GetScrollOffset(scrollView)->y > 250);

    CHECK(wcw::SetScrollOffset(scrollView, {0, 0}));
    SendMessageW(scrollView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 112));
    CHECK(GetCapture() == scrollView);
    SendMessageW(scrollView, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(190, 112));
    SendMessageW(scrollView, WM_LBUTTONUP, 0, MAKELPARAM(190, 112));
    CHECK(wcw::GetScrollOffset(scrollView)->x > 350);

    CHECK(wcw::SetScrollOffset(scrollView, {0, 0}));
    SendMessageW(scrollView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(192, 5));
    CHECK(GetCapture() == scrollView);
    SendMessageW(scrollView, WM_CANCELMODE, 0, 0);
    CHECK(GetCapture() != scrollView);
    SendMessageW(scrollView, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(192, 5));
    ShowWindow(scrollView, SW_HIDE);
    CHECK(GetCapture() != scrollView);
    ShowWindow(scrollView, SW_SHOW);

    CHECK(wcw::SetScrollOffset(scrollView, {150, 100}));
    SetWindowPos(scrollView, nullptr, 10, 10, 240, 150, SWP_NOZORDER | SWP_NOACTIVATE);
    offset = wcw::GetScrollOffset(scrollView);
    CHECK(offset && Near(offset->x, 150) && Near(offset->y, 100));
    SetWindowPos(scrollView, nullptr, 10, 10, 580, 390, SWP_NOZORDER | SWP_NOACTIVATE);
    offset = wcw::GetScrollOffset(scrollView);
    CHECK(offset && offset->x < 40 && offset->y < 30);

    CHECK(wcw::SetScrollContentExtent(scrollView, {100, 80}));
    CHECK(wcw::GetScrollOffset(scrollView) == wcw::ScrollOffsetDip{});
    CHECK(PositionIn(content, scrollView).x == 0);

    ThreadCheck threadCheck{scrollView};
    const auto thread = CreateThread(nullptr, 0, CheckWrongThread, &threadCheck, 0, nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!threadCheck.result);
    CHECK(threadCheck.error == ERROR_INVALID_WINDOW_HANDLE);

    SetLastError(ERROR_SUCCESS);
    CHECK(!wcw::SetScrollOffset(parent, {1, 1}));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!wcw::GetScrollOffset(parent).has_value());
    CHECK(wcw::GetScrollContentWindow(parent) == nullptr);
    CHECK(!wcw::SetScrollContentExtent(scrollView,
                                       {std::numeric_limits<float>::quiet_NaN(), 10}));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    auto invalid = options;
    invalid.contentExtent.width = -1;
    SetLastError(ERROR_SUCCESS);
    CHECK(wcw::CreateScrollView(invalid) == nullptr);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);

    DestroyWindow(scrollView);
    CHECK(!IsWindow(content));
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
