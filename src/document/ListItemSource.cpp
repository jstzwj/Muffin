#include "document/ListItemSource.h"

#include "document/PieceTable.h"
#include "document/BlockPredicates.h"

namespace muffin {
namespace {
qsizetype columnAfter(QStringView text, qsizetype column = 0) {
  for (QChar ch : text)
    column += ch == QLatin1Char('\t') ? 4 - column % 4 : 1;
  return column;
}

qsizetype markerDelimiterEnd(const PieceTable& markdown, const ListItemSource& source) {
  qsizetype end = source.markerStart();
  if (source.marker.ordered)
    while (end < source.firstLine.end && markdown.at(end).isDigit())
      ++end;
  return end + 1;
}

qsizetype markerPaddingEnd(const PieceTable& markdown, const ListItemSource& source) {
  const qsizetype delimiterEnd = source.markerToken.end;
  qsizetype end = delimiterEnd;
  while (end < source.firstLine.end && markdown.at(end).isSpace())
    ++end;
  const auto prefix = markdown.mid(source.firstLine.start, delimiterEnd - source.firstLine.start);
  const auto column = columnAfter(prefix);
  const auto padding = columnAfter(markdown.mid(delimiterEnd, end - delimiterEnd), column) - column;
  // CommonMark: 1..4 columns are marker padding; a wider gap leaves the
  // extra indentation in the content (for example an indented code block).
  return end == source.firstLine.end || padding <= 4 ? end : delimiterEnd + 1;
}

QString expandedPrefix(QStringView prefix) {
  QString result;
  for (QChar ch : prefix) {
    if (ch == QLatin1Char('\t'))
      result += QString(4 - result.size() % 4, QLatin1Char(' '));
    else
      result += ch;
  }
  return result;
}

void appendRebasedLine(QString& result, QStringView line, QStringView expected, QStringView replacement,
                       qsizetype column = 0) {
  qsizetype end = 0;
  while (column < expected.size() && end < line.size()) {
    const QChar ch = line.at(end);
    const qsizetype width = ch == QLatin1Char('\t') ? 4 - column % 4 : 1;
    if (ch != QLatin1Char(' ') && ch != QLatin1Char('\t') && ch != QLatin1Char('>'))
      break;
    bool matches = true;
    for (qsizetype i = column; i < qMin(column + width, expected.size()); ++i)
      matches &= expected.at(i) == (ch == QLatin1Char('>') ? ch : QLatin1Char(' '));
    if (!matches)
      break;
    column += width;
    ++end;
  }
  if (column >= expected.size()) {
    result += replacement;
    result += QString(column - expected.size(), QLatin1Char(' '));
    result += line.mid(end);
  } else {
    // Lazy paragraph continuations and unindented blank lines are retained.
    result += line;
  }
}

struct CodePrefix {
  InlineRange lines;
  qsizetype contentStart;
  QString from;
  QString to;
};

void collectCodePrefixes(const PieceTable& markdown, const MarkdownNode& node, const QString& from, const QString& to,
                         InlineRange slice, QVector<CodePrefix>& prefixes) {
  const auto range = node.sourceRange();
  if (range.byteEnd < slice.start || range.byteStart > slice.end)
    return;
  if (node.type() == BlockType::CodeFence && node.isIndentedCode()) {
    const qsizetype start = lineStartOffset(markdown, range.byteStart);
    QString prefix = markdown.mid(start, range.byteStart - start);
    // The AST starts an indented block after its code gutter, before its literal
    // payload. Rebase that gutter too: changing a tab stop must not turn code
    // into a paragraph, or expand tabs belonging to the literal itself.
    for (QChar& ch : prefix)
      if (ch != QLatin1Char('>') && !ch.isSpace())
        ch = QLatin1Char(' ');
    prefix = expandedPrefix(prefix);
    if (prefix.startsWith(from))
      prefixes.append({{start, range.byteEnd}, range.byteStart, prefix, to + prefix.mid(from.size())});
    return;
  }
  for (const auto& child : node.children())
    collectCodePrefixes(markdown, *child, from, to, slice, prefixes);
}
} // namespace

ListItemSource listItemSource(const PieceTable& markdown, const MarkdownNode& node) {
  ListItemSource result;
  const SourceRange range = node.sourceRange();
  if (node.type() != BlockType::ListItem || range.byteStart < 0 || range.byteStart > markdown.size()) {
    return result;
  }
  const qsizetype lineStart = lineStartOffset(markdown, range.byteStart);
  qsizetype lineEnd = markdown.indexOf(QLatin1Char('\n'), range.byteStart);
  if (lineEnd < 0) {
    lineEnd = markdown.size();
  }
  result.firstLine = {lineStart, lineEnd};
  result.marker = listLineInfoFor(markdown.mid(lineStart, lineEnd - lineStart), range.byteStart - lineStart);
  if (!result.marker.valid) {
    return result;
  }
  result.markerToken = {result.markerStart(), markerDelimiterEnd(markdown, result)};
  result.node = &node;
  // A checkbox-looking literal inside an indented code block is not a task.
  // A checkbox belongs to the primary paragraph, never to a child block.
  result.paragraph = primaryParagraph(node);
  result.marker.task = result.marker.task && result.paragraph;
  result.marker.contentStart = markerPaddingEnd(markdown, result) - lineStart;
  result.item = {result.markerStart(), qMax(lineEnd, markdown.lineEndOffset(range.lineEnd))};
  if (result.paragraph) {
    const SourceRange text = result.paragraph->sourceRange();
    result.content = {paragraphContentStartIncludingCommonMarkIndent(markdown, text.byteStart), text.byteEnd};
    if (result.marker.task && result.content.start <= result.textStart()) {
      result.content.start = result.textStart();
    }
  } else {
    // "3. 4. body": the outer item has zero editable text; its sublist starts
    // exactly at textStart. Do not treat the sublist's source as parent text.
    result.content = {result.textStart(), result.textStart()};
  }
  for (const auto& child : node.children()) {
    if (child.get() == result.paragraph) {
      continue;
    }
    const qsizetype start = child->sourceRange().byteStart;
    if (start >= result.content.end && start <= result.item.end) {
      result.children = {start, result.item.end};
      break;
    }
  }
  return result;
}

QString listContinuationPrefix(const PieceTable& markdown, const ListItemSource& source) {
  QString prefix = markdown.mid(source.firstLine.start, source.item.start - source.firstLine.start);
  for (QChar& ch : prefix) {
    if (ch != QLatin1Char('>') && !ch.isSpace()) {
      ch = QLatin1Char(' ');
    }
  }
  return prefix;
}

QString listContentPrefix(const PieceTable& markdown, const ListItemSource& source) {
  if (!source.isValid())
    return {};
  const qsizetype delimiterEnd = source.markerToken.end;
  const auto prefix = markdown.mid(source.firstLine.start, delimiterEnd - source.firstLine.start);
  const auto column = columnAfter(prefix);
  qsizetype padding =
      columnAfter(markdown.mid(delimiterEnd, source.markerContentStart() - delimiterEnd), column) - column;
  if (source.markerContentStart() == source.firstLine.end || padding < 1 || padding > 4)
    padding = 1;
  return listContinuationPrefix(markdown, source) +
         QString(delimiterEnd - source.item.start + padding, QLatin1Char(' '));
}

QString rebaseListItemSource(const PieceTable& markdown, InlineRange range, const ListItemSource& from,
                             const ListItemSource& to) {
  const QString text = markdown.mid(range.start, range.length());
  const QString oldPrefix = expandedPrefix(listContentPrefix(markdown, from));
  const QString newPrefix = expandedPrefix(listContentPrefix(markdown, to));
  if (oldPrefix == newPrefix || text.isEmpty())
    return text;
  QVector<CodePrefix> code;
  if (from.node)
    collectCodePrefixes(markdown, *from.node, oldPrefix, newPrefix, range, code);
  QString result;
  result.reserve(text.size());
  qsizetype index = 0;
  qsizetype codeIndex = 0;
  while (index < text.size()) {
    const qsizetype absolute = range.start + index;
    const qsizetype newline = text.indexOf(QLatin1Char('\n'), index);
    const qsizetype next = newline < 0 ? text.size() : newline + 1;
    const QStringView line = QStringView(text).mid(index, next - index);
    while (codeIndex < code.size() && code.at(codeIndex).lines.end < absolute)
      ++codeIndex;
    const CodePrefix* gutter =
        codeIndex < code.size() && code.at(codeIndex).lines.start <= absolute ? &code.at(codeIndex) : nullptr;
    if (index == 0) {
      if (gutter && absolute < gutter->contentStart) {
        const qsizetype start = lineStartOffset(markdown, absolute);
        const auto column = columnAfter(markdown.mid(start, absolute - start));
        appendRebasedLine(result, line, gutter->from, QStringView(gutter->to).mid(newPrefix.size()), column);
      } else {
        result += line;
      }
    } else {
      appendRebasedLine(result, line, gutter ? gutter->from : oldPrefix, gutter ? gutter->to : newPrefix);
    }
    index = next;
  }
  return result;
}

namespace {
bool blankSeparator(const PieceTable& markdown, qsizetype previousEnd, qsizetype nextStart) {
  // The first newline ends the previous item's last line. A further newline
  // with only indentation between them is an authored blank separator.
  const qsizetype newline = markdown.indexOf(QLatin1Char('\n'), previousEnd);
  if (newline < 0 || newline >= nextStart) {
    return false;
  }
  const qsizetype second = markdown.indexOf(QLatin1Char('\n'), newline + 1);
  return second >= 0 && second < nextStart && markdown.mid(newline + 1, second - newline - 1).trimmed().isEmpty();
}
} // namespace

qsizetype listSiblingRunEnd(const PieceTable& markdown, const MarkdownNode* first) {
  qsizetype end = -1;
  for (const MarkdownNode* item = first; item && item->type() == BlockType::ListItem; item = item->nextSibling()) {
    const auto source = listItemSource(markdown, *item);
    if (!source.isValid() || (end >= 0 && blankSeparator(markdown, end, source.item.start))) {
      break;
    }
    end = source.item.end;
  }
  return end;
}

const MarkdownNode* nextListItemInRun(const PieceTable& markdown, const MarkdownNode& item) {
  const MarkdownNode* next = item.nextSibling();
  if (!next || next->type() != BlockType::ListItem) {
    return nullptr;
  }
  const auto current = listItemSource(markdown, item);
  const auto following = listItemSource(markdown, *next);
  return current.isValid() && following.isValid() && !blankSeparator(markdown, current.item.end, following.item.start)
             ? next
             : nullptr;
}

QString renumberListItems(const PieceTable& markdown, InlineRange source, const MarkdownNode* first, int nextNumber,
                          bool sequential) {
  QString result = markdown.mid(source.start, source.length());
  const qsizetype runEnd = listSiblingRunEnd(markdown, first);
  qsizetype delta = 0;
  for (const MarkdownNode* item = first; item && item->type() == BlockType::ListItem; item = item->nextSibling()) {
    const auto bounds = listItemSource(markdown, *item);
    if (!bounds.isValid() || bounds.item.start >= runEnd || bounds.item.start >= source.end) {
      break;
    }
    if (!bounds.marker.ordered || bounds.markerStart() < source.start) {
      continue;
    }
    // Authored leading zeroes are part of the number too.
    const qsizetype numberEnd = bounds.markerToken.end - 1;
    const QString number = QString::number(sequential ? nextNumber++ : 1);
    result.replace(bounds.markerStart() - source.start + delta, numberEnd - bounds.markerStart(), number);
    delta += number.size() - (numberEnd - bounds.markerStart());
  }
  return result;
}

} // namespace muffin
