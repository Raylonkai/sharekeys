#include "network/peer.h"
#include <QJsonDocument>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace sharekeys {
namespace {
QByteArray random_nonce() {
    QByteArray bytes;
    for (int i = 0; i < 8; ++i) bytes += QByteArray::number(QRandomGenerator::system()->generate(), 16).rightJustified(8, '0');
    return bytes;
}
bool valid_nonce(const QByteArray& nonce) { return QRegularExpression("^[0-9a-f]{64}$").match(QString::fromLatin1(nonce)).hasMatch(); }
bool valid_id(const QString& id) { return QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(id).hasMatch() && id != "host"; }
}
Peer::Peer(QTcpSocket* socket, bool server, QByteArray key, QString id, std::vector<Monitor> monitors, QObject* parent)
    : QObject(parent), socket_(socket), server_(server), key_(std::move(key)), local_id_(std::move(id)), local_monitors_(std::move(monitors)) {
    socket_->setParent(this);
    socket_->setReadBufferSize(wire::max_frame * 4);
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket_, &QTcpSocket::readyRead, this, &Peer::read);
    connect(socket_, &QTcpSocket::connected, this, &Peer::begin);
    connect(socket_, &QTcpSocket::disconnected, this, [this] { close("Connection closed"); });
    connect(socket_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { close(socket_->errorString()); });
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (socket_->bytesToWrite() && pending_write_.isValid() && pending_write_.elapsed() > 1500) { close("Input delivery stalled"); return; }
        if (last_receive_.elapsed() > (ready_ ? 3000 : 5000)) { close("Connection timed out"); return; }
        if (ready_) send({{"type", "ping"}});
    });
    last_receive_.start(); timer_.start();
}
void Peer::begin() {
    if (began_ || ended_) return;
    began_ = true;
    if (server_) { nonce_ = random_nonce(); phase_ = 1; write({{"type", "challenge"}, {"version", wire::version}, {"nonce", QString::fromLatin1(nonce_)}}); }
    if (socket_->bytesAvailable()) read();
}
QByteArray Peer::proof(const char* role) const {
    return wire::mac(key_, QByteArray(role) + ':' + nonce_ + ':' + client_nonce_ + ':' + (server_ ? remote_id_ : local_id_).toUtf8());
}
bool Peer::write(const QJsonObject& object) {
    if (ended_) return false;
    if (socket_->bytesToWrite() > 262144) { close("Network is too slow; pending input discarded"); return false; }
    try {
        const auto bytes = wire::frame(object);
        if (!socket_->bytesToWrite()) pending_write_.restart();
        if (socket_->write(bytes) != bytes.size()) { close("Network write failed"); return false; }
        return true;
    } catch (const std::exception& error) { close(QString::fromUtf8(error.what())); return false; }
}
bool Peer::send(const QJsonObject& message) {
    if (!ready_ || ended_) return false;
    auto object = message;
    object["sequence"] = double(++outgoing_);
    object["mac"] = QString::fromLatin1(wire::mac(key_, (server_ ? QByteArray("host:") : QByteArray("client:")) + nonce_ + client_nonce_ + QJsonDocument(object).toJson(QJsonDocument::Compact)));
    return write(object);
}
void Peer::read() {
    if (ended_) return;
    try {
        buffer_ += socket_->readAll();
        if (buffer_.size() > wire::max_frame * 4) { close("Receive buffer limit exceeded"); return; }
        for (auto object : wire::extract(buffer_)) { process(object); if (ended_) break; }
    } catch (const std::exception& error) { close(QString::fromUtf8(error.what())); }
}
void Peer::process(QJsonObject object) {
    const auto type = object.value("type").toString();
    if (!ready_) {
        if (!server_ && phase_ == 0 && type == "challenge" && object.value("version").toInt() == wire::version) {
            nonce_ = object.value("nonce").toString().toLatin1();
            if (!valid_nonce(nonce_)) { close("Invalid challenge"); return; }
            client_nonce_ = random_nonce(); phase_ = 1;
            write({{"type", "hello"}, {"id", local_id_}, {"nonce", QString::fromLatin1(client_nonce_)},
                   {"proof", QString::fromLatin1(proof("client"))}, {"monitors", wire::encode_monitors(local_monitors_)},
                   {"metadata_mac", QString::fromLatin1(wire::mac(key_, proof("client") + QJsonDocument(wire::encode_monitors(local_monitors_)).toJson(QJsonDocument::Compact)))}});
        } else if (server_ && phase_ == 1 && type == "hello") {
            remote_id_ = object.value("id").toString(); client_nonce_ = object.value("nonce").toString().toLatin1();
            if (!valid_id(remote_id_) || !valid_nonce(client_nonce_) || !wire::equal_mac(proof("client"), object.value("proof").toString().toLatin1()) ||
                !wire::decode_monitors(object.value("monitors").toArray(), remote_id_.toStdString(), remote_monitors_)) {
                close("Pairing failed: check connection key and client identity"); return;
            }
            // Bind monitor metadata to the authenticated transcript too.
            const auto metadata = QJsonDocument(object.value("monitors").toArray()).toJson(QJsonDocument::Compact);
            if (!wire::equal_mac(wire::mac(key_, proof("client") + metadata), object.value("metadata_mac").toString().toLatin1())) {
                close("Monitor authentication failed"); return;
            }
            write({{"type", "welcome"}, {"proof", QString::fromLatin1(proof("host"))}});
            ready_ = true; last_receive_.restart(); emit authenticated();
        } else if (!server_ && phase_ == 1 && type == "welcome" && wire::equal_mac(proof("host"), object.value("proof").toString().toLatin1())) {
            ready_ = true; remote_id_ = "host"; last_receive_.restart(); emit authenticated();
        } else close("Protocol or pairing mismatch");
        return;
    }
    const auto tag = object.take("mac").toString().toLatin1();
    const auto expected = wire::mac(key_, (server_ ? QByteArray("client:") : QByteArray("host:")) + nonce_ + client_nonce_ + QJsonDocument(object).toJson(QJsonDocument::Compact));
    const double sequence = object.value("sequence").toDouble(-1);
    if (sequence != double(incoming_ + 1) || !wire::equal_mac(tag, expected)) { close("Invalid message signature or sequence"); return; }
    ++incoming_; last_receive_.restart();
    if (type == "ping") return;
    emit received(object);
}
void Peer::close(const QString& reason) {
    if (ended_) return;
    ended_ = true; ready_ = false; timer_.stop(); socket_->abort(); emit ended(reason);
}
}
