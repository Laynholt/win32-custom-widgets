#pragma once

#include <wcw/Controls.h>

namespace wcw::internal {

class ScrollModel {
public:
    bool SetExtent(ScrollExtentDip extent);
    bool SetViewport(ScrollExtentDip viewport);
    bool SetOffset(ScrollOffsetDip offset);

    ScrollExtentDip Extent() const { return extent_; }
    ScrollExtentDip Viewport() const { return viewport_; }
    ScrollOffsetDip Offset() const { return offset_; }
    ScrollOffsetDip Maximum() const;

    int ConsumeWheelDelta(int delta, bool horizontal);

private:
    bool ClampOffset();

    ScrollExtentDip extent_{};
    ScrollExtentDip viewport_{};
    ScrollOffsetDip offset_{};
    int verticalWheelRemainder_{};
    int horizontalWheelRemainder_{};
};

} // namespace wcw::internal
