#pragma once
#include <cstdint>
#include <variant>

namespace sharekeys {
struct KeyEvent {
    std::uint16_t scan_code{};
    bool extended{};
    bool pressed{};
    std::uint16_t virtual_key{}; // Fallback for Pause and keys with no scan code.
};
struct MouseMove { int dx{}; int dy{}; };
struct MousePosition { int x{}; int y{}; };
enum class MouseButton { left, right, middle, extra1, extra2 };
struct MouseButtonEvent { MouseButton button{}; bool pressed{}; };
struct MouseScroll { int horizontal{}; int vertical{}; };
using InputEvent = std::variant<KeyEvent, MouseMove, MouseButtonEvent, MouseScroll, MousePosition>;
inline unsigned key_identity(const KeyEvent& key) {
    return key.scan_code ? (key.scan_code | (key.extended ? 0x10000U : 0U)) : (0x20000U | key.virtual_key);
}
} // namespace sharekeys
