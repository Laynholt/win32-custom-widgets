#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <memory>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wcw {
namespace {

constexpr wchar_t TooltipClass[] = L"WcwTooltipPopup";
constexpr UINT_PTR TooltipSubclassId = 0x57435454;
constexpr UINT_PTR ShowTimerId = 0x57435453;
constexpr UINT_PTR AutopopTimerId = 0x57435441;
constexpr ULONGLONG ReshowWindowMs = 1000;

struct Association {
    TooltipOptions options;
    std::vector<HWND> ancestors;
};

struct TooltipManager {
    std::unordered_map<HWND, std::unique_ptr<Association>> associations;
    HWND popup{};
    HWND current{};
    HWND pending{};
    HHOOK callHook{};
    HHOOK messageHook{};
    TooltipOptions visibleOptions;
    UINT_PTR autopopTimer{};
    ULONGLONG lastHideTick{};
};

thread_local TooltipManager manager;

LRESULT CALLBACK TooltipProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TargetProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT CALLBACK CallHookProc(int, WPARAM, LPARAM);
LRESULT CALLBACK MessageHookProc(int, WPARAM, LPARAM);

bool UsableTarget(HWND target) {
    if (!IsWindow(target) || !IsWindowVisible(target) || !IsWindowEnabled(target)) return false;
    for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor))
        if (!IsWindowVisible(ancestor) || !IsWindowEnabled(ancestor)) return false;
    return true;
}

void CancelPending(HWND target = nullptr) {
    if (target && manager.pending != target) return;
    if (manager.popup && manager.pending) KillTimer(manager.popup, ShowTimerId);
    manager.pending = nullptr;
}

void HideTooltip() {
    if (manager.popup && manager.autopopTimer) {
        KillTimer(manager.popup, manager.autopopTimer);
        manager.autopopTimer = 0;
    }
    const bool wasVisible = manager.popup &&
        (GetWindowLongPtrW(manager.popup, GWL_STYLE) & WS_VISIBLE) != 0;
    if (manager.popup) ShowWindow(manager.popup, SW_HIDE);
    manager.current = nullptr;
    if (wasVisible) manager.lastHideTick = GetTickCount64();
}

void DestroyPopupIfUnused() {
    if (!manager.associations.empty()) return;
    CancelPending();
    HideTooltip();
    if (const auto popup = std::exchange(manager.popup, nullptr); IsWindow(popup))
        DestroyWindow(popup);
    if (const auto hook = std::exchange(manager.messageHook, nullptr))
        UnhookWindowsHookEx(hook);
    if (const auto hook = std::exchange(manager.callHook, nullptr))
        UnhookWindowsHookEx(hook);
}

bool EnsurePopup() {
    if (manager.popup && IsWindow(manager.popup)) return true;
    manager.popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                    TooltipClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                    internal::Instance(), nullptr);
    ControlOptions accessible;
    internal::RegisterAccessibility(manager.popup, internal::AccessibleKind::Tooltip, accessible);
    return manager.popup != nullptr;
}

bool EnsureInputHook() {
    if (manager.callHook && manager.messageHook) return true;
    manager.callHook = SetWindowsHookExW(WH_CALLWNDPROC, CallHookProc, nullptr,
                                         GetCurrentThreadId());
    if (!manager.callHook) return false;
    manager.messageHook = SetWindowsHookExW(WH_GETMESSAGE, MessageHookProc, nullptr,
                                            GetCurrentThreadId());
    if (manager.messageHook) return true;
    const auto error = GetLastError();
    UnhookWindowsHookEx(std::exchange(manager.callHook, nullptr));
    SetLastError(error);
    return false;
}

unsigned MonitorDpi(HMONITOR monitor, HWND fallback) {
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static const auto getDpiForMonitor = [] {
        const auto module = LoadLibraryW(L"shcore.dll");
        return module ? reinterpret_cast<GetDpiForMonitorFn>(
                            GetProcAddress(module, "GetDpiForMonitor"))
                      : nullptr;
    }();
    UINT x{}, y{};
    if (getDpiForMonitor && SUCCEEDED(getDpiForMonitor(monitor, 0, &x, &y)) && x) return x;
    return paint::Dpi(manager.popup && IsWindow(manager.popup) ? manager.popup : fallback);
}

SIZE MeasureTooltip(HWND target, const TooltipOptions& options, unsigned dpi,
                    int workWidth, int workHeight) {
    const auto theme = GetTheme();
    const auto style = ResolveStyle(theme, options.appearance);
    const int paddingX = std::max(0, DipToPx(style.paddingXDip, dpi));
    const int paddingY = std::max(0, DipToPx(style.paddingYDip, dpi));
    const int maximum = std::max(1, std::min(DipToPx(options.maxWidthDip, dpi),
                                              std::max(1, workWidth - paddingX * 2)));
    RECT measured{0, 0, maximum, 0};
    const auto dc = GetDC(target);
    if (dc) {
        const auto oldFont = SelectObject(dc, paint::Font(style.font, dpi));
        DrawTextW(dc, options.text.c_str(), static_cast<int>(options.text.size()), &measured,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(target, dc);
    }
    return {std::min(workWidth,
                     std::min(maximum, std::max(1, static_cast<int>(measured.right))) +
                         paddingX * 2),
            std::min(workHeight,
                     std::max(1, static_cast<int>(measured.bottom)) + paddingY * 2)};
}

void PositionPopup(HWND target, const TooltipOptions& options) {
    if (!manager.popup || !IsWindow(manager.popup) || !IsWindow(target)) return;
    POINT cursor{};
    GetCursorPos(&cursor);
    const auto nearest = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(nearest, &monitor)) return;
    const int left = static_cast<int>(monitor.rcWork.left);
    const int top = static_cast<int>(monitor.rcWork.top);
    const int right = static_cast<int>(monitor.rcWork.right);
    const int bottom = static_cast<int>(monitor.rcWork.bottom);
    const auto dpi = MonitorDpi(nearest, target);
    const auto size = MeasureTooltip(target, options, dpi, std::max(1, right - left),
                                     std::max(1, bottom - top));
    int x = cursor.x + DipToPx(12, dpi);
    int y = cursor.y + DipToPx(20, dpi);
    x = std::clamp(x, left, std::max(left, right - static_cast<int>(size.cx)));
    y = std::clamp(y, top, std::max(top, bottom - static_cast<int>(size.cy)));
    SetWindowPos(manager.popup, HWND_TOPMOST, x, y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const auto style = ResolveStyle(GetTheme(), options.appearance);
    const int radius = std::max(0, DipToPx(style.cornerRadiusDip, dpi));
    const auto region = radius
                            ? CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1,
                                                 radius * 2, radius * 2)
                            : CreateRectRgn(0, 0, size.cx + 1, size.cy + 1);
    if (region && !SetWindowRgn(manager.popup, region, TRUE)) DeleteObject(region);
    InvalidateRect(manager.popup, nullptr, FALSE);
}

void RestartAutopopTimer() {
    if (manager.popup && manager.autopopTimer)
        KillTimer(manager.popup, manager.autopopTimer);
    manager.autopopTimer = 0;
    if (manager.popup && manager.visibleOptions.autopopDelayMs)
        manager.autopopTimer = SetTimer(manager.popup, AutopopTimerId,
                                        manager.visibleOptions.autopopDelayMs, nullptr);
}

void ShowTooltip(HWND target) {
    const auto found = manager.associations.find(target);
    if (found == manager.associations.end()) return;
    auto& association = *found->second;
    CancelPending(target);
    if (!UsableTarget(target) || association.options.text.empty() || !EnsurePopup()) return;
    TooltipOptions visible = association.options;
    if (manager.current && manager.current != target) HideTooltip();
    manager.current = target;
    manager.visibleOptions = std::move(visible);
    SetWindowTextW(manager.popup, manager.visibleOptions.text.c_str());
    PositionPopup(target, manager.visibleOptions);
    RestartAutopopTimer();
}

void ScheduleShow(HWND target, Association& association) {
    if (!UsableTarget(target) || manager.pending == target ||
        (manager.current == target && manager.popup && IsWindowVisible(manager.popup))) return;
    CancelPending();
    if (!EnsurePopup()) return;
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, target, 0};
    TrackMouseEvent(&tracking);
    const auto now = GetTickCount64();
    const bool reshow = manager.lastHideTick && now - manager.lastHideTick <= ReshowWindowMs;
    const UINT delay = reshow ? association.options.reshowDelayMs
                              : association.options.initialDelayMs;
    if (!delay)
        ShowTooltip(target);
    else {
        manager.pending = target;
        if (!SetTimer(manager.popup, ShowTimerId, delay, nullptr)) manager.pending = nullptr;
    }
}

void RemoveObservers(HWND target, Association& association) {
    RemoveWindowSubclass(target, TargetProc, TooltipSubclassId);
    for (const auto ancestor : association.ancestors)
        if (IsWindow(ancestor))
            RemoveWindowSubclass(ancestor, TargetProc, reinterpret_cast<UINT_PTR>(target));
    association.ancestors.clear();
}

bool DetachInternal(HWND target) {
    const auto found = manager.associations.find(target);
    if (found == manager.associations.end()) return false;
    auto& association = *found->second;
    CancelPending(target);
    if (manager.current == target) HideTooltip();
    RemoveObservers(target, association);
    manager.associations.erase(found);
    DestroyPopupIfUnused();
    return true;
}

void PaintTooltip(HWND window) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, manager.visibleOptions.appearance);
        const auto background = manager.visibleOptions.appearance.background.value_or(
            theme.palette.panel);
        paint::Clear(buffer.dc(), bounds, background);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::RectF surface{0, 0, static_cast<float>(bounds.right),
                                    static_cast<float>(bounds.bottom)};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        paint::Fill(graphics, surface, radius, background);
        paint::Border(graphics, surface, radius, style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));
        const int paddingX = std::max(0, DipToPx(style.paddingXDip, dpi));
        const int paddingY = std::max(0, DipToPx(style.paddingYDip, dpi));
        RECT textBounds{paddingX, paddingY, bounds.right - paddingX, bounds.bottom - paddingY};
        paint::Text(buffer.dc(), manager.visibleOptions.text, textBounds,
                    paint::Font(style.font, dpi), style.text,
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    }
    EndPaint(window, &ps);
}

LRESULT TooltipProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) internal::RegisterWindow(window);
    if (message == internal::ThemeChangedMessage && manager.current)
        PositionPopup(manager.current, manager.visibleOptions);
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_TIMER:
        if (wParam == ShowTimerId && manager.pending) {
            const auto pending = manager.pending;
            CancelPending();
            ShowTooltip(pending);
        } else if (manager.autopopTimer == wParam) {
            HideTooltip();
        }
        return 0;
    case WM_DPICHANGED:
        if (manager.current) PositionPopup(manager.current, manager.visibleOptions);
        return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_PAINT: PaintTooltip(window); return 0;
    case WM_NCDESTROY:
        if (manager.popup == window) {
            manager.popup = nullptr;
            manager.current = nullptr;
            manager.pending = nullptr;
            manager.autopopTimer = 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK TooltipProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return TooltipProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

bool DismissesTooltip(UINT message, WPARAM wParam) {
    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
    case WM_NCXBUTTONDOWN:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    case WM_ACTIVATEAPP:
        return !wParam;
    case WM_ACTIVATE:
        return LOWORD(wParam) == WA_INACTIVE;
    default:
        return false;
    }
}

void DismissForMessage(HWND window, UINT message, WPARAM wParam) {
    if (window != manager.popup && DismissesTooltip(message, wParam)) {
        CancelPending();
        HideTooltip();
    }
}

LRESULT CALLBACK CallHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const auto message = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (message) DismissForMessage(message->hwnd, message->message, message->wParam);
    }
    return CallNextHookEx(manager.callHook, code, wParam, lParam);
}

LRESULT CALLBACK MessageHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const auto message = reinterpret_cast<const MSG*>(lParam);
        if (message) DismissForMessage(message->hwnd, message->message, message->wParam);
    }
    return CallNextHookEx(manager.messageHook, code, wParam, lParam);
}

LRESULT TargetProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                       DWORD_PTR reference) {
    const auto target = reference ? reinterpret_cast<HWND>(reference) : window;
    const auto found = manager.associations.find(target);
    if (found == manager.associations.end())
        return DefSubclassProc(window, message, wParam, lParam);
    auto& association = *found->second;
    const bool isTarget = window == target;
    if (isTarget) {
        switch (message) {
        case WM_MOUSEMOVE: ScheduleShow(target, association); break;
        case WM_MOUSELEAVE:
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_WINDOWPOSCHANGING:
            CancelPending(target);
            if (manager.current == target) HideTooltip();
            break;
        default: break;
        }
    }
    if (((message == WM_SHOWWINDOW || message == WM_ENABLE) && !wParam) ||
        message == WM_WINDOWPOSCHANGING || message == WM_DESTROY) {
        CancelPending(target);
        if (manager.current == target) HideTooltip();
    }
    if (isTarget && message == WM_NCDESTROY) {
        DetachInternal(target);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK TargetProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                            UINT_PTR, DWORD_PTR reference) {
    try {
        return TargetProcImpl(window, message, wParam, lParam, reference);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    if (message == WM_NCDESTROY && !reference) DetachInternal(window);
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

bool AttachTooltip(HWND target, const TooltipOptions& options) {
    if (!IsWindow(target) || GetWindowThreadProcessId(target, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (!internal::Instance() || options.text.empty() || !std::isfinite(options.maxWidthDip) ||
        options.maxWidthDip <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (const auto found = manager.associations.find(target);
        found != manager.associations.end()) {
        try {
            TooltipOptions replacement = options;
            TooltipOptions visibleReplacement;
            const bool visible = manager.current == target;
            if (visible) visibleReplacement = options;
            const bool pending = manager.pending == target;
            found->second->options = std::move(replacement);
            if (pending) {
                CancelPending(target);
                ScheduleShow(target, *found->second);
            }
            if (visible) {
                manager.visibleOptions = std::move(visibleReplacement);
                PositionPopup(target, manager.visibleOptions);
                RestartAutopopTimer();
            }
            return true;
        } catch (const std::bad_alloc&) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
    }

    try {
        auto association = std::make_unique<Association>();
        association->options = options;
        for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor))
            association->ancestors.push_back(ancestor);
        if (!EnsurePopup() || !EnsureInputHook()) {
            const auto error = GetLastError();
            DestroyPopupIfUnused();
            SetLastError(error);
            return false;
        }
        if (!SetWindowSubclass(target, TargetProc, TooltipSubclassId, 0)) {
            const auto error = GetLastError();
            DestroyPopupIfUnused();
            SetLastError(error);
            return false;
        }
        size_t installed{};
        for (; installed < association->ancestors.size(); ++installed)
            if (!SetWindowSubclass(association->ancestors[installed], TargetProc,
                                   reinterpret_cast<UINT_PTR>(target),
                                   reinterpret_cast<DWORD_PTR>(target)))
                break;
        if (installed != association->ancestors.size()) {
            const auto error = GetLastError();
            RemoveWindowSubclass(target, TargetProc, TooltipSubclassId);
            while (installed)
                RemoveWindowSubclass(association->ancestors[--installed], TargetProc,
                                     reinterpret_cast<UINT_PTR>(target));
            DestroyPopupIfUnused();
            SetLastError(error);
            return false;
        }
        manager.associations.emplace(target, std::move(association));
        return true;
    } catch (const std::bad_alloc&) {
        RemoveWindowSubclass(target, TargetProc, TooltipSubclassId);
        for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor))
            RemoveWindowSubclass(ancestor, TargetProc, reinterpret_cast<UINT_PTR>(target));
        manager.associations.erase(target);
        DestroyPopupIfUnused();
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

bool DetachTooltip(HWND target) {
    if (!IsWindow(target) || GetWindowThreadProcessId(target, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (DetachInternal(target)) return true;
    SetLastError(ERROR_NOT_FOUND);
    return false;
}

void HideAllTooltips() {
    CancelPending();
    HideTooltip();
}

namespace internal {
bool RegisterTooltipClass() { return RegisterControlClass(TooltipClass, TooltipProc); }
} // namespace internal
} // namespace wcw
