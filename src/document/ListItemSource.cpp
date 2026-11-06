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
    // cmark may locate the literal at a tab whose first columns still belong
    // to the four-column code gutter. Keep the remaining tab columns literal.
    qsizetype gutter = from.size() + 4;
    for (auto* parent = node.parent(); parent; parent = parent->parent()) {
      if (parent->type() == BlockType::ListItem) {
        gutter = expandedPrefix(listContentPrefix(markdown, listItemSource(markdown, *parent))).size() + 4;
        break;
      }
    }
    if (const auto quote = prefix.lastIndexOf(u'>'); quote >= 0)
      gutter = qMax(gutter, quote + 6);
    if (prefix.size() < gutter)
      prefix += QString(gutter - prefix.size(), u' ');
    if (prefix.startsWith(from))
      prefixes.append({{start, range.byteEnd}, prefix, to + prefix.mid(from.size())});
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
  qsizetype end = delimiterEnd;
  while (end < source.firstLine.end && markdown.at(end).isSpace())
    ++end;
  qsizetype padding = columnAfter(markdown.mid(delimiterEnd, end - delimiterEnd), column) - column;
  if (end == source.firstLine.end || padding < 1 || padding > 4)
    padding = 1;
  return listContinuationPrefix(markdown, source) +
         QString(delimiterEnd - source.item.start + padding, QLatin1Char(' '));
}

namespace {
QString rebaseToPrefix(const PieceTable& markdown, InlineRange range, const ListItemSource& from,
                       const QString& targetPrefix) {
  const QString text = markdown.mid(range.start, range.length());
  const QString oldPrefix = expandedPrefix(listContentPrefix(markdown, from));
  const QString newPrefix = expandedPrefix(targetPrefix);
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
      const qsizetype start = lineStartOffset(markdown, absolute);
      const auto column = columnAfter(markdown.mid(start, absolute - start));
      if (gutter && column < gutter->from.size()) {
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
} // namespace

QString rebaseListItemSource(const PieceTable& markdown, InlineRange range, const ListItemSource& from,
                             const ListItemSource& to) {
  return rebaseToPrefix(markdown, range, from, listContentPrefix(markdown, to));
}

QVector<ListSourceEdit> listItemNumberEdits(const PieceTable& markdown, const ListItemSource& source, int number) {
  QVector<ListSourceEdit> edits;
  if (!source.isValid() || !source.marker.ordered)
    return edits;
  const auto digits = InlineRange{source.markerToken.start, source.markerToken.end - 1};
  const auto replacement = QString::number(number);
  if (markdown.mid(digits.start, digits.length()) == replacement)
    return edits;
  edits.append({digits, replacement});
  const auto delta = replacement.size() - digits.length();
  if (!delta)
    return edits;
  auto prefix = listContentPrefix(markdown, source);
  if (delta > 0)
    prefix += QString(delta, u' ');
  else
    prefix.chop(-delta);
  // A tab after the delimiter reaches a column stop, so its visual width
  // would change with the number. Preserve the padding width explicitly.
  const InlineRange padding{source.markerToken.end, source.markerContentStart()};
  const auto gap = markdown.mid(padding.start, padding.length());
  if (gap.contains(u'\t')) {
    const auto width = expandedPrefix(listContentPrefix(markdown, source)).size() -
                       expandedPrefix(listContinuationPrefix(markdown, source)).size() - source.markerToken.length();
    edits.append({padding, QString(width, u' ')});
  }
  // Keep the marker padding outside literal code-gutter rebasing, including
  // an indented code block beginning on the marker's own physical line.
  const InlineRange tail{source.markerContentStart(), source.item.end};
  const auto before = markdown.mid(tail.start, tail.length());
  const auto after = rebaseToPrefix(markdown, tail, source, prefix);
  qsizetype oldLine = 0, newLine = 0;
  while (oldLine < before.size()) {
    const auto oldNewline = before.indexOf(u'\n', oldLine), newNewline = after.indexOf(u'\n', newLine);
    const auto oldEnd = oldNewline < 0 ? before.size() : oldNewline + 1;
    const auto newEnd = newNewline < 0 ? after.size() : newNewline + 1;
    auto left = QStringView(before).mid(oldLine, oldEnd - oldLine);
    auto right = QStringView(after).mid(newLine, newEnd - newLine);
    qsizetype common = 0, suffix = 0;
    while (common < left.size() && common < right.size() && left[common] == right[common])
      ++common;
    while (suffix < left.size() - common && suffix < right.size() - common &&
           left[left.size() - 1 - suffix] == right[right.size() - 1 - suffix])
      ++suffix;
    if (common != left.size() || common != right.size())
      edits.append({{tail.start + oldLine + common, tail.start + oldEnd - suffix},
                    right.mid(common, right.size() - common - suffix).toString()});
    oldLine = oldEnd;
    newLine = newEnd;
  }
  return edits;
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
    for (const auto& edit : listItemNumberEdits(markdown, bounds, sequential ? nextNumber++ : 1)) {
      if (edit.range.start < source.start || edit.range.end > source.end)
        continue;
      result.replace(edit.range.start - source.start + delta, edit.range.length(), edit.text);
      delta += edit.text.size() - edit.range.length();
    }
  }
  return result;
}

} // namespace muffin
