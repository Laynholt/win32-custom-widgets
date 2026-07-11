#pragma once

#include <wcw/Controls.h>

#include <optional>
#include <string>
#include <string_view>

namespace wcw::internal {

enum class NumericTextState { Intermediate, Invalid, Valid };

class NumericModel {
public:
    NumericModel(NumericMode mode, double minimum, double maximum, double step, double value);

    NumericTextState SetText(std::wstring_view text);
    bool SetValue(double value);
    bool Step(int direction);
    const std::wstring& Text() const { return text_; }
    std::optional<double> Value() const { return value_; }
    double CommittedValue() const { return committed_; }

private:
    void Commit(double value);

    NumericMode mode_;
    double minimum_;
    double maximum_;
    double step_;
    double committed_{};
    std::optional<double> value_;
    std::wstring text_;
};

} // namespace wcw::internal
