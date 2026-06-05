#include <QApplication>
#include <QCoreApplication>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

void requireTranslation(const char* context, const char* source, const QString& expected) {
  const QString translated = QCoreApplication::translate(context, source);
  require(translated == expected, QStringLiteral("%1/%2 translation mismatch: %3")
                                     .arg(QString::fromUtf8(context), QString::fromUtf8(source), translated)
                                     .toStdString()
                                     .c_str());
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(Q_OS_MACOS)
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  QApplication app(argc, argv);

  QTranslator translator;
  require(translator.load(QStringLiteral(":/i18n/muffin_zh_CN.qm")), "zh_CN translation resource should load");
  require(QCoreApplication::installTranslator(&translator), "zh_CN translator should install");

  requireTranslation("muffin::MainWindow", "File", QStringLiteral("\u6587\u4EF6(&F)"));
  requireTranslation("muffin::PreferencesDialog", "Preferences", QStringLiteral("\u504F\u597D\u8BBE\u7F6E"));
  requireTranslation("muffin::SourceEditorWidget", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::BlockLayoutBuilder", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::FileController", "Open", QStringLiteral("\u6253\u5F00"));

  QCoreApplication::removeTranslator(&translator);
  return 0;
}
