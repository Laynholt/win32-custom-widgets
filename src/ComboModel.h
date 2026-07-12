#pragma once

#include <wcw/Controls.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wcw::internal {

class ComboModel {
public:
    void SetItems(std::vector<ComboItem> items);
    bool SetSelection(int index);
    void Open();
    void Cancel();
    bool Commit(bool preservePrefix = false);
    int Navigate(int direction);
    int Home();
    int End();
    int PrefixSearch(wchar_t character, std::uint64_t nowMilliseconds);

    int Selection() const { return selection_; }
    int Highlighted() const { return highlighted_; }
    bool IsOpen() const { return open_; }
    const std::vector<ComboItem>& Items() const { return items_; }

private:
    std::vector<ComboItem> items_;
    int selection_{-1};
    int highlighted_{-1};
    bool open_{};
    std::wstring prefix_;
    std::uint64_t lastPrefixMilliseconds_{};
};

} // namespace wcw::internal
