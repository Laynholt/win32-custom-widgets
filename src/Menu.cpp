#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "MenuModel.h"
#include "Paint.h"

#include <algorithm>
#include <commctrl.h>
#include <gdiplus.h>
#include <memory>
#include <utility>
#include <windowsx.h>

namespace wcw {
namespace {

constexpr wchar_t PopupClass[] = L"WcwMenuPopup";
constexpr float RowHeightDip = 32.0f;
constexpr float SeparatorHeightDip = 8.0f;
constexpr float ImageWidthDip = 20.0f;
constexpr float ChevronWidthDip = 16.0f;
constexpr UINT_PTR ChildTimer = 1;
constexpr UINT ChildDelayMs = 200;

struct PopupLevel {
    HWND window{};
    const std::vector<MenuItem>* items{};
    int selected{-1};
    RECT anchor{};
    std::vector<RECT> rows;
    int contentHeight{};
    int scrollOffset{};
    int wheelRemainder{};
};

struct Layout {
    int width{};
    int height{};
    int paddingX{};
    int paddingY{};
    int spacing{};
    int imageWidth{};
    int labelWidth{};
    int shortcutWidth{};
    int chevronWidth{};
};

class PopupController;
LRESULT PopupProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                           UINT_PTR id, DWORD_PTR reference);

thread_local PopupController* activeController;

Color SystemColor(int index) {
    const auto color = GetSysColor(index);
    return Color::FromRgb(GetRValue(color), GetGValue(color), GetBValue(color));
}

int TextWidth(HDC dc, HFONT font, const std::wstring& text) {
    if (text.empty()) return 0;
    const auto previous = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
    SelectObject(dc, previous);
    return size.cx;
}

Layout MeasureLayout(HWND window, const std::vector<MenuItem>& items,
                     const ResolvedStyle& style) {
    Layout result;
    const auto dpi = paint::Dpi(window);
    result.paddingX = DipToPx(style.paddingXDip, dpi);
    result.paddingY = DipToPx(style.paddingYDip, dpi);
    result.spacing = DipToPx(style.spacingDip, dpi);
    result.imageWidth = DipToPx(ImageWidthDip, dpi);
    result.chevronWidth = std::ranges::any_of(items, [](const auto& item) {
        return !item.separator && !item.children.empty();
    }) ? DipToPx(ChevronWidthDip, dpi) : 0;

    const auto dc = GetDC(window);
    const auto font = paint::Font(style.font, dpi);
    for (const auto& item : items) {
        if (item.separator) continue;
        result.labelWidth = (std::max)(result.labelWidth, TextWidth(dc, font, item.text));
        result.shortcutWidth = (std::max)(result.shortcutWidth,
                                          TextWidth(dc, font, item.shortcut));
    }
    ReleaseDC(window, dc);

    result.width = result.paddingX * 2 + result.imageWidth + result.spacing + result.labelWidth;
    if (result.shortcutWidth) result.width += result.spacing + result.shortcutWidth;
    if (result.chevronWidth) result.width += result.spacing + result.chevronWidth;
    result.width = (std::max)(result.width, DipToPx(96, dpi));
    result.height = result.paddingY * 2;
    for (const auto& item : items)
        result.height += DipToPx(item.separator ? SeparatorHeightDip : RowHeightDip, dpi);
    return result;
}

bool Contains(RECT rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right &&
           point.y >= rect.top && point.y < rect.bottom;
}

std::vector<internal::AccessibleMenuItem> AccessibleItems(const PopupLevel& level,
                                                          int& focusedChild) {
    std::vector<internal::AccessibleMenuItem> accessible;
    RECT bounds{};
    GetWindowRect(level.window, &bounds);
    focusedChild = 0;
    int child{};
    for (int row = 0; row < static_cast<int>(level.items->size()); ++row) {
        const auto& item = (*level.items)[row];
        if (item.separator) continue;
        ++child;
        auto screenBounds = level.rows[row];
        OffsetRect(&screenBounds, bounds.left, bounds.top - level.scrollOffset);
        RECT visible{};
        const bool onScreen = IntersectRect(&visible, &screenBounds, &bounds) != FALSE;
        accessible.push_back({item.text, visible, row, item.enabled, item.checked,
                              !item.children.empty(), !onScreen});
        if (row == level.selected) focusedChild = child;
    }
    return accessible;
}

class PopupController {
public:
    PopupController(HWND commandTarget, HWND source, RECT anchor,
                    std::vector<MenuItem> items, StyleOverride appearance,
                    HWND menuBar = nullptr, int topIndex = -1)
        : commandTarget_(commandTarget), source_(source), rootAnchor_(anchor),
          items_(std::move(items)), appearance_(std::move(appearance)), menuBar_(menuBar),
          topIndex_(topIndex) {}

    bool Run();
    int Command() const { return command_; }
    HWND CommandTarget() const { return commandTarget_; }
    HWND Source() const { return source_; }
    bool OwnsWindow(HWND window) const {
        for (const auto& level : levels_)
            if (level && level->window == window) return true;
        return false;
    }
    int NextTopIndex() const { return nextTopIndex_; }
    void Select(size_t level, int row);
    void OpenChild(size_t level, int row, bool immediate);
    void CloseFrom(size_t level);
    void Activate(size_t level, int row);
    void Cancel() { command_ = 0; done_ = true; }

private:
    bool CreateLevel(const std::vector<MenuItem>& items, RECT anchor, bool root);
    std::pair<size_t, int> HitTest(POINT screen) const;
    bool InsideChain(POINT screen) const;
    size_t LevelIndex(HWND window) const;
    void MouseMove(HWND window, LPARAM lParam);
    void MouseUp(HWND window, LPARAM lParam);
    void DismissOutside(HWND window, LPARAM lParam);
    void PointerUp(POINT point);
    void MouseWheel(HWND window, WPARAM wParam, LPARAM lParam);
    void KeyDown(WPARAM key);
    void EnsureVisible(size_t level, int row);
    void StopChildTimer();
    void ChildTimerElapsed();
    void WindowDestroyed(HWND window);
    int UpdateAccessibility(size_t level);
    void UpdateRegion(HWND window);
    void Paint(HWND window);
    bool InsideHost(POINT screen) const;
    int HostItem(POINT screen) const;
    bool SwitchFromHost(POINT screen, bool release);

    friend LRESULT PopupProcImpl(HWND, UINT, WPARAM, LPARAM);
    friend LRESULT CALLBACK OwnerProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    HWND commandTarget_{};
    HWND source_{};
    HWND previousFocus_{};
    RECT rootAnchor_{};
    std::vector<MenuItem> items_;
    StyleOverride appearance_;
    std::vector<std::unique_ptr<PopupLevel>> levels_;
    HWND menuBar_{};
    int topIndex_{-1};
    int nextTopIndex_{-1};
    int command_{};
    bool done_{};
    bool shown_{};
    size_t pendingLevel_{static_cast<size_t>(-1)};
    int pendingRow_{-1};
};

bool PopupController::CreateLevel(const std::vector<MenuItem>& items, RECT anchor, bool root) {
    auto level = std::make_unique<PopupLevel>();
    level->items = &items;
    level->anchor = anchor;
    auto* raw = level.get();
    levels_.push_back(std::move(level));

    raw->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, PopupClass, L"", WS_POPUP,
                                  anchor.left, anchor.top, 1, 1, commandTarget_, nullptr,
                                  internal::Instance(), raw);
    if (!raw->window) {
        levels_.pop_back();
        return false;
    }

    const auto colors = internal::ResolveMenuColors(appearance_);
    const auto layout = MeasureLayout(raw->window, items, colors.style);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const SIZE desired{layout.width, layout.height};
    const auto bounds = root ? internal::PlaceRootMenu(anchor, desired, monitor.rcWork)
                             : internal::PlaceSubmenu(anchor, desired, monitor.rcWork);
    const int width = bounds.right - bounds.left;
    raw->contentHeight = layout.height;
    raw->rows.reserve(items.size());
    int top = layout.paddingY;
    for (const auto& item : items) {
        const int height = DipToPx(item.separator ? SeparatorHeightDip : RowHeightDip,
                                   paint::Dpi(raw->window));
        raw->rows.push_back({0, top, width, top + height});
        top += height;
    }

    if (!SetWindowPos(raw->window, HWND_TOPMOST, bounds.left, bounds.top, width,
                      bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        DestroyWindow(raw->window);
        levels_.pop_back();
        return false;
    }
    UpdateRegion(raw->window);
    int focusedChild{};
    internal::RegisterMenuAccessibility(raw->window, AccessibleItems(*raw, focusedChild));
    return true;
}

bool PopupController::Run() {
    if (!IsWindow(commandTarget_)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (!internal::ValidMenuItems(items_)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (activeController) {
        SetLastError(ERROR_BUSY);
        return false;
    }

    previousFocus_ = GetFocus();
    const auto subclassId = reinterpret_cast<UINT_PTR>(this);
    if (!SetWindowSubclass(commandTarget_, OwnerProc, subclassId,
                           reinterpret_cast<DWORD_PTR>(this)))
        return false;

    activeController = this;
    if (CreateLevel(items_, rootAnchor_, true)) {
        shown_ = true;
        SetFocus(levels_.front()->window);
        SetCapture(levels_.front()->window);
        if (GetCapture() != levels_.front()->window) Cancel();
    } else {
        Cancel();
    }

    MSG message{};
    MSG deferredMessage{};
    bool dispatchDeferred{};
    while (!done_) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            Cancel();
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (message.message == WM_POINTERDOWN || message.message == WM_POINTERUP) {
            const POINT point{GET_X_LPARAM(message.lParam), GET_Y_LPARAM(message.lParam)};
            if (!InsideChain(point)) {
                Cancel();
                deferredMessage = message;
                dispatchDeferred = true;
                break;
            }
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (!levels_.empty() && levels_.front()->window)
        KillTimer(levels_.front()->window, ChildTimer);
    const auto root = levels_.empty() ? nullptr : levels_.front()->window;
    const auto replacementCapture = GetCapture();
    if (replacementCapture == root) ReleaseCapture();
    CloseFrom(0);
    if (IsWindow(commandTarget_)) RemoveWindowSubclass(commandTarget_, OwnerProc, subclassId);
    if (replacementCapture && replacementCapture != root && IsWindow(replacementCapture) &&
        GetCapture() != replacementCapture)
        SetCapture(replacementCapture);
    if (IsWindow(previousFocus_)) SetFocus(previousFocus_);
    activeController = nullptr;
    if (dispatchDeferred) {
        TranslateMessage(&deferredMessage);
        DispatchMessageW(&deferredMessage);
    }
    return shown_;
}

void PopupController::Select(size_t level, int row) {
    if (level >= levels_.size()) return;
    auto& current = *levels_[level];
    if (row < 0 || row >= static_cast<int>(current.items->size())) return;
    const auto& item = (*current.items)[row];
    if (!item.enabled || item.separator) return;
    if (current.selected == row) {
        EnsureVisible(level, row);
        return;
    }
    current.selected = row;
    StopChildTimer();
    CloseFrom(level + 1);
    EnsureVisible(level, row);
    InvalidateRect(current.window, nullptr, FALSE);
    const auto focusedChild = UpdateAccessibility(level);
    if (focusedChild)
        internal::NotifyAccessibility(current.window, EVENT_OBJECT_FOCUS, focusedChild);
}

void PopupController::OpenChild(size_t level, int row, bool immediate) {
    if (level >= levels_.size() || row < 0 ||
        row >= static_cast<int>(levels_[level]->items->size()))
        return;
    const auto& item = (*levels_[level]->items)[row];
    if (!item.enabled || item.separator || item.children.empty()) return;
    if (level + 1 < levels_.size()) return;
    if (!immediate) {
        if (pendingLevel_ == level && pendingRow_ == row) return;
        pendingLevel_ = level;
        pendingRow_ = row;
        SetTimer(levels_.front()->window, ChildTimer, ChildDelayMs, nullptr);
        return;
    }

    StopChildTimer();
    CloseFrom(level + 1);
    RECT parentBounds{};
    GetWindowRect(levels_[level]->window, &parentBounds);
    auto rowBounds = levels_[level]->rows[row];
    OffsetRect(&rowBounds, parentBounds.left,
               parentBounds.top - levels_[level]->scrollOffset);
    CreateLevel(item.children, rowBounds, false);
}

void PopupController::CloseFrom(size_t level) {
    while (levels_.size() > level) {
        auto& current = levels_.back();
        if (current->window) DestroyWindow(current->window);
        levels_.pop_back();
    }
}

void PopupController::Activate(size_t level, int row) {
    if (level >= levels_.size() || row < 0 ||
        row >= static_cast<int>(levels_[level]->items->size()))
        return;
    const auto& item = (*levels_[level]->items)[row];
    if (!item.enabled || item.separator) return;
    if (!item.children.empty()) {
        Select(level, row);
        OpenChild(level, row, true);
        return;
    }
    command_ = item.id;
    done_ = true;
}

std::pair<size_t, int> PopupController::HitTest(POINT screen) const {
    for (size_t level = levels_.size(); level-- > 0;) {
        RECT bounds{};
        if (!levels_[level]->window || !GetWindowRect(levels_[level]->window, &bounds) ||
            !internal::WindowRegionContainsScreenPoint(levels_[level]->window, screen))
            continue;
        for (int row = 0; row < static_cast<int>(levels_[level]->rows.size()); ++row) {
            auto rowBounds = levels_[level]->rows[row];
            OffsetRect(&rowBounds, bounds.left,
                       bounds.top - levels_[level]->scrollOffset);
            if (Contains(rowBounds, screen)) return {level, row};
        }
        return {level, -1};
    }
    return {static_cast<size_t>(-1), -1};
}

bool PopupController::InsideChain(POINT screen) const {
    for (const auto& level : levels_) {
        if (level->window && internal::WindowRegionContainsScreenPoint(level->window, screen))
            return true;
    }
    return InsideHost(screen);
}

bool PopupController::InsideHost(POINT screen) const {
    if (levels_.size() != 1 || !menuBar_ || !IsWindow(menuBar_)) return false;
    RECT bounds{};
    return GetWindowRect(menuBar_, &bounds) && Contains(bounds, screen);
}

int PopupController::HostItem(POINT screen) const {
    if (!InsideHost(screen)) return -1;
    auto point = screen;
    const auto result = SendMessageW(menuBar_, internal::MenuBarNextItemMessage, 0,
                                     reinterpret_cast<LPARAM>(&point));
    return static_cast<int>(result);
}

bool PopupController::SwitchFromHost(POINT screen, bool release) {
    if (!InsideHost(screen)) return false;
    const auto index = HostItem(screen);
    if (index < 0) return true;
    if (index == topIndex_) {
        if (release) {
            nextTopIndex_ = -1;
            Cancel();
        }
        return true;
    }
    nextTopIndex_ = index;
    Cancel();
    return true;
}

size_t PopupController::LevelIndex(HWND window) const {
    for (size_t index = 0; index < levels_.size(); ++index)
        if (levels_[index]->window == window) return index;
    return static_cast<size_t>(-1);
}

void PopupController::MouseMove(HWND window, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(window, &point);
    if (SwitchFromHost(point, false)) return;
    const auto [level, row] = HitTest(point);
    if (level == static_cast<size_t>(-1) || row < 0) {
        StopChildTimer();
        return;
    }
    const auto& item = (*levels_[level]->items)[row];
    if (!item.enabled || item.separator) {
        StopChildTimer();
        return;
    }
    Select(level, row);
    if (!item.children.empty()) OpenChild(level, row, false);
    else StopChildTimer();
}

void PopupController::MouseUp(HWND window, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(window, &point);
    PointerUp(point);
}

void PopupController::DismissOutside(HWND window, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(window, &point);
    if (!InsideChain(point)) Cancel();
}

void PopupController::PointerUp(POINT point) {
    if (SwitchFromHost(point, true)) return;
    const auto [level, row] = HitTest(point);
    if (level != static_cast<size_t>(-1) && row >= 0) Activate(level, row);
    else if (!InsideChain(point)) Cancel();
}

void PopupController::MouseWheel(HWND window, WPARAM wParam, LPARAM lParam) {
    if (levels_.empty()) return;
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    auto [level, row] = HitTest(point);
    if (level == static_cast<size_t>(-1)) level = LevelIndex(window);
    if (level == static_cast<size_t>(-1)) level = levels_.size() - 1;

    auto& current = *levels_[level];
    current.wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
    const int steps = current.wheelRemainder / WHEEL_DELTA;
    current.wheelRemainder %= WHEEL_DELTA;
    if (!steps) return;
    RECT client{};
    GetClientRect(current.window, &client);
    const int maximum = (std::max)(0, current.contentHeight - static_cast<int>(client.bottom));
    const int rowHeight = DipToPx(RowHeightDip, paint::Dpi(current.window));
    const int offset = std::clamp(current.scrollOffset - steps * rowHeight * 3, 0, maximum);
    if (offset == current.scrollOffset) return;
    current.scrollOffset = offset;
    CloseFrom(level + 1);
    InvalidateRect(current.window, nullptr, FALSE);
    UpdateAccessibility(level);
}

void PopupController::KeyDown(WPARAM key) {
    if (levels_.empty()) return;
    const size_t level = levels_.size() - 1;
    auto& current = *levels_[level];
    switch (key) {
    case VK_UP:
    case VK_DOWN:
        Select(level, internal::NextMenuIndex(*current.items, current.selected,
                                              key == VK_UP ? -1 : 1));
        break;
    case VK_HOME:
    case VK_END:
        Select(level, internal::EdgeMenuIndex(*current.items, key == VK_END));
        break;
    case VK_RIGHT:
        if (!level && menuBar_ && IsWindow(menuBar_)) {
            const auto next = SendMessageW(menuBar_, internal::MenuBarNextItemMessage, 1, 0);
            if (next >= 0 && next != topIndex_) {
                nextTopIndex_ = static_cast<int>(next);
                Cancel();
            }
            break;
        }
        if (current.selected >= 0) OpenChild(level, current.selected, true);
        break;
    case VK_LEFT:
        if (level) {
            CloseFrom(level);
            const auto focusedChild = UpdateAccessibility(level - 1);
            if (focusedChild)
                internal::NotifyAccessibility(levels_[level - 1]->window,
                                                EVENT_OBJECT_FOCUS, focusedChild);
        } else if (menuBar_ && IsWindow(menuBar_)) {
            const auto previous = SendMessageW(menuBar_, internal::MenuBarNextItemMessage, -1, 0);
            if (previous >= 0 && previous != topIndex_) {
                nextTopIndex_ = static_cast<int>(previous);
                Cancel();
            }
        }
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (current.selected >= 0) Activate(level, current.selected);
        break;
    case VK_ESCAPE:
    case VK_MENU:
        Cancel();
        break;
    }
}

void PopupController::EnsureVisible(size_t level, int row) {
    if (level >= levels_.size() || row < 0 || row >= static_cast<int>(levels_[level]->rows.size()))
        return;
    auto& current = *levels_[level];
    RECT client{};
    GetClientRect(current.window, &client);
    int offset = current.scrollOffset;
    if (current.rows[row].top < offset) offset = current.rows[row].top;
    else if (current.rows[row].bottom > offset + client.bottom)
        offset = current.rows[row].bottom - client.bottom;
    offset = std::clamp(offset, 0,
                        (std::max)(0, current.contentHeight - static_cast<int>(client.bottom)));
    if (offset == current.scrollOffset) return;
    current.scrollOffset = offset;
    InvalidateRect(current.window, nullptr, FALSE);
}

void PopupController::StopChildTimer() {
    pendingLevel_ = static_cast<size_t>(-1);
    pendingRow_ = -1;
    if (!levels_.empty() && levels_.front()->window)
        KillTimer(levels_.front()->window, ChildTimer);
}

void PopupController::ChildTimerElapsed() {
    if (pendingLevel_ >= levels_.size() || pendingRow_ < 0) {
        StopChildTimer();
        return;
    }
    const auto level = pendingLevel_;
    const auto row = pendingRow_;
    if (levels_[level]->selected == row) OpenChild(level, row, true);
    else StopChildTimer();
}

void PopupController::WindowDestroyed(HWND window) {
    for (auto& level : levels_)
        if (level->window == window) level->window = nullptr;
}

int PopupController::UpdateAccessibility(size_t level) {
    if (level >= levels_.size() || !levels_[level]->window) return 0;
    int focusedChild{};
    auto items = AccessibleItems(*levels_[level], focusedChild);
    internal::UpdateMenuAccessibility(levels_[level]->window, std::move(items), focusedChild);
    return focusedChild;
}

void PopupController::UpdateRegion(HWND window) {
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) return;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const auto style = internal::ResolveMenuColors(appearance_).style;
    const int radius = (std::max)(
        0, (std::min)({DipToPx(style.cornerRadiusDip, paint::Dpi(window)), width / 2, height / 2}));
    const auto region = radius ? CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                                    radius * 2, radius * 2)
                               : nullptr;
    if (!SetWindowRgn(window, region, TRUE) && region) DeleteObject(region);
}

void DrawCheck(Gdiplus::Graphics& graphics, RECT bounds, Color color, float width) {
    Gdiplus::Pen pen(paint::GdiPlusColor(color), width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const float left = static_cast<float>(bounds.left);
    const float top = static_cast<float>(bounds.top);
    const float w = static_cast<float>(bounds.right - bounds.left);
    const float h = static_cast<float>(bounds.bottom - bounds.top);
    graphics.DrawLine(&pen, left + w * .20f, top + h * .52f,
                      left + w * .43f, top + h * .74f);
    graphics.DrawLine(&pen, left + w * .43f, top + h * .74f,
                      left + w * .82f, top + h * .28f);
}

void DrawChevron(Gdiplus::Graphics& graphics, RECT bounds, Color color, float width) {
    Gdiplus::Pen pen(paint::GdiPlusColor(color), width);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    const float x = (bounds.left + bounds.right) / 2.0f;
    const float y = (bounds.top + bounds.bottom) / 2.0f;
    const float size = (bounds.bottom - bounds.top) * .14f;
    graphics.DrawLine(&pen, x - size, y - size, x + size, y);
    graphics.DrawLine(&pen, x + size, y, x - size, y + size);
}

void PopupController::Paint(HWND window) {
    const auto levelIndex = LevelIndex(window);
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    if (levelIndex != static_cast<size_t>(-1)) {
        const auto colors = internal::ResolveMenuColors(appearance_);
        const auto& style = colors.style;
        const auto layout = MeasureLayout(window, *levels_[levelIndex]->items, style);
        if (paint::Buffer buffer(target, bounds); buffer) {
            paint::Clear(buffer.dc(), bounds, style.background);
            const auto dpi = paint::Dpi(window);
            Gdiplus::Graphics graphics(buffer.dc());
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            const Gdiplus::RectF surface{0, 0, static_cast<float>(bounds.right),
                                         static_cast<float>(bounds.bottom)};
            paint::Fill(graphics, surface, paint::ToPixels(style.cornerRadiusDip, dpi),
                        style.background);
            paint::Border(graphics, surface, paint::ToPixels(style.cornerRadiusDip, dpi),
                          style.border, paint::ToPixels(style.borderWidthDip, dpi));

            const auto font = paint::Font(style.font, dpi);
            const auto penWidth = (std::max)(1.5f, paint::ToPixels(style.borderWidthDip, dpi));
            for (int row = 0; row < static_cast<int>(levels_[levelIndex]->items->size()); ++row) {
                const auto& item = (*levels_[levelIndex]->items)[row];
                auto rowBounds = levels_[levelIndex]->rows[row];
                OffsetRect(&rowBounds, 0, -levels_[levelIndex]->scrollOffset);
                if (rowBounds.bottom <= bounds.top || rowBounds.top >= bounds.bottom) continue;
                if (item.separator) {
                    const int y = (rowBounds.top + rowBounds.bottom) / 2;
                    Gdiplus::Pen pen(paint::GdiPlusColor(style.border), penWidth);
                    graphics.DrawLine(&pen, static_cast<float>(layout.paddingX),
                                      static_cast<float>(y),
                                      static_cast<float>(bounds.right - layout.paddingX),
                                      static_cast<float>(y));
                    continue;
                }

                const bool selected = levels_[levelIndex]->selected == row;
                if (selected) {
                    const Gdiplus::RectF selection{
                        static_cast<float>(layout.paddingX / 2), static_cast<float>(rowBounds.top),
                        static_cast<float>(bounds.right - layout.paddingX),
                        static_cast<float>(rowBounds.bottom - rowBounds.top)};
                    paint::Fill(graphics, selection,
                                paint::ToPixels(style.cornerRadiusDip / 2, dpi), style.hover);
                }
                const auto textColor = !item.enabled ? style.disabledText
                                           : selected ? colors.selectedText : style.text;
                const auto mutedColor = !item.enabled ? style.disabledText
                                            : selected ? colors.selectedText : style.mutedText;
                int x = layout.paddingX;
                const RECT imageBounds{x, rowBounds.top, x + layout.imageWidth, rowBounds.bottom};
                if (item.checked) DrawCheck(graphics, imageBounds, textColor, penWidth);
                else if (item.image.kind != ImageSource::Kind::None)
                    paint::Image(buffer.dc(), item.image, imageBounds, textColor);
                x += layout.imageWidth + layout.spacing;
                const int right = (std::max)(x, static_cast<int>(bounds.right) - layout.paddingX);
                const int chevronBlock = layout.chevronWidth
                                               ? layout.spacing + layout.chevronWidth : 0;
                const int afterChevron = (std::max)(0, right - x - chevronBlock);
                const int shortcutWidth = layout.shortcutWidth
                                              ? (std::min)(layout.shortcutWidth, afterChevron / 2)
                                              : 0;
                const int shortcutBlock = shortcutWidth ? layout.spacing + shortcutWidth : 0;
                const int labelWidth = (std::min)(layout.labelWidth,
                                                  (std::max)(0, afterChevron - shortcutBlock));
                RECT labelBounds{x, rowBounds.top, x + labelWidth, rowBounds.bottom};
                paint::Text(buffer.dc(), item.text, labelBounds, font, textColor,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                x += labelWidth;
                if (shortcutWidth) {
                    x += layout.spacing;
                    RECT shortcutBounds{x, rowBounds.top, x + shortcutWidth, rowBounds.bottom};
                    paint::Text(buffer.dc(), item.shortcut, shortcutBounds, font, mutedColor,
                                DT_RIGHT | DT_VCENTER | DT_SINGLELINE |
                                    DT_END_ELLIPSIS | DT_NOPREFIX);
                    x += shortcutWidth;
                }
                if (layout.chevronWidth) {
                    x += layout.spacing;
                    if (!item.children.empty())
                        DrawChevron(graphics, {x, rowBounds.top, x + layout.chevronWidth,
                                               rowBounds.bottom}, textColor, penWidth);
                }
            }
        }
    }
    EndPaint(window, &ps);
}

LRESULT PopupProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* controller = activeController;
    switch (message) {
    case WM_MOUSEMOVE:
        if (controller) controller->MouseMove(window, lParam);
        return 0;
    case WM_LBUTTONUP:
        if (controller) controller->MouseUp(window, lParam);
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (controller) controller->DismissOutside(window, lParam);
        return 0;
    case WM_POINTERUP:
        if (controller) {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window, &point);
            controller->PointerUp(point);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (controller) controller->MouseWheel(window, wParam, lParam);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (controller) controller->KeyDown(wParam);
        return 0;
    case internal::MenuActivateAccessibleMessage:
        if (controller)
            controller->Activate(controller->LevelIndex(window), static_cast<int>(wParam));
        return 0;
    case WM_TIMER:
        if (controller && wParam == ChildTimer) controller->ChildTimerElapsed();
        return 0;
    case WM_CAPTURECHANGED:
        if (controller && !controller->done_ &&
            reinterpret_cast<HWND>(lParam) != window)
            controller->Cancel();
        return 0;
    case WM_ACTIVATEAPP:
    case WM_ENABLE:
        if (controller && !controller->done_ && !wParam) controller->Cancel();
        return 0;
    case WM_PAINT:
        if (controller) controller->Paint(window);
        else ValidateRect(window, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (controller) controller->UpdateRegion(window);
        return 0;
    case WM_DPICHANGED:
        if (controller) controller->Cancel();
        return 0;
    case WM_GETOBJECT:
        if (controller) {
            LRESULT result{};
            if (internal::HandleAccessibilityMessage(window, message, wParam, lParam, result))
                return result;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_NCDESTROY:
        internal::DestroyAccessibility(window);
        if (controller) controller->WindowDestroyed(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK OwnerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                           UINT_PTR, DWORD_PTR reference) {
    auto* controller = reinterpret_cast<PopupController*>(reference);
    if (controller && ((message == WM_ENABLE && !wParam) || message == WM_NCDESTROY)) {
        controller->Cancel();
        if (message == WM_NCDESTROY) controller->commandTarget_ = nullptr;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

bool ShowContextMenu(HWND owner, POINT position, const ContextMenuOptions& options) {
    if (!owner || !IsWindow(owner)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    if (!internal::ValidMenuItems(options.items)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    const RECT anchor{position.x, position.y, position.x, position.y};
    return internal::ShowPopupMenu(owner, nullptr, anchor, options.items, options.appearance);
}

namespace internal {

MenuColors ResolveMenuColors(const StyleOverride& appearance) {
    const auto theme = GetTheme();
    auto local = appearance;
    if (!local.background) local.background = theme.palette.panel;
    MenuColors colors{ResolveStyle(theme, local), {}};
    colors.selectedText = colors.style.text;

    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) {
        colors.style.background = SystemColor(COLOR_MENU);
        colors.style.text = SystemColor(COLOR_MENUTEXT);
        colors.style.mutedText = colors.style.text;
        colors.style.hover = SystemColor(COLOR_HIGHLIGHT);
        colors.style.selected = colors.style.hover;
        colors.style.border = SystemColor(COLOR_WINDOWFRAME);
        colors.style.disabledText = SystemColor(COLOR_GRAYTEXT);
        colors.selectedText = SystemColor(COLOR_HIGHLIGHTTEXT);
    }
    return colors;
}

bool RegisterMenuClass() {
    BOOL dropShadow{};
    const UINT style = SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &dropShadow, 0) && dropShadow
                           ? CS_DROPSHADOW : 0;
    return RegisterControlClass(PopupClass, SafeWindowProc<PopupProcImpl>, style);
}

static PopupMenuResult RunPopup(HWND commandTarget, HWND source, RECT anchor,
                                std::vector<MenuItem> items, const StyleOverride& appearance,
                                HWND menuBar, int topIndex) {
    PopupController controller(commandTarget, source, anchor, std::move(items), appearance,
                               menuBar, topIndex);
    const bool shown = controller.Run();
    const auto target = controller.CommandTarget();
    if (shown && controller.Command() && target && IsWindow(target))
        SendMessageW(target, WM_COMMAND, MAKEWPARAM(controller.Command(), 0),
                     reinterpret_cast<LPARAM>(source));
    return {shown, controller.NextTopIndex()};
}

bool ShowPopupMenu(HWND commandTarget, HWND source, RECT anchor,
                   std::vector<MenuItem> items, const StyleOverride& appearance) {
    return RunPopup(commandTarget, source, anchor, std::move(items), appearance, nullptr, -1).shown;
}

PopupMenuResult ShowMenuBarPopup(HWND commandTarget, HWND source, RECT anchor,
                                 std::vector<MenuItem> items, const StyleOverride& appearance,
                                 HWND menuBar, int topIndex) {
    return RunPopup(commandTarget, source, anchor, std::move(items), appearance, menuBar, topIndex);
}

void CancelPopupMenu(HWND source) {
    if (activeController && activeController->Source() == source) activeController->Cancel();
}

bool IsPopupMenuWindow(HWND source, HWND window) {
    return activeController && activeController->Source() == source &&
           activeController->OwnsWindow(window);
}

} // namespace internal
} // namespace wcw
