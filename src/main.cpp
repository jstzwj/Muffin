#include "app/Application.h"
#include "app/MainWindow.h"

#include <QTimer>
#include <QString>

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
        QTimer::singleShot(800, &window, [&window, screenshotPath]() {
            window.grab().save(screenshotPath);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
