#include "document/SourceRangeUtil.h"

#include "document/MarkdownNode.h"

namespace muffin {

SourceRange fullBlockSourceRange(const MarkdownNode& node, const QString& markdown) {
  SourceRange range = node.sourceRange();
  if (range.byteStart < 0 || range.byteEnd < range.byteStart || range.byteEnd > markdown.size()) {
    return range;
  }

  if (node.type() == BlockType::MathBlock) {
    if (range.byteEnd + 3 <= markdown.size() && markdown.mid(range.byteEnd, 3) == QStringLiteral("\n$$")) {
      range.byteEnd += 3;
    } else if (range.byteEnd + 2 <= markdown.size() && markdown.mid(range.byteEnd, 2) == QStringLiteral("$$")) {
      range.byteEnd += 2;
    }
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
