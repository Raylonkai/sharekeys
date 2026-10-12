#pragma once
#include "network/protocol.h"
#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QElapsedTimer>

namespace sharekeys {
class Peer final : public QObject {
    Q_OBJECT
public:
    Peer(QTcpSocket* socket, bool server, QByteArray key, QString local_id,
         std::vector<Monitor> monitors, QObject* parent = nullptr);
    void begin();
    bool send(const QJsonObject& message);
    void close(const QString& reason);
    bool ready() const { return ready_; }
    QString remote_id() const { return remote_id_; }
    std::vector<Monitor> remote_monitors() const { return remote_monitors_; }
    QString address() const { return socket_->peerAddress().toString(); }
    qint64 queued_bytes() const { return socket_->bytesToWrite(); }
signals:
    void authenticated();
    void received(QJsonObject message);
    void ended(QString reason);
private:
    bool write(const QJsonObject& message);
    void read();
    void process(QJsonObject message);
    QByteArray proof(const char* role) const;
    QTcpSocket* socket_;
    bool server_{};
    bool ready_{};
    bool ended_{};
    bool began_{};
    QByteArray key_, nonce_, client_nonce_, buffer_;
    QString local_id_, remote_id_;
    std::vector<Monitor> local_monitors_, remote_monitors_;
    QTimer timer_;
    QElapsedTimer last_receive_;
    QElapsedTimer pending_write_;
    quint64 outgoing_{}, incoming_{};
    int phase_{};
};
}
