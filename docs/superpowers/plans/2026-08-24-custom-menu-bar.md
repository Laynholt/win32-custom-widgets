# Custom Menu Bar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a reusable, themed `MenuBar` control with standard mouse, keyboard, accessibility, demo, and Release executable support.

**Architecture:** Implement one custom child-window class that consumes the existing `MenuItem` tree and delegates popup rendering to `Menu.cpp`. Keep pure label parsing and validation in the existing `MenuModel`, extend the popup controller only for root-menu switching, and reuse the existing MSAA menu-item provider for the bar.

**Tech Stack:** C++20, Win32, GDI+, MSAA/oleacc, CMake, CTest, Visual Studio 2022 generator.

**Spec:** `docs/superpowers/specs/2026-08-24-custom-menu-bar-design.md`

## Global Constraints

- Use `std::vector<MenuItem>` directly; do not add `HMENU` conversion or a second menu model.
- Reuse the existing popup renderer; do not duplicate popup windows, layout, painting, or command dispatch.
- Keep the existing `MenuButton` and context-menu behavior unchanged.
- Preserve the single responsive demo window and all existing widget examples.
- Use existing Win32, C++ standard-library, and project helpers; add no dependency.
- All shell commands in this repository are prefixed with `rtk`.
- Every implementation task follows red-green-refactor and ends with its focused tests passing.
- Workers share the checkout: do not revert other agents' edits; integrate with changes already committed by earlier tasks.

---

### Task 1: Public Model, Validation, and Mnemonics

**Ownership:** `include/wcw/Controls.h`, `src/MenuModel.h`, `src/MenuModel.cpp`, `tests/test_menu_bar.cpp`, and the `MenuBarTests` line in `CMakeLists.txt`.

**Files:**
- Modify: `include/wcw/Controls.h`
- Modify: `src/MenuModel.h`
- Modify: `src/MenuModel.cpp`
- Create: `tests/test_menu_bar.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: existing `MenuItem`, `ValidMenuItems(std::span<const MenuItem>)`, and `NextMenuIndex(std::span<const MenuItem>, int, int)`.
- Produces: public `MenuBarOptions`, `CreateMenuBar`, and `SetMenuBarItems`; internal `MenuLabel`, `ParseMenuLabel`, and `ValidMenuBarItems`.

- [ ] **Step 1: Add a failing model/API test target**

Add `wcw_add_test(MenuBarTests tests/test_menu_bar.cpp)` after `MenuTests` in `CMakeLists.txt`. Create the test with the pure checks below; this first compile must fail because the new interfaces do not exist.

```cpp
#include "MenuModel.h"
#include "Test.h"

#include <wcw/Controls.h>

#include <vector>

int main() {
    using wcw::MenuItem;
    using wcw::internal::ParseMenuLabel;
    using wcw::internal::ValidMenuBarItems;

    const auto file = ParseMenuLabel(L"&File");
    CHECK(file.text == L"File");
    CHECK(file.mnemonic == L'f');

    const auto escaped = ParseMenuLabel(L"Save && Close");
    CHECK(escaped.text == L"Save & Close");
    CHECK(escaped.mnemonic == 0);

    const auto trailing = ParseMenuLabel(L"Help&");
    CHECK(trailing.text == L"Help&");
    CHECK(trailing.mnemonic == 0);

    const std::vector<MenuItem> valid{
        {.text = L"&File", .children = {{.id = 101, .text = L"Open"}}},
        {.id = 102, .text = L"&Help"},
    };
    CHECK(ValidMenuBarItems(valid));
    const std::vector<MenuItem> empty;
    const std::vector<MenuItem> separator{{.separator = true}};
    const std::vector<MenuItem> emptyLabel{{.id = 1, .text = L"&"}};
    CHECK(!ValidMenuBarItems(empty));
    CHECK(!ValidMenuBarItems(separator));
    CHECK(!ValidMenuBarItems(emptyLabel));

    const std::vector<MenuItem> navigation{
        {.text = L"Disabled", .children = {{.id = 1, .text = L"One"}}, .enabled = false},
        {.text = L"File", .children = {{.id = 2, .text = L"Two"}}},
        {.text = L"Edit", .children = {{.id = 3, .text = L"Three"}}},
    };
    CHECK(wcw::internal::NextMenuIndex(navigation, -1, 1) == 1);
    CHECK(wcw::internal::NextMenuIndex(navigation, 1, -1) == 2);

    wcw::MenuBarOptions options;
    options.items = valid;
    return testFailures ? 1 : 0;
}
```

- [ ] **Step 2: Verify the test fails for the missing API**

Run:

```powershell
rtk cmake -S . -B build -DCMAKE_CONFIGURATION_TYPES="Debug;Release" -DWCW_BUILD_DEMO=ON -DBUILD_TESTING=ON
rtk cmake --build build --config Debug --target MenuBarTests
```

Expected: compilation fails on `ParseMenuLabel`, `ValidMenuBarItems`, or `MenuBarOptions`.

- [ ] **Step 3: Add the public declarations and minimal model helpers**

Place `MenuBarOptions` beside `MenuButtonOptions`, and the functions beside the other menu entry points:

```cpp
struct MenuBarOptions : ControlOptions {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
};

HWND CreateMenuBar(const MenuBarOptions& options);
bool SetMenuBarItems(HWND menuBar, const std::vector<MenuItem>& items);
```

Declare in `src/MenuModel.h`:

```cpp
struct MenuLabel {
    std::wstring text;
    wchar_t mnemonic{};
};

MenuLabel ParseMenuLabel(std::wstring_view value);
bool ValidMenuBarItems(std::span<const MenuItem> items);
```

Implement `ParseMenuLabel` as one pass: `&&` appends one literal ampersand,
the first single ampersand records `std::towlower` of the following character,
and a trailing ampersand is literal. Implement `ValidMenuBarItems` by rejecting
an empty span, top-level separators, or labels whose parsed text is empty, then
calling `ValidMenuItems(items)` once for recursive IDs and children.

```cpp
bool ValidMenuBarItems(std::span<const MenuItem> items) {
    if (items.empty() || !ValidMenuItems(items)) return false;
    return std::ranges::all_of(items, [](const MenuItem& item) {
        return !item.separator && !ParseMenuLabel(item.text).text.empty();
    });
}
```

- [ ] **Step 4: Build and run the pure model tests**

Run:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests
rtk ctest --test-dir build -C Debug -R "MenuBarTests" --output-on-failure
```

Expected: `MenuBarTests` passes.

- [ ] **Step 5: Commit the model/API slice**

```powershell
rtk git add include/wcw/Controls.h src/MenuModel.h src/MenuModel.cpp tests/test_menu_bar.cpp CMakeLists.txt
rtk git commit -m "feat: add menu bar model API"
```

---

### Task 2: Menu Bar Window, Painting, Mouse Input, and Mutation

**Ownership:** `src/MenuBar.cpp`, plus menu color and registration changes in `src/Internal.h`, `src/Menu.cpp`, `src/Runtime.cpp`, `CMakeLists.txt`, and window tests in `tests/test_menu_bar.cpp`.

**Files:**
- Create: `src/MenuBar.cpp`
- Modify: `src/Internal.h`
- Modify: `src/Menu.cpp`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/test_menu_bar.cpp`

**Interfaces:**
- Consumes: Task 1 `ValidMenuBarItems`, `ParseMenuLabel`, public `MenuBarOptions`, existing paint/runtime helpers, `ShowPopupMenu`, and `CancelPopupMenu`.
- Produces: `RegisterMenuBarClass()`, a registered `WcwMenuBar` child control, working `CreateMenuBar`/`SetMenuBarItems`, and `MenuBarHitTestMessage`.

- [ ] **Step 1: Add failing creation, hit-test, command, popup, and mutation checks**

Extend `tests/test_menu_bar.cpp` with an initialized test dialog whose procedure
records `LOWORD(wParam)` and `lParam` from `WM_COMMAND`. Use these item trees:

```cpp
const std::vector<wcw::MenuItem> firstItems{
    {.text = L"&File", .children = {{.id = 201, .text = L"Open"}}},
    {.id = 202, .text = L"&Help"},
};
const std::vector<wcw::MenuItem> replacementItems{
    {.id = 203, .text = L"&About"},
};
```

Add checks that:

```cpp
wcw::MenuBarOptions options;
options.parent = dialog;
options.id = 77;
options.bounds = {0, 0, 320, 32};
options.style = WS_VISIBLE;
options.items = firstItems;
const HWND bar = wcw::CreateMenuBar(options);
CHECK(bar != nullptr);

POINT firstPoint{8, 16};
CHECK(SendMessageW(bar, wcw::internal::MenuBarHitTestMessage, 0,
                   reinterpret_cast<LPARAM>(&firstPoint)) == 0);
int secondX = -1;
for (int x = 0; x < 320; ++x) {
    POINT point{x, 16};
    if (SendMessageW(bar, wcw::internal::MenuBarHitTestMessage, 0,
                     reinterpret_cast<LPARAM>(&point)) == 1) {
        secondX = x;
        break;
    }
}
CHECK(secondX >= 0);
SendMessageW(bar, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(secondX, 16));
SendMessageW(bar, WM_LBUTTONUP, 0, MAKELPARAM(secondX, 16));
CHECK(commandId == 202);
CHECK(commandSource == reinterpret_cast<LPARAM>(bar));

CHECK(!wcw::SetMenuBarItems(bar, {}));
CHECK(wcw::SetMenuBarItems(bar, replacementItems));
```

Use a short owner timer to send `VK_ESCAPE` to `FindWindowW(L"WcwMenuPopup", nullptr)` after clicking the first item, so the nested popup loop exits deterministically. Also check invalid parent, invalid items, the wrong control class passed to `SetMenuBarItems`, `UpdateWindow(bar)`, disabling the bar, and destroying the parent while a popup is open.

- [ ] **Step 2: Verify the window tests fail before registration exists**

Run:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests
```

Expected: linking fails on `CreateMenuBar`/`SetMenuBarItems`, or initialization cannot register `WcwMenuBar`.

- [ ] **Step 3: Share menu colors instead of duplicating them**

Move the private `Colors` shape and `MenuColors` logic in `src/Menu.cpp` behind
these internal declarations in `src/Internal.h`:

```cpp
struct MenuColors {
    ResolvedStyle style;
    Color selectedText;
};

MenuColors ResolveMenuColors(const StyleOverride& appearance);
```

Keep the current panel-background default and High Contrast branches byte-for-byte equivalent. Replace the three popup call sites with `ResolveMenuColors` and use the same function from `MenuBar.cpp`.

- [ ] **Step 4: Register the new child-window class**

Add to `src/Internal.h`:

```cpp
bool RegisterMenuBarClass();
inline constexpr UINT MenuBarSetItemsMessage = WM_APP + 0x58B;
inline constexpr UINT MenuBarHitTestMessage = WM_APP + 0x58C;
```

Add `src/MenuBar.cpp` to `Win32CustomWidgets` in `CMakeLists.txt`, and append
`internal::RegisterMenuBarClass()` to the registration chain in
`Initialize(HINSTANCE)`.

- [ ] **Step 5: Implement the minimal menu bar state and layout**

Use one state object owned by `GWLP_USERDATA`:

```cpp
struct MenuBarState {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
    std::vector<RECT> itemBounds;
    int hot{-1};
    int active{-1};
    bool pressed{};
    bool popupOpen{};
};
```

Measure parsed labels with the resolved font. Build left-to-right rectangles
using `paddingXDip` on each side and the client height. `HitTest` returns `-1`
outside all rectangles. Recalculate on creation, `WM_SIZE`, `WM_DPICHANGED`,
theme changes, font/style changes observed through the existing shared control
message path, and item replacement.

- [ ] **Step 6: Paint and handle mouse input**

Paint the panel background once, then each item label. Use `hover` for `hot`,
`selected` for `active`/`popupOpen`, `disabledText` for disabled entries, and
draw mnemonic cues with Win32 prefix semantics. Handle `WM_MOUSEMOVE`,
`WM_MOUSELEAVE`, `WM_LBUTTONDOWN`, `WM_LBUTTONUP`, `WM_CAPTURECHANGED`,
`WM_ENABLE`, `WM_CANCELMODE`, and `WM_PAINT`.

Use a single activation function:

```cpp
void Activate(HWND bar, MenuBarState& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.items.size())) return;
    const auto& item = state.items[index];
    if (!item.enabled) return;
    if (item.children.empty()) {
        SendMessageW(GetParent(bar), WM_COMMAND, MAKEWPARAM(item.id, 0),
                     reinterpret_cast<LPARAM>(bar));
        return;
    }
    RECT anchor = state.itemBounds[index];
    MapWindowPoints(bar, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    state.popupOpen = true;
    state.active = index;
    InvalidateRect(bar, nullptr, FALSE);
    internal::ShowPopupMenu(GetParent(bar), bar, anchor, item.children,
                            state.menuAppearance);
    if (!IsWindow(bar)) return;
    state.popupOpen = false;
    InvalidateRect(bar, nullptr, FALSE);
}
```

Task 3 replaces only this popup call with the switching wrapper.

- [ ] **Step 7: Implement public creation and safe mutation**

`CreateMenuBar` validates the parent and `ValidMenuBarItems`, converts DIP
bounds, creates `WcwMenuBar` with `WS_CHILD | WS_TABSTOP | options.style`,
copies the state through `WM_NCCREATE`, registers the window, and applies
`options.appearance`. `SetMenuBarItems` validates the class and new tree before
calling `CancelPopupMenu(menuBar)` and the synchronous set-items message.

- [ ] **Step 8: Run focused and popup regression tests**

```powershell
rtk cmake --build build --config Debug --target MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(MenuBarTests|MenuTests)" --output-on-failure
```

Expected: both tests pass and no popup remains after either executable exits.

- [ ] **Step 9: Commit the visual/mouse slice**

```powershell
rtk git add src/MenuBar.cpp src/Internal.h src/Menu.cpp src/Runtime.cpp tests/test_menu_bar.cpp CMakeLists.txt
rtk git commit -m "feat: add themed menu bar control"
```

---

### Task 3: Popup Switching and Standard Keyboard Mode

**Ownership:** root switching in `src/Menu.cpp`, menu-mode orchestration in `src/MenuBar.cpp`, internal declarations in `src/Internal.h`, and interaction tests in `tests/test_menu_bar.cpp` plus regressions in `tests/test_menu.cpp`.

**Files:**
- Modify: `src/Internal.h`
- Modify: `src/Menu.cpp`
- Modify: `src/MenuBar.cpp`
- Modify: `tests/test_menu_bar.cpp`
- Modify: `tests/test_menu.cpp`

**Interfaces:**
- Consumes: Task 2 menu-bar window, hit testing, and existing popup controller.
- Produces: `PopupMenuResult`, `ShowMenuBarPopup`, parent system-key subclassing, `Alt/F10`, mnemonic, wrapping arrow, hover-switch, and focus-restoration behavior.

- [ ] **Step 1: Add failing keyboard and switching scenarios**

Extend the dialog test with three top-level entries: enabled `&File`, disabled
`&Edit`, and enabled `&View`. Record the original focused child. Add timer-based
callbacks which inspect `FindWindowW(L"WcwMenuPopup", nullptr)` and send keys.
Cover these exact sequences:

```cpp
SendMessageW(dialog, WM_KEYDOWN, VK_F10, 0);
CHECK(GetFocus() == bar);
SendMessageW(bar, WM_KEYDOWN, VK_RIGHT, 0); // skips disabled Edit
SendMessageW(bar, WM_KEYDOWN, VK_DOWN, 0);  // opens View
```

```cpp
SendMessageW(dialog, WM_SYSCHAR, L'f', 1L << 29); // opens File
```

While File is open, a timer sends `VK_RIGHT` to the popup and verifies that the
replacement popup is anchored below View. A second timer maps a point inside
View into the captured popup and sends `WM_MOUSEMOVE`, verifying hover switching.
Other checks cover repeat-click dismissal, `Escape`, another `Alt`, nested
submenu `Left` before root switching, and restoration of the original focus.

Add one regression in `tests/test_menu.cpp`: open a normal menu button, send
root `VK_RIGHT`, and verify it still stays within its current popup semantics
instead of attempting menu-bar switching.

- [ ] **Step 2: Verify the new behavior tests fail**

```powershell
rtk cmake --build build --config Debug --target MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(MenuBarTests|MenuTests)" --output-on-failure
```

Expected: MenuBar keyboard/switching assertions fail; the existing MenuTests remain green.

- [ ] **Step 3: Add an internal switching result without changing the public API**

Declare in `src/Internal.h`:

```cpp
struct PopupMenuResult {
    bool shown{};
    int nextTopIndex{-1};
};

PopupMenuResult ShowMenuBarPopup(HWND commandTarget, HWND source, RECT anchor,
                                 std::vector<MenuItem> items,
                                 const StyleOverride& appearance,
                                 HWND menuBar, int topIndex);
inline constexpr UINT MenuBarNextItemMessage = WM_APP + 0x58D;
```

Refactor the common popup construction and command dispatch into one private
runner. Keep `ShowPopupMenu(...) -> bool` as its compatibility wrapper.
`ShowMenuBarPopup` supplies the optional bar and active top index and returns
the requested replacement index.

- [ ] **Step 4: Teach the popup controller about only its menu-bar host**

Add `HWND menuBar_`, `int topIndex_`, and `int nextTopIndex_` to
`PopupController`. At root level:

- `VK_LEFT` and `VK_RIGHT` synchronously ask `MenuBarNextItemMessage` for the
  previous/next enabled index, store it, and cancel the current popup;
- pointer/mouse movement over another enabled bar item stores that hit index and
  cancels, while movement over the current item keeps the popup;
- a button release over the current item cancels, and over another item switches;
- `VK_MENU` cancels menu mode;
- child-level `Left` closes the child exactly as it does now;
- `Run()` treats points inside the host bar as part of the active interaction,
  so pointer events are not deferred as unrelated outside clicks.

Use synchronous `SendMessageW` with a `POINT*` for full signed screen
coordinates; do not pack monitor coordinates into a 16-bit `LPARAM`.

- [ ] **Step 5: Loop across top-level popups in the bar**

Replace Task 2's one-shot popup call with:

```cpp
int next = index;
while (next >= 0 && IsWindow(bar)) {
    state.active = next;
    const auto& activeItem = state.items[next];
    RECT anchor = state.itemBounds[next];
    MapWindowPoints(bar, nullptr, reinterpret_cast<POINT*>(&anchor), 2);
    const auto result = internal::ShowMenuBarPopup(
        GetParent(bar), bar, anchor, activeItem.children,
        state.menuAppearance, bar, next);
    next = result.nextTopIndex;
}
```

Guard state access after every nested popup run because command handlers may
destroy the bar or replace its items.

- [ ] **Step 6: Add parent system-key routing and menu mode**

Install one `SetWindowSubclass` entry on the parent with the menu bar `HWND` as
the subclass ID/reference. Remove it during `WM_NCDESTROY`. Route:

- `WM_KEYDOWN/VK_F10` to activate the bar;
- `WM_SYSKEYDOWN` and `WM_SYSKEYUP` for Alt-alone toggling;
- `WM_SYSCHAR` to `ParseMenuLabel(...).mnemonic` matching;
- parent deactivation/destruction to cancellation.

The bar saves `GetFocus()` when menu mode starts, calls `SetFocus(bar)`, shows
keyboard cues, selects the first enabled item with `NextMenuIndex`, and emits
invalidations. Its own `WM_KEYDOWN` handles wrapping `Left/Right`, activation
with `Down/Enter/Space`, and cancellation with `Escape`. Cancellation restores
the saved focus only if `IsWindow(savedFocus)`.

- [ ] **Step 7: Run interaction and regression tests**

```powershell
rtk cmake --build build --config Debug --target MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(MenuBarTests|MenuTests)" --output-on-failure
```

Expected: keyboard, mouse-switch, nested-menu, focus, menu-button, and context-menu cases pass.

- [ ] **Step 8: Commit the interaction slice**

```powershell
rtk git add src/Internal.h src/Menu.cpp src/MenuBar.cpp tests/test_menu_bar.cpp tests/test_menu.cpp
rtk git commit -m "feat: add standard menu bar navigation"
```

---

### Task 4: Menu Bar Accessibility

**Ownership:** MSAA model/provider changes in `src/Accessibility.h` and `src/Accessibility.cpp`, bar integration in `src/MenuBar.cpp`, and accessibility assertions in `tests/test_accessibility.cpp`.

**Files:**
- Modify: `src/Accessibility.h`
- Modify: `src/Accessibility.cpp`
- Modify: `src/MenuBar.cpp`
- Modify: `tests/test_accessibility.cpp`

**Interfaces:**
- Consumes: existing `AccessibleMenuItem`, `RegisterMenuAccessibility`, `UpdateMenuAccessibility`, `HandleAccessibilityMessage`, and Task 3 menu state.
- Produces: `AccessibleKind::MenuBar`, `RegisterMenuBarAccessibility`, `UpdateMenuBarAccessibility`, menu-bar roles/states/default actions, and accessibility events.

- [ ] **Step 1: Add failing MSAA assertions**

Create a menu bar in `tests/test_accessibility.cpp`, obtain `IAccessible` through
`AccessibleObjectFromWindow(bar, OBJID_CLIENT, IID_IAccessible, ...)`, and check:

```cpp
VARIANT self = Child(CHILDID_SELF);
VARIANT role{};
CHECK(accessible->get_accRole(self, &role) == S_OK);
CHECK(role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_MENUBAR);
VariantClear(&role);

long count{};
CHECK(accessible->get_accChildCount(&count) == S_OK);
CHECK(count == 3);

BSTR name{};
CHECK(accessible->get_accName(Child(1), &name) == S_OK);
CHECK(name && wcscmp(name, L"File") == 0);
SysFreeString(name);
```

Activate the bar and check `STATE_SYSTEM_FOCUSABLE`, `STATE_SYSTEM_FOCUSED`,
`STATE_SYSTEM_HASPOPUP`, `STATE_SYSTEM_EXPANDED/COLLAPSED`, disabled state,
`get_accFocus`, hit testing, screen bounds, default action (`Open` or `Execute`),
and `accDoDefaultAction`. Reuse the existing WinEvent hook to assert focus and
state-change notifications.

- [ ] **Step 2: Verify accessibility tests fail**

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests
rtk ctest --test-dir build -C Debug -R "AccessibilityTests" --output-on-failure
```

Expected: the new control does not yet expose `ROLE_SYSTEM_MENUBAR` or children.

- [ ] **Step 3: Generalize the existing menu accessibility storage minimally**

Add `MenuBar` to `AccessibleKind` and `expanded` to `AccessibleMenuItem`:

```cpp
struct AccessibleMenuItem {
    std::wstring text;
    RECT screenBounds{};
    int row{};
    bool enabled{};
    bool checked{};
    bool hasPopup{};
    bool offscreen{};
    bool expanded{};
};
```

Add wrappers:

```cpp
void RegisterMenuBarAccessibility(HWND window,
                                  std::vector<AccessibleMenuItem> items);
void UpdateMenuBarAccessibility(HWND window,
                                std::vector<AccessibleMenuItem> items,
                                int focusedChild);
```

Back both popup and bar registration with one private helper taking
`AccessibleKind`. Permit `UpdateMenuAccessibility` only for `Menu`, and the new
wrapper only for `MenuBar`.

- [ ] **Step 4: Extend provider roles and item states**

Return `ROLE_SYSTEM_MENUBAR` for self when kind is `MenuBar`; keep
`ROLE_SYSTEM_MENUPOPUP` for popup self and `ROLE_SYSTEM_MENUITEM` for children.
Treat both menu kinds as child collections for name, role, location, hit test,
focus, and default action. When a child has a popup, add
`STATE_SYSTEM_EXPANDED` or `STATE_SYSTEM_COLLAPSED` from its `expanded` field.
Handle the existing `MenuActivateAccessibleMessage` in `MenuBar.cpp` by
activating the one-based child index.

- [ ] **Step 5: Synchronize accessible children from bar state**

Build one `AccessibleMenuItem` per top-level item from parsed text, mapped
screen bounds, enabled state, `hasPopup = !children.empty()`, and
`expanded = popupOpen && active == index`. Call registration after successful
window creation and update after layout, focus/selection, open/close, enable,
theme/DPI relocation, and item replacement. Emit `EVENT_OBJECT_FOCUS` with the
one-based child ID and `EVENT_OBJECT_STATECHANGE` when expanded state changes.

- [ ] **Step 6: Run accessibility and menu regressions**

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(AccessibilityTests|MenuBarTests|MenuTests)" --output-on-failure
```

Expected: all three executables pass.

- [ ] **Step 7: Commit the accessibility slice**

```powershell
rtk git add src/Accessibility.h src/Accessibility.cpp src/MenuBar.cpp tests/test_accessibility.cpp
rtk git commit -m "feat: expose menu bar accessibility"
```

---

### Task 5: Complete Demo Gallery and Consumer Documentation

**Ownership:** `demo/main.cpp` and `README.md` only.

**Files:**
- Modify: `demo/main.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: completed public `MenuBarOptions`, `CreateMenuBar`, and all existing widget APIs.
- Produces: fixed top demo menu, command examples, preserved full widget gallery, and consumer-facing usage documentation.

- [ ] **Step 1: Add the menu bar to the demo state and item tree**

Add `HWND menuBar{}` to `Gallery` and these command IDs:

```cpp
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
```

Create this representative tree with `File`, `Edit`, `View`, and `Help`:

```cpp
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
```

- [ ] **Step 2: Create and pin the bar without replacing the gallery**

Create `MenuBarOptions` in `CreateGallery`, set its bounds to the full client
width and a 32-DIP height, and require `g.menuBar` in the final creation check.
In `Layout`, keep the bar at `{0, 0, width, 32}` and add a constant 32-DIP
vertical offset to every existing top/content coordinate. Preserve the status
label at the bottom and reduce the scroll-view height by the same offset.

- [ ] **Step 3: Wire commands to visible demo behavior**

Handle menu IDs before the generic button branch in `WM_COMMAND`:

- ordinary commands call `SetStatus` with the selected action;
- dark/light commands update `gallery->lightTheme`, call `SetMenuBarItems` with
  `ApplicationMenu(gallery->lightTheme)`, and then call `ApplyAppearance`;
- reset sets `radiusDip` and the radius slider to the default before applying;
- exit posts `WM_CLOSE`;
- every path returns `0`.

Keep the existing right-click `ShowContextMenu`, `MenuButton`, tooltip,
scroll-view, and every current `Create*` call intact.

- [ ] **Step 4: Document consumption from another application**

Add a short menu-bar example in `README.md` near the existing Controls section:

```cpp
wcw::MenuBarOptions menu;
menu.parent = window;
menu.bounds = {0, 0, 800, 32};
menu.style = WS_VISIBLE;
menu.items = {
    {.text = L"&File", .children = {
        {.id = 1001, .text = L"E&xit"},
    }},
};
HWND menuBar = wcw::CreateMenuBar(menu);
```

State that commands arrive through `WM_COMMAND`, `lParam` identifies the bar,
and theme changes follow `SetTheme` automatically.

- [ ] **Step 5: Build the demo and run focused tests**

```powershell
rtk cmake --build build --config Debug --target Win32CustomWidgetsDemo MenuBarTests
rtk ctest --test-dir build -C Debug -R "MenuBarTests" --output-on-failure
```

Expected: the demo links and the menu-bar tests pass.

- [ ] **Step 6: Commit demo and documentation**

```powershell
rtk git add demo/main.cpp README.md
rtk git commit -m "demo: showcase custom menu bar"
```

---

### Task 6: Full Verification and Release Executable

**Ownership:** verification only; modify source only for failures proven to originate in Tasks 1-5, and keep any such fix in the owning task's files.

**Files:**
- Verify: all modified files
- Produce: `build/bin/Release/Win32CustomWidgetsDemo.exe`

**Interfaces:**
- Consumes: the complete implementation and project CMake targets.
- Produces: clean Debug/Release builds, complete CTest evidence, and a smoke-tested Release executable.

- [ ] **Step 1: Check the final diff and configuration**

```powershell
rtk git status --short
rtk git diff --check HEAD~5..HEAD
rtk cmake -S . -B build -DCMAKE_CONFIGURATION_TYPES="Debug;Release" -DWCW_BUILD_DEMO=ON -DBUILD_TESTING=ON
```

Expected: only the pre-existing `.codegraph/` and `.codebase-memory/` paths may
remain untracked; the CMake generation succeeds.

- [ ] **Step 2: Build and test Debug**

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug --output-on-failure
```

Expected: every target builds and every CTest passes.

- [ ] **Step 3: Build and test Release**

```powershell
rtk cmake --build build --config Release
rtk ctest --test-dir build -C Release --output-on-failure
```

Expected: every target builds and every CTest passes.

- [ ] **Step 4: Smoke-launch the Release gallery**

Use PowerShell to start the exact executable, wait until its main window is
created, verify the process is alive, then close it normally with `WM_CLOSE` or
the demo's File > Exit item. Do not leave the process running.

```powershell
rtk powershell -NoProfile -Command "$p = Start-Process -FilePath '.\build\bin\Release\Win32CustomWidgetsDemo.exe' -PassThru; Start-Sleep -Milliseconds 1200; if ($p.HasExited) { throw 'Demo exited during smoke test' }; $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(5000)) { throw 'Demo did not close normally' }"
```

Expected: exit code `0` and no lingering `Win32CustomWidgetsDemo` process.

- [ ] **Step 5: Record artifact and repository state**

```powershell
rtk powershell -NoProfile -Command "$exe = Resolve-Path '.\build\bin\Release\Win32CustomWidgetsDemo.exe'; Get-Item -LiteralPath $exe | Select-Object FullName,Length,LastWriteTime"
rtk git status --short
rtk git log -6 --oneline
```

Expected artifact: `F:\Data\Code\C++\win32-custom-widgets\build\bin\Release\Win32CustomWidgetsDemo.exe`.
