# Implementation status

Plan audit: 2026-10-08. This file replaces historical Superpowers plans, briefs, and task reports.
The public API and usage contract are documented in [README](../README.md).

## Implemented work

| Former plan or specification | Current implementation and evidence |
| --- | --- |
| Initial widget library | Public headers in `include/wcw`, implementations in `src`, CMake library target, gallery, and standalone `tests/consumer` project. Runtime, themes, style overrides, geometry, text/numeric input, boolean/value controls, combo box, scrolling, tooltips, display controls, and MSAA have dedicated CTest targets. |
| Paint buffer lifetime | Buffered painters destroy their back buffer before `EndPaint`; `ButtonDisplayTests` covers rendered pixels. |
| Rounded controls and edit alignment | Runtime window regions, inset borders, and font-metric-based edit layout; `ButtonDisplayTests` and `TextNumericTests` cover regions, borders, and text alignment. |
| Fluent popup menus and icons | Padded/centered labels, built-in vector icons, context menus, nested submenus, menu buttons, keyboard navigation, high-contrast menu colors, and menu accessibility; covered by display, menu, and accessibility tests. |
| Custom menu bar | Public creation/replacement APIs, mouse and keyboard menu mode, popup switching, accessibility, themed rendering, and gallery examples; covered by `MenuBarTests`, `MenuTests`, and `AccessibilityTests`. |
| Subsequent tab control work | Public tab APIs, custom painting over the native tab control, persistent gallery pages, and consumer coverage; `TabControlTests` covers selection, keyboard input, page state, painting, accessibility, validation, and destruction. |

## Differences from the initial design

The implementation tasks above are present, but the earliest design promised a few conveniences
that the current public API does not provide. These remain open design items, not completed work:

- Label has fixed left alignment with wrapping and ellipsis; there is no label alignment option.
- Button has no separate checked-state API; boolean controls and menu items provide checked state.
- `ControlOptions::accessibleName` sets an accessible name at creation; there is no public setter
  for replacing that explicit name afterwards.
- Notifications have public payload structures and codes, but no typed validation/decoding helpers.

The later label implementation also relaxes vertical padding when the measured text does not fit
inside the padded height. This preserves readable text in short labels and differs from the
original instruction to always clip inside the padded rectangle.

## Verification still needed

Fresh automated verification on 2026-10-08 used MSVC 19.51 and the Windows 10.0.26100.0 SDK
in a clean `build-github-check` directory with the `Visual Studio 18 2026` x64 generator:

- Debug: complete build succeeded; all 14 CTest tests passed.
- Release: complete build succeeded; all 14 CTest tests passed.
- Release gallery: a hidden main window was found through the process's UI threads and closed
  normally with `WM_CLOSE`; the process exited with code 0. This checks startup and shutdown,
  not visual appearance.
- The gallery contains all 16 public `Create*` APIs, plus tooltip and context-menu examples.

The full manual gallery matrix has not been freshly verified: Dark/Light themes, radius 0 and
24 DIP, 100%/150% DPI and monitor transitions, keyboard-only traversal, hover/pressed/focus/
disabled/validation states, popup placement, tooltip timing, clipping, and flicker. Older reports
include launch and geometry checks, but some screenshot attempts failed and later menu-bar work
explicitly skipped visual inspection.

Run GUI tests sequentially: existing tests locate windows by class name and can interfere with
one another when multiple suites run concurrently.

## Publication

Keep source, public headers, tests, the demo, CMake files, README, and this status note in Git.
Local graph indexes, Superpowers session artifacts, and generated build files are ignored.
Tracked historical files were removed with Git; remaining local Superpowers session files are
ignored and are not part of the published tree. Historical tracked files remain in Git history.
No license has been selected, and no Git remote is configured in this checkout. Choose a license
before inviting reuse; configure the intended GitHub repository before pushing.
