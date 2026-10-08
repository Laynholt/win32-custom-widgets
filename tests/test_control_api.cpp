#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>
#include <cmath>

namespace {
int clicks{}, nameEvents{};
HWND slider{}, checkbox{}, tabs{}, watched{};
double notifiedValue{};
bool notifiedCheck{};
int notifiedSelection{-1};

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) ++clicks;
    if (message == WM_NOTIFY) {
        if (const auto* value = wcw::DecodeValueChangedNotification(lParam, slider))
            notifiedValue = value->value;
        if (const auto* check = wcw::DecodeCheckChangedNotification(lParam, checkbox))
            notifiedCheck = check->checked;
        if (const auto* selection = wcw::DecodeSelectionChangedNotification(lParam, tabs))
            notifiedSelection = selection->newIndex;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void CALLBACK NameEvent(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD) {
    if (window == watched && object == OBJID_CLIENT && child == CHILDID_SELF) ++nameEvents;
}

struct Capture {
    HDC dc{CreateCompatibleDC(nullptr)};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    RECT client{};
    explicit Capture(HWND window) {
        CHECK(GetClientRect(window, &client));
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = client.right;
        info.bmiHeader.biHeight = -client.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits{};
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        CHECK(dc && bitmap);
        if (dc && bitmap) {
            previous = SelectObject(dc, bitmap);
            CHECK(PrintWindow(window, dc, PW_CLIENTONLY));
        }
    }
    ~Capture() {
        if (previous) SelectObject(dc, previous);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    RECT TextBounds() const {
        RECT text{client.right, client.bottom, 0, 0};
        for (int y = 0; y < client.bottom; ++y) {
            for (int x = 0; x < client.right; ++x) {
                const auto pixel = GetPixel(dc, x, y);
                if (pixel == CLR_INVALID || GetRValue(pixel) < 128) continue;
                text.left = (std::min)(text.left, static_cast<LONG>(x));
                text.top = (std::min)(text.top, static_cast<LONG>(y));
                text.right = (std::max)(text.right, static_cast<LONG>(x + 1));
                text.bottom = (std::max)(text.bottom, static_cast<LONG>(y + 1));
            }
        }
        return text;
    }
};

IAccessible* Accessible(HWND window) {
    IAccessible* object{};
    CHECK(AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
                                    reinterpret_cast<void**>(&object)) == S_OK);
    return object;
}

std::wstring Name(IAccessible* object) {
    if (!object) return {};
    VARIANT self{};
    self.vt = VT_I4;
    BSTR text{};
    CHECK(object->get_accName(self, &text) == S_OK);
    std::wstring result = text ? text : L"";
    SysFreeString(text);
    return result;
}

DWORD WINAPI WrongThread(void* argument) {
    const auto button = static_cast<HWND>(argument);
    CHECK(!wcw::SetChecked(button, false));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!wcw::GetChecked(button));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!wcw::SetAccessibleName(button, L"Wrong thread"));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    return 0;
}
} // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto instance = GetModuleHandleW(nullptr);
    CHECK(wcw::Initialize(instance));
    const WNDCLASSW cls{.lpfnWndProc = ParentProc, .hInstance = instance,
                        .lpszClassName = L"WcwControlApiTest"};
    CHECK(RegisterClassW(&cls));
    const auto parent = CreateWindowExW(0, cls.lpszClassName, L"API test", WS_OVERLAPPEDWINDOW,
                                       0, 0, 600, 500, nullptr, nullptr, instance, nullptr);
    CHECK(parent);
    ShowWindow(parent, SW_SHOWNOACTIVATE);

    wcw::ControlOptions labelOptions;
    labelOptions.parent = parent;
    labelOptions.text = L"Aligned";
    labelOptions.style = WS_VISIBLE;
    labelOptions.bounds = {10, 10, 240, 60};
    labelOptions.appearance = {.background = wcw::Color::FromRgb(0, 0, 0),
                              .foreground = wcw::Color::FromRgb(255, 255, 255),
                              .font = wcw::FontSpec{L"Arial", 14.0f},
                              .paddingXDip = 12.0f, .paddingYDip = 8.0f,
                              .cornerRadiusDip = 0.0f};
    const UINT alignments[]{DT_LEFT, DT_CENTER, DT_RIGHT};
    for (const auto alignment : alignments) {
        const auto label = wcw::CreateLabel(labelOptions, alignment);
        CHECK(label);
        if (!label) continue;
        Capture image(label);
        const auto text = image.TextBounds();
        CHECK(!IsRectEmpty(&text));
        const auto padding = wcw::DipToPx(12, GetDpiForWindow(label));
        if (alignment == DT_LEFT) CHECK(std::abs(text.left - padding) <= 3);
        if (alignment == DT_CENTER)
            CHECK(std::abs(text.left + text.right - image.client.right) <= 3);
        if (alignment == DT_RIGHT) CHECK(std::abs(image.client.right - text.right - padding) <= 3);
        DestroyWindow(label);
    }
    CHECK(!wcw::CreateLabel(labelOptions, DT_CENTER | DT_RIGHT));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    // A short label must never paint into its top/bottom padding, even when text is clipped.
    labelOptions.bounds.height = 30;
    labelOptions.text = L"This label wraps over several lines";
    const auto shortLabel = wcw::CreateLabel(labelOptions);
    CHECK(shortLabel);
    {
        Capture image(shortLabel);
        const auto text = image.TextBounds();
        CHECK(!IsRectEmpty(&text));
        const auto padding = wcw::DipToPx(8, GetDpiForWindow(shortLabel));
        CHECK(text.top >= padding && text.bottom <= image.client.bottom - padding);
    }
    labelOptions.appearance.paddingYDip = 30.0f;
    const auto emptyLabel = wcw::CreateLabel(labelOptions);
    { Capture image(emptyLabel); const auto text = image.TextBounds(); CHECK(IsRectEmpty(&text)); }

    wcw::ButtonOptions options;
    options.parent = parent;
    options.bounds = {10, 100, 180, 40};
    options.style = WS_VISIBLE;
    options.text = L"Selected button";
    options.accessibleName = L"Original button name";
    options.checked = true;
    options.appearance = {.background = wcw::Color::FromRgb(0, 0, 0),
                          .selected = wcw::Color::FromRgb(40, 80, 160), .cornerRadiusDip = 0.0f};
    const auto button = wcw::CreateButton(options);
    CHECK(button && wcw::GetChecked(button));
    { Capture image(button); CHECK(GetPixel(image.dc, 4, 4) == RGB(40, 80, 160)); }
    auto* accessible = Accessible(button);
    CHECK(Name(accessible) == L"Original button name");
    if (accessible) {
        VARIANT self{}, state{};
        self.vt = VT_I4;
        CHECK(accessible->get_accState(self, &state) == S_OK);
        CHECK((state.lVal & STATE_SYSTEM_CHECKED) != 0);
    }
    SendMessageW(button, BM_CLICK, 0, 0);
    CHECK(clicks == 1 && wcw::GetChecked(button));
    CHECK(wcw::SetChecked(button, false));
    CHECK(!wcw::GetChecked(button) && clicks == 1);
    { Capture image(button); CHECK(GetPixel(image.dc, 4, 4) == RGB(0, 0, 0)); }
    CHECK(wcw::SetChecked(button, true));

    watched = button;
    const auto hook = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr,
                                    NameEvent, GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    CHECK(hook);
    CHECK(wcw::SetAccessibleName(button, L"Renamed button"));
    CHECK(Name(accessible) == L"Renamed button");
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    CHECK(nameEvents > 0);
    CHECK(wcw::SetAccessibleName(button, L""));
    CHECK(Name(accessible) == options.text);
    if (hook) UnhookWinEvent(hook);
    if (accessible) accessible->Release();
    CHECK(wcw::SetAccessibleName(shortLabel, L"Name before creating MSAA object"));
    accessible = Accessible(shortLabel);
    CHECK(Name(accessible) == L"Name before creating MSAA object");
    if (accessible) accessible->Release();
    CHECK(!wcw::SetAccessibleName(parent, L"Unrelated window"));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(!wcw::SetChecked(parent, true));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    const auto thread = CreateThread(nullptr, 0, WrongThread, button, 0, nullptr);
    CHECK(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    if (thread) CloseHandle(thread);
    CHECK(wcw::GetChecked(button));

    wcw::SliderOptions sliderOptions;
    sliderOptions.parent = parent;
    sliderOptions.bounds = {10, 160, 180, 32};
    slider = wcw::CreateSlider(sliderOptions);
    CHECK(wcw::SetSliderValue(slider, 25));
    SendMessageW(slider, WM_KEYDOWN, VK_RIGHT, 0);
    CHECK(notifiedValue == 26);
    wcw::CheckableOptions checkOptions;
    checkOptions.parent = parent;
    checkOptions.bounds = {10, 200, 180, 32};
    checkbox = wcw::CreateCheckbox(checkOptions);
    SendMessageW(checkbox, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(checkbox, WM_KEYUP, VK_SPACE, 0);
    CHECK(notifiedCheck && wcw::GetChecked(checkbox));
    wcw::TabControlOptions tabOptions;
    tabOptions.parent = parent;
    tabOptions.bounds = {10, 240, 180, 40};
    tabOptions.items = {{L"First", 1}, {L"Second", 2}};
    tabs = wcw::CreateTabControl(tabOptions);
    CHECK(wcw::SetTabSelection(tabs, 1));
    CHECK(notifiedSelection == 1);
    CHECK(wcw::SetAccessibleName(tabs, L"Renamed tabs"));
    accessible = Accessible(tabs);
    CHECK(Name(accessible) == L"Renamed tabs");
    if (accessible) accessible->Release();

    const wcw::ValueChangedNotification value{{slider, 0, wcw::WCN_VALUE_CHANGED}, 42};
    const auto payload = reinterpret_cast<LPARAM>(&value);
    CHECK(wcw::DecodeValueChangedNotification(payload, slider) == &value);
    CHECK(!wcw::DecodeValueChangedNotification(payload, checkbox));
    CHECK(!wcw::DecodeCheckChangedNotification(payload, slider));
    CHECK(!wcw::DecodeSelectionChangedNotification(payload, slider));
    CHECK(!wcw::DecodeValueChangedNotification(0, slider));
    CHECK(!wcw::DecodeCheckChangedNotification(0, checkbox));
    CHECK(!wcw::DecodeSelectionChangedNotification(0, tabs));
    CHECK(!wcw::DecodeValueChangedNotification(payload, nullptr));

    DestroyWindow(parent);
    CHECK(!wcw::SetAccessibleName(button, L"Destroyed"));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    wcw::Shutdown();
    return testFailures;
}
