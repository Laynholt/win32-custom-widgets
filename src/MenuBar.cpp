#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "MenuModel.h"
#include "Paint.h"

#include <algorithm>
#include <memory>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t MenuBarClass[] = L"WcwMenuBar";

struct MenuBarState {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
    std::vector<RECT> itemBounds;
    int hot{-1};
    int active{-1};
    bool pressed{};
    bool popupOpen{};
};

int TextWidth(HDC dc, HFONT font, const std::wstring& text) {
    if (text.empty()) return 0;
    const auto previous = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
    SelectObject(dc, previous);
    return size.cx;
}

internal::MenuColors ResolveBarColors(HWND window, const MenuBarState& state) {
    return internal::ResolveMenuColors(
        internal::OverlayStyle(state.menuAppearance, internal::WindowStyleOverride(window)));
}

void Layout(HWND window, MenuBarState& state) {
    RECT client{};
    if (!GetClientRect(window, &client)) return;
    const auto colors = ResolveBarColors(window, state);
    const auto dpi = paint::Dpi(window);
    const int padding = (std::max)(0, DipToPx(colors.style.paddingXDip, dpi));
    const auto dc = GetDC(window);
    const auto font = paint::Font(colors.style.font, dpi);
    state.itemBounds.clear();
    state.itemBounds.reserve(state.items.size());
    int left{};
    for (const auto& item : state.items) {
        const auto label = internal::ParseMenuLabel(item.text);
        const int width = (std::max)(0, TextWidth(dc, font, label.text)) + padding * 2;
        state.itemBounds.push_back({left, client.top, left + width, client.bottom});
        left += width;
    }
    if (dc) ReleaseDC(window, dc);
    if (state.hot >= static_cast<int>(state.itemBounds.size())) state.hot = -1;
    if (state.active >= static_cast<int>(state.itemBounds.size())) state.active = -1;
    InvalidateRect(window, nullptr, FALSE);
}

int HitTest(const MenuBarState& state, POINT point) {
    for (int index = 0; index < static_cast<int>(state.itemBounds.size()); ++index)
        if (PtInRect(&state.itemBounds[index], point)) return index;
    return -1;
}

void Activate(HWND bar, MenuBarState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.items.size())) return;
    const auto& item = state.items[index];
    if (!item.enabled) return;
    if (item.children.empty()) {
        SendMessageW(GetParent(bar), WM_COMMAND, MAKEWPARAM(item.id, 0),
                     reinterpret_cast<LPARAM>(bar));
        return;
    }
    RECT anchor = state.itemBounds[index];
    MapWindowPoints(bar, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    state.popupOpen = true;
    state.active = index;
    InvalidateRect(bar, nullptr, FALSE);
    internal::ShowPopupMenu(GetParent(bar), bar, anchor, item.children, state.menuAppearance);
    if (!IsWindow(bar)) return;
    state.popupOpen = false;
    InvalidateRect(bar, nullptr, FALSE);
}

void Paint(HWND window, const MenuBarState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (paint::Buffer buffer(target, bounds); buffer) {
        const auto colors = ResolveBarColors(window, state);
        const auto& style = colors.style;
        const auto dpi = paint::Dpi(window);
        paint::Clear(buffer.dc(), bounds, style.background);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const auto font = paint::Font(style.font, dpi);
        const int padding = (std::max)(0, DipToPx(style.paddingXDip, dpi));
        for (int index = 0; index < static_cast<int>(state.items.size()); ++index) {
            const auto& item = state.items[index];
            const auto itemBounds = index < static_cast<int>(state.itemBounds.size())
                                        ? state.itemBounds[index]
                                        : RECT{};
            const bool selected = state.active == index;
            if (selected || state.hot == index) {
                const Gdiplus::RectF selection{
                    static_cast<float>(itemBounds.left), static_cast<float>(itemBounds.top),
                    static_cast<float>(itemBounds.right - itemBounds.left),
                    static_cast<float>(itemBounds.bottom - itemBounds.top)};
                paint::Fill(graphics, selection, paint::ToPixels(style.cornerRadiusDip, dpi),
                            selected ? style.selected : style.hover);
            }
            RECT labelBounds = itemBounds;
            labelBounds.left += padding;
            labelBounds.right -= padding;
            const auto color = !item.enabled ? style.disabledText
                              : selected           ? colors.selectedText
                                                    : style.text;
            paint::Text(buffer.dc(), item.text, labelBounds, font, color,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    EndPaint(window, &ps);
}

LRESULT MenuBarProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<MenuBarState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* options = static_cast<const MenuBarState*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<MenuBarState>(*options);
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_NCDESTROY && state) internal::CancelPopupMenu(window);
    if (state && (message == WM_SIZE || message == WM_DPICHANGED ||
                  message == internal::ThemeChangedMessage))
        Layout(window, *state);

    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_MOUSEMOVE: {
        if (!state) return 0;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        const int hot = HitTest(*state, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        if (state->hot != hot) {
            state->hot = hot;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (state) {
            state->hot = -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state && IsWindowEnabled(window) &&
            HitTest(*state, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}) >= 0) {
            SetFocus(window);
            SetCapture(window);
            state->pressed = true;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state && state->pressed) {
            const bool activate = GetCapture() == window;
            const int index = HitTest(*state, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            state->pressed = false;
            if (GetCapture() == window) ReleaseCapture();
            InvalidateRect(window, nullptr, FALSE);
            if (activate) Activate(window, *state, index);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (state) state->pressed = false;
        return 0;
    case WM_CANCELMODE:
        if (state) {
            state->pressed = false;
            internal::CancelPopupMenu(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_ENABLE:
        if (state && !wParam) {
            state->pressed = false;
            state->hot = -1;
            internal::CancelPopupMenu(window);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_SHOWWINDOW:
        if (state && !wParam) internal::CancelPopupMenu(window);
        return DefWindowProcW(window, message, wParam, lParam);
    case internal::MenuBarSetItemsMessage:
        if (!state || !lParam) return FALSE;
        state->items = *reinterpret_cast<const std::vector<MenuItem>*>(lParam);
        state->hot = -1;
        state->active = -1;
        state->pressed = false;
        Layout(window, *state);
        return TRUE;
    case internal::MenuBarHitTestMessage:
        return state && lParam
                   ? HitTest(*state, *reinterpret_cast<const POINT*>(lParam))
                   : -1;
    case WM_PAINT:
        if (state) Paint(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

HWND Create(const MenuBarOptions& options) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    MenuBarState state;
    state.items = options.items;
    state.menuAppearance = options.menuAppearance;
    return CreateWindowExW(
        0, MenuBarClass, L"", WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(), &state);
}

} // namespace

HWND CreateMenuBar(const MenuBarOptions& options) {
    if (!internal::ValidMenuBarItems(options.items)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const auto window = Create(options);
    if (window) SetStyleOverride(window, options.appearance);
    return window;
}

bool SetMenuBarItems(HWND menuBar, const std::vector<MenuItem>& items) {
    if (!internal::IsLibraryWindow(menuBar, MenuBarClass)) return false;
    if (!internal::ValidMenuBarItems(items)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    internal::CancelPopupMenu(menuBar);
    return SendMessageW(menuBar, internal::MenuBarSetItemsMessage, 0,
                        reinterpret_cast<LPARAM>(&items)) != FALSE;
}

namespace internal {
bool RegisterMenuBarClass() {
    return RegisterControlClass(MenuBarClass, SafeWindowProc<MenuBarProcImpl>);
}
} // namespace internal
} // namespace wcw
