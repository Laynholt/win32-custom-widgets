# Win32 Custom Widgets

A C++20 static library of custom-painted Win32 controls. It uses the Windows SDK, GDI/GDI+,
and the standard library. Text input uses native `EDIT` services; tabs use a native tab control
with fully custom painting.

## Add it to an application

Keep the library beside the consuming project and add it from CMake:

```cmake
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../win32-custom-widgets" win32-custom-widgets)
target_link_libraries(MyApp PRIVATE Win32CustomWidgets::Win32CustomWidgets)
```

This is source-only integration: there is currently no install rule, package config, or exported
binary package. When this repository is the top-level project, `WCW_BUILD_DEMO` and CTest's
`BUILD_TESTING` default to `ON`. As an `add_subdirectory` dependency, the demo defaults to `OFF`
and WCW does not add its tests to the parent build. A parent may opt into the gallery with
`-DWCW_BUILD_DEMO=ON`.

Include `<wcw/Runtime.h>`, `<wcw/Theme.h>`, and `<wcw/Controls.h>`. Initialize once on the UI
thread after entering `wWinMain`, destroy every WCW window, then shut down on that same thread:

```cpp
if (!wcw::Initialize(instance)) return 1;
wcw::SetTheme(wcw::DarkTheme());
// Create the application window and WCW child controls, then run the message loop.
wcw::Shutdown();
```

All creation, mutation, theme, tooltip, and shutdown calls are UI-thread-affine. The caller owns
every `HICON` and `HBITMAP` supplied to a control; keep each handle alive while the control uses it
and destroy owned handles afterwards. Shared handles returned by `LoadIcon(nullptr, ...)` must not
be destroyed. Controls own their internal state and child windows.

## Themes and local styles

`DarkTheme()` and `LightTheme()` are complete presets. Copy a preset to change the global palette,
metrics, or fonts, then call `SetTheme`. `StyleOverride` changes only specified fields:

```cpp
auto theme = wcw::DarkTheme();
theme.metrics.cornerRadiusDip = 4;
theme.palette.accent = wcw::Color::FromRgb(80, 120, 240);
wcw::SetTheme(theme);

wcw::StyleOverride local;
local.background = wcw::Color::FromRgb(90, 55, 180);
local.cornerRadiusDip = 12;
wcw::SetStyleOverride(button, local); // ClearStyleOverride(button) restores the theme.
```

`SliderOptions` and `ScrollViewOptions` also expose `trackAppearance` and `thumbAppearance`, so
their parts can have independent colors, thicknesses, and corner radii.

## Controls and events

The public gallery includes Button (text, icon, or both), MenuBar, Label, ImageView, Separator, Panel,
TextBox, NumericBox, Checkbox, Toggle, Slider, ProgressBar, ComboBox, TabControl, ScrollView, and Tooltip. TextBox uses
native text services while its visible frame, states, and validation treatment are custom painted.

Menu bars are reusable child windows backed by the same `MenuItem` tree as menu buttons and context
menus:

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

Menu commands arrive through the parent `WM_COMMAND`: `LOWORD(wParam)` is the item ID and
`lParam` identifies the menu bar `HWND`. Use `&` for keyboard mnemonics (`&&` renders a literal
ampersand); `Alt`/`F10`, arrows, `Enter`, `Space`, and `Escape` provide standard menu navigation.
`ControlOptions::appearance` styles the bar and `MenuBarOptions::menuAppearance` styles its popups.
Calling `SetTheme` updates the menu bar and controls; newly opened menus use the current theme.
Use `SetMenuBarItems` to replace the item tree.

### Tabs and page windows

`TabControl` is the tab strip extracted from RatAI: equally sized rounded tabs, selected/hover/
disabled colors, theme and style overrides, native keyboard navigation and MSAA tab roles.
It sends the same `WCN_SELECTION_CHANGED` notification as ComboBox. Use a distinct control ID
or check `header.hwndFrom` to distinguish them.

```cpp
wcw::TabControlOptions options;
options.parent = window;
options.id = 101;
options.bounds = {20, 20, 600, 44};
options.style = WS_VISIBLE;
options.accessibleName = L"Views";
options.items = {{L"Overview", 1}, {L"Settings", 2}};
HWND tabs = wcw::CreateTabControl(options);
```

The caller owns page content and layout. For separate windows, create two sibling child windows
below the strip, with the first visible and the second hidden. They can be application-defined
windows or WCW `ScrollView`s containing controls. Keep both alive to preserve input and scroll
state. Handle selection in the parent's window procedure (here `pages` contains those two HWNDs):

```cpp
case WM_NOTIFY: {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header && header->hwndFrom == tabs && header->code == wcw::WCN_SELECTION_CHANGED) {
        const auto* change = reinterpret_cast<const wcw::SelectionChangedNotification*>(lParam);
        for (int i = 0; i < 2; ++i) {
            if (i != change->newIndex &&
                (GetFocus() == pages[i] || IsChild(pages[i], GetFocus()))) SetFocus(tabs);
            ShowWindow(pages[i], i == change->newIndex ? SW_SHOWNA : SW_HIDE);
        }
        return 0;
    }
    break;
}
```

For a shared drawing surface, use `change->newId` to choose what to draw and invalidate the
content window, as RatAI does. The gallery demonstrates two persistent `ScrollView` pages.
`SetTabSelection(tabs, index)` also sends the notification synchronously when selection changes;
selecting the current index does nothing. `GetTabSelection` returns `-1` for an invalid handle.
Items must be nonempty and remain fixed for the lifetime of the control. The initial index is
clamped to the item range; later invalid indices are rejected. Do not insert/delete items via
native `TCM_*` messages, which would bypass the control's item state.

Use Tab to enter/leave the strip, arrows to focus a tab and Space to activate it. Give the strip
enough width for its captions (long text is ellipsized); this compact RatAI implementation does
not add a scrolling or closable tab bar. Reposition the strip and page windows on resize/DPI
changes, just like other WCW controls.

Buttons send `WM_COMMAND` with `BN_CLICKED`. Value, check, and selection changes arrive through
`WM_NOTIFY` with `WCN_VALUE_CHANGED`, `WCN_CHECK_CHANGED`, and `WCN_SELECTION_CHANGED`; cast the
payload to the matching public notification structure. Controls provide names, roles, state,
value, focus, and default actions through Microsoft Active Accessibility.

The application owns layout. Bounds are expressed in DIPs at creation, but the application must
reposition controls on `WM_SIZE` and `WM_DPICHANGED` (the gallery demonstrates a small local
`MoveWindowDip` helper). Enable Per-Monitor-V2 awareness before creating windows. Use
`IsDialogMessage` in the message loop for Tab and dialog-key navigation.

## Build and run

Requires Windows 10 or later, MSVC with C++20 support, the Windows SDK, and CMake 3.21 or later.
Use a fresh build directory when changing the Visual Studio version.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\Debug\Win32CustomWidgetsDemo.exe
```

Use `-DWCW_BUILD_DEMO=OFF` for a library-and-tests top-level build or `-DBUILD_TESTING=OFF` for a
library-and-demo build without tests.

See [implementation status](docs/status.md) for the plan audit and remaining verification.

## Current non-goals

The library does not provide a layout engine, data binding, animation framework, renderer other
than GDI/GDI+, gamepad input, or cross-platform support. It does not copy or take ownership of
caller-supplied image handles.
