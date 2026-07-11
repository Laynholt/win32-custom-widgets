#include "Test.h"

#include <wcw/Control.h>
#include <wcw/Runtime.h>

#include <windows.h>

namespace {

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

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

    const wcw::RectDip bounds{1, 2, 3, 4};
    CHECK(bounds.x == 1 && bounds.y == 2 && bounds.width == 3 && bounds.height == 4);

    DestroyWindow(parent);
    wcw::Shutdown();
    wcw::Shutdown();
    return testFailures;
}
