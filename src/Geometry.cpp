#include <wcw/Geometry.h>

#include <algorithm>
#include <cmath>

namespace wcw {

int DipToPx(float dip, unsigned dpi) {
    return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / 96.0f));
}

float PxToDip(int px, unsigned dpi) {
    return dpi ? static_cast<float>(px) * 96.0f / static_cast<float>(dpi) : 0.0f;
}

float ClampRadiusDip(float radiusDip, float widthDip, float heightDip) {
    return std::clamp(radiusDip, 0.0f, std::max(0.0f, std::min(widthDip, heightDip) / 2.0f));
}

SliderGeometry::SliderGeometry(float trackStartPx, float trackEndPx, float minimum, float maximum,
                               float value)
    : thumbPx(trackStartPx) {
    if (minimum != maximum) {
        const auto normalized = (std::clamp(value, minimum, maximum) - minimum) / (maximum - minimum);
        thumbPx += normalized * (trackEndPx - trackStartPx);
    }
}

ScrollbarGeometry::ScrollbarGeometry(float trackStartPx, float trackEndPx, float contentSize,
                                     float viewportSize, float offset, float minimumThumbPx)
    : thumbStartPx(trackStartPx), thumbSizePx(std::max(0.0f, trackEndPx - trackStartPx)) {
    const auto trackSize = thumbSizePx;
    if (trackSize == 0.0f || contentSize <= 0.0f || viewportSize >= contentSize) {
        return;
    }

    thumbSizePx = std::clamp(trackSize * std::max(0.0f, viewportSize) / contentSize,
                             std::clamp(minimumThumbPx, 0.0f, trackSize), trackSize);
    const auto scrollRange = contentSize - std::max(0.0f, viewportSize);
    thumbStartPx += std::clamp(offset, 0.0f, scrollRange) / scrollRange * (trackSize - thumbSizePx);
}

} // namespace wcw
