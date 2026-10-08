#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Paint.h"

// Adapted from RatAI's vendored WCW tab strip. Page content belongs to the caller.
#include <algorithm>
#include <commctrl.h>
#include <memory>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr UINT_PTR TabSubclassId = 1;

struct TabState {
    std::vector<TabItem> items;
    int selectedIndex{-1};
    int hotIndex{-1};
    bool trackingMouse{};
};

LRESULT CALLBACK TabSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR reference);

TabState* State(HWND window) {
    DWORD_PTR reference{};
    if (!GetWindowSubclass(window, TabSubclassProc, TabSubclassId, &reference)) return nullptr;
    return reinterpret_cast<TabState*>(reference);
}

TabState* CheckedState(HWND window) {
    if (!internal::IsLibraryWindow(window, WC_TABCONTROLW)) return nullptr;
    auto* state = State(window);
    if (!state) SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return state;
}

void ResizeItems(HWND window, const TabState& state) {
    RECT bounds{};
    if (state.items.empty() || !GetClientRect(window, &bounds)) return;
    const int itemCount = static_cast<int>(state.items.size());
    int width = std::max(1, static_cast<int>(bounds.right - bounds.left) / itemCount);
    const int height = std::max(1, static_cast<int>(bounds.bottom - bounds.top));
    TabCtrl_SetItemSize(window, width, height);
    for (int attempt = 0; attempt < 32; ++attempt) {
        RECT firstItem{};
        RECT lastItem{};
        if (TabCtrl_GetItemRect(window, 0, &firstItem) == FALSE ||
            TabCtrl_GetItemRect(window, itemCount - 1, &lastItem) == FALSE ||
            (lastItem.top == firstItem.top && lastItem.right <= bounds.right)) {
            break;
        }
        if (width == 1) break;
        --width;
        TabCtrl_SetItemSize(window, width, height);
    }
}

int HitTest(HWND window, LPARAM position) {
    TCHITTESTINFO hit{};
    hit.pt = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    return TabCtrl_HitTest(window, &hit);
}

void TrackMouse(HWND window, TabState& state) {
    if (state.trackingMouse) return;
    TRACKMOUSEEVENT event{sizeof(event), TME_LEAVE, window, 0};
    state.trackingMouse = TrackMouseEvent(&event) != FALSE;
}

void NotifySelection(HWND window, TabState& state, int newIndex) {
    const int oldIndex = state.selectedIndex;
    if (newIndex == oldIndex || newIndex < 0 ||
        newIndex >= static_cast<int>(state.items.size())) return;
    state.selectedIndex = newIndex;
    SelectionChangedNotification notification{
        {window, static_cast<UINT_PTR>(GetDlgCtrlID(window)), WCN_SELECTION_CHANGED},
        oldIndex,
        newIndex,
        oldIndex >= 0 ? state.items[static_cast<std::size_t>(oldIndex)].id : 0,
        state.items[static_cast<std::size_t>(newIndex)].id};
    SendMessageW(GetParent(window), WM_NOTIFY, notification.header.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
}

void DrawItem(HWND window, const DRAWITEMSTRUCT& item, const TabState& state) {
    if (item.itemID >= state.items.size()) return;
    const auto theme = GetTheme();
    const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
    const bool selected = static_cast<int>(item.itemID) == TabCtrl_GetCurSel(window);
    const bool disabled = IsWindowEnabled(window) == FALSE;
    const bool hot = static_cast<int>(item.itemID) == state.hotIndex;
    const Color background = disabled ? style.disabledSurface
                                      : selected ? style.selected
                                      : hot ? style.hover : style.background;
    paint::Clear(item.hDC, item.rcItem, theme.palette.panel);

    Gdiplus::Graphics graphics(item.hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const unsigned dpi = paint::Dpi(window);
    const float inset = paint::ToPixels(3.0F, dpi);
    const float width = static_cast<float>(item.rcItem.right - item.rcItem.left);
    const float height = static_cast<float>(item.rcItem.bottom - item.rcItem.top);
    const Gdiplus::RectF shape{static_cast<float>(item.rcItem.left) + inset,
                               static_cast<float>(item.rcItem.top) + inset,
                               std::max(1.0F, width - inset * 2.0F),
                               std::max(1.0F, height - inset * 2.0F)};
    const float radius = std::min(paint::ToPixels(style.cornerRadiusDip, dpi),
                                  shape.Height / 2.0F);
    paint::Fill(graphics, shape, radius, background);
    paint::Border(graphics,
                  shape,
                  radius,
                  selected ? style.accent : style.border,
                  paint::ToPixels(style.borderWidthDip, dpi));

    RECT textBounds = item.rcItem;
    InflateRect(&textBounds, -static_cast<int>(inset), -static_cast<int>(inset));
    paint::Text(item.hDC,
                state.items[item.itemID].text,
                textBounds,
                paint::Font(style.font, dpi),
                disabled ? style.disabledText : selected ? style.text : hot ? style.text
                                                                            : style.mutedText,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) != 0) {
        paint::Border(graphics,
                      shape,
                      radius,
                      style.focus,
                      paint::ToPixels(style.focusWidthDip, dpi));
    }
}

void PaintTabs(HWND window, HDC device, const TabState& state) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Clear(device, bounds, GetTheme().palette.panel);
    for (int index = 0; index < static_cast<int>(state.items.size()); ++index) {
        RECT itemBounds{};
        if (TabCtrl_GetItemRect(window, index, &itemBounds) == FALSE) continue;
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_TAB;
        item.CtlID = static_cast<UINT>(GetDlgCtrlID(window));
        item.itemID = static_cast<UINT>(index);
        item.itemAction = ODA_DRAWENTIRE;
        item.itemState = index == TabCtrl_GetCurFocus(window) && GetFocus() == window
                             ? ODS_FOCUS
                             : 0;
        item.hwndItem = window;
        item.hDC = device;
        item.rcItem = itemBounds;
        DrawItem(window, item, state);
    }
}

void InvalidateItem(HWND window, int index) {
    RECT bounds{};
    if (index >= 0 && TabCtrl_GetItemRect(window, index, &bounds) != FALSE) {
        InvalidateRect(window, &bounds, FALSE);
    }
}

LRESULT CALLBACK ParentSubclassProc(HWND parent, UINT message, WPARAM wParam, LPARAM lParam,
                                   UINT_PTR subclassId, DWORD_PTR reference) {
    const HWND tab = reinterpret_cast<HWND>(reference);
    if (message == WM_DRAWITEM && reinterpret_cast<const DRAWITEMSTRUCT*>(lParam) != nullptr) {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item.hwndItem == tab) {
            if (const auto* state = State(tab)) DrawItem(tab, item, *state);
            return TRUE;
        }
    } else if (message == WM_NOTIFY) {
        const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
        if (notification != nullptr && notification->hwndFrom == tab &&
            notification->code == TCN_SELCHANGE) {
            if (auto* state = State(tab)) NotifySelection(tab, *state, TabCtrl_GetCurSel(tab));
            return 0;
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(parent, ParentSubclassProc, subclassId);
    }
    return DefSubclassProc(parent, message, wParam, lParam);
}

LRESULT CALLBACK TabSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                UINT_PTR, DWORD_PTR reference) {
    auto* state = reinterpret_cast<TabState*>(reference);
    if (message == WM_PAINT && state) {
        PAINTSTRUCT paint{};
        const HDC device = BeginPaint(window, &paint);
        RECT bounds{};
        GetClientRect(window, &bounds);
        {
            paint::Buffer buffer(device, bounds);
            PaintTabs(window, buffer ? buffer.dc() : device, *state);
        }
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return TRUE;
    if (message == WM_PRINTCLIENT && state) {
        PaintTabs(window, reinterpret_cast<HDC>(wParam), *state);
        return 0;
    }
    if (message == WM_SIZE && state) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        ResizeItems(window, *state);
        return result;
    }
    if (message == WM_MOUSEMOVE && state) {
        TrackMouse(window, *state);
        const int hotIndex = HitTest(window, lParam);
        if (hotIndex != state->hotIndex) {
            const int previousHotIndex = state->hotIndex;
            state->hotIndex = hotIndex;
            InvalidateItem(window, previousHotIndex);
            InvalidateItem(window, hotIndex);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == WM_MOUSELEAVE && state) {
        state->trackingMouse = false;
        if (state->hotIndex != -1) {
            const int previousHotIndex = state->hotIndex;
            state->hotIndex = -1;
            InvalidateItem(window, previousHotIndex);
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
    if (message == internal::ThemeChangedMessage) {
        InvalidateRect(window, nullptr, TRUE);
        return 0;
    }
    if (message == WM_NCDESTROY) {
        const HWND parent = GetParent(window);
        RemoveWindowSubclass(parent, ParentSubclassProc,
                             reinterpret_cast<UINT_PTR>(window));
        RemoveWindowSubclass(window, TabSubclassProc, TabSubclassId);
        internal::UnregisterWindow(window);
        delete state;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

HWND CreateTabControl(const TabControlOptions& options) {
    if (!options.parent || !IsWindow(options.parent) ||
        GetWindowThreadProcessId(options.parent, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (!internal::Instance() || options.items.empty()) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    auto state = std::make_unique<TabState>();
    state->items = options.items;
    const unsigned dpi = paint::Dpi(options.parent);
    const HWND window = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        options.accessibleName.empty() ? options.text.c_str() : options.accessibleName.c_str(),
        WS_CHILD | WS_TABSTOP | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH | TCS_BUTTONS | TCS_MULTILINE |
            options.style,
        DipToPx(options.bounds.x, dpi),
        DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi),
        DipToPx(options.bounds.height, dpi),
        options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)),
        internal::Instance(),
        nullptr);
    if (!window) return nullptr;

    for (std::size_t index = 0; index < state->items.size(); ++index) {
        TCITEMW item{};
        item.mask = TCIF_TEXT | TCIF_PARAM;
        item.pszText = state->items[index].text.data();
        item.lParam = static_cast<LPARAM>(state->items[index].id);
        if (TabCtrl_InsertItem(window, static_cast<int>(index), &item) == -1) {
            DestroyWindow(window);
            SetLastError(ERROR_INVALID_DATA);
            return nullptr;
        }
    }
    const int selected = std::clamp(options.selectedIndex, 0,
                                    static_cast<int>(state->items.size()) - 1);
    TabCtrl_SetCurSel(window, selected);
    state->selectedIndex = selected;
    if (!SetWindowSubclass(window, TabSubclassProc, TabSubclassId,
                           reinterpret_cast<DWORD_PTR>(state.get()))) {
        DestroyWindow(window);
        return nullptr;
    }
    if (!SetWindowSubclass(options.parent,
                           ParentSubclassProc,
                           reinterpret_cast<UINT_PTR>(window),
                           reinterpret_cast<DWORD_PTR>(window))) {
        RemoveWindowSubclass(window, TabSubclassProc, TabSubclassId);
        DestroyWindow(window);
        return nullptr;
    }
    ResizeItems(window, *state);
    state.release();
    try {
        internal::RegisterWindow(window);
        SetStyleOverride(window, options.appearance);
    } catch (...) {
        DestroyWindow(window);
        throw;
    }
    return window;
}

bool SetTabSelection(HWND tabControl, int index) {
    auto* state = CheckedState(tabControl);
    if (!state) return false;
    if (index < 0 || index >= static_cast<int>(state->items.size())) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (index == state->selectedIndex) return true;
    if (TabCtrl_SetCurSel(tabControl, index) == -1) return false;
    InvalidateRect(tabControl, nullptr, TRUE);
    // The parent may destroy the tab inside its notification handler.
    NotifySelection(tabControl, *state, index);
    return true;
}

int GetTabSelection(HWND tabControl) {
    return CheckedState(tabControl) ? TabCtrl_GetCurSel(tabControl) : -1;
}

} // namespace wcw
