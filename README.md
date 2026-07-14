# Win32 Custom Widgets

A C++20 static library of custom-painted Win32 controls. It uses the Windows SDK, GDI/GDI+,
and the standard library; the only native visual child used internally is `EDIT` for text input.

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

The public gallery includes Button (text, icon, or both), Label, ImageView, Separator, Panel, TextBox,
NumericBox, Checkbox, Toggle, Slider, ProgressBar, ComboBox, ScrollView, and Tooltip. TextBox uses
native text services while its visible frame, states, and validation treatment are custom painted.

Buttons send `WM_COMMAND` with `BN_CLICKED`. Value, check, and selection changes arrive through
`WM_NOTIFY` with `WCN_VALUE_CHANGED`, `WCN_CHECK_CHANGED`, and `WCN_SELECTION_CHANGED`; cast the
payload to the matching public notification structure. Controls provide names, roles, state,
value, focus, and default actions through Microsoft Active Accessibility.

The application owns layout. Bounds are expressed in DIPs at creation, but the application must
reposition controls on `WM_SIZE` and `WM_DPICHANGED` (the gallery demonstrates a small local
`MoveWindowDip` helper). Enable Per-Monitor-V2 awareness before creating windows. Use
`IsDialogMessage` in the message loop for Tab and dialog-key navigation.

## Build and run

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\Debug\Win32CustomWidgetsDemo.exe
```

Use `-DWCW_BUILD_DEMO=OFF` for a library-and-tests top-level build or `-DBUILD_TESTING=OFF` for a
library-and-demo build without tests.

## Current non-goals

The library does not provide a layout engine, data binding, animation framework, renderer other
than GDI/GDI+, gamepad input, or cross-platform support. It does not copy or take ownership of
caller-supplied image handles.
