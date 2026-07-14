# Fluent Popup Menus and Built-in Icons

## Goal

Make labels inside scroll views align like the rest of the library, and add DPI-aware custom context menus, menu buttons, nested submenus, and modern information, warning, and error icons. The selected visual direction is Fluent Outline: restrained surfaces, thin borders, rounded corners, clear hover and focus states, and anti-aliased outline icons.

## Label layout

Apply the resolved `paddingXDip` and `paddingYDip` to every label, not only labels hosted by a scroll view. Measure wrapped text inside the padded width, then vertically center the measured text block inside the padded height. Clip and ellipsize text that does not fit. This replaces the ineffective combination of `DT_VCENTER` and multiline `DT_WORDBREAK` while preserving wrapping.

The change intentionally uses the existing style metrics. It introduces no scroll-view-specific label mode or new public option.

## Public API

Add these declarations alongside the existing control APIs:

```cpp
enum class BuiltinIcon { Information, Warning, Error };

struct MenuItem {
    int id{};
    std::wstring text;
    std::wstring shortcut;
    ImageSource image;
    std::vector<MenuItem> children;
    bool enabled{true};
    bool checked{};
    bool separator{};
};

struct ContextMenuOptions {
    std::vector<MenuItem> items;
    StyleOverride appearance;
};

struct MenuButtonOptions : ButtonOptions {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
};

bool ShowContextMenu(HWND owner, POINT screenPosition,
                     const ContextMenuOptions& options);
HWND CreateMenuButton(const MenuButtonOptions& options);
bool SetMenuItems(HWND menuButton, const std::vector<MenuItem>& items);
```

Extend `ImageSource` with a built-in-image kind and `ImageSource(BuiltinIcon)`. Existing `HICON` and `HBITMAP` construction and rendering remain source-compatible.

A separator ignores its other fields. An item with children opens a submenu and does not dispatch its own ID. An enabled leaf item must use an ID from 1 through 65535 because selection is reported through `WM_COMMAND`.

## Command dispatch and lifetime

Both entry points copy their menu item trees, so callers may pass temporary option objects. Selecting a leaf first closes the complete popup hierarchy, then synchronously sends:

```cpp
WM_COMMAND, MAKEWPARAM(item.id, 0), source
```

For a context menu, `owner` receives the message and `source` is zero. For a menu button, its parent receives the message and `source` is the menu button window. Merely opening the menu button does not send `BN_CLICKED` for the button's own control ID.

`ShowContextMenu` returns `false` and sets `ERROR_INVALID_WINDOW_HANDLE` for an invalid owner, or `ERROR_INVALID_PARAMETER` for an invalid menu tree. Cancellation after a menu was successfully shown still returns `true`. `SetMenuItems` validates the target window and the replacement tree, then affects the next opening; if that button currently owns an open menu, the current hierarchy closes first.

## Popup architecture

Implement each visible menu level as a library-owned `WS_POPUP` window. A small internal popup controller owns the copied tree, the open window chain, current selection, and final command result. Each popup window only paints one level and forwards pointer and keyboard events to that controller.

`ShowContextMenu` runs a nested message loop until selection or dismissal, matching native context-menu call semantics. A menu button invokes the same controller and anchors the root popup below its client bounds. Context menus use the supplied screen coordinate. Root menus and submenus remain inside the nearest monitor work area by shifting or flipping horizontally and vertically as needed. Every level uses the DPI of the monitor where it is displayed.

The popup chain closes when the user selects a leaf, presses Escape, clicks outside the hierarchy, the owner is destroyed or disabled, or application activation is lost. Focus returns to the previously focused window when it is still valid.

## Interaction

The menu button behaves like the existing button but reserves room for a custom down chevron. Mouse click, Space, Enter, or Alt+Down opens its menu. Down also opens the menu when the button has focus.

Within a popup:

- Up and Down move across enabled, non-separator items and wrap at the ends.
- Home and End select the first and last eligible items.
- Enter and Space invoke a leaf or open a submenu.
- Right opens the selected submenu; Left closes a submenu and returns to its parent.
- Escape closes the entire hierarchy.
- Hover selects an item; hovering an item with children opens its submenu after a short delay and replaces any sibling submenu.
- Clicking a checked item still dispatches its ID; the application remains responsible for changing its model and refreshing menu items.

Disabled items and separators never receive keyboard selection or dispatch commands. Type-ahead search, mnemonic parsing, radio groups, arbitrary owner-draw content, and runtime mutation of an already visible popup are outside this feature.

## Fluent Outline rendering

Use the resolved theme and menu `StyleOverride` for all dimensions and colors. The root and submenu surfaces use the panel background, a one-DIP outline, the configured corner radius, and the platform popup shadow. Rows use compact Fluent spacing, with separate aligned columns for check/icon, label, shortcut, and submenu chevron. Text is vertically centered, shortcuts use the muted foreground, and disabled content uses the disabled foreground. Hover and keyboard selection share the resolved hover surface; no persistent selected surface is needed.

Menu width is derived from its content, including all four columns, and constrained to the monitor work area. Item height and all padding scale from existing style metrics. Separators use the border color and horizontal insets. Painting is double-buffered and anti-aliased.

Draw the down chevron, submenu chevron, check mark, and three built-in icons as DPI-scaled vector paths. Information is an outlined circle with a clear `i`, Warning is a rounded triangle with an exclamation mark, and Error is an outlined circle with a cross. Built-in icons are monochrome and use the resolved foreground supplied by their host; this lets themes and per-control overrides choose semantic colors without adding fixed colors to `Palette`. Native icons and bitmaps retain their original colors.

## Accessibility and system behavior

The menu button reuses the existing button focus, dialog navigation, enabled state, accessible name, and activation conventions. Extend the existing accessibility adapter so a popup reports `ROLE_SYSTEM_MENUPOPUP` and each row is a virtual `ROLE_SYSTEM_MENUITEM` child. Each virtual child exposes its text and the applicable focused, unavailable, checked, and has-popup states. Separators are not exposed as selectable children. Keyboard operation does not depend on pointer hover.

Respect system high-contrast colors when high contrast is active. Submenus use a 200 ms hover delay; keyboard and click opening is immediate. Popup transitions are not animated. No translucent acrylic or composition-only effect is required, keeping the implementation functional on supported Windows versions without a DirectComposition dependency.

## Verification and gallery

Add focused tests for:

- padded single-line and wrapped label layout, including vertical centering;
- menu-tree validation and copied lifetime;
- leaf selection command ID and source handle for both entry points;
- disabled items, separators, checked items, cancellation, and nested submenu traversal;
- keyboard navigation and dismissal;
- monitor-bound popup placement at multiple DPIs;
- `ImageSource(BuiltinIcon)` sizing and non-empty anti-aliased rendering;
- menu button chevron layout without overlapping text or an optional image.

Update the gallery with a Fluent menu button, a right-click context-menu region, at least one nested submenu, disabled/checked/separator examples, shortcut text, and information/warning/error icons in semantic foreground colors. Run the complete Debug and Release build and test suites after the focused tests pass.
