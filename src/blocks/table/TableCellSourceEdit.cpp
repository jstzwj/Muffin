#include "blocks/table/TableCellSourceEdit.h"

#include "document/InlineProjection.h"
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

  InlineProjection projection(cell.inlines(), content, InlineProjectionState{});
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
    const MarkdownNode& cell,
    const QString& content,
    qsizetype sourceOffset,
    DeleteDirection direction) {
  sourceOffset = qBound<qsizetype>(0, sourceOffset, content.size());

  qsizetype unitStart = 0;
  qsizetype unitEnd = 0;
  if (tableCellSpecialUnitContaining(content, sourceOffset, direction, unitStart, unitEnd)) {
    return TableCellSourceEdit{unitStart, unitEnd - unitStart, QString(), unitStart};
  }

  InlineProjection projection(cell.inlines(), content, InlineProjectionState{});
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (!span.editable) {
      continue;
    }
    if (direction == DeleteDirection::Backward && span.sourceStart < span.contentSourceStart && sourceOffset > span.sourceStart &&
        sourceOffset <= span.contentSourceStart) {
      return TableCellSourceEdit{sourceOffset, 0, QString(), sourceOffset};
    }
    if (direction == DeleteDirection::Forward && span.contentSourceEnd < span.sourceEnd && sourceOffset >= span.contentSourceEnd &&
        sourceOffset < span.sourceEnd) {
      return TableCellSourceEdit{sourceOffset, 0, QString(), sourceOffset};
    }
  }

  qsizetype replaceStart = -1;
  qsizetype replaceEnd = -1;

  for (const InlineProjectionSpan& span : projection.spans()) {
    if (!span.editable || span.visibleEnd <= span.visibleStart || span.contentSourceEnd <= span.contentSourceStart) {
      continue;
    }

    if (direction == DeleteDirection::Backward) {
      qsizetype effectiveEnd = -1;
      if (sourceOffset > span.contentSourceStart && sourceOffset <= span.contentSourceEnd) {
        effectiveEnd = sourceOffset;
      } else if (sourceOffset > span.contentSourceEnd && sourceOffset <= span.sourceEnd) {
        effectiveEnd = span.contentSourceEnd;
      } else if (sourceOffset > span.sourceEnd) {
        effectiveEnd = span.contentSourceEnd;
      }
      if (effectiveEnd > span.contentSourceStart && (replaceEnd < 0 || effectiveEnd > replaceEnd)) {
        replaceEnd = effectiveEnd;
        replaceStart = effectiveEnd - 1;
      }
    } else {
      qsizetype effectiveStart = -1;
      if (sourceOffset >= span.contentSourceStart && sourceOffset < span.contentSourceEnd) {
        effectiveStart = sourceOffset;
      } else if (sourceOffset >= span.sourceStart && sourceOffset < span.contentSourceStart) {
        effectiveStart = span.contentSourceStart;
      } else if (sourceOffset < span.sourceStart) {
        effectiveStart = span.contentSourceStart;
      }
      if (effectiveStart >= span.contentSourceStart && effectiveStart < span.contentSourceEnd &&
          (replaceStart < 0 || effectiveStart < replaceStart)) {
        replaceStart = effectiveStart;
        replaceEnd = effectiveStart + 1;
      }
    }
  }

  if (replaceStart < 0 || replaceEnd <= replaceStart) {
    return TableCellSourceEdit{sourceOffset, 0, QString(), sourceOffset};
  }
  return TableCellSourceEdit{replaceStart, replaceEnd - replaceStart, QString(), replaceStart};
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
  qsizetype visibleOffset = visibleOffsetForTableCellSourceOffset(content, sourceOffset);
  InlineProjection projection(cell.inlines(), content, InlineProjectionState{});
  if (projection.visibleOffsetForSourceOffset(sourceOffset, visibleOffset)) {
    visibleOffset = qMax<qsizetype>(0, visibleOffset - tableSpecialSourceExtraBefore(content, sourceOffset));
  }
  return visibleOffset;
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
