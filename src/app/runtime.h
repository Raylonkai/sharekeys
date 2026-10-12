#pragma once
#include "core/control_engine.h"
#include "core/held_input.h"
#include "core/monitor_layout.h"
#include "network/peer.h"
#include "platform/input_backend.h"
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <memory>

namespace sharekeys {
class Runtime final : public QObject {
    Q_OBJECT
public:
    explicit Runtime(std::unique_ptr<InputBackend> backend = {}, QObject* parent = nullptr);
    ~Runtime() override;
public slots:
    void start(QJsonObject config);
    void stop();
    void command(QString action, QString target = {});
    void configure_layout(QJsonArray positions);
    void set_participants(QStringList participants);
signals:
    void snapshot(QJsonObject state);
    void log_message(QString message);
private:
    void accept();
    void attach(Peer* peer, bool server);
    void message(Peer* peer, const QJsonObject& object);
    bool capture(const InputEvent& event);
    void movement(MouseMove event);
    void activate(const std::string& id, const CursorPosition* position = nullptr);
    void transition(const Transition& transition);
    void recover(const QString& reason);
    void release_client_input();
    bool inject(const InputEvent& event);
    bool send(const std::string& id, const QJsonObject& message);
    void rebuild_layout();
    void publish();
    void tick();
    void hotkey(Hotkey key);
    std::unique_ptr<InputBackend> backend_;
    ControlEngine engine_{"host"};
    HeldInput physical_, blocked_, injected_;
    QTcpServer* server_{};
    QTimer* timer_{};
    QMap<QString, QPointer<Peer>> peers_;
    QList<QPointer<Peer>> pending_;
    QPointer<Peer> upstream_;
    MonitorLayout layout_;
    std::vector<Monitor> local_monitors_;
    CursorPosition cursor_;
    QJsonArray saved_positions_;
    QMap<QString, CursorPosition> reported_cursors_;
    QStringList participants_;
    QJsonObject config_;
    bool running_{}, host_{}, recovering_{}, stopping_{};
    QString status_{"Stopped"};
    QElapsedTimer edge_cooldown_;
    int ticks_{};
};
}
