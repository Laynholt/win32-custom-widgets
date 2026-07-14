#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <algorithm>
#include <array>

namespace {

int clicks;
int dialogClicks;
int dialogCommand;

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) ++clicks;
    return DefWindowProcW(window, message, wParam, lParam);
}

INT_PTR CALLBACK DialogProc(HWND, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        dialogCommand = LOWORD(wParam);
        if (HIWORD(wParam) == BN_CLICKED && lParam) ++dialogClicks;
    }
    return FALSE;
}

HWND TestDialog(HINSTANCE instance) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog{WS_POPUP | WS_CAPTION | DS_CONTROL, 0, 0, 0, 0, 200, 100};
        WORD menu{};
        WORD windowClass{};
        WORD title{};
    } dialogTemplate;
    return CreateDialogIndirectParamW(instance, &dialogTemplate.dialog, nullptr, DialogProc, 0);
}

bool DialogKey(HWND dialog, WPARAM key) {
    MSG message{dialog, WM_KEYDOWN, key, 0};
    return IsDialogMessageW(dialog, &message) != FALSE;
}

void PumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Key(HWND button, WPARAM key) {
    SendMessageW(button, WM_KEYDOWN, key, 0);
    SendMessageW(button, WM_KEYUP, key, 0);
    PumpMessages();
}

void Mouse(HWND button, POINT release) {
    SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    CHECK(GetCapture() == button);
    SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(release.x, release.y));
    CHECK(GetCapture() != button);
    PumpMessages();
}

HBITMAP TestBitmap() {
    const auto screen = GetDC(nullptr);
    const auto bitmap = CreateCompatibleBitmap(screen, 2, 1);
    const auto dc = CreateCompatibleDC(screen);
    const auto old = SelectObject(dc, bitmap);
    SetPixel(dc, 0, 0, RGB(255, 0, 0));
    SetPixel(dc, 1, 0, RGB(0, 0, 255));
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return bitmap;
}

RECT ForegroundBounds(HDC dc, RECT client, COLORREF background, COLORREF foreground) {
    RECT bounds{client.right, client.bottom, 0, 0};
    bool found{};
    for (int y = client.top; y < client.bottom; ++y) {
        for (int x = client.left; x < client.right; ++x) {
            const auto pixel = GetPixel(dc, x, y);
            if (pixel == CLR_INVALID) continue;
            const int redFromBackground = GetRValue(pixel) - GetRValue(background);
            const int greenFromBackground = GetGValue(pixel) - GetGValue(background);
            const int blueFromBackground = GetBValue(pixel) - GetBValue(background);
            const int redFromForeground = GetRValue(pixel) - GetRValue(foreground);
            const int greenFromForeground = GetGValue(pixel) - GetGValue(foreground);
            const int blueFromForeground = GetBValue(pixel) - GetBValue(foreground);
            const int backgroundDistance = redFromBackground * redFromBackground +
                                           greenFromBackground * greenFromBackground +
                                           blueFromBackground * blueFromBackground;
            const int foregroundDistance = redFromForeground * redFromForeground +
                                           greenFromForeground * greenFromForeground +
                                           blueFromForeground * blueFromForeground;
            if (foregroundDistance >= backgroundDistance) continue;
            found = true;
            bounds.left = (std::min)(bounds.left, static_cast<LONG>(x));
            bounds.top = (std::min)(bounds.top, static_cast<LONG>(y));
            bounds.right = (std::max)(bounds.right, static_cast<LONG>(x + 1));
            bounds.bottom = (std::max)(bounds.bottom, static_cast<LONG>(y + 1));
        }
    }
    return found ? bounds : RECT{};
}

RECT ForegroundBounds(HWND window, COLORREF background, COLORREF foreground) {
    RECT client{};
    GetClientRect(window, &client);
    const auto dc = GetDC(window);
    if (!dc) return {};
    const auto bounds = ForegroundBounds(dc, client, background, foreground);
    ReleaseDC(window, dc);
    return bounds;
}

HBITMAP CaptureClient(HWND window, HDC& dc, HGDIOBJ& previous) {
    RECT client{};
    CHECK(GetClientRect(window, &client));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client.right;
    info.bmiHeader.biHeight = -client.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    dc = CreateCompatibleDC(nullptr);
    CHECK(bitmap != nullptr);
    CHECK(dc != nullptr);
    if (!bitmap || !dc) return bitmap;
    previous = SelectObject(dc, bitmap);
    CHECK(previous != nullptr && previous != HGDI_ERROR);
    PatBlt(dc, 0, 0, client.right, client.bottom, WHITENESS);
    CHECK(PrintWindow(window, dc, PW_CLIENTONLY));
    return bitmap;
}

void CheckBuiltinIconPixels(HWND window) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto dc = GetDC(window);
    CHECK(dc != nullptr);
    if (!dc) return;
    const auto background = RGB(0, 0, 0);
    const auto foreground = RGB(255, 255, 255);
    bool hasBackground{};
    bool hasForeground{};
    bool hasIntermediate{};
    for (int y = 2; y < bounds.bottom - 2; ++y) {
        for (int x = 2; x < bounds.right - 2; ++x) {
            const auto pixel = GetPixel(dc, x, y);
            if (pixel == CLR_INVALID) continue;
            hasBackground |= pixel == background;
            hasForeground |= pixel == foreground;
            hasIntermediate |= pixel != background && pixel != foreground;
        }
    }
    CHECK(hasBackground);
    CHECK(hasForeground);
    CHECK(hasIntermediate);
    CHECK(GetPixel(dc, 0, 0) == background);
    CHECK(GetPixel(dc, bounds.right - 1, 0) == background);
    CHECK(GetPixel(dc, 0, bounds.bottom - 1) == background);
    CHECK(GetPixel(dc, bounds.right - 1, bounds.bottom - 1) == background);
    ReleaseDC(window, dc);
}

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwButtonDisplayTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 300, 240, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::ControlOptions base{.parent = parent,
                             .id = 10,
                             .bounds = {0.6f, 0.6f, 100.6f, 30.6f},
                             .text = L"Button",
                             .appearance = {.cornerRadiusDip = 0.0f}};
    wcw::ButtonOptions buttonOptions{base};

    const auto button = wcw::CreateButton(buttonOptions);
    buttonOptions.id = 11;
    buttonOptions.text = L"Icon";
    const auto iconButton = wcw::CreateButton(buttonOptions);
    base.id = 12;
    base.text = L"Label";
    base.appearance.font = wcw::FontSpec{L"Arial", 19.0f, 700, true};
    const auto label = wcw::CreateLabel(base);
    base.id = 13;
    base.text = L"Image";
    const auto image = wcw::CreateImageView(base, {}, wcw::ImageMode::Contain);
    base.id = 14;
    base.text = L"Separator";
    const auto separator = wcw::CreateSeparator(base);
    base.id = 15;
    base.text = L"Panel";
    const auto panel = wcw::CreatePanel(base);

    const std::array controls{button, iconButton, label, image, separator, panel};
    for (size_t i = 0; i < controls.size(); ++i) {
        CHECK(controls[i] != nullptr);
        CHECK(GetDlgCtrlID(controls[i]) == 10 + static_cast<int>(i));
        CHECK(wcw::SetStyleOverride(controls[i], {.cornerRadiusDip = 0.0f}));
    }
    RECT createdBounds{};
    GetWindowRect(button, &createdBounds);
    CHECK(createdBounds.right - createdBounds.left ==
          wcw::DipToPx(100.6f, GetDpiForWindow(parent)));
    CHECK(wcw::SetStyleOverride(button, {.cornerRadiusDip = 12.0f}));
    const auto region = CreateRectRgn(0, 0, 0, 0);
    CHECK(region != nullptr);
    CHECK(GetWindowRgn(button, region) != ERROR);
    RECT buttonBounds{};
    GetClientRect(button, &buttonBounds);
    CHECK(!PtInRegion(region, 0, 0));
    CHECK(PtInRegion(region, buttonBounds.right / 2, buttonBounds.bottom / 2));
    DeleteObject(region);
    CHECK(wcw::SetStyleOverride(button, {.cornerRadiusDip = 0.0f}));
    wchar_t text[32]{};
    CHECK(GetWindowTextW(button, text, 32) == 6);
    CHECK(std::wstring_view(text) == L"Button");

    SetFocus(button);
    Key(button, VK_SPACE);
    Key(button, VK_RETURN);
    CHECK(clicks == 2);

    Mouse(button, {5, 5});
    CHECK(clicks == 3);
    Mouse(button, {150, 50});
    CHECK(clicks == 3);

    SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    EnableWindow(button, FALSE);
    CHECK(GetCapture() != button);
    EnableWindow(button, TRUE);
    SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    CHECK(clicks == 3);

    SetFocus(button);
    SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
    SetFocus(parent);
    SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
    CHECK(clicks == 3);

    SetFocus(button);
    SendMessageW(button, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(button, WM_CANCELMODE, 0, 0);
    SendMessageW(button, WM_KEYUP, VK_RETURN, 0);
    CHECK(clicks == 3);

    SetFocus(button);
    SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(button, WM_KEYUP, VK_RETURN, 0);
    CHECK(clicks == 3);
    SendMessageW(button, WM_CANCELMODE, 0, 0);
    SendMessageW(button, WM_KEYDOWN, VK_RETURN, 0);
    SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
    CHECK(clicks == 3);
    SendMessageW(button, WM_CANCELMODE, 0, 0);

    SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SetCapture(parent);
    SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    ReleaseCapture();
    CHECK(clicks == 3);

    buttonOptions.id = 20;
    buttonOptions.text = L"Default";
    buttonOptions.isDefault = true;
    const auto defaultButton = wcw::CreateButton(buttonOptions);
    CHECK((SendMessageW(defaultButton, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON) != 0);
    SetFocus(defaultButton);
    Key(defaultButton, VK_RETURN);
    CHECK(clicks == 4);

    buttonOptions.id = 21;
    buttonOptions.text = L"Cancel";
    buttonOptions.isDefault = false;
    buttonOptions.isCancel = true;
    const auto cancelButton = wcw::CreateButton(buttonOptions);
    SetFocus(cancelButton);
    Key(cancelButton, VK_ESCAPE);
    CHECK(clicks == 5);

    const auto dialog = TestDialog(instance);
    CHECK(dialog != nullptr);
    buttonOptions.parent = dialog;
    buttonOptions.id = 40;
    buttonOptions.isDefault = true;
    buttonOptions.isCancel = false;
    const auto dialogDefault = wcw::CreateButton(buttonOptions);
    CHECK(dialogDefault != nullptr);
    const auto defaultId = SendMessageW(dialog, DM_GETDEFID, 0, 0);
    CHECK(HIWORD(defaultId) == DC_HASDEFID);
    CHECK(LOWORD(defaultId) == 40);
    SendMessageW(dialogDefault, BM_CLICK, 0, 0);
    CHECK(dialogClicks == 1);
    EnableWindow(dialogDefault, FALSE);
    SendMessageW(dialogDefault, BM_CLICK, 0, 0);
    CHECK(dialogClicks == 1);
    EnableWindow(dialogDefault, TRUE);
    SetFocus(dialog);
    CHECK(DialogKey(dialog, VK_RETURN));
    CHECK(dialogClicks == 2);

    buttonOptions.id = 41;
    buttonOptions.isDefault = false;
    buttonOptions.isCancel = true;
    const auto dialogCancel = wcw::CreateButton(buttonOptions);
    CHECK(dialogCancel != nullptr);
    CHECK(GetDlgCtrlID(dialogCancel) == IDCANCEL);
    dialogCommand = 0;
    SetFocus(dialog);
    CHECK(DialogKey(dialog, VK_ESCAPE));
    CHECK(dialogCommand == IDCANCEL);

    const std::array displays{label, image, separator, panel};
    for (const auto display : displays) {
        CHECK((GetWindowLongPtrW(display, GWL_STYLE) & WS_TABSTOP) == 0);
        CHECK(SendMessageW(display, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE);
        SetFocus(display);
        CHECK(GetFocus() != display);
    }

    ShowWindow(parent, SW_SHOW);
    ShowWindow(button, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(button, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) != FALSE);
    const auto buttonDc = GetDC(button);
    CHECK(buttonDc != nullptr);
    if (buttonDc) {
        const auto input = wcw::DarkTheme().palette.input;
        CHECK(GetPixel(buttonDc, 5, 15) == RGB(input.r, input.g, input.b));
        RECT rendered{};
        GetClientRect(button, &rendered);
        const auto border = wcw::DarkTheme().palette.border;
        const auto borderColor = RGB(border.r, border.g, border.b);
        CHECK(GetPixel(buttonDc, rendered.right / 2, 0) == borderColor);
        CHECK(GetPixel(buttonDc, rendered.right / 2, rendered.bottom - 1) == borderColor);
        CHECK(GetPixel(buttonDc, 0, rendered.bottom / 2) == borderColor);
        CHECK(GetPixel(buttonDc, rendered.right - 1, rendered.bottom / 2) == borderColor);
        ReleaseDC(button, buttonDc);
    }

    const auto bitmap = TestBitmap();
    CHECK(bitmap != nullptr);
    std::array<HWND, 3> bitmapViews{};
    const std::array modes{wcw::ImageMode::Contain, wcw::ImageMode::Cover,
                           wcw::ImageMode::Stretch};
    base.bounds = {0, 40, 20, 20};
    base.appearance = {.background = wcw::Color::FromRgb(0, 255, 0), .cornerRadiusDip = 0.0f};
    for (size_t i = 0; i < modes.size(); ++i) {
        base.id = 30 + static_cast<int>(i);
        base.bounds.x = static_cast<float>(i * 25);
        bitmapViews[i] = wcw::CreateImageView(base, wcw::ImageSource(bitmap), modes[i]);
        CHECK(bitmapViews[i] != nullptr);
    }
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    for (const auto view : bitmapViews) {
        ShowWindow(view, SW_SHOWNOACTIVATE);
        CHECK(RedrawWindow(view, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    }
    for (const auto view : bitmapViews) DestroyWindow(view);
    BITMAP bitmapInfo{};
    CHECK(GetObjectW(bitmap, sizeof(bitmapInfo), &bitmapInfo) == sizeof(bitmapInfo));
    DeleteObject(bitmap);

    const std::array builtinIcons{wcw::BuiltinIcon::Information,
                                  wcw::BuiltinIcon::Warning,
                                  wcw::BuiltinIcon::Error};
    std::array<HWND, builtinIcons.size()> builtinViews{};
    for (size_t index = 0; index < builtinIcons.size(); ++index) {
        base.id = 60 + static_cast<int>(index);
        base.bounds = {static_cast<float>(index * 28), 130, 24, 24};
        base.appearance = {.background = wcw::Color::FromRgb(0, 0, 0),
                           .foreground = wcw::Color::FromRgb(255, 255, 255),
                           .cornerRadiusDip = 0.0f};
        builtinViews[index] = wcw::CreateImageView(base, wcw::ImageSource(builtinIcons[index]));
        CHECK(builtinViews[index] != nullptr);
        ShowWindow(builtinViews[index], SW_SHOWNOACTIVATE);
        CHECK(RedrawWindow(builtinViews[index], nullptr, nullptr,
                           RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
        CheckBuiltinIconPixels(builtinViews[index]);
    }

    base.id = 63;
    base.bounds = {90, 130, 120, 20};
    const auto stretchedBuiltin = wcw::CreateImageView(
        base, wcw::ImageSource(wcw::BuiltinIcon::Information), wcw::ImageMode::Stretch);
    CHECK(stretchedBuiltin != nullptr);
    ShowWindow(stretchedBuiltin, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(stretchedBuiltin, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    CheckBuiltinIconPixels(stretchedBuiltin);
    const auto stretchedForeground = ForegroundBounds(
        stretchedBuiltin, RGB(0, 0, 0), RGB(255, 255, 255));
    CHECK(stretchedForeground.right - stretchedForeground.left >= 8);
    CHECK(stretchedForeground.bottom - stretchedForeground.top >= 8);

    base.bounds = {0, 70, 120, 48};
    base.text = L"Centered";
    base.appearance = {.background = wcw::Color::FromRgb(0, 0, 0),
                       .foreground = wcw::Color::FromRgb(255, 255, 255),
                       .font = wcw::FontSpec{L"Arial", 14.0f},
                       .paddingXDip = 12.0f,
                       .paddingYDip = 8.0f,
                       .cornerRadiusDip = 0.0f};
    const auto paddedLabel = wcw::CreateLabel(base);
    CHECK(paddedLabel != nullptr);
    ShowWindow(paddedLabel, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(paddedLabel, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    RECT paddedClient{};
    GetClientRect(paddedLabel, &paddedClient);
    const auto paddedText = ForegroundBounds(paddedLabel, RGB(0, 0, 0), RGB(255, 255, 255));
    CHECK(!IsRectEmpty(&paddedText));
    CHECK(paddedText.left >= 10);
    CHECK(paddedClient.right - paddedText.right >= 10);
    CHECK((std::max)(paddedText.top, paddedClient.bottom - paddedText.bottom) -
              (std::min)(paddedText.top, paddedClient.bottom - paddedText.bottom) <=
          2);

    base.bounds = {130, 70, 70, 56};
    base.text = L"First second";
    const auto wrappedLabel = wcw::CreateLabel(base);
    CHECK(wrappedLabel != nullptr);
    ShowWindow(wrappedLabel, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(wrappedLabel, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    RECT wrappedClient{};
    GetClientRect(wrappedLabel, &wrappedClient);
    const auto wrappedText = ForegroundBounds(wrappedLabel, RGB(0, 0, 0), RGB(255, 255, 255));
    CHECK(!IsRectEmpty(&wrappedText));
    CHECK((std::max)(wrappedText.top, wrappedClient.bottom - wrappedText.bottom) -
              (std::min)(wrappedText.top, wrappedClient.bottom - wrappedText.bottom) <=
          2);

    wcw::MenuButtonOptions menuButtonOptions;
    menuButtonOptions.parent = parent;
    menuButtonOptions.id = 70;
    menuButtonOptions.bounds = {0, 160, 140, 36};
    menuButtonOptions.text = L"Menu";
    menuButtonOptions.appearance = {
        .background = wcw::Color::FromRgb(0, 0, 0),
        .foreground = wcw::Color::FromRgb(255, 255, 255),
        .font = wcw::FontSpec{L"Arial", 14.0f},
        .paddingXDip = 10.0f,
        .spacingDip = 6.0f,
        .cornerRadiusDip = 0.0f,
    };
    menuButtonOptions.items = {{.id = 701, .text = L"Item"}};
    const auto renderedMenuButton = wcw::CreateMenuButton(menuButtonOptions);
    CHECK(renderedMenuButton != nullptr);
    ShowWindow(renderedMenuButton, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(renderedMenuButton, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_UPDATENOW) != FALSE);
    RECT menuClient{};
    GetClientRect(renderedMenuButton, &menuClient);
    const auto dpi = GetDpiForWindow(renderedMenuButton);
    const int padding = wcw::DipToPx(10.0f, dpi);
    const int chevronWidth = wcw::DipToPx(16.0f, dpi);
    const RECT chevronArea{menuClient.right - padding - chevronWidth, menuClient.top,
                           menuClient.right - padding, menuClient.bottom};
    HDC dc{};
    HGDIOBJ previous{};
    const auto printed = CaptureClient(renderedMenuButton, dc, previous);
    const auto menuForeground = ForegroundBounds(dc, menuClient, RGB(0, 0, 0), RGB(255, 255, 255));
    CHECK(!IsRectEmpty(&menuForeground));
    bool chevronPixel{};
    LONG textRight{};
    if (dc) {
        for (int y = menuClient.top; y < menuClient.bottom; ++y) {
            for (int x = menuClient.left; x < menuClient.right; ++x) {
                const auto pixel = GetPixel(dc, x, y);
                if (pixel == CLR_INVALID) continue;
                const int backgroundDistance = GetRValue(pixel) * GetRValue(pixel) +
                                               GetGValue(pixel) * GetGValue(pixel) +
                                               GetBValue(pixel) * GetBValue(pixel);
                const int foregroundDistance = (255 - GetRValue(pixel)) * (255 - GetRValue(pixel)) +
                                               (255 - GetGValue(pixel)) * (255 - GetGValue(pixel)) +
                                               (255 - GetBValue(pixel)) * (255 - GetBValue(pixel));
                if (foregroundDistance >= backgroundDistance) continue;
                if (x >= chevronArea.left && x < chevronArea.right) chevronPixel = true;
                else textRight = (std::max)(textRight, static_cast<LONG>(x + 1));
            }
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
    }
    DeleteObject(printed);
    CHECK(chevronPixel);
    CHECK(textRight < chevronArea.left);

    DestroyWindow(defaultButton);
    DestroyWindow(cancelButton);
    DestroyWindow(dialogDefault);
    DestroyWindow(dialogCancel);
    DestroyWindow(dialog);
    DestroyWindow(paddedLabel);
    DestroyWindow(wrappedLabel);
    DestroyWindow(renderedMenuButton);
    DestroyWindow(stretchedBuiltin);
    for (const auto view : builtinViews) DestroyWindow(view);
    for (const auto control : controls) DestroyWindow(control);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
