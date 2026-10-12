#pragma once
#include "core/input_event.h"
#include <map>
#include <set>
#include <vector>

namespace sharekeys {
class HeldInput {
public:
    void observe(const InputEvent& event) {
        if (auto key = std::get_if<KeyEvent>(&event)) {
            if (key->pressed) keys_[key_identity(*key)] = *key;
            else keys_.erase(key_identity(*key));
        } else if (auto button = std::get_if<MouseButtonEvent>(&event)) {
            if (button->pressed) buttons_.insert(button->button);
            else buttons_.erase(button->button);
        }
    }
    std::vector<InputEvent> releases() const {
        std::vector<InputEvent> result;
        for (const auto& entry : keys_) {
            auto key = entry.second;
            key.pressed = false;
            result.push_back(key);
        }
        for (auto button : buttons_) result.push_back(MouseButtonEvent{button, false});
        return result;
    }
    bool contains(const InputEvent& event) const {
        if (auto key = std::get_if<KeyEvent>(&event)) return keys_.count(key_identity(*key)) != 0;
        if (auto button = std::get_if<MouseButtonEvent>(&event)) return buttons_.count(button->button) != 0;
        return false;
    }
    bool empty() const { return keys_.empty() && buttons_.empty(); }
    void clear() { keys_.clear(); buttons_.clear(); }
private:
    std::map<unsigned, KeyEvent> keys_;
    std::set<MouseButton> buttons_;
};
} // namespace sharekeys
