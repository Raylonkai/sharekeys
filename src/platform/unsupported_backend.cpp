#include "platform/input_backend.h"
namespace sharekeys {
class UnsupportedBackend final : public InputBackend {
public:
    bool available() const override { return false; }
    bool start_capture(CaptureCallback, std::function<void(Hotkey)>, std::function<void(std::string)>, std::string& error) override {
        error = "Live input sharing requires Windows. Use --demo to explore the interface."; return false;
    }
    void stop_capture() noexcept override {}
    bool inject(const InputEvent&, std::string& error) override { error = "Windows input backend unavailable"; return false; }
    void remote_control(bool) override {}
    Point cursor() const override { return {}; }
    std::vector<Monitor> monitors(const std::string&) const override { return {}; }
    bool desktop_available() const override { return false; }
};
std::unique_ptr<InputBackend> make_input_backend() { return std::make_unique<UnsupportedBackend>(); }
}
