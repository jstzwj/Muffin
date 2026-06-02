#include "theme/RenderTheme.h"

#include <QtGlobal>

namespace muffin {

RenderTheme RenderTheme::typoraLike(int zoomPercent) {
  return github(zoomPercent);
}

RenderTheme RenderTheme::github(int zoomPercent) {
  RenderTheme theme;
  theme.setZoomPercent(zoomPercent);
  return theme;
}

RenderTheme RenderTheme::newsprint(int zoomPercent) {
  RenderTheme theme;
  theme.backgroundColor_ = QColor(QStringLiteral("#fbfaf7"));
  theme.textColor_ = QColor(QStringLiteral("#1f2328"));
  theme.mutedTextColor_ = QColor(QStringLiteral("#6b665d"));
  theme.linkColor_ = QColor(QStringLiteral("#2f6f9f"));
  theme.codeBackgroundColor_ = QColor(QStringLiteral("#f1eee8"));
  theme.codeBorderColor_ = QColor(QStringLiteral("#ded8cc"));
  theme.quoteBorderColor_ = QColor(QStringLiteral("#c8bfae"));
  theme.tableBorderColor_ = QColor(QStringLiteral("#d8d0c2"));
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#eee9df"));
  theme.tableAlternateBackgroundColor_ = QColor(QStringLiteral("#f6f3ed"));
  theme.selectionColor_ = QColor(QStringLiteral("#d9e8ef"));
  theme.setZoomPercent(zoomPercent);
  return theme;
}

RenderTheme RenderTheme::night(int zoomPercent) {
  RenderTheme theme;
  theme.backgroundColor_ = QColor(QStringLiteral("#1f2328"));
  theme.textColor_ = QColor(QStringLiteral("#e6edf3"));
  theme.mutedTextColor_ = QColor(QStringLiteral("#9aa4af"));
  theme.linkColor_ = QColor(QStringLiteral("#7fb4f5"));
  theme.codeBackgroundColor_ = QColor(QStringLiteral("#2b3138"));
  theme.codeBorderColor_ = QColor(QStringLiteral("#3d444d"));
  theme.quoteBorderColor_ = QColor(QStringLiteral("#56616d"));
  theme.tableBorderColor_ = QColor(QStringLiteral("#3d444d"));
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#2b3138"));
  theme.tableAlternateBackgroundColor_ = QColor(QStringLiteral("#242a31"));
  theme.selectionColor_ = QColor(QStringLiteral("#264f78"));
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
  return scaled(48.0);
}

qreal RenderTheme::bottomMargin() const {
  return scaled(56.0);
}

qreal RenderTheme::blockSpacing() const {
  return scaled(11.0);
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
  return backgroundColor_;
}

QColor RenderTheme::textColor() const {
  return textColor_;
}

QColor RenderTheme::mutedTextColor() const {
  return mutedTextColor_;
}

QColor RenderTheme::linkColor() const {
  return linkColor_;
}

QColor RenderTheme::codeBackgroundColor() const {
  return codeBackgroundColor_;
}

QColor RenderTheme::codeBorderColor() const {
  return codeBorderColor_;
}

QColor RenderTheme::quoteBorderColor() const {
  return quoteBorderColor_;
}

QColor RenderTheme::tableBorderColor() const {
  return tableBorderColor_;
}

QColor RenderTheme::tableHeaderBackgroundColor() const {
  return tableHeaderBackgroundColor_;
}

QColor RenderTheme::tableAlternateBackgroundColor() const {
  return tableAlternateBackgroundColor_;
}

QColor RenderTheme::selectionColor() const {
  return selectionColor_;
}

QColor RenderTheme::codeHighlightColor(CodeHighlightRole role) const {
  const bool dark = backgroundColor_.lightness() < 128;
  switch (role) {
    case CodeHighlightRole::Comment:
      return dark ? QColor(QStringLiteral("#8b949e")) : QColor(QStringLiteral("#6a737d"));
    case CodeHighlightRole::Keyword:
    case CodeHighlightRole::Preprocessor:
      return dark ? QColor(QStringLiteral("#ff7b72")) : QColor(QStringLiteral("#d73a49"));
    case CodeHighlightRole::String:
      return dark ? QColor(QStringLiteral("#a5d6ff")) : QColor(QStringLiteral("#032f62"));
    case CodeHighlightRole::Number:
    case CodeHighlightRole::Constant:
      return dark ? QColor(QStringLiteral("#79c0ff")) : QColor(QStringLiteral("#005cc5"));
    case CodeHighlightRole::Function:
      return dark ? QColor(QStringLiteral("#d2a8ff")) : QColor(QStringLiteral("#6f42c1"));
    case CodeHighlightRole::Type:
      return dark ? QColor(QStringLiteral("#ffa657")) : QColor(QStringLiteral("#e36209"));
    case CodeHighlightRole::Variable:
      return textColor_;
    case CodeHighlightRole::Property:
      return dark ? QColor(QStringLiteral("#7ee787")) : QColor(QStringLiteral("#22863a"));
    case CodeHighlightRole::Operator:
    case CodeHighlightRole::Punctuation:
      return mutedTextColor_;
    case CodeHighlightRole::Escape:
      return dark ? QColor(QStringLiteral("#f2cc60")) : QColor(QStringLiteral("#b08800"));
    case CodeHighlightRole::Plain:
    default:
      return textColor_;
  }
}

QMarginsF RenderTheme::codePadding() const {
  return QMarginsF(scaled(12), scaled(10), scaled(12), scaled(10));
}

QMarginsF RenderTheme::tableCellPadding() const {
  return QMarginsF(scaled(12), scaled(6), scaled(12), scaled(6));
}

qreal RenderTheme::scaled(qreal value) const {
  return value * static_cast<qreal>(zoomPercent_) / 100.0;
}

}  // namespace muffin
