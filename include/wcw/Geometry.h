#pragma once

namespace wcw {

int DipToPx(float dip, unsigned dpi);
float PxToDip(int px, unsigned dpi);
float ClampRadiusDip(float radiusDip, float widthDip, float heightDip);

struct SliderGeometry {
    float thumbPx{};
    float trackStartPx{};
    float trackEndPx{};
    float thumbSizePx{};
    float trackThicknessPx{};

    SliderGeometry(float trackStartPx, float trackEndPx, float minimum, float maximum, float value);
    SliderGeometry(float controlWidthPx, float controlHeightPx, float desiredThumbSizePx,
                   float desiredTrackThicknessPx, float minimum, float maximum, float value);
    float ValueAt(float positionPx, float minimum, float maximum) const;
};

struct ScrollbarGeometry {
    float thumbStartPx;
    float thumbSizePx;

    ScrollbarGeometry(float trackStartPx, float trackEndPx, float contentSize, float viewportSize,
                      float offset, float minimumThumbPx = 28.0f);
};

} // namespace wcw
