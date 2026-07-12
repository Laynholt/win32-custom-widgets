# Paint Buffer Lifetime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the existing custom-widget appearance by presenting every back buffer before its `BeginPaint` HDC becomes invalid.

**Architecture:** Keep the current `paint::Buffer` API and drawing code. Scope each buffer so its destructor performs `BitBlt` before the matching `EndPaint`; cover the behavior with one rendered-pixel assertion in the existing button test.

**Tech Stack:** C++20, Win32 GDI/GDI+, CMake, CTest, MSVC

## Global Constraints

- Preserve the existing dark and light visual design.
- Add no dependency or new rendering abstraction.
- Leave the unbuffered `PaintContent` path unchanged.
- Verify both Debug and Release configurations.

---

### Task 1: Reproduce and fix buffered paint presentation

**Files:**
- Modify: `tests/test_button_display.cpp:245`
- Modify: `src/Button.cpp:51`
- Modify: `src/BooleanControl.cpp:76`
- Modify: `src/Display.cpp:61`
- Modify: `src/TextBox.cpp:115`
- Modify: `src/Slider.cpp:103`
- Modify: `src/ProgressBar.cpp:69`
- Modify: `src/ComboBox.cpp:155`
- Modify: `src/ComboBox.cpp:212`
- Modify: `src/ScrollView.cpp:165`
- Modify: `src/Tooltip.cpp:246`

**Interfaces:**
- Consumes: `paint::Buffer(HDC, const RECT&)`, whose destructor presents the back buffer to the target HDC.
- Produces: no new public interface; only corrected `WM_PAINT` ordering.

- [ ] **Step 1: Write the failing rendered-pixel check**

Insert this block in `tests/test_button_display.cpp` immediately before `const auto bitmap = TestBitmap();`:

```cpp
    ShowWindow(parent, SW_SHOWNOACTIVATE);
    ShowWindow(button, SW_SHOWNOACTIVATE);
    CHECK(RedrawWindow(button, nullptr, nullptr,
                       RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW) != FALSE);
    const auto buttonDc = GetDC(button);
    CHECK(buttonDc != nullptr);
    if (buttonDc) {
        const auto input = wcw::DarkTheme().palette.input;
        CHECK(GetPixel(buttonDc, 5, 15) == RGB(input.r, input.g, input.b));
        ReleaseDC(button, buttonDc);
    }
```

- [ ] **Step 2: Build and run the focused test to verify RED**

Run:

```powershell
rtk cmake --build build-review-debug --config Debug --target ButtonDisplayTests
rtk ctest --test-dir build-review-debug -C Debug -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: `ButtonDisplayTests` fails at the new `GetPixel` comparison because the white/unpresented surface is read instead of `DarkTheme().palette.input`.

- [ ] **Step 3: Make every buffered paint scope end before `EndPaint`**

In each function listed below, add an inner `{` immediately before the existing `paint::Buffer buffer(target, bounds);` line and a matching `}` immediately before the existing `EndPaint(window, &ps);` line. Do not change the drawing statements inside the scope.

```text
src/Button.cpp          PaintButton
src/BooleanControl.cpp  PaintControl
src/Display.cpp         PaintDisplay
src/TextBox.cpp         PaintTextBox
src/Slider.cpp          PaintSlider
src/ProgressBar.cpp     PaintProgress
src/ComboBox.cpp        PaintCombo
src/ComboBox.cpp        PaintPopup
src/ScrollView.cpp      PaintScrollView
src/Tooltip.cpp         PaintTooltip
```

The exact resulting lifetime at all ten sites must be:

```cpp
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    {
        paint::Buffer buffer(target, bounds);
        // Keep the site's existing drawing block here without other changes.
    }
    EndPaint(window, &ps);
```

- [ ] **Step 4: Rebuild and verify GREEN in Debug**

Run:

```powershell
rtk cmake --build build-review-debug --config Debug --target ButtonDisplayTests
rtk ctest --test-dir build-review-debug -C Debug -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: `ButtonDisplayTests` passes.

- [ ] **Step 5: Verify the full Debug and Release suites and demo builds**

Run:

```powershell
rtk cmake --build build-review-debug --config Debug
rtk ctest --test-dir build-review-debug -C Debug --output-on-failure
rtk cmake --build build-review-release --config Release
rtk ctest --test-dir build-review-release -C Release --output-on-failure
```

Expected: both demo targets build and both test suites report `100% tests passed, 0 tests failed`.

- [ ] **Step 6: Review and commit only the paint fix**

Run:

```powershell
rtk git diff --check
rtk git diff -- tests/test_button_display.cpp src/Button.cpp src/BooleanControl.cpp src/Display.cpp src/TextBox.cpp src/Slider.cpp src/ProgressBar.cpp src/ComboBox.cpp src/ScrollView.cpp src/Tooltip.cpp
rtk git add tests/test_button_display.cpp src/Button.cpp src/BooleanControl.cpp src/Display.cpp src/TextBox.cpp src/Slider.cpp src/ProgressBar.cpp src/ComboBox.cpp src/ScrollView.cpp src/Tooltip.cpp
rtk git commit -m "fix: present widget buffers before EndPaint"
```

Expected: one focused commit containing the regression check and scope-ordering fix; `.codegraph/` remains untracked.
