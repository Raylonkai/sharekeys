#pragma once
#include "core/input_event.h"
#include "core/monitor_layout.h"
#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>

namespace sharekeys::wire {
constexpr int version = 1;
constexpr int max_frame = 65536;
QJsonObject encode_event(const InputEvent& event);
bool decode_event(const QJsonObject& object, InputEvent& event);
QJsonArray encode_monitors(const std::vector<Monitor>& monitors);
bool decode_monitors(const QJsonArray& array, const std::string& id, std::vector<Monitor>& monitors);
QByteArray frame(const QJsonObject& object);
// Returns complete frames; retains a fragmented trailing frame. Throws on invalid input.
std::vector<QJsonObject> extract(QByteArray& buffer);
QByteArray mac(const QByteArray& key, const QByteArray& data);
bool equal_mac(const QByteArray& a, const QByteArray& b);
}
