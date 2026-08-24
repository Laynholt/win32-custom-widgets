# Custom Menu Bar Design

## Goal

Add a reusable, theme-aware menu bar control to Win32 Custom Widgets. The bar
must use the existing `MenuItem` model and popup renderer, behave like a normal
Windows application menu with mouse and keyboard input, and appear in the demo
alongside every other public widget. The finished work must include a tested
Release build of `Win32CustomWidgetsDemo.exe`.

## Scope

The work adds one public child-window control, extends the existing popup
controller only where root-menu switching requires it, documents the API, and
updates the existing single-window gallery. It does not add `HMENU` conversion,
owner-draw native menus, tabs, a second demo application, or a new menu model.

The current demo already creates every existing public widget and demonstrates
tooltips and context menus. Those examples remain; the new menu bar is the only
missing widget to add.

## Public API

`include/wcw/Controls.h` gains:

```cpp
struct MenuBarOptions : ControlOptions {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
};

HWND CreateMenuBar(const MenuBarOptions& options);
bool SetMenuBarItems(HWND menuBar, const std::vector<MenuItem>& items);
```

`ControlOptions::appearance` styles the bar. `menuAppearance` styles its popup
levels. Both use the existing theme and `StyleOverride` resolution rules.

Each top-level `MenuItem` supplies its visible text and enabled state. An item
with children opens them as a popup; an enabled leaf dispatches its nonzero ID
directly. Top-level separators are invalid. Images, checked state, and shortcut
labels are rendered only inside popup levels.

An ampersand introduces a mnemonic (`L"&File"`); a doubled ampersand renders a
literal ampersand. Matching is case-insensitive and selects the first enabled
match. Commands are sent to the parent with `WM_COMMAND`; `LOWORD(wParam)` is
the item ID and `lParam` is the menu bar `HWND`.

`CreateMenuBar` rejects an invalid parent or item tree and returns `nullptr`
with `GetLastError()` set. `SetMenuBarItems` validates before mutation, closes
an active popup owned by the bar, and leaves the old tree intact on failure.

## Control Architecture

`src/MenuBar.cpp` owns one registered child-window class and a small per-window
state object: copied menu items, measured top-level rectangles, selected and
hot indices, popup-open state, saved focus, and popup appearance. It paints
with the existing buffered GDI+ helpers and a menu-color resolver shared with
the popup renderer. The bar defaults to the theme panel surface, supports local
overrides, uses DPI-scaled metrics, and falls back to system menu colors in
High Contrast.

The control subclasses its parent while alive so `Alt`, `F10`, and system-key
mnemonics work without requiring consumers to modify their message loop. The
subclass is removed during destruction. Destruction, disabling, parent
deactivation, theme changes, and item replacement cancel any active popup and
restore focus safely when the saved window still exists.

`src/Menu.cpp` remains the only popup implementation. Its internal runner gains
an optional menu-bar owner and a small result containing whether a popup was
shown and which top-level index, if any, requested the next popup. Captured
pointer movement is mapped to the bar for hover switching. At the root popup
level, `Left` and `Right` request the previous or next enabled top-level item;
inside a submenu they retain their current close/open behavior. Existing menu
buttons and context menus use the same wrapper as before and receive no new
public behavior.

No interface, factory, or duplicate popup renderer is introduced.

## Interaction

- Clicking an enabled top-level item selects it and opens its children, or
  dispatches its leaf command.
- Clicking the active item again closes its popup.
- While a popup is open, moving across enabled top-level items switches the
  popup immediately. Disabled items do not become active.
- `Alt` or `F10` activates the bar, reveals mnemonic cues, saves the previous
  focus, and selects the first enabled item without opening it.
- `Alt+mnemonic` selects the matching item and opens or invokes it.
- In bar mode, `Left` and `Right` wrap across enabled top-level items;
  `Down`, `Enter`, or `Space` opens or invokes the selected item.
- In a root popup, `Left` and `Right` switch top-level items. In a child popup,
  `Left` closes one submenu before a later `Left` can switch the root.
- `Escape`, another `Alt`, clicking outside, or deactivating the parent closes
  menu mode and restores the saved focus.
- Mouse hover, focus, open, disabled, theme, DPI, and keyboard-cue changes
  invalidate only the menu bar as needed.

## Accessibility

The accessibility provider exposes the control as a menu bar and its
top-level entries as menu items. It reports names without mnemonic markers,
enabled/disabled state, focus, selection, and expanded/collapsed state.
Keyboard focus and expanded-state transitions emit the same WinEvent-style
notifications already used by the library. Popup accessibility remains owned
by the existing popup controller.

## Demo And Documentation

The existing responsive gallery remains one window. A menu bar is fixed at the
top and the current gallery layout is shifted below it. The bar contains
`File`, `Edit`, `View`, and `Help` examples covering normal commands, nested
children, shortcuts, icons, checked items, and disabled items. Representative
commands update the existing status label; `View` also demonstrates switching
the light and dark themes.

The existing right-click context menu, menu button, tooltips, scroll view, and
all other widget examples remain usable. `README.md` gains a concise menu-bar
creation example suitable for another application linking the library.

## Verification

Automated coverage includes:

- top-level validation, mnemonic parsing, literal ampersands, hit testing, and
  enabled-item wrapping;
- mouse opening, repeat-click dismissal, hover switching, command dispatch,
  and safe item replacement;
- `Alt/F10`, mnemonics, arrow navigation, popup switching, `Enter/Down`, and
  `Escape` focus restoration;
- theme, DPI, destruction, and accessibility states/events;
- regression coverage for menu buttons and context menus after the internal
  popup runner changes.

Completion requires fresh Debug and Release configurations, a full successful
CTest run, and a smoke launch of the Release demo. The final handoff reports the
absolute path to the generated `Win32CustomWidgetsDemo.exe`.
