#include "app/Application.h"
#include "app/MainWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QString>
#include <iostream>

int main(int argc, char* argv[])
{
    Muffin::Application app(argc, argv);

    app.setApplicationName("Muffin");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("Muffin");

    QString filePath;
    QString screenshotPath;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--screenshot") && i + 1 < argc) {
            screenshotPath = QString::fromLocal8Bit(argv[++i]);
        } else if (filePath.isEmpty()) {
            filePath = arg;
        }
    }

    Muffin::MainWindow window;
    if (!filePath.isEmpty()) {
        window.openFile(filePath);
    }
    window.show();

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &window, [&window, screenshotPath]() {
            const QFileInfo info(screenshotPath);
            if (!info.absoluteDir().exists()) {
                info.absoluteDir().mkpath(QStringLiteral("."));
            }
            const bool saved = window.grab().save(screenshotPath);
            if (!saved) {
                std::cerr << "Failed to save screenshot: " << screenshotPath.toLocal8Bit().constData() << '\n';
            }
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
