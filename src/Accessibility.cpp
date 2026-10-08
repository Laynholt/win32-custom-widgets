#include "Accessibility.h"

#include "Internal.h"

#include <oleacc.h>
#include <commctrl.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <string>
#include <unordered_map>

namespace wcw::internal {
namespace {

struct Info {
    AccessibleKind kind{};
    std::wstring name;
    std::wstring fallbackName;
    bool readOnly{};
    bool password{};
    std::vector<AccessibleMenuItem> menuItems;
    int focusedChild{};
};

bool IsMenuKind(AccessibleKind kind) {
    return kind == AccessibleKind::Menu || kind == AccessibleKind::MenuBar;
}

bool EffectivelyEnabled(HWND window) {
    for (auto current = window; current; current = GetParent(current))
        if (!IsWindowEnabled(current)) return false;
    return true;
}

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

    void UpdateMenu(std::vector<AccessibleMenuItem> items, int focusedChild) {
        info_.menuItems = std::move(items);
        info_.focusedChild = focusedChild;
    }

    void SetName(std::wstring name) { info_.name = std::move(name); }

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
        if (count) *count = 0;
        return Forward([&] { return standard_->GetTypeInfoCount(count); });
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID locale, ITypeInfo** info) override {
        if (info) *info = nullptr;
        return Forward([&] { return standard_->GetTypeInfo(index, locale, info); });
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                                            LCID locale, DISPID* ids) override {
        if (ids) std::fill_n(ids, count, DISPID_UNKNOWN);
        return Forward([&] { return standard_->GetIDsOfNames(iid, names, count, locale, ids); });
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID locale, WORD flags,
                                     DISPPARAMS* params, VARIANT* result,
                                     EXCEPINFO* exception, UINT* argument) override {
        if (result) VariantInit(result);
        if (exception) *exception = {};
        if (argument) *argument = 0;
        return Forward([&] { return standard_->Invoke(id, iid, locale, flags, params, result,
                                               exception, argument); });
    }

    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override {
        if (parent) *parent = nullptr;
        return Forward([&] { return standard_->get_accParent(parent); });
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override {
        if (count) *count = 0;
        if (!IsMenuKind(info_.kind))
            return Forward([&] { return standard_->get_accChildCount(count); });
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (!count) return E_POINTER;
        *count = static_cast<long>(info_.menuItems.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** result) override {
        if (result) *result = nullptr;
        if (!IsMenuKind(info_.kind))
            return Forward([&] { return standard_->get_accChild(child, result); });
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (!result) return E_POINTER;
        return MenuItem(child) ? S_FALSE : E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override {
        if (!name) return E_POINTER;
        *name = nullptr;
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (const auto* item = MenuItem(child)) {
            *name = SysAllocStringLen(item->text.data(), static_cast<UINT>(item->text.size()));
            return *name || item->text.empty() ? S_OK : E_OUTOFMEMORY;
        }
        const auto selfReady = SelfReady(child, name);
        if (FAILED(selfReady)) return selfReady;
        auto text = info_.name;
        if (text.empty()) text = info_.fallbackName;
        if (text.empty() && !info_.password) text = WindowText();
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
        if (description) *description = nullptr;
        return Forward([&] { return standard_->get_accDescription(child, description); });
    }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override {
        if (!role) return E_POINTER;
        VariantInit(role);
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (MenuItem(child)) {
            role->vt = VT_I4;
            role->lVal = ROLE_SYSTEM_MENUITEM;
            return S_OK;
        }
        const auto selfReady = SelfReady(child, role);
        if (FAILED(selfReady)) return selfReady;
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
        case AccessibleKind::Menu: role->lVal = ROLE_SYSTEM_MENUPOPUP; break;
        case AccessibleKind::MenuBar: role->lVal = ROLE_SYSTEM_MENUBAR; break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state) override {
        if (!state) return E_POINTER;
        VariantInit(state);
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (const auto* item = MenuItem(child)) {
            state->vt = VT_I4;
            const bool enabled = item->enabled &&
                                 (info_.kind != AccessibleKind::MenuBar ||
                                  EffectivelyEnabled(window_));
            auto flags = enabled ? STATE_SYSTEM_FOCUSABLE : STATE_SYSTEM_UNAVAILABLE;
            if (item->checked) flags |= STATE_SYSTEM_CHECKED;
            if (item->hasPopup) flags |= STATE_SYSTEM_HASPOPUP;
            if (item->hasPopup)
                flags |= item->expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
            if (item->offscreen) flags |= STATE_SYSTEM_OFFSCREEN;
            if (child.lVal == info_.focusedChild) flags |= STATE_SYSTEM_FOCUSED;
            state->lVal = flags;
            return S_OK;
        }
        const auto selfReady = SelfReady(child, state);
        if (FAILED(selfReady)) return selfReady;
        state->vt = VT_I4;
        auto flags = EffectivelyEnabled(window_) ? 0L : STATE_SYSTEM_UNAVAILABLE;
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
            info_.kind == AccessibleKind::ComboBox || info_.kind == AccessibleKind::ScrollView ||
            info_.kind == AccessibleKind::MenuBar)
            flags |= STATE_SYSTEM_FOCUSABLE;
        if ((info_.kind == AccessibleKind::Button || info_.kind == AccessibleKind::Checkbox ||
             info_.kind == AccessibleKind::Toggle) &&
            GetChecked(window_))
            flags |= STATE_SYSTEM_CHECKED;
        if (info_.kind == AccessibleKind::Button &&
            SendMessageW(window_, ButtonGetPressedMessage, 0, 0))
            flags |= STATE_SYSTEM_PRESSED;
        if (info_.kind == AccessibleKind::Button) {
            const auto menu = SendMessageW(window_, ButtonGetMenuStateMessage, 0, 0);
            if (menu & 1)
                flags |= STATE_SYSTEM_HASPOPUP |
                         (menu & 2 ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED);
        }
        if (info_.kind == AccessibleKind::ComboBox) {
            flags |= STATE_SYSTEM_HASPOPUP |
                     (SendMessageW(window_, ComboGetOpenMessage, 0, 0)
                          ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED);
        }
        if (info_.readOnly) flags |= STATE_SYSTEM_READONLY;
        if (info_.password) flags |= STATE_SYSTEM_PROTECTED;
        state->lVal = flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* help) override {
        if (help) *help = nullptr;
        return Forward([&] { return standard_->get_accHelp(child, help); });
    }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* helpFile, VARIANT child,
                                                long* topic) override {
        if (helpFile) *helpFile = nullptr;
        if (topic) *topic = 0;
        return Forward([&] { return standard_->get_accHelpTopic(helpFile, child, topic); });
    }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* shortcut) override {
        if (shortcut) *shortcut = nullptr;
        return Forward([&] { return standard_->get_accKeyboardShortcut(child, shortcut); });
    }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override {
        if (!focus) return E_POINTER;
        VariantInit(focus);
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (IsMenuKind(info_.kind) && info_.focusedChild > 0) {
            focus->vt = VT_I4;
            focus->lVal = info_.focusedChild;
            return S_OK;
        }
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
        if (selection) VariantInit(selection);
        return Forward([&] { return standard_->get_accSelection(selection); });
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) override {
        if (!action) return E_POINTER;
        *action = nullptr;
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (MenuItem(child)) {
            *action = SysAllocString(MenuItem(child)->hasPopup ? L"Open" : L"Execute");
            return *action ? S_OK : E_OUTOFMEMORY;
        }
        const auto selfReady = SelfReady(child, action);
        if (FAILED(selfReady)) return selfReady;
        const wchar_t* text{};
        switch (info_.kind) {
        case AccessibleKind::Button: {
            const auto menu = SendMessageW(window_, ButtonGetMenuStateMessage, 0, 0);
            text = menu & 1 ? (menu & 2 ? L"Close" : L"Open") : L"Press";
            break;
        }
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
        if (left) *left = 0;
        if (top) *top = 0;
        if (width) *width = 0;
        if (height) *height = 0;
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (const auto* item = MenuItem(child)) {
            if (!left || !top || !width || !height) return E_POINTER;
            *left = item->screenBounds.left;
            *top = item->screenBounds.top;
            *width = item->screenBounds.right - item->screenBounds.left;
            *height = item->screenBounds.bottom - item->screenBounds.top;
            return S_OK;
        }
        return Forward([&] { return standard_->accLocation(left, top, width, height, child); });
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start,
                                          VARIANT* destination) override {
        if (destination) VariantInit(destination);
        if (!IsMenuKind(info_.kind))
            return Forward([&] { return standard_->accNavigate(direction, start, destination); });
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (!destination) return E_POINTER;
        if (start.vt != VT_I4) return E_INVALIDARG;
        long child{};
        if (start.lVal == CHILDID_SELF && direction == NAVDIR_FIRSTCHILD &&
            !info_.menuItems.empty())
            child = 1;
        else if (start.lVal == CHILDID_SELF && direction == NAVDIR_LASTCHILD &&
                 !info_.menuItems.empty())
            child = static_cast<long>(info_.menuItems.size());
        else if (MenuItem(start) && (direction == NAVDIR_NEXT || direction == NAVDIR_DOWN) &&
                 start.lVal < static_cast<long>(info_.menuItems.size()))
            child = start.lVal + 1;
        else if (MenuItem(start) &&
                 (direction == NAVDIR_PREVIOUS || direction == NAVDIR_UP) && start.lVal > 1)
            child = start.lVal - 1;
        if (!child) return S_FALSE;
        destination->vt = VT_I4;
        destination->lVal = child;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) override {
        if (child) VariantInit(child);
        if (IsMenuKind(info_.kind)) {
            const auto ready = Ready();
            if (FAILED(ready)) return ready;
            if (!child) return E_POINTER;
            if (!WindowRegionContainsScreenPoint(window_, {x, y})) return S_FALSE;
            for (size_t index = 0; index < info_.menuItems.size(); ++index) {
                const auto& item = info_.menuItems[index];
                if (!item.offscreen && x >= item.screenBounds.left && x < item.screenBounds.right &&
                    y >= item.screenBounds.top && y < item.screenBounds.bottom) {
                    child->vt = VT_I4;
                    child->lVal = static_cast<long>(index + 1);
                    return S_OK;
                }
            }
            child->vt = VT_I4;
            child->lVal = CHILDID_SELF;
            return S_OK;
        }
        return Forward([&] { return standard_->accHitTest(x, y, child); });
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        const auto ready = Ready();
        if (FAILED(ready)) return ready;
        if (const auto* item = MenuItem(child)) {
            if (!item->enabled ||
                (info_.kind == AccessibleKind::MenuBar && !EffectivelyEnabled(window_)))
                return E_ACCESSDENIED;
            SendMessageW(window_, MenuActivateAccessibleMessage, item->row, 0);
            return S_OK;
        }
        if (child.vt != VT_I4 || child.lVal != CHILDID_SELF) return E_INVALIDARG;
        if (!EffectivelyEnabled(window_)) return E_ACCESSDENIED;
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

    const AccessibleMenuItem* MenuItem(const VARIANT& child) const {
        if (!IsMenuKind(info_.kind) || child.vt != VT_I4 || child.lVal <= 0 ||
            child.lVal > static_cast<long>(info_.menuItems.size()))
            return nullptr;
        return &info_.menuItems[static_cast<size_t>(child.lVal - 1)];
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
                           bool readOnly, bool password,
                           std::optional<std::wstring> fallbackName) {
    if (window)
        Entries()[window] = {{kind, options.accessibleName,
                              fallbackName.value_or(options.text), readOnly, password}, nullptr};
}

void RegisterMenu(HWND window, AccessibleKind kind, std::vector<AccessibleMenuItem> items) {
    if (window)
        Entries()[window] = {{kind, {}, {}, false, false, std::move(items)}, nullptr};
}

void UpdateMenu(HWND window, AccessibleKind kind, std::vector<AccessibleMenuItem> items,
                int focusedChild) {
    const auto found = Entries().find(window);
    if (found == Entries().end() || found->second.info.kind != kind) return;
    found->second.info.menuItems = items;
    found->second.info.focusedChild = focusedChild;
    if (found->second.object)
        found->second.object->UpdateMenu(std::move(items), focusedChild);
}

void RegisterMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items) {
    RegisterMenu(window, AccessibleKind::Menu, std::move(items));
}

void RegisterMenuBarAccessibility(HWND window, std::vector<AccessibleMenuItem> items) {
    RegisterMenu(window, AccessibleKind::MenuBar, std::move(items));
}

void UpdateMenuAccessibility(HWND window, std::vector<AccessibleMenuItem> items,
                             int focusedChild) {
    UpdateMenu(window, AccessibleKind::Menu, std::move(items), focusedChild);
}

void UpdateMenuBarAccessibility(HWND window, std::vector<AccessibleMenuItem> items,
                                int focusedChild) {
    UpdateMenu(window, AccessibleKind::MenuBar, std::move(items), focusedChild);
}

bool HandleAccessibilityMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                LRESULT& result) {
    if (message == SetAccessibleNameMessage) {
        result = FALSE;
        const auto found = Entries().find(window);
        if (found != Entries().end() && lParam) {
            const auto& name = *reinterpret_cast<const std::wstring*>(lParam);
            if (found->second.info.name != name) {
                auto replacement = name;
                if (found->second.object) found->second.object->SetName(name);
                found->second.info.name = std::move(replacement);
                NotifyAccessibility(window, EVENT_OBJECT_NAMECHANGE);
            }
            result = TRUE;
        }
        return true;
    }
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

void NotifyAccessibility(HWND window, DWORD event, LONG child) {
    if (IsWindow(window)) NotifyWinEvent(event, window, OBJID_CLIENT, child);
}

void NotifyAccessibilityFocus(HWND window, bool fromChild) {
    const auto found = Entries().find(window);
    if (found == Entries().end()) return;
    const bool text = found->second.info.kind == AccessibleKind::TextBox ||
                      found->second.info.kind == AccessibleKind::NumericBox;
    if (text == fromChild) NotifyAccessibility(window, EVENT_OBJECT_FOCUS);
}

} // namespace wcw::internal

namespace wcw {
bool SetAccessibleName(HWND control, const std::wstring& name) {
    if (!internal::IsLibraryWindow(control)) return false;
    wchar_t className[32]{};
    if (GetClassNameW(control, className, 32) && lstrcmpW(className, WC_TABCONTROLW) == 0) {
        if (!SetWindowTextW(control, name.c_str())) return false;
        internal::NotifyAccessibility(control, EVENT_OBJECT_NAMECHANGE);
        return true;
    }
    return SendMessageW(control, internal::SetAccessibleNameMessage, 0,
                        reinterpret_cast<LPARAM>(&name)) != FALSE;
}
} // namespace wcw
