# Paint buffer lifetime fix

## Goal

Restore the existing widget appearance without redesigning it. Every custom control must present its completed back buffer while the `BeginPaint` HDC is still valid.

## Root cause

Each buffered `WM_PAINT` handler creates `paint::Buffer`, calls `EndPaint`, and only then destroys the buffer. Its destructor therefore calls `BitBlt` with an HDC whose paint cycle has already ended. The custom controls remain white or incomplete, while native child controls such as the numeric edit can still draw themselves.

## Design

Keep the existing `paint::Buffer` API. In every buffered paint handler, place the buffer and all drawing operations in an inner scope, then call `EndPaint` after that scope. This guarantees the destructor presents the buffer before the paint HDC becomes invalid.

Do not add a new paint-session abstraction or an explicit `Present` method. The scope ordering is visible at each native `BeginPaint`/`EndPaint` pair and cannot introduce a forgotten flush path.

## Coverage

Apply the ordering fix to button, boolean control, display, combo box and popup, progress bar, scroll view, slider, text box, and tooltip painting. The unbuffered scroll-content painter is already ordered correctly and stays unchanged.

## Regression check

Extend the existing button display test with one rendered-pixel assertion. With a zero-radius button, a pixel inside the surface must match the dark theme input color rather than the white, unpresented window surface. Run this test first to observe failure, then rerun it after the fix.

Finally run the complete test suite and build the demo in both Debug and Release configurations.
