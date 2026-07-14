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

struct Colors {
    ResolvedStyle style;
    Color selectedText;
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

Colors MenuColors(const StyleOverride& appearance) {
    const auto theme = GetTheme();
    auto local = appearance;
    if (!local.background) local.background = theme.palette.panel;
    Colors colors{ResolveStyle(theme, local), {}};
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

class PopupController {
public:
    PopupController(HWND commandTarget, HWND source, RECT anchor,
                    std::vector<MenuItem> items, StyleOverride appearance)
        : commandTarget_(commandTarget), source_(source), rootAnchor_(anchor),
          items_(std::move(items)), appearance_(std::move(appearance)) {}

    bool Run();
    int Command() const { return command_; }
    HWND Source() const { return source_; }
    void Select(size_t level, int row);
    void OpenChild(size_t level, int row, bool immediate);
    void CloseFrom(size_t level);
    void Activate(size_t level, int row);
    void Cancel() { done_ = true; }

private:
    bool CreateLevel(const std::vector<MenuItem>& items, RECT anchor, bool root);
    std::pair<size_t, int> HitTest(POINT screen) const;
    bool InsideChain(POINT screen) const;
    size_t LevelIndex(HWND window) const;
    void MouseMove(HWND window, LPARAM lParam);
    void MouseUp(HWND window, LPARAM lParam);
    void KeyDown(WPARAM key);
    void StopChildTimer();
    void ChildTimerElapsed();
    void WindowDestroyed(HWND window);
    void Paint(HWND window);

    friend LRESULT PopupProcImpl(HWND, UINT, WPARAM, LPARAM);
    friend LRESULT CALLBACK OwnerProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    HWND commandTarget_{};
    HWND source_{};
    HWND previousFocus_{};
    RECT rootAnchor_{};
    std::vector<MenuItem> items_;
    StyleOverride appearance_;
    std::vector<std::unique_ptr<PopupLevel>> levels_;
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

    const auto colors = MenuColors(appearance_);
    const auto layout = MeasureLayout(raw->window, items, colors.style);
    raw->rows.reserve(items.size());
    int top = layout.paddingY;
    for (const auto& item : items) {
        const int height = DipToPx(item.separator ? SeparatorHeightDip : RowHeightDip,
                                   paint::Dpi(raw->window));
        raw->rows.push_back({0, top, layout.width, top + height});
        top += height;
    }

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const SIZE size{layout.width, layout.height};
    const auto bounds = root ? internal::PlaceRootMenu(anchor, size, monitor.rcWork)
                             : internal::PlaceSubmenu(anchor, size, monitor.rcWork);
    SetWindowPos(raw->window, HWND_TOPMOST, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
        if (GetCapture() != levels_.front()->window) done_ = true;
    } else {
        done_ = true;
    }

    MSG message{};
    while (!done_) {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            done_ = true;
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (!levels_.empty() && levels_.front()->window)
        KillTimer(levels_.front()->window, ChildTimer);
    CloseFrom(0);
    if (IsWindow(commandTarget_)) RemoveWindowSubclass(commandTarget_, OwnerProc, subclassId);
    if (GetCapture()) ReleaseCapture();
    if (IsWindow(previousFocus_)) SetFocus(previousFocus_);
    activeController = nullptr;
    return shown_;
}

void PopupController::Select(size_t level, int row) {
    if (level >= levels_.size()) return;
    auto& current = *levels_[level];
    if (row < 0 || row >= static_cast<int>(current.items->size())) return;
    const auto& item = (*current.items)[row];
    if (!item.enabled || item.separator) return;
    if (current.selected == row) return;
    current.selected = row;
    StopChildTimer();
    CloseFrom(level + 1);
    InvalidateRect(current.window, nullptr, FALSE);
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
    OffsetRect(&rowBounds, parentBounds.left, parentBounds.top);
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
            !Contains(bounds, screen))
            continue;
        for (int row = 0; row < static_cast<int>(levels_[level]->rows.size()); ++row) {
            auto rowBounds = levels_[level]->rows[row];
            OffsetRect(&rowBounds, bounds.left, bounds.top);
            if (Contains(rowBounds, screen)) return {level, row};
        }
        return {level, -1};
    }
    return {static_cast<size_t>(-1), -1};
}

bool PopupController::InsideChain(POINT screen) const {
    for (const auto& level : levels_) {
        RECT bounds{};
        if (level->window && GetWindowRect(level->window, &bounds) && Contains(bounds, screen))
            return true;
    }
    return false;
}

size_t PopupController::LevelIndex(HWND window) const {
    for (size_t index = 0; index < levels_.size(); ++index)
        if (levels_[index]->window == window) return index;
    return static_cast<size_t>(-1);
}

void PopupController::MouseMove(HWND window, LPARAM lParam) {
    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    ClientToScreen(window, &point);
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
    const auto [level, row] = HitTest(point);
    if (level != static_cast<size_t>(-1) && row >= 0) Activate(level, row);
    else if (!InsideChain(point)) Cancel();
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
        if (current.selected >= 0) OpenChild(level, current.selected, true);
        break;
    case VK_LEFT:
        if (level) CloseFrom(level);
        break;
    case VK_RETURN:
    case VK_SPACE:
        if (current.selected >= 0) Activate(level, current.selected);
        break;
    case VK_ESCAPE:
        Cancel();
        break;
    }
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
        const auto colors = MenuColors(appearance_);
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
                const auto rowBounds = levels_[levelIndex]->rows[row];
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
                RECT labelBounds{x, rowBounds.top, x + layout.labelWidth, rowBounds.bottom};
                paint::Text(buffer.dc(), item.text, labelBounds, font, textColor,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                x += layout.labelWidth;
                if (layout.shortcutWidth) {
                    x += layout.spacing;
                    RECT shortcutBounds{x, rowBounds.top, x + layout.shortcutWidth, rowBounds.bottom};
                    paint::Text(buffer.dc(), item.shortcut, shortcutBounds, font, mutedColor,
                                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    x += layout.shortcutWidth;
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
    case WM_KEYDOWN:
        if (controller) controller->KeyDown(wParam);
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
        if (controller && !wParam) controller->Cancel();
        return 0;
    case WM_PAINT:
        if (controller) controller->Paint(window);
        else ValidateRect(window, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETOBJECT:
        if (controller) {
            LRESULT result{};
            if (internal::HandleAccessibilityMessage(window, message, wParam, lParam, result))
                return result;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_NCDESTROY:
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

bool RegisterMenuClass() {
    BOOL dropShadow{};
    const UINT style = SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &dropShadow, 0) && dropShadow
                           ? CS_DROPSHADOW : 0;
    return RegisterControlClass(PopupClass, SafeWindowProc<PopupProcImpl>, style);
}

bool ShowPopupMenu(HWND commandTarget, HWND source, RECT anchor,
                   std::vector<MenuItem> items, const StyleOverride& appearance) {
    PopupController controller(commandTarget, source, anchor, std::move(items), appearance);
    const bool shown = controller.Run();
    if (shown && controller.Command() && IsWindow(commandTarget))
        SendMessageW(commandTarget, WM_COMMAND, MAKEWPARAM(controller.Command(), 0),
                     reinterpret_cast<LPARAM>(source));
    return shown;
}

void CancelPopupMenu(HWND source) {
    if (activeController && activeController->Source() == source) activeController->Cancel();
}

} // namespace internal
} // namespace wcw
