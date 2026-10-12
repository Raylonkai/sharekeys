#include "app/runtime.h"
#include "network/peer.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>
using namespace sharekeys;
namespace {
std::vector<Monitor> screen(const std::string& id) { return {{id, "main", {0, 0, 1920, 1080}, {0, 0, 1920, 1080}}}; }
class FakeBackend final : public InputBackend {
public:
    CaptureCallback callback;
    std::function<void(Hotkey)> hotkey;
    std::vector<InputEvent> injected;
    Point point{400, 300};
    bool remote{}, fail{}, desktop{true};
    bool available() const override { return true; }
    bool start_capture(CaptureCallback c, std::function<void(Hotkey)> h, std::function<void(std::string)>, std::string&) override { callback = std::move(c); hotkey = std::move(h); return true; }
    void stop_capture() noexcept override { callback = {}; remote = false; }
    bool inject(const InputEvent& event, std::string& error) override {
        if (fail) { error = "Test injection failure"; return false; }
        injected.push_back(event);
        if (auto p = std::get_if<MousePosition>(&event)) point = {p->x, p->y};
        return true;
    }
    void remote_control(bool value) override { remote = value; }
    Point cursor() const override { return point; }
    std::vector<Monitor> monitors(const std::string& id) const override { return screen(id); }
    bool desktop_available() const override { return desktop; }
};
}
class NetworkTests : public QObject {
    Q_OBJECT
private slots:
    void framing() {
        auto a = wire::frame({{"type", "a"}}), b = wire::frame({{"type", "b"}});
        QByteArray buffer = a.left(3); QVERIFY(wire::extract(buffer).empty());
        buffer += a.mid(3) + b; const auto frames = wire::extract(buffer);
        QCOMPARE(frames.size(), std::size_t(2)); QVERIFY(buffer.isEmpty());
        QByteArray bad(4, char(0xff)); QVERIFY_EXCEPTION_THROWN(wire::extract(bad), std::runtime_error);
        InputEvent event; QVERIFY(!wire::decode_event({{"kind", "key"}, {"scan", -2}}, event));
        for (const auto& source : std::vector<InputEvent>{KeyEvent{29, false, true, 17}, MousePosition{-100, 20}, MouseMove{2, -3}, MouseButtonEvent{MouseButton::extra2, false}, MouseScroll{120, -120}}) {
            const auto encoded = wire::encode_event(source); QVERIFY(wire::decode_event(encoded, event)); QCOMPARE(wire::encode_event(event), encoded);
        }
        QVERIFY(!wire::equal_mac(QByteArray(64, 'a'), QByteArray(64, 'b')));
    }
    void authenticated_transport() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        auto* socket = new QTcpSocket;
        Peer client(socket, false, "0123456789abcdef", "node", screen("node"));
        QSignalSpy client_ready(&client, &Peer::authenticated);
        socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        Peer host(server.nextPendingConnection(), true, "0123456789abcdef", "host", screen("host"));
        QSignalSpy host_ready(&host, &Peer::authenticated); QSignalSpy received(&client, &Peer::received);
        host.begin(); QTRY_COMPARE(client_ready.count(), 1); QTRY_COMPARE(host_ready.count(), 1);
        QCOMPARE(host.remote_id(), QString("node")); QVERIFY(host.send({{"type", "reset"}}));
        QTRY_COMPARE(received.count(), 1); QCOMPARE(received[0][0].toJsonObject().value("type").toString(), QString("reset"));
        QSignalSpy ended(&host, &Peer::ended);
        socket->write(wire::frame({{"type", "input"}, {"sequence", 1}, {"mac", QString(64, '0')}}));
        QTRY_COMPARE(ended.count(), 1); QVERIFY(!host.ready());
    }
    void wrong_key() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        auto* socket = new QTcpSocket; Peer client(socket, false, "wrong-key-1234567", "node", screen("node"));
        socket->connectToHost(QHostAddress::LocalHost, server.serverPort()); QTRY_VERIFY(server.hasPendingConnections());
        Peer host(server.nextPendingConnection(), true, "correct-key-12345", "host", screen("host"));
        QSignalSpy ended(&host, &Peer::ended); host.begin(); QTRY_COMPARE(ended.count(), 1); QVERIFY(!host.ready());
    }
    void handshake_timeout() {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY(server.hasPendingConnections());
        Peer peer(server.nextPendingConnection(), true, "0123456789abcdef", "host", screen("host"));
        QSignalSpy ended(&peer, &Peer::ended); peer.begin();
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 6500);
        QVERIFY(ended[0][0].toString().contains("timed out"));
    }
    void host_client_workflow() {
        QTcpServer probe; QVERIFY(probe.listen(QHostAddress::LocalHost)); const int port = probe.serverPort(); probe.close();
        auto host_backend = std::make_unique<FakeBackend>(); auto* host_input = host_backend.get();
        auto client_backend = std::make_unique<FakeBackend>(); auto* client_input = client_backend.get();
        QJsonObject host_state, client_state;
        Runtime host(std::move(host_backend)), client(std::move(client_backend));
        connect(&host, &Runtime::snapshot, this, [&](QJsonObject state) { host_state = state; });
        connect(&client, &Runtime::snapshot, this, [&](QJsonObject state) { client_state = state; });
        host.start({{"role", "host"}, {"port", port}, {"bind", "127.0.0.1"}, {"key", "0123456789abcdef"}});
        QVERIFY(host_input->callback);
        client.start({{"role", "client"}, {"id", "node"}, {"address", "127.0.0.1"}, {"port", port}, {"key", "0123456789abcdef"}});
        QTRY_COMPARE(host_state.value("clients").toArray().size(), 1);
        // Physical edge crossing, then the emergency hotkey, use the same runtime path.
        QTest::qWait(300); host_input->point = {1919, 500};
        host_input->callback(MouseMove{2, 0}); QVERIFY(host_input->remote);
        QTRY_VERIFY(!client_input->injected.empty());
        QCOMPARE(std::get<MousePosition>(client_input->injected.back()).x, 0);
        host_input->hotkey(Hotkey::home); QVERIFY(!host_input->remote);
        host.command("activate", "node"); QVERIFY(host_input->remote);
        const KeyEvent down{30, false, true, 65}, up{30, false, false, 65};
        QVERIFY(!host_input->callback(down));
        QTRY_VERIFY(client_input->injected.size() >= 2);
        QVERIFY(std::get<KeyEvent>(client_input->injected.back()).pressed);
        host.command("home"); QVERIFY(!host_input->remote);
        QTRY_VERIFY(std::holds_alternative<KeyEvent>(client_input->injected.back()) && !std::get<KeyEvent>(client_input->injected.back()).pressed);
        QVERIFY(!host_input->callback(down)); // Still-held key cannot leak into host.
        QVERIFY(!host_input->callback(up));
        host.set_participants({"node"}); host.command("mirror");
        QVERIFY(host_state.value("mirror").toBool()); QVERIFY(host_input->callback(down));
        QTRY_VERIFY(std::get<KeyEvent>(client_input->injected.back()).pressed);
        const auto count = client_input->injected.size();
        QVERIFY(host_input->callback(MouseButtonEvent{MouseButton::left, true}));
        QVERIFY(host_input->callback(MouseScroll{0, 120})); QVERIFY(host_input->callback(MouseMove{10, 10}));
        QTest::qWait(30); QCOMPARE(client_input->injected.size(), count);
        host.command("mirror"); QCOMPARE(host_state.value("active").toString(), QString("host"));
        QVERIFY(!host_state.value("mirror").toBool());
        host_input->callback(up); host_input->callback(MouseButtonEvent{MouseButton::left, false});
        host.command("activate", "node"); QVERIFY(!host_input->callback(down));
        QTRY_VERIFY(std::get<KeyEvent>(client_input->injected.back()).pressed);
        client.stop();
        QVERIFY(!std::get<KeyEvent>(client_input->injected.back()).pressed);
        QTRY_COMPARE(host_state.value("clients").toArray().size(), 0);
        QCOMPARE(host_state.value("active").toString(), QString("host")); QVERIFY(!host_input->remote);
        host.stop();
    }
};
QTEST_GUILESS_MAIN(NetworkTests)
#include "network_tests.moc"
