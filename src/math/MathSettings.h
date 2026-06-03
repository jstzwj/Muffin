#pragma once

#include <QColor>
#include <QHash>
#include <QString>

#include <functional>
#include <optional>

namespace muffin::math {

struct MathToken;

enum class MathStrictMode {
  Ignore,
  Warn,
  Error
};

struct MathTrustContext {
  QString command;
  QString url;
  QString protocol;
  QString className;
  QString id;
  QString style;
  QString attribute;
  QString value;
  QHash<QString, QString> attributes;
};

struct MathSettings {
  bool displayMode = false;
  bool throwOnError = false;
  MathStrictMode strict = MathStrictMode::Ignore;
  bool trust = false;
  std::function<MathStrictMode(const QString& errorCode, const QString& errorMessage, const MathToken* token)> strictHandler;
  std::function<bool(const MathTrustContext& context)> trustHandler;
  QColor errorColor = QColor(QStringLiteral("#cc0000"));
  qreal maxSize = 1000.0;
  qreal minRuleThickness = 0.04;
  int maxExpand = 1000;

  bool strictEnabled() const;
  void reportNonstrict(const QString& errorCode, const QString& errorMessage, const MathToken* token = nullptr) const;
  bool shouldApplyStrictBehavior(const QString& errorCode, const QString& errorMessage, const MathToken* token = nullptr) const;
  bool isTrusted(const MathTrustContext& context) const;
};

}  // namespace muffin::math
