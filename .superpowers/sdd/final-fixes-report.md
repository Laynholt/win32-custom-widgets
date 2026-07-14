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

## Residual review follow-up

Two residual findings were fixed after the final review of commit `81bf294`.

### Real pointer routing outside the popup chain

The nested popup loop now inspects queued `WM_POINTERDOWN` and `WM_POINTERUP` messages before dispatch. If their screen point is outside every popup level, it cancels the controller and still translates and dispatches the external message.

The regression uses Windows touch injection and a visible separate target window rather than sending a pointer message directly to the popup. It covers both directions independently:

- injects a new contact outside while the menu is running to route `WM_POINTERDOWN` to the target;
- establishes a contact on the target before opening the menu, then injects the release while the nested loop is running to route `WM_POINTERUP` to the target.

The target consumes the pointer messages to suppress promoted mouse input, so cancellation cannot be attributed to the existing mouse-capture path. Target message counts verify that cancellation does not swallow the external pointer message.

### DPI changes

`WM_DPICHANGED` now cancels the popup. This avoids retaining row bounds, content height, scroll state, and accessibility geometry measured for the previous DPI.

### RED/GREEN evidence

With only the new tests present, focused Release `MenuTests` failed exactly at the fallback assertions for routed pointer down, routed pointer up, and `WM_DPICHANGED`; all separate-target delivery assertions passed. After the two production edits:

```text
rtk proxy .\build-final-release\Release\MenuTests.exe
Exit code: 0
```

Fresh x64 Debug and Release trees were then configured and completely built. Final sequential suite results:

```text
rtk proxy ctest --test-dir build-residual-debug -C Debug --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 36.85 sec

rtk proxy ctest --test-dir build-residual-release -C Release --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 11.48 sec
```

The first Release suite and its first isolated retry encountered the same unchanged `ValueControlsTests` 40 ms `GetUpdateRect` timing flake noted above. The next isolated run passed, followed by the clean complete Release result recorded here. `MenuTests` passed in every GREEN/full-suite run.

## Final routing and region follow-up

Two findings remaining after commit `b9d9328` were handled with another RED/GREEN cycle.

### Dispatch only after complete teardown

An outside queued pointer message is now copied and removed from the nested loop without immediate dispatch. The controller then releases/restores capture, destroys the full popup hierarchy, removes the owner subclass, restores focus, and clears `activeController`. Only after that cleanup does it translate and dispatch the saved external message.

The genuine touch-injection regression makes the separate target window synchronously call `ShowContextMenu` from its pointer handler. It asserts that the original popup no longer exists, the replacement does not fail with `ERROR_BUSY`, and a controlled timer safely closes the replacement. The existing `WM_QUIT` test remains unchanged and passes; quit messages are not deferred because `GetMessageW` returns zero before pointer-message handling.

### Rounded-region containment

A shared internal helper now checks the screen rectangle, converts the point to window coordinates, and queries the actual window region. A missing/error region falls back to rectangular containment, while an empty region contains no points. The helper is reused by popup hierarchy containment, menu row hit-testing, and menu MSAA hit-testing.

The regressions place the separate pointer target immediately behind a rounded popup and inject a real contact at the visually clipped top-left corner. They verify that the contact dismisses the menu and is still delivered to the target. The accessibility regression verifies that `accHitTest` returns `S_FALSE`/`VT_EMPTY` for the same clipped corner.

### RED/GREEN and final verification

In the test-only RED state, focused Release `MenuTests` failed only on the intended observations:

- MSAA returned the menu self for the clipped corner instead of `S_FALSE`/`VT_EMPTY`;
- the pointer target saw the original popup still alive, and synchronous replacement opening returned `ERROR_BUSY`;
- the real clipped-corner contact reached the target but required the fallback timer to close the menu.

After the production changes, focused Release `MenuTests` exited `0`. Fresh x64 Debug and Release trees were configured, completely built, and tested sequentially:

```text
rtk proxy ctest --test-dir build-review2-debug -C Debug --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 36.86 sec

rtk proxy ctest --test-dir build-review2-release -C Release --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 9.67 sec
```

The first Release run encountered an unrelated `ButtonDisplayTests` `chevronPixel` visual sampling failure. It passed immediately in isolation and in the complete clean rerun above. `MenuTests` passed in every GREEN and full-suite run.

## Deterministic menu-button pixel capture

The remaining `ButtonDisplayTests` flake came from sampling a visible window twice with `GetDC`: one scan established the foreground bounds and a second scan searched separately for text and chevron pixels. Those live-window reads could observe compositor/occlusion timing instead of one stable rendered frame.

The unchanged Release binary reproduced the failure under a ten-run stress loop: run 2 failed at the existing `CHECK(chevronPixel)` while the other nine runs passed. This established a focused RED before changing the test.

Only `tests/test_button_display.cpp` changed. The menu-button check now creates a top-down 32-bit DIB, selects it into a compatible memory DC, and uses `PrintWindow(..., PW_CLIENTONLY)` to synchronously request the client rendering. A direct `WM_PRINTCLIENT` experiment left the sentinel bitmap untouched because the custom button class has no explicit handler; the `PrintWindow` client path provides the corresponding capture without adding test-only behavior to the production widget. Both the foreground-bounds scan and the chevron/text scan use that single off-screen DC. The strict `chevronPixel` and `textRight < chevronArea.left` assertions are unchanged, and no sleeps were added.

The modified focused Release test then passed ten consecutive runs. Fresh x64 Debug and Release trees were configured, completely built, and tested sequentially:

```text
rtk proxy ctest --test-dir build-button-capture-debug -C Debug --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 35.82 sec

rtk proxy ctest --test-dir build-button-capture-release -C Release --output-on-failure
100% tests passed, 0 tests failed out of 12
Total Test time (real) = 41.28 sec
```
