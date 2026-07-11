#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include <windows.h>

#include <array>

namespace wcw::internal {
FontSpec ResolveLabelFont(const Theme& theme, const StyleOverride& local);
}

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
    wcw::ButtonOptions buttonOptions;
    static_cast<wcw::ControlOptions&>(buttonOptions) = base;

    const auto button = wcw::CreateButton(buttonOptions);
    buttonOptions.id = 11;
    buttonOptions.text = L"Icon";
    const auto iconButton = wcw::CreateIconButton(buttonOptions);
    base.id = 12;
    base.text = L"Label";
    base.appearance.font = wcw::FontSpec{L"Arial", 19.0f, 700, true};
    const auto label = wcw::CreateLabel(base);
    const auto labelFont = wcw::internal::ResolveLabelFont(wcw::GetTheme(), base.appearance);
    CHECK(labelFont.family == L"Arial");
    CHECK(labelFont.sizeDip == 19.0f);
    CHECK(labelFont.weight == 700);
    CHECK(labelFont.italic);
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

    DestroyWindow(defaultButton);
    DestroyWindow(cancelButton);
    DestroyWindow(dialogDefault);
    DestroyWindow(dialogCancel);
    DestroyWindow(dialog);
    for (const auto control : controls) DestroyWindow(control);
    wcw::Shutdown();
    DestroyWindow(parent);
    return testFailures;
}
