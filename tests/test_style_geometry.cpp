#include "Test.h"

#include <wcw/Geometry.h>
#include <wcw/Style.h>

int main() {
    wcw::StyleOverride local;
    local.background = wcw::Color::FromRgb(1, 2, 3);
    local.cornerRadiusDip = 0.0f;
    local.trackThicknessDip = 6.0f;
    local.thumbSizeDip = 22.0f;
    const auto dark = wcw::DarkTheme();
    const auto resolved = wcw::ResolveStyle(dark, local);
    CHECK(resolved.background == *local.background);
    CHECK(resolved.text == dark.palette.text);
    CHECK(resolved.cornerRadiusDip == 0.0f);
    CHECK(resolved.trackThicknessDip == 6.0f);
    CHECK(resolved.thumbSizeDip == 22.0f);

    CHECK(wcw::DipToPx(10.0f, 144) == 15);
    CHECK(wcw::PxToDip(15, 144) == 10.0f);
    CHECK(wcw::ClampRadiusDip(-1.0f, 40.0f, 20.0f) == 0.0f);
    CHECK(wcw::ClampRadiusDip(50.0f, 40.0f, 20.0f) == 10.0f);

    CHECK(wcw::SliderGeometry(10.0f, 110.0f, 0.0f, 100.0f, 0.0f).thumbPx == 10.0f);
    CHECK(wcw::SliderGeometry(10.0f, 110.0f, 0.0f, 100.0f, 100.0f).thumbPx == 110.0f);
    CHECK(wcw::SliderGeometry(10.0f, 110.0f, 5.0f, 5.0f, 5.0f).thumbPx == 10.0f);
    const wcw::SliderGeometry lowSlider(200.0f, 2.0f, 18.0f, 4.0f,
                                        0.0f, 100.0f, 50.0f);
    CHECK(lowSlider.thumbSizePx == 2.0f);
    CHECK(lowSlider.trackThicknessPx == 2.0f);
    CHECK(lowSlider.trackStartPx == 1.0f);
    CHECK(lowSlider.trackEndPx == 199.0f);
    CHECK(lowSlider.ValueAt(100.0f, 0.0f, 100.0f) == 50.0f);

    const auto scrollbar = wcw::ScrollbarGeometry(0.0f, 100.0f, 1000.0f, 10.0f, 500.0f);
    CHECK(scrollbar.thumbSizePx == 28.0f);
    CHECK(scrollbar.thumbStartPx >= 0.0f);
    CHECK(scrollbar.thumbStartPx + scrollbar.thumbSizePx <= 100.0f);

    return testFailures;
}
