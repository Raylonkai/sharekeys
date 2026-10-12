#include "core/monitor_layout.h"
#include <iostream>
#include <stdexcept>
using namespace sharekeys;
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main() {
    try {
        MonitorLayout layout{{{"host", "left", {-1920, 0, 1920, 1080}, {-1920, 0, 1920, 1080}},
                              {"host", "main", {0, 0, 1920, 1080}, {0, 0, 1920, 1080}},
                              {"client", "main", {-1280, 0, 1280, 1024}, {1920, 0, 1280, 1024}}}};
        std::string error; check(layout.validate(error), "Valid mixed-resolution layout");
        auto cursor = layout.locate("host", {-1, 100}); check(cursor.monitor_id == "left", "Negative desktop coordinates");
        cursor = layout.move(cursor, 2, 0, true); check(cursor.monitor_id == "main", "Same-computer monitor crossing");
        cursor = layout.locate("host", {1919, 500});
        auto blocked = layout.move(cursor, 20, 0, false); check(blocked.computer_id == "host", "Disabled edges");
        auto crossed = layout.move(cursor, 20, 0, true);
        check(crossed.computer_id == "client" && crossed.desktop_position.x == -1280, "Native destination coordinates");
        auto back = layout.move(crossed, -3, 0, true); check(back.computer_id == "host", "Return crossing");
        cursor = layout.locate("host", {1919, 1050}); check(layout.move(cursor, 10, 0, true).computer_id == "host", "Non-overlapping edge span");
        layout.monitors[2].layout_bounds.x += 10;
        cursor = layout.locate("host", {1919, 500}); check(layout.move(cursor, 50, 0, true).computer_id == "host", "Cannot jump gaps");
        check(layout.move(cursor, 50, 50, true).computer_id == "host", "Diagonal movement cannot jump gaps");
        auto slid = layout.move(cursor, 20, 20, true); check(slid.desktop_position.y > cursor.desktop_position.y, "Slide along blocked edge");
        layout.monitors[2].layout_bounds.x = 100;
        check(!layout.validate(error), "Overlapping layouts rejected");
        std::cout << "Layout checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
