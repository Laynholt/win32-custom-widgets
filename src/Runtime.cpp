#include <wcw/Runtime.h>
#include <wcw/Geometry.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <commctrl.h>
#include <gdiplus.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace wcw {
namespace {

struct RuntimeState {
    HINSTANCE instance{};
    ULONG_PTR gdiplusToken{};
    bool oleInitialized{};
    DWORD uiThread{};
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

void UpdateWindowRegion(HWND window) {
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) return;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const auto found = State().overrides.find(window);
    const auto local = found == State().overrides.end() ? StyleOverride{} : found->second;
    const auto style = ResolveStyle(State().theme, local);
    const int radius = (std::max)(
        0, (std::min)({DipToPx(style.cornerRadiusDip, paint::Dpi(window)), width / 2, height / 2}));
    if (!radius) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }
    const auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                           radius * 2, radius * 2);
    if (region && !SetWindowRgn(window, region, TRUE)) DeleteObject(region);
}

} // namespace

bool Initialize(HINSTANCE instance) {
    auto& state = State();
    const auto thread = GetCurrentThreadId();
    if (state.gdiplusToken) {
        if (state.uiThread == thread) return true;
        SetLastError(ERROR_INVALID_THREAD_ID);
        return false;
    }
    if (!instance) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    const auto ole = OleInitialize(nullptr);
    if (FAILED(ole) && ole != RPC_E_CHANGED_MODE) return false;
    state.oleInitialized = SUCCEEDED(ole);

    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) {
        if (state.oleInitialized) {
            OleUninitialize();
            state.oleInitialized = false;
        }
        return false;
    }

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&state.gdiplusToken, &input, nullptr) != Gdiplus::Ok) {
        state.gdiplusToken = 0;
        if (state.oleInitialized) {
            OleUninitialize();
            state.oleInitialized = false;
        }
        SetLastError(ERROR_DLL_INIT_FAILED);
        return false;
    }
    state.instance = instance;
    if (!internal::RegisterButtonClasses() || !internal::RegisterDisplayClasses() ||
        !internal::RegisterTextBoxClass() || !internal::RegisterBooleanControlClasses() ||
        !internal::RegisterSliderClass() || !internal::RegisterProgressBarClass() ||
        !internal::RegisterComboBoxClasses() || !internal::RegisterScrollViewClasses() ||
        !internal::RegisterTooltipClass() || !internal::RegisterMenuClass()) {
        Gdiplus::GdiplusShutdown(state.gdiplusToken);
        state.gdiplusToken = 0;
        state.instance = nullptr;
        if (state.oleInitialized) {
            OleUninitialize();
            state.oleInitialized = false;
        }
        return false;
    }
    state.uiThread = thread;
    return true;
}

void Shutdown() {
    auto& state = State();
    if (!state.gdiplusToken) return;
    if (state.uiThread != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_THREAD_ID);
        return;
    }
    internal::ShutdownAccessibility();
    paint::ClearFontCache();
    Gdiplus::GdiplusShutdown(state.gdiplusToken);
    state.gdiplusToken = 0;
    state.instance = nullptr;
    state.uiThread = 0;
    if (state.oleInitialized) {
        OleUninitialize();
        state.oleInitialized = false;
    }
    state.windows.clear();
    state.overrides.clear();
}

void SetTheme(const Theme& theme) {
    auto& state = State();
    state.theme = theme;
    paint::ClearFontCache();
    for (const auto window : state.windows) {
        if (IsWindow(window)) PostMessageW(window, internal::ThemeChangedMessage, 0, 0);
    }
}

Theme GetTheme() {
    auto& state = State();
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

bool IsLibraryWindow(HWND window, const wchar_t* className) {
    if (!IsOwnedLibraryWindow(window)) return false;
    wchar_t actual[32]{};
    if (className && (!GetClassNameW(window, actual, 32) ||
                      lstrcmpW(actual, className) != 0)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

StyleOverride WindowStyleOverride(HWND window) {
    const auto found = State().overrides.find(window);
    return found == State().overrides.end() ? StyleOverride{} : found->second;
}

bool HandleControlMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                          LRESULT& result) {
    if (HandleAccessibilityMessage(window, message, wParam, lParam, result)) return true;
    if (message == WM_SIZE || message == ThemeChangedMessage) UpdateWindowRegion(window);
    switch (message) {
    case WM_ERASEBKGND:
        result = 1;
        return true;
    case ThemeChangedMessage:
        InvalidateRect(window, nullptr, FALSE);
        result = 0;
        return true;
    case WM_NCDESTROY:
        DestroyAccessibility(window);
        UnregisterWindow(window);
        return false;
    case WM_SETFOCUS:
        NotifyAccessibilityFocus(window);
        return false;
    case WM_ENABLE:
        NotifyAccessibility(window, EVENT_OBJECT_STATECHANGE);
        return false;
    case WM_SHOWWINDOW:
        NotifyAccessibility(window, wParam ? EVENT_OBJECT_SHOW : EVENT_OBJECT_HIDE);
        return false;
    default:
        return false;
    }
}

} // namespace internal
} // namespace wcw
