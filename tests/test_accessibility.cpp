#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <oleacc.h>
#include <windows.h>

#include <string>

namespace {

int clicks;

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) ++clicks;
    return DefWindowProcW(window, message, wParam, lParam);
}

IAccessible* Accessible(HWND window) {
    IAccessible* accessible{};
    CHECK(AccessibleObjectFromWindow(window, OBJID_CLIENT, IID_IAccessible,
                                     reinterpret_cast<void**>(&accessible)) == S_OK);
    CHECK(accessible != nullptr);
    return accessible;
}

VARIANT Self() {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_I4;
    value.lVal = CHILDID_SELF;
    return value;
}

long Role(IAccessible* accessible) {
    VARIANT role;
    VariantInit(&role);
    CHECK(accessible->get_accRole(Self(), &role) == S_OK);
    CHECK(role.vt == VT_I4);
    return role.lVal;
}

long State(IAccessible* accessible) {
    VARIANT state;
    VariantInit(&state);
    CHECK(accessible->get_accState(Self(), &state) == S_OK);
    CHECK(state.vt == VT_I4);
    return state.lVal;
}

std::wstring TextProperty(IAccessible* accessible,
                          HRESULT (STDMETHODCALLTYPE IAccessible::*getter)(VARIANT, BSTR*)) {
    BSTR text{};
    CHECK((accessible->*getter)(Self(), &text) == S_OK);
    std::wstring result = text ? text : L"";
    SysFreeString(text);
    return result;
}

struct ThreadCheck {
    IAccessible* accessible{};
    HRESULT result{};
};

DWORD WINAPI ReadFromWrongThread(void* value) {
    auto& check = *static_cast<ThreadCheck*>(value);
    VARIANT role;
    VariantInit(&role);
    check.result = check.accessible->get_accRole(Self(), &role);
    return 0;
}

} // namespace

int main() {
    const auto instance = GetModuleHandleW(nullptr);
    const WNDCLASSW parentClass{.lpfnWndProc = ParentProc,
                                .hInstance = instance,
                                .lpszClassName = L"WcwAccessibilityTestParent"};
    CHECK(RegisterClassW(&parentClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    const auto parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                        0, 0, 600, 400, nullptr, nullptr, instance, nullptr);
    CHECK(parent != nullptr);
    CHECK(wcw::Initialize(instance));

    wcw::ButtonOptions buttonOptions;
    buttonOptions.parent = parent;
    buttonOptions.id = 10;
    buttonOptions.bounds = {0, 0, 100, 30};
    buttonOptions.text = L"Visible caption";
    buttonOptions.accessibleName = L"Run timer";
    const auto button = wcw::CreateButton(buttonOptions);
    auto* accessible = Accessible(button);
    CHECK(TextProperty(accessible, &IAccessible::get_accName) == L"Run timer");
    CHECK(Role(accessible) == ROLE_SYSTEM_PUSHBUTTON);
    CHECK((State(accessible) & STATE_SYSTEM_FOCUSABLE) != 0);
    CHECK(TextProperty(accessible, &IAccessible::get_accDefaultAction) == L"Press");
    CHECK(accessible->accDoDefaultAction(Self()) == S_OK);
    CHECK(clicks == 1);
    accessible->Release();

    wcw::ControlOptions displayOptions;
    displayOptions.parent = parent;
    displayOptions.id = 12;
    displayOptions.bounds = {110, 0, 100, 30};
    displayOptions.text = L"Timer status";
    const auto label = wcw::CreateLabel(displayOptions);
    accessible = Accessible(label);
    CHECK(Role(accessible) == ROLE_SYSTEM_STATICTEXT);
    CHECK(TextProperty(accessible, &IAccessible::get_accName) == L"Timer status");
    CHECK((State(accessible) & STATE_SYSTEM_FOCUSABLE) == 0);
    accessible->Release();

    buttonOptions.id = 11;
    buttonOptions.accessibleName.clear();
    const auto fallbackButton = wcw::CreateButton(buttonOptions);
    accessible = Accessible(fallbackButton);
    CHECK(TextProperty(accessible, &IAccessible::get_accName) == L"Visible caption");
    EnableWindow(fallbackButton, FALSE);
    CHECK((State(accessible) & STATE_SYSTEM_UNAVAILABLE) != 0);
    accessible->Release();

    wcw::CheckableOptions checkOptions;
    checkOptions.parent = parent;
    checkOptions.id = 20;
    checkOptions.bounds = {0, 40, 120, 30};
    checkOptions.text = L"Enabled";
    checkOptions.checked = true;
    const auto checkbox = wcw::CreateCheckbox(checkOptions);
    accessible = Accessible(checkbox);
    CHECK(Role(accessible) == ROLE_SYSTEM_CHECKBUTTON);
    CHECK((State(accessible) & STATE_SYSTEM_CHECKED) != 0);
    CHECK(TextProperty(accessible, &IAccessible::get_accDefaultAction) == L"Uncheck");
    accessible->Release();

    checkOptions.id = 21;
    const auto toggle = wcw::CreateToggle(checkOptions);
    accessible = Accessible(toggle);
    CHECK(Role(accessible) == ROLE_SYSTEM_CHECKBUTTON);
    CHECK((State(accessible) & STATE_SYSTEM_CHECKED) != 0);
    accessible->Release();

    wcw::TextBoxOptions textOptions;
    textOptions.parent = parent;
    textOptions.id = 30;
    textOptions.bounds = {0, 80, 160, 30};
    textOptions.text = L"hello";
    textOptions.accessibleName = L"Character name";
    const auto textBox = wcw::CreateTextBox(textOptions);
    accessible = Accessible(textBox);
    CHECK(Role(accessible) == ROLE_SYSTEM_TEXT);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"hello");
    accessible->Release();

    textOptions.id = 32;
    textOptions.password = true;
    const auto password = wcw::CreateTextBox(textOptions);
    accessible = Accessible(password);
    BSTR protectedValue = reinterpret_cast<BSTR>(1);
    CHECK(accessible->get_accValue(Self(), &protectedValue) == S_FALSE);
    CHECK(protectedValue == nullptr);
    CHECK((State(accessible) & STATE_SYSTEM_PROTECTED) != 0);
    accessible->Release();

    wcw::NumericBoxOptions numericOptions;
    numericOptions.parent = parent;
    numericOptions.id = 31;
    numericOptions.bounds = {170, 80, 100, 30};
    numericOptions.value = 42;
    const auto numeric = wcw::CreateNumericBox(numericOptions);
    accessible = Accessible(numeric);
    CHECK(Role(accessible) == ROLE_SYSTEM_SPINBUTTON);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"42");
    accessible->Release();

    wcw::SliderOptions sliderOptions;
    sliderOptions.parent = parent;
    sliderOptions.id = 40;
    sliderOptions.bounds = {0, 120, 180, 30};
    sliderOptions.value = 25;
    const auto slider = wcw::CreateSlider(sliderOptions);
    accessible = Accessible(slider);
    CHECK(Role(accessible) == ROLE_SYSTEM_SLIDER);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"25");
    accessible->Release();

    wcw::ProgressBarOptions progressOptions;
    progressOptions.parent = parent;
    progressOptions.id = 41;
    progressOptions.bounds = {0, 160, 180, 20};
    progressOptions.value = 75;
    const auto progress = wcw::CreateProgressBar(progressOptions);
    accessible = Accessible(progress);
    CHECK(Role(accessible) == ROLE_SYSTEM_PROGRESSBAR);
    CHECK((State(accessible) & STATE_SYSTEM_FOCUSABLE) == 0);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"75");
    BSTR missingAction = reinterpret_cast<BSTR>(1);
    CHECK(accessible->get_accDefaultAction(Self(), &missingAction) == S_FALSE);
    CHECK(missingAction == nullptr);
    accessible->Release();

    wcw::ComboBoxOptions comboOptions;
    comboOptions.parent = parent;
    comboOptions.id = 50;
    comboOptions.bounds = {0, 190, 180, 30};
    comboOptions.accessibleName = L"Build";
    comboOptions.items = {{L"Hammerdin", 1, {}}, {L"Sorceress", 2, {}}};
    comboOptions.selectedIndex = 1;
    const auto combo = wcw::CreateComboBox(comboOptions);
    accessible = Accessible(combo);
    CHECK(Role(accessible) == ROLE_SYSTEM_COMBOBOX);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"Sorceress");
    CHECK(TextProperty(accessible, &IAccessible::get_accDefaultAction) == L"Open");
    ShowWindow(parent, SW_SHOW);
    ShowWindow(combo, SW_SHOW);
    CHECK(accessible->accDoDefaultAction(Self()) == S_OK);
    CHECK(TextProperty(accessible, &IAccessible::get_accDefaultAction) == L"Close");
    VARIANT comboFocus;
    VariantInit(&comboFocus);
    CHECK(accessible->get_accFocus(&comboFocus) == S_OK);
    CHECK(comboFocus.vt == VT_I4 && comboFocus.lVal == CHILDID_SELF);
    CHECK(accessible->accDoDefaultAction(Self()) == S_OK);
    CHECK(TextProperty(accessible, &IAccessible::get_accDefaultAction) == L"Open");
    accessible->Release();

    wcw::ScrollViewOptions scrollOptions;
    scrollOptions.parent = parent;
    scrollOptions.id = 60;
    scrollOptions.bounds = {200, 120, 180, 100};
    scrollOptions.accessibleName = L"Timer list";
    scrollOptions.contentExtent = {400, 300};
    const auto scroll = wcw::CreateScrollView(scrollOptions);
    accessible = Accessible(scroll);
    CHECK(Role(accessible) == ROLE_SYSTEM_PANE);
    long childCount{};
    CHECK(accessible->get_accChildCount(&childCount) == S_OK);
    CHECK(childCount == 1);

    ThreadCheck threadCheck{accessible};
    const auto thread = CreateThread(nullptr, 0, ReadFromWrongThread, &threadCheck, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(threadCheck.result == RPC_E_WRONG_THREAD);

    DestroyWindow(parent);
    VARIANT deadRole;
    VariantInit(&deadRole);
    CHECK(accessible->get_accRole(Self(), &deadRole) != S_OK);
    accessible->Release();
    wcw::Shutdown();
    return testFailures;
}
