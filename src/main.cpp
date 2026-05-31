#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName("Muffin");
  QApplication::setOrganizationName("Muffin");

  MainWindow window;
  window.show();

  return QApplication::exec();
}
