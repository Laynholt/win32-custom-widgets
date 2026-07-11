# Win32 Custom Widgets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a reusable, themeable C++20 static library of custom-painted HWND controls, with a visual demo and tests, for HoradricTimer and other Win32 applications.

**Architecture:** Each control is a real child HWND registered by one process-wide runtime and painted through shared GDI/GDI+ primitives. Public creation and mutation APIs stay Win32-shaped; pure theme, geometry, parsing, and navigation logic remains separately testable. Consumers link the namespaced CMake target with `add_subdirectory`.

**Tech Stack:** C++20, MSVC, CMake 3.20+, Win32 API, GDI, GDI+, Comctl32 subclass helpers, Oleacc/MSAA, CTest, standard-library assertions.

## Global Constraints

- Support Windows 10 and later with MSVC and C++20.
- Use only the C++ standard library and Windows SDK libraries; add no third-party dependency.
- Produce the static target `Win32CustomWidgets` and alias `Win32CustomWidgets::Win32CustomWidgets`.
- Every visible control surface is library-painted; a hidden borderless native `EDIT` is allowed inside TextBox.
- Public sizes and style metrics use device-independent pixels; convert at HWND boundaries using that window's DPI.
- Controls are UI-thread-affine real HWNDs and notify parents through `WM_COMMAND` or `WM_NOTIFY`.
- Preserve keyboard focus, Tab navigation, disabled states, visible focus rings, and basic MSAA name/role/value/state/default-action exposure.
- Built-in themes are Dark and Light; every control supports local style overrides, including a corner radius where its geometry permits rounding.
- Keep flat rendering; do not implement shadows, elevation, a layout engine, callbacks/signals, logging, background threads, package-manager manifests, or CMake install/export packaging.
- Use red-green-refactor for each production behavior and leave the smallest runnable regression check.

---

### Task 1: CMake Skeleton And Theme Presets

**Files:**
- Create: `CMakeLists.txt`
- Modify: `.gitignore`
- Create: `include/wcw/Theme.h`
- Create: `src/Theme.cpp`
- Create: `tests/Test.h`
- Create: `tests/test_theme.cpp`

**Interfaces:**
- Consumes: none.
- Produces: `wcw::Color`, `FontSpec`, `Palette`, `Metrics`, `Theme`, `DarkTheme()`, and `LightTheme()`.

- [ ] **Step 1: Write the failing preset test and build graph**

Extend the existing worktree/build ignore rules as needed, then create a CMake project that defines the library, alias, test executable, CTest entry, and links `user32`, `gdi32`, `gdiplus`, `comctl32`, and `oleacc`. In `tests/test_theme.cpp`, assert exact preset values:

```cpp
#include "Test.h"
#include <wcw/Theme.h>

int main() {
    const auto dark = wcw::DarkTheme();
    CHECK(dark.palette.window == wcw::Color::FromRgb(0x14, 0x14, 0x16));
    CHECK(dark.palette.accent == wcw::Color::FromRgb(0xE8, 0x48, 0x55));
    CHECK(dark.metrics.cornerRadiusDip == 8.0f);

    const auto light = wcw::LightTheme();
    CHECK(light.palette.window == wcw::Color::FromRgb(0xF4, 0xF5, 0xF7));
    CHECK(light.palette.text == wcw::Color::FromRgb(0x18, 0x18, 0x1B));
    CHECK(light.palette.accent == dark.palette.accent);
}
```

`Test.h` defines `CHECK(expression)` to print file, line, and expression and return a nonzero process exit code through a shared failure counter.

- [ ] **Step 2: Verify RED**

Run:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Debug --target Win32CustomWidgetsTests
```

Expected: compilation fails because `wcw/Theme.h` does not exist.

- [ ] **Step 3: Implement the theme data and exact presets**

Use this public shape:

```cpp
namespace wcw {
struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};
    static constexpr Color FromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) { return {r, g, b, 255}; }
    friend constexpr bool operator==(const Color&, const Color&) = default;
};
struct FontSpec { std::wstring family{L"Segoe UI"}; float sizeDip{14}; int weight{400}; bool italic{}; };
struct Palette {
    Color window, panel, input, text, mutedText, border, hover, pressed, selected,
          disabledSurface, disabledText, focus, accent, accentHover, accentPressed, danger, success;
};
struct Metrics {
    float borderWidthDip{1}, focusWidthDip{2}, paddingXDip{12}, paddingYDip{8}, spacingDip{8},
          controlHeightDip{36}, cornerRadiusDip{8};
};
struct Theme { Palette palette; Metrics metrics; FontSpec body; FontSpec label; };
Theme DarkTheme();
Theme LightTheme();
}
```

Populate every palette role with the exact colors in the approved design spec; use `#232326` and `#8A8A92` for Dark disabled surface/text, and `#E5E6EA` and `#9A9CA4` for Light disabled surface/text. Use the accent color for focus and a 600-weight label font.

- [ ] **Step 4: Verify GREEN and commit**

Run the build and `ctest --test-dir build -C Debug --output-on-failure`; expect one passing test. Commit:

```powershell
git add CMakeLists.txt .gitignore include src tests
git commit -m "feat: add widget theme presets"
```

### Task 2: Style Resolution, DPI, And Geometry

**Files:**
- Create: `include/wcw/Style.h`
- Create: `include/wcw/Geometry.h`
- Create: `src/Style.cpp`
- Create: `src/Geometry.cpp`
- Create: `tests/test_style_geometry.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Theme`, `Color`, and `FontSpec` from Task 1.
- Produces: `StyleOverride`, `ResolvedStyle`, `ResolveStyle`, `DipToPx`, `PxToDip`, `ClampRadiusDip`, `SliderGeometry`, and `ScrollbarGeometry`.

- [ ] **Step 1: Write failing pure-logic tests**

Test that a local background and zero radius override only those theme fields, `DipToPx(10, 144) == 15`, negative radii become zero, oversized radii become half the smaller dimension, slider endpoints match min/max, and a scrollbar thumb is at least 28 physical pixels.

```cpp
wcw::StyleOverride local;
local.background = wcw::Color::FromRgb(1, 2, 3);
local.cornerRadiusDip = 0.0f;
const auto resolved = wcw::ResolveStyle(wcw::DarkTheme(), local);
CHECK(resolved.background == *local.background);
CHECK(resolved.text == wcw::DarkTheme().palette.text);
CHECK(resolved.cornerRadiusDip == 0.0f);
CHECK(wcw::DipToPx(10.0f, 144) == 15);
CHECK(wcw::ClampRadiusDip(50.0f, 40.0f, 20.0f) == 10.0f);
```

- [ ] **Step 2: Verify RED**

Build the test target. Expected: missing `wcw/Style.h` and `wcw/Geometry.h`.

- [ ] **Step 3: Implement value resolution and geometry**

`StyleOverride` contains `std::optional` values for background, foreground, muted foreground, border, hover, pressed, selected, disabled surface/text, focus, accent, danger, font, border width, focus width, horizontal/vertical padding, spacing, control height, and corner radius. `ResolvedStyle` stores concrete values. Resolve absent fields from the Theme.

Implement DPI rounding as `std::lround(dip * dpi / 96.0f)`. Normalize slider values with `std::clamp`; when min equals max, place the thumb at the track start. Compute scrollbar thumb size as `clamp(track * viewport / content, minThumb, track)` and position it proportionally over the remaining scroll range.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest; expect both tests to pass. Commit:

```powershell
git add CMakeLists.txt include/wcw src tests/test_style_geometry.cpp
git commit -m "feat: add style and geometry primitives"
```

### Task 3: Runtime, Theme Propagation, And Paint Core

**Files:**
- Create: `include/wcw/Runtime.h`
- Create: `include/wcw/Control.h`
- Create: `src/Internal.h`
- Create: `src/Runtime.cpp`
- Create: `src/Paint.h`
- Create: `src/Paint.cpp`
- Create: `tests/test_runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Theme/style/DPI primitives from Tasks 1-2.
- Produces: `Initialize(HINSTANCE)`, `Shutdown()`, `SetTheme(const Theme&)`, `GetTheme()`, `SetStyleOverride(HWND, const StyleOverride&)`, `ClearStyleOverride(HWND)`, `RectDip`, and internal control registration/painting helpers.

- [ ] **Step 1: Write the failing hidden-window test**

Create a message-only parent, call `Initialize(GetModuleHandleW(nullptr))` twice, change to Light theme, verify `GetTheme()`, then call `Shutdown()` twice. Assert initialization and idempotence without showing UI.

- [ ] **Step 2: Verify RED**

Build. Expected: missing Runtime and Control headers.

- [ ] **Step 3: Implement runtime and shared painting**

Use a process-local runtime guarded by a mutex only during initialize/shutdown/theme replacement. Start GDI+ once, initialize common controls once, register library classes, and keep a set of live library HWNDs on the UI thread. `SetTheme` stores a copy and posts a private theme-changed message to each valid live HWND.

The paint module must provide concrete helpers for a compatible-memory-DC buffer, DIP conversion, rounded GDI+ paths, solid fill/border/focus drawing, GDI text drawing, icon/bitmap drawing, and font caching keyed by `FontSpec + DPI`. `WM_ERASEBKGND` returns nonzero because the full background is buffer-painted.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest; expect three passing tests. Commit:

```powershell
git add CMakeLists.txt include/wcw src tests/test_runtime.cpp
git commit -m "feat: add widget runtime and paint core"
```

### Task 4: Button And Display Controls

**Files:**
- Create: `include/wcw/Controls.h`
- Create: `src/Button.cpp`
- Create: `src/Display.cpp`
- Create: `tests/test_button_display.cpp`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: runtime registration and paint helpers from Task 3.
- Produces: `ControlOptions`, `ButtonOptions`, `ImageSource`, `ImageMode`, `CreateButton`, `CreateIconButton`, `CreateLabel`, `CreateImageView`, `CreateSeparator`, and `CreatePanel`.

- [ ] **Step 1: Write failing creation and activation tests**

Create hidden controls, verify their window text and IDs, send `WM_KEYDOWN/UP` for Space to a focused button, pump messages, and assert the parent receives exactly one `BN_CLICKED`. Verify disabled buttons emit none. Verify setting `StyleOverride{.cornerRadiusDip = 0}` succeeds for every created control.

- [ ] **Step 2: Verify RED**

Expected: `wcw/Controls.h` is missing.

- [ ] **Step 3: Implement APIs and window procedures**

`ControlOptions` contains `HWND parent`, `int id`, `RectDip bounds`, `DWORD style`, `std::wstring text`, `std::wstring accessibleName`, and `StyleOverride appearance`. `ButtonOptions` adds `HICON icon`, `HBITMAP bitmap`, icon size DIP, alignment, and default/cancel flags. `ImageSource` is a non-owning tagged HICON/HBITMAP.

Buttons track hover with `TrackMouseEvent`, capture on left press, activate only on release inside, and support Space/Enter. Display controls never take focus. All five classes use double-buffered paint and resolved styles; ImageView implements contain, cover, and stretch with clipping to the resolved radius.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_button_display.cpp
git commit -m "feat: add buttons and display controls"
```

### Task 5: TextBox And NumericBox

**Files:**
- Create: `src/TextBox.cpp`
- Create: `src/NumericBox.cpp`
- Create: `src/NumericModel.h`
- Create: `src/NumericModel.cpp`
- Create: `tests/test_text_numeric.cpp`
- Modify: `include/wcw/Controls.h`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: ControlOptions, styles, runtime, and paint core.
- Produces: `TextBoxOptions`, `NumericBoxOptions`, `NumericMode`, `CreateTextBox`, `SetTextBoxText`, `GetTextBoxText`, `SetValidationError`, `CreateNumericBox`, `SetNumericValue`, and `GetNumericValue`.

- [ ] **Step 1: Write failing parser and HWND tests**

Test exact parsing for `L"-12"`, `L"3.5"`, empty text, `L"1x"`, comma decimal input, min/max clamping, and step increments. Create a hidden TextBox and assert its internal EDIT has no `WS_BORDER` or client edge. Assert placeholder and validation setters invalidate without changing entered text.

- [ ] **Step 2: Verify RED**

Expected: missing NumericModel and TextBox/Numeric APIs.

- [ ] **Step 3: Implement text editing and numeric validation**

TextBox is a custom outer HWND with one borderless child `EDIT`. Forward focus to the EDIT, subclass it to relay focus/text changes, use `WM_SETFONT`, and paint placeholder only when text is empty and the child is unfocused. Password and read-only options map to native EDIT behavior while the visible shell remains custom.

NumericModel uses `std::from_chars` for locale-stable integer and floating parsing; a comma is invalid. Empty, sign-only, and decimal-point-only text are valid intermediate states but have no committed numeric value. Arrow/wheel steps clamp and reformat using `std::to_chars`. Emit a structured `WCN_VALUE_CHANGED` notification only after a valid committed change.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_text_numeric.cpp
git commit -m "feat: add text and numeric inputs"
```

### Task 6: Checkbox, Toggle, Slider, And ProgressBar

**Files:**
- Create: `src/BooleanControl.cpp`
- Create: `src/Slider.cpp`
- Create: `src/ProgressBar.cpp`
- Create: `tests/test_value_controls.cpp`
- Modify: `include/wcw/Controls.h`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: button state behavior, geometry, styles, and notifications.
- Produces: creation/get/set APIs for Checkbox, Toggle, Slider, and ProgressBar plus `WCN_CHECK_CHANGED` and `WCN_VALUE_CHANGED` notifications.

- [ ] **Step 1: Write failing state tests**

Assert Space toggles Checkbox and Toggle exactly once, disabled controls do not change, slider arrows move by step, Page Up moves by ten steps, Home/End select endpoints, and progress values clamp. Verify indeterminate ProgressBar owns a timer only while visible and enabled.

- [ ] **Step 2: Verify RED**

Expected: missing declarations or unresolved creation functions.

- [ ] **Step 3: Implement value controls**

Share one internal boolean input state machine between Checkbox and Toggle, with separate painters. Slider uses Task 2 geometry for paint and hit testing, captures during thumb drag, and sends a notification only when the normalized value changes. ProgressBar paints determinate fill or moves one indeterminate segment on a 16 ms control timer; stop the timer on hide, disable, mode change, and destroy.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_value_controls.cpp
git commit -m "feat: add boolean and range controls"
```

### Task 7: ComboBox And Custom Popup

**Files:**
- Create: `src/ComboModel.h`
- Create: `src/ComboModel.cpp`
- Create: `src/ComboBox.cpp`
- Create: `tests/test_combo.cpp`
- Modify: `include/wcw/Controls.h`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: ImageSource, ScrollbarGeometry, theme/style, runtime popup registration.
- Produces: `ComboItem`, `ComboBoxOptions`, create/items/selection APIs, and `WCN_SELECTION_CHANGED`.

- [ ] **Step 1: Write failing model and popup tests**

Test empty-list navigation, wrapping-disabled Up/Down, Home/End, case-insensitive repeated prefix search, selection preservation when items are replaced, Escape cancellation, and Enter commit. Create the popup and assert it is `WS_POPUP`, contains no standard LISTBOX/COMBOBOX child, and restores focus to the ComboBox when closed.

- [ ] **Step 2: Verify RED**

Expected: missing ComboModel and ComboBox APIs.

- [ ] **Step 3: Implement ComboBox**

`ComboItem` contains `std::wstring text`, `std::intptr_t id`, and optional non-owning ImageSource. The owner control paints current selection and chevron. The popup is a library window with custom rows and scrollbar; it closes on outside click, deactivation, Escape, selection, owner destruction, or DPI/theme change. Prefix search resets after one second. A selection notification carries old/new indexes and item IDs.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_combo.cpp
git commit -m "feat: add custom combo box"
```

### Task 8: ScrollView

**Files:**
- Create: `src/ScrollModel.h`
- Create: `src/ScrollModel.cpp`
- Create: `src/ScrollView.cpp`
- Create: `tests/test_scroll.cpp`
- Modify: `include/wcw/Controls.h`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: scrollbar geometry, runtime, styles, and paint helpers.
- Produces: `ScrollViewOptions`, `CreateScrollView`, `SetScrollContentExtent`, `SetScrollOffset`, `GetScrollOffset`, and `GetScrollContentWindow`.

- [ ] **Step 1: Write failing scroll tests**

Test content smaller than viewport, exact max offsets, wheel accumulation, Home/End, Page Up/Down, horizontal Shift+wheel, thumb dragging, and offset preservation/clamping after resize.

- [ ] **Step 2: Verify RED**

Expected: missing ScrollModel and ScrollView functions.

- [ ] **Step 3: Implement container scrolling**

ScrollView owns one content child HWND. Moving the content child applies the normalized negative offset. Paint custom vertical/horizontal tracks and thumbs only when needed. Mouse wheel scrolls three text lines using `SPI_GETWHEELSCROLLLINES`; keyboard scrolling works when the ScrollView itself has focus. Thumb dragging captures the mouse and maps travel through Task 2 geometry. No automatic child layout is added.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_scroll.cpp
git commit -m "feat: add custom scroll view"
```

### Task 9: Custom Tooltip

**Files:**
- Create: `src/Tooltip.cpp`
- Create: `tests/test_tooltip.cpp`
- Modify: `include/wcw/Controls.h`
- Modify: `src/Runtime.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: popup runtime, Theme/StyleOverride, paint and DPI helpers.
- Produces: `TooltipOptions`, `AttachTooltip`, `DetachTooltip`, and `HideAllTooltips`.

- [ ] **Step 1: Write failing lifecycle tests**

Attach a tooltip to a hidden owner, simulate hover timers, assert the popup has `WS_EX_NOACTIVATE | WS_EX_TOPMOST`, verify max-width wrapping, and verify owner destruction removes the association without leaving a live popup.

- [ ] **Step 2: Verify RED**

Expected: missing tooltip functions.

- [ ] **Step 3: Implement tooltip ownership**

Subclass the owner with Comctl32 subclass helpers, start the show timer on hover, cancel it on leave/click/focus loss, and create one non-activating custom popup per UI thread. Measure wrapped text with the resolved font and clamp the popup to the nearest monitor work area. Never transfer focus.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt include/wcw/Controls.h src tests/test_tooltip.cpp
git commit -m "feat: add custom tooltips"
```

### Task 10: MSAA Accessibility

**Files:**
- Create: `src/Accessibility.h`
- Create: `src/Accessibility.cpp`
- Create: `tests/test_accessibility.cpp`
- Modify: `src/Internal.h`
- Modify: each interactive control implementation
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: internal control kind, text, focus, enabled, selected, checked, numeric values, and default actions.
- Produces: `WM_GETOBJECT` responses for `OBJID_CLIENT` with correct MSAA properties.

- [ ] **Step 1: Write failing accessible-object tests**

Call `AccessibleObjectFromWindow` for each hidden interactive control and assert name, role, focusable/unavailable state, value or checked/selected state, and default action. Invoke Button's accessible default action and assert one click notification.

- [ ] **Step 2: Verify RED**

Expected: custom controls return no useful library MSAA object or incorrect roles.

- [ ] **Step 3: Implement a minimal shared IAccessible provider**

Implement COM lifetime, `IDispatch` plumbing through `CreateStdAccessibleObject` delegation, and override only library-owned name, role, state, value, default-action text, and `accDoDefaultAction`. Use `NotifyWinEvent` for focus, value, selection, checked, show, and hide transitions. Release providers during `WM_NCDESTROY`; never expose raw state after HWND destruction.

- [ ] **Step 4: Verify GREEN and commit**

Run CTest and commit:

```powershell
git add CMakeLists.txt src tests/test_accessibility.cpp
git commit -m "feat: add widget accessibility"
```

### Task 11: Demo Gallery And Public Documentation

**Files:**
- Create: `demo/main.cpp`
- Create: `README.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the complete public library API.
- Produces: `Win32CustomWidgetsDemo` and consumer documentation.

- [ ] **Step 1: Add the demo target before its source**

Declare `Win32CustomWidgetsDemo` linked only to `Win32CustomWidgets::Win32CustomWidgets` and Windows SDK libraries. Build and verify RED because `demo/main.cpp` is absent.

- [ ] **Step 2: Implement the gallery**

Create one Per-Monitor-V2-aware Win32 window containing every control. Include Dark/Light buttons, a radius slider spanning 0-24 DIP, enabled/disabled examples, validation error, icon/text ComboBox, determinate/indeterminate progress, nested controls in ScrollView, tooltips, and a status Label that displays notifications. Lay out controls using a small local `MoveWindowDip` helper; do not add layout APIs to the library.

- [ ] **Step 3: Document consumption and ownership**

README must show the exact sibling integration:

```cmake
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../win32-custom-widgets" win32-custom-widgets)
target_link_libraries(MyApp PRIVATE Win32CustomWidgets::Win32CustomWidgets)
```

Document Initialize/Shutdown order, UI-thread affinity, image-handle ownership, Theme and StyleOverride examples, notifications, DPI responsibility, supported controls, build commands, and current non-goals.

- [ ] **Step 4: Build, launch, and commit**

Run:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
Start-Process -FilePath '.\build\bin\Debug\Win32CustomWidgetsDemo.exe'
```

Expected: all tests pass and the gallery opens. Perform keyboard-only traversal and switch theme/radius. Commit:

```powershell
git add CMakeLists.txt demo README.md
git commit -m "docs: add widget gallery and usage guide"
```

### Task 12: Release Verification And HoradricTimer Consumption Smoke Test

**Files:**
- Create: `tests/consumer/CMakeLists.txt`
- Create: `tests/consumer/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: public headers and namespaced target only.
- Produces: proof that an external subdirectory consumer can build without private includes.

- [ ] **Step 1: Write and configure the standalone consumer**

The consumer includes only `<wcw/Runtime.h>`, `<wcw/Theme.h>`, and `<wcw/Controls.h>`, initializes the runtime, creates a hidden Button, destroys it, and shuts down. Configure it as a separate CMake tree pointing at the repository through `add_subdirectory`.

- [ ] **Step 2: Verify the standalone boundary**

Configure and build the consumer. If it fails, preserve the exact failure as regression evidence and fix only the target metadata responsible. If it already passes, make no production change: this task is an integration verification, not a new production behavior that needs an artificial RED failure.

- [ ] **Step 3: Fix only public target metadata and register the smoke test**

Expose `include` through `BUILD_INTERFACE`, keep `src` private, propagate required compile definitions (`UNICODE`, `_UNICODE`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`) without leaking demo/test definitions, and add a CTest script that configures and builds the consumer.

- [ ] **Step 4: Run fresh full verification**

Run:

```powershell
cmake -S . -B build-debug -A x64
cmake --build build-debug --config Debug
ctest --test-dir build-debug -C Debug --output-on-failure
cmake -S . -B build-release -A x64
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure
```

Expected: Debug and Release builds exit 0; all tests, including the external consumer, pass.

- [ ] **Step 5: Perform the final manual matrix and commit**

Launch the Release demo and verify: Dark/Light, radius 0 and 24, 100% and 150% DPI, hover/pressed/focus/disabled/validation states, ComboBox popup, ScrollView drag/wheel, Tooltip timing, no visible standard control chrome, no clipping, no flicker, and keyboard-only traversal. Commit:

```powershell
git add CMakeLists.txt tests/consumer
git commit -m "test: verify external widget consumers"
```
