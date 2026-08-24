# Task 4 report: Menu Bar Accessibility

## Implementation

- Generalized the existing MSAA menu provider to support `AccessibleKind::MenuBar`.
- Added menu-bar children with mnemonic-free names, screen bounds, enabled state,
  popup/expanded state, one-based rows, focus, hit testing, navigation, and
  default actions.
- Registered and synchronized menu-bar accessibility through layout, focus,
  selection, popup open/close, enable/show, theme/DPI, and item replacement
  paths; emitted child focus and expanded-state WinEvents.
- Added integration assertions for role, children, states, names, bounds,
  hit testing, focus, default action, popup expansion, and notifications.

## Files

- `src/Accessibility.h`
- `src/Accessibility.cpp`
- `src/MenuBar.cpp`
- `tests/test_accessibility.cpp`

## RED

Commands:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests
rtk ctest --test-dir build -C Debug -R "AccessibilityTests" --output-on-failure
```

The build succeeded. `AccessibilityTests` failed as expected on the new
MenuBar assertions: the control exposed neither `ROLE_SYSTEM_MENUBAR` nor its
top-level children/states/actions/events.

## GREEN

Focused commands:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(AccessibilityTests|MenuBarTests|MenuTests)" --output-on-failure
```

Result: all 3/3 focused tests passed.

Full Debug command:

```powershell
rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, including `AccessibilityTests`,
`MenuTests`, `MenuBarTests`, and `ConsumerSmoke`.

Additional check:

```powershell
rtk git diff --check
```

Result: clean.

## Concerns

No known blockers. High Contrast colors continue through the existing shared
`ResolveMenuColors` system-color fallback; this task does not mutate global OS
contrast settings.

## Reviewer fix round 1

Implementation:

- Menu-bar child state and default actions now honor `EffectivelyEnabled`,
  including disabled ancestors.
- Menu-bar accessibility bounds refresh on `WM_MOVE` and
  `WM_DPICHANGED_AFTERPARENT` without rerunning layout for pure relocation.
- Popup accessibility marks a selected parent item expanded while its child
  popup exists and returns to collapsed when that child closes.
- Added `accNavigate` and `SetMenuBarItems` refresh assertions.

Files:

- `src/Accessibility.cpp`
- `src/Menu.cpp`
- `src/MenuBar.cpp`
- `tests/test_accessibility.cpp`
- `tests/test_menu.cpp`

Exact RED:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests MenuTests
rtk ctest --test-dir build -C Debug -R "(AccessibilityTests|MenuTests)" --output-on-failure
```

The build succeeded, then both tests failed as expected: MenuBar screen
bounds stayed stale after relocation, disabled ancestors left children
focusable and default actions successful, and an open popup parent remained
collapsed.

Exact focused GREEN:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(AccessibilityTests|MenuBarTests|MenuTests)" --output-on-failure
```

Result: all 3/3 focused tests passed.

Exact full Debug verification:

```powershell
rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, including `AccessibilityTests`,
`MenuTests`, `MenuBarTests`, and `ConsumerSmoke`.

`rtk git diff --check` is clean.

Concerns: none known. High Contrast behavior remains delegated to the
existing shared menu-color resolver.

## Reviewer fix round 2

Implementation:

- `WM_DPICHANGED_AFTERPARENT` now runs the existing `Layout` path before
  accessibility synchronization, so DPI-dependent item rectangles are
  recalculated.
- The existing per-menu-bar `MenuBarParentProc` now synchronizes accessibility
  on parent `WM_MOVE`, keeping child menu-bar screen bounds current after
  parent relocation without polling or a global hook.
- Added regressions for parent relocation and for a DPI-after-parent layout
  notification isolated from the normal `WM_SIZE` path.

Files:

- `src/MenuBar.cpp`
- `tests/test_accessibility.cpp`

Exact RED:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests
rtk ctest --test-dir build -C Debug -R "AccessibilityTests" --output-on-failure
```

The build succeeded, then `AccessibilityTests` failed as expected: parent
relocation left the cached screen bounds unchanged, and the isolated
`WM_DPICHANGED_AFTERPARENT` layout assertion observed the old item height.

Exact focused GREEN:

```powershell
rtk cmake --build build --config Debug --target AccessibilityTests MenuBarTests MenuTests
rtk ctest --test-dir build -C Debug -R "(AccessibilityTests|MenuBarTests|MenuTests)" --output-on-failure
```

Result: all 3/3 focused tests passed.

Exact full Debug verification:

```powershell
rtk ctest --test-dir build -C Debug --output-on-failure
```

Result: all 13/13 CTest targets passed, including `AccessibilityTests`,
`MenuTests`, `MenuBarTests`, and `ConsumerSmoke`.

`rtk git diff --check` is clean.

Concerns: none known. The DPI regression uses a test-only subclass to suppress
the normal size notification while exercising the `WM_DPICHANGED_AFTERPARENT`
layout path; no manual/UI inspection was performed.
