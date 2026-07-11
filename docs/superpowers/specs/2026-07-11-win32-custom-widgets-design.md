# Win32 Custom Widgets Design

## Goal

Build a reusable C++20 static library of visually custom Win32 controls for Windows 10 and later. Controls are real child `HWND`s, draw their complete visible appearance with GDI/GDI+, retain native message-loop integration, and require no third-party dependencies.

The first consumer is HoradricTimer. Other Win32 applications can consume the same CMake target through `add_subdirectory` without copying library sources.

## Scope

The first release provides:

- Button and IconButton
- TextBox
- NumericBox
- ComboBox with a custom popup
- Checkbox and Toggle
- Slider
- ProgressBar
- ScrollView with custom scrollbars
- Tooltip
- Label
- ImageView
- Separator
- Panel/Card

The library also provides application themes, per-control style overrides, DPI conversion, common paint primitives, notifications, keyboard behavior, and basic MSAA accessibility.

Game-specific behavior, application layout, file-based image loading, dialogs, custom title bars, shadows, elevation effects, animation frameworks, package-manager manifests, and CMake install/export packaging are outside this release.

## Repository And Build

The library lives in `F:\Data\Code\C++\win32-custom-widgets` as an independent Git repository.

CMake exposes:

- `Win32CustomWidgets`: the static library
- `Win32CustomWidgets::Win32CustomWidgets`: namespaced alias used by consumers
- `Win32CustomWidgetsDemo`: interactive control gallery
- `Win32CustomWidgetsTests`: minimal assertion-based test executable registered with CTest

The supported toolchain is MSVC with C++20. Public headers live under `include/wcw`, implementation files under `src`, demo code under `demo`, and tests under `tests`.

HoradricTimer supplies the sibling repository path to `add_subdirectory` and links the namespaced target. A Git submodule can replace the sibling path when the projects are published; source copying is not part of the workflow.

## Runtime Architecture

`wcw::Initialize(HINSTANCE)` starts GDI+, registers all control and popup window classes, and creates shared theme resources. Repeated calls for the same process are safe. `wcw::Shutdown()` releases shared resources after all library windows have been destroyed.

Each control is a real child `HWND`. The parent owns its lifetime through normal Win32 destruction. Control state is stored in window user data and released during `WM_NCDESTROY`.

The public surface follows Win32 conventions:

- `Create*` functions return an `HWND` or `nullptr`.
- Typed setters and getters operate on the returned handle.
- State changes notify the parent through `WM_COMMAND` for simple actions and `WM_NOTIFY` for structured values.
- Typed helpers validate and decode library notifications.
- `SetEnabled`, focus, visibility, position, and destruction continue to use ordinary Win32 APIs where they already solve the problem.

The library has no callback, signal, layout, ownership, logging, or background-thread framework.

## Theme And Style Model

`Theme` contains a semantic `Palette`, typography, and shared `Metrics`. Semantic colors cover window, panel, input, text, muted text, border, hover, pressed, selected, disabled, focus, accent, danger, and success roles.

The built-in Dark theme starts with the YoutubeDownloader visual language:

- window: `#141416`
- panel: `#1C1C1F`
- input: `#19191C`
- text: `#F2F2F2`
- muted text: `#ACACB2`
- border: `#3A3A40`
- hover surface: `#37373D`
- pressed surface: `#2F2F34`
- accent: `#E84855`
- accent hover: `#F55B68`
- accent pressed: `#CF3B48`
- danger: `#E84855`
- success: `#ABE6C1`

The built-in Light theme uses:

- window: `#F4F5F7`
- panel: `#FFFFFF`
- input: `#F8F9FB`
- text: `#18181B`
- muted text: `#62626B`
- border: `#D7D8DD`
- hover surface: `#ECEEF2`
- pressed surface: `#DFE2E8`
- accent: `#E84855`
- accent hover: `#F55B68`
- accent pressed: `#CF3B48`
- danger: `#D7263D`
- success: `#268A55`

Metrics are expressed in device-independent pixels and include border width, focus-ring width, padding, spacing, control height, and corner radius. A zero corner radius produces square corners.

Every control can receive a `StyleOverride`. Each optional override replaces one resolved theme value, including colors, font, border width, padding, and the radii relevant to that control. Missing values continue to inherit from the active application theme. Changing the application theme invalidates all library controls without removing local overrides.

Flat rendering is the only rendering model in this release. Style data and shared paint helpers keep later shadow or elevation work isolated from control creation APIs, but no placeholder shadow API is added now.

## Rendering And DPI

GDI+ draws antialiased fills, borders, focus rings, rounded geometry, progress shapes, and images. GDI draws text and provides native font metrics. Every control paints into a memory buffer and copies the completed frame to the window DC to prevent flicker.

Public dimensions and theme metrics are DIP values. Controls query the DPI of their own window, convert metrics at paint and hit-test boundaries, and cache fonts and paint resources per theme and DPI. A DPI change invalidates those resources and repaints the control. The consumer remains responsible for positioning and resizing child windows.

Corner radii are clamped to valid control geometry. Slider track and thumb, scrollbar track and thumb, popup items, panels, and input shells have independently resolved style metrics.

ImageView accepts `HICON` or `HBITMAP` and supports contain, cover, and stretch modes. The library does not load image files. Consumers use WIC or another Windows facility and own the supplied image handles unless an individual API explicitly documents ownership transfer.

## Control Behavior

### Button And IconButton

Buttons support text, icon, combined text and icon, default action, cancel action, enabled, hovered, pressed, focused, and checked-style visual states. Mouse release inside the control and keyboard activation with Space or Enter produce `BN_CLICKED`.

### TextBox

TextBox uses an internal borderless Win32 `EDIT` for Unicode editing, IME, selection, clipboard, password, read-only, and caret behavior. The library draws the complete shell, placeholder, validation state, background, border, and focus ring. No standard edit border or background is visible.

### NumericBox

NumericBox builds on TextBox and supports integer or floating-point input, minimum, maximum, step, keyboard arrows, and mouse wheel. Parsing is locale-stable for persisted numeric values. Invalid intermediate text is retained and displayed with the validation style; it does not emit a value-changed notification. Committed valid values are clamped to the configured range.

### ComboBox

ComboBox owns a custom top-level popup containing custom-drawn items and scrollbars. Items may contain text, an icon, and an application-defined integer identifier. Mouse, Up/Down, Home/End, Enter, Escape, and incremental prefix search are supported. Selection changes send a structured notification.

### Checkbox And Toggle

Checkbox and Toggle expose the same boolean behavior with different visuals. Space toggles the value. Checked state, focus, hover, pressed, and disabled visuals are themed.

### Slider

Slider supports a numeric range, step, mouse dragging, wheel input, Home/End, arrows, and Page Up/Page Down. Track and thumb have independent styles. Value changes send structured notifications.

### ProgressBar

ProgressBar supports determinate and indeterminate modes. Determinate values are clamped to the configured range. Indeterminate animation uses a control timer only while the control is visible and enabled.

### ScrollView

ScrollView is a child-window container for other HWND controls. It supports vertical and horizontal scrolling, mouse wheel, keyboard scrolling, thumb dragging, and custom themed scrollbars. It exposes content extent and scroll-offset APIs; it does not implement automatic layout.

### Tooltip

Tooltip is a custom topmost popup associated with an owner control. It supports configurable show delay, hide delay, maximum width, and theme/style overrides. It never takes keyboard focus.

### Display Controls

Label supports alignment, wrapping, ellipsis, and enabled/muted roles. ImageView supports the documented image modes. Separator renders horizontal or vertical lines. Panel/Card renders a themed background and border and acts as a visual container; the consumer positions its child controls.

## Input And Accessibility

Interactive controls support `WS_TABSTOP`, visible focus indication, correct disabled behavior, and keyboard activation consistent with their Win32 counterparts. Popup controls restore focus to their owner when closed.

Controls answer `WM_GETOBJECT` with a minimal MSAA provider exposing accessible name, role, value, checked/selected state, enabled state, focus state, and default action where applicable. Window text or an explicit accessible-name setter supplies the name. The demo includes keyboard-only navigation checks.

## Errors And Threading

`Initialize()` reports failure with `false`. Creation functions report failure with `nullptr`. APIs preserve a useful Win32 error code through `GetLastError()` when they fail.

No C++ exception crosses a window-procedure boundary. Allocation or rendering failures are contained, leave the control valid where possible, and return a conservative Win32 result.

All control APIs must be called from the UI thread that owns the target window. The library does not marshal work between threads.

Numeric ranges, progress values, radii, dimensions, and scroll offsets are normalized at the public boundary. Invalid handles or handles belonging to a different control type cause typed APIs to fail without mutating unrelated windows.

## Testing And Verification

Development follows red-green-refactor. Production behavior is implemented only after a focused failing test demonstrates the requirement where the behavior can be isolated from Win32 painting.

The assertion-based test executable covers:

- theme inheritance and StyleOverride resolution
- Dark and Light preset values
- DIP-to-pixel conversion and rounding
- radius normalization
- slider and scrollbar geometry
- numeric parsing, stepping, validation, and clamping
- ComboBox keyboard navigation and prefix search
- progress and scroll range normalization
- control state transitions that can be modeled without a visible desktop

The demo provides the runnable visual check for native window behavior. It displays every control in normal, hovered, focused, pressed, disabled, selected, and validation-error states; switches Dark and Light themes; and changes the global corner radius from 0 through 24 DIP.

Completion requires fresh Debug and Release builds, CTest success, demo launch, keyboard-only traversal, theme switching, DPI change verification, and a visual pass for clipping, flicker, state colors, and custom popups.

## HoradricTimer Integration Contract

HoradricTimer uses only the library's public headers and namespaced CMake target. The application owns layout, settings, WIC image loading, overlay drawing, input hooks, timers, tray behavior, and persistence.

Every visible settings-window widget is either a Win32 Custom Widgets control or application-painted content. HoradricTimer does not copy private paint helpers from YoutubeDownloader and does not create visibly default Win32 controls.

