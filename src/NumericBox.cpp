#include <wcw/Controls.h>

#include "Internal.h"

#include <cmath>

namespace wcw {

HWND CreateNumericBox(const NumericBoxOptions& options) {
    if (!std::isfinite(options.minimum) || !std::isfinite(options.maximum) ||
        !std::isfinite(options.step) || !std::isfinite(options.value) || options.step <= 0 ||
        options.minimum > options.maximum ||
        (options.mode != NumericMode::Integer && options.mode != NumericMode::Floating)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return internal::CreateNumericBoxWindow(options);
}

bool SetNumericValue(HWND numericBox, double value) {
    if (!internal::IsTextBoxWindow(numericBox, true)) return false;
    if (!std::isfinite(value)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return SendMessageW(numericBox, internal::NumericSetValueMessage, 0,
                        reinterpret_cast<LPARAM>(&value)) != FALSE;
}

std::optional<double> GetNumericValue(HWND numericBox) {
    std::optional<double> value;
    if (!internal::IsTextBoxWindow(numericBox, true)) return value;
    SendMessageW(numericBox, internal::NumericGetValueMessage, 0,
                 reinterpret_cast<LPARAM>(&value));
    return value;
}

} // namespace wcw
