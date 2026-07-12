#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ScrollModel.h"

#include <algorithm>

namespace wcw::internal {

bool ScrollModel::SetExtent(ScrollExtentDip extent) {
    extent_ = {std::max(0.0f, extent.width), std::max(0.0f, extent.height)};
    return ClampOffset();
}

bool ScrollModel::SetViewport(ScrollExtentDip viewport) {
    viewport_ = {std::max(0.0f, viewport.width), std::max(0.0f, viewport.height)};
    return ClampOffset();
}

bool ScrollModel::SetOffset(ScrollOffsetDip offset) {
    const auto maximum = Maximum();
    const ScrollOffsetDip normalized{std::clamp(offset.x, 0.0f, maximum.x),
                                     std::clamp(offset.y, 0.0f, maximum.y)};
    if (normalized == offset_) return false;
    offset_ = normalized;
    return true;
}

ScrollOffsetDip ScrollModel::Maximum() const {
    return {std::max(0.0f, extent_.width - viewport_.width),
            std::max(0.0f, extent_.height - viewport_.height)};
}

int ScrollModel::ConsumeWheelDelta(int delta, bool horizontal) {
    auto& remainder = horizontal ? horizontalWheelRemainder_ : verticalWheelRemainder_;
    remainder += delta;
    const int notches = remainder / WHEEL_DELTA;
    remainder %= WHEEL_DELTA;
    return notches;
}

bool ScrollModel::ClampOffset() { return SetOffset(offset_); }

} // namespace wcw::internal
