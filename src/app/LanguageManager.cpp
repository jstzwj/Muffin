#include "app/LanguageManager.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>

namespace muffin {

LanguageManager& LanguageManager::instance() {
  static LanguageManager manager;
  return manager;
}

LanguageManager::LanguageManager(QObject* parent) : QObject(parent) {}

QVector<LanguageInfo> LanguageManager::availableLanguages() const {
  return {
      {QStringLiteral("system"), tr("System Default"), QStringLiteral("System Default")},
      {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
      {QStringLiteral("ja"), QStringLiteral("\u65E5\u672C\u8A9E"), QStringLiteral("Japanese")},
      {QStringLiteral("zh_CN"), QStringLiteral("\u7B80\u4F53\u4E2D\u6587"), QStringLiteral("Simplified Chinese")},
      {QStringLiteral("vi"), QStringLiteral("Ti\u1EBFng Vi\u1EC7t"), QStringLiteral("Vietnamese")},
      {QStringLiteral("fr"), QStringLiteral("Fran\u00E7ais"), QStringLiteral("French")},
      {QStringLiteral("es"), QStringLiteral("Espa\u00F1ol"), QStringLiteral("Spanish")},
      {QStringLiteral("ru"), QStringLiteral("\u0420\u0443\u0441\u0441\u043A\u0438\u0439"), QStringLiteral("Russian")},
      {QStringLiteral("de"), QStringLiteral("Deutsch"), QStringLiteral("German")},
      {QStringLiteral("pt_BR"), QStringLiteral("Portugu\u00EAs (Brasil)"), QStringLiteral("Portuguese (Brazil)")},
      {QStringLiteral("ko"), QStringLiteral("\uD55C\uAD6D\uC5B4"), QStringLiteral("Korean")},
      {QStringLiteral("it"), QStringLiteral("Italiano"), QStringLiteral("Italian")},
      {QStringLiteral("zh_TW"), QStringLiteral("\u7E41\u9AD4\u4E2D\u6587"), QStringLiteral("Traditional Chinese")},
      {QStringLiteral("tr"), QStringLiteral("T\u00FCrk\u00E7e"), QStringLiteral("Turkish")},
      {QStringLiteral("pl"), QStringLiteral("Polski"), QStringLiteral("Polish")},
      {QStringLiteral("nl"), QStringLiteral("Nederlands"), QStringLiteral("Dutch")},
  };
}

QString LanguageManager::currentLanguageCode() const {
  return selectedCode_;
}

void LanguageManager::initialize() {
  QSettings settings;
  setLanguage(settings.value(settingsKey(), QStringLiteral("system")).toString());
}

bool LanguageManager::setLanguage(QString code) {
  const QString selectedCode = code.isEmpty() ? QStringLiteral("system") : std::move(code);
  const QString resolved = resolvedCode(selectedCode);
  if (selectedCode == selectedCode_ && resolved == currentCode_) {
    return true;
  }

  if (!installTranslator(resolved)) {
    return false;
  }

  selectedCode_ = selectedCode;
  currentCode_ = resolved;
  QSettings settings;
  settings.setValue(settingsKey(), selectedCode);
  emit languageChanged(selectedCode);
  return true;
}

QString LanguageManager::settingsKey() {
  return QStringLiteral("language");
}

QString LanguageManager::normalizedCode(QString code) {
  return resolvedCode(code);
}

QString LanguageManager::resolvedCode(const QString& code) {
  if (code.isEmpty() || code == QStringLiteral("system")) {
    const QString system = resolvedCode(QLocale::system().name());
    return system;
  }

  QString normalized = code;
  normalized.replace(QLatin1Char('-'), QLatin1Char('_'));

  if (normalized.compare(QStringLiteral("zh_TW"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("zh_HK"), Qt::CaseInsensitive) == 0 ||
      normalized.compare(QStringLiteral("zh_MO"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("zh_TW");
  }
  if (normalized.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
    return QStringLiteral("zh_CN");
  }

  if (normalized.compare(QStringLiteral("pt_BR"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("pt_BR");
  }

  const QString language = normalized.section(QLatin1Char('_'), 0, 0).toLower();
  if (language == QStringLiteral("ja") || language == QStringLiteral("vi") || language == QStringLiteral("fr") ||
      language == QStringLiteral("es") || language == QStringLiteral("ru") || language == QStringLiteral("de") ||
      language == QStringLiteral("ko") || language == QStringLiteral("it") || language == QStringLiteral("tr") ||
      language == QStringLiteral("pl") || language == QStringLiteral("nl") || language == QStringLiteral("en")) {
    return language;
  }

  return QStringLiteral("en");
}

bool LanguageManager::installTranslator(const QString& code) {
  if (translator_) {
    QApplication::removeTranslator(translator_.get());
    translator_.reset();
  }

  if (code == QStringLiteral("en")) {
    return true;
  }

  auto translator = std::make_unique<QTranslator>();
  if (!translator->load(QStringLiteral(":/i18n/muffin_%1.qm").arg(code))) {
    return false;
  }

  QApplication::installTranslator(translator.get());
  translator_ = std::move(translator);
  return true;
}

}  // namespace muffin
