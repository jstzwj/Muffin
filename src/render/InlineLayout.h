#pragma once

#include "document/InlineNode.h"
#include "editor/CursorPosition.h"
#include "theme/RenderTheme.h"

#include <QTextDocument>
#include <QString>

#include <memory>

namespace muffin {

class InlineLayout {
public:
  InlineLayout() = default;
  InlineLayout(const InlineLayout&) = delete;
  InlineLayout& operator=(const InlineLayout&) = delete;
  InlineLayout(InlineLayout&&) noexcept = default;
  InlineLayout& operator=(InlineLayout&&) noexcept = default;

  void build(const QVector<InlineNode>& inlines, const RenderTheme& theme, qreal width, const QFont& baseFont);

  QSizeF size() const;
  qreal height() const;
  void paint(QPainter& painter, QPointF origin) const;
  qsizetype hitTestTextOffset(QPointF localPos) const;
  QRectF cursorRect(qsizetype textOffset) const;

  QString plainText() const;
  QString html() const;

private:
  QString renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme) const;
  QString renderInline(const InlineNode& node, const RenderTheme& theme) const;
  QString cssColor(const QColor& color) const;

  std::unique_ptr<QTextDocument> document_;
  QSizeF size_;
  QString html_;
  QString plainText_;
};

}  // namespace muffin
