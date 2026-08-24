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
