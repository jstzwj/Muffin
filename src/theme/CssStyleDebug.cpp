#include "theme/CssStyleDebug.h"

#include <QColor>
#include <QMarginsF>
#include <QStringList>

namespace muffin {

Q_LOGGING_CATEGORY(themeStyleLog, "muffin.theme.style", QtWarningMsg)
Q_LOGGING_CATEGORY(renderLayoutDebugLog, "muffin.render.layout.debug", QtWarningMsg)

namespace {

QString colorName(const QColor& color) {
  return color.isValid() ? color.name(QColor::HexArgb) : QStringLiteral("<invalid>");
}

QString marginsName(const QMarginsF& margins) {
  return QStringLiteral("l=%1 t=%2 r=%3 b=%4")
      .arg(margins.left())
      .arg(margins.top())
      .arg(margins.right())
      .arg(margins.bottom());
}

}  // namespace

QString formatCssStyleWinner(const CssStyleDebugWinner& winner) {
  QStringList parts;
  parts << QStringLiteral("target=%1").arg(winner.target.isEmpty() ? QStringLiteral("<unknown>") : winner.target);
  parts << QStringLiteral("property=%1").arg(winner.property.isEmpty() ? QStringLiteral("<unknown>") : winner.property);
  if (!winner.selector.isEmpty()) { parts << QStringLiteral("selector=%1").arg(winner.selector); }
  if (!winner.rawValue.isEmpty()) { parts << QStringLiteral("raw=%1").arg(winner.rawValue); }
  if (!winner.resolvedValue.isEmpty()) { parts << QStringLiteral("resolved=%1").arg(winner.resolvedValue); }
  if (!winner.fallbackTier.isEmpty()) { parts << QStringLiteral("tier=%1").arg(winner.fallbackTier); }
  parts << QStringLiteral("spec=%1").arg(winner.specificity);
  parts << QStringLiteral("order=%1").arg(winner.sourceOrder);
  if (winner.important) { parts << QStringLiteral("important"); }
  return parts.join(QStringLiteral(" "));
}

QString formatThemeDefinitionSummary(const ThemeDefinition& definition) {
  const ThemeColors& c = definition.colors;
  const ThemePage& p = definition.page;
  QStringList parts;
  parts << QStringLiteral("id=%1").arg(definition.id);
  parts << QStringLiteral("label=%1").arg(definition.label);
  parts << QStringLiteral("dark=%1").arg(c.isDark ? QStringLiteral("true") : QStringLiteral("false"));
  parts << QStringLiteral("bg=%1").arg(colorName(c.background));
  parts << QStringLiteral("text=%1").arg(colorName(c.text));
  parts << QStringLiteral("muted=%1").arg(colorName(c.muted));
  parts << QStringLiteral("link=%1").arg(colorName(c.link));
  parts << QStringLiteral("codeBg=%1").arg(colorName(c.codeBackground));
  parts << QStringLiteral("viewport=%1").arg(colorName(p.viewportBackground));
  parts << QStringLiteral("page=%1").arg(colorName(p.pageBackground));
  parts << QStringLiteral("pageMaxW=%1").arg(p.pageMaxWidth);
  parts << QStringLiteral("pagePadding=[%1]").arg(marginsName(p.pagePadding));
  parts << QStringLiteral("pageMarginExplicit=%1").arg(p.pageMarginExplicit ? QStringLiteral("true") : QStringLiteral("false"));
  parts << QStringLiteral("pageMargin=[%1]").arg(marginsName(p.pageMargin));
  parts << QStringLiteral("pseudos=%1 backgrounds=%2 hover=%3 keyframes=%4 animations=%5")
               .arg(definition.decorations.pseudos.size())
               .arg(definition.decorations.backgrounds.size())
               .arg(definition.decorations.hoverEffects.size())
               .arg(definition.decorations.keyframes.size())
               .arg(definition.decorations.animations.size());
  return parts.join(QStringLiteral(" "));
}

}  // namespace muffin
