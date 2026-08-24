# Task 5 report

Status: complete for demo and consumer documentation.

Implemented in `demo/main.cpp`:

- Added a fixed 32-DIP `CreateMenuBar` with File/Edit/View/Help items, nested actions,
  mnemonics, shortcut labels, an icon, checked theme entries, and a disabled item.
- Shifted the existing responsive gallery below the bar without removing buttons, the menu
  button, context menu, tooltip examples, scroll content, or any existing public `Create*` use.
- Wired menu commands to the status label, theme/radius behavior, and `WM_CLOSE`; button theme
  toggles refresh the menu check state as well.

Implemented in `README.md`:

- Added a reusable `MenuBarOptions`/`CreateMenuBar` example and documented CMake integration,
  `WM_COMMAND`/`lParam`, mnemonics and keyboard navigation, bar/popup styles, `SetTheme`, and
  `SetMenuBarItems`.

Automated inventory: a temporary PowerShell check found all 15 public `Create*` APIs represented
in the demo, plus `AttachTooltip` and `ShowContextMenu`; the check passed and was removed.

Tests:

- RED: `rtk rg "CreateMenuBar" demo/main.cpp` exited 1 before the implementation.
- GREEN: `cmake --build build --config Debug --target Win32CustomWidgetsDemo MenuBarTests` passed.
- Focused: `ctest --test-dir build -C Debug -R MenuBarTests --output-on-failure` passed (1/1).
- Full Debug: `ctest --test-dir build -C Debug --output-on-failure` passed (13/13).

Concern: visual UI inspection was intentionally not performed; the user requested automated
build/test verification only.
