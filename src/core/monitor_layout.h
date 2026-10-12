#pragma once
#include <string>
#include <vector>

namespace sharekeys {
struct Point { int x{}; int y{}; };
struct Rectangle { int x{}; int y{}; int width{}; int height{}; };
struct Monitor {
    std::string computer_id;
    std::string monitor_id;
    Rectangle desktop_bounds;
    Rectangle layout_bounds;
};
struct CursorPosition {
    std::string computer_id;
    std::string monitor_id;
    Point desktop_position;
};
struct MonitorLayout {
    std::vector<Monitor> monitors;
    bool validate(std::string& error) const;
    const Monitor* find(const std::string& computer, const std::string& monitor) const;
    CursorPosition locate(const std::string& computer, Point point) const;
    CursorPosition move(const CursorPosition& current, int dx, int dy, bool cross_edges) const;
};
} // namespace sharekeys
