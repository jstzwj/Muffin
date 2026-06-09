#include "app/LanguageManager.h"

#include <QApplication>
#include <QLocale>
#include <QSettings>

muffin::LanguageManager& muffin::LanguageManager::instance() {
  static muffin::LanguageManager manager;
  return manager;
}

muffin::LanguageManager::LanguageManager(QObject* parent) : QObject(parent) {}

QVector<muffin::LanguageInfo> muffin::LanguageManager::availableLanguages() const {
  return {
      {QStringLiteral("system"), tr("System Default"), QStringLiteral("System Default")},
      {QStringLiteral("en"), QStringLiteral("English"), QStringLiteral("English")},
      {QStringLiteral("ja"), QStringLiteral("日本語"), QStringLiteral("Japanese")},
      {QStringLiteral("zh_CN"), QStringLiteral("简体中文"), QStringLiteral("Simplified Chinese")},
      {QStringLiteral("vi"), QStringLiteral("Tiếng Việt"), QStringLiteral("Vietnamese")},
      {QStringLiteral("fr"), QStringLiteral("Français"), QStringLiteral("French")},
      {QStringLiteral("es"), QStringLiteral("Español"), QStringLiteral("Spanish")},
      {QStringLiteral("ru"), QStringLiteral("Русский"), QStringLiteral("Russian")},
      {QStringLiteral("de"), QStringLiteral("Deutsch"), QStringLiteral("German")},
      {QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)"), QStringLiteral("Portuguese (Brazil)")},
      {QStringLiteral("ko"), QStringLiteral("한국어"), QStringLiteral("Korean")},
      {QStringLiteral("it"), QStringLiteral("Italiano"), QStringLiteral("Italian")},
      {QStringLiteral("zh_TW"), QStringLiteral("繁體中文"), QStringLiteral("Traditional Chinese")},
      {QStringLiteral("tr"), QStringLiteral("Türkçe"), QStringLiteral("Turkish")},
      {QStringLiteral("pl"), QStringLiteral("Polski"), QStringLiteral("Polish")},
      {QStringLiteral("nl"), QStringLiteral("Nederlands"), QStringLiteral("Dutch")},
  };
}

QString muffin::LanguageManager::currentLanguageCode() const {
  return selectedCode_;
}

void muffin::LanguageManager::initialize() {
  QSettings settings;
  setLanguage(settings.value(settingsKey(), QStringLiteral("system")).toString());
}

bool muffin::LanguageManager::setLanguage(QString code) {
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

QString muffin::LanguageManager::settingsKey() {
  return QStringLiteral("language");
}

QString muffin::LanguageManager::normalizedCode(QString code) {
  return resolvedCode(code);
}

QString muffin::LanguageManager::resolvedCode(const QString& code) {
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

bool muffin::LanguageManager::installTranslator(const QString& code) {
  if (translator_) {
    QApplication::removeTranslator(translator_.get());
    translator_.reset();
  }
  if (qtTranslator_) {
    QApplication::removeTranslator(qtTranslator_.get());
    qtTranslator_.reset();
  }

  if (code == QStringLiteral("en")) {
    return true;
  }

  // Load application translations
  const QString qmPath = QStringLiteral(":/i18n/muffin_%1.qm").arg(code);
  auto translator = std::make_unique<QTranslator>();
  if (!translator->load(qmPath)) {
    return false;
  }

  QApplication::installTranslator(translator.get());
  translator_ = std::move(translator);

  // Load Qt base translations for standard dialog buttons (Save, Discard, Cancel, etc.)
  const QString qtQmPath = QStringLiteral(":/i18n/qt/qtbase_%1.qm").arg(code);
  auto qtTranslator = std::make_unique<QTranslator>();
  if (qtTranslator->load(qtQmPath)) {
    QApplication::installTranslator(qtTranslator.get());
    qtTranslator_ = std::move(qtTranslator);
  }

  return true;
}
