#include "blocks/table/TableCellSourceEdit.h"

#include "projection/InlineProjection.h"
#include "document/MarkdownNode.h"

namespace muffin {
namespace {

enum class DeleteDirection {
  Backward,
  Forward
};

qsizetype visibleOffsetForTableCellSourceOffset(const QString& content, qsizetype sourceOffset) {
  sourceOffset = qBound<qsizetype>(0, sourceOffset, content.size());
  qsizetype visible = 0;
  qsizetype source = 0;
  while (source < sourceOffset) {
    if (content.at(source) == QLatin1Char('\\') && source + 1 < content.size() &&
        content.at(source + 1) == QLatin1Char('|')) {
      source = qMin(sourceOffset, source + 2);
      ++visible;
      continue;
    }
    if (content.mid(source, 4) == QStringLiteral("<br>")) {
      source = qMin(sourceOffset, source + 4);
      ++visible;
      continue;
    }
    ++source;
    ++visible;
  }
  return visible;
}

bool escapedPipeAt(const QString& content, qsizetype offset) {
  return offset >= 0 && offset + 1 < content.size() && content.at(offset) == QLatin1Char('\\') &&
         content.at(offset + 1) == QLatin1Char('|');
}

bool tableBreakAt(const QString& content, qsizetype offset) {
  return offset >= 0 && offset + 4 <= content.size() && content.mid(offset, 4) == QStringLiteral("<br>");
}

bool tableCellSpecialUnitAt(const QString& content, qsizetype offset, qsizetype& unitStart, qsizetype& unitEnd) {
  if (escapedPipeAt(content, offset)) {
    unitStart = offset;
    unitEnd = offset + 2;
    return true;
  }
  if (tableBreakAt(content, offset)) {
    unitStart = offset;
    unitEnd = offset + 4;
    return true;
  }
  return false;
}

qsizetype tableSpecialSourceExtraBefore(const QString& content, qsizetype sourceOffset) {
  sourceOffset = qBound<qsizetype>(0, sourceOffset, content.size());
  qsizetype extra = 0;
  for (qsizetype offset = 0; offset < sourceOffset;) {
    qsizetype unitStart = 0;
    qsizetype unitEnd = 0;
    if (!tableCellSpecialUnitAt(content, offset, unitStart, unitEnd)) {
      ++offset;
      continue;
    }
    const qsizetype consumed = qMin(sourceOffset, unitEnd) - unitStart;
    extra += qMax<qsizetype>(0, consumed - 1);
    offset = unitEnd;
  }
  return extra;
}

bool tableCellSpecialUnitContaining(
    const QString& content,
    qsizetype sourceOffset,
    DeleteDirection direction,
    qsizetype& unitStart,
    qsizetype& unitEnd) {
  for (qsizetype offset = 0; offset < content.size(); ++offset) {
    if (!tableCellSpecialUnitAt(content, offset, unitStart, unitEnd)) {
      continue;
    }
    if (direction == DeleteDirection::Backward && sourceOffset > unitStart && sourceOffset <= unitEnd) {
      return true;
    }
    if (direction == DeleteDirection::Forward && sourceOffset >= unitStart && sourceOffset < unitEnd) {
      return true;
    }
  }
  return false;
}

// Convert an absolute InlineRange to a local range relative to sourceBase.
InlineRange toLocalRange(InlineRange range, qsizetype sourceBase) {
  if (!range.isValid()) {
    return {};
  }
  if (sourceBase < 0) {
    return range;
  }
  range.start -= sourceBase;
  range.end -= sourceBase;
  return range;
}

// Count inline marker characters that are hidden (not visible) before sourceOffset.
// These are the markdown syntax characters like **, `, [ ], etc. that are invisible
// in the rendered cell text.
qsizetype countHiddenMarkerChars(const QVector<InlineNode>& nodes, qsizetype sourceBase, qsizetype sourceOffset) {
  qsizetype hidden = 0;
  for (const InlineNode& node : nodes) {
    const InlineRange srcAbs = node.sourceRange();
    if (!srcAbs.isValid()) {
      continue;
    }
    const InlineRange src = toLocalRange(srcAbs, sourceBase);
    // Skip nodes entirely before or after the range of interest.
    if (src.end <= 0 || src.start >= sourceOffset) {
      continue;
    }

    switch (node.type()) {
      case InlineType::Strong:
      case InlineType::Emphasis:
      case InlineType::Strikethrough: {
        InlineRange content = toLocalRange(node.contentRange(), sourceBase);
        if (!content.isValid() || content.start < src.start || content.end > src.end) {
          const qsizetype markerLen = qMax<qsizetype>(1, node.marker().size());
          content = InlineRange{src.start + markerLen, src.end - markerLen};
        }
        // Opening marker (e.g., ** before content)
        hidden += qMax<qsizetype>(0, qMin(content.start, sourceOffset) - src.start);
        // Closing marker (e.g., ** after content)
        if (src.end > content.end) {
          hidden += qMax<qsizetype>(0, qMin(src.end, sourceOffset) - content.end);
        }
        // Recurse into children for nested formatting
        hidden += countHiddenMarkerChars(node.children(), sourceBase, sourceOffset);
        break;
      }
      case InlineType::Code:
      case InlineType::InlineMath: {
        InlineRange content = toLocalRange(node.contentRange(), sourceBase);
        if (!content.isValid() || content.start < src.start || content.end > src.end) {
          const qsizetype markerLen = qMax<qsizetype>(1, node.marker().size());
          content = InlineRange{src.start + markerLen, src.end - markerLen};
        }
        hidden += qMax<qsizetype>(0, qMin(content.start, sourceOffset) - src.start);
        if (src.end > content.end) {
          hidden += qMax<qsizetype>(0, qMin(src.end, sourceOffset) - content.end);
        }
        break;
      }
      case InlineType::Link: {
        if (!node.isAutolink()) {
          InlineRange content = toLocalRange(node.contentRange(), sourceBase);
          if (content.isValid()) {
            // Opening marker ([)
            hidden += qMax<qsizetype>(0, qMin(content.start, sourceOffset) - src.start);
            // Closing hidden syntax (](url))
            if (src.end > content.end) {
              hidden += qMax<qsizetype>(0, qMin(src.end, sourceOffset) - content.end);
            }
          }
        }
        // Recurse into children for nested formatting in label
        hidden += countHiddenMarkerChars(node.children(), sourceBase, sourceOffset);
        break;
      }
      case InlineType::Image: {
        InlineRange content = toLocalRange(node.contentRange(), sourceBase);
        if (content.isValid()) {
          hidden += qMax<qsizetype>(0, qMin(content.start, sourceOffset) - src.start);
          if (src.end > content.end) {
            hidden += qMax<qsizetype>(0, qMin(src.end, sourceOffset) - content.end);
          }
        }
        break;
      }
      default:
        hidden += countHiddenMarkerChars(node.children(), sourceBase, sourceOffset);
        break;
    }
  }
  return hidden;
}

qsizetype normalizedTableCellInsertOffset(const MarkdownNode& cell, const QString& content, qsizetype sourceOffset) {
  sourceOffset = qBound<qsizetype>(0, sourceOffset, content.size());
  qsizetype unitStart = 0;
  qsizetype unitEnd = 0;
  for (qsizetype offset = 0; offset < content.size(); ++offset) {
    if (!tableCellSpecialUnitAt(content, offset, unitStart, unitEnd)) {
      continue;
    }
    if (sourceOffset > unitStart && sourceOffset < unitEnd) {
      return sourceOffset - unitStart < unitEnd - sourceOffset ? unitStart : unitEnd;
    }
  }

  InlineProjection projection(cell.inlines(), content, InlineProjectionState{}, cell.sourceRange().byteStart);
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (!span.editable || span.contentSourceEnd < span.contentSourceStart) {
      continue;
    }
    if (sourceOffset >= span.sourceStart && sourceOffset < span.contentSourceStart) {
      return span.contentSourceStart;
    }
    if (sourceOffset > span.contentSourceEnd && sourceOffset < span.sourceEnd) {
      return span.contentSourceEnd;
    }
  }
  return sourceOffset;
}

std::optional<TableCellSourceEdit> tableCellDeleteEdit(
    const MarkdownNode& /*cell*/,
    const QString& content,
    qsizetype sourceOffset,
    DeleteDirection direction) {
  sourceOffset = qBound<qsizetype>(0, sourceOffset, content.size());

  qsizetype unitStart = 0;
  qsizetype unitEnd = 0;
  if (tableCellSpecialUnitContaining(content, sourceOffset, direction, unitStart, unitEnd)) {
    return TableCellSourceEdit{unitStart, unitEnd - unitStart, QString(), unitStart};
  }

  // Delete the source character directly, matching regular paragraph behavior.
  // The InlineProjection marker protection that was here previously was too
  // aggressive: it blocked forward delete at closing marker boundaries and
  // backspace at opening marker boundaries, and caused backspace to skip
  // markers and delete content characters instead.
  if (direction == DeleteDirection::Backward && sourceOffset > 0) {
    return TableCellSourceEdit{sourceOffset - 1, 1, QString(), sourceOffset - 1};
  }
  if (direction == DeleteDirection::Forward && sourceOffset < content.size()) {
    return TableCellSourceEdit{sourceOffset, 1, QString(), sourceOffset};
  }
  return TableCellSourceEdit{sourceOffset, 0, QString(), sourceOffset};
}

}  // namespace

QString escapeTableCellInsertedText(QString text) {
  return text.replace('|', QStringLiteral("\\|")).replace('\n', QStringLiteral("<br>"));
}

qsizetype tableCellSourceOffsetForVisibleOffset(const QString& content, qsizetype visibleOffset) {
  visibleOffset = qMax<qsizetype>(0, visibleOffset);
  qsizetype visible = 0;
  qsizetype source = 0;
  while (source < content.size()) {
    if (visible >= visibleOffset) {
      return source;
    }
    if (content.at(source) == QLatin1Char('\\') && source + 1 < content.size() &&
        content.at(source + 1) == QLatin1Char('|')) {
      source += 2;
      ++visible;
      continue;
    }
    if (content.mid(source, 4) == QStringLiteral("<br>")) {
      source += 4;
      ++visible;
      continue;
    }
    ++source;
    ++visible;
  }
  return content.size();
}

qsizetype tableCellVisibleOffsetForEditCursor(const MarkdownNode& cell, const QString& content, qsizetype sourceOffset) {
  const qsizetype rawVis = visibleOffsetForTableCellSourceOffset(content, sourceOffset);
  const qsizetype hiddenChars = countHiddenMarkerChars(cell.inlines(), cell.sourceRange().byteStart, sourceOffset);
  return rawVis - hiddenChars;
}

std::optional<TableCellSourceEdit> buildTableCellInsertEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset,
    QString text) {
  const qsizetype insertOffset = normalizedTableCellInsertOffset(cell, content, sourceOffset);
  const qsizetype insertedSize = text.size();
  return TableCellSourceEdit{insertOffset, 0, std::move(text), insertOffset + insertedSize};
}

std::optional<TableCellSourceEdit> buildTableCellDeleteBackwardEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset) {
  return tableCellDeleteEdit(cell, content, sourceOffset, DeleteDirection::Backward);
}

std::optional<TableCellSourceEdit> buildTableCellDeleteForwardEdit(
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset) {
  return tableCellDeleteEdit(cell, content, sourceOffset, DeleteDirection::Forward);
}

}  // namespace muffin
