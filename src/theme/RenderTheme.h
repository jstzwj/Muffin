#pragma once

#include "document/MarkdownTypes.h"
#include "render/CodeHighlight.h"

#include <QColor>
#include <QFont>
#include <QMarginsF>

namespace muffin {

class RenderTheme {
public:
  static RenderTheme typoraLike(int zoomPercent = 100);
  static RenderTheme github(int zoomPercent = 100);
  static RenderTheme newsprint(int zoomPercent = 100);
  static RenderTheme night(int zoomPercent = 100);

  int zoomPercent() const;
  void setZoomPercent(int percent);

  qreal pageWidth() const;
  qreal topMargin() const;
  qreal bottomMargin() const;
  qreal blockSpacing() const;
  qreal listIndent() const;
  qreal blockQuoteIndent() const;

  QFont paragraphFont() const;
  QFont headingFont(int level) const;
  QFont codeFont() const;
  QFont mathFont() const;

  QColor backgroundColor() const;
  QColor textColor() const;
  QColor mutedTextColor() const;
  QColor linkColor() const;
  QColor codeBackgroundColor() const;
  QColor codeBorderColor() const;
  QColor quoteBorderColor() const;
  QColor tableBorderColor() const;
  QColor tableHeaderBackgroundColor() const;
  QColor tableAlternateBackgroundColor() const;
  QColor selectionColor() const;
  QColor codeHighlightColor(CodeHighlightRole role) const;

  QMarginsF codePadding() const;
  QMarginsF tableCellPadding() const;

private:
  qreal scaled(qreal value) const;

  int zoomPercent_ = 100;
  QColor backgroundColor_ = QColor(QStringLiteral("#ffffff"));
  QColor textColor_ = QColor(QStringLiteral("#202124"));
  QColor mutedTextColor_ = QColor(QStringLiteral("#57606a"));
  QColor linkColor_ = QColor(QStringLiteral("#4183c4"));
  QColor codeBackgroundColor_ = QColor(QStringLiteral("#f6f8fa"));
  QColor codeBorderColor_ = QColor(QStringLiteral("#e5e7eb"));
  QColor quoteBorderColor_ = QColor(QStringLiteral("#d0d7de"));
  QColor tableBorderColor_ = QColor(QStringLiteral("#dfe2e5"));
  QColor tableHeaderBackgroundColor_ = QColor(QStringLiteral("#f6f8fa"));
  QColor tableAlternateBackgroundColor_ = QColor(QStringLiteral("#f6f8fa"));
  QColor selectionColor_ = QColor(QStringLiteral("#d7e8ff"));
};

}  // namespace muffin
