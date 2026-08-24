#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "MenuModel.h"
#include "Paint.h"

#include <algorithm>
#include <commctrl.h>
#include <cwctype>
#include <memory>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t MenuBarClass[] = L"WcwMenuBar";

struct MenuBarState;
LRESULT CALLBACK MenuBarParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR id, DWORD_PTR reference);
MenuBarState* State(HWND window);
void CancelMenuMode(HWND bar);
bool BeginMenuMode(HWND bar, int index = -1);
void RunMenuMode(HWND bar, int index);

struct MenuBarState {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
    std::vector<RECT> itemBounds;
    int hot{-1};
    int active{-1};
    bool pressed{};
    bool popupOpen{};
    bool menuMode{};
    bool altDown{};
    HWND savedFocus{};
};

MenuBarState* State(HWND window) {
    return reinterpret_cast<MenuBarState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

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

void CancelPress(HWND window, MenuBarState& state) {
    const bool changed = state.pressed;
    state.pressed = false;
    if (GetCapture() == window) ReleaseCapture();
    if (changed) InvalidateRect(window, nullptr, FALSE);
}

void CancelMenuMode(HWND bar) {
    internal::CancelPopupMenu(bar);
    auto* state = IsWindow(bar) ? State(bar) : nullptr;
    if (!state) return;
    const auto savedFocus = state->savedFocus;
    const bool changed = state->menuMode || state->popupOpen || state->active >= 0;
    state->savedFocus = nullptr;
    state->menuMode = false;
    state->altDown = false;
    state->popupOpen = false;
    state->active = -1;
    SendMessageW(bar, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEACCEL), 0);
    if (IsWindow(savedFocus)) SetFocus(savedFocus);
    if (changed) InvalidateRect(bar, nullptr, FALSE);
}

bool BeginMenuMode(HWND bar, int index) {
    auto* state = IsWindow(bar) ? State(bar) : nullptr;
    if (!state || !IsWindowEnabled(bar)) return false;
    if (index < 0) index = internal::NextMenuIndex(state->items, -1, 1);
    if (index < 0 || index >= static_cast<int>(state->items.size()) ||
        !state->items[index].enabled)
        return false;
    if (!state->menuMode) {
        state->savedFocus = GetFocus();
        state->menuMode = true;
        SendMessageW(bar, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEACCEL), 0);
    }
    state->active = index;
    SetFocus(bar);
    InvalidateRect(bar, nullptr, FALSE);
    return true;
}

void RunMenuMode(HWND bar, int index) {
    int next = index;
    while (next >= 0 && IsWindow(bar)) {
        auto* state = State(bar);
        if (!state || next >= static_cast<int>(state->items.size())) break;
        const auto& item = state->items[next];
        if (!item.enabled) break;
        state->active = next;
        if (item.children.empty()) {
            const auto parent = GetParent(bar);
            const auto id = item.id;
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, 0), reinterpret_cast<LPARAM>(bar));
            if (!IsWindow(bar)) return;
            CancelMenuMode(bar);
            return;
        }

        RECT anchor = state->itemBounds[next];
        MapWindowPoints(bar, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
        auto children = item.children;
        const auto appearance = state->menuAppearance;
        const auto parent = GetParent(bar);
        state->popupOpen = true;
        InvalidateRect(bar, nullptr, FALSE);
        const auto result = internal::ShowMenuBarPopup(
            parent ? parent : nullptr, bar, anchor, std::move(children), appearance, bar, next);
        if (!IsWindow(bar)) return;
        state = State(bar);
        if (!state) return;
        state->popupOpen = false;
        next = result.nextTopIndex;
    }
    if (IsWindow(bar)) CancelMenuMode(bar);
}

void Activate(HWND bar, MenuBarState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.items.size()) ||
        !state.items[index].enabled)
        return;
    if (!BeginMenuMode(bar, index)) return;
    RunMenuMode(bar, index);
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

bool HandleMnemonic(HWND bar, wchar_t character) {
    auto* state = IsWindow(bar) ? State(bar) : nullptr;
    if (!state) return false;
    character = std::towlower(character);
    for (int index = 0; index < static_cast<int>(state->items.size()); ++index) {
        const auto& item = state->items[index];
        if (item.enabled && internal::ParseMenuLabel(item.text).mnemonic == character) {
            if (!BeginMenuMode(bar, index)) return false;
            RunMenuMode(bar, index);
            return true;
        }
    }
    return false;
}

LRESULT CALLBACK MenuBarParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR id, DWORD_PTR reference) {
    const auto bar = reinterpret_cast<HWND>(reference);
    if (!bar || !IsWindow(bar)) {
        RemoveWindowSubclass(window, MenuBarParentProc, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }
    auto* state = State(bar);
    if (!state) return DefSubclassProc(window, message, wParam, lParam);
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_F10) {
            BeginMenuMode(bar);
            return 0;
        }
        break;
    case WM_SYSKEYDOWN:
        if (wParam == VK_MENU) {
            state->altDown = true;
            return 0;
        }
        break;
    case WM_SYSKEYUP:
        if (wParam == VK_MENU) {
            const bool toggle = state->altDown;
            state->altDown = false;
            if (toggle) {
                if (state->menuMode) CancelMenuMode(bar);
                else BeginMenuMode(bar);
            }
            return 0;
        }
        break;
    case WM_SYSCHAR:
        if (HandleMnemonic(bar, static_cast<wchar_t>(wParam))) return 0;
        break;
    case WM_ACTIVATEAPP:
        if (!wParam) {
            if (state->popupOpen) internal::CancelPopupMenu(bar);
            else CancelMenuMode(bar);
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && (!state->popupOpen || !lParam))
            CancelMenuMode(bar);
        break;
    case WM_CANCELMODE:
        if (state->popupOpen) internal::CancelPopupMenu(bar);
        else CancelMenuMode(bar);
        break;
    case WM_NCDESTROY:
        CancelMenuMode(bar);
        RemoveWindowSubclass(window, MenuBarParentProc, id);
        break;
    default:
        break;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT MenuBarProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = State(window);
    if (message == WM_NCCREATE) {
        const auto* options = static_cast<const MenuBarState*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<MenuBarState>(*options);
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == WM_NCDESTROY && state) {
        CancelMenuMode(window);
        CancelPress(window, *state);
    }
    if (state && (message == WM_SIZE || message == WM_DPICHANGED ||
                  message == internal::ThemeChangedMessage))
        Layout(window, *state);

    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        if (const auto parent = GetParent(window); parent && IsWindow(parent))
            RemoveWindowSubclass(parent, MenuBarParentProc,
                                 reinterpret_cast<UINT_PTR>(window));
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (!state) return 0;
        if (wParam == VK_F10) {
            BeginMenuMode(window);
            return 0;
        }
        if (!state->menuMode) return DefWindowProcW(window, message, wParam, lParam);
        if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            const auto next = internal::NextMenuIndex(
                state->items, state->active, wParam == VK_LEFT ? -1 : 1);
            if (next >= 0 && next != state->active) {
                state->active = next;
                InvalidateRect(window, nullptr, FALSE);
            }
        } else if (wParam == VK_DOWN || wParam == VK_RETURN || wParam == VK_SPACE) {
            const auto active = state->active;
            if (active >= 0) Activate(window, *state, active);
        } else if (wParam == VK_ESCAPE) {
            CancelMenuMode(window);
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (state && wParam == VK_MENU) {
            state->altDown = true;
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_SYSKEYUP:
        if (state && wParam == VK_MENU) {
            const bool toggle = state->altDown;
            state->altDown = false;
            if (toggle) {
                if (state->menuMode) CancelMenuMode(window);
                else BeginMenuMode(window);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_SYSCHAR:
        if (HandleMnemonic(window, static_cast<wchar_t>(wParam))) return 0;
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
        if (state && IsWindowEnabled(window)) {
            const auto index = HitTest(*state, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (index < 0) return 0;
            BeginMenuMode(window, index);
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
        if (state) CancelPress(window, *state);
        return 0;
    case WM_CANCELMODE:
        if (state) {
            CancelPress(window, *state);
            if (state->popupOpen) internal::CancelPopupMenu(window);
            else CancelMenuMode(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_ENABLE:
        if (state && !wParam) {
            CancelPress(window, *state);
            state->hot = -1;
            if (state->popupOpen) internal::CancelPopupMenu(window);
            else CancelMenuMode(window);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_SHOWWINDOW:
        if (state && !wParam) {
            if (state->popupOpen) internal::CancelPopupMenu(window);
            else CancelMenuMode(window);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case internal::MenuBarSetItemsMessage:
        if (!state || !lParam) return FALSE;
        if (state->menuMode && !state->popupOpen) CancelMenuMode(window);
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
    case internal::MenuBarNextItemMessage:
        {
            if (!state) return -1;
            if (lParam) {
                auto point = *reinterpret_cast<const POINT*>(lParam);
                ScreenToClient(window, &point);
                const auto index = HitTest(*state, point);
                return index >= 0 && index < static_cast<int>(state->items.size()) &&
                               state->items[index].enabled
                           ? index
                           : -1;
            }
            const int direction = wParam == static_cast<WPARAM>(-1) ? -1 : 1;
            return internal::NextMenuIndex(state->items, state->active, direction);
        }
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
    const auto window = CreateWindowExW(
        0, MenuBarClass, L"", WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(), &state);
    if (window && !SetWindowSubclass(options.parent, MenuBarParentProc,
                                     reinterpret_cast<UINT_PTR>(window),
                                     reinterpret_cast<DWORD_PTR>(window))) {
        const auto error = GetLastError();
        DestroyWindow(window);
        SetLastError(error);
        return nullptr;
    }
    return window;
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
