#include "document/PendingBlockMarker.h"

#include "document/MarkdownNode.h"

#include <QRegularExpression>

namespace muffin {
namespace {

QString sourceForNode(const QString& markdown, const MarkdownNode& node) {
  const SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteStart > markdown.size()) {
    return {};
  }
  const qsizetype end = qMin(range.byteEnd, markdown.size());
  return markdown.mid(range.byteStart, end - range.byteStart);
}

bool isListContainerMarkerAt(QStringView line, qsizetype index, qsizetype& markerEnd) {
  if (index >= line.size()) {
    return false;
  }
  const QChar marker = line.at(index);
  if ((marker == QLatin1Char('-') || marker == QLatin1Char('*') || marker == QLatin1Char('+')) &&
      index + 1 < line.size() && line.at(index + 1).isSpace()) {
    markerEnd = index + 2;
    return true;
  }

  qsizetype digitEnd = index;
  while (digitEnd < line.size() && line.at(digitEnd).isDigit()) {
    ++digitEnd;
  }
  if (digitEnd > index && digitEnd + 1 < line.size() &&
      (line.at(digitEnd) == QLatin1Char('.') || line.at(digitEnd) == QLatin1Char(')')) &&
      line.at(digitEnd + 1).isSpace()) {
    markerEnd = digitEnd + 2;
    return true;
  }
  return false;
}

qsizetype containerContentStart(QStringView line) {
  qsizetype index = 0;
  bool consumedContainer = false;
  while (true) {
    const qsizetype beforeIndent = index;
    int indent = 0;
    while (index < line.size() && indent < 3 && line.at(index) == QLatin1Char(' ')) {
      ++index;
      ++indent;
    }

    if (index < line.size() && line.at(index) == QLatin1Char('>')) {
      ++index;
      if (index < line.size() && line.at(index) == QLatin1Char(' ')) {
        ++index;
      }
      consumedContainer = true;
      continue;
    }

    qsizetype markerEnd = -1;
    if (isListContainerMarkerAt(line, index, markerEnd)) {
      index = markerEnd;
      consumedContainer = true;
      continue;
    }

    index = consumedContainer ? beforeIndent : 0;
    int contentIndent = 0;
    while (index < line.size() && contentIndent < 4 && line.at(index) == QLatin1Char(' ')) {
      ++index;
      ++contentIndent;
    }
    return contentIndent <= 3 ? index : -1;
  }
}

PendingBlockMarker detectFenceMarker(QStringView singleLine) {
  PendingBlockMarker marker;
  static const QRegularExpression backtickFence(QStringLiteral("^[ \\t]{0,3}(`{3,})[^`\\n]*$"));
  QRegularExpressionMatch fenceMatch = backtickFence.matchView(singleLine);
  if (!fenceMatch.hasMatch()) {
    static const QRegularExpression tildeFence(QStringLiteral("^[ \\t]{0,3}(~{3,})[^\\n]*$"));
    fenceMatch = tildeFence.matchView(singleLine);
  }
  if (!fenceMatch.hasMatch()) {
    return marker;
  }

  const QString fenceRun = fenceMatch.captured(1);
  marker.kind = PendingBlockMarkerKind::CodeFence;
  marker.targetType = BlockType::CodeFence;
  marker.opener = singleLine.trimmed().toString();
  marker.closer = QString(fenceRun.at(0)).repeated(fenceRun.size());
  marker.prefixLength = singleLine.size();
  marker.highlightPrefix = true;
  return marker;
}

qsizetype pendingMarkerStartInLine(QStringView line) {
  if (detectPendingBlockMarker(line).isValid()) {
    return 0;
  }
  const qsizetype contentStart = containerContentStart(line);
  if (contentStart > 0 && contentStart < line.size() &&
      detectPendingBlockMarker(line.mid(contentStart)).isValid()) {
    return contentStart;
  }
  return -1;
}

QString pendingMarkerParagraphText(const QString& markdown, const MarkdownNode& node) {
  if (!shouldDemotePendingMarker(markdown, node)) {
    return {};
  }
  const SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteStart > markdown.size()) {
    return {};
  }
  const qsizetype end = qMin(range.byteEnd, markdown.size());
  return markdown.mid(range.byteStart, end - range.byteStart);
}

}  // namespace

bool PendingBlockMarker::isValid() const {
  return kind != PendingBlockMarkerKind::None;
}

bool PendingBlockMarker::commitsOnEnter() const {
  return targetType == BlockType::CodeFence || targetType == BlockType::MathBlock;
}

PendingBlockMarker detectPendingBlockMarker(QStringView singleLine) {
  PendingBlockMarker marker;
  if (singleLine.indexOf(QLatin1Char('\n')) >= 0) {
    return marker;
  }

  static const QRegularExpression headingMarker(QStringLiteral("^[ \\t]{0,3}#{1,6}[ \\t]*$"));
  if (headingMarker.matchView(singleLine).hasMatch()) {
    marker.kind = PendingBlockMarkerKind::Heading;
    marker.targetType = BlockType::Heading;
    marker.opener = singleLine.toString();
    marker.prefixLength = singleLine.size();
    return marker;
  }

  static const QRegularExpression listMarker(QStringLiteral("^[ \\t]{0,3}([-+*]|\\d+[.)])$"));
  if (listMarker.matchView(singleLine).hasMatch()) {
    marker.kind = PendingBlockMarkerKind::List;
    marker.targetType = BlockType::List;
    marker.opener = singleLine.toString();
    marker.prefixLength = singleLine.size();
    return marker;
  }

  marker = detectFenceMarker(singleLine);
  if (marker.isValid()) {
    return marker;
  }

  if (singleLine.trimmed() == QStringLiteral("$$")) {
    marker.kind = PendingBlockMarkerKind::MathDollar;
    marker.targetType = BlockType::MathBlock;
    marker.opener = QStringLiteral("$$");
    marker.closer = QStringLiteral("$$");
    marker.prefixLength = singleLine.size();
    marker.highlightPrefix = true;
    return marker;
  }
  if (singleLine.trimmed() == QStringLiteral("\\[")) {
    marker.kind = PendingBlockMarkerKind::MathBracket;
    marker.targetType = BlockType::MathBlock;
    marker.opener = QStringLiteral("\\[");
    marker.closer = QStringLiteral("\\]");
    marker.prefixLength = singleLine.size();
    marker.highlightPrefix = true;
    return marker;
  }

  return marker;
}

PendingBlockMarker detectPendingBlockMarkerForNode(const QString& markdown, const MarkdownNode& node) {
  const QString source = sourceForNode(markdown, node);
  if (source.isEmpty()) {
    return {};
  }
  const qsizetype markerStart = pendingMarkerStartInLine(QStringView(source));
  PendingBlockMarker marker =
      markerStart >= 0 ? detectPendingBlockMarker(QStringView(source).mid(markerStart)) : PendingBlockMarker();
  if (!marker.isValid()) {
    return {};
  }

  switch (node.type()) {
    case BlockType::Paragraph:
      return marker;
    case BlockType::Heading:
      return marker.kind == PendingBlockMarkerKind::Heading ? marker : PendingBlockMarker();
    case BlockType::List:
      return marker.kind == PendingBlockMarkerKind::List ? marker : PendingBlockMarker();
    case BlockType::CodeFence:
      return marker.kind == PendingBlockMarkerKind::CodeFence ? marker : PendingBlockMarker();
    case BlockType::MathBlock:
      return marker.kind == PendingBlockMarkerKind::MathDollar || marker.kind == PendingBlockMarkerKind::MathBracket
                 ? marker
                 : PendingBlockMarker();
    default:
      return {};
  }
}

bool shouldDemotePendingMarker(const QString& markdown, const MarkdownNode& node) {
  return node.type() != BlockType::Paragraph && detectPendingBlockMarkerForNode(markdown, node).isValid();
}

void demotePendingMarkerToParagraph(const QString& markdown, MarkdownNode& node) {
  const QString text = pendingMarkerParagraphText(markdown, node);
  if (text.isEmpty()) {
    return;
  }
  const SourceRange range = node.sourceRange();
  const qsizetype end = qMin(range.byteEnd, markdown.size());
  node.setType(BlockType::Paragraph);
  node.setLiteral(QString());
  node.setCodeLanguage(QString());
  node.setHeadingLevel(0);
  node.setListKind(ListKind::None);
  // A lone-marker List carries ListItem children; a Paragraph must not, so collapse them.
  node.clearChildren();
  QVector<InlineNode> inlines;
  InlineNode inlineNode = InlineNode::text(text);
  inlineNode.setSourceRange(InlineRange{range.byteStart, end});
  inlines.append(inlineNode);
  node.inlines() = std::move(inlines);
}

void demotePendingListMarkers(MarkdownNode& node, const QString& markdown) {
  if (node.type() == BlockType::List) {
    const PendingBlockMarker marker = detectPendingBlockMarkerForNode(markdown, node);
    if (marker.isValid() && marker.kind == PendingBlockMarkerKind::List) {
      demotePendingMarkerToParagraph(markdown, node);
    }
  }
  for (const auto& child : node.children()) {
    if (child) {
      demotePendingListMarkers(*child, markdown);
    }
  }
}

QVector<qsizetype> collectPendingMarkerOffsets(const QString& markdown, const MarkdownNode& root) {
  QVector<qsizetype> offsets;
  const auto visit = [&](const auto& self, const MarkdownNode& node) -> void {
    if (node.type() == BlockType::Paragraph) {
      const QString source = sourceForNode(markdown, node);
      const qsizetype markerStart = pendingMarkerStartInLine(QStringView(source));
      if (markerStart >= 0) {
        offsets.append(node.sourceRange().byteStart + markerStart);
      }
    }
    for (const auto& child : node.children()) {
      if (child) {
        self(self, *child);
      }
    }
  };
  visit(visit, root);
  return offsets;
}

qsizetype pendingBlockMarkerOffset(const QString& markdown, qsizetype offset) {
  if (offset < 0 || offset > markdown.size()) {
    return -1;
  }
  qsizetype lineStart = offset;
  while (lineStart > 0 && markdown.at(lineStart - 1) != QLatin1Char('\n')) {
    --lineStart;
  }
  qsizetype lineEnd = offset;
  while (lineEnd < markdown.size() && markdown.at(lineEnd) != QLatin1Char('\n')) {
    ++lineEnd;
  }
  const qsizetype markerStart = pendingMarkerStartInLine(QStringView(markdown).mid(lineStart, lineEnd - lineStart));
  return markerStart >= 0 ? lineStart + markerStart : -1;
}

QVector<qsizetype> shiftPendingMarkerOffsets(
    const QVector<qsizetype>& preEditOffsets,
    qsizetype sourceStart,
    qsizetype removedLength,
    qsizetype insertedLength) {
  const qsizetype sourceEnd = sourceStart + removedLength;
  const qsizetype delta = insertedLength - removedLength;
  QVector<qsizetype> shifted;
  shifted.reserve(preEditOffsets.size());
  for (const qsizetype offset : preEditOffsets) {
    if (offset < sourceStart) {
      shifted.append(offset);
    } else if (offset >= sourceEnd) {
      shifted.append(offset + delta);
    }
    // Offsets in [sourceStart, sourceEnd) are inside the replaced region — discard.
  }
  return shifted;
}

}  // namespace muffin
