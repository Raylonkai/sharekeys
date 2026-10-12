#include "core/control_engine.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sharekeys {
ControlEngine::ControlEngine(std::string host_id)
    : host_(std::move(host_id)), active_(host_) {
    if (host_.empty()) throw std::invalid_argument("Host ID must not be empty");
}
void ControlEngine::connect_client(const std::string& id) {
    if (id.empty() || id == host_) throw std::invalid_argument("Invalid client ID");
    connected_.insert(id);
}
Transition ControlEngine::disconnect_client(const std::string& id) {
    if (!connected_.count(id)) return {};
    // A lost connection is a runtime failure: restore host control, even if
    // the lost client was not active. Releases to it may be undeliverable.
    auto transition = return_to_host();
    connected_.erase(id);
    return transition;
}
Transition ControlEngine::activate(const std::string& id) {
    if (mode_ == ControlMode::mirror)
        throw std::logic_error("Leave mirror mode before switching computers");
    if (id != host_ && !connected_.count(id))
        throw std::invalid_argument("Client is not connected");
    if (id == active_) return {};
    auto transition = release_held();
    active_ = id;
    return transition;
}
Transition ControlEngine::enable_mirror(const std::set<std::string>& participants) {
    for (const auto& id : participants)
        if (!connected_.count(id)) throw std::invalid_argument("Mirror client is not connected");
    auto transition = release_held();
    participants_ = participants;
    mode_ = ControlMode::mirror;
    active_ = host_;
    return transition;
}
Transition ControlEngine::return_to_host() {
    auto transition = release_held();
    mode_ = ControlMode::individual;
    active_ = host_;
    participants_.clear();
    return transition;
}
RoutingDecision ControlEngine::destinations(const InputEvent& event) const {
    if (mode_ == ControlMode::mirror) {
        if (std::holds_alternative<KeyEvent>(event))
            return {true, {participants_.begin(), participants_.end()}};
        return {true, {}};
    }
    if (active_ == host_) return {true, {}};
    return {false, {active_}};
}
RoutingDecision ControlEngine::route(const InputEvent& event) {
    const auto result = destinations(event);
    if (const auto* key = std::get_if<KeyEvent>(&event)) {
        const auto match = [&](const KeyEvent& held) {
            return key_identity(held) == key_identity(*key);
        };
        const auto it = std::find_if(held_keys_.begin(), held_keys_.end(), match);
        if (key->pressed && it == held_keys_.end()) held_keys_.push_back(*key);
        if (!key->pressed && it != held_keys_.end()) held_keys_.erase(it);
    } else if (const auto* button = std::get_if<MouseButtonEvent>(&event)) {
        if (button->pressed) held_buttons_.insert(button->button);
        else held_buttons_.erase(button->button);
    }
    return result;
}
Transition ControlEngine::release_held() {
    Transition transition;
    const auto add_release = [&](const InputEvent& event) {
        const auto targets = destinations(event);
        const auto append = [&](const std::string& id) {
            auto it = std::find_if(transition.releases.begin(), transition.releases.end(),
                [&](const ReleaseRequest& request) { return request.computer_id == id; });
            if (it == transition.releases.end()) {
                transition.releases.push_back({id, {event}});
            } else {
                it->events.push_back(event);
            }
        };
        if (targets.deliver_to_host) append(host_);
        for (const auto& id : targets.clients) append(id);
    };
    for (auto key : held_keys_) {
        key.pressed = false;
        add_release(key);
    }
    for (const auto button : held_buttons_)
        add_release(MouseButtonEvent{button, false});
    held_keys_.clear();
    held_buttons_.clear();
    return transition;
}
} // namespace sharekeys
