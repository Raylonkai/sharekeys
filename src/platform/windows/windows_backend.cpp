#include "platform/windows/windows_backend.h"
#include "third_party/deskflow/mouse_buttons.h"
#include <algorithm>
#include <array>
#include <vector>

namespace sharekeys {
thread_local WindowsBackend* WindowsBackend::current_ = nullptr;
namespace {
constexpr ULONG_PTR injected_tag = 0x534b4559;
std::string win_error(const char* operation) {
    return std::string(operation) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}
bool send(INPUT input, std::string& error) {
    if (SendInput(1, &input, sizeof(input)) == 1) return true;
    error = win_error("SendInput") + "; check application elevation and desktop access";
    return false;
}
}
bool WindowsBackend::start_capture(CaptureCallback capture, std::function<void(Hotkey)> hotkey,
                                    std::function<void(std::string)> failure, std::string& error) {
    stop_capture();
    if (current_ && current_ != this) { error = "Capture already active on this thread"; return false; }
    capture_ = std::move(capture); hotkey_ = std::move(hotkey); failure_ = std::move(failure);
    current_ = this;
    WNDCLASSW klass{};
    klass.lpfnWndProc = window_proc;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"ShareKeysInputWindow";
    RegisterClassW(&klass);
    window_ = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, klass.hInstance, nullptr);
    if (!window_) { error = win_error("Input window creation"); stop_capture(); return false; }
    RAWINPUTDEVICE mouse{0x01, 0x02, RIDEV_INPUTSINK, window_};
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) {
        error = win_error("Raw mouse registration"); stop_capture(); return false;
    }
    const std::array<UINT, 3> codes{VK_HOME, VK_RIGHT, 'M'};
    for (int i = 0; i < 3; ++i) {
        if (!RegisterHotKey(window_, i + 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, codes[i])) {
            error = "A ShareKeys hotkey is already registered by another application";
            stop_capture(); return false;
        }
    }
    keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, klass.hInstance, 0);
    mouse_ = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, klass.hInstance, 0);
    if (!keyboard_ || !mouse_) { error = win_error("Input hook installation"); stop_capture(); return false; }
    return true;
}
void WindowsBackend::stop_capture() noexcept {
    remote_ = false;
    if (keyboard_) UnhookWindowsHookEx(keyboard_);
    if (mouse_) UnhookWindowsHookEx(mouse_);
    keyboard_ = nullptr; mouse_ = nullptr;
    if (window_) {
        for (int id = 1; id <= 3; ++id) UnregisterHotKey(window_, id);
        RAWINPUTDEVICE mouse{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
        DestroyWindow(window_); window_ = nullptr;
    }
    if (current_ == this) current_ = nullptr;
    capture_ = {}; hotkey_ = {}; failure_ = {};
    keys_.clear(); consumed_.clear();
}
void WindowsBackend::fail(const std::string& message) {
    remote_ = false;
    if (failure_) failure_(message);
}
LRESULT CALLBACK WindowsBackend::keyboard_proc(int code, WPARAM message, LPARAM data) {
    auto* self = current_;
    if (code < 0 || !self) return CallNextHookEx(nullptr, code, message, data);
    const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
    if (key.flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, message, data);
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool repeat = self->keys_.count(key.vkCode) != 0;
    if (down) self->keys_.insert(key.vkCode); else self->keys_.erase(key.vkCode);
    if (self->consumed_.count(key.vkCode)) {
        if (!down) self->consumed_.erase(key.vkCode);
        return 1;
    }
    const bool ctrl = self->keys_.count(VK_LCONTROL) || self->keys_.count(VK_RCONTROL);
    const bool alt = self->keys_.count(VK_LMENU) || self->keys_.count(VK_RMENU);
    try {
        if (ctrl && alt && down && !repeat && (key.vkCode == VK_HOME || key.vkCode == VK_RIGHT || key.vkCode == 'M')) {
            self->consumed_.insert(key.vkCode);
            self->hotkey_(key.vkCode == VK_HOME ? Hotkey::home : key.vkCode == VK_RIGHT ? Hotkey::next : Hotkey::mirror);
            return 1;
        }
        if (!self->capture_(KeyEvent{static_cast<std::uint16_t>(key.scanCode), (key.flags & LLKHF_EXTENDED) != 0,
                                    down, static_cast<std::uint16_t>(key.vkCode)})) return 1;
    } catch (...) { self->fail("Keyboard capture failed"); }
    return CallNextHookEx(nullptr, code, message, data);
}
LRESULT CALLBACK WindowsBackend::mouse_proc(int code, WPARAM message, LPARAM data) {
    auto* self = current_;
    if (code < 0 || !self) return CallNextHookEx(nullptr, code, message, data);
    const auto& mouse = *reinterpret_cast<MSLLHOOKSTRUCT*>(data);
    if (mouse.flags & LLMHF_INJECTED) return CallNextHookEx(nullptr, code, message, data);
    if (message == WM_MOUSEMOVE) return self->remote_ ? 1 : CallNextHookEx(nullptr, code, message, data);
    InputEvent event;
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: event = MouseButtonEvent{MouseButton::left, message == WM_LBUTTONDOWN}; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: event = MouseButtonEvent{MouseButton::right, message == WM_RBUTTONDOWN}; break;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: event = MouseButtonEvent{MouseButton::middle, message == WM_MBUTTONDOWN}; break;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        event = MouseButtonEvent{HIWORD(mouse.mouseData) == XBUTTON1 ? MouseButton::extra1 : MouseButton::extra2, message == WM_XBUTTONDOWN}; break;
    case WM_MOUSEWHEEL: event = MouseScroll{0, static_cast<short>(HIWORD(mouse.mouseData))}; break;
    case WM_MOUSEHWHEEL: event = MouseScroll{static_cast<short>(HIWORD(mouse.mouseData)), 0}; break;
    default: return CallNextHookEx(nullptr, code, message, data);
    }
    // Send logical buttons, preserving the host's handedness on other machines.
    if (auto* button = std::get_if<MouseButtonEvent>(&event); button && GetSystemMetrics(SM_SWAPBUTTON)) {
        if (button->button == MouseButton::left) button->button = MouseButton::right;
        else if (button->button == MouseButton::right) button->button = MouseButton::left;
    }
    try { if (!self->capture_(event)) return 1; }
    catch (...) { self->fail("Mouse capture failed"); }
    return CallNextHookEx(nullptr, code, message, data);
}
LRESULT CALLBACK WindowsBackend::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = current_;
    if (self && message == WM_INPUT) {
        RAWINPUT raw{}; UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) {
            self->fail("Cannot read raw mouse input");
        } else if (raw.header.dwType == RIM_TYPEMOUSE && raw.header.hDevice && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            const auto& mouse = raw.data.mouse;
            if (mouse.lLastX || mouse.lLastY) {
                try { self->capture_(MouseMove{int(mouse.lLastX), int(mouse.lLastY)}); }
                catch (...) { self->fail("Raw mouse capture failed"); }
            }
        }
    } else if (self && message == WM_HOTKEY && self->hotkey_) {
        self->hotkey_(wparam == 1 ? Hotkey::home : wparam == 2 ? Hotkey::next : Hotkey::mirror);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
bool WindowsBackend::inject(const InputEvent& event, std::string& error) {
    INPUT input{};
    if (auto key = std::get_if<KeyEvent>(&event)) {
        input.type = INPUT_KEYBOARD;
        input.ki.dwExtraInfo = injected_tag;
        if (!key->scan_code || key->virtual_key == VK_PAUSE) input.ki.wVk = key->virtual_key;
        else { input.ki.wScan = key->scan_code; input.ki.dwFlags = KEYEVENTF_SCANCODE; }
        if (key->extended) input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (!key->pressed) input.ki.dwFlags |= KEYEVENTF_KEYUP;
        return send(input, error);
    }
    input.type = INPUT_MOUSE; input.mi.dwExtraInfo = injected_tag;
    if (auto point = std::get_if<MousePosition>(&event)) {
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        input.mi.dx = LONG((double(point->x - x) * 65535) / std::max(1, w - 1));
        input.mi.dy = LONG((double(point->y - y) * 65535) / std::max(1, h - 1));
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    } else if (auto move = std::get_if<MouseMove>(&event)) {
        const auto p = cursor(); return inject(MousePosition{p.x + move->dx, p.y + move->dy}, error);
    } else if (auto button = std::get_if<MouseButtonEvent>(&event)) {
        deskflow::encode_button(button->button, button->pressed, input.mi);
    } else if (auto scroll = std::get_if<MouseScroll>(&event)) {
        if (scroll->vertical) {
            input.mi.dwFlags = MOUSEEVENTF_WHEEL; input.mi.mouseData = DWORD(scroll->vertical);
            if (!send(input, error)) return false;
        }
        if (!scroll->horizontal) return true;
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL; input.mi.mouseData = DWORD(scroll->horizontal);
    }
    return send(input, error);
}
Point WindowsBackend::cursor() const { POINT point{}; GetCursorPos(&point); return {int(point.x), int(point.y)}; }
std::vector<Monitor> WindowsBackend::monitors(const std::string& id) const {
    std::vector<Monitor> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& list = *reinterpret_cast<std::vector<Monitor>*>(data);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return TRUE;
        char device[128]{};
        WideCharToMultiByte(CP_UTF8, 0, info.szDevice, -1, device, sizeof(device), nullptr, nullptr);
        auto r = info.rcMonitor;
        Rectangle bounds{int(r.left), int(r.top), int(r.right - r.left), int(r.bottom - r.top)};
        list.push_back({{}, device, bounds, bounds}); return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    for (auto& monitor : result) monitor.computer_id = id;
    return result;
}
bool WindowsBackend::desktop_available() const {
    const HDESK desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS | DESKTOP_SWITCHDESKTOP);
    if (!desktop) return false;
    wchar_t name[256]{}; DWORD size{};
    const bool ok = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &size) != FALSE;
    CloseDesktop(desktop);
    return ok && _wcsicmp(name, L"Default") == 0;
}
std::unique_ptr<InputBackend> make_input_backend() { return std::make_unique<WindowsBackend>(); }
}
