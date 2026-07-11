# Task 4 Report: Button And Display Controls

## Result

Implemented the requested public options/image types and HWND factories for Button, IconButton,
Label, ImageView, Separator, and Panel. Controls register during `wcw::Initialize`, use the Task 3
style/runtime/paint helpers, and paint through `paint::Buffer`.

## TDD Evidence

- RED command: `cmake -S . -B build; cmake --build build --target ButtonDisplayTests --config Debug`
- RED result: failed with `error C1083: ... wcw/Controls.h: No such file or directory`.
- Focused GREEN command: `ctest --test-dir build -C Debug -R ButtonDisplayTests --output-on-failure`
- Focused GREEN result: `1/1` passed, `0` failed.
- Full verification command: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
- Full verification result: build exit `0`; `4/4` tests passed, `0` failed.
- Diff check: `git diff --check` exited `0` before commit.

## Behavior Covered

- Every factory creates a hidden child HWND with the supplied text, ID, DIP bounds, and style.
- Button and IconButton share one registered implementation and store real hover, capture,
  mouse-press, and keyboard-press state.
- Mouse activation requires a captured left-button release inside the client rectangle.
- Space and Enter activate on key release; disabled buttons cannot send `BN_CLICKED`.
- Display controls remove `WS_TABSTOP`, return `MA_NOACTIVATE`, and do not take focus.
- All controls accept `StyleOverride{.cornerRadiusDip = 0.0f}`.
- ImageView supports contain, cover, and stretch; drawing is clipped to the resolved rounded shape.
- All control painting is double-buffered and uses resolved theme/local styles.

## Files

- `include/wcw/Controls.h` — public Task 4 types and factories.
- `src/Button.cpp` — Button/IconButton creation, interaction, notification, and paint.
- `src/Display.cpp` — Label/ImageView/Separator/Panel creation and paint.
- `src/Internal.h` — internal class-registration declarations.
- `src/Runtime.cpp` — Task 4 registration during initialization.
- `tests/test_button_display.cpp` — creation, ID/text, focus, activation, disabled, radius tests.
- `CMakeLists.txt` — sources and focused test target.

## Commit

`3bb0710` — `feat: add buttons and display controls`

## Concerns

None known within Task 4 scope. `ImageSource` remains intentionally non-owning, and no layout,
file loading, future controls, or separate IconButton window class was added.

## Review Fix Follow-up

### RED Evidence

- Focused command: `cmake --build build --target ButtonDisplayTests --config Debug; ctest --test-dir build -C Debug -R ButtonDisplayTests --output-on-failure`
- Behavioral RED: `ButtonDisplayTests` failed with 10 checks covering fractional-DIP bounds,
  focus/cancel/capture activation cancellation, cancel-button Escape activation, display focus, and
  buffered ImageView rendering.
- Label-font RED: the focused target then failed to link with unresolved
  `wcw::internal::ResolveLabelFont`, proving the new resolver path did not yet exist.

### Fixes

- Button mouse and keyboard press state is separate and centrally cancelled on disable, focus loss,
  `WM_CANCELMODE`, `WM_CAPTURECHANGED`, and destruction; owned capture is released.
- `isDefault` reports `DLGC_DEFPUSHBUTTON` for Win32 dialog-manager Enter handling, while
  `isCancel` gives a focused button Escape activation. Both contracts are documented and tested.
- Label painting uses `ResolveLabelFont`, preserving the theme label font fallback while honoring a
  local `StyleOverride::font`.
- Button and display buffers clear the complete client rectangle before antialiased rounded paint.
- Both Task 4 window procedures catch `std::bad_alloc` and all other C++ exceptions, return `FALSE`
  for failed `WM_NCCREATE`, and set `ERROR_NOT_ENOUGH_MEMORY` or `ERROR_GEN_FAILURE`.
- Display controls reject programmatic focus; bitmap tests execute contain, cover, and stretch paint
  paths with a real `HBITMAP` and prove the handle remains caller-owned after destruction.
- Task 4 DIP window bounds and integral paint measurements now use `DipToPx` rounding.

### Files

- `include/wcw/Controls.h`
- `src/Button.cpp`
- `src/Display.cpp`
- `src/Internal.h`
- `src/Paint.cpp`
- `src/Paint.h`
- `tests/test_button_display.cpp`
- `.superpowers/sdd/task-4-report.md`

### GREEN Evidence

- Focused command: `cmake --build build --target ButtonDisplayTests --config Debug; ctest --test-dir build -C Debug -R ButtonDisplayTests --output-on-failure`
- Focused result: `1/1` passed, `0` failed.
- Full command: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
- Full result: Debug build exit `0`; `4/4` tests passed, `0` failed.
- Fix commit: `ef31e27` — `fix: address Task 4 review findings`.

### Concerns

None within Task 4 scope. Cancel activation intentionally applies while the cancel button owns focus;
dialog-wide `IDCANCEL` routing remains the Win32 dialog manager's responsibility.

## Dialog Integration And Key-Matching Follow-up

### RED Evidence

- Command: `cmake --build build --target ButtonDisplayTests --config Debug; ctest --test-dir build -C Debug -R ButtonDisplayTests --output-on-failure`
- Build result: `ButtonDisplayTests.vcxproj` built successfully.
- Test result: `ButtonDisplayTests` failed with 10 checks at lines 175, 179, 185, 194, 203,
  215, 217, 220, 224, and 231. The failures proved mixed key-up activation, absent
  `DM_SETDEFID` registration, absent enabled/disabled `BM_CLICK` behavior, and a cancel button whose
  ID was not `IDCANCEL`.

### Fixes

- Button state stores the pressed virtual key; only the matching `WM_KEYUP` activates, while the
  existing focus-loss, disable, cancel-mode, capture-change, and destruction paths clear it.
- `BM_CLICK` emits one enabled `BN_CLICKED` notification and none while disabled.
- Default creation sends `DM_SETDEFID` to the dialog parent, allowing `IsDialogMessage` Enter
  handling to route through `BM_CLICK`.
- Cancel creation normalizes the control ID to `IDCANCEL`; the public option comment documents the
  contract, and a real dialog test proves `IsDialogMessage` Escape produces `IDCANCEL`.

### GREEN Evidence

- Focused command: `cmake --build build --target ButtonDisplayTests --config Debug; ctest --test-dir build -C Debug -R ButtonDisplayTests --output-on-failure`
- Focused result: build exit `0`; `1/1` passed, `0` failed.
- Full command: `cmake --build build --config Debug; ctest --test-dir build -C Debug --output-on-failure`
- Full result: Debug build exit `0`; `4/4` tests passed, `0` failed.
- Diff check: `git diff --check` exited `0` before the fix commit.
- Fix commit: `20b3c99` — `fix: integrate buttons with dialog manager`.

### Concerns

None within Task 4 scope. `isCancel` intentionally overrides a conflicting caller-supplied ID with
the documented Win32 `IDCANCEL` contract.
