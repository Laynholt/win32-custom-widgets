#pragma once

#include <wcw/Controls.h>

#include <span>

namespace wcw::internal {

bool ValidMenuItems(std::span<const MenuItem> items);
int NextMenuIndex(std::span<const MenuItem> items, int current, int direction);
int EdgeMenuIndex(std::span<const MenuItem> items, bool end);
RECT PlaceRootMenu(RECT anchor, SIZE popup, RECT workArea);
RECT PlaceSubmenu(RECT parentRow, SIZE popup, RECT workArea);

} // namespace wcw::internal
