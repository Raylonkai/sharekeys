#include "core/control_engine.h"
#include <iostream>
#include <stdexcept>

using namespace sharekeys;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        ControlEngine engine("host");
        const KeyEvent down{0x1d, false, true};
        const KeyEvent up{0x1d, false, false};
        require(engine.route(MouseMove{1, 2}).deliver_to_host, "Initial host control");
        engine.connect_client("a");
        engine.connect_client("b");
        engine.activate("a");
        auto route = engine.route(down);
        require(!route.deliver_to_host && route.clients == std::vector<std::string>{"a"},
                "Client input must suppress host input");
        engine.route(down); // Key repeat must not duplicate release requests.
        engine.route(MouseButtonEvent{MouseButton::left, true});
        auto transition = engine.enable_mirror({"a", "b"});
        require(transition.releases.size() == 1 && transition.releases[0].computer_id == "a",
                "Release previous destination on mode change");
        require(transition.releases[0].events.size() == 2, "Release key and mouse button once");
        require(!std::get<KeyEvent>(transition.releases[0].events[0]).pressed,
                "Generated key release");
        route = engine.route(down);
        require(route.deliver_to_host && route.clients.size() == 2, "Mirror keyboard destinations");
        for (const InputEvent event : {InputEvent{MouseMove{2, 3}},
                                      InputEvent{MouseScroll{0, 120}},
                                      InputEvent{MouseButtonEvent{MouseButton::right, true}}}) {
            route = engine.route(event);
            require(route.deliver_to_host && route.clients.empty(), "Mirror mouse is host only");
        }
        transition = engine.return_to_host();
        require(engine.mode() == ControlMode::individual && engine.active_computer() == "host",
                "Mirror exit must return to host");
        require(transition.releases.size() == 3, "Release held mirror input on all destinations");
        engine.route(up);
        engine.activate("a");
        engine.route(down);
        transition = engine.disconnect_client("b");
        require(engine.active_computer() == "host", "Any lost connection restores host control");
        require(transition.releases.size() == 1, "Disconnect releases active client's keys");
        bool rejected = false;
        try { engine.activate("missing"); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && engine.active_computer() == "host", "Invalid switch preserves state");
        engine.enable_mirror({"a"});
        rejected = false;
        try { engine.enable_mirror({"missing"}); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && engine.mode() == ControlMode::mirror, "Invalid mirror participants preserve state");
        engine.disconnect_client("a");
        require(engine.mode() == ControlMode::individual, "Mirror disconnect restores individual mode");
        std::cout << "Control engine checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
