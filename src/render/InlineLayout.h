#pragma once

#include "document/InlineNode.h"
#include "document/InlineProjection.h"
#include "editor/CursorPosition.h"
#include "theme/RenderTheme.h"

#include <QTextDocument>
#include <QString>
#include <QVector>

#include <memory>

namespace muffin {

class InlineLayout {
public:
  struct BuildOptions {
    qsizetype activeTextOffset = -1;
    qsizetype activeSourceOffset = -1;
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
  QString html() const;

private:
  struct OffsetMapEntry {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
  };

  QString renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options);
  QString renderInline(const InlineNode& node, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options);
  QString cssColor(const QColor& color) const;
  qsizetype visibleOffsetForDisplayOffset(qsizetype displayOffset) const;
  qsizetype displayOffsetForVisibleOffset(qsizetype visibleOffset) const;

  std::unique_ptr<QTextDocument> document_;
  QSizeF size_;
  QString html_;
  QString plainText_;
  QString displayText_;
  QVector<OffsetMapEntry> offsetMap_;
  InlineProjection projection_;
};

}  // namespace muffin
