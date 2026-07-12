# Rounded Controls and Edit Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove rectangular corner artifacts, render a uniform thin border, and vertically center native editable text and selection.

**Architecture:** Keep the existing opaque Win32 child controls and GDI+ renderer. Apply a native rounded window region from the shared runtime, inset only border geometry inside the drawable pixel bounds, and size the child `EDIT` from its active font metrics.

**Tech Stack:** C++20, Win32 USER/GDI/GDI+, CMake, CTest, MSVC

## Global Constraints

- Preserve the current dark/light palette and one-DIP default border.
- Add no dependency and no layered-window or parent-background-copying path.
- A zero corner radius restores the default rectangular window region.
- Editable text remains a native `EDIT`; do not custom-draw selection or caret.
- Verify the final Release build and all tests in `build`.

---

### Task 1: Clip rounded controls to a native window region

**Files:**
- Modify: `tests/test_button_display.cpp:134`
- Modify: `src/Runtime.cpp:1-43`
- Modify: `src/Runtime.cpp:192-219`

**Interfaces:**
- Consumes: `ResolveStyle(const Theme&, const StyleOverride&)`, `DipToPx(float, unsigned)`, `internal::ThemeChangedMessage`.
- Produces: private `void UpdateWindowRegion(HWND window)`; no public API change.

- [ ] **Step 1: Add a failing rounded-region check**

Insert after the existing `createdBounds` size assertion in `tests/test_button_display.cpp`:

```cpp
    CHECK(wcw::SetStyleOverride(button, {.cornerRadiusDip = 12.0f}));
    const auto region = CreateRectRgn(0, 0, 0, 0);
    CHECK(region != nullptr);
    CHECK(GetWindowRgn(button, region) != ERROR);
    RECT buttonBounds{};
    GetClientRect(button, &buttonBounds);
    CHECK(!PtInRegion(region, 0, 0));
    CHECK(PtInRegion(region, buttonBounds.right / 2, buttonBounds.bottom / 2));
    DeleteObject(region);
    CHECK(wcw::SetStyleOverride(button, {.cornerRadiusDip = 0.0f}));
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk cmake --build build --config Release --target ButtonDisplayTests
rtk ctest --test-dir build -C Release -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: `ButtonDisplayTests` fails because `GetWindowRgn` returns `ERROR` and the control center is absent from the empty test region.

- [ ] **Step 3: Add the shared native-region updater**

Add `#include <wcw/Geometry.h>` and `#include <algorithm>` to `src/Runtime.cpp`. Add this function in its anonymous namespace after `IsOwnedLibraryWindow`:

```cpp
void UpdateWindowRegion(HWND window) {
    RECT bounds{};
    if (!GetClientRect(window, &bounds)) return;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    const auto found = State().overrides.find(window);
    const auto local = found == State().overrides.end() ? StyleOverride{} : found->second;
    const auto style = ResolveStyle(State().theme, local);
    const int radius = (std::max)(
        0, (std::min)({DipToPx(style.cornerRadiusDip, paint::Dpi(window)), width / 2, height / 2}));
    if (!radius) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }
    const auto region = CreateRoundRectRgn(0, 0, width + 1, height + 1,
                                           radius * 2, radius * 2);
    if (region && !SetWindowRgn(window, region, TRUE)) DeleteObject(region);
}
```

At the start of `internal::HandleControlMessage`, immediately after the accessibility check, update the region without consuming `WM_SIZE`:

```cpp
    if (message == WM_SIZE || message == ThemeChangedMessage) UpdateWindowRegion(window);
```

- [ ] **Step 4: Rebuild and verify GREEN**

Run:

```powershell
rtk cmake --build build --config Release --target ButtonDisplayTests
rtk ctest --test-dir build -C Release -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: `ButtonDisplayTests` passes.

- [ ] **Step 5: Commit the region fix**

```powershell
rtk git add tests/test_button_display.cpp src/Runtime.cpp
rtk git commit -m "fix: clip rounded controls to their shape"
```

---

### Task 2: Keep the thin border visible on all four sides

**Files:**
- Modify: `tests/test_button_display.cpp:245-255`
- Modify: `src/Paint.cpp:92-99`

**Interfaces:**
- Consumes: `paint::Border(Gdiplus::Graphics&, const Gdiplus::RectF&, float, Color, float)`.
- Produces: the same function with corrected internal outline bounds; no signature change.

- [ ] **Step 1: Add failing four-edge pixel checks**

Inside the existing `if (buttonDc)` block in `tests/test_button_display.cpp`, after the input-surface assertion, add:

```cpp
        RECT rendered{};
        GetClientRect(button, &rendered);
        const auto border = wcw::DarkTheme().palette.border;
        const auto borderColor = RGB(border.r, border.g, border.b);
        CHECK(GetPixel(buttonDc, rendered.right / 2, 0) == borderColor);
        CHECK(GetPixel(buttonDc, rendered.right / 2, rendered.bottom - 1) == borderColor);
        CHECK(GetPixel(buttonDc, 0, rendered.bottom / 2) == borderColor);
        CHECK(GetPixel(buttonDc, rendered.right - 1, rendered.bottom / 2) == borderColor);
```

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
rtk cmake --build build --config Release --target ButtonDisplayTests
rtk ctest --test-dir build -C Release -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: the right and/or bottom edge assertion fails because the current path uses coordinates outside the final drawable pixel.

- [ ] **Step 3: Inset the outline's right and bottom coordinates**

Replace `paint::Border` in `src/Paint.cpp` with:

```cpp
void Border(Gdiplus::Graphics& graphics, const Gdiplus::RectF& bounds, float radius, Color color,
            float width) {
    if (width <= 0) return;
    Gdiplus::Pen pen(GdiPlusColor(color), width);
    pen.SetAlignment(Gdiplus::PenAlignmentInset);
    auto outline = bounds;
    outline.Width = (std::max)(0.0f, outline.Width - 1.0f);
    outline.Height = (std::max)(0.0f, outline.Height - 1.0f);
    const auto path = RoundedPath(outline, radius);
    graphics.DrawPath(&pen, path.get());
}
```

- [ ] **Step 4: Rebuild and verify GREEN**

```powershell
rtk cmake --build build --config Release --target ButtonDisplayTests
rtk ctest --test-dir build -C Release -R '^ButtonDisplayTests$' --output-on-failure
```

Expected: `ButtonDisplayTests` passes with the border color on all four edge midpoints.

- [ ] **Step 5: Commit the border fix**

```powershell
rtk git add tests/test_button_display.cpp src/Paint.cpp
rtk git commit -m "fix: draw control borders on every edge"
```

---

### Task 3: Center native editable text by its active font metrics

**Files:**
- Modify: `tests/test_text_numeric.cpp:114-120`
- Modify: `src/TextBox.cpp:47-62`
- Modify: `src/TextBox.cpp:211-214`
- Modify: `src/TextBox.cpp:275-277`

**Interfaces:**
- Consumes: the native child `EDIT`, `WM_GETFONT`, and `GetTextMetricsW`.
- Produces: private `int EditLineHeight(HWND edit, const ResolvedStyle& style, unsigned dpi)` and updated `LayoutEdit`; no public API change.

- [ ] **Step 1: Add a failing line-height and centering check**

Insert after the child-edit style assertions in `tests/test_text_numeric.cpp`:

```cpp
    const auto editDc = GetDC(edit);
    CHECK(editDc != nullptr);
    const auto editFont = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    const auto previousFont = editFont ? SelectObject(editDc, editFont) : nullptr;
    TEXTMETRICW metrics{};
    CHECK(GetTextMetricsW(editDc, &metrics) != FALSE);
    if (previousFont && previousFont != HGDI_ERROR) SelectObject(editDc, previousFont);
    ReleaseDC(edit, editDc);

    RECT editBounds{};
    RECT textBoxBounds{};
    GetWindowRect(edit, &editBounds);
    MapWindowPoints(nullptr, textBox, reinterpret_cast<POINT*>(&editBounds), 2);
    GetClientRect(textBox, &textBoxBounds);
    CHECK(editBounds.bottom - editBounds.top == metrics.tmHeight);
    CHECK(std::abs(editBounds.top + editBounds.bottom - textBoxBounds.bottom) <= 1);
```

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
rtk cmake --build build --config Release --target TextNumericTests
rtk ctest --test-dir build -C Release -R '^TextNumericTests$' --output-on-failure
```

Expected: `TextNumericTests` fails because the child `EDIT` currently fills the padding-derived height rather than the font line height.

- [ ] **Step 3: Measure and center the child edit**

Add this helper before `LayoutEdit` in `src/TextBox.cpp`:

```cpp
int EditLineHeight(HWND edit, const ResolvedStyle& style, unsigned dpi) {
    int height = (std::max)(1, DipToPx(style.font.sizeDip, dpi));
    const auto dc = GetDC(edit);
    if (!dc) return height;
    const auto font = reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0));
    const auto previous = font ? SelectObject(dc, font) : nullptr;
    TEXTMETRICW metrics{};
    if (GetTextMetricsW(dc, &metrics)) height = metrics.tmHeight;
    if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
    ReleaseDC(edit, dc);
    return height;
}
```

Replace `LayoutEdit` with:

```cpp
void LayoutEdit(HWND window, TextBoxState& state) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(window));
    const auto dpi = paint::Dpi(window);
    const int x = DipToPx(style.paddingXDip, dpi);
    const int height = (std::min)(static_cast<int>(bounds.bottom),
                                  EditLineHeight(state.edit, style, dpi));
    const int y = (std::max)(0, (static_cast<int>(bounds.bottom) - height) / 2);
    MoveWindow(state.edit, x, y, (std::max)(0, static_cast<int>(bounds.right) - x * 2),
               height, TRUE);
}
```

Extend the existing `ThemeChangedMessage` pre-handler so font changes also relayout:

```cpp
    if (message == internal::ThemeChangedMessage && state) {
        RefreshBrush(window, *state);
        ApplyFont(window, *state);
        LayoutEdit(window, *state);
    }
```

Replace the `WM_SETFONT` case with:

```cpp
    case WM_SETFONT:
        if (state) {
            SendMessageW(state->edit, WM_SETFONT, wParam, lParam);
            LayoutEdit(window, *state);
        }
        return 0;
```

- [ ] **Step 4: Rebuild and verify GREEN**

```powershell
rtk cmake --build build --config Release --target TextNumericTests
rtk ctest --test-dir build -C Release -R '^TextNumericTests$' --output-on-failure
```

Expected: `TextNumericTests` passes.

- [ ] **Step 5: Commit the edit alignment fix**

```powershell
rtk git add tests/test_text_numeric.cpp src/TextBox.cpp
rtk git commit -m "fix: center native edit text vertically"
```

---

### Task 4: Verify the complete final Release build

**Files:**
- Verify only: `build/bin/Release/Win32CustomWidgetsDemo.exe`

**Interfaces:**
- Consumes: the three preceding commits.
- Produces: a tested final Release artifact in `build`.

- [ ] **Step 1: Build every Release target**

```powershell
rtk cmake --build build --config Release
```

Expected: exit code `0`, including `Win32CustomWidgetsDemo.exe`.

- [ ] **Step 2: Run the complete Release suite**

```powershell
rtk ctest --test-dir build -C Release --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 11`.

- [ ] **Step 3: Review repository state**

```powershell
rtk git diff --check
rtk git status --short
```

Expected: no source changes remain; only the pre-existing untracked `.codegraph/` may be listed.
