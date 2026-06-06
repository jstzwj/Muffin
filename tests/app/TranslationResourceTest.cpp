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

void verifyTranslationResource(const QString& code, const QString& expectedFile, const QString& expectedPreferences) {
  QTranslator translator;
  require(
      translator.load(QStringLiteral(":/i18n/muffin_%1.qm").arg(code)),
      QStringLiteral("%1 translation resource should load").arg(code).toStdString().c_str());
  require(
      QCoreApplication::installTranslator(&translator),
      QStringLiteral("%1 translator should install").arg(code).toStdString().c_str());

  requireTranslation("muffin::MainWindow", "File", expectedFile);
  requireTranslation("muffin::PreferencesDialog", "Preferences", expectedPreferences);

  QCoreApplication::removeTranslator(&translator);
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(Q_OS_MACOS)
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  QApplication app(argc, argv);

  verifyTranslationResource(QStringLiteral("ja"), QStringLiteral("\u30D5\u30A1\u30A4\u30EB"), QStringLiteral("\u74B0\u5883\u8A2D\u5B9A"));

  QTranslator translator;
  require(translator.load(QStringLiteral(":/i18n/muffin_zh_CN.qm")), "zh_CN translation resource should load");
  require(QCoreApplication::installTranslator(&translator), "zh_CN translator should install");
  requireTranslation("muffin::MainWindow", "File", QStringLiteral("\u6587\u4EF6(&F)"));
  requireTranslation("muffin::PreferencesDialog", "Preferences", QStringLiteral("\u504F\u597D\u8BBE\u7F6E"));
  requireTranslation("muffin::SourceEditorWidget", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::BlockLayoutBuilder", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::FileController", "Open", QStringLiteral("\u6253\u5F00"));
  QCoreApplication::removeTranslator(&translator);

  verifyTranslationResource(QStringLiteral("vi"), QStringLiteral("T\u1EC7p"), QStringLiteral("T\u00F9y ch\u1ECDn"));
  verifyTranslationResource(QStringLiteral("fr"), QStringLiteral("Fichier"), QStringLiteral("Pr\u00E9f\u00E9rences"));
  verifyTranslationResource(QStringLiteral("es"), QStringLiteral("Archivo"), QStringLiteral("Preferencias"));
  verifyTranslationResource(QStringLiteral("ru"), QStringLiteral("\u0424\u0430\u0439\u043B"), QStringLiteral("\u041D\u0430\u0441\u0442\u0440\u043E\u0439\u043A\u0438"));
  verifyTranslationResource(QStringLiteral("de"), QStringLiteral("Datei"), QStringLiteral("Einstellungen"));
  verifyTranslationResource(QStringLiteral("pt_BR"), QStringLiteral("Arquivo"), QStringLiteral("Prefer\u00EAncias"));
  verifyTranslationResource(QStringLiteral("ko"), QStringLiteral("\uD30C\uC77C"), QStringLiteral("\uD658\uACBD\uC124\uC815"));
  verifyTranslationResource(QStringLiteral("it"), QStringLiteral("File"), QStringLiteral("Preferenze"));
  verifyTranslationResource(QStringLiteral("zh_TW"), QStringLiteral("\u6A94\u6848(&F)"), QStringLiteral("\u504F\u597D\u8A2D\u5B9A"));
  verifyTranslationResource(QStringLiteral("tr"), QStringLiteral("Dosya(&F)"), QStringLiteral("Tercihler"));
  verifyTranslationResource(QStringLiteral("pl"), QStringLiteral("Plik"), QStringLiteral("Preferencje"));
  verifyTranslationResource(QStringLiteral("nl"), QStringLiteral("Bestand"), QStringLiteral("Voorkeuren"));
  return 0;
}
