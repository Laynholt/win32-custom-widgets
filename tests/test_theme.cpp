#include "Test.h"

#include <wcw/Theme.h>

int main() {
    const auto dark = wcw::DarkTheme();
    CHECK(dark.palette.window == wcw::Color::FromRgb(0x14, 0x14, 0x16));
    CHECK(dark.palette.accent == wcw::Color::FromRgb(0xE8, 0x48, 0x55));
    CHECK(dark.metrics.cornerRadiusDip == 8.0f);

    const auto light = wcw::LightTheme();
    CHECK(light.palette.window == wcw::Color::FromRgb(0xF4, 0xF5, 0xF7));
    CHECK(light.palette.text == wcw::Color::FromRgb(0x18, 0x18, 0x1B));
    CHECK(light.palette.accent == dark.palette.accent);

    return testFailures;
}
