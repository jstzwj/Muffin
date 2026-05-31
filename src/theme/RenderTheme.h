#pragma once

#include "document/MarkdownTypes.h"

#include <QColor>
#include <QFont>
#include <QMarginsF>

namespace muffin {

class RenderTheme {
public:
  static RenderTheme typoraLike(int zoomPercent = 100);

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
  QColor selectionColor() const;

  QMarginsF codePadding() const;
  QMarginsF tableCellPadding() const;

private:
  qreal scaled(qreal value) const;

  int zoomPercent_ = 100;
};

}  // namespace muffin
