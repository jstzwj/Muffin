#include "app/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("Muffin");
  QApplication::setOrganizationName("Muffin");
  QApplication::setApplicationVersion("0.1.0");

  QCommandLineParser parser;
  parser.setApplicationDescription("A fast native Markdown editor built with C++ and Qt 6 Widgets.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Markdown or text file to open.");
  parser.process(app);

  muffin::MainWindow window;
  const QStringList positionalArguments = parser.positionalArguments();
  if (!positionalArguments.isEmpty()) {
    window.openFile(QFileInfo(positionalArguments.first()).absoluteFilePath());
  }
  window.show();

  return QApplication::exec();
}
