#include "core/monitor_layout.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace sharekeys {
namespace {
bool contains(const Rectangle& r, Point p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
Point clamp(const Rectangle& r, Point p) {
    return {std::clamp(p.x, r.x, r.x + r.width - 1), std::clamp(p.y, r.y, r.y + r.height - 1)};
}
}
bool MonitorLayout::validate(std::string& error) const {
    if (monitors.empty()) { error = "At least one monitor is required"; return false; }
    std::set<std::pair<std::string, std::string>> ids;
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        if (m.computer_id.empty() || m.monitor_id.empty() || !ids.insert({m.computer_id, m.monitor_id}).second) {
            error = "Monitor IDs must be unique per computer"; return false;
        }
        for (auto r : {m.desktop_bounds, m.layout_bounds}) {
            if (r.width < 1 || r.height < 1 || r.width > 32768 || r.height > 32768 ||
                std::abs(static_cast<long long>(r.x)) > 1000000 || std::abs(static_cast<long long>(r.y)) > 1000000) {
                error = "Monitor bounds are invalid"; return false;
            }
        }
        // One layout pixel equals one native desktop pixel; avoids ambiguous DPI mapping.
        if (m.desktop_bounds.width != m.layout_bounds.width || m.desktop_bounds.height != m.layout_bounds.height) {
            error = "Layout must preserve native monitor dimensions"; return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            auto a = m.layout_bounds; auto b = monitors[j].layout_bounds;
            if (a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y) {
                error = "Layout monitors must not overlap"; return false;
            }
        }
    }
    error.clear(); return true;
}
const Monitor* MonitorLayout::find(const std::string& computer, const std::string& monitor) const {
    for (const auto& m : monitors) if (m.computer_id == computer && m.monitor_id == monitor) return &m;
    return nullptr;
}
CursorPosition MonitorLayout::locate(const std::string& computer, Point point) const {
    const Monitor* closest = nullptr; double distance = std::numeric_limits<double>::max();
    for (const auto& m : monitors) {
        if (m.computer_id != computer) continue;
        if (contains(m.desktop_bounds, point)) return {computer, m.monitor_id, point};
        const auto p = clamp(m.desktop_bounds, point);
        const double d = std::hypot(double(p.x) - point.x, double(p.y) - point.y);
        if (d < distance) { distance = d; closest = &m; }
    }
    if (!closest) return {computer, {}, point};
    return {computer, closest->monitor_id, clamp(closest->desktop_bounds, point)};
}
CursorPosition MonitorLayout::move(const CursorPosition& current, int dx, int dy, bool cross_edges) const {
    dx = std::clamp(dx, -32768, 32768); dy = std::clamp(dy, -32768, 32768);
    const auto* source = find(current.computer_id, current.monitor_id);
    if (!source) return current;
    auto p = current.desktop_position;
    // Walk the segment, preventing high-speed movement from jumping across gaps or computers.
    const int steps = std::max(std::abs(std::clamp(dx, -32768, 32768)), std::abs(std::clamp(dy, -32768, 32768)));
    if (!steps) return current;
    Point world{source->layout_bounds.x + p.x - source->desktop_bounds.x,
                source->layout_bounds.y + p.y - source->desktop_bounds.y};
    Point previous_offset{};
    const Monitor* target = source;
    for (int step = 1; step <= steps; ++step) {
        const Point offset{int(std::lround(double(dx) * step / steps)), int(std::lround(double(dy) * step / steps))};
        const Point increment{offset.x - previous_offset.x, offset.y - previous_offset.y};
        previous_offset = offset;
        Point next{world.x + increment.x, world.y + increment.y};
        const auto at = [&](Point point) -> const Monitor* {
            for (const auto& monitor : monitors) {
                if ((!cross_edges && monitor.computer_id != current.computer_id) || !contains(monitor.layout_bounds, point)) continue;
                return &monitor;
            }
            return nullptr;
        };
        const Monitor* found = at(next);
        // Slide along a blocked screen edge instead of freezing both axes.
        if (!found) { next = {world.x, next.y}; found = at(next); }
        if (!found || (next.x == world.x && next.y == world.y)) {
            next = {world.x + increment.x, world.y};
            found = at(next);
        }
        if (!found) break;
        target = found; world = next;
        // One cross-computer switch per event; land just inside the destination edge.
        if (target->computer_id != current.computer_id) break;
    }
    return {target->computer_id, target->monitor_id,
            {target->desktop_bounds.x + world.x - target->layout_bounds.x,
             target->desktop_bounds.y + world.y - target->layout_bounds.y}};
}
} // namespace sharekeys
