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
      {QStringLiteral("zh_CN"), QStringLiteral("\u7B80\u4F53\u4E2D\u6587"), QStringLiteral("Simplified Chinese")},
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
    const QString system = QLocale::system().name();
    if (system.startsWith(QStringLiteral("zh"))) {
      return QStringLiteral("zh_CN");
    }
    return QStringLiteral("en");
  }
  if (code == QStringLiteral("zh") || code == QStringLiteral("zh-Hans") || code == QStringLiteral("zh_CN")) {
    return QStringLiteral("zh_CN");
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
