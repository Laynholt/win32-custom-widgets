# Implementation and verification status

Updated: 2026-10-08. This file replaces historical Superpowers plans, briefs, and task reports.
The public API and usage contract are documented in [README](../README.md). The library is
licensed under [MIT](../LICENSE).

## Implemented work

| Former plan or specification | Current implementation and evidence |
| --- | --- |
| Initial widget library | Public headers in `include/wcw`, implementations in `src`, CMake library target, gallery, and standalone `tests/consumer` project. Runtime, themes, style overrides, geometry, text/numeric input, boolean/value controls, combo box, scrolling, tooltips, display controls, and MSAA have dedicated CTest targets. |
| Paint buffer lifetime | Buffered painters destroy their back buffer before `EndPaint`; `ButtonDisplayTests` covers rendered pixels. |
| Rounded controls and edit alignment | Runtime window regions, inset borders, and font-metric-based edit layout; `ButtonDisplayTests` and `TextNumericTests` cover regions, borders, and text alignment. |
| Fluent popup menus and icons | Padded/centered labels, built-in vector icons, context menus, nested submenus, menu buttons, keyboard navigation, high-contrast menu colors, and menu accessibility; covered by display, menu, and accessibility tests. |
| Custom menu bar | Public creation/replacement APIs, mouse and keyboard menu mode, popup switching, accessibility, themed rendering, and gallery examples; covered by `MenuBarTests`, `MenuTests`, and `AccessibilityTests`. |
| Subsequent tab control work | Public tab APIs, custom painting over the native tab control, persistent gallery pages, and consumer coverage; `TabControlTests` covers selection, keyboard input, page state, painting, accessibility, validation, and destruction. |

## Completed design items

| Previously open requirement | Implementation and evidence |
| --- | --- |
| Label alignment | `CreateLabel(options, DT_LEFT / DT_CENTER / DT_RIGHT)` preserves the original one-argument API. `ControlApiTests` checks rendered positions and invalid alignment flags. |
| Strict label padding | Label text stays inside the padded rectangle, including short multiline controls and an empty padded area. The gallery sets smaller vertical padding on its short captions. Pixel assertions cover both cases. |
| Checked button state | `ButtonOptions::checked`, `SetChecked` / `GetChecked`, native check messages, selected styling, and the MSAA checked state are supported. A click keeps the application-controlled check state. Tests cover rendering, notifications, and accessibility. |
| Accessible name updates | `SetAccessibleName` updates existing and lazily created accessibility providers and native tabs. Tests cover name-change events, clearing names, invalid handles, and calls from the wrong thread. |
| Typed notification helpers | Value, check, and selection decoders validate a live `WM_NOTIFY` payload's source and code. Tests cover real notifications, null arguments, mismatched sources, and codes. |

The standalone consumer also compiles and uses the new APIs. README covers the control catalog,
runtime ownership, themes and styles, layout, menus, tabs, notifications, and accessibility.

## Automated verification

Fresh automated verification on 2026-10-08 used MSVC 19.51 and the Windows 10.0.26100.0 SDK
in a clean `build-github-check` directory with the `Visual Studio 18 2026` x64 generator:

- Debug: complete build succeeded; all 15 CTest tests passed.
- Release: complete build succeeded; all 15 CTest tests passed.
- The new API test initially failed to compile against the missing interfaces, then exposed the
  previous short-label padding behavior before that behavior was corrected.
- The gallery contains all 16 public `Create*` APIs, plus tooltip and context-menu examples.
- Popup mnemonic rendering is checked against native `DrawTextW`: single `&` marks an
  underlined character, `&&` displays a literal ampersand, and disabled rows behave consistently.
  A regression test also checks that mnemonic markers do not increase popup width.

Run GUI tests sequentially with the gallery closed: existing tests locate windows by class name
and can interfere across processes. An initial Release run found two tooltip assertion failures
while the Debug gallery was open. After closing that gallery, both the isolated tooltip test and
the full Release suite passed; no production change was needed for that interference.

## Visual verification and remaining checks

User-provided dark- and light-theme screenshots of the Widgets and Settings tabs were inspected.
Selected buttons and tabs, captions, and icon spacing show no obvious clipping at the supplied
window size. The dark Widgets screenshot is included in README. Light-theme screenshots also
show an open menu and a submenu placed to its left near the screen edge. A dark-menu screenshot
exposed literal mnemonic markers (`&Undo`); popup label painting and width measurement were fixed
and covered by the regression test above.

Computer Use obtained the gallery accessibility tree, but its screenshot request timed out.
The user then stopped Computer Use with Escape; no further UI automation was performed.

The full manual gallery matrix remains unverified: radius 0 and 24 DIP, physical
100%/150% DPI and monitor transitions, keyboard-only traversal, hover/pressed/focus states,
popup interaction and placement, tooltip timing, and flicker. Automated tests cover many of
these behaviors, but the supplied screenshots do not establish the complete visual matrix.

## Publication

Keep source, public headers, tests, the demo, CMake files, README, the gallery screenshot,
MIT license, and this status note in Git.
Local graph indexes, Superpowers session artifacts, and generated build files are ignored.
Tracked historical files were removed with Git; remaining local Superpowers session files are
ignored and are not part of the published tree. Historical tracked files remain in Git history.
No Git remote is configured in this checkout. Configure the intended GitHub repository before
pushing; no upload has been performed.

The local `Build` directory contains the latest Release library and gallery, built with tests
disabled. After the passing Debug and Release runs, old builds and their generated test results
were moved outside the repository to the sibling `win32-custom-widgets-build-backup-20261008`
directory because automatic approval review blocked recursive deletion. They can be deleted
manually. Test sources remain in the repository.
