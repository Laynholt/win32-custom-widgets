#include "ComboModel.h"

#include <algorithm>
#include <cwctype>

namespace wcw::internal {
namespace {

bool StartsWith(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), text.begin(),
                      [](wchar_t left, wchar_t right) {
                          return std::towlower(left) == std::towlower(right);
                      });
}

} // namespace

void ComboModel::SetItems(std::vector<ComboItem> items) {
    const auto selectedId = selection_ >= 0 ? std::optional(items_[selection_].id) : std::nullopt;
    items_ = std::move(items);
    selection_ = -1;
    if (selectedId) {
        const auto found = std::find_if(items_.begin(), items_.end(),
                                        [&](const ComboItem& item) { return item.id == *selectedId; });
        if (found != items_.end()) selection_ = static_cast<int>(found - items_.begin());
    }
    highlighted_ = selection_;
    prefix_.clear();
}

bool ComboModel::SetSelection(int index) {
    if (index < -1 || index >= static_cast<int>(items_.size())) return false;
    if (selection_ == index) return false;
    selection_ = highlighted_ = index;
    return true;
}

void ComboModel::Open() {
    open_ = true;
    highlighted_ = selection_ >= 0 ? selection_ : (items_.empty() ? -1 : 0);
    prefix_.clear();
}

void ComboModel::Cancel() {
    highlighted_ = selection_;
    open_ = false;
    prefix_.clear();
}

bool ComboModel::Commit() {
    const bool changed = selection_ != highlighted_;
    selection_ = highlighted_;
    open_ = false;
    prefix_.clear();
    return changed;
}

int ComboModel::Navigate(int direction) {
    if (items_.empty()) return highlighted_ = -1;
    const int start = highlighted_ < 0 ? (direction < 0 ? static_cast<int>(items_.size()) - 1 : 0)
                                       : highlighted_ + direction;
    return highlighted_ = std::clamp(start, 0, static_cast<int>(items_.size()) - 1);
}

int ComboModel::Home() {
    return highlighted_ = items_.empty() ? -1 : 0;
}

int ComboModel::End() {
    return highlighted_ = items_.empty() ? -1 : static_cast<int>(items_.size()) - 1;
}

int ComboModel::PrefixSearch(wchar_t character, std::uint64_t nowMilliseconds) {
    if (items_.empty() || !std::iswprint(character)) return highlighted_;
    const auto lower = static_cast<wchar_t>(std::towlower(character));
    const bool expired = nowMilliseconds < lastPrefixMilliseconds_ ||
                         nowMilliseconds - lastPrefixMilliseconds_ > 1000;
    lastPrefixMilliseconds_ = nowMilliseconds;

    if (expired) prefix_.clear();
    const bool cycle = prefix_.size() == 1 && prefix_.front() == lower;
    if (!cycle) prefix_.push_back(lower);

    const int count = static_cast<int>(items_.size());
    const int begin = cycle && highlighted_ >= 0 ? (highlighted_ + 1) % count : 0;
    for (int offset = 0; offset < count; ++offset) {
        const int index = (begin + offset) % count;
        if (StartsWith(items_[index].text, prefix_)) return highlighted_ = index;
    }
    if (prefix_.size() > 1) {
        prefix_.assign(1, lower);
        for (int index = 0; index < count; ++index)
            if (StartsWith(items_[index].text, prefix_)) return highlighted_ = index;
    }
    return highlighted_;
}

} // namespace wcw::internal
