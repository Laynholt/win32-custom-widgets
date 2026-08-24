#include <wcw/Controls.h>
#include <wcw/Runtime.h>
#include <wcw/Theme.h>

#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t WindowClass[] = L"WcwGalleryWindow";
constexpr DWORD MainStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr DWORD MainExStyle = WS_EX_CONTROLPARENT;
constexpr float InitialClientWidthDip = 960.0f;
constexpr float InitialClientHeightDip = 620.0f;
constexpr float MinimumClientWidthDip = 840.0f;
constexpr float MinimumClientHeightDip = 560.0f;
constexpr float MenuBarHeightDip = 32.0f;
constexpr float DefaultRadiusDip = 8.0f;
enum Id {
    Dark = 100,
    Light,
    Accent,
    Disabled,
    Radius,
    Checkbox,
    Toggle,
    Slider,
    Combo,
    Numeric,
};
enum : int {
    MenuOpen = 3001,
    MenuExit,
    MenuUndo,
    MenuUnavailable,
    MenuDark,
    MenuLight,
    MenuResetRadius,
    MenuAbout,
};

struct Gallery {
    HWND window{}, menuBar{}, dark{}, light{}, accent{}, disabled{}, iconButton{}, menuButton{}, label{},
        informationIcon{}, warningIcon{}, errorIcon{}, separator{}, panel{}, text{}, numeric{},
        checkbox{}, toggle{}, slider{}, radius{}, progress{}, activity{}, combo{}, scroll{}, status{};
    float radiusDip{DefaultRadiusDip};
    bool lightTheme{};
    std::vector<HWND> rounded;
    std::vector<std::pair<HWND, wcw::RectDip>> nested;
};

int Px(HWND window, float dip) {
    return static_cast<int>(std::lround(dip * GetDpiForWindow(window) / 96.0f));
}

SIZE OuterSizeForClient(float widthDip, float heightDip, UINT dpi) {
    RECT bounds{0, 0, static_cast<LONG>(std::lround(widthDip * dpi / 96.0f)),
                static_cast<LONG>(std::lround(heightDip * dpi / 96.0f))};
    AdjustWindowRectExForDpi(&bounds, MainStyle, FALSE, MainExStyle, dpi);
    return {bounds.right - bounds.left, bounds.bottom - bounds.top};
}

void MoveWindowDip(HWND window, float x, float y, float width, float height) {
    const auto parent = GetParent(window);
    MoveWindow(window, Px(parent, x), Px(parent, y), Px(parent, width), Px(parent, height), TRUE);
}

void ApplyDarkTitleBar(HWND window) {
    const BOOL dark = TRUE;
    const COLORREF black = RGB(0, 0, 0);
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &black, sizeof(black));
}

wcw::ControlOptions Base(HWND parent, int id, std::wstring text, std::wstring name = {}) {
    wcw::ControlOptions options;
    options.parent = parent;
    options.id = id;
    options.bounds = {0, 0, 120, 36};
    options.style = WS_VISIBLE;
    options.text = std::move(text);
    options.accessibleName = name.empty() ? options.text : std::move(name);
    return options;
}

void SetStatus(Gallery& gallery, const std::wstring& text) {
    SetWindowTextW(gallery.status, text.c_str());
}

std::vector<wcw::MenuItem> ApplicationMenu(bool lightTheme) {
    return {
        {.text = L"&File", .children = {
            {.id = MenuOpen, .text = L"&Open", .shortcut = L"Ctrl+O",
             .image = wcw::BuiltinIcon::Information},
            {.separator = true},
            {.id = MenuExit, .text = L"E&xit"},
        }},
        {.text = L"&Edit", .children = {
            {.id = MenuUndo, .text = L"&Undo", .shortcut = L"Ctrl+Z"},
            {.id = MenuUnavailable, .text = L"Unavailable", .enabled = false},
        }},
        {.text = L"&View", .children = {
            {.id = MenuDark, .text = L"&Dark theme", .checked = !lightTheme},
            {.id = MenuLight, .text = L"&Light theme", .checked = lightTheme},
            {.text = L"More", .children = {
                {.id = MenuResetRadius, .text = L"Reset corner radius"},
            }},
        }},
        {.text = L"&Help", .children = {
            {.id = MenuAbout, .text = L"&About"},
        }},
    };
}

std::vector<wcw::MenuItem> GalleryMenu() {
    return {
        {.id = 2001, .text = L"Information", .shortcut = L"Ctrl+I",
         .image = wcw::BuiltinIcon::Information},
        {.id = 2002, .text = L"Warning", .image = wcw::BuiltinIcon::Warning, .checked = true},
        {.separator = true},
        {.text = L"More actions",
         .children = {
             {.id = 2003, .text = L"Report error", .image = wcw::BuiltinIcon::Error},
             {.id = 2004, .text = L"Unavailable", .enabled = false},
         }},
    };
}

void ApplyAppearance(Gallery& gallery) {
    auto theme = gallery.lightTheme ? wcw::LightTheme() : wcw::DarkTheme();
    theme.metrics.cornerRadiusDip = gallery.radiusDip;
    wcw::SetTheme(theme);

    wcw::StyleOverride rounded;
    rounded.cornerRadiusDip = gallery.radiusDip;
    for (const auto control : gallery.rounded) wcw::SetStyleOverride(control, rounded);

    rounded.background = theme.palette.accent;
    rounded.foreground = wcw::Color::FromRgb(0xFF, 0xFF, 0xFF);
    wcw::SetStyleOverride(gallery.accent, rounded);
    InvalidateRect(gallery.window, nullptr, TRUE);
    SetStatus(gallery, std::format(L"{} theme, corner radius {:.0f} DIP",
                                   gallery.lightTheme ? L"Light" : L"Dark", gallery.radiusDip));
}

void Layout(Gallery& g) {
    RECT client{};
    GetClientRect(g.window, &client);
    const float width = client.right * 96.0f / GetDpiForWindow(g.window);
    const float height = client.bottom * 96.0f / GetDpiForWindow(g.window);
    const float margin = 20, gap = 10;
    const bool wide = width >= 820;
    const float column = wide ? (width - margin * 2 - 24) / 2 : width - margin * 2;
    const float right = wide ? margin + column + 24 : margin;
    const float contentOffset = MenuBarHeightDip;

    MoveWindowDip(g.menuBar, 0, 0, width, MenuBarHeightDip);
    MoveWindowDip(g.label, margin, 16 + contentOffset, column, 28);
    MoveWindowDip(g.dark, margin, 52 + contentOffset, 100, 36);
    MoveWindowDip(g.light, margin + 110, 52 + contentOffset, 100, 36);
    MoveWindowDip(g.radius, margin + 220, 52 + contentOffset, (std::max)(80.0f, column - 220), 36);
    MoveWindowDip(g.accent, margin, 100 + contentOffset, 150, 36);
    MoveWindowDip(g.disabled, margin + 160, 100 + contentOffset, 150, 36);
    MoveWindowDip(g.iconButton, margin + 320, 100 + contentOffset, 52, 36);
    MoveWindowDip(g.separator, margin, 148 + contentOffset, column, 1);
    MoveWindowDip(g.text, margin, 162 + contentOffset, column, 38);
    MoveWindowDip(g.numeric, margin, 210 + contentOffset, column, 38);
    MoveWindowDip(g.checkbox, margin, 258 + contentOffset, column * .48f, 36);
    MoveWindowDip(g.toggle, margin + column * .52f, 258 + contentOffset, column * .48f, 36);
    MoveWindowDip(g.slider, margin, 304 + contentOffset, column, 36);
    MoveWindowDip(g.progress, margin, 350 + contentOffset, column, 18);
    MoveWindowDip(g.activity, margin, 380 + contentOffset, column, 18);
    MoveWindowDip(g.combo, margin, 410 + contentOffset, column, 38);

    const float headerTop = (wide ? 16 : 462) + contentOffset;
    const float scrollTop = (wide ? 58 : 510) + contentOffset;
    MoveWindowDip(g.panel, right, headerTop, column - 224, 28);
    MoveWindowDip(g.informationIcon, right + column - 214, headerTop + 2, 24, 24);
    MoveWindowDip(g.warningIcon, right + column - 184, headerTop + 2, 24, 24);
    MoveWindowDip(g.errorIcon, right + column - 154, headerTop + 2, 24, 24);
    MoveWindowDip(g.menuButton, right + column - 120, headerTop - 4, 120, 36);
    MoveWindowDip(g.scroll, right, scrollTop, column,
                  (std::max)(120.0f, height - scrollTop - 62));
    MoveWindowDip(g.status, margin, height - 40, width - margin * 2, 28);
    for (const auto& [window, bounds] : g.nested)
        MoveWindowDip(window, bounds.x, bounds.y, bounds.width, bounds.height);
}

bool CreateGallery(Gallery& g) {
    wcw::MenuBarOptions menuBar{Base(g.window, 0, L"", L"Application menu")};
    menuBar.bounds = {0, 0, InitialClientWidthDip, MenuBarHeightDip};
    menuBar.items = ApplicationMenu(g.lightTheme);
    g.menuBar = wcw::CreateMenuBar(menuBar);

    wcw::ButtonOptions button{Base(g.window, Dark, L"Dark")};
    g.dark = wcw::CreateButton(button);
    static_cast<wcw::ControlOptions&>(button) = Base(g.window, Light, L"Light");
    g.light = wcw::CreateButton(button);

    wcw::ButtonOptions accent{Base(g.window, Accent, L"Accent override")};
    accent.appearance.background = wcw::Color::FromRgb(0x7A, 0x48, 0xE8);
    accent.appearance.foreground = wcw::Color::FromRgb(0xFF, 0xFF, 0xFF);
    accent.appearance.cornerRadiusDip = 14.0f;
    g.accent = wcw::CreateButton(accent);

    wcw::ButtonOptions disabled{Base(g.window, Disabled, L"Disabled button")};
    g.disabled = wcw::CreateButton(disabled);
    EnableWindow(g.disabled, FALSE);

    wcw::ButtonOptions icon{Base(g.window, 0, L"", L"Information")};
    icon.icon = LoadIconW(nullptr, IDI_INFORMATION);
    g.iconButton = wcw::CreateButton(icon);

    wcw::MenuButtonOptions menuButton{Base(g.window, 0, L"Open menu")};
    menuButton.items = GalleryMenu();
    g.menuButton = wcw::CreateMenuButton(menuButton);

    g.label = wcw::CreateLabel(Base(g.window, 0, L"Win32 Custom Widgets gallery"));
    g.separator = wcw::CreateSeparator(Base(g.window, 0, L""));
    g.panel = wcw::CreatePanel(Base(g.window, 0, L"Widget gallery and scroll view"));
    auto semanticIcon = Base(g.window, 0, L"", L"Information icon");
    semanticIcon.appearance.foreground = wcw::Color::FromRgb(0x00, 0x78, 0xD4);
    g.informationIcon = wcw::CreateImageView(semanticIcon, wcw::BuiltinIcon::Information);
    semanticIcon.accessibleName = L"Warning icon";
    semanticIcon.appearance.foreground = wcw::Color::FromRgb(0xF7, 0xA8, 0x00);
    g.warningIcon = wcw::CreateImageView(semanticIcon, wcw::BuiltinIcon::Warning);
    semanticIcon.accessibleName = L"Error icon";
    semanticIcon.appearance.foreground = wcw::Color::FromRgb(0xD1, 0x34, 0x38);
    g.errorIcon = wcw::CreateImageView(semanticIcon, wcw::BuiltinIcon::Error);

    wcw::TextBoxOptions text{Base(g.window, 0, L"", L"Text input with error")};
    text.placeholder = L"Validation error example";
    g.text = wcw::CreateTextBox(text);
    wcw::SetValidationError(g.text, true);

    wcw::NumericBoxOptions numeric{{Base(g.window, Numeric, L"", L"Numeric input")}};
    numeric.placeholder = L"Number";
    numeric.minimum = -10;
    numeric.maximum = 100;
    numeric.value = 25;
    g.numeric = wcw::CreateNumericBox(numeric);

    wcw::CheckableOptions check{Base(g.window, Checkbox, L"Checkbox")};
    check.checked = true;
    g.checkbox = wcw::CreateCheckbox(check);
    static_cast<wcw::ControlOptions&>(check) = Base(g.window, Toggle, L"Toggle");
    g.toggle = wcw::CreateToggle(check);

    wcw::SliderOptions slider{Base(g.window, Slider, L"", L"Value slider")};
    slider.value = 40;
    slider.trackAppearance.background = wcw::Color::FromRgb(0x39, 0x39, 0x42);
    slider.trackAppearance.cornerRadiusDip = 2.0f;
    slider.thumbAppearance.background = wcw::Color::FromRgb(0xE8, 0x48, 0x55);
    slider.thumbAppearance.cornerRadiusDip = 12.0f;
    g.slider = wcw::CreateSlider(slider);
    slider.id = Radius;
    slider.accessibleName = L"Corner radius, 0 to 24 DIP";
    slider.maximum = 24;
    slider.value = g.radiusDip;
    g.radius = wcw::CreateSlider(slider);

    wcw::ProgressBarOptions progress{Base(g.window, 0, L"", L"Progress 65 percent")};
    progress.value = 65;
    g.progress = wcw::CreateProgressBar(progress);
    progress.accessibleName = L"Indeterminate progress";
    progress.indeterminate = true;
    g.activity = wcw::CreateProgressBar(progress);

    wcw::ComboBoxOptions combo{Base(g.window, Combo, L"", L"Icon and text choices")};
    combo.items = {{L"Information", 1, wcw::ImageSource(LoadIconW(nullptr, IDI_INFORMATION))},
                   {L"Warning", 2, wcw::ImageSource(LoadIconW(nullptr, IDI_WARNING))},
                   {L"Text only", 3, {}}};
    combo.selectedIndex = 0;
    g.combo = wcw::CreateComboBox(combo);

    wcw::ScrollViewOptions scroll{Base(g.window, 0, L"", L"Scrollable widget examples")};
    scroll.contentExtent = {620, 520};
    scroll.trackAppearance.background = wcw::Color::FromRgb(0x24, 0x24, 0x29);
    scroll.trackAppearance.cornerRadiusDip = 4.0f;
    scroll.thumbAppearance.background = wcw::Color::FromRgb(0x88, 0x62, 0xEE);
    scroll.thumbAppearance.cornerRadiusDip = 8.0f;
    g.scroll = wcw::CreateScrollView(scroll);
    const auto content = wcw::GetScrollContentWindow(g.scroll);
    if (!content) return false;
    auto nested = Base(content, 0, L"Nested controls remain keyboard accessible");
    nested.bounds = {18, 18, 360, 30};
    g.nested.emplace_back(wcw::CreateLabel(nested), nested.bounds);
    wcw::ButtonOptions nestedButton{Base(content, 0, L"Nested button")};
    nestedButton.bounds = {18, 62, 180, 36};
    g.nested.emplace_back(wcw::CreateButton(nestedButton), nestedButton.bounds);
    wcw::TextBoxOptions nestedText{Base(content, 0, L"", L"Nested text input")};
    nestedText.bounds = {18, 112, 300, 38};
    nestedText.placeholder = L"Tab into the scroll content";
    g.nested.emplace_back(wcw::CreateTextBox(nestedText), nestedText.bounds);
    for (int row = 0; row < 6; ++row) {
        auto item = Base(content, 0, std::format(L"Scrollable row {}", row + 1));
        item.bounds = {18, 174.0f + row * 52.0f, 360, 36};
        g.nested.emplace_back(wcw::CreateLabel(item), item.bounds);
    }

    g.status = wcw::CreateLabel(Base(g.window, 0, L"Ready", L"Notification status"));
    wcw::TooltipOptions tooltip;
    tooltip.text = L"Tooltips are custom drawn and follow the active theme.";
    tooltip.appearance.cornerRadiusDip = 8.0f;
    const bool iconTooltip = wcw::AttachTooltip(g.iconButton, tooltip);
    tooltip.text = L"Drag or use arrow, Page Up, Home, and End keys.";
    const bool sliderTooltip = wcw::AttachTooltip(g.slider, tooltip);

    g.rounded = {g.dark, g.light, g.text, g.numeric, g.checkbox, g.toggle, g.combo};
    return std::ranges::all_of(g.rounded, [](HWND window) { return window != nullptr; }) &&
           g.menuBar && g.accent && g.disabled && g.iconButton && g.menuButton && g.label &&
           g.informationIcon && g.warningIcon && g.errorIcon && g.separator && g.panel && g.slider &&
           g.radius && g.progress && g.activity && g.scroll && g.status &&
           std::ranges::all_of(g.nested, [](const auto& item) { return item.first != nullptr; }) &&
           iconTooltip && sliderTooltip;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* gallery = reinterpret_cast<Gallery*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        gallery = static_cast<Gallery*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        gallery->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(gallery));
    }
    switch (message) {
    case WM_CREATE:
        if (!CreateGallery(*gallery)) return -1;
        ApplyAppearance(*gallery);
        return 0;
    case WM_GETMINMAXINFO: {
        const auto minimum = OuterSizeForClient(MinimumClientWidthDip, MinimumClientHeightDip,
                                                GetDpiForWindow(window));
        const auto info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {minimum.cx, minimum.cy};
        return 0;
    }
    case WM_SIZE:
        if (gallery && gallery->status) Layout(*gallery);
        return 0;
    case WM_DPICHANGED:
        if (const auto suggested = reinterpret_cast<const RECT*>(lParam))
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
        Layout(*gallery);
        return 0;
    case WM_CONTEXTMENU: {
        POINT position{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (position.x == -1 && position.y == -1) {
            RECT bounds{};
            GetClientRect(window, &bounds);
            position = {(bounds.left + bounds.right) / 2, (bounds.top + bounds.bottom) / 2};
            ClientToScreen(window, &position);
        }
        wcw::ContextMenuOptions menu{GalleryMenu()};
        wcw::ShowContextMenu(window, position, menu);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case MenuOpen:
            if (gallery) SetStatus(*gallery, L"Open selected");
            return 0;
        case MenuExit:
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        case MenuUndo:
            if (gallery) SetStatus(*gallery, L"Undo selected");
            return 0;
        case MenuUnavailable:
            if (gallery) SetStatus(*gallery, L"Unavailable selected");
            return 0;
        case MenuDark:
            if (gallery) {
                gallery->lightTheme = false;
                wcw::SetMenuBarItems(gallery->menuBar, ApplicationMenu(gallery->lightTheme));
                ApplyAppearance(*gallery);
            }
            return 0;
        case MenuLight:
            if (gallery) {
                gallery->lightTheme = true;
                wcw::SetMenuBarItems(gallery->menuBar, ApplicationMenu(gallery->lightTheme));
                ApplyAppearance(*gallery);
            }
            return 0;
        case MenuResetRadius:
            if (gallery) {
                gallery->radiusDip = DefaultRadiusDip;
                wcw::SetSliderValue(gallery->radius, gallery->radiusDip);
                ApplyAppearance(*gallery);
            }
            return 0;
        case MenuAbout:
            if (gallery) SetStatus(*gallery, L"About selected");
            return 0;
        case 2001: SetStatus(*gallery, L"Information selected"); return 0;
        case 2002: SetStatus(*gallery, L"Warning selected"); return 0;
        case 2003: SetStatus(*gallery, L"Report error selected"); return 0;
        case 2004: SetStatus(*gallery, L"Unavailable selected"); return 0;
        }
        if (HIWORD(wParam) == BN_CLICKED && gallery) {
            if (LOWORD(wParam) == Dark) gallery->lightTheme = false;
            else if (LOWORD(wParam) == Light) gallery->lightTheme = true;
            else {
                SetStatus(*gallery, std::format(L"Button {} clicked", LOWORD(wParam)));
                return 0;
            }
            wcw::SetMenuBarItems(gallery->menuBar, ApplicationMenu(gallery->lightTheme));
            ApplyAppearance(*gallery);
        }
        return 0;
    case WM_NOTIFY:
        if (gallery) {
            const auto header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->code == wcw::WCN_VALUE_CHANGED) {
                const auto note = reinterpret_cast<const wcw::ValueChangedNotification*>(lParam);
                if (header->idFrom == Radius) {
                    gallery->radiusDip = static_cast<float>(note->value);
                    ApplyAppearance(*gallery);
                } else SetStatus(*gallery, std::format(L"Value changed: {:.2f}", note->value));
            } else if (header->code == wcw::WCN_CHECK_CHANGED) {
                const auto note = reinterpret_cast<const wcw::CheckChangedNotification*>(lParam);
                SetStatus(*gallery, note->checked ? L"Checked" : L"Unchecked");
            } else if (header->code == wcw::WCN_SELECTION_CHANGED) {
                const auto note = reinterpret_cast<const wcw::SelectionChangedNotification*>(lParam);
                SetStatus(*gallery, std::format(L"Combo selection {} (item id {})",
                                                note->newIndex, note->newId));
            }
        }
        return 0;
    case WM_ERASEBKGND: {
        const auto color = wcw::GetTheme().palette.window;
        const auto brush = CreateSolidBrush(RGB(color.r, color.g, color.b));
        RECT bounds{};
        GetClientRect(window, &bounds);
        FillRect(reinterpret_cast<HDC>(wParam), &bounds, brush);
        DeleteObject(brush);
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!wcw::Initialize(instance)) return 1;

    WNDCLASSEXW cls{sizeof(cls)};
    cls.hInstance = instance;
    cls.lpfnWndProc = WindowProc;
    cls.lpszClassName = WindowClass;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&cls)) {
        wcw::Shutdown();
        return 2;
    }

    Gallery gallery;
    const auto window = CreateWindowExW(
        MainExStyle, WindowClass, L"Win32 Custom Widgets", MainStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, &gallery);
    if (!window) {
        wcw::Shutdown();
        return 3;
    }
    ApplyDarkTitleBar(window);
    const auto initial = OuterSizeForClient(InitialClientWidthDip, InitialClientHeightDip,
                                            GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 0, 0, initial.cx, initial.cy,
                 SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    Layout(gallery);
    ShowWindow(window, show);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    wcw::Shutdown();
    return static_cast<int>(message.wParam);
}
