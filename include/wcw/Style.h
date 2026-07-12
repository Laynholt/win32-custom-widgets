#pragma once

#include <wcw/Theme.h>

#include <optional>

namespace wcw {

struct StyleOverride {
    std::optional<Color> background, foreground, mutedForeground, border, hover, pressed, selected,
        disabledSurface, disabledText, focus, accent, danger;
    std::optional<FontSpec> font;
    std::optional<float> borderWidthDip, focusWidthDip, paddingXDip, paddingYDip, spacingDip,
        controlHeightDip, cornerRadiusDip, trackThicknessDip, thumbSizeDip, indicatorSizeDip;
};

struct ResolvedStyle {
    Color background, text, mutedText, border, hover, pressed, selected, disabledSurface,
        disabledText, focus, accent, danger;
    FontSpec font;
    float borderWidthDip, focusWidthDip, paddingXDip, paddingYDip, spacingDip, controlHeightDip,
        cornerRadiusDip, trackThicknessDip, thumbSizeDip, indicatorSizeDip;
};

ResolvedStyle ResolveStyle(const Theme& theme, const StyleOverride& local = {});

} // namespace wcw
