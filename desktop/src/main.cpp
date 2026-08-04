#include "core/workspace.hpp"
#include "ui/pages.hpp"
#include "ui/style.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>
#include <QMessageBox>
#include <QTimer>

#ifndef ZEN_APP_KIND
#define ZEN_APP_KIND "notes"
#endif

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QApplication::setOrganizationName("Zenov");
    QApplication::setOrganizationDomain("zenov.local");

    const zenapps::AppKind kind = zenapps::appKindFromString(QString::fromUtf8(ZEN_APP_KIND));
    QApplication::setApplicationName(zenapps::appTitle(kind));
    zenapps::applyZenStyle(application);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QString("Standalone %1 application backed by the shared local Zen Apps workspace.")
            .arg(zenapps::appTitle(kind)));
    parser.addHelpOption();
    const QCommandLineOption dataDirOption(
        "data-dir", "Use a specific workspace directory.", "path");
    const QCommandLineOption smokeOption(
        "smoke-test", "Construct the application, process the event loop and exit.");
    const QCommandLineOption screenshotOption(
        "screenshot", "Capture the application window to a PNG file and exit.", "path");
    parser.addOption(dataDirOption);
    parser.addOption(smokeOption);
    parser.addOption(screenshotOption);
    parser.process(application);

    zenapps::Workspace workspace(parser.value(dataDirOption));
    if (!workspace.initialize()) {
        QMessageBox::critical(nullptr, "Workspace unavailable", workspace.lastError());
        return 2;
    }

    QMainWindow window;
    window.setWindowTitle(zenapps::appTitle(kind));
    window.setMinimumSize(900, 620);
    window.resize(1180, 760);
    window.setCentralWidget(zenapps::createPage(kind, &workspace, &window));
    window.show();

    if (parser.isSet(smokeOption)) {
        QTimer::singleShot(250, &application, &QApplication::quit);
    }

    if (parser.isSet(screenshotOption)) {
        const QString requested = parser.value(screenshotOption);
        const QString output = QFileInfo(requested).isAbsolute()
            ? requested : QDir::current().absoluteFilePath(requested);
        QDir().mkpath(QFileInfo(output).absolutePath());
        QTimer::singleShot(700, &window, [&application, &window, output] {
            const bool saved = window.grab().save(output, "PNG");
            application.exit(saved ? 0 : 3);
        });
    }

    return application.exec();
}
