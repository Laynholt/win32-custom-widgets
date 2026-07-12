#include <wcw/Controls.h>
#include <wcw/Geometry.h>
#include <wcw/Runtime.h>

#include "Internal.h"
#include "Accessibility.h"
#include "NumericModel.h"
#include "Paint.h"

#include <commctrl.h>

#include <memory>
#include <new>
#include <string>

namespace wcw {
namespace {

constexpr wchar_t TextBoxClass[] = L"WcwTextBox";
constexpr UINT_PTR EditSubclassId = 1;

struct Creation {
    TextBoxOptions text;
    const NumericBoxOptions* numeric{};
};

struct TextBoxState {
    HWND edit{};
    std::wstring placeholder;
    bool validationError{};
    bool suppressChange{};
    bool readOnly{};
    bool password{};
    std::unique_ptr<internal::NumericModel> numeric;
    HBRUSH editBrush{};
};

COLORREF ColorRef(Color color) { return RGB(color.r, color.g, color.b); }

void RefreshBrush(HWND window, TextBoxState& state) {
    if (state.editBrush) DeleteObject(state.editBrush);
    const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(window));
    state.editBrush = CreateSolidBrush(
        ColorRef(IsWindowEnabled(window) ? style.background : style.disabledSurface));
}

void LayoutEdit(HWND window, TextBoxState& state) {
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(window));
    const auto dpi = paint::Dpi(window);
    const int x = DipToPx(style.paddingXDip, dpi);
    const int y = (std::max)(1, DipToPx(style.paddingYDip, dpi) / 2);
    MoveWindow(state.edit, x, y, (std::max)(0, static_cast<int>(bounds.right) - x * 2),
               (std::max)(0, static_cast<int>(bounds.bottom) - y * 2), TRUE);
}

void ApplyFont(HWND window, TextBoxState& state) {
    const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(window));
    SendMessageW(state.edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(paint::Font(style.font, paint::Dpi(window))), TRUE);
}

void NotifyValue(HWND window, double value) {
    ValueChangedNotification notification{{window, static_cast<UINT_PTR>(GetDlgCtrlID(window)),
                                            WCN_VALUE_CHANGED},
                                           value};
    SendMessageW(GetParent(window), WM_NOTIFY, notification.header.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
}

void SetEditText(TextBoxState& state, const std::wstring& text) {
    state.suppressChange = true;
    SetWindowTextW(state.edit, text.c_str());
    state.suppressChange = false;
    ShowWindow(state.edit, !text.empty() || GetFocus() == state.edit ? SW_SHOWNA : SW_HIDE);
}

void OnEditChanged(HWND window, TextBoxState& state) {
    if (state.suppressChange) return;
    const int length = GetWindowTextLengthW(state.edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    if (length) GetWindowTextW(state.edit, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    if (!state.numeric) {
        if (!state.password) SetWindowTextW(window, text.c_str());
        InvalidateRect(window, nullptr, FALSE);
    } else {
        const auto before = state.numeric->CommittedValue();
        const auto parsed = state.numeric->SetText(text);
        state.validationError = parsed == internal::NumericTextState::Invalid;
        if (parsed == internal::NumericTextState::Valid) {
            SetEditText(state, state.numeric->Text());
            if (state.numeric->CommittedValue() != before)
                NotifyValue(window, state.numeric->CommittedValue());
        }
    }
    internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
    SendMessageW(GetParent(window), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(window), EN_CHANGE), reinterpret_cast<LPARAM>(window));
    InvalidateRect(window, nullptr, FALSE);
}

bool StepNumeric(HWND window, TextBoxState& state, int direction) {
    if (!state.numeric || state.readOnly || !IsWindowEnabled(window) || !direction) return false;
    if (!state.numeric->Step(direction)) return true;
    state.validationError = false;
    SetEditText(state, state.numeric->Text());
    InvalidateRect(window, nullptr, FALSE);
    internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
    NotifyValue(window, state.numeric->CommittedValue());
    return true;
}

void PaintTextBox(HWND window, TextBoxState& state) {
    PAINTSTRUCT ps{};
    const auto target = BeginPaint(window, &ps);
    RECT bounds{};
    GetClientRect(window, &bounds);
    paint::Buffer buffer(target, bounds);
    if (buffer) {
        const auto theme = GetTheme();
        const auto style = ResolveStyle(theme, internal::WindowStyleOverride(window));
        const auto dpi = paint::Dpi(window);
        const Gdiplus::RectF shape{0, 0, static_cast<float>(bounds.right),
                                   static_cast<float>(bounds.bottom)};
        const auto radius = paint::ToPixels(style.cornerRadiusDip, dpi);
        Gdiplus::Graphics graphics(buffer.dc());
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        paint::Clear(buffer.dc(), bounds, theme.palette.window);
        paint::Fill(graphics, shape, radius,
                    IsWindowEnabled(window) ? style.background : style.disabledSurface);
        paint::Border(graphics, shape, radius,
                      state.validationError ? style.danger : style.border,
                      paint::ToPixels(style.borderWidthDip, dpi));
        if (GetFocus() == state.edit)
            paint::Focus(graphics, shape, radius, style.focus,
                         paint::ToPixels(style.focusWidthDip, dpi));
        if (GetWindowTextLengthW(state.edit) == 0 && GetFocus() != state.edit &&
            !state.placeholder.empty()) {
            RECT textBounds = bounds;
            InflateRect(&textBounds, -DipToPx(style.paddingXDip, dpi), 0);
            paint::Text(buffer.dc(), state.placeholder, textBounds, paint::Font(style.font, dpi),
                        style.mutedText);
        }
    }
    EndPaint(window, &ps);
}

LRESULT EditProcImpl(HWND edit, UINT message, WPARAM wParam, LPARAM lParam) {
    const auto outer = GetParent(edit);
    if (message == WM_SETFOCUS) {
        const auto result = DefSubclassProc(edit, message, wParam, lParam);
        internal::NotifyAccessibilityFocus(outer, true);
        InvalidateRect(outer, nullptr, FALSE);
        return result;
    }
    if (message == WM_KILLFOCUS) {
        const auto result = DefSubclassProc(edit, message, wParam, lParam);
        if (GetWindowTextLengthW(edit) == 0) ShowWindow(edit, SW_HIDE);
        InvalidateRect(outer, nullptr, FALSE);
        return result;
    }
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_UP || wParam == VK_DOWN) {
            if (SendMessageW(outer, internal::NumericStepMessage,
                             wParam == VK_UP ? 1 : -1, 0))
                return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta && SendMessageW(outer, internal::NumericStepMessage, delta > 0 ? 1 : -1, 0))
            return 0;
        break;
    }
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam,
                          UINT_PTR, DWORD_PTR) {
    try {
        return EditProcImpl(edit, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

LRESULT TextBoxProcImpl(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto state = reinterpret_cast<TextBoxState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto creation = static_cast<const Creation*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        auto created = std::make_unique<TextBoxState>();
        created->placeholder = creation->text.placeholder;
        created->readOnly = creation->text.readOnly;
        created->password = creation->text.password;
        if (creation->numeric) {
            const auto& options = *creation->numeric;
            created->numeric = std::make_unique<internal::NumericModel>(
                options.mode, options.minimum, options.maximum, options.step, options.value);
        }
        internal::RegisterWindow(window);
        state = created.release();
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (message == internal::ThemeChangedMessage && state) {
        RefreshBrush(window, *state);
        ApplyFont(window, *state);
    }
    LRESULT shared{};
    if (internal::HandleControlMessage(window, message, wParam, lParam, shared)) return shared;

    switch (message) {
    case WM_CREATE: {
        const auto creation = static_cast<const Creation*>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
        if (creation->text.readOnly) editStyle |= ES_READONLY;
        if (creation->text.password) editStyle |= ES_PASSWORD;
        state->edit = CreateWindowExW(WS_EX_TRANSPARENT, L"EDIT", L"", editStyle, 0, 0, 0, 0,
                                      window, nullptr, internal::Instance(), nullptr);
        if (!state->edit || !SetWindowSubclass(state->edit, EditProc, EditSubclassId, 0)) return -1;
        RefreshBrush(window, *state);
        ApplyFont(window, *state);
        LayoutEdit(window, *state);
        SetEditText(*state, state->numeric ? state->numeric->Text() : creation->text.text);
        return 0;
    }
    case WM_COMMAND:
        if (state && reinterpret_cast<HWND>(lParam) == state->edit && HIWORD(wParam) == EN_CHANGE)
            OnEditChanged(window, *state);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        if (state && reinterpret_cast<HWND>(lParam) == state->edit) {
            const auto style = ResolveStyle(GetTheme(), internal::WindowStyleOverride(window));
            SetTextColor(reinterpret_cast<HDC>(wParam),
                         ColorRef(IsWindowEnabled(window) ? style.text : style.disabledText));
            SetBkColor(reinterpret_cast<HDC>(wParam),
                       ColorRef(IsWindowEnabled(window) ? style.background
                                                        : style.disabledSurface));
            return reinterpret_cast<LRESULT>(state->editBrush);
        }
        break;
    case WM_SETFOCUS:
        if (state) {
            ShowWindow(state->edit, SW_SHOWNA);
            SetFocus(state->edit);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (state) {
            ShowWindow(state->edit, SW_SHOWNA);
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, 0);
        }
        return 0;
    case WM_LBUTTONUP:
        return 0;
    case WM_ENABLE:
        if (state) {
            EnableWindow(state->edit, wParam != 0);
            RefreshBrush(window, *state);
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_SIZE:
        if (state && state->edit) LayoutEdit(window, *state);
        return 0;
    case WM_SETFONT:
        if (state) SendMessageW(state->edit, WM_SETFONT, wParam, lParam);
        return 0;
    case WM_PAINT:
        if (state) PaintTextBox(window, *state);
        return 0;
    case internal::TextBoxSetTextMessage:
        if (state) {
            const auto before = GetTextBoxText(window);
            SetEditText(*state, *reinterpret_cast<const std::wstring*>(lParam));
            if (state->numeric) OnEditChanged(window, *state);
            else if (before != *reinterpret_cast<const std::wstring*>(lParam))
                internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
            InvalidateRect(window, nullptr, FALSE);
            return TRUE;
        }
        return FALSE;
    case internal::TextBoxGetTextMessage:
        if (state) {
            const int length = GetWindowTextLengthW(state->edit);
            auto& output = *reinterpret_cast<std::wstring*>(lParam);
            output.assign(static_cast<size_t>(length) + 1, L'\0');
            if (length) GetWindowTextW(state->edit, output.data(), length + 1);
            output.resize(static_cast<size_t>(length));
            return TRUE;
        }
        return FALSE;
    case internal::TextBoxSetErrorMessage:
        if (state) state->validationError = wParam != 0;
        InvalidateRect(window, nullptr, FALSE);
        return state != nullptr;
    case internal::NumericSetValueMessage:
        if (state && state->numeric) {
            const auto changed = state->numeric->SetValue(*reinterpret_cast<const double*>(lParam));
            state->validationError = false;
            SetEditText(*state, state->numeric->Text());
            InvalidateRect(window, nullptr, FALSE);
            if (changed) {
                internal::NotifyAccessibility(window, EVENT_OBJECT_VALUECHANGE);
                NotifyValue(window, state->numeric->CommittedValue());
            }
            return TRUE;
        }
        return FALSE;
    case internal::NumericGetValueMessage:
        if (state && state->numeric) {
            if (lParam)
                *reinterpret_cast<std::optional<double>*>(lParam) = state->numeric->Value();
            return TRUE;
        }
        return FALSE;
    case internal::NumericStepMessage:
        return state && StepNumeric(window, *state, static_cast<int>(wParam));
    case WM_NCDESTROY:
        if (state) {
            if (state->edit) RemoveWindowSubclass(state->edit, EditProc, EditSubclassId);
            if (state->editBrush) DeleteObject(state->editBrush);
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK TextBoxProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return TextBoxProcImpl(window, message, wParam, lParam);
    } catch (const std::bad_alloc&) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    } catch (...) {
        SetLastError(ERROR_GEN_FAILURE);
    }
    return message == WM_NCCREATE ? FALSE : DefWindowProcW(window, message, wParam, lParam);
}

HWND Create(const TextBoxOptions& options, const NumericBoxOptions* numeric) {
    if (!options.parent || !IsWindow(options.parent) ||
        GetWindowThreadProcessId(options.parent, nullptr) != GetCurrentThreadId()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    const auto dpi = paint::Dpi(options.parent);
    Creation creation{options, numeric};
    const auto window = CreateWindowExW(
        0, TextBoxClass, L"", WS_CHILD | WS_TABSTOP | options.style,
        DipToPx(options.bounds.x, dpi), DipToPx(options.bounds.y, dpi),
        DipToPx(options.bounds.width, dpi), DipToPx(options.bounds.height, dpi), options.parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(options.id)), internal::Instance(), &creation);
    if (window) SetStyleOverride(window, options.appearance);
    internal::RegisterAccessibility(window,
        numeric ? internal::AccessibleKind::NumericBox : internal::AccessibleKind::TextBox,
        options, options.readOnly, options.password,
        options.password ? std::optional<std::wstring>(options.placeholder) : std::nullopt);
    return window;
}

} // namespace

HWND CreateTextBox(const TextBoxOptions& options) { return Create(options, nullptr); }

bool SetTextBoxText(HWND textBox, const std::wstring& text) {
    if (!internal::IsTextBoxWindow(textBox)) return false;
    return SendMessageW(textBox, internal::TextBoxSetTextMessage, 0,
                        reinterpret_cast<LPARAM>(&text)) != FALSE;
}

std::wstring GetTextBoxText(HWND textBox) {
    std::wstring text;
    if (!internal::IsTextBoxWindow(textBox)) return text;
    SendMessageW(textBox, internal::TextBoxGetTextMessage, 0, reinterpret_cast<LPARAM>(&text));
    return text;
}

bool SetValidationError(HWND textBox, bool error) {
    if (!internal::IsTextBoxWindow(textBox)) return false;
    return SendMessageW(textBox, internal::TextBoxSetErrorMessage, error, 0) != FALSE;
}

namespace internal {

bool IsTextBoxWindow(HWND window, bool numericOnly) {
    wchar_t className[32]{};
    if (!IsLibraryWindow(window) ||
        GetClassNameW(window, className, 32) == 0 || std::wstring_view(className) != TextBoxClass ||
        (numericOnly && SendMessageW(window, NumericGetValueMessage, 0, 0) == FALSE)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    return true;
}

HWND CreateNumericBoxWindow(const NumericBoxOptions& options) { return Create(options, &options); }

bool RegisterTextBoxClass() { return RegisterControlClass(TextBoxClass, TextBoxProc); }

} // namespace internal
} // namespace wcw
