#pragma once

#include <cstdint>
#include <string>

namespace wcw {

struct Color {
    std::uint8_t r{}, g{}, b{}, a{255};

    static constexpr Color FromRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return {r, g, b, 255};
    }

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

struct FontSpec {
    std::wstring family{L"Segoe UI"};
    float sizeDip{14};
    int weight{400};
    bool italic{};
};

struct Palette {
    Color window, panel, input, text, mutedText, border, hover, pressed, selected,
          disabledSurface, disabledText, focus, accent, accentHover, accentPressed, danger, success;
};

struct Metrics {
    float borderWidthDip{1}, focusWidthDip{2}, paddingXDip{12}, paddingYDip{8}, spacingDip{8},
          controlHeightDip{36}, cornerRadiusDip{8}, trackThicknessDip{4}, thumbSizeDip{18},
          indicatorSizeDip{20};
};

struct Theme {
    Palette palette;
    Metrics metrics;
    FontSpec body;
    FontSpec label;
};

Theme DarkTheme();
Theme LightTheme();

} // namespace wcw
