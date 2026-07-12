#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"
#include "ScrollModel.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <memory>
#include <new>
#include <optional>
#include <vector>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t ScrollViewClass[] = L"WcwScrollView";
constexpr wchar_t ScrollViewportClass[] = L"WcwScrollViewport";
constexpr wchar_t ScrollContentClass[] = L"WcwScrollContent";

enum class DragAxis { None, Horizontal, Vertical };

struct ScrollState {
    internal::ScrollModel model;
    ScrollExtentDip contentExtentDip{};
    StyleOverride trackAppearance;
    StyleOverride thumbAppearance;
    HWND viewport{};
    HWND content{};
    DragAxis dragAxis{DragAxis::None};
    float dragGrabPx{};
    unsigned dpi{USER_DEFAULT_SCREEN_DPI};
    std::vector<HWND> observedAncestors;
};

struct LayoutInfo {
    int width{};
    int height{};
    int viewportWidth{};
    int viewportHeight{};
    int barSize{};
    int margin{};
    int minimumThumb{};
    bool horizontal{};
    bool vertical{};
};

LRESULT CALLBACK AncestorProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

StyleOverride Overlay(StyleOverride result, const StyleOverride& local) {
#define WCW_OVERLAY(member) if (local.member) result.member = local.member
    WCW_OVERLAY(background); WCW_OVERLAY(foreground); WCW_OVERLAY(mutedForeground);
    WCW_OVERLAY(border); WCW_OVERLAY(hover); WCW_OVERLAY(pressed); WCW_OVERLAY(selected);
    WCW_OVERLAY(disabledSurface); WCW_OVERLAY(disabledText); WCW_OVERLAY(focus);
    WCW_OVERLAY(accent); WCW_OVERLAY(danger); WCW_OVERLAY(font); WCW_OVERLAY(borderWidthDip);
    WCW_OVERLAY(focusWidthDip); WCW_OVERLAY(paddingXDip); WCW_OVERLAY(paddingYDip);
    WCW_OVERLAY(spacingDip); WCW_OVERLAY(controlHeightDip); WCW_OVERLAY(cornerRadiusDip);
    WCW_OVERLAY(trackThicknessDip); WCW_OVERLAY(thumbSizeDip); WCW_OVERLAY(indicatorSizeDip);
#undef WCW_OVERLAY
    return result;
}

bool FiniteNonNegative(ScrollExtentDip extent) {
    return std::isfinite(extent.width) && std::isfinite(extent.height) && extent.width >= 0 &&
           extent.height >= 0;
}

bool Finite(ScrollOffsetDip offset) { return std::isfinite(offset.x) && std::isfinite(offset.y); }

bool IsScrollViewWindow(HWND window) {
    wchar_t name[32]{};
    if (!IsWindow(window) || GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId() ||
        !GetClassNameW(window, name, static_cast<int>(std::size(name))) ||
        wcscmp(name, ScrollViewClass) != 0) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

LayoutInfo CalculateLayout(HWND window, const ScrollState& state) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto dpi = paint::Dpi(window);
    const auto base = internal::WindowStyleOverride(window);
    const auto thumb = ResolveStyle(GetTheme(), Overlay(base, state.thumbAppearance));
    LayoutInfo result;
    result.width = std::max(0L, bounds.right);
    result.height = std::max(0L, bounds.bottom);
    result.barSize = std::max(1, DipToPx(thumb.thumbSizeDip, dpi));
    result.margin = std::max(1, DipToPx(2, dpi));
    result.minimumThumb = std::max(result.barSize, DipToPx(28, dpi));

    const int contentWidth = std::max(0, DipToPx(state.contentExtentDip.width, dpi));
    const int contentHeight = std::max(0, DipToPx(state.contentExtentDip.height, dpi));
    result.vertical = contentHeight > result.height;
    result.horizontal = contentWidth > result.width - (result.vertical ? result.barSize : 0);
    if (result.horizontal && !result.vertical)
        result.vertical = contentHeight > result.height - result.barSize;
    if (result.vertical && !result.horizontal)
        result.horizontal = contentWidth > result.width - result.barSize;
    result.viewportWidth = std::max(0, result.width - (result.vertical ? result.barSize : 0));
    result.viewportHeight = std::max(0, result.height - (result.horizontal ? result.barSize : 0));
    return result;
}

void ApplyLayout(HWND window, ScrollState& state) {
    const auto layout = CalculateLayout(window, state);
    const auto dpi = paint::Dpi(window);
    auto preservedOffset = state.model.Offset();
    if (state.dpi != dpi) {
        preservedOffset.x *= static_cast<float>(dpi) / state.dpi;
        preservedOffset.y *= static_cast<float>(dpi) / state.dpi;
        state.dpi = dpi;
    }
    state.model.SetExtent({static_cast<float>(std::max(0, DipToPx(state.contentExtentDip.width, dpi))),
                           static_cast<float>(std::max(0, DipToPx(state.contentExtentDip.height, dpi)))});
    state.model.SetViewport({static_cast<float>(layout.viewportWidth),
                             static_cast<float>(layout.viewportHeight)});
    state.model.SetOffset(preservedOffset);
    if (IsWindow(state.viewport))
        SetWindowPos(state.viewport, nullptr, 0, 0, layout.viewportWidth, layout.viewportHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    if (IsWindow(state.content)) {
        const auto offset = state.model.Offset();
        const auto extent = state.model.Extent();
        SetWindowPos(state.content, nullptr, -static_cast<int>(std::lround(offset.x)),
                     -static_cast<int>(std::lround(offset.y)),
                     std::max(layout.viewportWidth, static_cast<int>(std::lround(extent.width))),
                     std::max(layout.viewportHeight, static_cast<int>(std::lround(extent.height))),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(window, nullptr, FALSE);
}

std::optional<ScrollbarGeometry> VerticalGeometry(const LayoutInfo& layout,
                                                  const ScrollState& state) {
    if (!layout.vertical) return std::nullopt;
    return ScrollbarGeometry(static_cast<float>(layout.margin),
                             static_cast<float>(std::max(layout.margin,
                                 layout.viewportHeight - layout.margin)),
                             state.model.Extent().height, state.model.Viewport().height,
                             state.model.Offset().y, static_cast<float>(layout.minimumThumb));
}

std::optional<ScrollbarGeometry> HorizontalGeometry(const LayoutInfo& layout,
                                                    const ScrollState& state) {
    if (!layout.horizontal) return std::nullopt;
    return ScrollbarGeometry(static_cast<float>(layout.margin),
                             static_cast<float>(std::max(layout.margin,
                                 layout.viewportWidth - layout.margin)),
                             state.model.Extent().width, state.model.Viewport().width,
                             state.model.Offset().x, static_cast<float>(layout.minimumThumb));
}

void PaintScrollView(HWND window, const ScrollState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (paint::Buffer buffer(target, bounds); buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto base = internal::WindowStyleOverride(window);
        const auto style = ResolveStyle(theme, base);
        const auto track = ResolveStyle(theme, Overlay(base, state.trackAppearance));
        const auto thumb = ResolveStyle(theme, Overlay(base, state.thumbAppearance));
        const auto layout = CalculateLayout(window, state);
        const auto enabled = IsWindowEnabled(window) != FALSE;
        paint::Clear(buffer.dc(), bounds, base.background.value_or(theme.palette.panel));
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto trackColor = enabled ? track.background : track.disabledSurface;
        const auto thumbColor = enabled ? thumb.accent : thumb.disabledText;
        const float trackRadius = paint::ToPixels(track.cornerRadiusDip, dpi);
        const float thumbRadius = paint::ToPixels(thumb.cornerRadiusDip, dpi);
        const float thickness = std::min(static_cast<float>(layout.barSize),
                                         paint::ToPixels(track.trackThicknessDip, dpi));

        if (const auto geometry = VerticalGeometry(layout, state)) {
            const float x = layout.viewportWidth + (layout.barSize - thickness) / 2.0f;
            paint::Fill(graphics, {x, 0, thickness, static_cast<float>(layout.viewportHeight)},
                        trackRadius, trackColor);
            const float thumbWidth = std::max(thickness, layout.barSize * 0.55f);
            paint::Fill(graphics,
                        {layout.viewportWidth + (layout.barSize - thumbWidth) / 2.0f,
                         geometry->thumbStartPx, thumbWidth, geometry->thumbSizePx},
                        thumbRadius, thumbColor);
        }
        if (const auto geometry = HorizontalGeometry(layout, state)) {
            const float y = layout.viewportHeight + (layout.barSize - thickness) / 2.0f;
            paint::Fill(graphics, {0, y, static_cast<float>(layout.viewportWidth), thickness},
                        trackRadius, trackColor);
            const float thumbHeight = std::max(thickness, layout.barSize * 0.55f);
            paint::Fill(graphics,
                        {geometry->thumbStartPx,
                         layout.viewportHeight + (layout.barSize - thumbHeight) / 2.0f,
                         geometry->thumbSizePx, thumbHeight}, thumbRadius, thumbColor);
        }
        if (GetFocus() == window) {
            const Gdiplus::RectF focus{0, 0, static_cast<float>(bounds.right),
                                      static_cast<float>(bounds.bottom)};
            paint::Focus(graphics, focus, paint::ToPixels(style.cornerRadiusDip, dpi), style.focus,
                         paint::ToPixels(style.focusWidthDip, dpi));
        }
    }
    EndPaint(window, &ps);
}

void PaintContent(HWND window) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    const auto theme = GetTheme();
    const auto local = internal::WindowStyleOverride(window);
    paint::Clear(target, ps.rcPaint, local.background.value_or(theme.palette.panel));
    EndPaint(window, &ps);
}

bool SetOffsetPixels(HWND window, ScrollState& state, ScrollOffsetDip offset) {
    if (!state.model.SetOffset(offset)) return false;
    ApplyLayout(window, state);
    internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
    return true;
}

void DragTo(HWND window, ScrollState& state, int x, int y) {
    const auto layout = CalculateLayout(window, state);
    if (state.dragAxis == DragAxis::Vertical) {
        const auto geometry = VerticalGeometry(layout, state);
        if (!geometry) return;
        const float trackStart = static_cast<float>(layout.margin);
        const float travel = layout.viewportHeight - layout.margin * 2 - geometry->thumbSizePx;
        const float fraction = travel <= 0 ? 0 :
            std::clamp((y - trackStart - state.dragGrabPx) / travel, 0.0f, 1.0f);
        SetOffsetPixels(window, state, {state.model.Offset().x,
                                       fraction * state.model.Maximum().y});
    } else if (state.dragAxis == DragAxis::Horizontal) {
        const auto geometry = HorizontalGeometry(layout, state);
        if (!geometry) return;
        const float trackStart = static_cast<float>(layout.margin);
        const float travel = layout.viewportWidth - layout.margin * 2 - geometry->thumbSizePx;
        const float fraction = travel <= 0 ? 0 :
            std::clamp((x - trackStart - state.dragGrabPx) / travel, 0.0f, 1.0f);
        SetOffsetPixels(window, state, {fraction * state.model.Maximum().x,
                                       state.model.Offset().y});
    }
}

void BeginDrag(HWND window, ScrollState& state, int x, int y) {
    const auto layout = CalculateLayout(window, state);
    if (layout.vertical && x >= layout.viewportWidth && y < layout.viewportHeight) {
        const auto geometry = *VerticalGeometry(layout, state);
        state.dragAxis = DragAxis::Vertical;
        state.dragGrabPx = y >= geometry.thumbStartPx &&
                                   y <= geometry.thumbStartPx + geometry.thumbSizePx
                               ? y - geometry.thumbStartPx
                               : geometry.thumbSizePx / 2.0f;
    } else if (layout.horizontal && y >= layout.viewportHeight && x < layout.viewportWidth) {
        const auto geometry = *HorizontalGeometry(layout, state);
        state.dragAxis = DragAxis::Horizontal;
        state.dragGrabPx = x >= geometry.thumbStartPx &&
                                   x <= geometry.thumbStartPx + geometry.thumbSizePx
                               ? x - geometry.thumbStartPx
                               : geometry.thumbSizePx / 2.0f;
    }
    if (state.dragAxis != DragAxis::None) {
        SetFocus(window);
        SetCapture(window);
        DragTo(window, state, x, y);
    }
}

void CancelDrag(HWND window, ScrollState& state) {
    state.dragAxis = DragAxis::None;
    if (GetCapture() == window) ReleaseCapture();
}

void RemoveAncestorObservers(HWND scrollView, ScrollState& state) {
    const auto id = reinterpret_cast<UINT_PTR>(scrollView);
    for (const auto ancestor : state.observedAncestors)
        if (IsWindow(ancestor)) RemoveWindowSubclass(ancestor, AncestorProc, id);
    state.observedAncestors.clear();
}

void ObserveAncestors(HWND scrollView, ScrollState& state) {
    const auto id = reinterpret_cast<UINT_PTR>(scrollView);
    for (auto ancestor = GetParent(scrollView); ancestor; ancestor = GetParent(ancestor))
        if (SetWindowSubclass(ancestor, AncestorProc, id,
                              reinterpret_cast<DWORD_PTR>(scrollView)))
            state.observedAncestors.push_back(ancestor);
}

LRESULT CALLBACK AncestorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR reference) {
    const auto scrollView = reinterpret_cast<HWND>(reference);
    if ((((message == WM_SHOWWINDOW || message == WM_ENABLE) && !wParam) ||
         message == WM_DESTROY || message == WM_NCDESTROY) && IsWindow(scrollView))
        SendMessageW(scrollView, WM_CANCELMODE, 0, 0);
    return DefSubclassProc(window, message, wParam, lParam);
}

void ScrollWheel(HWND window, ScrollState& state, int delta, bool horizontal,
                 bool nativeHorizontal = false) {
    const int normalizedDelta = horizontal && !nativeHorizontal ? -delta : delta;
    const int notches = state.model.ConsumeWheelDelta(normalizedDelta, horizontal);
    if (!notches) return;
    UINT units{3};
    SystemParametersInfoW(horizontal ? SPI_GETWHEELSCROLLCHARS
                                     : SPI_GETWHEELSCROLLLINES,
                          0, &units, 0);
    const float page = horizontal ? state.model.Viewport().width : state.model.Viewport().height;
    const float amount = units == WHEEL_PAGESCROLL
                             ? page
                             : DipToPx(16.0f * static_cast<float>(units), paint::Dpi(window));
    auto offset = state.model.Offset();
    if (horizontal)
        offset.x += notches * amount;
    else
        offset.y -= notches * amount;
    SetOffsetPixels(window, state, offset);
}

LRESULT ScrollProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<ScrollState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto options = static_cast<const ScrollViewOptions*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<ScrollState>();
        created->contentExtentDip = options->contentExtent;
        created->trackAppearance = options->trackAppearance;
        created->thumbAppearance = options->thumbAppearance;
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == internal::ThemeChangedMessage && state) {
        ApplyLayout(window, *state);
        const auto layout = CalculateLayout(window, *state);
        if ((state->dragAxis == DragAxis::Vertical && !layout.vertical) ||
            (state->dragAxis == DragAxis::Horizontal && !layout.horizontal))
            CancelDrag(window, *state);
    }
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_CREATE:
        state->viewport = CreateWindowExW(WS_EX_CONTROLPARENT, ScrollViewportClass, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, window, nullptr,
            internal::Instance(), nullptr);
        if (!state->viewport) return -1;
        state->content = CreateWindowExW(WS_EX_CONTROLPARENT, ScrollContentClass, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, state->viewport, nullptr,
            internal::Instance(), nullptr);
        if (!state->content) return -1;
        ObserveAncestors(window, *state);
        ApplyLayout(window, *state);
        return 0;
    case WM_NCDESTROY:
        if (state) {
            if (state->dragAxis != DragAxis::None) CancelDrag(window, *state);
            RemoveAncestorObservers(window, *state);
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SIZE:
    case WM_DPICHANGED_AFTERPARENT:
        if (state) {
            ApplyLayout(window, *state);
            const auto layout = CalculateLayout(window, *state);
            if ((state->dragAxis == DragAxis::Vertical && !layout.vertical) ||
                (state->dragAxis == DragAxis::Horizontal && !layout.horizontal))
                CancelDrag(window, *state);
        }
        return 0;
    case WM_SHOWWINDOW:
        if (state && !wParam) CancelDrag(window, *state);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ENABLE:
        if (state && !wParam) CancelDrag(window, *state);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_CANCELMODE:
        if (state) CancelDrag(window, *state);
        return 0;
    case WM_CAPTURECHANGED:
        if (state) state->dragAxis = DragAxis::None;
        return 0;
    case WM_LBUTTONDOWN:
        if (state && IsWindowEnabled(window))
            BeginDrag(window, *state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (state && state->dragAxis != DragAxis::None && GetCapture() == window)
            DragTo(window, *state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (state && state->dragAxis != DragAxis::None) {
            if (GetCapture() == window)
                DragTo(window, *state, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            CancelDrag(window, *state);
        }
        return 0;
    case WM_KEYDOWN:
        if (!state || !IsWindowEnabled(window)) return 0;
        {
            auto offset = state->model.Offset();
            const auto line = static_cast<float>(DipToPx(16, paint::Dpi(window)));
            switch (wParam) {
            case VK_LEFT: offset.x -= line; break;
            case VK_RIGHT: offset.x += line; break;
            case VK_UP: offset.y -= line; break;
            case VK_DOWN: offset.y += line; break;
            case VK_PRIOR: offset.y -= state->model.Viewport().height; break;
            case VK_NEXT: offset.y += state->model.Viewport().height; break;
            case VK_HOME: offset.y = 0; break;
            case VK_END: offset.y = state->model.Maximum().y; break;
            default: return 0;
            }
            SetOffsetPixels(window, *state, offset);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state && IsWindowEnabled(window))
            ScrollWheel(window, *state, GET_WHEEL_DELTA_WPARAM(wParam),
                        (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0);
        return 0;
    case WM_MOUSEHWHEEL:
        if (state && IsWindowEnabled(window))
            ScrollWheel(window, *state, GET_WHEEL_DELTA_WPARAM(wParam), true, true);
        return 0;
    case internal::ScrollSetExtentMessage:
        if (!state || !lParam) return FALSE;
        state->contentExtentDip = *reinterpret_cast<const ScrollExtentDip*>(lParam);
        ApplyLayout(window, *state);
        return TRUE;
    case internal::ScrollSetOffsetMessage:
        if (!state || !lParam) return FALSE;
        {
            const auto dpi = paint::Dpi(window);
            const auto dip = *reinterpret_cast<const ScrollOffsetDip*>(lParam);
            SetOffsetPixels(window, *state,
                            {paint::ToPixels(dip.x, dpi), paint::ToPixels(dip.y, dpi)});
        }
        return TRUE;
    case internal::ScrollGetOffsetMessage:
        if (!state || !lParam) return FALSE;
        {
            const auto dpi = paint::Dpi(window);
            const auto offset = state->model.Offset();
            *reinterpret_cast<std::optional<ScrollOffsetDip>*>(lParam) =
                ScrollOffsetDip{offset.x * 96.0f / dpi, offset.y * 96.0f / dpi};
        }
        return TRUE;
    case internal::ScrollGetContentMessage:
        return state && IsWindow(state->content) ? reinterpret_cast<LRESULT>(state->content) : 0;
    case WM_PAINT:
        if (state) PaintScrollView(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK ScrollProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return ScrollProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK ViewportProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK ContentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) internal::RegisterWindow(window);
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_PAINT: PaintContent(window); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

HWND CreateScrollView(const ScrollViewOptions& options) {
    if (!options.parent || !IsWindow(options.parent) ||
        GetWindowThreadProcessId(options.parent, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (!FiniteNonNegative(options.contentExtent)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(
        WS_EX_CONTROLPARENT, ScrollViewClass, L"",
        WS_CHILD | WS_TABSTOP | WS_CLIPCHILDREN | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(),
        const_cast<ScrollViewOptions*>(&options));
    if (window) {
        SetStyleOverride(window, options.appearance);
        if (const auto content = GetScrollContentWindow(window))
            SetStyleOverride(content, options.appearance);
    }
    internal::RegisterAccessibility(window, internal::AccessibleKind::ScrollView, options);
    return window;
}

bool SetScrollContentExtent(HWND scrollView, ScrollExtentDip extent) {
    if (!IsScrollViewWindow(scrollView)) return false;
    if (!FiniteNonNegative(extent)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return SendMessageW(scrollView, internal::ScrollSetExtentMessage, 0,
                        reinterpret_cast<LPARAM>(&extent)) != FALSE;
}

bool SetScrollOffset(HWND scrollView, ScrollOffsetDip offset) {
    if (!IsScrollViewWindow(scrollView)) return false;
    if (!Finite(offset)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return SendMessageW(scrollView, internal::ScrollSetOffsetMessage, 0,
                        reinterpret_cast<LPARAM>(&offset)) != FALSE;
}

std::optional<ScrollOffsetDip> GetScrollOffset(HWND scrollView) {
    std::optional<ScrollOffsetDip> offset;
    if (IsScrollViewWindow(scrollView))
        SendMessageW(scrollView, internal::ScrollGetOffsetMessage, 0,
                     reinterpret_cast<LPARAM>(&offset));
    return offset;
}

HWND GetScrollContentWindow(HWND scrollView) {
    if (!IsScrollViewWindow(scrollView)) return nullptr;
    return reinterpret_cast<HWND>(SendMessageW(scrollView, internal::ScrollGetContentMessage, 0, 0));
}

namespace internal {
bool RegisterScrollViewClasses() {
    return RegisterControlClass(ScrollViewClass, ScrollProc) &&
           RegisterControlClass(ScrollViewportClass, ViewportProc) &&
           RegisterControlClass(ScrollContentClass, ContentProc);
}
} // namespace internal
} // namespace wcw
