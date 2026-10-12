#pragma once

#include "core/input_event.h"
#include <set>
#include <string>
#include <vector>

namespace sharekeys {
enum class ControlMode { individual, mirror };
struct RoutingDecision {
    bool deliver_to_host{true};
    std::vector<std::string> clients;
};
struct ReleaseRequest {
    std::string computer_id;
    std::vector<InputEvent> events;
};
struct Transition {
    std::vector<ReleaseRequest> releases;
};

// Single-threaded state owner. An orchestrator serializes input and connection
// callbacks, then applies releases before delivering subsequent input.
class ControlEngine {
public:
    explicit ControlEngine(std::string host_id);
    void connect_client(const std::string& id);
    Transition disconnect_client(const std::string& id);
    Transition activate(const std::string& id);
    Transition enable_mirror(const std::set<std::string>& participants);
    Transition return_to_host();
    RoutingDecision route(const InputEvent& event);

    ControlMode mode() const { return mode_; }
    const std::string& active_computer() const { return active_; }
    const std::string& host_id() const { return host_; }
private:
    RoutingDecision destinations(const InputEvent& event) const;
    Transition release_held();
    std::string host_;
    std::string active_;
    ControlMode mode_{ControlMode::individual};
    std::set<std::string> connected_;
    std::set<std::string> participants_;
    std::vector<KeyEvent> held_keys_;
    std::set<MouseButton> held_buttons_;
};
} // namespace sharekeys
