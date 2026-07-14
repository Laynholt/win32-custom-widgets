#include "Test.h"

#include <wcw/Control.h>
#include <wcw/Runtime.h>

#include "../src/Internal.h"
#include "../src/Paint.h"

#include <windows.h>

#include <future>
#include <thread>

namespace {

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TestProc(HWND, UINT, WPARAM, LPARAM) { return 42; }

LRESULT ThrowingProc(HWND, UINT, WPARAM, LPARAM) { throw std::bad_alloc{}; }

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwRuntimeTestParent"};
    CHECK(RegisterClassW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

    const auto parent = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, instance, nullptr);
    CHECK(parent != nullptr);

    SetLastError(ERROR_SUCCESS);
    const bool initialized = wcw::Initialize(instance);
    if (!initialized) std::cerr << "Initialize failed with Win32 error " << GetLastError() << '\n';
    CHECK(initialized);
    CHECK(wcw::Initialize(instance));

    const auto light = wcw::LightTheme();
    wcw::SetTheme(light);
    CHECK(wcw::GetTheme().palette.window == light.palette.window);
    CHECK(wcw::GetTheme().palette.text == light.palette.text);

    wcw::StyleOverride overrideStyle;
    overrideStyle.background = wcw::Color{255, 1, 2, 3};
    CHECK(!wcw::SetStyleOverride(parent, overrideStyle));
    CHECK(!wcw::internal::WindowStyleOverride(parent).background.has_value());
    CHECK(!wcw::ClearStyleOverride(parent));

    wcw::internal::RegisterWindow(parent);
    CHECK(wcw::internal::IsLibraryWindow(parent, windowClass.lpszClassName));
    CHECK(!wcw::internal::IsLibraryWindow(parent, L"NotTheParentClass"));
    CHECK(wcw::SetStyleOverride(parent, overrideStyle));
    CHECK(wcw::internal::WindowStyleOverride(parent).background.has_value());
    CHECK(wcw::ClearStyleOverride(parent));
    wcw::internal::UnregisterWindow(parent);

    std::promise<HWND> workerWindow;
    std::promise<void> checkWorkerWindow;
    std::promise<bool> workerWindowUnchanged;
    auto worker = std::thread([&] {
        const auto window = CreateWindowExW(0, windowClass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                            HWND_MESSAGE, nullptr, instance, nullptr);
        wcw::internal::RegisterWindow(window);
        workerWindow.set_value(window);
        checkWorkerWindow.get_future().wait();
        workerWindowUnchanged.set_value(
            !wcw::internal::WindowStyleOverride(window).background.has_value());
        wcw::internal::UnregisterWindow(window);
        DestroyWindow(window);
    });
    const auto otherThreadWindow = workerWindow.get_future().get();
    CHECK(otherThreadWindow != nullptr);
    CHECK(!wcw::SetStyleOverride(otherThreadWindow, overrideStyle));
    CHECK(!wcw::ClearStyleOverride(otherThreadWindow));
    checkWorkerWindow.set_value();
    CHECK(workerWindowUnchanged.get_future().get());
    worker.join();

    CHECK(wcw::internal::SafeWindowProc<TestProc>(nullptr, WM_NULL, 0, 0) == 42);
    SetLastError(ERROR_SUCCESS);
    CHECK(wcw::internal::SafeWindowProc<ThrowingProc>(nullptr, WM_NCCREATE, 0, 0) == FALSE);
    CHECK(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);

    const auto screen = GetDC(nullptr);
    const auto target = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, 8, 8);
    const auto previous = SelectObject(target, bitmap);
    PatBlt(target, 0, 0, 8, 8, BLACKNESS);
    {
        const RECT dirty{2, 3, 6, 7};
        wcw::paint::Buffer buffer(target, dirty);
        CHECK(buffer);
        CHECK(SetPixel(buffer.dc(), 3, 4, RGB(255, 0, 0)) != CLR_INVALID);
    }
    CHECK(GetPixel(target, 3, 4) == RGB(255, 0, 0));
    CHECK(GetPixel(target, 2, 3) == RGB(0, 0, 0));
    SelectObject(target, previous);
    DeleteObject(bitmap);
    DeleteDC(target);
    ReleaseDC(nullptr, screen);

    const wcw::RectDip bounds{1, 2, 3, 4};
    CHECK(bounds.x == 1 && bounds.y == 2 && bounds.width == 3 && bounds.height == 4);

    DestroyWindow(parent);
    wcw::Shutdown();
    wcw::Shutdown();
    return testFailures;
}
