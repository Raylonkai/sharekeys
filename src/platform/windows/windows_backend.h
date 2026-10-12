#pragma once
#include "platform/input_backend.h"
#include <windows.h>
#include <set>

namespace sharekeys {
class WindowsBackend final : public InputBackend {
public:
    ~WindowsBackend() override { stop_capture(); }
    bool available() const override { return true; }
    bool start_capture(CaptureCallback, std::function<void(Hotkey)>, std::function<void(std::string)>, std::string&) override;
    void stop_capture() noexcept override;
    bool inject(const InputEvent&, std::string&) override;
    void remote_control(bool enabled) override { remote_ = enabled; }
    Point cursor() const override;
    std::vector<Monitor> monitors(const std::string& id) const override;
    bool desktop_available() const override;
private:
    static LRESULT CALLBACK keyboard_proc(int, WPARAM, LPARAM);
    static LRESULT CALLBACK mouse_proc(int, WPARAM, LPARAM);
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    void fail(const std::string& message);
    static thread_local WindowsBackend* current_;
    HWND window_{};
    HHOOK keyboard_{};
    HHOOK mouse_{};
    bool remote_{};
    CaptureCallback capture_;
    std::function<void(Hotkey)> hotkey_;
    std::function<void(std::string)> failure_;
    std::set<DWORD> keys_;
    std::set<DWORD> consumed_;
};
}
