# Rounded Controls and Edit Alignment

## Goal

Preserve the current visual language while removing rectangular corner artifacts, making the thin border uniform on all sides, and vertically centering editable text and its selection.

## Rounded window regions

Apply a native rounded window region to library-owned controls using the resolved `cornerRadiusDip`, current client size, and current DPI. Update the region on `WM_SIZE` and `ThemeChangedMessage`, so resizing, theme changes, and `SetStyleOverride` remain correct. A zero radius restores the default rectangular window region.

The region is owned by the OS after a successful `SetWindowRgn`. Delete it only when `SetWindowRgn` fails. This makes the pixels outside the rounded shape belong to the actual parent instead of guessing or repainting the parent's background color.

## Uniform border

Keep the existing one-DIP default border. Inset the GDI+ outline bounds so the right and bottom edges are inside the drawable client area, matching the already visible top and left edges. Fill geometry remains unchanged.

## Editable text alignment

Measure the child `EDIT` control's active font with `GetTextMetricsW`. Size the borderless child to that measured line height and center it vertically inside the outer text box while retaining the existing horizontal padding. Re-run layout after theme/font changes and `WM_SETFONT`, not only after resize.

This moves the native glyphs and their selection highlight together without custom-drawing editable text.

## Regression checks

- A rounded button window region excludes a corner and includes its center.
- A rendered square button has the configured border color at the midpoint of all four edges.
- A text box child `EDIT` uses the active font's measured line height and remains vertically centered.

Run the focused tests through RED and GREEN, then build and run the complete Release suite in `build`.
