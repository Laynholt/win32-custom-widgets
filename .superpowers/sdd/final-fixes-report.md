# Final review fixes: RED/GREEN report

Date: 2026-07-14
Branch: `master`
Base: `ddd36a9`
Scope: the seven final-review findings for Fluent popup menus; no public API or dependency changes.

## Test package

All regression coverage was added to the existing `tests/test_menu.cpp` executable so the change remains one focused package. The tests use real popup windows and messages rather than mocks:

- a menu button is destroyed both from synchronous `WM_COMMAND` and while its popup loop is active;
- `IAccessible` is obtained with `AccessibleObjectFromWindow` and queried through `accHitTest`, `accLocation`, `get_accState`, and `get_accDefaultAction`;
- a 100-row popup is queried before and after real `WM_MOUSEWHEEL` scrolling;
- external `WM_RBUTTONUP`, `WM_MBUTTONUP`, and `WM_POINTERUP` messages are sent while the popup owns capture;
- internal right/middle releases are checked for backward-compatible non-activation;
- `GetWindowRgn` and `PtInRegion` verify rounded corners and a retained center;
- an out-of-context WinEvent hook verifies the parent `EVENT_OBJECT_FOCUS` notification after `VK_LEFT` closes a submenu.

## RED

Production code was unchanged when the initial regression package was built and run:

```text
rtk proxy cmake --build build --config Release --target MenuTests
rtk proxy .\build\Release\MenuTests.exe
```

The executable exited `1` with 14 expected assertions:

1. menu `accHitTest` did not return child ID 1;
2. a submenu item's default action was `Execute`, not `Open`;
3. `GetWindowRgn` returned `ERROR` and the region had no center;
4. `VK_LEFT` produced no parent accessibility focus event;
5. external right click reached the fallback Escape timer;
6. external middle click reached the fallback Escape timer;
7. external pointer release reached the fallback Escape timer;
8. visible-row hit testing did not return the virtual child;
9. the last hidden row lacked `STATE_SYSTEM_OFFSCREEN`;
10. the last hidden row retained an unclipped non-empty location;
11. the first row lacked `STATE_SYSTEM_OFFSCREEN` after scrolling it out;
12. the first row retained an unclipped non-empty location after scrolling;
13. destroying the menu-button source did not end the popup loop before the fallback timer;
14. the popup source-destruction path therefore exercised the stale post-popup `ButtonState` continuation.

The initial WinEvent test used `WINEVENT_INCONTEXT` without a hook DLL and correctly failed at hook creation; the harness was corrected to install `WINEVENT_OUTOFCONTEXT` only after opening the child. Re-running RED removed the harness error and retained the missing-focus-event failure.

During GREEN review, an additional backward-compatibility test was added before changing production code further. It exited `1` with four expected assertions showing that the first mouse implementation incorrectly activated rows on internal right/middle releases. The final implementation only dismisses those buttons outside the popup chain.

## GREEN changes

### 1. Button lifetime / UAF

- `WM_NCDESTROY` now calls `CancelPopupMenu(window)` before deleting `ButtonState`.
- `ActivateButton` remembers the opened state pointer, then after synchronous popup/command dispatch checks both `IsWindow(window)` and the current `GWLP_USERDATA` pointer before clearing `menuOpen` or notifying accessibility.

### 2. Menu MSAA hit testing

- `AccessibleKind::Menu::accHitTest` scans visible virtual child bounds and returns the matching one-based `VT_I4` child ID.
- Empty menu surface returns `CHILDID_SELF`; points outside retain the standard accessible fallback.

### 3. Scrolled accessibility geometry

- Accessibility rows are intersected with the popup window bounds.
- Fully hidden rows retain an empty location and an `offscreen` bit that maps to `STATE_SYSTEM_OFFSCREEN`.
- `accLocation` and menu `accHitTest` consume those clipped bounds.

### 4. Non-primary mouse and pointer dismissal

- External right/middle releases dismiss through the existing popup-chain containment check but do not activate internal rows.
- `WM_POINTERUP` consumes its documented screen coordinates and uses the same existing row hit-test/activation/dismissal flow as primary pointer input.

### 5. Rounded popup region

- Each popup applies a DPI-scaled native `CreateRoundRectRgn`/`SetWindowRgn` region after creation.
- The region is reapplied on `WM_SIZE` and after applying the suggested bounds from `WM_DPICHANGED`.

### 6. `VK_LEFT` accessibility focus

- Closing a child refreshes the parent menu accessibility snapshot and emits `EVENT_OBJECT_FOCUS` for its still-selected virtual child.

### 7. Default action

- A menu child with `hasPopup` reports `Open`; leaf rows report `Execute`.

Focused GREEN:

```text
rtk proxy .\build-final-release\Release\MenuTests.exe
Exit code: 0
```

## Fresh verification

Fresh x64 trees were configured independently and completely built:

```text
rtk proxy cmake -S . -B build-final-debug -A x64 -DCMAKE_CONFIGURATION_TYPES=Debug
rtk proxy cmake --build build-final-debug --config Debug
Exit code: 0

rtk proxy cmake -S . -B build-final-release -A x64 -DCMAKE_CONFIGURATION_TYPES=Release
rtk proxy cmake --build build-final-release --config Release
Exit code: 0
```

Final test evidence after the last production/test change:

```text
rtk proxy ctest --test-dir build-final-debug -C Debug --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time: 18.02 sec

rtk proxy ctest --test-dir build-final-release -C Release --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time: 13.51 sec
```

`git diff --check` also exited `0`.

## Verification notes / concerns

- Debug and Release GUI suites must run sequentially: running them concurrently causes cross-process collisions because existing tests use global `FindWindow` class-name lookup. No product workaround was added.
- One unchanged Release `ValueControlsTests` timing assertion (`GetUpdateRect`) failed once, passed immediately in isolation, and passed in the two subsequent complete Release runs. The final required Release result is clean 12/12.
- No remaining functional concern was found in the requested scope.
