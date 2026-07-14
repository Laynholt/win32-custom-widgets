#include "Test.h"

#include <wcw/Controls.h>
#include <wcw/Runtime.h>

#include <oleacc.h>
#include <windows.h>

#include <string>

namespace {

int clicks;
HWND watchedWindow;
int focusEvents;
int valueEvents;
int selectionEvents;
int stateEvents;
int menuCommand;
int keyboardMenuCommand;
HWND menuButton;

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child,
                           DWORD, DWORD) {
    if (window != watchedWindow || object != OBJID_CLIENT || child != CHILDID_SELF) return;
    if (event == EVENT_OBJECT_FOCUS) ++focusEvents;
    if (event == EVENT_OBJECT_VALUECHANGE) ++valueEvents;
    if (event == EVENT_OBJECT_SELECTION) ++selectionEvents;
    if (event == EVENT_OBJECT_STATECHANGE) ++stateEvents;
}

void PumpEvents() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
}

LRESULT CALLBACK ParentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED) ++clicks;
    if (message == WM_COMMAND && reinterpret_cast<HWND>(lParam) == menuButton)
        menuCommand = LOWORD(wParam);
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

long Role(IAccessible* accessible, VARIANT child = Self()) {
    VARIANT role;
    VariantInit(&role);
    CHECK(accessible->get_accRole(child, &role) == S_OK);
    CHECK(role.vt == VT_I4);
    return role.lVal;
}

long State(IAccessible* accessible, VARIANT child = Self()) {
    VARIANT state;
    VariantInit(&state);
    CHECK(accessible->get_accState(child, &state) == S_OK);
    CHECK(state.vt == VT_I4);
    return state.lVal;
}

std::wstring TextProperty(IAccessible* accessible,
                          HRESULT (STDMETHODCALLTYPE IAccessible::*getter)(VARIANT, BSTR*),
                          VARIANT child = Self()) {
    BSTR text{};
    CHECK((accessible->*getter)(child, &text) == S_OK);
    std::wstring result = text ? text : L"";
    SysFreeString(text);
    return result;
}

void CALLBACK SelectMenuWithKeyboard(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = FindWindowW(L"WcwMenuPopup", nullptr);
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

void CALLBACK InspectAccessibleMenu(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = FindWindowW(L"WcwMenuPopup", nullptr);
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);

    auto* buttonAccessible = Accessible(menuButton);
    CHECK((State(buttonAccessible) & STATE_SYSTEM_EXPANDED) != 0);
    CHECK(TextProperty(buttonAccessible, &IAccessible::get_accDefaultAction) == L"Close");
    buttonAccessible->Release();

    auto* menuAccessible = Accessible(popup);
    CHECK(Role(menuAccessible) == ROLE_SYSTEM_MENUPOPUP);
    long childCount{};
    CHECK(menuAccessible->get_accChildCount(&childCount) == S_OK);
    CHECK(childCount == 3);

    auto first = Self();
    first.lVal = 1;
    auto parentItem = Self();
    parentItem.lVal = 2;
    auto unavailable = Self();
    unavailable.lVal = 3;
    CHECK(Role(menuAccessible, first) == ROLE_SYSTEM_MENUITEM);
    CHECK(TextProperty(menuAccessible, &IAccessible::get_accName, first) == L"Open");
    CHECK((State(menuAccessible, first) & STATE_SYSTEM_FOCUSED) != 0);
    CHECK((State(menuAccessible, first) & STATE_SYSTEM_CHECKED) != 0);
    CHECK((State(menuAccessible, parentItem) & STATE_SYSTEM_HASPOPUP) != 0);
    CHECK((State(menuAccessible, unavailable) & STATE_SYSTEM_UNAVAILABLE) != 0);
    CHECK(TextProperty(menuAccessible, &IAccessible::get_accDefaultAction, first) == L"Execute");

    VARIANT focus;
    VariantInit(&focus);
    CHECK(menuAccessible->get_accFocus(&focus) == S_OK);
    CHECK(focus.vt == VT_I4 && focus.lVal == 1);
    VARIANT destination;
    VariantInit(&destination);
    CHECK(menuAccessible->accNavigate(NAVDIR_FIRSTCHILD, Self(), &destination) == S_OK);
    CHECK(destination.vt == VT_I4 && destination.lVal == 1);
    VariantClear(&destination);
    CHECK(menuAccessible->accNavigate(NAVDIR_NEXT, first, &destination) == S_OK);
    CHECK(destination.vt == VT_I4 && destination.lVal == 2);
    long left{}, top{}, width{}, height{};
    CHECK(menuAccessible->accLocation(&left, &top, &width, &height, first) == S_OK);
    CHECK(width > 0 && height > 0);

    const auto action = menuAccessible->accDoDefaultAction(first);
    CHECK(action == S_OK);
    if (action != S_OK) SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
    menuAccessible->Release();
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

struct RuntimeThreadCheck {
    HINSTANCE instance{};
    bool initialized{};
    DWORD error{};
    DWORD shutdownError{};
};

DWORD WINAPI InitializeFromWrongThread(void* value) {
    auto& check = *static_cast<RuntimeThreadCheck*>(value);
    SetLastError(ERROR_SUCCESS);
    check.initialized = wcw::Initialize(check.instance);
    check.error = GetLastError();
    SetLastError(ERROR_SUCCESS);
    wcw::Shutdown();
    check.shutdownError = GetLastError();
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

    RuntimeThreadCheck runtimeThreadCheck{instance};
    auto thread = CreateThread(nullptr, 0, InitializeFromWrongThread, &runtimeThreadCheck, 0,
                               nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(!runtimeThreadCheck.initialized);
    CHECK(runtimeThreadCheck.error == ERROR_INVALID_THREAD_ID);
    CHECK(runtimeThreadCheck.shutdownError == ERROR_INVALID_THREAD_ID);

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
    EnableWindow(fallbackButton, TRUE);
    EnableWindow(parent, FALSE);
    CHECK((State(accessible) & STATE_SYSTEM_UNAVAILABLE) != 0);
    const auto clicksBeforeUnavailableAction = clicks;
    CHECK(accessible->accDoDefaultAction(Self()) == E_ACCESSDENIED);
    CHECK(clicks == clicksBeforeUnavailableAction);
    EnableWindow(parent, TRUE);
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
    textOptions.text = L"initial-secret";
    textOptions.placeholder = L"Password";
    textOptions.accessibleName.clear();
    const auto password = wcw::CreateTextBox(textOptions);
    accessible = Accessible(password);
    const auto passwordEdit = GetWindow(password, GW_CHILD);
    CHECK(passwordEdit != nullptr);
    SetWindowTextW(passwordEdit, L"edited-secret");
    wchar_t outerText[64]{};
    GetWindowTextW(password, outerText, 64);
    CHECK(std::wstring(outerText).empty());
    CHECK(TextProperty(accessible, &IAccessible::get_accName) == L"Password");
    BSTR protectedValue = reinterpret_cast<BSTR>(1);
    CHECK(accessible->get_accValue(Self(), &protectedValue) == S_FALSE);
    CHECK(protectedValue == nullptr);
    CHECK((State(accessible) & STATE_SYSTEM_PROTECTED) != 0);

    ShowWindow(parent, SW_SHOW);
    ShowWindow(password, SW_SHOW);
    const auto eventHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_VALUECHANGE, nullptr,
                                           WinEventProc, GetCurrentProcessId(), GetCurrentThreadId(),
                                           WINEVENT_OUTOFCONTEXT);
    CHECK(eventHook != nullptr);
    watchedWindow = password;
    focusEvents = 0;
    SetFocus(passwordEdit);
    PumpEvents();
    CHECK(focusEvents == 1);
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
    CHECK((State(accessible) & STATE_SYSTEM_SELECTED) == 0);
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

    watchedWindow = combo;
    valueEvents = 0;
    selectionEvents = 0;
    auto replacementItems = comboOptions.items;
    replacementItems[1].text = L"Blizzard Sorceress";
    CHECK(wcw::SetComboItems(combo, replacementItems));
    PumpEvents();
    CHECK(valueEvents == 1);
    CHECK(selectionEvents == 0);
    CHECK(TextProperty(accessible, &IAccessible::get_accValue) == L"Blizzard Sorceress");
    accessible->Release();

    wcw::MenuButtonOptions menuOptions;
    menuOptions.parent = parent;
    menuOptions.id = 55;
    menuOptions.bounds = {0, 230, 180, 30};
    menuOptions.text = L"Actions";
    menuOptions.items = {{.id = 70, .text = L"Open", .checked = true},
                         {.separator = true},
                         {.text = L"More", .children = {{.id = 71, .text = L"Nested"}}},
                         {.id = 72, .text = L"Unavailable", .enabled = false}};
    menuButton = wcw::CreateMenuButton(menuOptions);
    CHECK(menuButton != nullptr);
    auto* menuButtonAccessible = Accessible(menuButton);
    CHECK(Role(menuButtonAccessible) == ROLE_SYSTEM_PUSHBUTTON);
    const auto collapsedState = State(menuButtonAccessible);
    CHECK((collapsedState & STATE_SYSTEM_HASPOPUP) != 0);
    CHECK((collapsedState & STATE_SYSTEM_COLLAPSED) != 0);
    CHECK(TextProperty(menuButtonAccessible, &IAccessible::get_accDefaultAction) == L"Open");

    watchedWindow = menuButton;
    stateEvents = 0;
    menuCommand = 0;
    SetTimer(parent, 1, 1, SelectMenuWithKeyboard);
    SendMessageW(menuButton, BM_CLICK, 0, 0);
    keyboardMenuCommand = menuCommand;
    CHECK(keyboardMenuCommand == 70);

    menuCommand = 0;
    SetTimer(parent, 2, 1, InspectAccessibleMenu);
    SendMessageW(menuButton, BM_CLICK, 0, 0);
    CHECK(menuCommand == keyboardMenuCommand);
    CHECK((State(menuButtonAccessible) & STATE_SYSTEM_COLLAPSED) != 0);
    CHECK(TextProperty(menuButtonAccessible, &IAccessible::get_accDefaultAction) == L"Open");
    PumpEvents();
    CHECK(stateEvents == 4);
    menuButtonAccessible->Release();

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
    thread = CreateThread(nullptr, 0, ReadFromWrongThread, &threadCheck, 0, nullptr);
    CHECK(thread != nullptr);
    CHECK(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(threadCheck.result == RPC_E_WRONG_THREAD);

    DestroyWindow(parent);
    VARIANT deadRole;
    deadRole.vt = VT_I4;
    deadRole.lVal = 123;
    CHECK(accessible->get_accRole(Self(), &deadRole) != S_OK);
    CHECK(deadRole.vt == VT_EMPTY);
    long deadChildCount = 123;
    CHECK(accessible->get_accChildCount(&deadChildCount) == CO_E_OBJNOTCONNECTED);
    CHECK(deadChildCount == 0);
    IDispatch* deadParent = reinterpret_cast<IDispatch*>(1);
    CHECK(accessible->get_accParent(&deadParent) == CO_E_OBJNOTCONNECTED);
    CHECK(deadParent == nullptr);
    BSTR deadDescription = reinterpret_cast<BSTR>(1);
    CHECK(accessible->get_accDescription(Self(), &deadDescription) == CO_E_OBJNOTCONNECTED);
    CHECK(deadDescription == nullptr);
    VARIANT deadNavigation;
    deadNavigation.vt = VT_I4;
    deadNavigation.lVal = 123;
    CHECK(accessible->accNavigate(NAVDIR_FIRSTCHILD, Self(), &deadNavigation) ==
          CO_E_OBJNOTCONNECTED);
    CHECK(deadNavigation.vt == VT_EMPTY);
    accessible->Release();
    UnhookWinEvent(eventHook);
    wcw::Shutdown();
    return testFailures;
}
