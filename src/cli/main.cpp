#include "app/runtime.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <atomic>
#include <csignal>

namespace {
std::atomic_bool interrupted{false};
void handle_signal(int) { interrupted.store(true); }

QString read_key_file(const QString& path, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("Cannot read key file %1: %2").arg(path, file.errorString());
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("sharekeys-node");
    app.setApplicationVersion("0.2.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Headless ShareKeys host/client node (live input requires Windows)");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption host("host", "Run as the keyboard/mouse host.");
    const QCommandLineOption client("client", "Run as a client receiving input.");
    const QCommandLineOption name({"n", "name"}, "Unique client name (required for clients).", "name");
    const QCommandLineOption address({"a", "address"}, "Host address (required for clients).", "address");
    const QCommandLineOption bind({"b", "bind"}, "Host listen address.", "address", "0.0.0.0");
    const QCommandLineOption port({"p", "port"}, "TCP port.", "port", "24820");
    const QCommandLineOption key({"k", "key"}, "Shared connection key (16-256 characters).", "key");
    const QCommandLineOption key_file("key-file", "Read the shared connection key from a text file.", "path");
    const QCommandLineOption participant({"m", "mirror-participant"},
        "Client selected for Ctrl+Alt+M mirroring; repeat for multiple clients.", "name");
    const QCommandLineOption no_edges("no-edge-switch", "Disable switching at touching monitor edges.");
    parser.addOptions({host, client, name, address, bind, port, key, key_file, participant, no_edges});
    parser.process(app);

    QTextStream out(stdout), err(stderr);
    const bool is_host = parser.isSet(host);
    if (is_host == parser.isSet(client)) {
        err << "Choose exactly one of --host or --client.\n";
        return 2;
    }
    if (parser.isSet(key) && parser.isSet(key_file)) {
        err << "Use either --key or --key-file, not both.\n";
        return 2;
    }
    QString shared_key = parser.value(key);
    if (parser.isSet(key_file)) {
        QString error;
        shared_key = read_key_file(parser.value(key_file), error);
        if (!error.isEmpty()) { err << error << '\n'; return 2; }
    }
    if (shared_key.size() < 16 || shared_key.size() > 256) {
        err << "The connection key must contain 16-256 characters.\n";
        return 2;
    }
    bool port_ok = false;
    const int port_number = parser.value(port).toInt(&port_ok);
    if (!port_ok || port_number < 1 || port_number > 65535) {
        err << "Port must be an integer from 1 to 65535.\n";
        return 2;
    }
    if (!is_host && (parser.value(name).isEmpty() || parser.value(address).isEmpty())) {
        err << "Clients require --name and --address.\n";
        return 2;
    }

    QJsonObject config{{"role", is_host ? "host" : "client"},
        {"id", is_host ? "host" : parser.value(name)}, {"address", parser.value(address)},
        {"bind", parser.value(bind)}, {"port", port_number}, {"key", shared_key},
        {"edges", !parser.isSet(no_edges)},
        {"participants", QJsonArray::fromStringList(parser.values(participant))}};

    sharekeys::Runtime runtime;
    bool ever_running = false;
    bool currently_running = false;
    QObject::connect(&runtime, &sharekeys::Runtime::log_message, &app,
        [&out](const QString& message) { out << message << '\n'; out.flush(); });
    QObject::connect(&runtime, &sharekeys::Runtime::snapshot, &app,
        [&out, &app, &ever_running, &currently_running](const QJsonObject& state) {
            out << QJsonDocument(state).toJson(QJsonDocument::Compact) << '\n';
            out.flush();
            currently_running = state.value("running").toBool();
            ever_running = ever_running || currently_running;
            if (ever_running && !currently_running && !interrupted.load())
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
        });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtime, &sharekeys::Runtime::stop);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    QTimer signal_timer;
    signal_timer.setInterval(100);
    QObject::connect(&signal_timer, &QTimer::timeout, &app, [&app] {
        if (interrupted.load()) app.quit();
    });
    signal_timer.start();

    out << (is_host ? "Starting headless Host" : "Starting headless Client")
        << " on port " << port_number << ". Press Ctrl+C to stop.\n";
    out.flush();
    QTimer::singleShot(0, &runtime, [&runtime, &app, &currently_running, config] {
        runtime.start(config);
        if (!currently_running)
            QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
    });
    return app.exec();
}
