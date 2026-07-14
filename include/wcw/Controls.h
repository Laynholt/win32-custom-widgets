#pragma once

#include <wcw/Control.h>
#include <wcw/Style.h>

#include <windows.h>

#include <string>
#include <optional>
#include <vector>

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

enum class BuiltinIcon { Information, Warning, Error };

struct ImageSource {
    enum class Kind { None, Icon, Bitmap, Builtin };

    Kind kind{Kind::None};
    HANDLE handle{};
    BuiltinIcon builtin{BuiltinIcon::Information};

    ImageSource() = default;
    ImageSource(HICON icon) : kind(Kind::Icon), handle(icon) {}
    ImageSource(HBITMAP bitmap) : kind(Kind::Bitmap), handle(bitmap) {}
    ImageSource(BuiltinIcon icon) : kind(Kind::Builtin), builtin(icon) {}
};

struct MenuItem {
    int id{};
    std::wstring text;
    std::wstring shortcut;
    ImageSource image;
    std::vector<MenuItem> children;
    bool enabled{true};
    bool checked{};
    bool separator{};
};

struct ContextMenuOptions {
    std::vector<MenuItem> items;
    StyleOverride appearance;
};

struct MenuButtonOptions : ButtonOptions {
    std::vector<MenuItem> items;
    StyleOverride menuAppearance;
};

struct ComboItem {
    std::wstring text;
    std::intptr_t id{};
    ImageSource image;
};

struct ComboBoxOptions : ControlOptions {
    std::vector<ComboItem> items;
    int selectedIndex{-1};
    float popupHeightDip{240};
};

struct ScrollExtentDip {
    float width{};
    float height{};

    friend constexpr bool operator==(const ScrollExtentDip&, const ScrollExtentDip&) = default;
};

struct ScrollOffsetDip {
    float x{};
    float y{};

    friend constexpr bool operator==(const ScrollOffsetDip&, const ScrollOffsetDip&) = default;
};

struct ScrollViewOptions : ControlOptions {
    ScrollExtentDip contentExtent{};
    StyleOverride trackAppearance;
    StyleOverride thumbAppearance;
};

struct TooltipOptions {
    std::wstring text;
    UINT initialDelayMs{500};
    UINT reshowDelayMs{100};
    UINT autopopDelayMs{5000};
    float maxWidthDip{320};
    StyleOverride appearance;
};

inline constexpr UINT WCN_SELECTION_CHANGED = 0x5703;

struct SelectionChangedNotification {
    NMHDR header;
    int oldIndex;
    int newIndex;
    std::intptr_t oldId;
    std::intptr_t newId;
};

enum class ImageMode { Contain, Cover, Stretch };

HWND CreateButton(const ButtonOptions& options);
bool ShowContextMenu(HWND owner, POINT screenPosition, const ContextMenuOptions& options);
HWND CreateMenuButton(const MenuButtonOptions& options);
bool SetMenuItems(HWND menuButton, const std::vector<MenuItem>& items);
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
HWND CreateComboBox(const ComboBoxOptions& options);
bool SetComboItems(HWND comboBox, const std::vector<ComboItem>& items);
bool SetComboSelection(HWND comboBox, int index);
int GetComboSelection(HWND comboBox);
HWND CreateScrollView(const ScrollViewOptions& options);
bool SetScrollContentExtent(HWND scrollView, ScrollExtentDip extent);
bool SetScrollOffset(HWND scrollView, ScrollOffsetDip offset);
std::optional<ScrollOffsetDip> GetScrollOffset(HWND scrollView);
HWND GetScrollContentWindow(HWND scrollView);
bool AttachTooltip(HWND target, const TooltipOptions& options);
bool DetachTooltip(HWND target);
void HideAllTooltips();

} // namespace wcw
