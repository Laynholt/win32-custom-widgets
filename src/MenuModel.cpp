#include "MenuModel.h"

#include <algorithm>
#include <cwctype>

namespace wcw::internal {
namespace {

bool Eligible(const MenuItem& item) {
    return item.enabled && !item.separator;
}

int ClampStart(int value, int start, int end, int size) {
    return std::clamp(value, start, (std::max)(start, end - size));
}

RECT PopupRect(int left, int top, SIZE size) {
    return {left, top, left + size.cx, top + size.cy};
}

SIZE FitPopup(SIZE popup, RECT workArea) {
    return {(std::min)((std::max)(0L, popup.cx), workArea.right - workArea.left),
            (std::min)((std::max)(0L, popup.cy), workArea.bottom - workArea.top)};
}

} // namespace

MenuLabel ParseMenuLabel(std::wstring_view value) {
    MenuLabel label;
    for (size_t index = 0; index < value.size(); ++index) {
        if (value[index] != L'&') {
            label.text += value[index];
            continue;
        }
        if (index + 1 == value.size()) {
            if (!label.text.empty()) label.text += L'&';
        } else if (value[index + 1] == L'&') {
            label.text += L'&';
            ++index;
        } else {
            if (!label.mnemonic) label.mnemonic = std::towlower(value[index + 1]);
            else label.text += L'&';
        }
    }
    return label;
}

bool ValidMenuItems(std::span<const MenuItem> items) {
    if (items.empty()) return false;
    for (const auto& item : items) {
        if (item.separator) continue;
        if (item.text.empty()) return false;
        if (!item.children.empty()) {
            if (!ValidMenuItems(item.children)) return false;
        } else if (item.enabled && (item.id < 1 || item.id > 65535)) {
            return false;
        }
    }
    return true;
}

bool ValidMenuBarItems(std::span<const MenuItem> items) {
    if (items.empty() || !ValidMenuItems(items)) return false;
    return std::ranges::all_of(items, [](const MenuItem& item) {
        return !item.separator && !ParseMenuLabel(item.text).text.empty();
    });
}

int NextMenuIndex(std::span<const MenuItem> items, int current, int direction) {
    const int count = static_cast<int>(items.size());
    const int step = direction < 0 ? -1 : 1;
    int index = current;
    for (int checked = 0; checked < count; ++checked) {
        if (index < 0 || index >= count)
            index = step > 0 ? 0 : count - 1;
        else
            index = (index + step + count) % count;
        if (Eligible(items[index])) return index;
    }
    return -1;
}

int EdgeMenuIndex(std::span<const MenuItem> items, bool end) {
    return NextMenuIndex(items, end ? 0 : -1, end ? -1 : 1);
}

RECT PlaceRootMenu(RECT anchor, SIZE popup, RECT workArea) {
    popup = FitPopup(popup, workArea);
    int left = anchor.left;
    int top = anchor.bottom;
    if (top + popup.cy > workArea.bottom) top = anchor.top - popup.cy;
    left = ClampStart(left, workArea.left, workArea.right, popup.cx);
    top = ClampStart(top, workArea.top, workArea.bottom, popup.cy);
    return PopupRect(left, top, popup);
}

RECT PlaceSubmenu(RECT parentRow, SIZE popup, RECT workArea) {
    popup = FitPopup(popup, workArea);
    int left = parentRow.right;
    int top = parentRow.top;
    if (left + popup.cx > workArea.right) left = parentRow.left - popup.cx;
    left = ClampStart(left, workArea.left, workArea.right, popup.cx);
    top = ClampStart(top, workArea.top, workArea.bottom, popup.cy);
    return PopupRect(left, top, popup);
}

} // namespace wcw::internal
