#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Paint.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <new>

namespace wcw {
namespace {

constexpr wchar_t ProgressBarClass[] = L"WcwProgressBar";
constexpr UINT_PTR AnimationTimer = 1;
constexpr UINT AnimationIntervalMs = 16;
constexpr UINT ProgressUpdateTimerMessage = WM_APP + 0x57E;

struct ProgressState {
    double minimum{};
    double maximum{};
    double value{};
    bool indeterminate{};
    float phase{};
    bool timerRunning{};
};

bool IsProgressWindow(HWND window) {
    wchar_t name[32]{};
    if (!IsWindow(window) || !GetClassNameW(window, name, 32) ||
        wcscmp(name, ProgressBarClass) != 0) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

void UpdateTimer(HWND window, ProgressState& state) {
    const bool shouldRun = state.indeterminate && IsWindowEnabled(window) && IsWindowVisible(window);
    if (shouldRun && !state.timerRunning) {
        state.timerRunning = SetTimer(window, AnimationTimer, AnimationIntervalMs, nullptr) != 0;
    } else if (!shouldRun && state.timerRunning) {
        KillTimer(window, AnimationTimer);
        state.timerRunning = false;
    }
}

LRESULT CALLBACK ParentVisibilityProc(HWND parent, UINT message, WPARAM wParam, LPARAM lParam,
                                      UINT_PTR subclassId, DWORD_PTR reference) {
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(parent, ParentVisibilityProc, subclassId);
        return DefSubclassProc(parent, message, wParam, lParam);
    }
    const auto result = DefSubclassProc(parent, message, wParam, lParam);
    const auto progress = reinterpret_cast<HWND>(reference);
    if ((message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGED) && IsWindow(progress))
        SendMessageW(progress, ProgressUpdateTimerMessage, 0, 0);
    return result;
}

void PaintProgress(HWND window, const ProgressState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
        const auto enabled = IsWindowEnabled(window) != FALSE;
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::RectF track{0, 0, static_cast<float>(bounds.right),
                                   static_cast<float>(bounds.bottom)};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        paint::Fill(graphics, track, radius, enabled ? style.background : style.disabledSurface);
        paint::Border(graphics, track, radius, style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));

        Gdiplus::RectF fill = track;
        if (state.indeterminate) {
            fill.Width = track.Width * .25f;
            fill.X = (state.phase * 1.25f - .25f) * track.Width;
            const auto left = std::max(track.X, fill.X);
            const auto right = std::min(track.GetRight(), fill.GetRight());
            fill.X = left;
            fill.Width = std::max(0.0f, right - left);
        } else {
            const auto fraction = (state.value - state.minimum) / (state.maximum - state.minimum);
            fill.Width = track.Width * static_cast<float>(fraction);
        }
        if (fill.Width > 0)
            paint::Fill(graphics, fill, std::min(radius, fill.Width / 2.0f),
                        enabled ? style.accent : style.disabledText);
    }
    EndPaint(window, &ps);
}

LRESULT ProgressProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<ProgressState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto options = static_cast<const ProgressBarOptions*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<ProgressState>(ProgressState{
            options->minimum, options->maximum,
            std::clamp(options->value, options->minimum, options->maximum),
            options->indeterminate});
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        if (const auto parent = GetParent(window); parent)
            RemoveWindowSubclass(parent, ParentVisibilityProc,
                                 reinterpret_cast<UINT_PTR>(window));
        if (state && state->timerRunning) KillTimer(window, AnimationTimer);
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_SHOWWINDOW: {
        const auto result = DefWindowProcW(window, message, wParam, lParam);
        if (state) UpdateTimer(window, *state);
        return result;
    }
    case WM_ENABLE:
        if (state) UpdateTimer(window, *state);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (state && wParam == AnimationTimer && state->timerRunning) {
            state->phase = std::fmod(state->phase + .025f, 1.0f);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case ProgressUpdateTimerMessage:
        if (state) UpdateTimer(window, *state);
        return 0;
    case internal::ProgressSetValueMessage:
        if (state && lParam) {
            state->value = std::clamp(*reinterpret_cast<double*>(lParam),
                                      state->minimum, state->maximum);
            InvalidateRect(window, nullptr, FALSE);
            return TRUE;
        }
        return FALSE;
    case internal::ProgressGetValueMessage:
        if (state && lParam) {
            *reinterpret_cast<std::optional<double>*>(lParam) = state->value;
            return TRUE;
        }
        return FALSE;
    case internal::ProgressSetIndeterminateMessage:
        if (state) {
            state->indeterminate = wParam != 0;
            state->phase = 0;
            UpdateTimer(window, *state);
            InvalidateRect(window, nullptr, FALSE);
            return TRUE;
        }
        return FALSE;
    case WM_PAINT:
        if (state) PaintProgress(window, *state);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK ProgressProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return ProgressProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

HWND CreateProgressBar(const ProgressBarOptions& options) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (!std::isfinite(options.minimum) || !std::isfinite(options.maximum) ||
        !std::isfinite(options.value) || options.minimum >= options.maximum) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(
        0, ProgressBarClass, L"", WS_CHILD | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(),
        const_cast<ProgressBarOptions*>(&options));
    if (window && !SetWindowSubclass(options.parent, ParentVisibilityProc,
                                     reinterpret_cast<UINT_PTR>(window),
                                     reinterpret_cast<DWORD_PTR>(window))) {
        DestroyWindow(window);
        return nullptr;
    }
    if (window) {
        SetStyleOverride(window, options.appearance);
        SendMessageW(window, ProgressUpdateTimerMessage, 0, 0);
    }
    return window;
}

bool SetProgressValue(HWND progressBar, double value) {
    if (!IsProgressWindow(progressBar)) return false;
    if (!std::isfinite(value)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return SendMessageW(progressBar, internal::ProgressSetValueMessage, 0,
                        reinterpret_cast<LPARAM>(&value)) != FALSE;
}

std::optional<double> GetProgressValue(HWND progressBar) {
    std::optional<double> value;
    if (IsProgressWindow(progressBar))
        SendMessageW(progressBar, internal::ProgressGetValueMessage, 0,
                     reinterpret_cast<LPARAM>(&value));
    return value;
}

bool SetProgressIndeterminate(HWND progressBar, bool indeterminate) {
    return IsProgressWindow(progressBar) &&
           SendMessageW(progressBar, internal::ProgressSetIndeterminateMessage,
                        indeterminate, 0) != FALSE;
}

namespace internal {
bool RegisterProgressBarClass() { return RegisterControlClass(ProgressBarClass, ProgressProc); }
} // namespace internal
} // namespace wcw
