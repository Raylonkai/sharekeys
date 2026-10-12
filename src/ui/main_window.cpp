#include "ui/main_window.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsTextItem>
#include <QGroupBox>
#include <QHeaderView>
#include <QHostInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

namespace sharekeys {
namespace {
QTableWidgetItem* cell(const QString& text, bool editable = false) {
    auto* item = new QTableWidgetItem(text);
    if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
QString new_key() { return QUuid::createUuid().toString(QUuid::Id128); }
}
MainWindow::MainWindow(bool demo) : demo_(demo) {
    setWindowTitle(demo ? "ShareKeys — interface demo" : "ShareKeys"); resize(1160, 860);
    auto* root = new QWidget; auto* page = new QVBoxLayout(root); page->setContentsMargins(24, 20, 24, 20); page->setSpacing(16);
    auto* title = new QLabel("ShareKeys"); title->setObjectName("title");
    auto* subtitle = new QLabel("ONE DESK. MULTIPLE COMPUTERS."); subtitle->setObjectName("subtitle");
    page->addWidget(title); page->addWidget(subtitle);
    auto* summary = new QHBoxLayout;
    mode_ = new QLabel("INDIVIDUAL CONTROL"); active_ = new QLabel("Active computer: host"); cursor_ = new QLabel("Cursor: —");
    for (auto* label : {mode_, active_, cursor_}) { label->setObjectName("metric"); summary->addWidget(label, 1); }
    page->addLayout(summary);
    auto* connection = new QGroupBox("Connection"); auto* connection_layout = new QHBoxLayout(connection);
    auto* form = new QFormLayout; auto* form2 = new QFormLayout;
    role_ = new QComboBox; role_->addItems({"Host", "Client"}); role_->setCurrentIndex(settings_.value("role", 0).toInt());
    identity_ = new QLineEdit(settings_.value("id", QHostInfo::localHostName().replace(QRegularExpression("[^A-Za-z0-9_-]"), "_").left(64)).toString());
    address_ = new QLineEdit(settings_.value("address", "").toString()); address_->setPlaceholderText("Host IP address or hostname");
    bind_ = new QLineEdit(settings_.value("bind", "0.0.0.0").toString());
    port_ = new QSpinBox; port_->setRange(1, 65535); port_->setValue(settings_.value("port", 24820).toInt());
    key_ = new QLineEdit; key_->setEchoMode(QLineEdit::Password); key_->setPlaceholderText("Paste the host's connection key");
    if (role_->currentIndex() == 0) key_->setText(new_key());
    auto* key_row = new QHBoxLayout; key_row->addWidget(key_);
    auto* copy = new QPushButton("Copy"); auto* generate = new QPushButton("New key"); key_row->addWidget(copy); key_row->addWidget(generate);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(key_->text()); });
    connect(generate, &QPushButton::clicked, this, [this] { if (!state_.value("running").toBool()) key_->setText(new_key()); });
    form->addRow("This computer's role", role_); form->addRow("Client name", identity_); form->addRow("Host address", address_);
    form2->addRow("Host listen address", bind_); form2->addRow("Port", port_); form2->addRow("Connection key", key_row);
    connection_layout->addLayout(form, 1); connection_layout->addSpacing(20); connection_layout->addLayout(form2, 1);
    page->addWidget(connection);
    auto* instructions = new QLabel("On the host, start listening and copy the key. On each client, enter the host address and key, then connect. Keys are not saved.");
    instructions->setWordWrap(true); instructions->setObjectName("muted"); page->addWidget(instructions);
    auto* controls = new QHBoxLayout;
    start_ = new QPushButton("Start host"); start_->setObjectName("primary"); stop_ = new QPushButton("Stop");
    home_ = new QPushButton("Return to host"); next_ = new QPushButton("Next computer"); mirror_ = new QPushButton("Start keyboard mirror");
    for (auto* button : {start_, stop_, home_, next_, mirror_}) controls->addWidget(button);
    controls->addStretch(); page->addLayout(controls);
    connect(start_, &QPushButton::clicked, this, &MainWindow::start);
    connect(stop_, &QPushButton::clicked, this, [this] { if (demo_) { state_["running"] = false; update_state(state_); } else QMetaObject::invokeMethod(runtime_, "stop", Qt::QueuedConnection); });
    connect(home_, &QPushButton::clicked, this, [this] { action("home"); });
    connect(next_, &QPushButton::clicked, this, [this] { action("next"); });
    connect(mirror_, &QPushButton::clicked, this, [this] { action("mirror"); });
    status_ = new QLabel("Stopped"); status_->setObjectName("status"); status_->setWordWrap(true); page->addWidget(status_);
    auto* tabs = new QTabWidget;
    auto* desk = new QWidget; auto* desk_layout = new QVBoxLayout(desk);
    auto* hint = new QLabel("Keyboard mirror includes the host and checked clients. Mouse input always stays on the host while mirroring."); hint->setWordWrap(true); desk_layout->addWidget(hint);
    clients_ = new QTableWidget(0, 4); clients_->setHorizontalHeaderLabels({"Mirror", "Computer", "Address", "Control"});
    clients_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); clients_->verticalHeader()->hide(); clients_->setSelectionBehavior(QAbstractItemView::SelectRows);
    desk_layout->addWidget(clients_);
    activate_ = new QPushButton("Control selected computer"); desk_layout->addWidget(activate_);
    connect(activate_, &QPushButton::clicked, this, [this] { const int row = clients_->currentRow(); if (row >= 0) action("activate", clients_->item(row, 1)->text()); });
    connect(clients_, &QTableWidget::itemChanged, this, [this] { if (!updating_) selected_participants(); });
    tabs->addTab(desk, "Computers");
    auto* layout_page = new QWidget; auto* layout_box = new QVBoxLayout(layout_page);
    auto* layout_hint = new QLabel("Arrange monitor origins in native pixels. Touching edges allow switching; gaps block it. Negative coordinates are supported."); layout_hint->setWordWrap(true); layout_box->addWidget(layout_hint);
    scene_ = new QGraphicsScene(this); view_ = new QGraphicsView(scene_); view_->setMinimumHeight(140); view_->setRenderHint(QPainter::Antialiasing); layout_box->addWidget(view_, 1);
    monitors_ = new QTableWidget(0, 6); monitors_->setHorizontalHeaderLabels({"Computer", "Monitor", "X", "Y", "Width", "Height"});
    monitors_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); monitors_->verticalHeader()->hide(); layout_box->addWidget(monitors_, 1);
    auto* layout_controls = new QHBoxLayout;
    edges_ = new QCheckBox("Switch at touching screen edges"); edges_->setChecked(settings_.value("edges", true).toBool());
    apply_ = new QPushButton("Apply monitor layout"); layout_controls->addWidget(edges_); layout_controls->addStretch(); layout_controls->addWidget(apply_); layout_box->addLayout(layout_controls);
    connect(apply_, &QPushButton::clicked, this, &MainWindow::apply_layout);
    tabs->addTab(layout_page, "Monitor layout");
    logs_ = new QPlainTextEdit; logs_->setReadOnly(true); logs_->setMaximumBlockCount(300); tabs->addTab(logs_, "Activity");
    page->addWidget(tabs, 1);
    auto* shortcuts = new QLabel("Ctrl + Alt + Home  ·  Return to host      Ctrl + Alt + Right  ·  Next computer      Ctrl + Alt + M  ·  Keyboard mirror");
    shortcuts->setWordWrap(true); shortcuts->setObjectName("muted"); page->addWidget(shortcuts);
    setCentralWidget(root);
    setStyleSheet(R"(
        QWidget { font-family: 'Segoe UI', sans-serif; font-size: 13px; color: #dce5f2; }
        QMainWindow, QWidget#qt_tabwidget_stackedwidget { background: #111827; }
        QLabel#title { font-size: 29px; font-weight: 700; color: #ffffff; }
        QLabel#subtitle { color: #6ee7c7; font-size: 10px; font-weight: 600; }
        QLabel#muted { color: #92a2b9; }
        QLabel#metric { background: #1e293b; padding: 14px; border-radius: 7px; font-weight: 600; }
        QLabel#status { background: #132e35; color: #8ee8cf; padding: 10px; border-radius: 5px; }
        QGroupBox { border: 1px solid #344155; border-radius: 7px; margin-top: 12px; padding: 16px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; }
        QLineEdit, QSpinBox, QComboBox { background: #1e293b; border: 1px solid #42516b; border-radius: 4px; padding: 7px; selection-background-color: #287d75; }
        QPushButton { background: #26354a; border: 1px solid #43516b; border-radius: 5px; padding: 9px 13px; }
        QPushButton:hover { background: #354963; } QPushButton:disabled { color: #748095; background: #1a2433; }
        QPushButton#primary { background: #4cdbc0; color: #102623; border: none; font-weight: 600; }
        QTabWidget::pane { border: 1px solid #344155; } QTabBar::tab { background: #1b2638; padding: 10px 18px; }
        QTabBar::tab:selected { background: #304158; color: #83ead2; }
        QTableWidget, QPlainTextEdit, QGraphicsView { background: #152033; alternate-background-color: #1e293b; border: 1px solid #344155; gridline-color: #2b3a50; }
        QHeaderView::section { background: #24334a; border: none; padding: 8px; }
        QTableWidget::item:selected { background: #33556e; }
    )");
    saved_layout_ = QJsonDocument::fromJson(settings_.value("layout").toByteArray()).array();
    participants_ = settings_.value("participants").toStringList();
    connect(role_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        const bool host = role_->currentIndex() == 0;
        if (host && key_->text().isEmpty()) key_->setText(new_key());
        start_->setText(host ? "Start host" : "Connect to host");
        address_->setEnabled(!host); identity_->setEnabled(!host); bind_->setEnabled(host);
    });
    if (!demo_) {
        runtime_ = new Runtime; runtime_->moveToThread(&worker_);
        connect(&worker_, &QThread::finished, runtime_, &QObject::deleteLater);
        connect(runtime_, &Runtime::snapshot, this, &MainWindow::update_state);
        connect(runtime_, &Runtime::log_message, this, &MainWindow::log);
        worker_.start();
        update_state({{"status", "Ready — select a role and start"}, {"running", false}, {"active", "host"}});
    } else {
        role_->setCurrentIndex(0);
        QJsonArray monitors{QJsonObject{{"computer", "host"}, {"monitor", "DISPLAY1"}, {"x", 0}, {"y", 0}, {"w", 1920}, {"h", 1080}},
            QJsonObject{{"computer", "TRADING-02"}, {"monitor", "DISPLAY1"}, {"x", 1920}, {"y", 0}, {"w", 2560}, {"h", 1440}}};
        update_state({{"running", true}, {"host", true}, {"status", "INTERFACE DEMO — no input is captured or transmitted"}, {"active", "host"},
            {"clients", QJsonArray{QJsonObject{{"id", "TRADING-02"}, {"address", "192.0.2.20"}, {"mirror", true}}}}, {"monitors", monitors}});
    }
}
MainWindow::~MainWindow() {
    save();
    if (runtime_) {
        QMetaObject::invokeMethod(runtime_, "stop", Qt::BlockingQueuedConnection);
        worker_.quit(); worker_.wait();
    }
}
QJsonObject MainWindow::config() const {
    return {{"role", role_->currentIndex() == 0 ? "host" : "client"}, {"id", identity_->text().trimmed()},
        {"address", address_->text().trimmed()}, {"bind", bind_->text().trimmed()}, {"port", port_->value()}, {"key", key_->text()},
        {"edges", edges_->isChecked()}, {"layout", saved_layout_}, {"participants", QJsonArray::fromStringList(participants_)}};
}
void MainWindow::start() {
    save();
    if (demo_) { state_["running"] = true; update_state(state_); return; }
    QMetaObject::invokeMethod(runtime_, "start", Qt::QueuedConnection, Q_ARG(QJsonObject, config()));
}
void MainWindow::save() {
    if (demo_) return;
    settings_.setValue("role", role_->currentIndex()); settings_.setValue("id", identity_->text());
    settings_.setValue("address", address_->text()); settings_.setValue("bind", bind_->text()); settings_.setValue("port", port_->value());
    settings_.setValue("edges", edges_->isChecked()); settings_.setValue("layout", QJsonDocument(saved_layout_).toJson(QJsonDocument::Compact));
    settings_.setValue("participants", participants_);
}
void MainWindow::update_state(QJsonObject state) {
    updating_ = true; state_ = state;
    const bool running = state.value("running").toBool(), host = role_->currentIndex() == 0, mirror = state.value("mirror").toBool();
    status_->setText(state.value("status").toString()); mode_->setText(mirror ? "KEYBOARD MIRROR" : "INDIVIDUAL CONTROL");
    active_->setText(host ? "Active computer: " + state.value("active").toString("host") : "Client: " + identity_->text());
    cursor_->setText(QString("%1 · (%2, %3)").arg(state.value("monitor").toString("Monitor —")).arg(state.value("cursor_x").toInt()).arg(state.value("cursor_y").toInt()));
    start_->setEnabled(!running); start_->setText(host ? "Start host" : "Connect to host"); stop_->setEnabled(running);
    for (QWidget* widget : std::initializer_list<QWidget*>{role_, identity_, address_, bind_, key_, port_, edges_}) widget->setEnabled(!running);
    if (!running) { address_->setEnabled(!host); identity_->setEnabled(!host); bind_->setEnabled(host); }
    home_->setEnabled(running && host); next_->setEnabled(running && host && !mirror); mirror_->setEnabled(running && host);
    activate_->setEnabled(running && host && !mirror); apply_->setEnabled(running && host);
    mirror_->setText(mirror ? "End keyboard mirror" : "Start keyboard mirror");
    const auto clients = state.value("clients").toArray();
    QString selected;
    if (clients_->currentRow() >= 0 && clients_->item(clients_->currentRow(), 1)) selected = clients_->item(clients_->currentRow(), 1)->text();
    clients_->setRowCount(clients.size());
    for (int row = 0; row < clients.size(); ++row) {
        const auto client = clients[row].toObject();
        auto* check = cell(""); check->setFlags(check->flags() | Qt::ItemIsUserCheckable); check->setCheckState(client.value("mirror").toBool() ? Qt::Checked : Qt::Unchecked);
        clients_->setItem(row, 0, check); clients_->setItem(row, 1, cell(client.value("id").toString()));
        clients_->setItem(row, 2, cell(client.value("address").toString()));
        clients_->setItem(row, 3, cell(client.value("active").toBool() ? "Active" : mirror && client.value("mirror").toBool() ? "Mirroring" : "Connected"));
        if (client.value("id").toString() == selected) clients_->selectRow(row);
    }
    const auto monitors = state.value("monitors").toArray();
    const auto signature = QString::fromUtf8(QJsonDocument(monitors).toJson(QJsonDocument::Compact));
    if (signature != topology_) { topology_ = signature; update_layout(monitors); }
    updating_ = false;
}
void MainWindow::update_layout(const QJsonArray& monitors) {
    monitors_->setRowCount(monitors.size()); scene_->clear();
    for (int row = 0; row < monitors.size(); ++row) {
        const auto m = monitors[row].toObject();
        const QStringList values{m.value("computer").toString(), m.value("monitor").toString(), QString::number(m.value("x").toInt()),
            QString::number(m.value("y").toInt()), QString::number(m.value("w").toInt()), QString::number(m.value("h").toInt())};
        for (int col = 0; col < 6; ++col) monitors_->setItem(row, col, cell(values[col], col == 2 || col == 3));
        const QRectF bounds(m.value("x").toInt(), m.value("y").toInt(), m.value("w").toInt(), m.value("h").toInt());
        scene_->addRect(bounds, QPen(QColor("#75ddc6"), 8), QBrush(QColor(m.value("computer") == "host" ? "#20514d" : "#293e62")));
        auto* label = scene_->addText(values[0] + "\n" + values[4] + " × " + values[5], QFont("Segoe UI", 55));
        label->setDefaultTextColor(QColor("#e3eef9")); label->setPos(bounds.x() + 45, bounds.y() + 45);
    }
    scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-80, -80, 80, 80));
    QTimer::singleShot(0, this, [this] { view_->fitInView(scene_->sceneRect(), Qt::KeepAspectRatio); });
}
void MainWindow::apply_layout() {
    QJsonArray positions;
    for (int row = 0; row < monitors_->rowCount(); ++row) {
        bool x_ok{}, y_ok{}; const int x = monitors_->item(row, 2)->text().toInt(&x_ok), y = monitors_->item(row, 3)->text().toInt(&y_ok);
        if (!x_ok || !y_ok || x < -1000000 || x > 1000000 || y < -1000000 || y > 1000000) { log("Enter valid integer monitor coordinates"); return; }
        positions.append(QJsonObject{{"computer", monitors_->item(row, 0)->text()}, {"monitor", monitors_->item(row, 1)->text()}, {"x", x}, {"y", y}});
    }
    // Persist only after local geometry validation; runtime checks it again.
    MonitorLayout candidate;
    for (int row = 0; row < monitors_->rowCount(); ++row) {
        auto o = positions[row].toObject(); Rectangle bounds{0, 0, monitors_->item(row, 4)->text().toInt(), monitors_->item(row, 5)->text().toInt()};
        auto shared = bounds; shared.x = o.value("x").toInt(); shared.y = o.value("y").toInt();
        candidate.monitors.push_back({o.value("computer").toString().toStdString(), o.value("monitor").toString().toStdString(), bounds, shared});
    }
    std::string error; if (!candidate.validate(error)) { log(QString::fromStdString(error)); return; }
    saved_layout_ = positions; save();
    if (demo_) { log("Demo layout validated"); return; }
    QMetaObject::invokeMethod(runtime_, "configure_layout", Qt::QueuedConnection, Q_ARG(QJsonArray, positions));
}
void MainWindow::selected_participants() {
    participants_.clear();
    for (int row = 0; row < clients_->rowCount(); ++row) if (clients_->item(row, 0)->checkState() == Qt::Checked) participants_.append(clients_->item(row, 1)->text());
    save();
    if (!demo_) QMetaObject::invokeMethod(runtime_, "set_participants", Qt::QueuedConnection, Q_ARG(QStringList, participants_));
}
void MainWindow::action(const QString& action, const QString& target) {
    if (!demo_) { QMetaObject::invokeMethod(runtime_, "command", Qt::QueuedConnection, Q_ARG(QString, action), Q_ARG(QString, target)); return; }
    if (action == "home") { state_["active"] = "host"; state_["mirror"] = false; }
    if (action == "mirror") { state_["mirror"] = !state_.value("mirror").toBool(); state_["active"] = "host"; }
    if (action == "activate") state_["active"] = target;
    if (action == "next") state_["active"] = state_.value("active").toString() == "host" ? "TRADING-02" : "host";
    update_state(state_);
}
void MainWindow::log(const QString& text) { logs_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text); status_->setText(text); }
}
