#include "document/SourceRangeUtil.h"

#include "document/MarkdownNode.h"

namespace muffin {

QString mathOpeningDelimiter(const MarkdownNode& node) {
  return mathOpeningDelimiter(node.mathDelimiter());
}

QString mathClosingDelimiter(const MarkdownNode& node) {
  return mathClosingDelimiter(node.mathDelimiter());
}

QString mathOpeningDelimiter(MathDelimiter delimiter) {
  return delimiter == MathDelimiter::Bracket ? QStringLiteral("\\[") : QStringLiteral("$$");
}

QString mathClosingDelimiter(MathDelimiter delimiter) {
  return delimiter == MathDelimiter::Bracket ? QStringLiteral("\\]") : QStringLiteral("$$");
}

bool isMathClosingLine(const MarkdownNode& node, QStringView line) {
  return line.trimmed() == mathClosingDelimiter(node);
}

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown) {
  SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return range;
  }

  if (node.type() == BlockType::MathBlock) {
    const QString closing = mathClosingDelimiter(node);
    const QString newlineClosing = QLatin1Char('\n') + closing;
    if (range.byteEnd + newlineClosing.size() <= markdown.size() &&
        markdown.mid(range.byteEnd, newlineClosing.size()) == newlineClosing) {
      range.byteEnd += newlineClosing.size();
    } else if (range.byteEnd + closing.size() <= markdown.size() &&
               markdown.mid(range.byteEnd, closing.size()) == closing) {
      range.byteEnd += closing.size();
    }
  }

  // cmark reports indented-code content starting past its 4-space indent, so extend the
  // range back to the line start so serialization (which re-applies the indent) replaces the
  // whole block rather than double-indenting the first line.
  if (node.type() == BlockType::CodeFence && node.isIndentedCode() && range.byteStart > 0) {
    range.byteStart = lineStartOffset(markdown, range.byteStart);
  }

  return range;
}

qsizetype sourceOffsetForLineColumn(const QString& text, int line, int column) {
  if (line <= 0 || column <= 0) {
    return -1;
  }
  qsizetype offset = 0;
  for (int currentLine = 1; currentLine < line; ++currentLine) {
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
    if (newline < 0) {
      return -1;
    }
    offset = newline + 1;
  }
  return qMin<qsizetype>(text.size(), offset + column - 1);
}

qsizetype sourceOffsetForLineEnd(const QString& text, int line) {
  if (line <= 0) {
    return -1;
  }
  const qsizetype offset = sourceOffsetForLineColumn(text, line, 1);
  if (offset < 0) {
    return -1;
  }
  const qsizetype newline = text.indexOf(QLatin1Char('\n'), offset);
  return newline < 0 ? text.size() : newline;
}

qsizetype lineStartOffset(const QString& text, qsizetype offset) {
  const qsizetype bounded = qBound<qsizetype>(0, offset, text.size());
  for (qsizetype i = bounded; i > 0; --i) {
    if (text.at(i - 1) == QLatin1Char('\n')) {
      return i;
    }
  }
  return 0;
}

SourceRange blockLineSpan(const MarkdownNode& node, const QString& markdown) {
  SourceRange span;
  span.byteStart = -1;
  span.byteEnd = -1;
  const SourceRange range = node.sourceRange();
  if (range.lineStart <= 0 || range.lineEnd < range.lineStart) {
    return span;
  }
  span.byteStart = sourceOffsetForLineColumn(markdown, range.lineStart, 1);
  span.byteEnd = sourceOffsetForLineEnd(markdown, range.lineEnd);
  if (span.byteStart < 0 || span.byteEnd < span.byteStart) {
    span.byteStart = -1;
    span.byteEnd = -1;
  }
  return span;
}

qsizetype headingContentEndOffset(const MarkdownNode& node, const QString& markdown) {
  const SourceRange range = node.sourceRange();
  if (node.setext() && range.lineStart > 0) {
    // Setext: editable content is the text line only; stop before the underline line.
    return sourceOffsetForLineEnd(markdown, range.lineStart);
  }
  return range.byteEnd > range.byteStart ? range.byteEnd
                                         : sourceOffsetForLineEnd(markdown, range.lineEnd);
}

MarkdownNode* primaryParagraph(MarkdownNode& node) {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

const MarkdownNode* primaryParagraph(const MarkdownNode& node) {
  for (const auto& child : node.children()) {
    if (child->type() == BlockType::Paragraph) {
      return child.get();
    }
  }
  return nullptr;
}

QString listMarkerFor(const QString& line) {
  return listLineInfoFor(line).marker;
}

ListLineInfo listLineInfoFor(const QString& line) {
  ListLineInfo info;
  qsizetype index = 0;
  while (index < line.size() && line.at(index) == QLatin1Char(' ')) {
    ++index;
  }

  const qsizetype markerStart = index;
  if (index + 2 <= line.size() && (line.at(index) == QLatin1Char('-') || line.at(index) == QLatin1Char('*') ||
                                   line.at(index) == QLatin1Char('+')) &&
      line.at(index + 1).isSpace()) {
    info.valid = true;
    info.ordered = false;
    info.markerStart = markerStart;
    info.markerEnd = index + 2;
    info.contentStart = info.markerEnd;
    info.marker = line.mid(info.markerStart, info.markerEnd - info.markerStart);
  } else {
    const qsizetype numberStart = index;
    while (index < line.size() && line.at(index).isDigit()) {
      ++index;
    }
    if (index > numberStart && index + 2 <= line.size() &&
        (line.at(index) == QLatin1Char('.') || line.at(index) == QLatin1Char(')')) &&
        line.at(index + 1).isSpace()) {
      info.valid = true;
      info.ordered = true;
      info.markerStart = numberStart;
      info.markerEnd = index + 2;
      info.contentStart = info.markerEnd;
      info.marker = line.mid(info.markerStart, info.markerEnd - info.markerStart);
      info.orderedDelimiter = line.at(index);
      info.orderedNumber = line.mid(numberStart, index - numberStart).toInt();
    }
  }

  if (!info.valid) {
    return info;
  }

  qsizetype taskIndex = info.contentStart;
  while (taskIndex < line.size() && line.at(taskIndex).isSpace()) {
    ++taskIndex;
  }
  if (taskIndex + 3 < line.size() &&
      line.at(taskIndex) == QLatin1Char('[') &&
      (line.at(taskIndex + 1) == QLatin1Char(' ') || line.at(taskIndex + 1) == QLatin1Char('x') ||
       line.at(taskIndex + 1) == QLatin1Char('X')) &&
      line.at(taskIndex + 2) == QLatin1Char(']') &&
      line.at(taskIndex + 3).isSpace()) {
    info.task = true;
    info.taskChecked = line.at(taskIndex + 1).toLower() == QLatin1Char('x');
    info.taskMarkerStart = taskIndex;
    info.taskMarkerEnd = taskIndex + 4;
    info.taskContentStart = info.taskMarkerEnd;
  }
  return info;
}

}  // namespace muffin
