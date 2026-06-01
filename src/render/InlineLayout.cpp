#include "render/InlineLayout.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLine>

namespace muffin {
namespace {

QString escaped(QString value) {
  return value.toHtmlEscaped();
}

QString flattenPlainText(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    switch (node.type()) {
      case InlineType::Text:
      case InlineType::Code:
      case InlineType::InlineMath:
      case InlineType::HtmlInline:
        text += node.text();
        break;
      case InlineType::SoftBreak:
        text += QLatin1Char(' ');
        break;
      case InlineType::LineBreak:
        text += QLatin1Char('\n');
        break;
      case InlineType::Image:
        text += node.alt();
        break;
      default:
        text += flattenPlainText(node.children());
        break;
    }
  }
  return text;
}

QString markerForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Code:
      return QStringLiteral("`");
    case InlineType::InlineMath:
      return QStringLiteral("$");
    case InlineType::Emphasis:
      return node.marker().isEmpty() ? QStringLiteral("*") : node.marker();
    case InlineType::Strong:
      return node.marker().isEmpty() ? QStringLiteral("**") : node.marker();
    case InlineType::Strikethrough:
      return QStringLiteral("~~");
    default:
      return {};
  }
}

}  // namespace

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont) {
  build(inlines, theme, width, baseFont, BuildOptions{});
}

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont,
    BuildOptions options) {
  build(inlines, InlineProjection::markdownForInlines(inlines), theme, width, baseFont, options);
}

void InlineLayout::build(
    const QVector<InlineNode>& inlines,
    QString sourceText,
    const RenderTheme& theme,
    qreal width,
    const QFont& baseFont,
    BuildOptions options) {
  plainText_ = flattenPlainText(inlines);
  offsetMap_.clear();
  displayText_.clear();
  projection_ = InlineProjection(inlines, std::move(sourceText), options.activeSourceOffset);
  qsizetype visibleOffset = 0;
  html_ = renderInlines(inlines, theme, visibleOffset, options);
  document_ = std::make_unique<QTextDocument>();
  document_->setDefaultFont(baseFont);
  document_->setDocumentMargin(0);
  document_->setTextWidth(qMax<qreal>(1.0, width));
  document_->setHtml(QStringLiteral(
                         "<html><head><style>"
                         "body { margin:0; color:%1; }"
                         "p { margin:0; line-height:1.45; }"
                         "a { color:%2; text-decoration:none; }"
                         "code { font-family:'Cascadia Mono','Consolas',monospace; background:%3;"
                         " border:1px solid %4; padding:1px 4px; }"
                         ".math { font-family:'Cambria Math','Times New Roman',serif; }"
                         "</style></head><body><p>%5</p></body></html>")
                         .arg(
                             cssColor(theme.textColor()),
                             cssColor(theme.linkColor()),
                             cssColor(theme.codeBackgroundColor()),
                             cssColor(theme.codeBorderColor()),
                             html_));
  size_ = document_->size();
}

QSizeF InlineLayout::size() const {
  return size_;
}

qreal InlineLayout::height() const {
  return size_.height();
}

void InlineLayout::paint(QPainter& painter, QPointF origin) const {
  if (!document_) {
    return;
  }

  painter.save();
  painter.translate(origin);
  QAbstractTextDocumentLayout::PaintContext context;
  document_->documentLayout()->draw(&painter, context);
  painter.restore();
}

qsizetype InlineLayout::hitTestTextOffset(QPointF localPos) const {
  if (!document_) {
    return 0;
  }
  const int position = document_->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
  return visibleOffsetForDisplayOffset(position);
}

qsizetype InlineLayout::hitTestSourceOffset(QPointF localPos) const {
  if (!document_) {
    return 0;
  }
  const int position = document_->documentLayout()->hitTest(localPos, Qt::FuzzyHit);
  qsizetype sourceOffset = -1;
  if (projection_.sourceOffsetForDisplayOffset(position, sourceOffset)) {
    return sourceOffset;
  }
  return visibleOffsetForDisplayOffset(position);
}

QRectF InlineLayout::cursorRect(qsizetype textOffset) const {
  if (!document_) {
    return {};
  }
  const int displayOffset = static_cast<int>(displayOffsetForVisibleOffset(textOffset));
  const int position = qBound(0, displayOffset, qMax(0, document_->characterCount() - 1));
  const QTextBlock block = document_->findBlock(position);
  if (!block.isValid() || !block.layout()) {
    return {};
  }

  const int relativePosition = qBound(0, position - block.position(), block.length());
  const QTextLine line = block.layout()->lineForTextPosition(relativePosition);
  if (!line.isValid()) {
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    return QRectF(blockRect.left(), blockRect.top(), 1.0, blockRect.height());
  }

  const qreal x = line.cursorToX(relativePosition);
  const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
  return QRectF(blockRect.left() + x, blockRect.top() + line.y(), 1.0, line.height());
}

QRectF InlineLayout::cursorRectForSourceOffset(qsizetype sourceOffset) const {
  qsizetype displayOffset = -1;
  if (!projection_.displayOffsetForSourceOffset(sourceOffset, displayOffset)) {
    return cursorRect(sourceOffset);
  }
  if (!document_) {
    return {};
  }
  const int position = qBound(0, static_cast<int>(displayOffset), qMax(0, document_->characterCount() - 1));
  const QTextBlock block = document_->findBlock(position);
  if (!block.isValid() || !block.layout()) {
    return {};
  }

  const int relativePosition = qBound(0, position - block.position(), block.length());
  const QTextLine line = block.layout()->lineForTextPosition(relativePosition);
  if (!line.isValid()) {
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    return QRectF(blockRect.left(), blockRect.top(), 1.0, blockRect.height());
  }

  const qreal x = line.cursorToX(relativePosition);
  const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
  return QRectF(blockRect.left() + x, blockRect.top() + line.y(), 1.0, line.height());
}

QVector<QRectF> InlineLayout::selectionRects(qsizetype startOffset, qsizetype endOffset) const {
  QVector<QRectF> rects;
  if (!document_) {
    return rects;
  }

  const int start = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMin(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  const int end = qBound(0, static_cast<int>(displayOffsetForVisibleOffset(qMax(startOffset, endOffset))), static_cast<int>(displayText_.size()));
  if (start == end) {
    return rects;
  }

  for (QTextBlock block = document_->begin(); block.isValid(); block = block.next()) {
    QTextLayout* layout = block.layout();
    if (!layout) {
      continue;
    }
    const QRectF blockRect = document_->documentLayout()->blockBoundingRect(block);
    const int blockStart = block.position();
    const int blockEnd = blockStart + block.length();
    const int localStart = qMax(0, start - blockStart);
    const int localEnd = qMin(block.length(), end - blockStart);
    if (localStart >= localEnd || end <= blockStart || start >= blockEnd) {
      continue;
    }

    for (int i = 0; i < layout->lineCount(); ++i) {
      const QTextLine line = layout->lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(localStart, lineStart);
      const int rangeEnd = qMin(localEnd, lineEnd);
      if (rangeStart >= rangeEnd) {
        continue;
      }

      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      rects.push_back(QRectF(blockRect.left() + qMin(x1, x2), blockRect.top() + line.y(), qAbs(x2 - x1), line.height()));
    }
  }
  return rects;
}

QString InlineLayout::plainText() const {
  return plainText_;
}

QString InlineLayout::html() const {
  return html_;
}

const InlineProjection& InlineLayout::projection() const {
  return projection_;
}

QString InlineLayout::renderInlines(const QVector<InlineNode>& inlines, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options) {
  QString html;
  for (const InlineNode& node : inlines) {
    html += renderInline(node, theme, visibleOffset, options);
  }
  return html;
}

QString InlineLayout::renderInline(const InlineNode& node, const RenderTheme& theme, qsizetype& visibleOffset, BuildOptions options) {
  const QString visible = flattenPlainText(QVector<InlineNode>{node});
  const qsizetype visibleStart = visibleOffset;
  const qsizetype visibleEnd = visibleStart + visible.size();
  const QString marker = markerForInline(node);
  const bool expandable = !marker.isEmpty();
  const bool active = expandable && options.activeTextOffset >= visibleStart && options.activeTextOffset <= visibleEnd;
  auto appendDisplay = [&](QString text, qsizetype mapVisibleStart, qsizetype mapVisibleEnd) {
    const qsizetype displayStart = displayText_.size();
    displayText_ += text;
    offsetMap_.push_back(OffsetMapEntry{displayStart, displayText_.size(), mapVisibleStart, mapVisibleEnd});
    return escaped(text);
  };
  const QString markerHtml = QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(cssColor(theme.mutedTextColor()));

  switch (node.type()) {
    case InlineType::Text: {
      const QString html = appendDisplay(node.text(), visibleStart, visibleEnd);
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::SoftBreak: {
      const QString html = appendDisplay(QStringLiteral(" "), visibleStart, visibleEnd);
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::LineBreak: {
      appendDisplay(QStringLiteral("\n"), visibleStart, visibleEnd);
      visibleOffset = visibleEnd;
      return QStringLiteral("<br/>");
    }
    case InlineType::Code: {
      QString html;
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleStart, visibleStart);
      }
      html += QStringLiteral("<code>%1</code>").arg(appendDisplay(node.text(), visibleStart, visibleEnd));
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleEnd, visibleEnd);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough: {
      QString html;
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleStart, visibleStart);
      }
      const QString inner = renderInlines(node.children(), theme, visibleOffset, options);
      const QString tag = node.type() == InlineType::Emphasis ? QStringLiteral("em")
                          : node.type() == InlineType::Strong ? QStringLiteral("strong")
                                                              : QStringLiteral("s");
      html += QStringLiteral("<%1>%2</%1>").arg(tag, inner);
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleEnd, visibleEnd);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::Link: {
      const QString inner = renderInlines(node.children(), theme, visibleOffset, options);
      visibleOffset = visibleEnd;
      return QStringLiteral("<a href=\"%1\">%2</a>").arg(escaped(node.href()), inner);
    }
    case InlineType::Image: {
      visibleOffset = visibleEnd;
      return QStringLiteral("<span style=\"color:%1;\">[%2]</span>").arg(cssColor(theme.mutedTextColor()), appendDisplay(node.alt(), visibleStart, visibleEnd));
    }
    case InlineType::InlineMath: {
      QString html;
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleStart, visibleStart);
      }
      html += QStringLiteral("<span class=\"math\">%1</span>").arg(appendDisplay(node.text(), visibleStart, visibleEnd));
      if (active) {
        html += markerHtml.arg(escaped(marker));
        appendDisplay(marker, visibleEnd, visibleEnd);
      }
      visibleOffset = visibleEnd;
      return html;
    }
    case InlineType::HtmlInline: {
      const QString html = appendDisplay(node.text(), visibleStart, visibleEnd);
      visibleOffset = visibleEnd;
      return html;
    }
    default:
      return renderInlines(node.children(), theme, visibleOffset, options);
  }
}

QString InlineLayout::cssColor(const QColor& color) const {
  return color.name(QColor::HexRgb);
}

qsizetype InlineLayout::visibleOffsetForDisplayOffset(qsizetype displayOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, displayOffset, plainText_.size());
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  for (const OffsetMapEntry& entry : offsetMap_) {
    if (displayOffset <= entry.displayEnd) {
      if (entry.visibleEnd <= entry.visibleStart || entry.displayEnd <= entry.displayStart) {
        return entry.visibleStart;
      }
      const qsizetype delta = qBound<qsizetype>(0, displayOffset - entry.displayStart, entry.visibleEnd - entry.visibleStart);
      return qBound<qsizetype>(entry.visibleStart, entry.visibleStart + delta, entry.visibleEnd);
    }
  }
  return plainText_.size();
}

qsizetype InlineLayout::displayOffsetForVisibleOffset(qsizetype visibleOffset) const {
  if (offsetMap_.isEmpty()) {
    return qBound<qsizetype>(0, visibleOffset, plainText_.size());
  }
  visibleOffset = qBound<qsizetype>(0, visibleOffset, plainText_.size());
  for (const OffsetMapEntry& entry : offsetMap_) {
    if (visibleOffset <= entry.visibleEnd) {
      if (entry.visibleEnd <= entry.visibleStart || entry.displayEnd <= entry.displayStart) {
        continue;
      }
      const qsizetype delta = qBound<qsizetype>(0, visibleOffset - entry.visibleStart, entry.displayEnd - entry.displayStart);
      return qBound<qsizetype>(entry.displayStart, entry.displayStart + delta, entry.displayEnd);
    }
  }
  return displayText_.size();
}

}  // namespace muffin
