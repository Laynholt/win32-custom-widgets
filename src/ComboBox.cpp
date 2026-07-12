#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "ComboModel.h"
#include "Internal.h"
#include "Accessibility.h"
#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <memory>
#include <new>
#include <utility>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t ComboClass[] = L"WcwComboBox";
constexpr wchar_t PopupClass[] = L"WcwComboPopup";
constexpr float RowHeightDip = 36.0f;
constexpr float ScrollbarWidthDip = 12.0f;

struct ComboState {
    internal::ComboModel model;
    HWND popup{};
    std::vector<HWND> observedAncestors;
    float popupHeightDip{240};
    bool hover{};
    bool pressed{};
};

struct PopupState {
    HWND combo{};
    int firstVisible{};
    int hover{-1};
    bool draggingScrollbar{};
    float scrollbarAnchor{};
    int wheelRemainder{};
};

LRESULT CALLBACK AncestorProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

ComboState* State(HWND combo) {
    return reinterpret_cast<ComboState*>(GetWindowLongPtrW(combo, GWLP_USERDATA));
}

bool IsComboWindow(HWND window) {
    wchar_t name[32]{};
    if (!IsWindow(window) || !GetClassNameW(window, name, 32) || wcscmp(name, ComboClass) != 0 ||
        !internal::IsLibraryWindow(window)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

std::intptr_t ItemId(const internal::ComboModel& model, int index) {
    return index >= 0 && index < static_cast<int>(model.Items().size()) ? model.Items()[index].id : 0;
}

void NotifyChanged(HWND combo, int oldIndex, std::intptr_t oldId) {
    const auto* state = State(combo);
    if (!state || oldIndex == state->model.Selection()) return;
    SelectionChangedNotification notification{
        {combo, static_cast<UINT_PTR>(GetDlgCtrlID(combo)), WCN_SELECTION_CHANGED},
        oldIndex, state->model.Selection(), oldId, ItemId(state->model, state->model.Selection())};
    SendMessageW(GetParent(combo), WM_NOTIFY, notification.header.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
    internal::NotifyAccessibility(combo, EVENT_OBJECT_SELECTION);
    internal::NotifyAccessibility(combo, EVENT_OBJECT_VALUECHANGE);
}

void RemoveObservers(HWND combo, ComboState& state) {
    const auto id = reinterpret_cast<UINT_PTR>(combo);
    for (const auto ancestor : state.observedAncestors)
        if (IsWindow(ancestor)) RemoveWindowSubclass(ancestor, AncestorProc, id);
    state.observedAncestors.clear();
}

void ClosePopup(HWND combo, bool commit, bool restoreFocus = true) {
    auto* state = State(combo);
    if (!state || !state->popup) return;
    const int oldIndex = state->model.Selection();
    const auto oldId = ItemId(state->model, oldIndex);
    const auto popup = std::exchange(state->popup, nullptr);
    RemoveObservers(combo, *state);
    if (commit) state->model.Commit(); else state->model.Cancel();
    if (GetCapture() == popup) ReleaseCapture();
    if (IsWindow(popup)) DestroyWindow(popup);
    if (IsWindow(combo)) {
        InvalidateRect(combo, nullptr, FALSE);
        if (restoreFocus && IsWindowEnabled(combo)) SetFocus(combo);
        if (commit) NotifyChanged(combo, oldIndex, oldId);
        internal::NotifyAccessibility(combo, EVENT_OBJECT_STATECHANGE);
    }
}

int RowHeight(HWND window) { return std::max(1, DipToPx(RowHeightDip, paint::Dpi(window))); }

int VisibleRows(HWND popup) {
    RECT bounds{};
    GetClientRect(popup, &bounds);
    return std::max(1, static_cast<int>(bounds.bottom - bounds.top) / RowHeight(popup));
}

void EnsureVisible(HWND popup, PopupState& popupState) {
    const auto* comboState = State(popupState.combo);
    if (!comboState) return;
    const int highlighted = comboState->model.Highlighted();
    const int visible = VisibleRows(popup);
    const int maximum = std::max(0, static_cast<int>(comboState->model.Items().size()) - visible);
    if (highlighted < popupState.firstVisible) popupState.firstVisible = highlighted;
    if (highlighted >= popupState.firstVisible + visible)
        popupState.firstVisible = highlighted - visible + 1;
    popupState.firstVisible = std::clamp(popupState.firstVisible, 0, maximum);
    InvalidateRect(popup, nullptr, FALSE);
}

void DragScrollbar(HWND popup, PopupState& popupState, int y) {
    const auto* comboState = State(popupState.combo);
    if (!comboState) return;
    RECT bounds{};
    GetClientRect(popup, &bounds);
    const int visible = VisibleRows(popup);
    const int maximum = std::max(0, static_cast<int>(comboState->model.Items().size()) - visible);
    if (!maximum) return;
    const int rowHeight = RowHeight(popup);
    const ScrollbarGeometry bar{0, static_cast<float>(bounds.bottom),
                                static_cast<float>(comboState->model.Items().size() * rowHeight),
                                static_cast<float>(bounds.bottom),
                                static_cast<float>(popupState.firstVisible * rowHeight),
                                static_cast<float>(DipToPx(28, paint::Dpi(popup)))};
    const float travel = static_cast<float>(bounds.bottom) - bar.thumbSizePx;
    const float thumb = std::clamp(static_cast<float>(y) - popupState.scrollbarAnchor, 0.0f, travel);
    popupState.firstVisible = travel > 0
        ? static_cast<int>(std::lround(thumb / travel * maximum)) : 0;
    InvalidateRect(popup, nullptr, FALSE);
}

void DrawImage(HDC dc, const std::optional<ImageSource>& source, RECT bounds) {
    if (!source || !source->handle) return;
    if (source->kind == ImageSource::Kind::Icon)
        paint::Icon(dc, static_cast<HICON>(source->handle), bounds);
    else if (source->kind == ImageSource::Kind::Bitmap)
        paint::Bitmap(dc, static_cast<HBITMAP>(source->handle), bounds);
}

void PaintCombo(HWND window, const ComboState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
        const auto dpi = paint::Dpi(window);
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
        const bool enabled = IsWindowEnabled(window) != FALSE;
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::RectF surface{0, 0, static_cast<float>(bounds.right),
                                     static_cast<float>(bounds.bottom)};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        const auto background = !enabled ? style.disabledSurface
                              : state.pressed ? style.pressed
                              : state.hover || state.popup ? style.hover : style.background;
        paint::Fill(graphics, surface, radius, background);
        paint::Border(graphics, surface, radius, style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));

        const int padding = DipToPx(style.paddingXDip, dpi);
        const int chevronWidth = DipToPx(20, dpi);
        RECT textBounds{padding, 0, bounds.right - padding - chevronWidth, bounds.bottom};
        const int selected = state.model.Selection();
        if (selected >= 0) {
            const auto& item = state.model.Items()[selected];
            if (item.image) {
                const int size = std::min(DipToPx(18, dpi),
                                          static_cast<int>(bounds.bottom) - 2 * padding);
                RECT imageBounds{textBounds.left, (bounds.bottom - size) / 2,
                                 textBounds.left + size, (bounds.bottom + size) / 2};
                DrawImage(buffer.dc(), item.image, imageBounds);
                textBounds.left = imageBounds.right + DipToPx(style.spacingDip, dpi);
            }
            paint::Text(buffer.dc(), item.text, textBounds, paint::Font(style.font, dpi),
                        enabled ? style.text : style.disabledText,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        const float centerX = static_cast<float>(bounds.right - padding - chevronWidth / 2);
        const float centerY = static_cast<float>(bounds.bottom) / 2.0f;
        Gdiplus::Pen pen(paint::GdiPlusColor(enabled ? style.text : style.disabledText),
                         std::max(1.0f, paint::ToPixels(style.borderWidthDip, dpi)));
        graphics.DrawLine(&pen, centerX - 4, centerY - 2, centerX, centerY + 2);
        graphics.DrawLine(&pen, centerX, centerY + 2, centerX + 4, centerY - 2);
        if (GetFocus() == window) {
            paint::Focus(graphics, surface, radius, style.focus,
                         paint::ToPixels(style.focusWidthDip, dpi));
        }
    }
    EndPaint(window, &ps);
}

void PaintPopup(HWND window, const PopupState& popupState) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    const auto* comboState = State(popupState.combo);
    if (buffer && comboState) {
        const auto dpi = paint::Dpi(window);
        const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(popupState.combo));
        paint::Clear(buffer.dc(), bounds, style.background);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const Gdiplus::RectF surface{0, 0, static_cast<float>(bounds.right),
                                     static_cast<float>(bounds.bottom)};
        paint::Border(graphics, surface, paint::ToPixels(style.cornerRadiusDip, dpi), style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));

        const int rowHeight = RowHeight(window);
        const int visible = VisibleRows(window);
        const bool scrolling = static_cast<int>(comboState->model.Items().size()) > visible;
        const int scrollbarWidth = scrolling ? DipToPx(ScrollbarWidthDip, dpi) : 0;
        const int padding = DipToPx(style.paddingXDip, dpi);
        for (int row = 0; row < visible; ++row) {
            const int index = popupState.firstVisible + row;
            if (index >= static_cast<int>(comboState->model.Items().size())) break;
            Gdiplus::RectF rowBounds{1.0f, static_cast<float>(row * rowHeight),
                                     static_cast<float>(bounds.right - scrollbarWidth - 2),
                                     static_cast<float>(rowHeight)};
            if (index == comboState->model.Highlighted())
                paint::Fill(graphics, rowBounds, 0, style.selected);
            else if (index == popupState.hover)
                paint::Fill(graphics, rowBounds, 0, style.hover);
            RECT textBounds{padding, row * rowHeight, bounds.right - scrollbarWidth - padding,
                            (row + 1) * rowHeight};
            const auto& item = comboState->model.Items()[index];
            if (item.image) {
                const int size = std::min(DipToPx(18, dpi), rowHeight - 2 * padding);
                RECT imageBounds{textBounds.left, row * rowHeight + (rowHeight - size) / 2,
                                 textBounds.left + size,
                                 row * rowHeight + (rowHeight + size) / 2};
                DrawImage(buffer.dc(), item.image, imageBounds);
                textBounds.left = imageBounds.right + DipToPx(style.spacingDip, dpi);
            }
            paint::Text(buffer.dc(), item.text, textBounds, paint::Font(style.font, dpi), style.text,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        if (scrolling) {
            const float left = static_cast<float>(bounds.right - scrollbarWidth);
            const ScrollbarGeometry bar{0, static_cast<float>(bounds.bottom),
                                        static_cast<float>(comboState->model.Items().size() * rowHeight),
                                        static_cast<float>(bounds.bottom),
                                        static_cast<float>(popupState.firstVisible * rowHeight),
                                        static_cast<float>(DipToPx(28, dpi))};
            paint::Fill(graphics, {left, 0, static_cast<float>(scrollbarWidth),
                                    static_cast<float>(bounds.bottom)}, 0, style.border);
            paint::Fill(graphics, {left, bar.thumbStartPx, static_cast<float>(scrollbarWidth),
                                    bar.thumbSizePx},
                        static_cast<float>(scrollbarWidth) / 2, style.mutedText);
        }
    }
    EndPaint(window, &ps);
}

RECT PopupBounds(HWND combo, const ComboState& state) {
    RECT comboBounds{};
    GetWindowRect(combo, &comboBounds);
    const auto dpi = paint::Dpi(combo);
    const int rowHeight = DipToPx(RowHeightDip, dpi);
    const int desired = std::max(rowHeight, DipToPx(state.popupHeightDip, dpi));
    const int height = std::min(desired,
                                rowHeight * static_cast<int>(state.model.Items().size()));
    RECT popupBounds{comboBounds.left, comboBounds.bottom, comboBounds.right,
                     comboBounds.bottom + height};
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&popupBounds, MONITOR_DEFAULTTONEAREST), &monitor);
    if (popupBounds.bottom > monitor.rcWork.bottom) {
        popupBounds.top = comboBounds.top - height;
        popupBounds.bottom = comboBounds.top;
    }
    popupBounds.left = std::clamp(popupBounds.left, monitor.rcWork.left,
                                  std::max(monitor.rcWork.left,
                                           monitor.rcWork.right - (popupBounds.right - popupBounds.left)));
    popupBounds.right = popupBounds.left + (comboBounds.right - comboBounds.left);
    return popupBounds;
}

void RepositionPopup(HWND combo) {
    auto* state = State(combo);
    if (!state || !state->popup) return;
    if (!IsWindowVisible(combo)) {
        ClosePopup(combo, false, false);
        return;
    }
    const auto bounds = PopupBounds(combo, *state);
    SetWindowPos(state->popup, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                 bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER);
}

LRESULT CALLBACK AncestorProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                              UINT_PTR, DWORD_PTR reference) {
    const auto combo = reinterpret_cast<HWND>(reference);
    if ((message == WM_SHOWWINDOW && !wParam) || (message == WM_ENABLE && !wParam) ||
        message == WM_DESTROY || message == WM_NCDESTROY)
        ClosePopup(combo, false, false);
    const auto result = DefSubclassProc(window, message, wParam, lParam);
    if ((message == WM_MOVE || message == WM_SIZE || message == WM_WINDOWPOSCHANGED) &&
        IsWindow(combo))
        RepositionPopup(combo);
    return result;
}

void OpenPopup(HWND combo) {
    auto* state = State(combo);
    if (!state || state->popup || state->model.Items().empty() || !IsWindowEnabled(combo) ||
        !IsWindowVisible(combo))
        return;
    state->model.Open();
    const auto popupBounds = PopupBounds(combo, *state);

    state->popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, L"", WS_POPUP,
        popupBounds.left, popupBounds.top, popupBounds.right - popupBounds.left,
        popupBounds.bottom - popupBounds.top, combo, nullptr, internal::Instance(), combo);
    if (!state->popup) {
        state->model.Cancel();
        return;
    }
    const auto id = reinterpret_cast<UINT_PTR>(combo);
    for (auto ancestor = GetParent(combo); ancestor; ancestor = GetParent(ancestor)) {
        if (SetWindowSubclass(ancestor, AncestorProc, id, reinterpret_cast<DWORD_PTR>(combo)))
            state->observedAncestors.push_back(ancestor);
    }
    ShowWindow(state->popup, SW_SHOWNOACTIVATE);
    SetFocus(state->popup);
    SetCapture(state->popup);
    InvalidateRect(combo, nullptr, FALSE);
    internal::NotifyAccessibility(combo, EVENT_OBJECT_STATECHANGE);
}

void CommitClosedNavigation(HWND window, ComboState& state, WPARAM key) {
    const int oldIndex = state.model.Selection();
    const auto oldId = ItemId(state.model, oldIndex);
    state.model.Open();
    if (key == VK_HOME) state.model.Home();
    else if (key == VK_END) state.model.End();
    else {
        const int page = std::max(1, DipToPx(state.popupHeightDip, paint::Dpi(window)) /
                                        RowHeight(window));
        const int direction = key == VK_UP ? -1 : key == VK_PRIOR ? -page
                              : key == VK_NEXT ? page : 1;
        state.model.Navigate(direction);
    }
    state.model.Commit();
    InvalidateRect(window, nullptr, FALSE);
    NotifyChanged(window, oldIndex, oldId);
}

void CommitClosedPrefix(HWND window, ComboState& state, wchar_t character) {
    const int oldIndex = state.model.Selection();
    const auto oldId = ItemId(state.model, oldIndex);
    state.model.PrefixSearch(character, GetTickCount64());
    state.model.Commit(true);
    InvalidateRect(window, nullptr, FALSE);
    NotifyChanged(window, oldIndex, oldId);
}

LRESULT ComboProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = State(window);
    if (message == WM_NCCREATE) {
        const auto* options = static_cast<const ComboBoxOptions*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<ComboState>();
        created->model.SetItems(options->items);
        created->model.SetSelection(options->selectedIndex);
        created->popupHeightDip = options->popupHeightDip;
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if ((message == internal::ThemeChangedMessage || message == WM_DPICHANGED) && state)
        ClosePopup(window, false);
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        ClosePopup(window, false, false);
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ENABLE:
        if (state && !wParam) ClosePopup(window, false, false);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_SHOWWINDOW:
        if (state && !wParam) ClosePopup(window, false, false);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_WINDOWPOSCHANGED:
        if (state && state->popup) RepositionPopup(window);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_MOUSEMOVE:
        if (state && !state->hover) {
            state->hover = true;
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (state) state->hover = false;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (state && IsWindowEnabled(window)) {
            SetFocus(window);
            state->pressed = true;
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state && state->pressed) {
            state->pressed = false;
            RECT bounds{};
            GetClientRect(window, &bounds);
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (GetCapture() == window) ReleaseCapture();
            if (PtInRect(&bounds, point)) OpenPopup(window);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        if (state) state->pressed = false;
        if (message == WM_CANCELMODE && GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_KEYDOWN:
        if (!state || !IsWindowEnabled(window)) return 0;
        if (wParam == VK_F4 || wParam == VK_RETURN || wParam == VK_SPACE) OpenPopup(window);
        else if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_HOME || wParam == VK_END ||
                 wParam == VK_PRIOR || wParam == VK_NEXT)
            CommitClosedNavigation(window, *state, wParam);
        return 0;
    case WM_CHAR:
        if (state && IsWindowEnabled(window))
            CommitClosedPrefix(window, *state, static_cast<wchar_t>(wParam));
        return 0;
    case internal::ComboSetItemsMessage:
        if (state && lParam) {
            ClosePopup(window, false);
            const int oldIndex = state->model.Selection();
            const auto oldId = ItemId(state->model, oldIndex);
            const auto oldText = oldIndex >= 0 ? state->model.Items()[oldIndex].text
                                               : std::wstring{};
            state->model.SetItems(*reinterpret_cast<const std::vector<ComboItem>*>(lParam));
            InvalidateRect(window, nullptr, FALSE);
            const int newIndex = state->model.Selection();
            if (oldIndex >= 0 && newIndex < 0) {
                NotifyChanged(window, oldIndex, oldId);
            } else if (newIndex >= 0 && oldId == ItemId(state->model, newIndex) &&
                       oldText != state->model.Items()[newIndex].text) {
                internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
            }
            return TRUE;
        }
        return FALSE;
    case internal::ComboSetSelectionMessage:
        if (state) {
            const int index = static_cast<int>(wParam);
            if (index < -1 || index >= static_cast<int>(state->model.Items().size())) return FALSE;
            const int oldIndex = state->model.Selection();
            const auto oldId = ItemId(state->model, oldIndex);
            state->model.SetSelection(index);
            InvalidateRect(window, nullptr, FALSE);
            NotifyChanged(window, oldIndex, oldId);
            return TRUE;
        }
        return FALSE;
    case internal::ComboGetSelectionMessage:
        return state ? state->model.Selection() : -1;
    case internal::ComboGetAccessibleValueMessage:
        if (state && lParam) {
            auto& text = *reinterpret_cast<std::wstring*>(lParam);
            const int selected = state->model.Selection();
            text = selected >= 0 ? state->model.Items()[selected].text : std::wstring{};
            return TRUE;
        }
        return FALSE;
    case internal::ComboGetOpenMessage:
        return state ? reinterpret_cast<LRESULT>(state->popup) : 0;
    case WM_PAINT:
        if (state) PaintCombo(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT PopupProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PopupState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto created = std::make_unique<PopupState>();
        created->combo = static_cast<HWND>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if ((message == internal::ThemeChangedMessage || message == WM_DPICHANGED) && state) {
        ClosePopup(state->combo, false);
        return 0;
    }
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;
    switch (message) {
    case WM_NCDESTROY:
        if (state && IsWindow(state->combo)) {
            if (auto* comboState = State(state->combo); comboState && comboState->popup == window) {
                comboState->popup = nullptr;
                comboState->model.Cancel();
                RemoveObservers(state->combo, *comboState);
            }
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTALLKEYS;
    case WM_ACTIVATE:
        if (state && LOWORD(wParam) == WA_INACTIVE) ClosePopup(state->combo, false, false);
        return 0;
    case WM_KILLFOCUS:
        if (state) ClosePopup(state->combo, false, false);
        return 0;
    case WM_SETFOCUS:
        if (state) internal::NotifyAccessibility(state->combo, EVENT_OBJECT_FOCUS);
        return 0;
    case WM_KEYDOWN:
        if (!state || !IsWindow(state->combo)) return 0;
        if (wParam == VK_TAB) {
            const auto combo = state->combo;
            const auto next = GetNextDlgTabItem(GetParent(combo), combo,
                                                GetKeyState(VK_SHIFT) < 0);
            ClosePopup(combo, false, false);
            SetFocus(next ? next : combo);
        } else if (wParam == VK_ESCAPE) ClosePopup(state->combo, false);
        else if (wParam == VK_RETURN) ClosePopup(state->combo, true);
        else if (auto* comboState = State(state->combo)) {
            if (wParam == VK_UP) comboState->model.Navigate(-1);
            else if (wParam == VK_DOWN) comboState->model.Navigate(1);
            else if (wParam == VK_HOME) comboState->model.Home();
            else if (wParam == VK_END) comboState->model.End();
            else if (wParam == VK_PRIOR) comboState->model.Navigate(-VisibleRows(window));
            else if (wParam == VK_NEXT) comboState->model.Navigate(VisibleRows(window));
            else return 0;
            EnsureVisible(window, *state);
        }
        return 0;
    case WM_CHAR:
        if (state && IsWindow(state->combo)) {
            if (auto* comboState = State(state->combo)) {
                comboState->model.PrefixSearch(static_cast<wchar_t>(wParam), GetTickCount64());
                EnsureVisible(window, *state);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (state && IsWindow(state->combo)) {
            const auto* comboState = State(state->combo);
            const int maximum = std::max(0, static_cast<int>(comboState->model.Items().size()) -
                                                VisibleRows(window));
            state->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = state->wheelRemainder / WHEEL_DELTA;
            state->wheelRemainder %= WHEEL_DELTA;
            if (notches) {
                state->firstVisible = std::clamp(state->firstVisible - notches * 3, 0, maximum);
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    case WM_CANCELMODE:
        if (state) ClosePopup(state->combo, false);
        return 0;
    case WM_CAPTURECHANGED:
        if (state && reinterpret_cast<HWND>(lParam) != window)
            ClosePopup(state->combo, false, false);
        return 0;
    case WM_MOUSEMOVE:
        if (state && IsWindow(state->combo)) {
            if (state->draggingScrollbar) {
                DragScrollbar(window, *state, GET_Y_LPARAM(lParam));
                return 0;
            }
            RECT bounds{};
            GetClientRect(window, &bounds);
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int index = state->firstVisible + point.y / RowHeight(window);
            const auto* comboState = State(state->combo);
            state->hover = PtInRect(&bounds, point) &&
                           point.y < VisibleRows(window) * RowHeight(window) && index >= 0 &&
                           index < static_cast<int>(comboState->model.Items().size()) ? index : -1;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!PtInRect(&bounds, point)) {
                ClosePopup(state->combo, false);
            } else if (const auto* comboState = State(state->combo)) {
                const int visible = VisibleRows(window);
                const bool scrolling = static_cast<int>(comboState->model.Items().size()) > visible;
                const int scrollbarWidth = DipToPx(ScrollbarWidthDip, paint::Dpi(window));
                if (scrolling && point.x >= bounds.right - scrollbarWidth) {
                    const int rowHeight = RowHeight(window);
                    const ScrollbarGeometry bar{
                        0, static_cast<float>(bounds.bottom),
                        static_cast<float>(comboState->model.Items().size() * rowHeight),
                        static_cast<float>(bounds.bottom),
                        static_cast<float>(state->firstVisible * rowHeight),
                        static_cast<float>(DipToPx(28, paint::Dpi(window)))};
                    state->draggingScrollbar = true;
                    state->scrollbarAnchor = point.y >= bar.thumbStartPx &&
                                                     point.y <= bar.thumbStartPx + bar.thumbSizePx
                        ? static_cast<float>(point.y) - bar.thumbStartPx : bar.thumbSizePx / 2;
                    DragScrollbar(window, *state, point.y);
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (state && IsWindow(state->combo)) {
            if (state->draggingScrollbar) {
                state->draggingScrollbar = false;
                return 0;
            }
            RECT bounds{};
            GetClientRect(window, &bounds);
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const int index = state->firstVisible + point.y / RowHeight(window);
            auto* comboState = State(state->combo);
            if (PtInRect(&bounds, point) &&
                point.y < VisibleRows(window) * RowHeight(window) && index >= 0 &&
                index < static_cast<int>(comboState->model.Items().size())) {
                while (comboState->model.Highlighted() < index) comboState->model.Navigate(1);
                while (comboState->model.Highlighted() > index) comboState->model.Navigate(-1);
                ClosePopup(state->combo, true);
            }
        }
        return 0;
    case WM_PAINT:
        if (state) PaintPopup(window, *state);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

template <LRESULT (*Procedure)(HWND, UINT, WPARAM, LPARAM)>
LRESULT CALLBACK SafeProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return Procedure(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

HWND CreateComboBox(const ComboBoxOptions& options) {
    if (!options.parent || !IsWindow(options.parent)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    if (options.selectedIndex < -1 || options.selectedIndex >= static_cast<int>(options.items.size()) ||
        !std::isfinite(options.popupHeightDip) || options.popupHeightDip <= 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    const auto window = CreateWindowExW(0, ComboClass, L"", WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(),
        const_cast<ComboBoxOptions*>(&options));
    if (window) SetStyleOverride(window, options.appearance);
    internal::RegisterAccessibility(window, internal::AccessibleKind::ComboBox, options);
    return window;
}

bool SetComboItems(HWND comboBox, const std::vector<ComboItem>& items) {
    return IsComboWindow(comboBox) &&
           SendMessageW(comboBox, internal::ComboSetItemsMessage, 0,
                        reinterpret_cast<LPARAM>(&items)) != FALSE;
}

bool SetComboSelection(HWND comboBox, int index) {
    return IsComboWindow(comboBox) &&
           SendMessageW(comboBox, internal::ComboSetSelectionMessage,
                        static_cast<WPARAM>(static_cast<INT_PTR>(index)), 0) != FALSE;
}

int GetComboSelection(HWND comboBox) {
    return IsComboWindow(comboBox)
        ? static_cast<int>(SendMessageW(comboBox, internal::ComboGetSelectionMessage, 0, 0)) : -1;
}

namespace internal {
bool RegisterComboBoxClasses() {
    return RegisterControlClass(ComboClass, SafeProc<ComboProcImpl>) &&
           RegisterControlClass(PopupClass, SafeProc<PopupProcImpl>);
}
} // namespace internal
} // namespace wcw
