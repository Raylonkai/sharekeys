#include "network/protocol.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageAuthenticationCode>
#include <QtEndian>
#include <cmath>
#include <stdexcept>

namespace sharekeys::wire {
namespace {
bool integer(const QJsonObject& o, const char* name, int low, int high, int& value) {
    auto v = o.value(QLatin1String(name)); const double d = v.toDouble(double(high) + 1);
    if (!v.isDouble() || d < low || d > high || d != std::floor(d)) return false;
    value = int(d); return true;
}
}
QJsonObject encode_event(const InputEvent& event) {
    if (auto k = std::get_if<KeyEvent>(&event)) return {{"kind", "key"}, {"scan", k->scan_code}, {"extended", k->extended}, {"down", k->pressed}, {"vk", k->virtual_key}};
    if (auto p = std::get_if<MousePosition>(&event)) return {{"kind", "position"}, {"x", p->x}, {"y", p->y}};
    if (auto m = std::get_if<MouseMove>(&event)) return {{"kind", "move"}, {"x", m->dx}, {"y", m->dy}};
    if (auto b = std::get_if<MouseButtonEvent>(&event)) return {{"kind", "button"}, {"button", int(b->button)}, {"down", b->pressed}};
    auto s = std::get<MouseScroll>(event); return {{"kind", "scroll"}, {"x", s.horizontal}, {"y", s.vertical}};
}
bool decode_event(const QJsonObject& o, InputEvent& event) {
    const auto kind = o.value("kind").toString(); int a{}, b{};
    if (kind == "key") {
        if (!integer(o, "scan", 0, 255, a) || !integer(o, "vk", 0, 255, b) || (!a && !b) ||
            !o.value("extended").isBool() || !o.value("down").isBool()) return false;
        event = KeyEvent{std::uint16_t(a), o.value("extended").toBool(), o.value("down").toBool(), std::uint16_t(b)};
    } else if (kind == "button") {
        if (!integer(o, "button", 0, 4, a) || !o.value("down").isBool()) return false;
        event = MouseButtonEvent{MouseButton(a), o.value("down").toBool()};
    } else {
        const int limit = kind == "position" ? 1000000 : 32768;
        if (!integer(o, "x", -limit, limit, a) || !integer(o, "y", -limit, limit, b)) return false;
        if (kind == "position") event = MousePosition{a, b};
        else if (kind == "move") event = MouseMove{a, b};
        else if (kind == "scroll") event = MouseScroll{a, b};
        else return false;
    }
    return true;
}
QJsonArray encode_monitors(const std::vector<Monitor>& monitors) {
    QJsonArray result;
    for (const auto& m : monitors) result.append(QJsonObject{{"id", QString::fromStdString(m.monitor_id)},
        {"x", m.desktop_bounds.x}, {"y", m.desktop_bounds.y}, {"w", m.desktop_bounds.width}, {"h", m.desktop_bounds.height}});
    return result;
}
bool decode_monitors(const QJsonArray& array, const std::string& id, std::vector<Monitor>& result) {
    if (array.isEmpty() || array.size() > 32) return false;
    std::vector<Monitor> list;
    for (const auto& value : array) {
        if (!value.isObject()) return false;
        auto o = value.toObject(); int x{}, y{}, w{}, h{};
        const auto name = o.value("id").toString();
        if (name.isEmpty() || name.size() > 128 || !integer(o, "x", -100000, 100000, x) || !integer(o, "y", -100000, 100000, y) ||
            !integer(o, "w", 1, 32768, w) || !integer(o, "h", 1, 32768, h)) return false;
        Rectangle bounds{x, y, w, h}; list.push_back({id, name.toStdString(), bounds, bounds});
    }
    std::string error;
    if (!MonitorLayout{list}.validate(error)) return false;
    result = std::move(list); return true;
}
QByteArray frame(const QJsonObject& object) {
    auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > max_frame) throw std::runtime_error("Frame exceeds size limit");
    QByteArray result(4, '\0'); qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar*>(result.data()));
    result.append(payload); return result;
}
std::vector<QJsonObject> extract(QByteArray& buffer) {
    std::vector<QJsonObject> result;
    while (buffer.size() >= 4) {
        const auto size = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(buffer.constData()));
        if (!size || size > max_frame) throw std::runtime_error("Invalid frame length");
        if (buffer.size() < int(size + 4)) break;
        QJsonParseError error{}; const auto doc = QJsonDocument::fromJson(buffer.mid(4, size), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid JSON frame");
        result.push_back(doc.object()); buffer.remove(0, size + 4);
    }
    return result;
}
QByteArray mac(const QByteArray& key, const QByteArray& data) { return QMessageAuthenticationCode::hash(data, key, QCryptographicHash::Sha256).toHex(); }
bool equal_mac(const QByteArray& a, const QByteArray& b) {
    if (a.size() != 64 || b.size() != 64) return false;
    unsigned char diff = 0;
    for (int i = 0; i < 64; ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}
}
