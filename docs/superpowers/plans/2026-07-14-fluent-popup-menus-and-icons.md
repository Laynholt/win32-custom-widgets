# Fluent Popup Menus and Built-in Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add correctly padded labels, Fluent custom context menus and menu buttons with nested submenus, and DPI-aware built-in information, warning, and error icons.

**Architecture:** Keep label and icon rendering in the existing display/paint path. Put only recursive validation, selection navigation, and monitor-bound placement in a small internal menu model; one `Menu.cpp` owns every popup level and its modal loop. Extend the existing button implementation with menu state and a chevron instead of creating a second button control.

**Tech Stack:** C++20, Win32 windowing/messages, GDI/GDI+, MSAA (`IAccessible`), CMake/CTest.

## Global Constraints

- Preserve the public API approved in `docs/superpowers/specs/2026-07-14-fluent-popup-menus-and-icons-design.md`.
- Use no new dependency and no DirectComposition/acrylic effect.
- Keep native `HICON` and `HBITMAP` behavior source-compatible.
- Dispatch a selected ID only after closing every popup window.
- Use `WM_COMMAND` with IDs in the range 1 through 65535.
- Keep keyboard operation and MSAA states available without pointer hover.
- Use existing `StyleOverride`, `ResolvedStyle`, `paint::Buffer`, and runtime class registration.
- Prefix every repository shell command with `rtk`.

---

### Task 1: Pad and vertically center all labels

**Files:**
- Modify: `src/Display.cpp:57-90`
- Test: `tests/test_button_display.cpp`

**Interfaces:**
- Consumes: `ResolvedStyle::paddingXDip`, `ResolvedStyle::paddingYDip`, `paint::Font`, `paint::Text`.
- Produces: one shared label layout path used by labels inside and outside scroll views.

- [ ] **Step 1: Add a rendering regression test**

Create a 120-by-48 label in `tests/test_button_display.cpp` with black background, white foreground, `paddingXDip = 12`, `paddingYDip = 8`, and the text `L"Centered"`. Redraw it, scan the rendered pixels, and assert that the first non-background text pixel is at least 10 pixels from each horizontal edge and that the top and bottom empty areas differ by no more than two pixels. Add a 70-by-56 wrapped label and assert the same vertical balance for its multi-line text block.

```cpp
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
```

Use a local scan helper returning the non-background pixel bounds; it must return an empty `RECT` when no foreground pixel is found so the test can fail explicitly.

- [ ] **Step 2: Run the focused test and confirm RED**

Run:

```powershell
rtk cmake --build build-verify --config Debug --target ButtonDisplayTests
rtk ctest --test-dir build-verify -C Debug -R ButtonDisplayTests --output-on-failure
```

Expected: the target builds, then `ButtonDisplayTests` fails because label text still reaches the unpadded bounds and multiline `DT_VCENTER` does not center the measured block.

- [ ] **Step 3: Measure and center the text block once in `PaintDisplay`**

Add a local helper in `src/Display.cpp` and use the returned bounds with `DT_TOP`, not `DT_VCENTER`:

```cpp
RECT LabelTextBounds(HDC dc, RECT bounds, HFONT font, const ResolvedStyle& style,
                     unsigned dpi, std::wstring_view text) {
    const int paddingX = (std::max)(0, DipToPx(style.paddingXDip, dpi));
    const int paddingY = (std::max)(0, DipToPx(style.paddingYDip, dpi));
    RECT content{bounds.left + paddingX, bounds.top + paddingY,
                 (std::max)(bounds.left + paddingX, bounds.right - paddingX),
                 (std::max)(bounds.top + paddingY, bounds.bottom - paddingY)};
    RECT measured{0, 0, content.right - content.left, 0};
    const auto old = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &measured,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
    const int height = (std::min)(measured.bottom - measured.top,
                                  content.bottom - content.top);
    content.top += (content.bottom - content.top - height) / 2;
    content.bottom = content.top + height;
    return content;
}
```

Call `paint::Text` with `DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX`. Keep clipping inside the measured padded rectangle.

- [ ] **Step 4: Run GREEN and commit**

```powershell
rtk cmake --build build-verify --config Debug --target ButtonDisplayTests
rtk ctest --test-dir build-verify -C Debug -R ButtonDisplayTests --output-on-failure
rtk git add src/Display.cpp tests/test_button_display.cpp
rtk git commit -m "fix: pad and center label text"
```

Expected: `ButtonDisplayTests` passes and the commit succeeds.

---

### Task 2: Add built-in Fluent outline icons

**Files:**
- Modify: `include/wcw/Controls.h:84-93`
- Modify: `src/Paint.h`
- Modify: `src/Paint.cpp:107-125`
- Modify: `src/Display.cpp:26-40,90-103`
- Modify: `src/ComboBox.cpp` at its image drawing sites
- Test: `tests/test_button_display.cpp`

**Interfaces:**
- Produces: `BuiltinIcon`, `ImageSource(BuiltinIcon)`, and `paint::Image(HDC, ImageSource, const RECT&, Color)`.
- Consumes later: menu rows use `paint::Image` for native and built-in sources.

- [ ] **Step 1: Add API and pixel tests that fail to compile**

Append three image views to `tests/test_button_display.cpp`, one per built-in icon. Give each a white foreground and black background, redraw, and assert that every center region contains background, foreground, and at least one intermediate anti-aliased pixel while every corner remains background.

```cpp
const std::array builtinIcons{wcw::BuiltinIcon::Information,
                              wcw::BuiltinIcon::Warning,
                              wcw::BuiltinIcon::Error};
for (size_t index = 0; index < builtinIcons.size(); ++index) {
    base.id = 60 + static_cast<int>(index);
    base.bounds = {static_cast<float>(index * 28), 130, 24, 24};
    base.appearance = {.background = wcw::Color::FromRgb(0, 0, 0),
                       .foreground = wcw::Color::FromRgb(255, 255, 255),
                       .cornerRadiusDip = 0.0f};
    const auto view = wcw::CreateImageView(base, wcw::ImageSource(builtinIcons[index]));
    CHECK(view != nullptr);
}
```

- [ ] **Step 2: Run the focused build and confirm RED**

```powershell
rtk cmake --build build-verify --config Debug --target ButtonDisplayTests
```

Expected: compilation fails because `BuiltinIcon` and its `ImageSource` constructor do not exist.

- [ ] **Step 3: Extend `ImageSource` without changing native handle ownership**

Use this public representation:

```cpp
enum class BuiltinIcon { Information, Warning, Error };

struct ImageSource {
    enum class Kind { None, Icon, Bitmap, Builtin };

    Kind kind{Kind::None};
    HANDLE handle{};
    BuiltinIcon builtin{BuiltinIcon::Information};

    ImageSource() = default;
    ImageSource(HICON icon) : kind(Kind::Icon), handle(icon) {}
    ImageSource(HBITMAP bitmap) : kind(Kind::Bitmap), handle(bitmap) {}
    ImageSource(BuiltinIcon icon) : kind(Kind::Builtin), builtin(icon) {}
};
```

Do not add ownership or destruction logic; built-ins own no handle and existing handles remain caller-owned.

- [ ] **Step 4: Add one unified paint entry point**

Declare and implement:

```cpp
void Image(HDC dc, ImageSource source, const RECT& bounds, Color foreground);
```

Dispatch native sources to the existing `Icon` and `Bitmap` functions. For built-ins, create `Gdiplus::Graphics`, enable `SmoothingModeAntiAlias`, use a round-cap/round-join pen sized to `(std::max)(1.5f, width / 12.0f)`, and draw inside a one-pen inset square:

- Information: outline circle, small filled dot, vertical stem.
- Warning: closed triangular `GraphicsPath` with round joins, vertical stem, small filled dot.
- Error: outline circle and two diagonal strokes.

Update `Display.cpp` so `ImageSize` returns `{16, 16}` for a built-in and `PaintDisplay` calls `paint::Image`. Replace the duplicated icon/bitmap branch in `ComboBox.cpp` with the same function and its resolved text color.

- [ ] **Step 5: Run GREEN and commit**

```powershell
rtk cmake --build build-verify --config Debug --target ButtonDisplayTests ComboTests
rtk ctest --test-dir build-verify -C Debug -R "ButtonDisplayTests|ComboTests" --output-on-failure
rtk git add include/wcw/Controls.h src/Paint.h src/Paint.cpp src/Display.cpp src/ComboBox.cpp tests/test_button_display.cpp
rtk git commit -m "feat: add fluent built-in icons"
```

Expected: both focused tests pass and the commit succeeds.

---

### Task 3: Add menu data, validation, navigation, and placement

**Files:**
- Modify: `include/wcw/Controls.h`
- Create: `src/MenuModel.h`
- Create: `src/MenuModel.cpp`
- Create: `tests/test_menu.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces public: `MenuItem`, `ContextMenuOptions`, `MenuButtonOptions`, `ShowContextMenu`, `CreateMenuButton`, `SetMenuItems`.
- Produces internal: `ValidMenuItems`, `NextMenuIndex`, `EdgeMenuIndex`, `PlaceRootMenu`, `PlaceSubmenu`.
- Consumes: `MenuItem` only; the model has no HWND or painting dependency.

- [ ] **Step 1: Add model tests and register `MenuTests`**

Add `tests/test_menu.cpp` with checks for recursive validation, skipped separators/disabled rows, wrapped Up/Down navigation, Home/End, and both popup placement directions:

```cpp
const std::vector<wcw::MenuItem> valid{
    {.id = 1, .text = L"Open"},
    {.separator = true},
    {.text = L"More", .children = {{.id = 2, .text = L"Nested"}}},
    {.id = 0, .text = L"Unavailable", .enabled = false},
};
CHECK(wcw::internal::ValidMenuItems(valid));
const std::vector<wcw::MenuItem> empty;
const std::vector<wcw::MenuItem> zeroId{{.id = 0, .text = L"Bad"}};
const std::vector<wcw::MenuItem> largeId{{.id = 65536, .text = L"Bad"}};
CHECK(!wcw::internal::ValidMenuItems(empty));
CHECK(!wcw::internal::ValidMenuItems(zeroId));
CHECK(!wcw::internal::ValidMenuItems(largeId));
CHECK(wcw::internal::NextMenuIndex(valid, -1, 1) == 0);
CHECK(wcw::internal::NextMenuIndex(valid, 0, 1) == 2);
CHECK(wcw::internal::NextMenuIndex(valid, 2, 1) == 0);

const RECT work{0, 0, 800, 600};
const auto sameRect = [](RECT left, RECT right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
};
CHECK(sameRect(wcw::internal::PlaceRootMenu({760, 580, 760, 580}, {120, 100}, work),
               {680, 480, 800, 580}));
CHECK(sameRect(wcw::internal::PlaceSubmenu({700, 400, 780, 432}, {160, 120}, work),
               {540, 400, 700, 520}));
```

Register `src/MenuModel.cpp` in the library and add:

```cmake
wcw_add_test(MenuTests tests/test_menu.cpp)
```

- [ ] **Step 2: Run the new target and confirm RED**

```powershell
rtk cmake -S . -B build-verify
rtk cmake --build build-verify --config Debug --target MenuTests
```

Expected: compilation fails because the public menu types and internal model do not exist.

- [ ] **Step 3: Add the approved public declarations**

Add the exact structs and functions from the design specification. Keep field order stable so designated initializers in the demo and tests remain readable:

```cpp
struct MenuItem {
    int id{};
    std::wstring text;
    std::wstring shortcut;
    ImageSource image;
    std::vector<MenuItem> children;
    bool enabled{true};
    bool checked{};
    bool separator{};
};

struct ContextMenuOptions {
    std::vector<MenuItem> items;
    StyleOverride appearance;
};

struct MenuButtonOptions : ButtonOptions {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
};
```

- [ ] **Step 4: Implement the minimal pure menu model**

Declare these exact signatures in `src/MenuModel.h`:

```cpp
bool ValidMenuItems(std::span<const MenuItem> items);
int NextMenuIndex(std::span<const MenuItem> items, int current, int direction);
int EdgeMenuIndex(std::span<const MenuItem> items, bool end);
RECT PlaceRootMenu(RECT anchor, SIZE popup, RECT workArea);
RECT PlaceSubmenu(RECT parentRow, SIZE popup, RECT workArea);
```

Validation rules are: root and child vectors are non-empty; every non-separator has non-empty text; an enabled leaf has ID 1 through 65535; a parent may use ID zero because it never dispatches; separators ignore every other field. Navigation treats enabled non-separators as eligible and wraps. Placement first uses below/right, flips above/left on overflow, then clamps both axes to the work area.

- [ ] **Step 5: Run GREEN and commit**

```powershell
rtk cmake --build build-verify --config Debug --target MenuTests
rtk ctest --test-dir build-verify -C Debug -R MenuTests --output-on-failure
rtk git add CMakeLists.txt include/wcw/Controls.h src/MenuModel.h src/MenuModel.cpp tests/test_menu.cpp
rtk git commit -m "feat: add menu model and public API"
```

Expected: `MenuTests` passes and the commit succeeds. Undefined public menu functions are acceptable until Task 4 because this target does not call them yet.

---

### Task 4: Implement custom popup menus and nested submenus

**Files:**
- Create: `src/Menu.cpp`
- Modify: `src/Internal.h`
- Modify: `src/Runtime.cpp:70-90`
- Modify: `CMakeLists.txt`
- Test: `tests/test_menu.cpp`

**Interfaces:**
- Produces public: `bool ShowContextMenu(HWND, POINT, const ContextMenuOptions&)`.
- Produces internal: `bool ShowPopupMenu(HWND commandTarget, HWND source, RECT anchor, std::vector<MenuItem> items, const StyleOverride&)`, `void CancelPopupMenu(HWND source)`, and `bool RegisterMenuClass()`.
- Consumes: all Task 3 model functions and Task 2 `paint::Image`.

- [ ] **Step 1: Add blocking popup behavior tests**

Add a test parent that records `LOWORD(wParam)`, `HIWORD(wParam)`, and `lParam` for `WM_COMMAND`. Use `SetTimer` before calling `ShowContextMenu`; the timer callback finds `L"WcwMenuPopup"` and sends keys while the nested loop is active.

```cpp
void CALLBACK SelectSecond(HWND owner, UINT, UINT_PTR timer, DWORD) {
    KillTimer(owner, timer);
    const auto popup = FindWindowW(L"WcwMenuPopup", nullptr);
    CHECK(popup != nullptr);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    SendMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
}

wcw::ContextMenuOptions menu{{
    {.id = 101, .text = L"First"},
    {.id = 102, .text = L"Second", .shortcut = L"Ctrl+S"},
    {.text = L"More", .children = {{.id = 103, .text = L"Nested"}}},
}};
SetTimer(parent, 1, 1, SelectSecond);
CHECK(wcw::ShowContextMenu(parent, {20, 20}, menu));
CHECK(commandId == 102);
CHECK(commandCode == 0);
CHECK(commandSource == 0);
```

Add separate timer callbacks for Right/Down/Enter nested selection, Escape cancellation, owner disable/destruction, and popup-rectangle capture near a work-area edge. Verify an invalid owner sets `ERROR_INVALID_WINDOW_HANDLE`, an invalid tree sets `ERROR_INVALID_PARAMETER`, a disabled row is skipped, owner loss and Escape send no command, and a temporary destroyed owner does not leave a popup window behind. Repeat pure placement assertions with dimensions multiplied by 1.5 to cover DPI scaling without depending on CI monitor hardware.

- [ ] **Step 2: Run the focused target and confirm RED**

```powershell
rtk cmake --build build-verify --config Debug --target MenuTests
```

Expected: link fails because `ShowContextMenu` is declared but not defined.

- [ ] **Step 3: Register one popup class and controller**

Add `src/Menu.cpp` to the library, declare `RegisterMenuClass`/`ShowPopupMenu` in `src/Internal.h`, and call registration from `Initialize`. Use these internal ownership types:

```cpp
struct PopupLevel {
    HWND window{};
    const std::vector<MenuItem>* items{};
    int selected{-1};
    RECT anchor{};
    std::vector<RECT> rows;
};

class PopupController {
public:
    PopupController(HWND commandTarget, HWND source, RECT anchor,
                    std::vector<MenuItem> items, StyleOverride appearance);
    bool Run();
    int Command() const { return command_; }
    void Select(size_t level, int row);
    void OpenChild(size_t level, int row, bool immediate);
    void CloseFrom(size_t level);
    void Activate(size_t level, int row);
    void Cancel();

private:
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
};
```

`Run` validates before showing, stores focus, creates the root, keeps mouse capture on the root, and dispatches a nested `GetMessageW` loop until `done_`. Convert captured mouse coordinates to screen coordinates and hit-test every open level. If the loop receives `WM_QUIT`, repost the quit code and cancel. Subclass `commandTarget_` while open so `WM_NCDESTROY` or `WM_ENABLE(FALSE)` cancels immediately. Destroy all levels, remove that subclass, release capture, and restore focus before returning. `ShowPopupMenu` reads `Command()` and sends `WM_COMMAND` only after `Run` has completed cleanup. Keep the active controller in one thread-local pointer while `Run` is active; `CancelPopupMenu(source)` cancels it only when its source matches.

- [ ] **Step 4: Implement placement, input, and dismissal**

For each level, measure four columns: 20 DIP check/image, label, optional shortcut, and 16 DIP submenu chevron, separated by resolved spacing and padding. Use 32 DIP rows and an 8 DIP separator row, scaled by the popup monitor DPI. Get the work area with `MonitorFromRect` and `GetMonitorInfoW`, then call Task 3 placement functions.

Handle these messages in the safe popup procedure:

```cpp
case WM_MOUSEMOVE:       // hit-test, select, arm 200 ms child timer
case WM_LBUTTONUP:      // activate hit row or cancel outside all levels
case WM_KEYDOWN:        // Up/Down/Home/End/Right/Left/Enter/Space/Escape
case WM_TIMER:          // open only the still-hovered child
case WM_CAPTURECHANGED: // cancel when the root loses capture
case WM_ACTIVATEAPP:    // cancel when deactivated
case WM_ENABLE:         // cancel when disabled
case WM_PAINT:          // buffered Fluent Outline surface and rows
case WM_GETOBJECT:      // delegated to Task 6 accessibility path
case WM_NCDESTROY:      // clear the matching PopupLevel::window
```

Use `CS_DROPSHADOW` only when `SPI_GETDROPSHADOW` is enabled. In high contrast (`SPI_GETHIGHCONTRAST` with `HCF_HIGHCONTRASTON`), map surface/text/selection/border to `COLOR_MENU`, `COLOR_MENUTEXT`, `COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT`, and `COLOR_WINDOWFRAME`. Otherwise use resolved panel, text, muted text, border, hover, disabled text, padding, spacing, radius, and border width.

Draw check marks and chevrons with the same anti-aliased round pen style as built-in icons. Draw item images through `paint::Image`. Clicking a leaf records its ID and sets `done_`; opening a child never dispatches the parent's ID.

- [ ] **Step 5: Define context-menu validation and command order**

Implement the public wrapper exactly:

```cpp
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
    return internal::ShowPopupMenu(owner, nullptr, anchor, options.items,
                                   options.appearance);
}
```

`ShowPopupMenu` returns true after either selection or cancellation once the root was shown. It sends `SendMessageW(commandTarget, WM_COMMAND, MAKEWPARAM(command, 0), reinterpret_cast<LPARAM>(source))` only after the popup chain is destroyed.

- [ ] **Step 6: Run GREEN and commit**

```powershell
rtk cmake -S . -B build-verify
rtk cmake --build build-verify --config Debug --target MenuTests
rtk ctest --test-dir build-verify -C Debug -R MenuTests --output-on-failure
rtk git add CMakeLists.txt src/Internal.h src/Runtime.cpp src/Menu.cpp tests/test_menu.cpp
rtk git commit -m "feat: add fluent context menus"
```

Expected: validation, command, cancellation, keyboard, nesting, and placement checks pass.

---

### Task 5: Extend the existing button into a menu button

**Files:**
- Modify: `src/Button.cpp`
- Modify: `src/Internal.h`
- Test: `tests/test_menu.cpp`
- Test: `tests/test_button_display.cpp`

**Interfaces:**
- Produces public: `CreateMenuButton`, `SetMenuItems`.
- Consumes: `internal::ShowPopupMenu`, existing button painting/focus/input, and `MenuButtonOptions`.
- Produces internal messages: `ButtonSetMenuItemsMessage`, `ButtonGetMenuStateMessage`.

- [ ] **Step 1: Add menu-button behavior and chevron tests**

Create a menu button with two items. A timer chooses the first item while `BM_CLICK` is blocked inside the popup loop. Assert that the parent receives the item ID with `lParam == reinterpret_cast<LPARAM>(menuButton)` and never receives the button's control ID. In a second opening, call `SetMenuItems` from the timer while the old popup is active; assert that opening is cancelled without a command, then activate again and assert the replacement ID. Assert `SetMenuItems` rejects a normal button and an invalid tree.

Render a 140-by-36 menu button with contrasting colors and assert that the rightmost padded 16-DIP area contains foreground pixels from the chevron while the text pixel bounds end before that area.

- [ ] **Step 2: Run focused tests and confirm RED**

```powershell
rtk cmake --build build-verify --config Debug --target MenuTests ButtonDisplayTests
```

Expected: link fails for `CreateMenuButton` and `SetMenuItems`.

- [ ] **Step 3: Add menu state to `ButtonState` and reuse all existing input**

Extend the state rather than adding another window class:

```cpp
struct ButtonState {
    HICON icon{};
    HBITMAP bitmap{};
    float iconSizeDip{16};
    UINT alignment{DT_CENTER};
    bool isDefault{};
    bool isCancel{};
    bool hover{};
    bool mousePressed{};
    WPARAM keyboardPressedKey{};
    std::vector<MenuItem> menuItems;
    StyleOverride menuAppearance;
    bool menuOpen{};
};
```

Make the internal create helper accept an optional `const MenuButtonOptions*`. `CreateButton` passes null; `CreateMenuButton` validates and passes its options. Replace `NotifyClicked` with `ActivateButton(HWND, ButtonState&)`: normal buttons retain current `BN_CLICKED`; menu buttons set `menuOpen`, call `ShowPopupMenu(GetParent(window), window, screenWindowRect, state.menuItems, state.menuAppearance)`, then clear `menuOpen` and repaint.

Handle Down and Alt+Down as activation only for menu buttons. Keep Space, Enter, mouse capture, focus, disabled behavior, default/cancel behavior, and `BM_CLICK` in the existing code path.

- [ ] **Step 4: Reserve and paint the chevron**

Before laying out the optional image and text, subtract `iconSizeDip + spacingDip` from the content right edge when `menuItems` is non-empty. Draw a centered down chevron in that reserved rectangle with the enabled or disabled text color. The focus border remains around the whole button.

Implement `SetMenuItems` by validating the target and tree, calling `CancelPopupMenu(window)`, then sending `ButtonSetMenuItemsMessage`; the WNDPROC copies the vector and invalidates. `ButtonGetMenuStateMessage` returns bit 0 for has-popup and bit 1 for expanded. `CreateMenuButton` returns null with `ERROR_INVALID_PARAMETER` for an invalid tree, while `SetMenuItems` returns false with the same error.

- [ ] **Step 5: Run GREEN and commit**

```powershell
rtk cmake --build build-verify --config Debug --target MenuTests ButtonDisplayTests
rtk ctest --test-dir build-verify -C Debug -R "MenuTests|ButtonDisplayTests" --output-on-failure
rtk git add src/Button.cpp src/Internal.h tests/test_menu.cpp tests/test_button_display.cpp
rtk git commit -m "feat: add fluent menu button"
```

Expected: menu dispatch, replacement, keyboard activation, and chevron layout checks pass.

---

### Task 6: Expose menu state through MSAA

**Files:**
- Modify: `src/Accessibility.h`
- Modify: `src/Accessibility.cpp`
- Modify: `src/Menu.cpp`
- Test: `tests/test_accessibility.cpp`

**Interfaces:**
- Produces internal: `AccessibleMenuItem`, `RegisterMenuAccessibility`, `UpdateMenuAccessibility`.
- Consumes: popup row screen bounds and selection; `ButtonGetMenuStateMessage` from Task 5.

- [ ] **Step 1: Add accessibility tests for the button and popup children**

Create a menu button using three non-separator rows—checked `Open`, parent `More`, and disabled `Unavailable`—with a separator between the first two. Assert its self role remains `ROLE_SYSTEM_PUSHBUTTON`, its state includes `STATE_SYSTEM_HASPOPUP | STATE_SYSTEM_COLLAPSED`, and its default action is `L"Open"`. While a timer-opened popup is visible, obtain `IAccessible` from `L"WcwMenuPopup"`. Extend the local `Role`, `State`, and `TextProperty` test helpers to accept a `VARIANT child`, then assert:

```cpp
long childCount{};
CHECK(menuAccessible->get_accChildCount(&childCount) == S_OK);
CHECK(childCount == 3);
VARIANT first = Self();
first.lVal = 1;
CHECK(Role(menuAccessible, first) == ROLE_SYSTEM_MENUITEM);
CHECK(TextProperty(menuAccessible, first, &IAccessible::get_accName) == L"Open");
CHECK((State(menuAccessible, first) & STATE_SYSTEM_FOCUSED) != 0);
```

Use a checked row, a disabled row, a separator, and a parent row. Assert checked/unavailable/has-popup states, assert the separator is excluded from `childCount`, and invoke `accDoDefaultAction` on a leaf to verify the same `WM_COMMAND` result as keyboard selection.

- [ ] **Step 2: Run the focused target and confirm RED**

```powershell
rtk cmake --build build-verify --config Debug --target AccessibilityTests
rtk ctest --test-dir build-verify -C Debug -R AccessibilityTests --output-on-failure
```

Expected: the new popup child and menu-button state checks fail.

- [ ] **Step 3: Add virtual menu children to the existing adapter**

Declare:

```cpp
struct AccessibleMenuItem {
    std::wstring text;
    RECT screenBounds{};
    int row{};
    bool enabled{};
    bool checked{};
    bool hasPopup{};
};

void RegisterMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items);
void UpdateMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items,
                             int focusedChild);
void NotifyAccessibility(HWND window, DWORD event, LONG child = CHILDID_SELF);
```

Add `AccessibleKind::Menu`. Store copied virtual items and `focusedChild` in the existing accessibility `Info`. For `CHILDID_SELF`, return `ROLE_SYSTEM_MENUPOPUP`. For child IDs 1 through the vector size, implement name, role, state, screen location, focus, default action, and child navigation directly. Forward unsupported properties to the standard object. `accDoDefaultAction` sends an internal activation message carrying `AccessibleMenuItem::row` to the popup; the popup calls the same `Activate` path as mouse and keyboard. Preserve all existing notification call sites through the default child argument.

Every popup level registers after computing rows, updates when selection changes, and unregisters through the existing `WM_NCDESTROY` cleanup. Fire `EVENT_OBJECT_FOCUS` with the virtual child ID when keyboard or hover selection changes.

- [ ] **Step 4: Report menu-button expanded state through existing button accessibility**

When `ButtonGetMenuStateMessage` reports has-popup, add `STATE_SYSTEM_HASPOPUP` plus expanded/collapsed to `get_accState`. Return `Open` or `Close` from `get_accDefaultAction`; keep `accDoDefaultAction` routed through `BM_CLICK`. Fire `EVENT_OBJECT_STATECHANGE` when `menuOpen` changes.

- [ ] **Step 5: Run GREEN and commit**

```powershell
rtk cmake --build build-verify --config Debug --target AccessibilityTests MenuTests
rtk ctest --test-dir build-verify -C Debug -R "AccessibilityTests|MenuTests" --output-on-failure
rtk git add src/Accessibility.h src/Accessibility.cpp src/Menu.cpp tests/test_accessibility.cpp
rtk git commit -m "feat: expose popup menus to accessibility"
```

Expected: existing accessibility coverage and new virtual-menu coverage pass.

---

### Task 7: Update the gallery and verify both configurations

**Files:**
- Modify: `demo/main.cpp:37-45,145-230,252-310`
- Verify: `CMakeLists.txt`

**Interfaces:**
- Consumes: every new public API.
- Produces: visible examples for label padding, menu button, nested context menu, checked/disabled/separator/shortcut rows, and semantic built-in icons.

- [ ] **Step 1: Add gallery controls and commands**

Add handles for one menu button and three icon views to `Gallery`. Create a shared menu tree so both entry points demonstrate the same behavior:

```cpp
std::vector<wcw::MenuItem> GalleryMenu() {
    return {
        {.id = 2001, .text = L"Information", .shortcut = L"Ctrl+I",
         .image = wcw::BuiltinIcon::Information},
        {.id = 2002, .text = L"Warning", .checked = true,
         .image = wcw::BuiltinIcon::Warning},
        {.separator = true},
        {.text = L"More actions",
         .children = {
             {.id = 2003, .text = L"Report error", .image = wcw::BuiltinIcon::Error},
             {.id = 2004, .text = L"Unavailable", .enabled = false},
         }},
    };
}
```

Create `MenuButtonOptions` with the text `L"Open menu"`, attach `GalleryMenu()`, and add it to the normal layout. On `WM_CONTEXTMENU`, convert keyboard invocation `{-1, -1}` to the center of the main window and call `ShowContextMenu`. On `WM_COMMAND` IDs 2001 through 2004, update the status label with the selected action.

Create three 24-DIP image views using the built-in sources. Set their `appearance.foreground` to blue, amber, and danger-red values in the demo only; the library icons remain theme-driven monochrome.

- [ ] **Step 2: Build the demo and run the complete Debug suite**

```powershell
rtk cmake -S . -B build-verify -DWCW_BUILD_DEMO=ON -DBUILD_TESTING=ON
rtk cmake --build build-verify --config Debug
rtk ctest --test-dir build-verify -C Debug --output-on-failure
```

Expected: the demo builds and all 12 tests pass.

- [ ] **Step 3: Run the complete Release suite**

```powershell
rtk cmake --build build-verify --config Release
rtk ctest --test-dir build-verify -C Release --output-on-failure
```

Expected: all 12 tests pass in Release.

- [ ] **Step 4: Perform one visual smoke check**

Run:

```powershell
rtk proxy powershell -NoProfile -Command "Start-Process -FilePath '.\build-verify\bin\Debug\Win32CustomWidgetsDemo.exe'"
```

Confirm: scroll-row labels have balanced vertical space and visible horizontal padding; the menu button chevron does not overlap its caption; right-click opens the same Fluent surface; nested submenus stay on screen; info/warning/error outlines remain smooth at the current DPI; keyboard Up/Down/Right/Left/Enter/Escape works.

- [ ] **Step 5: Commit the gallery and final integration**

```powershell
rtk git add demo/main.cpp CMakeLists.txt
rtk git commit -m "demo: showcase fluent menus and icons"
rtk git status --short
```

Expected: commit succeeds; only the pre-existing untracked `.codegraph/` directory remains.
