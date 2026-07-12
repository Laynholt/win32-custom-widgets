#include "Accessibility.h"

#include "Internal.h"

#include <oleacc.h>

#include <atomic>
#include <charconv>
#include <string>
#include <unordered_map>

namespace wcw::internal {
namespace {

struct Info {
    AccessibleKind kind{};
    std::wstring name;
    bool readOnly{};
    bool password{};
};

class Accessible final : public IAccessible {
public:
    Accessible(HWND window, Info info)
        : window_(window), thread_(GetCurrentThreadId()), info_(std::move(info)) {
        CreateStdAccessibleObject(window, OBJID_CLIENT, IID_IAccessible,
                                  reinterpret_cast<void**>(&standard_));
    }

    void Disconnect() {
        window_ = nullptr;
        if (standard_) {
            standard_->Release();
            standard_ = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IAccessible) {
            *value = static_cast<IAccessible*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto remaining = --references_;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        return Forward([&] { return standard_->GetTypeInfoCount(count); });
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID locale, ITypeInfo** info) override {
        return Forward([&] { return standard_->GetTypeInfo(index, locale, info); });
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                                            LCID locale, DISPID* ids) override {
        return Forward([&] { return standard_->GetIDsOfNames(iid, names, count, locale, ids); });
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID locale, WORD flags,
                                     DISPPARAMS* params, VARIANT* result,
                                     EXCEPINFO* exception, UINT* argument) override {
        return Forward([&] { return standard_->Invoke(id, iid, locale, flags, params, result,
                                               exception, argument); });
    }

    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override {
        return Forward([&] { return standard_->get_accParent(parent); });
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override {
        return Forward([&] { return standard_->get_accChildCount(count); });
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** result) override {
        return Forward([&] { return standard_->get_accChild(child, result); });
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override {
        if (!name) return E_POINTER;
        *name = nullptr;
        const auto ready = SelfReady(child, name);
        if (FAILED(ready)) return ready;
        auto text = info_.name;
        if (text.empty()) text = WindowText();
        *name = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        return *name || text.empty() ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const auto ready = SelfReady(child, value);
        if (FAILED(ready)) return ready;
        if (info_.password) return S_FALSE;
        std::wstring text;
        switch (info_.kind) {
        case AccessibleKind::TextBox:
        case AccessibleKind::NumericBox:
            text = GetTextBoxText(window_);
            break;
        case AccessibleKind::Slider:
            text = Number(GetSliderValue(window_));
            break;
        case AccessibleKind::ProgressBar:
            if (!SendMessageW(window_, ProgressGetIndeterminateMessage, 0, 0))
                text = Number(GetProgressValue(window_));
            break;
        case AccessibleKind::ComboBox:
            SendMessageW(window_, ComboGetAccessibleValueMessage, 0,
                         reinterpret_cast<LPARAM>(&text));
            break;
        case AccessibleKind::ScrollView: {
            const auto offset = GetScrollOffset(window_);
            if (offset) text = Number(offset->x) + L", " + Number(offset->y);
            break;
        }
        default:
            return S_FALSE;
        }
        *value = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
        return *value || text.empty() ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR* description) override {
        return Forward([&] { return standard_->get_accDescription(child, description); });
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override {
        if (!role) return E_POINTER;
        VariantInit(role);
        const auto ready = SelfReady(child, role);
        if (FAILED(ready)) return ready;
        role->vt = VT_I4;
        switch (info_.kind) {
        case AccessibleKind::Button: role->lVal = ROLE_SYSTEM_PUSHBUTTON; break;
        case AccessibleKind::Checkbox:
        case AccessibleKind::Toggle: role->lVal = ROLE_SYSTEM_CHECKBUTTON; break;
        case AccessibleKind::TextBox: role->lVal = ROLE_SYSTEM_TEXT; break;
        case AccessibleKind::NumericBox: role->lVal = ROLE_SYSTEM_SPINBUTTON; break;
        case AccessibleKind::Slider: role->lVal = ROLE_SYSTEM_SLIDER; break;
        case AccessibleKind::ProgressBar: role->lVal = ROLE_SYSTEM_PROGRESSBAR; break;
        case AccessibleKind::ComboBox: role->lVal = ROLE_SYSTEM_COMBOBOX; break;
        case AccessibleKind::ScrollView: role->lVal = ROLE_SYSTEM_PANE; break;
        case AccessibleKind::Label: role->lVal = ROLE_SYSTEM_STATICTEXT; break;
        case AccessibleKind::Image: role->lVal = ROLE_SYSTEM_GRAPHIC; break;
        case AccessibleKind::Separator: role->lVal = ROLE_SYSTEM_SEPARATOR; break;
        case AccessibleKind::Panel: role->lVal = ROLE_SYSTEM_GROUPING; break;
        case AccessibleKind::Tooltip: role->lVal = ROLE_SYSTEM_TOOLTIP; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state) override {
        if (!state) return E_POINTER;
        VariantInit(state);
        const auto ready = SelfReady(child, state);
        if (FAILED(ready)) return ready;
        state->vt = VT_I4;
        auto flags = IsWindowEnabled(window_) ? 0L : STATE_SYSTEM_UNAVAILABLE;
        if (!IsWindowVisible(window_)) flags |= STATE_SYSTEM_INVISIBLE;
        const auto focus = GetFocus();
        if (focus == window_ || IsChild(window_, focus) ||
            (info_.kind == AccessibleKind::ComboBox &&
             (GetWindow(focus, GW_OWNER) == window_ ||
              SendMessageW(window_, ComboGetOpenMessage, 0, 0))))
            flags |= STATE_SYSTEM_FOCUSED;
        if (info_.kind == AccessibleKind::Button || info_.kind == AccessibleKind::Checkbox ||
            info_.kind == AccessibleKind::Toggle || info_.kind == AccessibleKind::TextBox ||
            info_.kind == AccessibleKind::NumericBox || info_.kind == AccessibleKind::Slider ||
            info_.kind == AccessibleKind::ComboBox || info_.kind == AccessibleKind::ScrollView)
            flags |= STATE_SYSTEM_FOCUSABLE;
        if ((info_.kind == AccessibleKind::Checkbox || info_.kind == AccessibleKind::Toggle) &&
            GetChecked(window_))
            flags |= STATE_SYSTEM_CHECKED;
        if (info_.kind == AccessibleKind::Button &&
            SendMessageW(window_, ButtonGetPressedMessage, 0, 0))
            flags |= STATE_SYSTEM_PRESSED;
        if (info_.kind == AccessibleKind::ComboBox) {
            flags |= STATE_SYSTEM_HASPOPUP |
                     (SendMessageW(window_, ComboGetOpenMessage, 0, 0)
                          ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED);
            if (GetComboSelection(window_) >= 0) flags |= STATE_SYSTEM_SELECTED;
        }
        if (info_.readOnly) flags |= STATE_SYSTEM_READONLY;
        if (info_.password) flags |= STATE_SYSTEM_PROTECTED;
        state->lVal = flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* help) override {
        return Forward([&] { return standard_->get_accHelp(child, help); });
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* helpFile, VARIANT child,
                                                long* topic) override {
        return Forward([&] { return standard_->get_accHelpTopic(helpFile, child, topic); });
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
        return Forward([&] { return standard_->get_accKeyboardShortcut(child, shortcut); });
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override {
        if (!focus) return E_POINTER;
        VariantInit(focus);
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        const auto focusWindow = GetFocus();
        if (focusWindow == window_ || IsChild(window_, focusWindow) ||
            (info_.kind == AccessibleKind::ComboBox &&
             (GetWindow(focusWindow, GW_OWNER) == window_ ||
              SendMessageW(window_, ComboGetOpenMessage, 0, 0)))) {
            focus->vt = VT_I4;
            focus->lVal = CHILDID_SELF;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection) override {
        return Forward([&] { return standard_->get_accSelection(selection); });
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) override {
        if (!action) return E_POINTER;
        *action = nullptr;
        const auto ready = SelfReady(child, action);
        if (FAILED(ready)) return ready;
        const wchar_t* text{};
        switch (info_.kind) {
        case AccessibleKind::Button: text = L"Press"; break;
        case AccessibleKind::Checkbox:
        case AccessibleKind::Toggle: text = GetChecked(window_) ? L"Uncheck" : L"Check"; break;
        case AccessibleKind::ComboBox:
            text = SendMessageW(window_, ComboGetOpenMessage, 0, 0) ? L"Close" : L"Open";
            break;
        default: return S_FALSE;
        }
        *action = SysAllocString(text);
        return *action ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) override {
        return Forward([&] { return standard_->accSelect(flags, child); });
    }
    HRESULT STDMETHODCALLTYPE accLocation(long* left, long* top, long* width, long* height,
                                          VARIANT child) override {
        return Forward([&] { return standard_->accLocation(left, top, width, height, child); });
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start,
                                          VARIANT* destination) override {
        return Forward([&] { return standard_->accNavigate(direction, start, destination); });
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) override {
        return Forward([&] { return standard_->accHitTest(x, y, child); });
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (child.vt != VT_I4 || child.lVal != CHILDID_SELF) return E_INVALIDARG;
        if (!IsWindowEnabled(window_)) return E_ACCESSDENIED;
        switch (info_.kind) {
        case AccessibleKind::Button:
            SendMessageW(window_, BM_CLICK, 0, 0);
            return S_OK;
        case AccessibleKind::Checkbox:
        case AccessibleKind::Toggle:
            SendMessageW(window_, WM_KEYDOWN, VK_SPACE, 0);
            SendMessageW(window_, WM_KEYUP, VK_SPACE, 0);
            return S_OK;
        case AccessibleKind::ComboBox:
            if (const auto popup = reinterpret_cast<HWND>(
                    SendMessageW(window_, ComboGetOpenMessage, 0, 0)))
                SendMessageW(popup, WM_KEYDOWN, VK_ESCAPE, 0);
            else
                SendMessageW(window_, WM_KEYDOWN, VK_F4, 0);
            return S_OK;
        default:
            return DISP_E_MEMBERNOTFOUND;
        }
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }

private:
    ~Accessible() {
        if (standard_) standard_->Release();
    }

    HRESULT Ready() const {
        if (GetCurrentThreadId() != thread_) return RPC_E_WRONG_THREAD;
        return window_ && IsWindow(window_) ? S_OK : CO_E_OBJNOTCONNECTED;
    }

    template <class Pointer>
    HRESULT SelfReady(const VARIANT& child, Pointer output) const {
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (!output) return E_POINTER;
        return child.vt == VT_I4 && child.lVal == CHILDID_SELF ? S_OK : E_INVALIDARG;
    }

    template <class Function>
    HRESULT Forward(Function&& function) const {
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        return standard_ ? function() : E_NOTIMPL;
    }

    std::wstring WindowText() const {
        const auto length = GetWindowTextLengthW(window_);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        if (length) GetWindowTextW(window_, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }

    static std::wstring Number(std::optional<double> value) {
        if (!value) return {};
        char buffer[64]{};
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *value,
                                          std::chars_format::general);
        return result.ec == std::errc{} ? std::wstring(buffer, result.ptr) : std::wstring{};
    }

    std::atomic_ulong references_{1};
    HWND window_{};
    DWORD thread_{};
    Info info_;
    IAccessible* standard_{};
};

struct Entry {
    Info info;
    Accessible* object{};
};

std::unordered_map<HWND, Entry>& Entries() {
    static std::unordered_map<HWND, Entry> entries;
    return entries;
}

} // namespace

void RegisterAccessibility(HWND window, AccessibleKind kind, const ControlOptions& options,
                           bool readOnly, bool password) {
    if (window) Entries()[window] = {{kind, options.accessibleName, readOnly, password}, nullptr};
}

bool HandleAccessibilityMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT& result) {
    if (message != WM_GETOBJECT || static_cast<LONG>(lParam) != OBJID_CLIENT) return false;
    const auto found = Entries().find(window);
    if (found == Entries().end()) return false;
    if (!found->second.object)
        found->second.object = new (std::nothrow) Accessible(window, found->second.info);
    if (!found->second.object) {
        result = 0;
        return true;
    }
    result = LresultFromObject(IID_IAccessible, wParam, found->second.object);
    return true;
}

void DestroyAccessibility(HWND window) {
    const auto found = Entries().find(window);
    if (found == Entries().end()) return;
    if (found->second.object) {
        found->second.object->Disconnect();
        found->second.object->Release();
    }
    Entries().erase(found);
}

void ShutdownAccessibility() {
    while (!Entries().empty()) DestroyAccessibility(Entries().begin()->first);
}

void NotifyAccessibility(HWND window, DWORD event) {
    if (IsWindow(window)) NotifyWinEvent(event, window, OBJID_CLIENT, CHILDID_SELF);
}

} // namespace wcw::internal
