#pragma once

#include "theme/ThemeDefinition.h"

#include <QLoggingCategory>
#include <QString>

namespace muffin {

Q_DECLARE_LOGGING_CATEGORY(themeStyleLog)
Q_DECLARE_LOGGING_CATEGORY(renderLayoutDebugLog)

struct CssStyleDebugWinner {
  QString target;
  QString property;
  QString selector;
  QString rawValue;
  QString resolvedValue;
  int specificity = 0;
  int sourceOrder = 0;
  bool important = false;
  QString fallbackTier;
};

QString formatCssStyleWinner(const CssStyleDebugWinner& winner);
QString formatThemeDefinitionSummary(const ThemeDefinition& definition);

}  // namespace muffin
