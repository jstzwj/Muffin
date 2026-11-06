#include "document/BlockSourceBoundary.h"
#include "document/MarkdownNode.h"
#include "document/PieceTable.h"
#include "document/SourceRangeUtil.h"
#include <QRegularExpression>
#include <algorithm>

namespace muffin {
QString blockClosingSuffix(const PieceTable& text, const MarkdownNode& node) {
  const auto range = node.sourceRange();
  if (node.type() == BlockType::CodeFence && !node.isIndentedCode()) {
    const auto firstEnd = sourceOffsetForLineEnd(text, range.lineStart);
    const QString opening = text.mid(range.byteStart, firstEnd - range.byteStart).trimmed();
    if (opening.isEmpty() || (opening.front() != u'`' && opening.front() != u'~'))
      return {};
    const auto count = countLeading(QStringView(opening), 0, opening.size(), opening.front());
    const QString fence(count, opening.front());
    const auto lastStart = sourceOffsetForLineColumn(text, range.lineEnd, 1);
    const auto lastEnd = sourceOffsetForLineEnd(text, range.lineEnd);
    const auto last = text.mid(lastStart, lastEnd - lastStart);
    const auto indent = countLeading(QStringView(last), 0, last.size(), u' ');
    const auto line = QStringView(last).mid(indent);
    const auto closing = countLeading(line, 0, line.size(), opening.front());
    const auto padding = line.mid(closing);
    const bool blankPadding =
        std::all_of(padding.begin(), padding.end(), [](QChar ch) { return ch == u' ' || ch == u'\t'; });
    if (range.lineEnd > range.lineStart && indent <= 3 && closing >= count && blankPadding)
      return {};
    return u'\n' + fence;
  }
  if (node.type() == BlockType::MathBlock) {
    const auto lastStart = sourceOffsetForLineColumn(text, range.lineEnd, 1);
    const auto lastEnd = sourceOffsetForLineEnd(text, range.lineEnd);
    return range.lineEnd > range.lineStart && isMathClosingLine(node, text.mid(lastStart, lastEnd - lastStart))
               ? QString()
               : u'\n' + mathClosingDelimiter(node);
  }
  if (node.type() != BlockType::HtmlBlock)
    return {};
  const QString literal = node.literal();
  const auto source = QStringView(literal).trimmed();
  QString closing;
  if (source.startsWith(u"<!--"))
    closing = QStringLiteral("-->");
  else if (source.startsWith(u"<![CDATA["))
    closing = QStringLiteral("]]>");
  else if (source.startsWith(u"<?"))
    closing = QStringLiteral("?>");
  else {
    static const QRegularExpression rawTag(QStringLiteral("^<(script|style|pre|textarea)(?:[ \\t\\r\\n\\f]|>|$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    const auto match = rawTag.matchView(source);
    if (match.hasMatch()) {
      const auto tag = match.captured(1);
      // CommonMark HTML type 1 ends at any of these exact closing strings,
      // even when the name differs from the opening tag. Spaces before '>'
      // are not accepted by that grammar.
      static const QRegularExpression endTag(QStringLiteral("</(?:script|pre|textarea|style)>"),
                                             QRegularExpression::CaseInsensitiveOption);
      return endTag.matchView(source).hasMatch() ? QString() : QStringLiteral("\n</%1>").arg(tag);
    }
    if (source.size() > 2 && source.startsWith(u"<!") && source[2] >= u'A' && source[2] <= u'Z')
      closing = u'>';
  }
  return closing.isEmpty() || source.contains(closing) ? QString() : u'\n' + closing;
}
}
