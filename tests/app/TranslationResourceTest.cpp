#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTranslator>
#include <QXmlStreamReader>

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

QStringList placeholders(const QString& text) {
  static const QRegularExpression pattern(QStringLiteral("%L?(?:[1-9][0-9]?|n)"));
  QStringList result;
  auto matches = pattern.globalMatch(text);
  while (matches.hasNext()) result.append(matches.next().captured());
  result.sort();
  return result;
}

void verifyCatalogs() {
  const QDir dir(QString::fromUtf8(MUFFIN_TRANSLATIONS_DIR));
  const auto files = dir.entryList({QStringLiteral("muffin_*.ts")}, QDir::Files);
  require(files.size() == 14, "All supported language catalogs must be checked");
  QSet<QString> referenceKeys;
  for (const QString& name : files) {
    QSet<QString> keys;
    QTranslator translator;
    const QString qm = name.chopped(3) + QStringLiteral(".qm");
    require(translator.load(QStringLiteral(":/i18n/") + qm), "Embedded catalog must load");
    QFile file(dir.filePath(name));
    require(file.open(QIODevice::ReadOnly), "Translation source must open");
    QXmlStreamReader xml(&file);
    QString context;
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement()) continue;
      if (xml.name() == QLatin1String("name")) {
        context = xml.readElementText();
      } else if (xml.name() == QLatin1String("message")) {
        const bool numerus = xml.attributes().value(QLatin1String("numerus")) == QLatin1String("yes");
        QString source, comment, state;
        QStringList forms;
        while (xml.readNextStartElement()) {
          if (xml.name() == QLatin1String("source")) source = xml.readElementText();
          else if (xml.name() == QLatin1String("comment")) comment = xml.readElementText();
          else if (xml.name() == QLatin1String("translation")) {
            state = xml.attributes().value(QLatin1String("type")).toString();
            if (numerus) {
              while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("numerusform")) forms.append(xml.readElementText());
                else xml.skipCurrentElement();
              }
            } else {
              forms.append(xml.readElementText());
            }
          } else xml.skipCurrentElement();
        }
        if (state == QLatin1String("obsolete") || state == QLatin1String("vanished")) continue;
        const QString label = QStringLiteral("%1: %2/%3").arg(name, context, source);
        const QString key = context + QChar(0x1f) + source + QChar(0x1f) + comment;
        require(!keys.contains(key), (label + QStringLiteral(" is duplicated")).toStdString().c_str());
        keys.insert(key);
        require(state != QLatin1String("unfinished"), (label + QStringLiteral(" is unfinished")).toStdString().c_str());
        require(!forms.isEmpty(), (label + QStringLiteral(" has no translation")).toStdString().c_str());
        for (const QString& form : forms) {
          require(!form.trimmed().isEmpty(), (label + QStringLiteral(" has an empty translation")).toStdString().c_str());
          // A localized percentage (25% -> %25 in Turkish) is not an argument.
          if (!placeholders(source).isEmpty()) {
            require(placeholders(form) == placeholders(source), (label + QStringLiteral(" has mismatched placeholders")).toStdString().c_str());
          }
        }
        const QByteArray ctx = context.toUtf8(), src = source.toUtf8(), disambiguation = comment.toUtf8();
        for (int n : {-1, 0, 1, 2, 5, 21, 101}) {
          if (numerus == (n < 0)) continue;
          const QString compiled = translator.translate(ctx.constData(), src.constData(),
              comment.isEmpty() ? nullptr : disambiguation.constData(), n);
          require(!compiled.isEmpty(), (label + QStringLiteral(" is absent from the embedded QM")).toStdString().c_str());
          if (!numerus) require(compiled == forms.first(), (label + QStringLiteral(" has a stale embedded QM")).toStdString().c_str());
        }
      }
    }
    require(!xml.hasError(), (name + QStringLiteral(" is invalid XML")).toStdString().c_str());
    require(!keys.isEmpty(), "Translation catalogs must contain active messages");
    if (referenceKeys.isEmpty()) referenceKeys = keys;
    require(keys == referenceKeys, (name + QStringLiteral(" has a different set of active messages")).toStdString().c_str());
  }
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
  verifyCatalogs();

  verifyTranslationResource(QStringLiteral("ja"), QStringLiteral("\u30D5\u30A1\u30A4\u30EB"), QStringLiteral("\u8A2D\u5B9A"));

  QTranslator translator;
  require(translator.load(QStringLiteral(":/i18n/muffin_zh_CN.qm")), "zh_CN translation resource should load");
  require(QCoreApplication::installTranslator(&translator), "zh_CN translator should install");
  requireTranslation("muffin::MainWindow", "File", QStringLiteral("\u6587\u4EF6(&F)"));
  requireTranslation("muffin::PreferencesDialog", "Preferences", QStringLiteral("\u504F\u597D\u8BBE\u7F6E"));
  requireTranslation("muffin::SourceEditorWidget", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::BlockLayoutBuilder", "Start writing...", QStringLiteral("\u5F00\u59CB\u5199\u4F5C..."));
  requireTranslation("muffin::FileController", "Open", QStringLiteral("\u6253\u5F00"));
  requireTranslation("muffin::MainWindow", "Export Mermaid as SVG...",
                     QStringLiteral("\u5C06 Mermaid \u5BFC\u51FA\u4E3A SVG..."));
  requireTranslation("muffin::MainWindow", "Chinese Simplified (GB2312)", QStringLiteral("简体中文 (GB2312)"));
  requireTranslation("muffin::EditorView", "Markdown editor", QStringLiteral("Markdown 编辑器"));
  requireTranslation("muffin::VirtualSourceEdit", "Markdown source editor", QStringLiteral("Markdown 源码编辑器"));
  QCoreApplication::removeTranslator(&translator);

  verifyTranslationResource(QStringLiteral("vi"), QStringLiteral("T\u1EC7p"), QStringLiteral("T\u00F9y ch\u1EC9nh"));
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
