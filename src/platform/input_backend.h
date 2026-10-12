#pragma once
#include "core/input_event.h"
#include "core/monitor_layout.h"
#include <functional>
#include <memory>
#include <string>

namespace sharekeys {
enum class Hotkey { home, next, mirror };
using CaptureCallback = std::function<bool(const InputEvent&)>;
class InputBackend {
public:
    virtual ~InputBackend() = default;
    virtual bool available() const = 0;
    virtual bool start_capture(CaptureCallback callback, std::function<void(Hotkey)> hotkey,
                               std::function<void(std::string)> failure, std::string& error) = 0;
    virtual void stop_capture() noexcept = 0;
    virtual bool inject(const InputEvent& event, std::string& error) = 0;
    virtual void remote_control(bool enabled) = 0;
    virtual Point cursor() const = 0;
    virtual std::vector<Monitor> monitors(const std::string& id) const = 0;
    virtual bool desktop_available() const = 0;
};
std::unique_ptr<InputBackend> make_input_backend();
} // namespace sharekeys
