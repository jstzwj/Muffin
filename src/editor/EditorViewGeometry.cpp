#include "editor/EditorViewGeometry.h"

#include "unicode/WordBoundary.h"

#include <QFontMetricsF>
#include <QSettings>
#include <QTextLayout>
#include <QTextOption>
#include <QStringList>

namespace muffin {
namespace {
// markdown/codeBlockWrap (default on): mirrors the same-named file-local helpers in
// BlockLayout.cpp / BlockLayoutBuilder.cpp so the caret-geometry path agrees with
// paint and hit-test on whether a code fence soft-wraps its source lines.
bool codeBlockWrapEnabled() {
  return QSettings().value(QStringLiteral("markdown/codeBlockWrap"), true).toBool();
}
}  // namespace
namespace editor_geometry {

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin) {
  const QFontMetricsF metrics(font);
  const qreal lineHeight = qMax<qreal>(14.0, metrics.height());
  offset = qBound<qsizetype>(0, offset, literal.size());

  qsizetype lineStart = 0;
  int line = 0;
  for (qsizetype i = 0; i < offset && i < literal.size(); ++i) {
    if (literal.at(i) == QLatin1Char('\n')) {
      ++line;
      lineStart = i + 1;
    }
  }

  const qreal x = metrics.horizontalAdvance(literal.mid(lineStart, offset - lineStart));
  return QRectF(origin.x() + x, origin.y() + line * lineHeight, 1.0, lineHeight);
}

QRectF literalCursorRectForOffset(const QString& literal, qsizetype offset, const QFont& font, QPointF origin, qreal width,
                                  qreal lineHeight, bool wrap) {
  const QFontMetricsF metrics(font);
  const qreal fallbackHeight = qMax<qreal>(14.0, lineHeight);
  offset = qBound<qsizetype>(0, offset, literal.size());

  QTextOption option;
  option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);

  const QStringList physicalLines = literal.isEmpty() ? QStringList{QString()} : literal.split(QLatin1Char('\n'));
  const qreal lineWidth = qMax<qreal>(1.0, width);
  qreal y = 0.0;
  qsizetype globalStart = 0;
  QRectF fallback(origin.x(), origin.y(), 1.0, fallbackHeight);

  for (const QString& sourceLine : physicalLines) {
    const QString lineText = sourceLine.isEmpty() ? QStringLiteral(" ") : sourceLine;
    QTextLayout layout(lineText, font);
    layout.setTextOption(option);
    layout.beginLayout();
    bool producedLine = false;
    while (true) {
      QTextLine textLine = layout.createLine();
      if (!textLine.isValid()) {
        break;
      }
      textLine.setLineWidth(lineWidth);
      const qreal height = qMax<qreal>(fallbackHeight, textLine.height());
      textLine.setPosition(QPointF(0.0, y + (height - textLine.height()) * 0.5));
      producedLine = true;
      const qsizetype visualStart = globalStart + textLine.textStart();
      const qsizetype visualLength = qMin<qsizetype>(textLine.textLength(), sourceLine.size() - textLine.textStart());
      const qsizetype visualEnd = visualStart + visualLength;
      fallback = QRectF(origin.x() + metrics.horizontalAdvance(sourceLine), origin.y() + y, 1.0, height);
      if (offset >= visualStart && offset <= visualEnd) {
        const qsizetype localOffset = qBound<qsizetype>(visualStart, offset, visualEnd);
        const qreal x = metrics.horizontalAdvance(literal.mid(visualStart, localOffset - visualStart));
        layout.endLayout();
        return QRectF(origin.x() + x, origin.y() + y, 1.0, height);
      }
      y += height;
    }
    layout.endLayout();
    if (!producedLine) {
      if (offset == globalStart) {
        return QRectF(origin.x(), origin.y() + y, 1.0, fallbackHeight);
      }
      y += fallbackHeight;
    }
    globalStart += sourceLine.size() + 1;
  }
  return fallback;
}

bool isSelectableZone(HitTestResult::Zone zone) {
  return zone == HitTestResult::Zone::Text || zone == HitTestResult::Zone::Code || zone == HitTestResult::Zone::Math ||
         zone == HitTestResult::Zone::Html || zone == HitTestResult::Zone::FrontMatter || zone == HitTestResult::Zone::TableCell;
}

bool isDragSelectableZone(HitTestResult::Zone zone) {
  return isSelectableZone(zone) || zone == HitTestResult::Zone::BlockAfter;
}

QPointF tableCellTextOrigin(const BlockLayout::TableCellLayout& cell, const RenderTheme& theme) {
  const QRectF contentRect = cell.rect.marginsRemoved(theme.tableCellPadding());
  qreal textX = contentRect.left();
  if (cell.alignment == TableAlignment::Right) {
    textX = contentRect.right() - cell.text.size().width();
  } else if (cell.alignment == TableAlignment::Center) {
    textX = contentRect.left() + (contentRect.width() - cell.text.size().width()) / 2.0;
  }
  return QPointF(qMax(contentRect.left(), textX), contentRect.top());
}

qsizetype selectableLength(const BlockLayout* block) {
  if (!block) {
    return 0;
  }
  if (const InlineLayout* inlineLayout = block->inlineLayout()) {
    return inlineLayout->plainText().size();
  }
  switch (block->type()) {
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
    case BlockType::MathBlock:
    case BlockType::HtmlBlock:
      return block->literal().size();
    case BlockType::Table:
      return 1;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition: {
      const DefinitionBlock& def = block->definition();
      if (!def.markerRange.isValid()) {
        return 0;
      }
      const qsizetype end = def.sourceRange.isValid()
                                ? def.sourceRange.end
                                : qMax(def.markerRange.end,
                                       qMax(def.destinationRange.end, qMax(def.titleRange.end, def.noteRange.end)));
      return qMax<qsizetype>(0, end - def.markerRange.start);
    }
    default:
      return 0;
  }
}

QRect viewportUpdateRect(QRectF documentRect, qreal scrollY, const QSize& viewportSize) {
  if (documentRect.isNull() || documentRect.isEmpty()) {
    return {};
  }
  documentRect.translate(0, -scrollY);
  return documentRect.adjusted(-4, -4, 4, 4).toAlignedRect().intersected(QRect(QPoint(0, 0), viewportSize));
}

QRect uniteDocumentRectDirty(QRect dirty, QRectF documentRect, qreal scrollY, const QSize& viewportSize) {
  if (documentRect.isNull() || documentRect.isEmpty()) {
    return dirty;
  }
  const QRect viewportRect = viewportUpdateRect(documentRect, scrollY, viewportSize);
  if (viewportRect.isEmpty()) {
    return dirty;
  }
  return dirty.isEmpty() ? viewportRect : dirty.united(viewportRect);
}

QPair<qsizetype, qsizetype> wordRangeAtOffset(const QString& text, qsizetype offset) {
  if (text.isEmpty() || offset < 0 || offset >= text.size()) {
    return {qMax<qsizetype>(0, offset), qMax<qsizetype>(0, offset)};
  }

  const QChar c = text[offset];

  // For word characters, use ICU BreakIterator for dictionary-based segmentation.
  if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
    const auto seg = findWordSegment(text, offset);
    if (seg.isWord && seg.start < seg.end) {
      return {seg.start, seg.end};
    }
    return {offset, offset + 1};
  }

  // For spaces, extend through the contiguous space run.
  qsizetype start = offset;
  qsizetype end = offset + 1;
  if (c.isSpace()) {
    while (start > 0 && text[start - 1].isSpace()) {
      --start;
    }
    while (end < text.size() && text[end].isSpace()) {
      ++end;
    }
  }

  return {start, end};
}

HitTestResult hitForCursorPosition(DocumentLayout& layout, const RenderTheme& theme, CursorPosition position) {
  if (!position.isValid()) {
    return {};
  }

  const BlockLayout* block = layout.block(position.blockId, theme);
  if (!block) {
    return {};
  }

  // Reproduce the virtual trailing-paragraph caret so it survives layout
  // rebuilds (resize, theme change, refresh). Without this, recomputation from
  // a CursorPosition would resolve to the block's real type and snap the caret
  // back inside the last block.
  if (position.afterBlock) {
    HitTestResult hit;
    hit.blockId = position.blockId;
    hit.textNodeId = position.blockId;
    hit.textOffset = 0;
    hit.sourceOffset = -1;
    hit.blockRect = block->rect();
    hit.zone = HitTestResult::Zone::BlockAfter;
    hit.cursorRect = DocumentLayout::trailingParagraphCursorRect(*block, theme, layout.pageLeft());
    return hit;
  }

  HitTestResult hit;
  hit.blockId = position.blockId;
  hit.textNodeId = position.text.nodeId.isValid() ? position.text.nodeId : position.blockId;
  hit.textOffset = position.text.textOffset;
  hit.sourceOffset = position.text.sourceOffset;
  hit.blockRect = block->rect();
  hit.zone = HitTestResult::Zone::Block;

  switch (block->type()) {
    case BlockType::Paragraph:
    case BlockType::Heading:
    case BlockType::ListItem:
      hit.zone = position.text.inMeta ? HitTestResult::Zone::Marker : HitTestResult::Zone::Text;
      if (const InlineLayout* inlineLayout = block->inlineLayout()) {
        const qreal textLeft = block->hasListMarker() ? block->rect().left() + block->listContentIndent() : block->rect().left();
        const qsizetype localSourceOffset =
            position.text.sourceOffset >= 0 && block->contentSourceStart() >= 0 ? position.text.sourceOffset - block->contentSourceStart() : -1;
        hit.cursorRect = localSourceOffset >= 0
                             ? inlineLayout->cursorRectForSourceOffset(localSourceOffset).translated(QPointF(textLeft, block->rect().top()))
                             : inlineLayout->cursorRect(position.text.textOffset).translated(QPointF(textLeft, block->rect().top()));
      }
      break;
    case BlockType::FrontMatter:
    case BlockType::CodeFence:
      hit.zone = block->type() == BlockType::FrontMatter ? HitTestResult::Zone::FrontMatter : HitTestResult::Zone::Code;
      {
        const QRectF contentRect = block->literalContentRect(theme);
        // Code fences honour markdown/codeBlockWrap; front-matter always wraps. Mirrors the hit-test
        // path in BlockLayout: without this a long line in a wrap-OFF fence is laid out wrapped and
        // the caret drops onto a phantom second row.
        const bool wrap = block->type() == BlockType::CodeFence ? codeBlockWrapEnabled() : true;
        hit.cursorRect = literalCursorRectForOffset(block->literal(), position.text.textOffset, theme.codeFont(),
                                                    contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), wrap);
      }
      break;
    case BlockType::MathBlock:
      hit.zone = HitTestResult::Zone::Math;
      if (block->literalEditing()) {
        const QRectF contentRect = block->literalContentRect(theme);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), true);
      } else {
        const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, block->literal().size());
        const qreal x = offset <= block->literal().size() / 2 ? block->rect().left() : block->rect().right();
        hit.cursorRect = QRectF(x, block->rect().top(), 1.0, block->rect().height());
      }
      break;
    case BlockType::HtmlBlock:
      hit.zone = HitTestResult::Zone::Html;
      if (block->literalEditing()) {
        const QRectF contentRect = block->literalContentRect(theme);
        hit.cursorRect =
            literalCursorRectForOffset(block->literal(), position.text.textOffset, theme.codeFont(), contentRect.topLeft(), contentRect.width(), theme.codeLineHeight(), true);
      } else {
        const qsizetype offset = qBound<qsizetype>(0, position.text.textOffset, block->literal().size());
        const qreal x = offset <= block->literal().size() / 2 ? block->rect().left() : block->rect().right();
        hit.cursorRect = QRectF(x, block->rect().top(), 1.0, block->rect().height());
      }
      break;
    case BlockType::Table:
      hit.zone = HitTestResult::Zone::TableCell;
      hit.tableRow = -1;
      hit.tableColumn = -1;
      for (int row = 0; row < static_cast<int>(block->tableRows().size()); ++row) {
        const auto& tableRow = block->tableRows().at(static_cast<size_t>(row));
        for (int column = 0; column < static_cast<int>(tableRow.cells.size()); ++column) {
          if (tableRow.cells.at(static_cast<size_t>(column)).nodeId == hit.textNodeId) {
            hit.tableRow = row;
            hit.tableColumn = column;
            break;
          }
        }
        if (hit.tableRow >= 0) {
          break;
        }
      }
      if (hit.tableRow >= 0 && hit.tableColumn >= 0) {
        const QRectF cellRect = block->tableCellRect(hit.tableRow, hit.tableColumn);
        for (const auto& tableRow : block->tableRows()) {
          for (const auto& cell : tableRow.cells) {
            if (cell.nodeId == hit.textNodeId) {
              const QPointF textOrigin = tableCellTextOrigin(cell, theme);
              const qsizetype localSourceOffset =
                  position.text.sourceOffset >= 0 && cell.contentSourceStart >= 0 ? position.text.sourceOffset - cell.contentSourceStart : -1;
              hit.cursorRect = localSourceOffset >= 0
                                   ? cell.text.cursorRectForSourceOffset(localSourceOffset).translated(textOrigin)
                                   : cell.text.cursorRect(position.text.textOffset).translated(textOrigin);
              break;
            }
          }
        }
        if (hit.cursorRect.isEmpty()) {
          hit.cursorRect = QRectF(cellRect.left() + 6.0, cellRect.top() + 4.0, 1.0, qMax<qreal>(14.0, cellRect.height() - 8.0));
        }
      }
      break;
    case BlockType::LinkDefinition:
    case BlockType::FootnoteDefinition:
      hit.zone = HitTestResult::Zone::Text;
      hit.cursorRect = block->definitionCursorRectForSourceOffset(position.text.sourceOffset, theme);
      break;
    default:
      break;
  }

  if (hit.cursorRect.isEmpty()) {
    hit.cursorRect = QRectF(hit.blockRect.left(), hit.blockRect.top(), 1.0, hit.blockRect.height());
  }
  return hit;
}

QVector<const BlockLayout*> blocksBetween(const DocumentLayout& layout, NodeId first, NodeId last) {
  QVector<const BlockLayout*> result;
  if (!first.isValid() || !last.isValid()) {
    return result;
  }

  const qsizetype firstIdx = layout.topLevelIndexFor(first);
  const qsizetype lastIdx = layout.topLevelIndexFor(last);
  if (firstIdx < 0 || lastIdx < 0) {
    return result;
  }

  NodeId startId = first;
  NodeId endId = last;
  qsizetype startIdx = firstIdx;
  qsizetype endIdx = lastIdx;
  if (firstIdx > lastIdx) {
    qSwap(startIdx, endIdx);
    qSwap(startId, endId);
  }

  // Collect promoted top-level blocks (and their nested children) between the endpoints. Un-promoted
  // (offscreen) blocks are skipped — their selection rects are not visible anyway under the lazy layout.
  bool collecting = false;
  const auto collect = [&](const auto& self, const BlockLayout& block) -> void {
    if (block.nodeId() == startId) {
      collecting = true;
    }
    if (collecting) {
      result.push_back(&block);
    }
    if (block.nodeId() == endId) {
      collecting = false;
      return;
    }
    for (const auto& child : block.children()) {
      self(self, *child);
      if (!collecting && !result.isEmpty() && result.last()->nodeId() == endId) {
        return;
      }
    }
  };

  for (qsizetype i = startIdx; i <= endIdx; ++i) {
    const BlockLayout* block = layout.blockIfPromoted(layout.slotNodeId(i));
    if (!block) {
      continue;
    }
    collect(collect, *block);
    if (!result.isEmpty() && result.last()->nodeId() == endId) {
      break;
    }
  }
  return result;
}

bool blockComesBefore(const DocumentLayout& layout, NodeId first, NodeId second) {
  if (first == second) {
    return true;
  }
  // Order by top-level slot index (robust to un-promoted offscreen blocks); for two ids nested in
  // the same top-level block fall back to the promoted-tree traversal.
  const qsizetype a = layout.topLevelIndexFor(first);
  const qsizetype b = layout.topLevelIndexFor(second);
  if (a >= 0 && b >= 0 && a != b) {
    return a < b;
  }
  const QVector<const BlockLayout*> range = blocksBetween(layout, first, second);
  return !range.isEmpty() && range.first()->nodeId() == first;
}

}  // namespace editor_geometry
}  // namespace muffin
