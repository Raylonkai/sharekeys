#include "ui/main_window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QDir>
#include <QSettings>
#ifdef _WIN32
#include <windows.h>
#endif
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("ShareKeys"); app.setApplicationName("ShareKeys"); app.setApplicationVersion("0.2.0");
    QCommandLineParser parser; parser.setApplicationDescription("Windows LAN keyboard and mouse sharing");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({"demo", "Explore the interface with sample computers; no live input or network."});
    parser.addOption({"smoke-test", "Open and close the UI automatically for build verification."});
    parser.addOption({"screenshot", "Save a window screenshot (use with --smoke-test).", "path"});
    parser.process(app);
    if (parser.isSet("smoke-test")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath() + "/sharekeys-ui-smoke");
    }
    sharekeys::MainWindow window(parser.isSet("demo")); window.show();
    if (parser.isSet("smoke-test")) QTimer::singleShot(500, &app, [&] {
        if (parser.isSet("screenshot")) window.grab().save(parser.value("screenshot"));
        app.quit();
    });
    return app.exec();
}
