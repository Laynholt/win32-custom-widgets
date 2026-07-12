#include <wcw/Runtime.h>

#include "Internal.h"
#include "Paint.h"

#include <commctrl.h>
#include <gdiplus.h>

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace wcw {
namespace {

struct RuntimeState {
    std::mutex mutex;
    HINSTANCE instance{};
    ULONG_PTR gdiplusToken{};
    bool commonControlsInitialized{};
    Theme theme{DarkTheme()};
    std::unordered_set<HWND> windows;
    std::unordered_map<HWND, StyleOverride> overrides;
};

RuntimeState& State() {
    static RuntimeState state;
    return state;
}

bool IsOwnedLibraryWindow(HWND window) {
    if (!IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        !State().windows.contains(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

} // namespace

bool Initialize(HINSTANCE instance) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (state.gdiplusToken) return true;
    if (!instance) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    if (!state.commonControlsInitialized) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
        if (!InitCommonControlsEx(&controls)) return false;
        state.commonControlsInitialized = true;
    }

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&state.gdiplusToken, &input, nullptr) != Gdiplus::Ok) {
        state.gdiplusToken = 0;
        SetLastError(ERROR_DLL_INIT_FAILED);
        return false;
    }
    state.instance = instance;
    if (!internal::RegisterButtonClasses() || !internal::RegisterDisplayClasses() ||
        !internal::RegisterTextBoxClass() || !internal::RegisterBooleanControlClasses() ||
        !internal::RegisterSliderClass() || !internal::RegisterProgressBarClass()) {
        Gdiplus::GdiplusShutdown(state.gdiplusToken);
        state.gdiplusToken = 0;
        state.instance = nullptr;
        return false;
    }
    return true;
}

void Shutdown() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (!state.gdiplusToken) return;
    paint::ClearFontCache();
    Gdiplus::GdiplusShutdown(state.gdiplusToken);
    state.gdiplusToken = 0;
    state.instance = nullptr;
    state.windows.clear();
    state.overrides.clear();
}

void SetTheme(const Theme& theme) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    state.theme = theme;
    paint::ClearFontCache();
    for (const auto window : state.windows) {
        if (IsWindow(window)) PostMessageW(window, internal::ThemeChangedMessage, 0, 0);
    }
}

Theme GetTheme() {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    return state.theme;
}

bool SetStyleOverride(HWND window, const StyleOverride& style) {
    if (!IsOwnedLibraryWindow(window)) return false;
    State().overrides[window] = style;
    SendMessageW(window, internal::ThemeChangedMessage, 0, 0);
    return true;
}

bool ClearStyleOverride(HWND window) {
    if (!IsOwnedLibraryWindow(window)) return false;
    State().overrides.erase(window);
    SendMessageW(window, internal::ThemeChangedMessage, 0, 0);
    return true;
}

namespace internal {

HINSTANCE Instance() {
    return State().instance;
}

bool RegisterControlClass(const wchar_t* name, WNDPROC procedure, UINT style) {
    if (!name || !procedure || !State().instance) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = style;
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = State().instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = name;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void RegisterWindow(HWND window) {
    if (window) State().windows.insert(window);
}

void UnregisterWindow(HWND window) {
    State().windows.erase(window);
    State().overrides.erase(window);
}

bool IsLibraryWindow(HWND window) { return IsOwnedLibraryWindow(window); }

StyleOverride WindowStyleOverride(HWND window) {
    const auto found = State().overrides.find(window);
    return found == State().overrides.end() ? StyleOverride{} : found->second;
}

bool HandleControlMessage(HWND window, UINT message, LRESULT& result) {
    switch (message) {
    case WM_ERASEBKGND:
        result = 1;
        return true;
    case ThemeChangedMessage:
        InvalidateRect(window, nullptr, FALSE);
        result = 0;
        return true;
    case WM_NCDESTROY:
        UnregisterWindow(window);
        return false;
    default:
        return false;
    }
}

} // namespace internal
} // namespace wcw
