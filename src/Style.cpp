#include <wcw/Style.h>

namespace wcw {
namespace {

template <class T>
T ValueOr(const std::optional<T>& value, const T& fallback) {
    return value.value_or(fallback);
}

} // namespace

ResolvedStyle ResolveStyle(const Theme& theme, const StyleOverride& local) {
    return {
        ValueOr(local.background, theme.palette.input),
        ValueOr(local.foreground, theme.palette.text),
        ValueOr(local.mutedForeground, theme.palette.mutedText),
        ValueOr(local.border, theme.palette.border),
        ValueOr(local.hover, theme.palette.hover),
        ValueOr(local.pressed, theme.palette.pressed),
        ValueOr(local.selected, theme.palette.selected),
        ValueOr(local.disabledSurface, theme.palette.disabledSurface),
        ValueOr(local.disabledText, theme.palette.disabledText),
        ValueOr(local.focus, theme.palette.focus),
        ValueOr(local.accent, theme.palette.accent),
        ValueOr(local.danger, theme.palette.danger),
        ValueOr(local.font, theme.body),
        ValueOr(local.borderWidthDip, theme.metrics.borderWidthDip),
        ValueOr(local.focusWidthDip, theme.metrics.focusWidthDip),
        ValueOr(local.paddingXDip, theme.metrics.paddingXDip),
        ValueOr(local.paddingYDip, theme.metrics.paddingYDip),
        ValueOr(local.spacingDip, theme.metrics.spacingDip),
        ValueOr(local.controlHeightDip, theme.metrics.controlHeightDip),
        ValueOr(local.cornerRadiusDip, theme.metrics.cornerRadiusDip),
    };
}

} // namespace wcw
