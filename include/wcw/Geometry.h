#pragma once

namespace wcw {

int DipToPx(float dip, unsigned dpi);
float PxToDip(int px, unsigned dpi);
float ClampRadiusDip(float radiusDip, float widthDip, float heightDip);

struct SliderGeometry {
    float thumbPx;

    SliderGeometry(float trackStartPx, float trackEndPx, float minimum, float maximum, float value);
};

struct ScrollbarGeometry {
    float thumbStartPx;
    float thumbSizePx;

    ScrollbarGeometry(float trackStartPx, float trackEndPx, float contentSize, float viewportSize,
                      float offset, float minimumThumbPx = 28.0f);
};

} // namespace wcw
