#include <wcw/Style.h>

namespace wcw {
ResolvedStyle ResolveStyle(const Theme& theme, const StyleOverride& local) {
    return {
        local.background.value_or(theme.palette.input),
        local.foreground.value_or(theme.palette.text),
        local.mutedForeground.value_or(theme.palette.mutedText),
        local.border.value_or(theme.palette.border),
        local.hover.value_or(theme.palette.hover),
        local.pressed.value_or(theme.palette.pressed),
        local.selected.value_or(theme.palette.selected),
        local.disabledSurface.value_or(theme.palette.disabledSurface),
        local.disabledText.value_or(theme.palette.disabledText),
        local.focus.value_or(theme.palette.focus),
        local.accent.value_or(theme.palette.accent),
        local.danger.value_or(theme.palette.danger),
        local.font.value_or(theme.body),
        local.borderWidthDip.value_or(theme.metrics.borderWidthDip),
        local.focusWidthDip.value_or(theme.metrics.focusWidthDip),
        local.paddingXDip.value_or(theme.metrics.paddingXDip),
        local.paddingYDip.value_or(theme.metrics.paddingYDip),
        local.spacingDip.value_or(theme.metrics.spacingDip),
        local.controlHeightDip.value_or(theme.metrics.controlHeightDip),
        local.cornerRadiusDip.value_or(theme.metrics.cornerRadiusDip),
        local.trackThicknessDip.value_or(theme.metrics.trackThicknessDip),
        local.thumbSizeDip.value_or(theme.metrics.thumbSizeDip),
        local.indicatorSizeDip.value_or(theme.metrics.indicatorSizeDip),
    };
}

} // namespace wcw
