#pragma once

#include <wcw/Control.h>
#include <wcw/Style.h>

#include <windows.h>

#include <string>
#include <optional>

namespace wcw {

struct ControlOptions {
    HWND parent{};
    int id{};
    RectDip bounds{};
    DWORD style{};
    std::wstring text;
    std::wstring accessibleName;
    StyleOverride appearance;
};

struct ButtonOptions : ControlOptions {
    HICON icon{};
    HBITMAP bitmap{};
    float iconSizeDip{16};
    UINT alignment{DT_CENTER};
    // Registers this control ID with its dialog parent so Enter invokes it.
    bool isDefault{};
    // Uses the conventional IDCANCEL control ID so a dialog parent routes Escape to it.
    bool isCancel{};
};

struct TextBoxOptions : ControlOptions {
    std::wstring placeholder;
    bool readOnly{};
    bool password{};
};

enum class NumericMode { Integer, Floating };

struct NumericBoxOptions : TextBoxOptions {
    NumericMode mode{NumericMode::Integer};
    double minimum{};
    double maximum{100};
    double step{1};
    double value{};
};

struct CheckableOptions : ControlOptions {
    bool checked{};
};

struct SliderOptions : ControlOptions {
    double minimum{};
    double maximum{100};
    double step{1};
    double value{};
    StyleOverride trackAppearance;
    StyleOverride thumbAppearance;
};

struct ProgressBarOptions : ControlOptions {
    double minimum{};
    double maximum{100};
    double value{};
    bool indeterminate{};
};

inline constexpr UINT WCN_VALUE_CHANGED = 0x5701;
inline constexpr UINT WCN_CHECK_CHANGED = 0x5702;

struct ValueChangedNotification {
    NMHDR header;
    double value;
};

struct CheckChangedNotification {
    NMHDR header;
    bool checked;
};

struct ImageSource {
    enum class Kind { None, Icon, Bitmap };

    Kind kind{Kind::None};
    HANDLE handle{};

    ImageSource() = default;
    ImageSource(HICON icon) : kind(Kind::Icon), handle(icon) {}
    ImageSource(HBITMAP bitmap) : kind(Kind::Bitmap), handle(bitmap) {}
};

enum class ImageMode { Contain, Cover, Stretch };

HWND CreateButton(const ButtonOptions& options);
HWND CreateIconButton(const ButtonOptions& options);
HWND CreateLabel(const ControlOptions& options);
HWND CreateImageView(const ControlOptions& options, ImageSource source,
                     ImageMode mode = ImageMode::Contain);
HWND CreateSeparator(const ControlOptions& options, bool vertical = false);
HWND CreatePanel(const ControlOptions& options);
HWND CreateTextBox(const TextBoxOptions& options);
bool SetTextBoxText(HWND textBox, const std::wstring& text);
std::wstring GetTextBoxText(HWND textBox);
bool SetValidationError(HWND textBox, bool error);
HWND CreateNumericBox(const NumericBoxOptions& options);
bool SetNumericValue(HWND numericBox, double value);
std::optional<double> GetNumericValue(HWND numericBox);
HWND CreateCheckbox(const CheckableOptions& options);
HWND CreateToggle(const CheckableOptions& options);
bool SetChecked(HWND control, bool checked);
bool GetChecked(HWND control);
HWND CreateSlider(const SliderOptions& options);
bool SetSliderValue(HWND slider, double value);
std::optional<double> GetSliderValue(HWND slider);
HWND CreateProgressBar(const ProgressBarOptions& options);
bool SetProgressValue(HWND progressBar, double value);
std::optional<double> GetProgressValue(HWND progressBar);
bool SetProgressIndeterminate(HWND progressBar, bool indeterminate);

} // namespace wcw
