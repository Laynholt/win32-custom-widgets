#include "NumericModel.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace wcw::internal {
namespace {

bool IsIntermediate(std::wstring_view text, NumericMode mode) {
    return text.empty() || text == L"+" || text == L"-" ||
           (mode == NumericMode::Floating &&
            (text == L"." || text == L"+." || text == L"-."));
}

std::optional<std::string> NarrowAscii(std::wstring_view text) {
    std::string result;
    result.reserve(text.size());
    for (const auto character : text) {
        if (character > 0x7f) return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

} // namespace

NumericModel::NumericModel(NumericMode mode, double minimum, double maximum, double step,
                           double value)
    : mode_(mode), minimum_((std::min)(minimum, maximum)),
      maximum_((std::max)(minimum, maximum)), step_(std::abs(step)) {
    if (mode_ == NumericMode::Integer) {
        minimum_ = std::ceil(minimum_);
        maximum_ = std::floor(maximum_);
    }
    if (!std::isfinite(step_) || step_ == 0) step_ = 1;
    Commit(value);
}

void NumericModel::Commit(double value) {
    if (!std::isfinite(value)) value = minimum_;
    value = std::clamp(value, minimum_, maximum_);
    if (mode_ == NumericMode::Integer) value = std::trunc(value);
    committed_ = value;
    value_ = value;

    char buffer[64]{};
    std::to_chars_result result;
    if (mode_ == NumericMode::Integer)
        result = std::to_chars(buffer, std::end(buffer), static_cast<long long>(value));
    else
        result = std::to_chars(buffer, std::end(buffer), value, std::chars_format::general);
    text_.clear();
    for (auto current = buffer; current != result.ptr; ++current)
        text_.push_back(static_cast<wchar_t>(*current));
}

NumericTextState NumericModel::SetText(std::wstring_view text) {
    text_ = text;
    value_.reset();
    if (IsIntermediate(text, mode_)) return NumericTextState::Intermediate;
    const auto narrow = NarrowAscii(text);
    if (!narrow || narrow->empty()) return NumericTextState::Invalid;

    double parsed{};
    const auto first = narrow->data() + (narrow->front() == '+' ? 1 : 0);
    const auto last = narrow->data() + narrow->size();
    std::from_chars_result result;
    if (mode_ == NumericMode::Integer) {
        long long integer{};
        result = std::from_chars(first, last, integer, 10);
        parsed = static_cast<double>(integer);
    } else {
        result = std::from_chars(first, last, parsed, std::chars_format::general);
    }
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(parsed))
        return NumericTextState::Invalid;
    Commit(parsed);
    return NumericTextState::Valid;
}

bool NumericModel::SetValue(double value) {
    const auto before = committed_;
    Commit(value);
    return committed_ != before;
}

bool NumericModel::Step(int direction) {
    if (!direction) return false;
    return SetValue(committed_ + step_ * direction);
}

} // namespace wcw::internal
