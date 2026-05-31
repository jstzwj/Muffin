#include "theme/RenderTheme.h"

#include <QtGlobal>

namespace muffin {

RenderTheme RenderTheme::typoraLike(int zoomPercent) {
  RenderTheme theme;
  theme.setZoomPercent(zoomPercent);
  return theme;
}

int RenderTheme::zoomPercent() const {
  return zoomPercent_;
}

void RenderTheme::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
}

qreal RenderTheme::pageWidth() const {
  return scaled(800.0);
}

qreal RenderTheme::topMargin() const {
  return scaled(38.0);
}

qreal RenderTheme::bottomMargin() const {
  return scaled(56.0);
}

qreal RenderTheme::blockSpacing() const {
  return scaled(8.0);
}

qreal RenderTheme::listIndent() const {
  return scaled(26.0);
}

qreal RenderTheme::blockQuoteIndent() const {
  return scaled(16.0);
}

QFont RenderTheme::paragraphFont() const {
  QFont font(QStringLiteral("Microsoft YaHei UI"));
  font.setStyleStrategy(QFont::PreferDefault);
  font.setPointSizeF(scaled(12.0));
  return font;
}

QFont RenderTheme::headingFont(int level) const {
  static constexpr qreal sizes[] = {24.0, 19.0, 16.0, 14.0, 12.5, 12.0};
  QFont font = paragraphFont();
  font.setBold(true);
  font.setPointSizeF(scaled(sizes[qBound(0, level - 1, 5)]));
  return font;
}

QFont RenderTheme::codeFont() const {
  QFont font(QStringLiteral("Cascadia Mono"));
  font.setStyleHint(QFont::Monospace);
  font.setPointSizeF(scaled(10.5));
  return font;
}

QFont RenderTheme::mathFont() const {
  QFont font(QStringLiteral("Cambria Math"));
  font.setPointSizeF(scaled(12.5));
  return font;
}

QColor RenderTheme::backgroundColor() const {
  return QColor(QStringLiteral("#ffffff"));
}

QColor RenderTheme::textColor() const {
  return QColor(QStringLiteral("#202124"));
}

QColor RenderTheme::mutedTextColor() const {
  return QColor(QStringLiteral("#57606a"));
}

QColor RenderTheme::linkColor() const {
  return QColor(QStringLiteral("#2f6fbd"));
}

QColor RenderTheme::codeBackgroundColor() const {
  return QColor(QStringLiteral("#f6f8fa"));
}

QColor RenderTheme::codeBorderColor() const {
  return QColor(QStringLiteral("#e5e7eb"));
}

QColor RenderTheme::quoteBorderColor() const {
  return QColor(QStringLiteral("#d0d7de"));
}

QColor RenderTheme::tableBorderColor() const {
  return QColor(QStringLiteral("#d0d7de"));
}

QColor RenderTheme::tableHeaderBackgroundColor() const {
  return QColor(QStringLiteral("#f6f8fa"));
}

QColor RenderTheme::selectionColor() const {
  return QColor(QStringLiteral("#d7e8ff"));
}

QMarginsF RenderTheme::codePadding() const {
  return QMarginsF(scaled(12), scaled(10), scaled(12), scaled(10));
}

QMarginsF RenderTheme::tableCellPadding() const {
  return QMarginsF(scaled(10), scaled(7), scaled(10), scaled(7));
}

qreal RenderTheme::scaled(qreal value) const {
  return value * static_cast<qreal>(zoomPercent_) / 100.0;
}

}  // namespace muffin
