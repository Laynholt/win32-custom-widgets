#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
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
    UINT_PTR showTimer{};
    std::vector<HWND> ancestors;
};

struct TooltipManager {
    std::unordered_map<HWND, std::unique_ptr<Association>> associations;
    HWND popup{};
    HWND current{};
    TooltipOptions visibleOptions;
    UINT_PTR autopopTimer{};
    ULONGLONG lastHideTick{};
};

thread_local TooltipManager manager;

LRESULT CALLBACK TooltipProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK TargetProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

bool UsableTarget(HWND target) {
    if (!IsWindow(target) || !IsWindowVisible(target) || !IsWindowEnabled(target)) return false;
    for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor))
        if (!IsWindowVisible(ancestor) || !IsWindowEnabled(ancestor)) return false;
    return true;
}

void CancelShow(HWND target, Association& association) {
    if (!association.showTimer) return;
    KillTimer(target, association.showTimer);
    association.showTimer = 0;
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
    if (!manager.associations.empty() || !manager.popup) return;
    HideTooltip();
    const auto popup = std::exchange(manager.popup, nullptr);
    if (IsWindow(popup)) DestroyWindow(popup);
}

bool EnsurePopup() {
    if (manager.popup && IsWindow(manager.popup)) return true;
    manager.popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                    TooltipClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                    internal::Instance(), nullptr);
    return manager.popup != nullptr;
}

SIZE MeasureTooltip(HWND target, const TooltipOptions& options) {
    const auto dpi = paint::Dpi(target);
    const auto theme = GetTheme();
    const auto style = ResolveStyle(theme, options.appearance);
    const int maximum = std::max(1, DipToPx(options.maxWidthDip, dpi));
    RECT measured{0, 0, maximum, 0};
    const auto dc = GetDC(target);
    if (dc) {
        const auto oldFont = SelectObject(dc, paint::Font(style.font, dpi));
        DrawTextW(dc, options.text.c_str(), static_cast<int>(options.text.size()), &measured,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        if (oldFont) SelectObject(dc, oldFont);
        ReleaseDC(target, dc);
    }
    const int paddingX = std::max(0, DipToPx(style.paddingXDip, dpi));
    const int paddingY = std::max(0, DipToPx(style.paddingYDip, dpi));
    return {std::min(maximum, std::max(1, static_cast<int>(measured.right))) + paddingX * 2,
            std::max(1, static_cast<int>(measured.bottom)) + paddingY * 2};
}

void PositionPopup(HWND target, const TooltipOptions& options) {
    if (!manager.popup || !IsWindow(manager.popup) || !IsWindow(target)) return;
    const auto size = MeasureTooltip(target, options);
    POINT cursor{};
    GetCursorPos(&cursor);
    const auto dpi = paint::Dpi(target);
    int x = cursor.x + DipToPx(12, dpi);
    int y = cursor.y + DipToPx(20, dpi);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const int left = static_cast<int>(monitor.rcWork.left);
        const int top = static_cast<int>(monitor.rcWork.top);
        const int right = static_cast<int>(monitor.rcWork.right);
        const int bottom = static_cast<int>(monitor.rcWork.bottom);
        x = std::clamp(x, left, std::max(left, right - static_cast<int>(size.cx)));
        y = std::clamp(y, top, std::max(top, bottom - static_cast<int>(size.cy)));
    }
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

void ShowTooltip(HWND target) {
    const auto found = manager.associations.find(target);
    if (found == manager.associations.end()) return;
    auto& association = *found->second;
    CancelShow(target, association);
    if (!UsableTarget(target) || association.options.text.empty() || !EnsurePopup()) return;
    if (manager.current && manager.current != target) HideTooltip();
    manager.current = target;
    manager.visibleOptions = association.options;
    PositionPopup(target, manager.visibleOptions);
    if (manager.autopopTimer) KillTimer(manager.popup, manager.autopopTimer);
    if (manager.visibleOptions.autopopDelayMs)
        manager.autopopTimer = SetTimer(manager.popup, AutopopTimerId,
                                        manager.visibleOptions.autopopDelayMs, nullptr);
}

void ScheduleShow(HWND target, Association& association) {
    if (!UsableTarget(target) || association.showTimer ||
        (manager.current == target && manager.popup && IsWindowVisible(manager.popup))) return;
    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, target, 0};
    TrackMouseEvent(&tracking);
    const auto now = GetTickCount64();
    const bool reshow = manager.lastHideTick && now - manager.lastHideTick <= ReshowWindowMs;
    const UINT delay = reshow ? association.options.reshowDelayMs
                              : association.options.initialDelayMs;
    if (!delay)
        ShowTooltip(target);
    else
        association.showTimer = SetTimer(target, ShowTimerId, delay, nullptr);
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
    CancelShow(target, association);
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
        const auto dpi = manager.current && IsWindow(manager.current)
                             ? paint::Dpi(manager.current)
                             : paint::Dpi(window);
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
    if (internal::HandleControlMessage(window, message, shared)) return shared;
    switch (message) {
    case WM_TIMER:
        if (manager.autopopTimer == wParam) HideTooltip();
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
        case WM_TIMER:
            if (association.showTimer == wParam) ShowTooltip(target);
            break;
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
            CancelShow(target, association);
            if (manager.current == target) HideTooltip();
            break;
        default: break;
        }
    }
    if (((message == WM_SHOWWINDOW || message == WM_ENABLE) && !wParam) ||
        message == WM_WINDOWPOSCHANGING || message == WM_DESTROY) {
        CancelShow(target, association);
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
    try {
        if (const auto found = manager.associations.find(target);
            found != manager.associations.end()) {
            found->second->options = options;
            CancelShow(target, *found->second);
            if (manager.current == target) {
                manager.visibleOptions = options;
                PositionPopup(target, options);
            }
            return true;
        }
        auto association = std::make_unique<Association>();
        association->options = options;
        if (!SetWindowSubclass(target, TargetProc, TooltipSubclassId, 0)) return false;
        for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor)) {
            if (SetWindowSubclass(ancestor, TargetProc, reinterpret_cast<UINT_PTR>(target),
                                  reinterpret_cast<DWORD_PTR>(target)))
                association->ancestors.push_back(ancestor);
        }
        manager.associations.emplace(target, std::move(association));
        return true;
    } catch (const std::bad_alloc&) {
        RemoveWindowSubclass(target, TargetProc, TooltipSubclassId);
        for (auto ancestor = GetParent(target); ancestor; ancestor = GetParent(ancestor))
            RemoveWindowSubclass(ancestor, TargetProc, reinterpret_cast<UINT_PTR>(target));
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
    for (auto& [target, association] : manager.associations)
        CancelShow(target, *association);
    HideTooltip();
}

namespace internal {
bool RegisterTooltipClass() { return RegisterControlClass(TooltipClass, TooltipProc); }
} // namespace internal
} // namespace wcw
