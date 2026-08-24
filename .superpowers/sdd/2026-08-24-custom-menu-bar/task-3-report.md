# Task 3 report: Popup Switching and Standard Keyboard Mode

## Implementation

- Added the internal `PopupMenuResult`, `ShowMenuBarPopup`, and `MenuBarNextItemMessage` declarations.
- Refactored the existing popup command/runner path so `ShowPopupMenu` keeps its `bool` compatibility behavior while menu-bar popups can request a replacement top-level index.
- Extended `PopupController` with an optional menu-bar host, root `Left`/`Right` switching, signed `POINT*` host hit testing, hover/button switching, repeat-click dismissal, `VK_MENU` cancellation, and host-aware pointer interaction.
- Added menu-bar menu mode with saved-focus restoration, keyboard cues, F10/Alt/mnemonic routing through a parent subclass, wrapping arrows, `Down`/`Enter`/`Space` activation, Escape cancellation, and destruction/deactivation/item-mutation guards.
- Added timer-driven keyboard, mnemonic, hover, repeat-click, Alt, nested-submenu, focus-restoration, and normal menu-button regression coverage.

## Files

- `src/Internal.h`
- `src/Menu.cpp`
- `src/MenuBar.cpp`
- `tests/test_menu_bar.cpp`
- `tests/test_menu.cpp`

## RED

Command:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(MenuBarTests|MenuTests)" --output-on-failure
```

After the new tests compiled, the expected pre-implementation result was `MenuTests` passed and `MenuBarTests` failed at the new F10/focus, popup-switch, and nested-submenu assertions. This demonstrated the tests were exercising behavior absent from the existing implementation.

## GREEN

Focused verification:

```text
MenuTests    Passed
MenuBarTests Passed
100% tests passed, 0 tests failed out of 2
```

Full Debug verification:

```powershell
rtk cmake --build build --config Debug
rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, including `MenuTests`, `MenuBarTests`, `AccessibilityTests`, and `ConsumerSmoke`.

## Self-review

- Existing popup rendering, capture, accessibility, command dispatch, `std::vector<MenuItem>`, and normal menu-button/context-menu paths remain shared.
- Nested popup loops refresh `MenuBarState` after each run and do not dereference bar state after destruction or item replacement.
- Parent popup activation is distinguished from external `WM_ACTIVATE`; app deactivation still cancels, and cancellation during an open popup defers focus restoration until the controller unwinds.
- `git diff --check` is clean.

## Concerns

No known blockers. `WM_ACTIVATE` with a nonzero activating-window handle while a menu-bar popup is open is intentionally left to the popup controller; `WM_ACTIVATEAPP(FALSE)` remains an unconditional cancellation path.

## Reviewer fix round 1

Implementation:

- Routed F10, Alt state, and mnemonics from the focused child through per-menu-bar descendant subclasses, including descendants created after the bar; teardown removes each bar's subclass IDs on child, bar, and parent destruction.
- Shared the Alt shortcut state machine so any intervening `WM_SYSKEYDOWN` clears `altDown` before Alt release.
- Kept activation caused by the menu-bar popup distinct from same-process parent deactivation, cancelling the active controller for other activating windows.
- Strengthened popup switching coverage with File/View horizontal anchor movement and `MenuBarNextItemMessage` active-index confirmation.

Files:

- `src/MenuBar.cpp`
- `tests/test_menu_bar.cpp`
- `.superpowers/sdd/2026-08-24-custom-menu-bar/task-3-report.md`

Exact RED on `fa6c90b` after adding the focused-child/deactivation/switching assertions:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests; rtk ctest --test-dir build -C Debug -R '^MenuBarTests$' --output-on-failure
```

Build succeeded, then `MenuBarTests` failed (`exit_code=1`): focused-child F10 at `tests/test_menu_bar.cpp:313`, Alt focus restoration at `:355` and `:361`, and dependent popup/lifetime assertions at `:154`, `:164`, `:377`, and `:384`.

Exact focused GREEN:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests; rtk ctest --test-dir build -C Debug -R '^MenuBarTests$' --output-on-failure
```

Result: build succeeded; `MenuBarTests` passed 1/1, 100%.

Exact focused regression GREEN:

```powershell
rtk ctest --test-dir build -C Debug -R '^(MenuBarTests|MenuTests)$' --output-on-failure
```

Result: `MenuTests` and `MenuBarTests` passed 2/2, 100%.

Exact full Debug verification:

```powershell
rtk cmake --build build --config Debug; rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, 100%, including `MenuTests`, `MenuBarTests`, `AccessibilityTests`, and `ConsumerSmoke`; total test time 17.06 seconds.

Self-review:

- Popup rendering/controller ownership, normal `ShowPopupMenu` behavior, command dispatch, capture, accessibility, and `std::vector<MenuItem>` remain shared.
- Keyboard observers use one subclass ID per bar, choose one enabled visible bar for shared-parent routing, observe both existing and newly-created descendants, and remove observers before bar state teardown.
- Nested popup cancellation continues to refresh state only after `IsWindow(bar)` checks; focus restoration remains owned by the popup loop while it unwinds.
- `rtk git diff --check` is clean.

Concerns: none known. Popup activation is recognized by its owned parent plus `WS_POPUP | WS_EX_TOOLWINDOW`; other same-process `WM_ACTIVATE(WA_INACTIVE)` targets cancel the controller. `WM_ACTIVATEAPP(FALSE)` remains unconditional.

## Reviewer fix round 2

Implementation:

- Added an internal ownership query backed by the active `PopupController` level HWNDs; `WM_ACTIVATE` now accepts only a live popup window actually owned by the current controller, including nested popup levels.
- Removed the `WS_POPUP | WS_EX_TOOLWINDOW` style/owner heuristic, so a foreign same-owner toolwindow cancels the menu controller.
- Added a timer-driven regression using a foreign same-owner `WS_EX_TOOLWINDOW` and preserved destruction-safe cancellation behavior.

Files:

- `src/Internal.h`
- `src/Menu.cpp`
- `src/MenuBar.cpp`
- `tests/test_menu_bar.cpp`
- `.superpowers/sdd/2026-08-24-custom-menu-bar/task-3-report.md`

Exact RED on `ecdbae5` after adding the foreign same-owner toolwindow regression:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests; rtk ctest --test-dir build -C Debug -R '^MenuBarTests$' --output-on-failure
```

Build succeeded, then `MenuBarTests` failed (`exit_code=1`) at `tests/test_menu_bar.cpp:164: CHECK(false) failed` while the foreign toolwindow incorrectly remained treated as a menu popup.

Exact focused GREEN:

```powershell
rtk cmake --build build --config Debug --target MenuBarTests; rtk ctest --test-dir build -C Debug -R '^MenuBarTests$' --output-on-failure
```

Result: build succeeded; `MenuBarTests` passed 1/1, 100%.

Exact focused regression GREEN:

```powershell
rtk ctest --test-dir build -C Debug -R '^(MenuBarTests|MenuTests)$' --output-on-failure
```

Result: `MenuTests` and `MenuBarTests` passed 2/2, 100%.

Exact full Debug verification:

```powershell
rtk cmake --build build --config Debug; rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, 100%, including `MenuTests`, `MenuBarTests`, `AccessibilityTests`, and `ConsumerSmoke`; total test time 26.30 seconds.

Self-review: exact controller HWND ownership is queried before accepting activation, live-window checks remain in the menu-bar path, and normal popup/menu-button behavior remains unchanged. `rtk git diff --check` is clean.

Concerns: none known.
