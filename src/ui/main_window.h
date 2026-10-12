#pragma once
#include "app/runtime.h"
#include <QMainWindow>
#include <QSettings>
#include <QThread>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QPlainTextEdit;
class QGraphicsView;
class QGraphicsScene;
namespace sharekeys {
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool demo = false);
    ~MainWindow() override;
private:
    QJsonObject config() const;
    void start();
    void update_state(QJsonObject state);
    void update_layout(const QJsonArray& monitors);
    void save();
    void apply_layout();
    void selected_participants();
    void action(const QString& name, const QString& target = {});
    void log(const QString& text);
    Runtime* runtime_{};
    QThread worker_;
    QSettings settings_;
    QComboBox* role_{};
    QLineEdit *identity_{}, *address_{}, *bind_{}, *key_{};
    QSpinBox* port_{};
    QCheckBox* edges_{};
    QPushButton *start_{}, *stop_{}, *home_{}, *mirror_{}, *next_{}, *activate_{}, *apply_{};
    QLabel *status_{}, *active_{}, *cursor_{}, *mode_{};
    QTableWidget *clients_{}, *monitors_{};
    QGraphicsView* view_{};
    QGraphicsScene* scene_{};
    QPlainTextEdit* logs_{};
    QJsonObject state_;
    QJsonArray saved_layout_;
    QStringList participants_;
    QString topology_;
    bool updating_{}, demo_{};
};
}
