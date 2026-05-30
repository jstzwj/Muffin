#include "app/Application.h"
#include "app/MainWindow.h"

#include <QCommandLineParser>
#include <QFile>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    Application app(argc, argv);
    app.setOrganizationName("Muffin");
    app.setApplicationName("Muffin");
    app.setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Muffin Markdown Editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Markdown file to open");
    parser.process(app);

    MainWindow window;
    window.show();

    const auto args = parser.positionalArguments();
    if (!args.isEmpty()) {
        if (!window.openFile(args.first())) {
            QMessageBox::warning(&window, "Error",
                                 QString("Cannot open file: %1").arg(args.first()));
        }
    }

    return app.exec();
}
