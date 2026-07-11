#pragma once

#include <wcw/Control.h>
#include <wcw/Style.h>

#include <windows.h>

#include <string>

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

} // namespace wcw
