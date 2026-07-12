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
    : thumbPx(trackStartPx), trackStartPx(trackStartPx), trackEndPx(trackEndPx) {
    if (minimum != maximum) {
        const auto normalized = (std::clamp(value, minimum, maximum) - minimum) / (maximum - minimum);
        thumbPx += normalized * (trackEndPx - trackStartPx);
    }
}

SliderGeometry::SliderGeometry(float controlWidthPx, float controlHeightPx,
                               float desiredThumbSizePx, float desiredTrackThicknessPx,
                               float minimum, float maximum, float value) {
    const auto width = std::max(0.0f, controlWidthPx);
    const auto height = std::max(0.0f, controlHeightPx);
    thumbSizePx = std::clamp(desiredThumbSizePx, 0.0f, std::min(width, height));
    trackThicknessPx = std::clamp(desiredTrackThicknessPx, 0.0f, height);
    trackStartPx = thumbSizePx / 2.0f;
    trackEndPx = std::max(trackStartPx, width - thumbSizePx / 2.0f);
    thumbPx = SliderGeometry(trackStartPx, trackEndPx, minimum, maximum, value).thumbPx;
}

float SliderGeometry::ValueAt(float positionPx, float minimum, float maximum) const {
    if (trackStartPx == trackEndPx) return minimum;
    const auto fraction = std::clamp((positionPx - trackStartPx) /
                                     (trackEndPx - trackStartPx), 0.0f, 1.0f);
    return minimum + fraction * (maximum - minimum);
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
