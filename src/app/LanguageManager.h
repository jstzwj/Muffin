#pragma once

#include <QObject>
#include <QString>
#include <QTranslator>
#include <QVector>

#include <memory>

namespace muffin {

struct LanguageInfo {
  QString code;
  QString nativeName;
  QString englishName;
};

class LanguageManager final : public QObject {
  Q_OBJECT

public:
  static LanguageManager& instance();

  QVector<LanguageInfo> availableLanguages() const;
  QString currentLanguageCode() const;
  void initialize();
  bool setLanguage(QString code);

signals:
  void languageChanged(QString code);

private:
  explicit LanguageManager(QObject* parent = nullptr);

  static QString settingsKey();
  static QString normalizedCode(QString code);
  static QString resolvedCode(const QString& code);
  bool installTranslator(const QString& code);

  QString currentCode_ = QStringLiteral("en");
  QString selectedCode_ = QStringLiteral("system");
  std::unique_ptr<QTranslator> translator_;
};

}  // namespace muffin
