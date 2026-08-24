#pragma once

#include <wcw/Controls.h>

#include <span>
#include <string_view>

namespace wcw::internal {

struct MenuLabel {
    std::wstring text;
    wchar_t mnemonic{};
};

MenuLabel ParseMenuLabel(std::wstring_view value);
bool ValidMenuItems(std::span<const MenuItem> items);
bool ValidMenuBarItems(std::span<const MenuItem> items);
int NextMenuIndex(std::span<const MenuItem> items, int current, int direction);
int EdgeMenuIndex(std::span<const MenuItem> items, bool end);
RECT PlaceRootMenu(RECT anchor, SIZE popup, RECT workArea);
RECT PlaceSubmenu(RECT parentRow, SIZE popup, RECT workArea);

} // namespace wcw::internal
