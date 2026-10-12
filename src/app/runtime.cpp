#include "app/runtime.h"
#include <QHostInfo>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

namespace sharekeys {
Runtime::Runtime(std::unique_ptr<InputBackend> backend, QObject* parent)
    : QObject(parent), backend_(backend ? std::move(backend) : make_input_backend()) {}
Runtime::~Runtime() { stop(); }
void Runtime::start(QJsonObject config) {
    stop(); config_ = config; host_ = config.value("role").toString() == "host";
    const QString id = host_ ? "host" : config.value("id").toString();
    const QByteArray key = config.value("key").toString().toUtf8();
    const int port = config.value("port").toInt(24820);
    if (!backend_->available()) { status_ = "Live input sharing requires Windows"; publish(); return; }
    if (key.size() < 16 || key.size() > 256 || port < 1 || port > 65535 ||
        !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(id).hasMatch() || (!host_ && id == "host")) {
        status_ = "Check identity, port, and connection key (at least 16 characters)"; publish(); return;
    }
    if (!backend_->desktop_available()) { status_ = "An unlocked Windows desktop is required"; publish(); return; }
    local_monitors_ = backend_->monitors(id.toStdString());
    std::string error;
    if (!MonitorLayout{local_monitors_}.validate(error)) { status_ = QString::fromStdString(error); publish(); return; }
    saved_positions_ = config.value("layout").toArray();
    participants_ = config.value("participants").toVariant().toStringList();
    running_ = true; edge_cooldown_.start();
    timer_ = new QTimer(this); timer_->setInterval(200);
    connect(timer_, &QTimer::timeout, this, &Runtime::tick); timer_->start();
    if (host_) {
        server_ = new QTcpServer(this);
        server_->setMaxPendingConnections(16);
        connect(server_, &QTcpServer::newConnection, this, &Runtime::accept);
        QHostAddress bind;
        if (!bind.setAddress(config.value("bind").toString("0.0.0.0")) || !server_->listen(bind, quint16(port))) {
            const auto reason = "Cannot listen: " + server_->errorString(); stop(); status_ = reason; publish(); return;
        }
        if (!backend_->start_capture([this](const InputEvent& event) { return capture(event); },
            [this](Hotkey key) { hotkey(key); },
            [this](std::string reason) { recover(QString::fromStdString(reason)); }, error)) {
            stop(); status_ = QString::fromStdString(error); publish(); return;
        }
        status_ = QString("Listening on %1:%2").arg(bind.toString()).arg(server_->serverPort());
        rebuild_layout();
    } else {
        auto* socket = new QTcpSocket;
        auto* peer = new Peer(socket, false, key, id, local_monitors_, this);
        upstream_ = peer; attach(peer, false);
        status_ = "Connecting to " + config.value("address").toString();
        socket->connectToHost(config.value("address").toString(), quint16(port));
        layout_.monitors = local_monitors_;
    }
    emit log_message(status_); publish();
}
void Runtime::accept() {
    while (server_ && server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        if (peers_.size() + pending_.size() >= 16) { socket->abort(); socket->deleteLater(); continue; }
        auto* peer = new Peer(socket, true, config_.value("key").toString().toUtf8(), "host", local_monitors_, this);
        pending_.append(peer); attach(peer, true); peer->begin();
    }
}
void Runtime::attach(Peer* peer, bool server) {
    connect(peer, &Peer::authenticated, this, [this, peer, server] {
        if (server) {
            const auto id = peer->remote_id();
            if (peers_.contains(id)) { peer->close("Client identity already connected"); return; }
            pending_.removeAll(peer); peers_[id] = peer;
            engine_.connect_client(id.toStdString());
            // New clients are explicit mirror participants only after selection.
            recover("Client connected: " + id); rebuild_layout();
        } else status_ = "Connected to host";
        emit log_message("Authenticated " + peer->remote_id() + " at " + peer->address()); publish();
    });
    connect(peer, &Peer::received, this, [this, peer](const QJsonObject& message) { this->message(peer, message); });
    connect(peer, &Peer::ended, this, [this, peer, server](const QString& reason) {
        pending_.removeAll(peer);
        const auto id = peer->remote_id();
        if (server && peers_.value(id) == peer) {
            peers_.remove(id); reported_cursors_.remove(id);
            transition(engine_.disconnect_client(id.toStdString()));
            if (!stopping_) { recover("Client disconnected: " + id + " — " + reason); rebuild_layout(); }
        } else if (!server) {
            release_client_input(); upstream_.clear();
            if (!stopping_) { status_ = "Disconnected — " + reason; running_ = false; }
        }
        if (!stopping_) { emit log_message(reason); publish(); }
        peer->deleteLater();
    });
}
void Runtime::message(Peer* peer, const QJsonObject& object) {
    const auto type = object.value("type").toString();
    if (host_) {
        if (type == "fault") { recover("Client reported an input failure"); peer->close("Client input failure"); }
        else if (type == "cursor") {
            const auto id = peer->remote_id().toStdString();
            InputEvent event;
            if (!wire::decode_event(object.value("event").toObject(), event) || !std::holds_alternative<MousePosition>(event)) {
                peer->close("Invalid cursor update"); return;
            }
            // Keep confirmed cursor position separate from routing; delayed feedback must not rewind motion.
            const auto point = std::get<MousePosition>(event);
            reported_cursors_[peer->remote_id()] = layout_.locate(id, {point.x, point.y});
        } else peer->close("Unexpected client message");
        return;
    }
    if (type == "reset") { release_client_input(); return; }
    if (type != "input") { peer->close("Unexpected host message"); return; }
    InputEvent event;
    if (!wire::decode_event(object.value("event").toObject(), event)) { peer->close("Invalid input event"); return; }
    if (auto point = std::get_if<MousePosition>(&event)) {
        auto located = layout_.locate(config_.value("id").toString().toStdString(), {point->x, point->y});
        if (located.monitor_id.empty() || located.desktop_position.x != point->x || located.desktop_position.y != point->y) {
            peer->close("Cursor outside client monitors"); return;
        }
    }
    if (!backend_->desktop_available() || !inject(event)) {
        peer->send({{"type", "fault"}}); peer->close("Input injection failed; verify desktop and application permissions"); return;
    }
    injected_.observe(event);
}
bool Runtime::inject(const InputEvent& event) {
    std::string error;
    if (backend_->inject(event, error)) return true;
    emit log_message(QString::fromStdString(error)); return false;
}
bool Runtime::send(const std::string& id, const QJsonObject& object) {
    const auto peer = peers_.value(QString::fromStdString(id));
    return peer && peer->send(object);
}
void Runtime::transition(const Transition& change) {
    blocked_ = physical_;
    for (const auto& request : change.releases) {
        for (const auto& event : request.events) {
            if (request.computer_id == "host") inject(event);
            else send(request.computer_id, {{"type", "input"}, {"event", wire::encode_event(event)}});
        }
    }
}
void Runtime::recover(const QString& reason) {
    if (recovering_) return;
    recovering_ = true;
    backend_->remote_control(false);
    transition(engine_.return_to_host());
    const auto peers = peers_;
    for (const auto& peer : peers) if (peer) peer->send({{"type", "reset"}});
    cursor_ = layout_.locate("host", backend_->cursor());
    edge_cooldown_.restart();
    if (!reason.isEmpty()) { status_ = reason; emit log_message(reason); }
    recovering_ = false; publish();
}
void Runtime::release_client_input() {
    for (const auto& event : injected_.releases()) if (inject(event)) injected_.observe(event);
}
void Runtime::stop() {
    if (stopping_) return;
    stopping_ = true;
    if (host_) recover({});
    release_client_input(); backend_->stop_capture();
    if (server_) { server_->close(); delete server_; server_ = nullptr; }
    const auto peers = peers_; const auto pending = pending_;
    for (auto peer : peers) if (peer) peer->close("Session stopped");
    for (auto peer : pending) if (peer) peer->close("Session stopped");
    if (upstream_) upstream_->close("Session stopped");
    peers_.clear(); pending_.clear(); upstream_.clear();
    if (timer_) { timer_->stop(); delete timer_; timer_ = nullptr; }
    engine_ = ControlEngine("host"); physical_.clear(); blocked_.clear();
    layout_.monitors.clear(); local_monitors_.clear(); reported_cursors_.clear();
    running_ = false; status_ = "Stopped"; stopping_ = false; publish();
}
bool Runtime::capture(const InputEvent& event) {
    if (!running_ || !host_) return true;
    const bool blocked = blocked_.contains(event);
    physical_.observe(event);
    if (blocked) { blocked_.observe(event); return false; }
    if (auto move = std::get_if<MouseMove>(&event)) { movement(*move); return engine_.active_computer() == "host"; }
    const auto decision = engine_.route(event);
    for (const auto& id : decision.clients) {
        if (!send(id, {{"type", "input"}, {"event", wire::encode_event(event)}})) {
            recover("Input delivery failed; control returned to host"); return true;
        }
    }
    return decision.deliver_to_host;
}
void Runtime::movement(MouseMove event) {
    if (engine_.mode() == ControlMode::mirror) { cursor_ = layout_.locate("host", backend_->cursor()); return; }
    const bool local = engine_.active_computer() == "host";
    if (local) cursor_ = layout_.locate("host", backend_->cursor());
    if (cursor_.monitor_id.empty()) return;
    event.dx = std::clamp(event.dx, -4096, 4096); event.dy = std::clamp(event.dy, -4096, 4096);
    // Local movement is already applied by Windows. Only test outward movement
    // at its current edge; never double-apply raw movement on the host.
    int dx = event.dx, dy = event.dy;
    if (local) {
        const auto* m = layout_.find(cursor_.computer_id, cursor_.monitor_id);
        const auto b = m->desktop_bounds; const auto p = cursor_.desktop_position;
        dx = (event.dx < 0 && p.x <= b.x) ? -1 : (event.dx > 0 && p.x >= b.x + b.width - 1) ? 1 : 0;
        dy = (event.dy < 0 && p.y <= b.y) ? -1 : (event.dy > 0 && p.y >= b.y + b.height - 1) ? 1 : 0;
    }
    const bool edges = config_.value("edges").toBool(true) && edge_cooldown_.elapsed() > 250;
    const auto next = layout_.move(cursor_, dx, dy, edges);
    if (next.computer_id != engine_.active_computer()) { activate(next.computer_id, &next); return; }
    cursor_ = next;
    if (!local && !send(cursor_.computer_id, {{"type", "input"}, {"event", wire::encode_event(MousePosition{next.desktop_position.x, next.desktop_position.y})}}))
        recover("Mouse delivery failed");
}
void Runtime::activate(const std::string& id, const CursorPosition* position) {
    if (!running_ || !host_ || engine_.mode() == ControlMode::mirror) return;
    if (id == engine_.active_computer()) return;
    if (id != "host" && !peers_.contains(QString::fromStdString(id))) return;
    transition(engine_.activate(id));
    if (engine_.active_computer() != id) return; // Delivery failure may have recovered synchronously.
    if (position) cursor_ = *position;
    else {
        const auto it = std::find_if(layout_.monitors.begin(), layout_.monitors.end(), [&](const Monitor& m) { return m.computer_id == id; });
        if (it == layout_.monitors.end()) { recover("Destination monitor unavailable"); return; }
        const auto b = it->desktop_bounds;
        cursor_ = {id, it->monitor_id, {b.x + b.width / 2, b.y + b.height / 2}};
    }
    backend_->remote_control(id != "host"); edge_cooldown_.restart();
    const InputEvent event = MousePosition{cursor_.desktop_position.x, cursor_.desktop_position.y};
    if (id == "host") { if (!inject(event)) recover("Cannot restore host cursor"); }
    else if (!send(id, {{"type", "input"}, {"event", wire::encode_event(event)}})) recover("Cannot activate client");
    publish();
}
void Runtime::hotkey(Hotkey key) {
    command(key == Hotkey::home ? "home" : key == Hotkey::next ? "next" : "mirror");
}
void Runtime::command(QString action, QString target) {
    if (!running_ || !host_) return;
    if (action == "home") recover("Control returned to host");
    else if (action == "activate") activate(target.toStdString());
    else if (action == "next") {
        if (engine_.mode() == ControlMode::mirror) return;
        QStringList ids{"host"}; ids.append(peers_.keys());
        const auto index = ids.indexOf(QString::fromStdString(engine_.active_computer()));
        activate(ids[(index + 1) % ids.size()].toStdString());
    } else if (action == "mirror") {
        if (engine_.mode() == ControlMode::mirror) { recover("Mirror mode ended; control returned to host"); return; }
        std::set<std::string> selected;
        for (const auto& id : participants_) if (peers_.contains(id)) selected.insert(id.toStdString());
        if (selected.empty()) { emit log_message("Select at least one connected mirror participant"); return; }
        backend_->remote_control(false);
        transition(engine_.enable_mirror(selected));
        if (engine_.mode() != ControlMode::mirror) return;
        cursor_ = layout_.locate("host", backend_->cursor());
        status_ = "Mirroring keyboard; mouse stays on host"; publish();
    }
}
void Runtime::set_participants(QStringList participants) {
    if (engine_.mode() == ControlMode::mirror) recover("Mirror participants changed; control returned to host");
    participants_ = std::move(participants); publish();
}
void Runtime::rebuild_layout() {
    layout_.monitors = local_monitors_;
    int right = 0;
    for (const auto& m : layout_.monitors) right = std::max(right, m.layout_bounds.x + m.layout_bounds.width);
    for (auto peer : peers_) {
        if (!peer) continue;
        auto monitors = peer->remote_monitors();
        int left = std::numeric_limits<int>::max(), top = std::numeric_limits<int>::max(), width = 0;
        for (auto m : monitors) { left = std::min(left, m.desktop_bounds.x); top = std::min(top, m.desktop_bounds.y); }
        for (auto& m : monitors) {
            m.layout_bounds.x = right + m.desktop_bounds.x - left; m.layout_bounds.y = m.desktop_bounds.y - top;
            width = std::max(width, m.layout_bounds.x + m.layout_bounds.width - right);
            layout_.monitors.push_back(m);
        }
        right += width;
    }
    auto candidate = layout_;
    for (auto& monitor : candidate.monitors) for (const auto& value : saved_positions_) {
        auto object = value.toObject();
        if (object.value("computer").toString().toStdString() == monitor.computer_id && object.value("monitor").toString().toStdString() == monitor.monitor_id) {
            monitor.layout_bounds.x = object.value("x").toInt(); monitor.layout_bounds.y = object.value("y").toInt();
        }
    }
    std::string error;
    if (candidate.validate(error)) layout_ = std::move(candidate);
    else emit log_message("Saved layout does not fit connected monitors; using automatic arrangement");
    cursor_ = layout_.locate("host", backend_->cursor()); publish();
}
void Runtime::configure_layout(QJsonArray positions) {
    if (!running_ || !host_) return;
    auto candidate = layout_;
    for (auto& monitor : candidate.monitors) for (const auto& value : positions) {
        auto object = value.toObject();
        if (object.value("computer").toString().toStdString() == monitor.computer_id && object.value("monitor").toString().toStdString() == monitor.monitor_id) {
            monitor.layout_bounds.x = object.value("x").toInt(); monitor.layout_bounds.y = object.value("y").toInt();
        }
    }
    std::string error;
    if (!candidate.validate(error)) { emit log_message(QString::fromStdString(error)); return; }
    recover("Monitor layout updated"); layout_ = std::move(candidate); saved_positions_ = positions; publish();
}
void Runtime::tick() {
    if (!running_) {
        if (!injected_.empty() && backend_->desktop_available()) release_client_input();
        return;
    }
    if (!backend_->desktop_available()) {
        if (host_) recover("Desktop locked or unavailable; control returned to host");
        else { release_client_input(); if (upstream_) upstream_->close("Desktop unavailable"); }
        return;
    }
    if (++ticks_ % 5 == 0) {
        const auto current = backend_->monitors(host_ ? "host" : config_.value("id").toString().toStdString());
        if (wire::encode_monitors(current) != wire::encode_monitors(local_monitors_)) {
            stop(); status_ = "Monitor configuration changed. Start again to refresh the layout."; publish(); return;
        }
    }
    if (host_ && engine_.active_computer() == "host") cursor_ = layout_.locate("host", backend_->cursor());
    if (!host_ && upstream_ && upstream_->ready()) {
        const auto point = backend_->cursor(); cursor_ = layout_.locate(config_.value("id").toString().toStdString(), point);
        upstream_->send({{"type", "cursor"}, {"event", wire::encode_event(MousePosition{point.x, point.y})}});
    }
    publish();
}
void Runtime::publish() {
    auto displayed_cursor = cursor_;
    const auto active = QString::fromStdString(engine_.active_computer());
    if (host_ && active != "host" && reported_cursors_.contains(active)) displayed_cursor = reported_cursors_.value(active);
    QJsonArray clients, monitors;
    for (auto it = peers_.begin(); it != peers_.end(); ++it) if (it.value()) clients.append(QJsonObject{
        {"id", it.key()}, {"address", it.value()->address()}, {"mirror", participants_.contains(it.key())},
        {"active", engine_.active_computer() == it.key().toStdString()}});
    for (const auto& m : layout_.monitors) monitors.append(QJsonObject{{"computer", QString::fromStdString(m.computer_id)},
        {"monitor", QString::fromStdString(m.monitor_id)}, {"x", m.layout_bounds.x}, {"y", m.layout_bounds.y},
        {"w", m.layout_bounds.width}, {"h", m.layout_bounds.height}});
    emit snapshot({{"running", running_}, {"host", host_}, {"status", status_}, {"available", backend_->available()},
        {"active", QString::fromStdString(engine_.active_computer())}, {"mirror", engine_.mode() == ControlMode::mirror},
        {"clients", clients}, {"monitors", monitors}, {"monitor", QString::fromStdString(displayed_cursor.monitor_id)},
        {"cursor_x", displayed_cursor.desktop_position.x}, {"cursor_y", displayed_cursor.desktop_position.y}});
}
}
