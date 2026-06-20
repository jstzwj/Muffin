#include "theme/RenderTheme.h"

#include <QFontDatabase>
#include <QStringList>
#include <QtGlobal>

#include <initializer_list>

namespace muffin {
namespace {

QString firstAvailableFontFamily(std::initializer_list<QString> candidates) {
  const QStringList availableFamilies = QFontDatabase::families();
  for (const QString& candidate : candidates) {
    for (const QString& family : availableFamilies) {
      if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
        return family;
      }
    }
  }
  const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  return systemFamily.isEmpty() ? QStringLiteral("sans-serif") : systemFamily;
}

}  // namespace

RenderTheme RenderTheme::defaultTheme(int zoomPercent) {
  return github(zoomPercent);
}

RenderTheme RenderTheme::fromDefinition(const ThemeDefinition& definition, int zoomPercent, int fontSizePx) {
  const ThemeColors& c = definition.colors;
  RenderTheme t;
  t.backgroundColor_ = c.background;
  t.textColor_ = c.text;
  t.mutedTextColor_ = c.muted;
  t.linkColor_ = c.link;
  t.codeBackgroundColor_ = c.codeBackground;
  t.highlightBackgroundColor_ = c.highlight;
  t.codeBorderColor_ = c.codeBorder;
  t.quoteBorderColor_ = c.quoteBorder;
  t.tableBorderColor_ = c.tableBorder;
  t.tableHeaderBackgroundColor_ = c.tableHeaderBackground;
  t.tableAlternateBackgroundColor_ = c.tableAlternateBackground;
  t.selectionColor_ = c.selection;
  // Spell-check red isn't part of the theme JSON — pick the readable variant for
  // the page tone (matches the night() factory's lighter red on dark pages).
  t.spellCheckColor_ = c.isDark ? QColor(QStringLiteral("#ff6a6a")) : QColor(QStringLiteral("#d1242f"));
  t.serifBody_ = c.serifBody;
  t.setZoomPercent(zoomPercent);
  t.setFontSizePx(fontSizePx);
  return t;
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
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#efe3ce"));
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
  theme.highlightBackgroundColor_ = QColor(QStringLiteral("#3a341a"));
  theme.codeBorderColor_ = QColor(QStringLiteral("#3d444d"));
  theme.quoteBorderColor_ = QColor(QStringLiteral("#56616d"));
  theme.tableBorderColor_ = QColor(QStringLiteral("#3d444d"));
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#303b4a"));
  theme.tableAlternateBackgroundColor_ = QColor(QStringLiteral("#242a31"));
  theme.selectionColor_ = QColor(QStringLiteral("#264f78"));
  theme.spellCheckColor_ = QColor(QStringLiteral("#ff6a6a"));
  theme.setZoomPercent(zoomPercent);
  return theme;
}

RenderTheme RenderTheme::pixyll(int zoomPercent) {
  RenderTheme theme;
  theme.backgroundColor_ = QColor(QStringLiteral("#fafaf7"));
  theme.textColor_ = QColor(QStringLiteral("#2c2c2c"));
  theme.mutedTextColor_ = QColor(QStringLiteral("#6a6a6a"));
  theme.linkColor_ = QColor(QStringLiteral("#0e8a7a"));
  theme.codeBackgroundColor_ = QColor(QStringLiteral("#f0eee6"));
  theme.codeBorderColor_ = QColor(QStringLiteral("#ddd9cc"));
  theme.quoteBorderColor_ = QColor(QStringLiteral("#c2b280"));
  theme.tableBorderColor_ = QColor(QStringLiteral("#ddd9cc"));
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#eef3ec"));
  theme.tableAlternateBackgroundColor_ = QColor(QStringLiteral("#f4f2ea"));
  theme.selectionColor_ = QColor(QStringLiteral("#cdeae3"));
  theme.serifBody_ = true;  // keep the factory in sync with fromDefinition
  theme.setZoomPercent(zoomPercent);
  return theme;
}

RenderTheme RenderTheme::whitey(int zoomPercent) {
  RenderTheme theme;
  theme.backgroundColor_ = QColor(QStringLiteral("#fcfcfc"));
  theme.textColor_ = QColor(QStringLiteral("#4a4a4a"));
  theme.mutedTextColor_ = QColor(QStringLiteral("#8a8a8a"));
  theme.linkColor_ = QColor(QStringLiteral("#6a7d9a"));
  theme.codeBackgroundColor_ = QColor(QStringLiteral("#f4f4f4"));
  theme.codeBorderColor_ = QColor(QStringLiteral("#ececec"));
  theme.quoteBorderColor_ = QColor(QStringLiteral("#d4d4d4"));
  theme.tableBorderColor_ = QColor(QStringLiteral("#ececec"));
  theme.tableHeaderBackgroundColor_ = QColor(QStringLiteral("#f2f5f8"));
  theme.tableAlternateBackgroundColor_ = QColor(QStringLiteral("#f8f8f8"));
  theme.selectionColor_ = QColor(QStringLiteral("#e0e8f0"));
  theme.setZoomPercent(zoomPercent);
  return theme;
}

int RenderTheme::zoomPercent() const {
  return zoomPercent_;
}

void RenderTheme::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
}

int RenderTheme::fontSizePx() const {
  return fontSizePx_;
}

void RenderTheme::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
}

qreal RenderTheme::pageWidth() const {
  return scaled(860.0);
}

qreal RenderTheme::topMargin() const {
  return scaled(30.0);
}

qreal RenderTheme::bottomMargin() const {
  return scaled(70.0);
}

qreal RenderTheme::blockSpacing() const {
  return scaled(11.0);
}

qreal RenderTheme::listIndent() const {
  return scaled(30.0);
}

qreal RenderTheme::blockQuoteIndent() const {
  return scaled(16.0);
}

QFont RenderTheme::paragraphFont() const {
  static const QString sansFamily = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Microsoft YaHei UI"),
      QStringLiteral("Segoe UI"),
      QStringLiteral("Arial"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("PingFang SC"),
      QStringLiteral("Hiragino Sans GB"),
      QStringLiteral("Helvetica Neue"),
      QStringLiteral("Arial"),
#else
      QStringLiteral("Noto Sans CJK SC"),
      QStringLiteral("Noto Sans"),
      QStringLiteral("DejaVu Sans"),
      QStringLiteral("Arial"),
#endif
  });
  // Serif body (Pixyll). Qt substitutes an available CJK face for any glyphs
  // the serif family lacks, so mixed CJK+Latin content still renders.
  static const QString serifFamily = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Georgia"),
      QStringLiteral("Cambria"),
      QStringLiteral("Times New Roman"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("New York"),
      QStringLiteral("Times New Roman"),
      QStringLiteral("Georgia"),
#else
      QStringLiteral("Noto Serif"),
      QStringLiteral("DejaVu Serif"),
      QStringLiteral("Times New Roman"),
#endif
      QStringLiteral("serif"),
  });
  QFont font(serifBody_ ? serifFamily : sansFamily);
  font.setStyleStrategy(QFont::PreferDefault);
  font.setPointSizeF(scaledFont(12.0));
  return font;
}

QFont RenderTheme::headingFont(int level) const {
  static constexpr qreal sizes[] = {24.0, 19.0, 16.0, 14.0, 12.5, 12.0};
  QFont font = paragraphFont();
  font.setBold(true);
  font.setPointSizeF(scaledFont(sizes[qBound(0, level - 1, 5)]));
  return font;
}

QFont RenderTheme::codeFont() const {
  static const QString codeFamily = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Lucida Console"),
      QStringLiteral("Consolas"),
      QStringLiteral("Courier"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("Menlo"),
      QStringLiteral("Monaco"),
      QStringLiteral("Courier New"),
#else
      QStringLiteral("DejaVu Sans Mono"),
      QStringLiteral("Noto Sans Mono"),
      QStringLiteral("Liberation Mono"),
#endif
      QStringLiteral("monospace"),
  });
  QFont font(codeFamily);
  font.setStyleHint(QFont::Monospace);
  font.setPointSizeF(scaledFont(10.8));
  return font;
}

qreal RenderTheme::codeLineHeight() const {
  return scaledFont(23.04);
}

QFont RenderTheme::mathFont() const {
  static const QString mathFamily = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Cambria Math"),
      QStringLiteral("Segoe UI Symbol"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("STIX Two Math"),
      QStringLiteral("STIXGeneral"),
      QStringLiteral("Apple Symbols"),
#else
      QStringLiteral("STIX Two Math"),
      QStringLiteral("Latin Modern Math"),
      QStringLiteral("DejaVu Math TeX Gyre"),
#endif
  });
  QFont font(mathFamily);
  font.setPointSizeF(scaledFont(12.5));
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

QColor RenderTheme::highlightBackgroundColor() const {
  return highlightBackgroundColor_;
}

QColor RenderTheme::codeBorderColor() const {
  return codeBorderColor_;
}

QColor RenderTheme::quoteBorderColor() const {
  return quoteBorderColor_;
}

QColor RenderTheme::alertAccent(AlertKind kind) const {
  switch (kind) {
    case AlertKind::Note:
      return QColor(QStringLiteral("#0969da"));      // blue
    case AlertKind::Tip:
      return QColor(QStringLiteral("#1a7f37"));      // green
    case AlertKind::Important:
      return QColor(QStringLiteral("#8250df"));      // purple
    case AlertKind::Warning:
      return QColor(QStringLiteral("#9a6700"));      // amber
    case AlertKind::Caution:
      return QColor(QStringLiteral("#cf222e"));      // red
    case AlertKind::None:
      break;
  }
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

QColor RenderTheme::spellCheckColor() const {
  return spellCheckColor_;
}

QColor RenderTheme::codeHighlightColor(CodeHighlightRole role) const {
  const bool dark = backgroundColor_.lightness() < 128;
  switch (role) {
    case CodeHighlightRole::Comment:
      return dark ? QColor(QStringLiteral("#8b949e")) : QColor(QStringLiteral("#8c8c8c"));
    case CodeHighlightRole::Keyword:
      return dark ? QColor(QStringLiteral("#ff7b72")) : QColor(QStringLiteral("#9b008b"));
    case CodeHighlightRole::Preprocessor:
      return dark ? QColor(QStringLiteral("#ff7b72")) : QColor(QStringLiteral("#202124"));
    case CodeHighlightRole::String:
      return dark ? QColor(QStringLiteral("#a5d6ff")) : QColor(QStringLiteral("#a31515"));
    case CodeHighlightRole::Number:
    case CodeHighlightRole::Constant:
      return dark ? QColor(QStringLiteral("#79c0ff")) : QColor(QStringLiteral("#1a4fb5"));
    case CodeHighlightRole::Function:
      return dark ? QColor(QStringLiteral("#d2a8ff")) : QColor(QStringLiteral("#0000a8"));
    case CodeHighlightRole::Type:
      return dark ? QColor(QStringLiteral("#ffa657")) : QColor(QStringLiteral("#008000"));
    case CodeHighlightRole::Variable:
      return textColor_;
    case CodeHighlightRole::Property:
      return dark ? QColor(QStringLiteral("#7ee787")) : QColor(QStringLiteral("#795e26"));
    case CodeHighlightRole::Operator:
    case CodeHighlightRole::Punctuation:
      return dark ? mutedTextColor_ : QColor(QStringLiteral("#3f3f3f"));
    case CodeHighlightRole::Escape:
      return dark ? QColor(QStringLiteral("#f2cc60")) : QColor(QStringLiteral("#b000b0"));
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

qreal RenderTheme::scaledFont(qreal value) const {
  return scaled(value) * static_cast<qreal>(fontSizePx_) / 16.0;
}

}  // namespace muffin
