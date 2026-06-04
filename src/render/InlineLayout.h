#pragma once

#include "document/InlineNode.h"
#include "document/InlineProjection.h"
#include "editor/CursorPosition.h"
#include "math/MathRenderer.h"
#include "math/MathRenderNode.h"
#include "theme/RenderTheme.h"

#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <QString>
#include <QVector>

#include <memory>

class QPainter;

namespace muffin {

class InlineLayout {
public:
  struct BuildOptions {
    InlineProjectionState projectionState;
  };

  InlineLayout() = default;
  InlineLayout(const InlineLayout&) = delete;
  InlineLayout& operator=(const InlineLayout&) = delete;
  InlineLayout(InlineLayout&&) noexcept = default;
  InlineLayout& operator=(InlineLayout&&) noexcept = default;

  void build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QFont& baseFont);
  void build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QFont& baseFont, BuildOptions options);
  void build(const QVector<InlineNode>& inlines, QString sourceText, const RenderTheme& theme, qreal width, const QFont& baseFont,
             BuildOptions options);

  QSizeF size() const;
  qreal height() const;
  void paint(QPainter& painter, QPointF origin) const;
  qsizetype hitTestTextOffset(QPointF localPos) const;
  qsizetype hitTestSourceOffset(QPointF localPos) const;
  QRectF cursorRect(qsizetype textOffset) const;
  QRectF cursorRectForSourceOffset(qsizetype sourceOffset) const;
  QVector<QRectF> selectionRects(qsizetype startOffset, qsizetype endOffset) const;

  QString plainText() const;
  QString displayText() const;
  int mathAtomCount() const;

private:
  struct OffsetMapEntry {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
  };

  struct MathAtom {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    qsizetype contentSourceStart = 0;
    qsizetype contentSourceEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
    std::shared_ptr<math::MathLayoutResult> layout;
  };

  void buildOffsetMapFromProjection();
  void buildMathAtoms(const QVector<InlineNode>& inlines, const RenderTheme& theme);
  QString texForInlineMathVisibleRange(const QVector<InlineNode>& inlines, qsizetype visibleStart, qsizetype visibleEnd) const;
  void buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont);
  void paintTextLayoutCodeSpans(QPainter& painter, QPointF origin) const;
  void paintTextLayoutMathAtoms(QPainter& painter, QPointF origin) const;
  QVector<QTextLayout::FormatRange> textLayoutFormats(const RenderTheme& theme, const QFont& baseFont) const;
  qsizetype visibleOffsetForDisplayOffset(qsizetype displayOffset) const;
  qsizetype displayOffsetForVisibleOffset(qsizetype visibleOffset) const;
  qsizetype textLayoutDisplayOffsetForPoint(QPointF localPos) const;
  QRectF textLayoutCursorRectForDisplayOffset(qsizetype displayOffset) const;

  std::unique_ptr<QTextLayout> textLayout_;
  QSizeF size_;
  QColor textLayoutCodeBackgroundColor_;
  QColor textLayoutCodeBorderColor_;
  QString plainText_;
  QString displayText_;
  QVector<OffsetMapEntry> offsetMap_;
  QVector<MathAtom> mathAtoms_;
  InlineProjection projection_;
  math::MathRenderer mathRenderer_;
};

}  // namespace muffin
