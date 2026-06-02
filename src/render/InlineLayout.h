#pragma once

#include "document/InlineNode.h"
#include "document/InlineProjection.h"
#include "editor/CursorPosition.h"
#include "theme/RenderTheme.h"

#include <QTextDocument>
#include <QTextLayout>
#include <QString>
#include <QVector>

#include <memory>

namespace muffin {

class InlineLayout {
public:
  enum class InlineGeometryBackend {
    QTextDocument,
    QTextLayout
  };

  struct BuildOptions {
    InlineProjectionState projectionState;
    InlineGeometryBackend geometryBackend = InlineGeometryBackend::QTextLayout;
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
  QSizeF textLayoutSize() const;
  QRectF textLayoutCursorRect(qsizetype textOffset) const;
  qsizetype textLayoutHitTestTextOffset(QPointF localPos) const;
  QVector<QRectF> textLayoutSelectionRects(qsizetype startOffset, qsizetype endOffset) const;

  QString plainText() const;
  QString displayText() const;
  QString documentText() const;
  QString html() const;

private:
  struct OffsetMapEntry {
    qsizetype displayStart = 0;
    qsizetype displayEnd = 0;
    qsizetype visibleStart = 0;
    qsizetype visibleEnd = 0;
  };

  struct ProjectionSyntaxSpanRef {
    const InlineProjectionSpan* span = nullptr;
    QString text;

    bool isValid() const { return span != nullptr && !text.isEmpty(); }
  };

  QString renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options);
  QString renderInline(const InlineNode& node, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options);
  ProjectionSyntaxSpanRef projectionSyntaxSpan(InlineType type, InlineSpanKind kind, qsizetype visiblePosition) const;
  QString renderProjectionSyntaxSpan(InlineType type, InlineSpanKind kind, qsizetype visiblePosition, const RenderTheme& theme) const;
  void buildOffsetMapFromProjection();
  void buildTextLayout(const RenderTheme& theme, qreal width, const QFont& baseFont);
  void paintTextLayoutCodeSpans(QPainter& painter, QPointF origin) const;
  QVector<QTextLayout::FormatRange> textLayoutFormats(const RenderTheme& theme, const QFont& baseFont) const;
  QString cssColor(const QColor& color) const;
  qsizetype visibleOffsetForDisplayOffset(qsizetype displayOffset) const;
  qsizetype displayOffsetForVisibleOffset(qsizetype visibleOffset) const;
  qsizetype textLayoutDisplayOffsetForPoint(QPointF localPos) const;
  QRectF textLayoutCursorRectForDisplayOffset(qsizetype displayOffset) const;

  std::unique_ptr<QTextDocument> document_;
  std::unique_ptr<QTextLayout> textLayout_;
  QSizeF size_;
  QSizeF textLayoutSize_;
  InlineGeometryBackend geometryBackend_ = InlineGeometryBackend::QTextLayout;
  QColor textLayoutCodeBackgroundColor_;
  QColor textLayoutCodeBorderColor_;
  QString html_;
  QString plainText_;
  QString displayText_;
  QVector<OffsetMapEntry> offsetMap_;
  InlineProjection projection_;
};

}  // namespace muffin
