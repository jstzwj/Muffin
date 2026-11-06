#include "document/InlineBlockConversion.h"
#include "document/SourceRangeUtil.h"
#include "parser/CmarkGfmParser.h"
#include <algorithm>

namespace muffin {
namespace {
// Retain source correspondence through delimiter removal, escaping and nesting.
// These runs also let reading anchors follow the conversion and its undo.
struct MappedText {
  QString text;
  QVector<SourceTextChange::Move> runs;
  void replace(qsizetype start, qsizetype removed, const QString& inserted) {
    QVector<SourceTextChange::Move> next;
    for (const auto& run : runs) {
      const auto end = run.to + run.length;
      const auto before = qMin(end, start);
      if (before > run.to)
        next.append({run.from, before - run.to, run.to});
      const auto overlap = qMax(run.to, start);
      const auto kept = qMin(qMin(end, start + removed) - overlap, inserted.size() - (overlap - start));
      if (kept > 0)
        next.append({run.from + overlap - run.to, kept, overlap});
      const auto after = qMax(run.to, start + removed);
      if (end > after)
        next.append({run.from + after - run.to, end - after, after + inserted.size() - removed});
    }
    text.replace(start, removed, inserted);
    runs = std::move(next);
  }
};
qsizetype firstToken(const QString& text, int line) {
  auto pos = sourceOffsetForLineColumn(text, line, 1);
  if (pos < 0)
    return -1;
  while (pos < text.size() && (text[pos] == u' ' || text[pos] == u'\t'))
    ++pos;
  return pos < text.size() ? pos : -1;
}
bool protectParagraph(MappedText& body, ParseOptions options) {
  // Ask the real parser, with the document's extensions, rather than maintain a
  // second approximation of Markdown's block-start grammar in UI commands.
  options.enableFrontMatter = false;
  CmarkGfmParser parser;
  for (int pass = 0; pass < 8; ++pass) {
    const auto parsed = parser.parseDocument(body.text, options);
    if (!parsed.root)
      return false;
    QVector<qsizetype> escapes;
    int paragraphs = 0;
    for (const auto& node : parsed.root->children()) {
      if (node->type() == BlockType::Paragraph) {
        if (!node->inlines().isEmpty())
          ++paragraphs;
        continue;
      }
      int line = node->sourceRange().lineStart;
      if (node->type() == BlockType::Heading && node->setext())
        line = node->sourceRange().lineEnd;
      if (node->type() == BlockType::Table)
        ++line;
      auto pos = firstToken(body.text, line);
      if (pos < 0)
        return false;
      if (node->type() == BlockType::List && body.text[pos].isDigit()) {
        while (pos < body.text.size() && body.text[pos].isDigit())
          ++pos;
      } else if (node->type() == BlockType::Table) {
        // Escaping a delimiter dash invalidates the table; an escaped outer
        // pipe alone can still leave a valid delimiter row.
        while (pos < body.text.size() && body.text[pos] != u'-' && body.text[pos] != u'\n')
          ++pos;
      } else if (node->type() == BlockType::HtmlBlock) {
        // Do not turn authored inline HTML into visible tag text. Such a
        // conversion has no lossless paragraph spelling here.
        return false;
      }
      if (pos >= body.text.size() || body.text[pos] == u'\\' || body.text[pos] == u'\n')
        return false;
      escapes.append(pos);
    }
    if (escapes.isEmpty()) {
      if (paragraphs == 1 || body.text.trimmed().isEmpty())
        return true;
      // A heading can contain something which becomes a reference definition
      // only after its heading marker has been removed.
      for (int line = 1; line <= body.text.count(u'\n') + 1; ++line) {
        const auto pos = firstToken(body.text, line);
        if (pos >= 0 && body.text[pos] == u'[')
          escapes.append(pos);
      }
      if (escapes.isEmpty())
        return false;
    }
    std::sort(escapes.begin(), escapes.end(), std::greater<qsizetype>());
    for (const auto pos : escapes)
      body.replace(pos, 0, QStringLiteral("\\"));
  }
  return false;
}
}
InlineBlockConversion convertInlineBlock(const PieceTable& text, const MarkdownNode& node, InlineBlockFormat format,
                                         const ParseOptions& options) {
  InlineBlockConversion result;
  if (node.type() != BlockType::Paragraph && node.type() != BlockType::Heading)
    return result;
  const auto range = node.sourceRange();
  const auto lineStart = sourceOffsetForLineColumn(text, range.lineStart, 1);
  const auto end = sourceOffsetForLineEnd(text, range.lineEnd);
  if (lineStart < 0 || range.byteStart < lineStart || end < range.byteStart)
    return result;
  const bool top = node.parent() && node.parent()->type() == BlockType::Document;
  result.range = {top ? lineStart : range.byteStart, end};
  auto start = range.byteStart, contentEnd = end;
  if (node.type() == BlockType::Heading) {
    if (node.setext()) {
      contentEnd = sourceOffsetForLineColumn(text, range.lineEnd, 1) - 1;
    } else {
      while (start < end && text.at(start) == u'#')
        ++start;
      while (start < end && (text.at(start) == u' ' || text.at(start) == u'\t'))
        ++start;
      while (contentEnd > start && text.at(contentEnd - 1).isSpace())
        --contentEnd;
      auto hashes = contentEnd;
      while (hashes > start && text.at(hashes - 1) == u'#')
        --hashes;
      if (hashes < contentEnd && (hashes == start || text.at(hashes - 1).isSpace())) {
        contentEnd = hashes;
        while (contentEnd > start && text.at(contentEnd - 1).isSpace())
          --contentEnd;
      }
    }
  }
  MappedText body{text.mid(start, contentEnd - start), {{start, contentEnd - start, 0}}};
  QString continuation = top ? QString() : text.mid(lineStart, range.byteStart - lineStart);
  for (auto& ch : continuation)
    if (!ch.isSpace() && ch != u'>')
      ch = u' ';
  if (!continuation.isEmpty()) {
    for (auto pos = body.text.lastIndexOf(u'\n'); pos >= 0; pos = body.text.lastIndexOf(u'\n', pos - 1)) {
      if (QStringView(body.text).mid(pos + 1).startsWith(continuation))
        body.replace(pos + 1, continuation.size(), {});
      if (pos == 0)
        break;
    }
  }
  if (format.kind == InlineBlockFormat::Heading) {
    if (format.level < 1 || format.level > 6)
      return result;
    for (auto pos = body.text.lastIndexOf(u'\n'); pos >= 0; pos = body.text.lastIndexOf(u'\n', pos - 1)) {
      body.replace(pos, 1, QStringLiteral(" "));
      if (pos == 0)
        break;
    }
  }
  const bool atx = format.kind == InlineBlockFormat::Heading && !(format.setext && format.level <= 2);
  if (format.kind != InlineBlockFormat::Code && !atx && !protectParagraph(body, options))
    return result;
  if (format.kind == InlineBlockFormat::Paragraph && result.range.start == 0 && options.enableFrontMatter) {
    const auto firstLine = QStringView(body.text)
                               .first(body.text.indexOf(u'\n') < 0 ? body.text.size() : body.text.indexOf(u'\n'))
                               .trimmed();
    // The closing marker/object may be in an untouched later block, outside
    // the small fragment parsed above. Do not let a demotion create metadata.
    if (firstLine == u"+++" || firstLine.startsWith(u'{'))
      body.replace(firstToken(body.text, 1), 0, QStringLiteral("\\"));
  }
  QString prefix, suffix, linePrefix;
  switch (format.kind) {
  case InlineBlockFormat::Heading:
    if (format.setext && format.level <= 2 && !body.text.trimmed().isEmpty()) {
      suffix = u'\n' + QString(qMax<qsizetype>(1, body.text.size()), format.level == 1 ? u'=' : u'-');
    } else {
      prefix = QString(format.level, u'#') + u' ';
      auto endHashes = body.text.size();
      while (endHashes > 0 && body.text[endHashes - 1].isSpace())
        --endHashes;
      auto hashes = endHashes;
      while (hashes > 0 && body.text[hashes - 1] == u'#')
        --hashes;
      if (hashes < endHashes && (hashes == 0 || body.text[hashes - 1].isSpace()))
        body.replace(hashes, 0, QStringLiteral("\\"));
    }
    break;
  case InlineBlockFormat::BulletList:
  case InlineBlockFormat::OrderedList:
  case InlineBlockFormat::TaskList:
    prefix = format.kind == InlineBlockFormat::OrderedList ? QStringLiteral("1. ") : QString(format.bullet) + u' ';
    if (format.kind == InlineBlockFormat::TaskList)
      prefix += QStringLiteral("[ ] ");
    else if (options.enableTaskList && body.text.size() >= 3 && body.text[0] == u'[' && body.text[2] == u']' &&
             (body.text[1] == u' ' || body.text[1] == u'x' || body.text[1] == u'X'))
      body.replace(0, 0, QStringLiteral("\\"));
    linePrefix = QString(prefix.size(), u' ');
    break;
  case InlineBlockFormat::Quote:
    if (options.enableAlertBox) {
      for (const auto marker : {u"[!NOTE]", u"[!TIP]", u"[!IMPORTANT]", u"[!WARNING]", u"[!CAUTION]"})
        if (QStringView(body.text).trimmed().startsWith(QStringView(marker), Qt::CaseInsensitive)) {
          body.replace(firstToken(body.text, 1), 0, QStringLiteral("\\"));
          break;
        }
    }
    prefix = linePrefix = QStringLiteral("> ");
    break;
  case InlineBlockFormat::Code: {
    QString fence = QStringLiteral("```");
    while (body.text.contains(fence))
      fence += u'`';
    prefix = fence + u'\n';
    suffix = u'\n' + fence;
    break;
  }
  case InlineBlockFormat::Paragraph:
    break;
  }
  for (auto pos = body.text.lastIndexOf(u'\n'); pos >= 0; pos = body.text.lastIndexOf(u'\n', pos - 1)) {
    body.replace(pos + 1, 0, linePrefix);
    if (pos == 0)
      break;
  }
  body.replace(body.text.size(), 0, suffix);
  body.replace(0, 0, prefix);
  if (!continuation.isEmpty()) {
    for (auto pos = body.text.lastIndexOf(u'\n'); pos >= 0; pos = body.text.lastIndexOf(u'\n', pos - 1)) {
      body.replace(pos + 1, 0, continuation);
      if (pos == 0)
        break;
    }
  }
  result.text = std::move(body.text);
  result.change.edits.append({result.range.start, result.range.length(), result.text.size()});
  for (auto run = body.runs.crbegin(); run != body.runs.crend(); ++run)
    result.change.moves.append({run->from, run->length, result.range.start + run->to});
  result.valid = true;
  return result;
}
} // namespace muffin
