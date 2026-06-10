#include "projection/InlineProjection.h"

#include "houdini.h"
#include "html/InlineHtmlRenderer.h"

#include <QRegularExpression>

namespace muffin {
namespace {

struct EntitySpan {
  qsizetype sourceStart;  // offset within the local source slice
  qsizetype sourceEnd;    // past-the-end offset
  QString decodedText;
};

QString decodeHtmlEntity(QStringView rawEntity) {
  if (rawEntity.size() < 3 || rawEntity.front() != QLatin1Char('&') || rawEntity.back() != QLatin1Char(';')) {
    return {};
  }

  const QByteArray body = rawEntity.mid(1).toUtf8();
  cmark_strbuf decoded = CMARK_BUF_INIT(cmark_get_default_mem_allocator());
  const bufsize_t consumed = houdini_unescape_ent(
      &decoded,
      reinterpret_cast<const uint8_t*>(body.constData()),
      static_cast<bufsize_t>(body.size()));
  QString result;
  if (consumed == body.size() && cmark_strbuf_len(&decoded) > 0) {
    result = QString::fromUtf8(cmark_strbuf_cstr(&decoded), static_cast<qsizetype>(cmark_strbuf_len(&decoded)));
  }
  cmark_strbuf_free(&decoded);
  return result;
}

QVector<EntitySpan> findHtmlEntities(QStringView source) {
  QVector<EntitySpan> entities;
  qsizetype i = 0;
  while (i < source.size()) {
    if (source.at(i) == QLatin1Char('&')) {
      qsizetype semi = source.indexOf(QLatin1Char(';'), i + 1);
      if (semi >= 0 && semi - i <= 33) {
        const QString decoded = decodeHtmlEntity(source.mid(i, semi + 1 - i));
        if (!decoded.isEmpty()) {
          entities.push_back({i, semi + 1, decoded});
          i = semi + 1;
          continue;
        }
      }
      ++i;
    } else {
      ++i;
    }
  }
  return entities;
}

bool textMatchesAt(const QString& decoded, qsizetype decodedPos, QStringView text) {
  return decodedPos >= 0 && decodedPos + text.size() <= decoded.size() && decoded.mid(decodedPos, text.size()) == text;
}

bool containsOffset(qsizetype start, qsizetype end, qsizetype offset) {
  return offset >= start && offset <= end;
}

bool overlapsRange(qsizetype firstStart, qsizetype firstEnd, qsizetype secondStart, qsizetype secondEnd) {
  return firstStart >= 0 && firstEnd >= firstStart && secondStart >= 0 && secondEnd >= secondStart && firstStart < secondEnd &&
         secondStart < firstEnd;
}

InlineRange localRange(InlineRange range, qsizetype sourceBase) {
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

bool rangeWithin(InlineRange range, qsizetype start, qsizetype end) {
  return range.isValid() && range.start >= start && range.end <= end;
}

// Extract the tag name from an HTML inline tag text.
// For "<b>", "<span ...>" returns "b", "span".
// For "</b>", returns empty (closing tag).
// For "<br>", "<br/>" returns "br".
// For non-tag text, returns empty.
QStringView extractOpeningTagName(const QString& text) {
  if (text.size() < 2 || text[0] != QLatin1Char('<')) {
    return {};
  }
  if (text.size() >= 2 && text[1] == QLatin1Char('/')) {
    return {};  // closing tag
  }
  qsizetype end = 1;
  while (end < text.size() && (text[end].isLetter() || text[end].isDigit())) {
    ++end;
  }
  return end > 1 ? QStringView(text).mid(1, end - 1) : QStringView();
}

QStringView extractClosingTagName(const QString& text) {
  if (text.size() < 3 || text[0] != QLatin1Char('<') || text[1] != QLatin1Char('/')) {
    return {};
  }
  qsizetype end = 2;
  while (end < text.size() && (text[end].isLetter() || text[end].isDigit())) {
    ++end;
  }
  return end > 2 ? QStringView(text).mid(2, end - 2) : QStringView();
}

// Extract a named attribute value from raw HTML tag text.
// Handles src="...", src='...', alt="...", etc.
QString extractHtmlAttr(const QString& tag, const QString& attrName) {
  const QRegularExpression re(
      QStringLiteral("(?:^|\\s)%1\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')")
          .arg(QRegularExpression::escape(attrName)),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = re.match(tag);
  if (!match.hasMatch()) return {};
  return match.captured(1).isNull() ? match.captured(2) : match.captured(1);
}

bool isSelfClosingBrTag(const QString& text) {
  if (text.size() < 3 || text[0] != QLatin1Char('<')) {
    return false;
  }
  const QStringView body = QStringView(text).mid(1).trimmed();
  return body.startsWith(u"br", Qt::CaseInsensitive) &&
         (body.size() == 2 || body[2] == QLatin1Char('>') || body[2] == QLatin1Char('/'));
}

}  // namespace

bool InlineProjectionState::shouldRevealSourceRange(qsizetype sourceStart, qsizetype sourceEnd) const {
  if (revealMarkdownMarkers) {
    return true;
  }
  if (cursorSourceOffset >= 0 && containsOffset(sourceStart, sourceEnd, cursorSourceOffset)) {
    return true;
  }
  return overlapsRange(selectionSourceStart, selectionSourceEnd, sourceStart, sourceEnd);
}

bool InlineProjectionState::shouldRevealVisibleRange(qsizetype visibleStart, qsizetype visibleEnd) const {
  if (revealMarkdownMarkers) {
    return true;
  }
  if (cursorVisibleOffset >= 0 && containsOffset(visibleStart, visibleEnd, cursorVisibleOffset)) {
    return true;
  }
  return overlapsRange(selectionVisibleStart, selectionVisibleEnd, visibleStart, visibleEnd);
}

InlineProjectionState InlineProjectionState::forCursor(
    const CursorPosition& cursor,
    NodeId blockId,
    qsizetype contentSourceStart) {
  SelectionRange selection;
  selection.anchor = cursor;
  selection.focus = cursor;
  return forSelection(selection, blockId, contentSourceStart);
}

InlineProjectionState InlineProjectionState::forSelection(
    const SelectionRange& selection,
    NodeId blockId,
    qsizetype contentSourceStart) {
  InlineProjectionState state;
  if (selection.focus.blockId != blockId) {
    return state;
  }

  state.cursorVisibleOffset = selection.focus.text.textOffset;
  state.cursorSourceOffset =
      selection.focus.text.sourceOffset >= 0 && contentSourceStart >= 0 ? selection.focus.text.sourceOffset - contentSourceStart : -1;

  if (!selection.isCollapsed() && selection.isSingleBlock()) {
    state.selectionVisibleStart = qMin(selection.anchor.text.textOffset, selection.focus.text.textOffset);
    state.selectionVisibleEnd = qMax(selection.anchor.text.textOffset, selection.focus.text.textOffset);
    if (selection.anchor.text.sourceOffset >= 0 && selection.focus.text.sourceOffset >= 0 && contentSourceStart >= 0) {
      state.selectionSourceStart = qMin(selection.anchor.text.sourceOffset, selection.focus.text.sourceOffset) - contentSourceStart;
      state.selectionSourceEnd = qMax(selection.anchor.text.sourceOffset, selection.focus.text.sourceOffset) - contentSourceStart;
    }
  }

  return state;
}

InlineProjection::InlineProjection(const QVector<InlineNode>& inlines, QString sourceText, InlineProjectionState projectionState, qsizetype sourceBase,
                                   qreal baseFontSize)
    : sourceText_(std::move(sourceText)), visibleText_(plainTextForInlines(inlines)) {
  BuildState state;
  state.sourceText = &sourceText_;
  state.sourceBase = sourceBase;
  state.projectionState = projectionState;
  state.baseFontSize = baseFontSize;
  QVector<HtmlInlineFormatData> htmlData;
  appendInlines(state, inlines, 0, sourceText_.size(), htmlData);
  displayText_ = state.displayText;
  visibleText_ = state.visibleText;
  spans_ = state.spans;
  linkRanges_ = state.linkRanges;
  htmlFormatData_ = std::move(htmlData);
  if (displayText_.isEmpty() && !sourceText_.isEmpty()) {
    appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, 0, sourceText_.size(), sourceText_, true);
    displayText_ = state.displayText;
    spans_ = state.spans;
  }
  valid_ = !spans_.isEmpty() || sourceText_.isEmpty();
  if (sourceText_.isEmpty() && displayText_.isEmpty()) {
    valid_ = true;
  }
}

bool InlineProjection::isValid() const {
  return valid_;
}

QString InlineProjection::sourceText() const {
  return sourceText_;
}

QString InlineProjection::displayText() const {
  return displayText_;
}

QString InlineProjection::visibleText() const {
  return visibleText_;
}

const QVector<InlineProjectionSpan>& InlineProjection::spans() const {
  return spans_;
}

const QVector<HtmlInlineFormatData>& InlineProjection::htmlFormatData() const {
  return htmlFormatData_;
}

QString InlineProjection::linkHrefAtDisplayOffset(qsizetype displayOffset) const {
  if (!valid_ || linkRanges_.isEmpty()) {
    return {};
  }
  for (const LinkRange& range : linkRanges_) {
    if (displayOffset >= range.displayStart && displayOffset < range.displayEnd) {
      return range.href;
    }
  }
  return {};
}

bool InlineProjection::sourceOffsetForVisibleOffset(qsizetype visibleOffset, qsizetype& sourceOffset) const {
  if (!valid_) {
    return false;
  }
  visibleOffset = qBound<qsizetype>(0, visibleOffset, visibleText_.size());
  if (visibleOffset == 0) {
    sourceOffset = 0;
    return true;
  }
  for (const InlineProjectionSpan& span : spans_) {
    if (visibleOffset <= span.visibleEnd) {
      if (span.visibleEnd <= span.visibleStart || span.sourceEnd <= span.sourceStart) {
        sourceOffset = span.sourceStart;
      } else if (visibleOffset >= span.visibleEnd) {
        sourceOffset = span.sourceEnd;
        for (const InlineProjectionSpan& following : spans_) {
          if (following.visibleStart == visibleOffset && following.visibleEnd == visibleOffset &&
              following.kind == InlineSpanKind::CloseMarker &&
              following.sourceStart >= sourceOffset && following.sourceEnd > following.sourceStart) {
            sourceOffset = following.sourceEnd;
          }
        }
      } else {
        sourceOffset = qBound<qsizetype>(
            span.contentSourceStart,
            span.contentSourceStart + visibleOffset - span.visibleStart,
            span.contentSourceEnd);
      }
      return true;
    }
  }
  sourceOffset = sourceText_.size();
  return true;
}

bool InlineProjection::visibleOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& visibleOffset) const {
  if (!valid_) {
    return false;
  }
  sourceOffset = qBound<qsizetype>(0, sourceOffset, sourceText_.size());
  for (const InlineProjectionSpan& span : spans_) {
    if (sourceOffset <= span.sourceEnd) {
      if (span.visibleEnd <= span.visibleStart || span.sourceEnd <= span.sourceStart) {
        visibleOffset = span.visibleStart;
      } else if (sourceOffset <= span.contentSourceStart) {
        visibleOffset = span.visibleStart;
      } else if (sourceOffset >= span.contentSourceEnd) {
        visibleOffset = span.visibleEnd;
      } else {
        visibleOffset = qBound<qsizetype>(
            span.visibleStart,
            span.visibleStart + sourceOffset - span.contentSourceStart,
            span.visibleEnd);
      }
      return true;
    }
  }
  visibleOffset = visibleText_.size();
  return true;
}

bool InlineProjection::sourceOffsetForDisplayOffset(qsizetype displayOffset, qsizetype& sourceOffset) const {
  return sourceOffsetForDisplayOffset(displayOffset, InlineProjectionBias::Backward, sourceOffset);
}

bool InlineProjection::sourceOffsetForDisplayOffset(qsizetype displayOffset, InlineProjectionBias bias, qsizetype& sourceOffset) const {
  if (!valid_) {
    return false;
  }
  displayOffset = qBound<qsizetype>(0, displayOffset, displayText_.size());
  if (bias == InlineProjectionBias::Forward) {
    for (qsizetype i = spans_.size() - 1; i >= 0; --i) {
      const InlineProjectionSpan& span = spans_.at(i);
      if (displayOffset < span.displayStart || displayOffset > span.displayEnd) {
        continue;
      }
      if (span.displayEnd <= span.displayStart || span.sourceEnd <= span.sourceStart) {
        sourceOffset = span.sourceEnd;
      } else if (displayOffset <= span.displayStart) {
        sourceOffset = span.sourceStart;
      } else if (displayOffset >= span.displayEnd) {
        sourceOffset = span.sourceEnd;
      } else {
        sourceOffset = qBound<qsizetype>(
            span.contentSourceStart,
            span.contentSourceStart + displayOffset - span.displayStart,
            span.contentSourceEnd);
      }
      return true;
    }
    sourceOffset = sourceText_.size();
    return true;
  }
  for (const InlineProjectionSpan& span : spans_) {
    if (displayOffset <= span.displayEnd) {
      if (span.displayEnd <= span.displayStart || span.sourceEnd <= span.sourceStart) {
        sourceOffset = span.sourceStart;
      } else if (displayOffset <= span.displayStart) {
        sourceOffset = span.contentSourceStart;
      } else if (displayOffset >= span.displayEnd) {
        sourceOffset = span.contentSourceEnd;
      } else {
        sourceOffset = qBound<qsizetype>(
            span.contentSourceStart,
            span.contentSourceStart + displayOffset - span.displayStart,
            span.contentSourceEnd);
      }
      return true;
    }
  }
  sourceOffset = sourceText_.size();
  return true;
}

bool InlineProjection::displayOffsetForSourceOffset(qsizetype sourceOffset, qsizetype& displayOffset) const {
  return displayOffsetForSourceOffset(sourceOffset, InlineProjectionBias::Backward, displayOffset);
}

bool InlineProjection::displayOffsetForSourceOffset(qsizetype sourceOffset, InlineProjectionBias bias, qsizetype& displayOffset) const {
  if (!valid_) {
    return false;
  }
  sourceOffset = qBound<qsizetype>(0, sourceOffset, sourceText_.size());
  if (bias == InlineProjectionBias::Forward) {
    for (qsizetype i = spans_.size() - 1; i >= 0; --i) {
      const InlineProjectionSpan& span = spans_.at(i);
      if (sourceOffset < span.sourceStart || sourceOffset > span.sourceEnd) {
        continue;
      }
      if (span.displayEnd <= span.displayStart || span.sourceEnd <= span.sourceStart) {
        displayOffset = span.displayEnd;
      } else if (sourceOffset <= span.contentSourceStart) {
        displayOffset = span.displayStart;
      } else if (sourceOffset >= span.contentSourceEnd) {
        displayOffset = span.displayEnd;
      } else {
        displayOffset = qBound<qsizetype>(
            span.displayStart,
            span.displayStart + sourceOffset - span.contentSourceStart,
            span.displayEnd);
      }
      return true;
    }
    displayOffset = displayText_.size();
    return true;
  }
  for (const InlineProjectionSpan& span : spans_) {
    if (sourceOffset <= span.sourceEnd) {
      if (span.displayEnd <= span.displayStart || span.sourceEnd <= span.sourceStart) {
        displayOffset = span.displayStart;
      } else if (sourceOffset <= span.contentSourceStart) {
        displayOffset = span.displayStart;
      } else if (sourceOffset >= span.contentSourceEnd) {
        displayOffset = span.displayEnd;
      } else {
        displayOffset = qBound<qsizetype>(
            span.displayStart,
            span.displayStart + sourceOffset - span.contentSourceStart,
            span.displayEnd);
      }
      return true;
    }
  }
  displayOffset = displayText_.size();
  return true;
}

QString InlineProjection::plainTextForInlines(const QVector<InlineNode>& inlines) {
  QString text;
  for (const InlineNode& node : inlines) {
    text += plainTextForInline(node);
  }
  return text;
}

bool InlineProjection::isPlainInlineSource(const QVector<InlineNode>& inlines, const QString& sourceText, qsizetype sourceBase) {
  InlineProjection projection(inlines, sourceText, InlineProjectionState{}, sourceBase);
  if (!projection.isValid()) {
    return false;
  }
  for (const InlineProjectionSpan& span : projection.spans()) {
    if (span.kind != InlineSpanKind::Text || span.type != InlineType::Text) {
      return false;
    }
  }
  return projection.visibleText() == sourceText;
}

QString InlineProjection::markerForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Code:
      return QStringLiteral("`");
    case InlineType::InlineMath:
      return QStringLiteral("$");
    case InlineType::Emphasis:
      return node.marker().isEmpty() ? QStringLiteral("*") : node.marker();
    case InlineType::Strong:
      return node.marker().isEmpty() ? QStringLiteral("**") : node.marker();
    case InlineType::Strikethrough:
      return QStringLiteral("~~");
    default:
      return {};
  }
}

QString InlineProjection::markdownForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
      return node.text();
    case InlineType::SoftBreak:
      return QStringLiteral("\n");
    case InlineType::LineBreak:
      return QStringLiteral("  \n");
    case InlineType::Code:
      return QStringLiteral("`%1`").arg(node.text());
    case InlineType::InlineMath:
      return QStringLiteral("$%1$").arg(node.text());
    case InlineType::HtmlInline:
      return node.text();
    case InlineType::Emphasis:
    case InlineType::Strong:
      return QStringLiteral("%1%2%1").arg(markerForInline(node), markdownForInlines(node.children()));
    case InlineType::Strikethrough:
      return QStringLiteral("~~%1~~").arg(markdownForInlines(node.children()));
    case InlineType::Link: {
      const QString label = markdownForInlines(node.children());
      if (node.isAutolink()) {
        return label;
      }
      return QStringLiteral("[%1](%2%3)").arg(
          label,
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    }
    case InlineType::Image:
      return QStringLiteral("![%1](%2%3)").arg(
          node.alt(),
          node.href(),
          node.title().isEmpty() ? QString() : QStringLiteral(" \"%1\"").arg(node.title()));
    default:
      return node.text();
  }
}

QString InlineProjection::markdownForInlines(const QVector<InlineNode>& inlines) {
  QString markdown;
  for (const InlineNode& node : inlines) {
    markdown += markdownForInline(node);
  }
  return markdown;
}

QString InlineProjection::plainTextForInline(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Text:
    case InlineType::Code:
    case InlineType::InlineMath:
    case InlineType::HtmlInline:
      return node.text();
    case InlineType::SoftBreak:
      return QStringLiteral(" ");
    case InlineType::LineBreak:
      return QStringLiteral("\n");
    case InlineType::Image:
      return node.alt();
    default:
      return plainTextForInlines(node.children());
  }
}

void InlineProjection::appendTextSpan(
    BuildState& state,
    InlineType type,
    InlineSpanKind kind,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    QString displayText,
    bool visible,
    bool editable) {
  appendTextSpan(state, type, kind, sourceStart, sourceEnd, sourceStart, sourceEnd, std::move(displayText), visible, editable);
}

void InlineProjection::appendTextSpan(
    BuildState& state,
    InlineType type,
    InlineSpanKind kind,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype contentSourceStart,
    qsizetype contentSourceEnd,
    QString displayText,
    bool visible,
    bool editable) {
  InlineProjectionSpan span;
  span.type = type;
  span.kind = kind;
  span.sourceStart = sourceStart;
  span.sourceEnd = sourceEnd;
  span.contentSourceStart = contentSourceStart;
  span.contentSourceEnd = contentSourceEnd;
  span.displayStart = state.displayOffset;
  span.displayEnd = state.displayOffset + displayText.size();
  span.visibleStart = state.visibleOffset;
  span.visibleEnd = state.visibleOffset + (visible ? displayText.size() : 0);
  span.editable = editable;
  span.bold = state.bold;
  span.italic = state.italic;
  span.strike = state.strike;
  state.displayText += displayText;
  if (visible) {
    state.visibleText += displayText;
    state.visibleOffset = span.visibleEnd;
  }
  state.displayOffset = span.displayEnd;
  state.spans.push_back(span);
}

bool InlineProjection::appendHtmlImageAtom(
    BuildState& state,
    const QString& tagText,
    qsizetype sourceStart,
    qsizetype sourceEnd,
    qsizetype contentSourceStart,
    qsizetype contentSourceEnd) {
  const QString src = extractHtmlAttr(tagText, QStringLiteral("src"));
  if (src.isEmpty()) {
    return false;
  }
  const QString alt = extractHtmlAttr(tagText, QStringLiteral("alt"));
  appendTextSpan(state, InlineType::Image, InlineSpanKind::Atom,
                 sourceStart, sourceEnd, contentSourceStart, contentSourceEnd,
                 alt.isEmpty() ? QString(QChar::Space) : alt, true);
  state.spans.last().href = src;
  return true;
}

void InlineProjection::appendInlines(BuildState& state, const QVector<InlineNode>& inlines, qsizetype sourceStart, qsizetype sourceEnd,
                                     QVector<HtmlInlineFormatData>& htmlFormatData) {
  qsizetype searchFrom = sourceStart;
  for (int i = 0; i < inlines.size(); ++i) {
    const InlineNode& node = inlines[i];
    const QString markdown = markdownForInline(node);
    qsizetype nodeStart = -1;
    qsizetype nodeEnd = -1;

    // Use parser-provided source positions when available.
    const InlineRange parserRange = localRange(node.sourceRange(), state.sourceBase);
    if (rangeWithin(parserRange, sourceStart, sourceEnd) && parserRange.start >= searchFrom) {
      nodeStart = parserRange.start;
      nodeEnd = parserRange.end;
    }

    // Fallback: substring search for nodes without stored positions
    if (nodeStart < 0) {
      nodeStart = findMarkdown(*state.sourceText, markdown, searchFrom, sourceEnd);
      nodeEnd = nodeStart >= 0 ? nodeStart + markdown.size() : -1;
    }

    if (nodeStart < 0) {
      // Fallback: the reconstructed markdown didn't match the source text exactly
      const QString plainText = plainTextForInline(node);
      const qsizetype textPos = plainText.isEmpty() ? qsizetype(-1) : state.sourceText->indexOf(plainText, searchFrom);
      if (textPos >= 0 && textPos + plainText.size() <= sourceEnd) {
        if (textPos > searchFrom) {
          appendTextSpan(
              state,
              InlineType::Text,
              InlineSpanKind::Text,
              searchFrom,
              textPos,
              state.sourceText->mid(searchFrom, textPos - searchFrom),
              true);
        }
        appendTextSpan(
            state,
            node.type(),
            InlineSpanKind::Text,
            textPos,
            textPos + plainText.size(),
            plainText,
            true);
        searchFrom = textPos + plainText.size();
      }
      continue;
    }
    if (nodeStart > searchFrom) {
      appendTextSpan(
          state,
          InlineType::Text,
          InlineSpanKind::Text,
          searchFrom,
          nodeStart,
          state.sourceText->mid(searchFrom, nodeStart - searchFrom),
          true);
    }

    // Try to group inline HTML sequences into a single renderable unit.
    if (node.type() == InlineType::HtmlInline) {
      // Handle standalone <img> tags as inline images (same as Markdown ![alt](src))
      const QStringView imgTagName = extractOpeningTagName(node.text());
      if (imgTagName.compare(u"img", Qt::CaseInsensitive) == 0) {
        if (appendHtmlImageAtom(state, node.text(), nodeStart, nodeEnd, nodeStart, nodeEnd)) {
          searchFrom = nodeEnd;
          continue;
        }
      }

      const int consumed = tryAppendHtmlInlineGroup(state, inlines, i, nodeStart, sourceEnd, searchFrom, htmlFormatData);
      if (consumed > 0) {
        // Advance past consumed nodes. The loop's ++i handles one increment.
        // We need to update searchFrom to past the last consumed node.
        // searchFrom is updated inside tryAppendHtmlInlineGroup via the last nodeEnd.
        i += consumed - 1;
        // searchFrom is already updated by the successful group handling.
        continue;
      }
    }

    appendInline(state, node, nodeStart, nodeEnd, htmlFormatData);
    searchFrom = nodeEnd;
  }
  if (searchFrom < sourceEnd) {
    appendTextSpan(
        state,
        InlineType::Text,
        InlineSpanKind::Text,
        searchFrom,
        sourceEnd,
        state.sourceText->mid(searchFrom, sourceEnd - searchFrom),
        true);
  }
}

int InlineProjection::tryAppendHtmlInlineGroup(BuildState& state, const QVector<InlineNode>& inlines, int index,
                                               qsizetype nodeStart, qsizetype sourceEnd, qsizetype& searchFrom,
                                               QVector<HtmlInlineFormatData>& htmlFormatData) {
  const InlineNode& openNode = inlines[index];
  const QString& openText = openNode.text();

  // Self-closing void tags like <br> should stay as raw text.
  // The table cell editing code depends on <br> appearing as raw text
  // in the projection so its offset calculations remain consistent.
  if (isSelfClosingBrTag(openText)) {
    return 0;
  }

  // Must be an opening tag with a renderable tag name
  const QStringView tagName = extractOpeningTagName(openText);
  if (tagName.isEmpty() || !html::InlineHtmlRenderer::isRenderableTag(tagName)) {
    return 0;
  }

  // Scan forward for matching closing tag
  const QString closeTag = QStringLiteral("</%1>").arg(tagName.toString());
  int closeIndex = -1;
  for (int j = index + 1; j < inlines.size(); ++j) {
    if (inlines[j].type() == InlineType::HtmlInline) {
      const QStringView closingName = extractClosingTagName(inlines[j].text());
      if (closingName.compare(tagName, Qt::CaseInsensitive) == 0) {
        closeIndex = j;
        break;
      }
    }
  }

  if (closeIndex < 0) {
    // No matching closing tag — fall back to raw text
    return 0;
  }

  // Compute source positions for the entire group
  const InlineRange openParserRange = localRange(openNode.sourceRange(), state.sourceBase);
  const qsizetype openStart = nodeStart;
  const qsizetype openEnd = openStart + openText.size();

  const InlineNode& closeNode = inlines[closeIndex];
  const QString closeMarkdown = markdownForInline(closeNode);
  // The close tag's source start is after all consumed nodes' text.
  // We need to compute sourceEnd for the close tag. Let's find it via the parser range or search.
  qsizetype closeNodeStart = -1;
  const InlineRange closeParserRange = localRange(closeNode.sourceRange(), state.sourceBase);
  if (rangeWithin(closeParserRange, sourceEnd > 0 ? qsizetype(0) : qsizetype(0), sourceEnd) && closeParserRange.start >= openEnd) {
    closeNodeStart = closeParserRange.start;
  }
  if (closeNodeStart < 0) {
    // Fallback: search for the close tag text in source after the open tag
    closeNodeStart = state.sourceText->indexOf(closeMarkdown, openEnd);
  }
  if (closeNodeStart < 0) {
    return 0;
  }
  const qsizetype closeEnd = closeNodeStart + closeNode.text().size();

  // Build the HTML fragment from all consumed nodes
  QString htmlFragment;
  for (int j = index; j <= closeIndex; ++j) {
    htmlFragment += inlines[j].text();
  }

  // Render via InlineHtmlRenderer — use the actual paragraph font size
  static const html::InlineHtmlRenderer renderer;
  const html::InlineHtmlFormatResult rendered = renderer.render(htmlFragment, state.baseFontSize);

  // Determine active state (cursor on the HTML group)
  const qsizetype visibleStart = state.visibleOffset;
  const qsizetype visibleEnd = visibleStart + rendered.text.size();
  const bool active = state.projectionState.shouldRevealSourceRange(openStart, closeEnd) ||
                      state.projectionState.shouldRevealVisibleRange(visibleStart, visibleEnd);

  if (active) {
    // When active, show raw HTML tags as visible text (like marker reveal for **bold**)
    appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::OpenMarker, openStart, openEnd,
                   state.sourceText->mid(openStart, openEnd - openStart), false);

    // Emit the content nodes between open and close tags
    // We need to process intermediate nodes (Text + HtmlInline) for source position accuracy
    qsizetype contentSourceStart = openEnd;
    for (int j = index + 1; j < closeIndex; ++j) {
      const InlineNode& mid = inlines[j];
      const QString midMd = markdownForInline(mid);
      // Find source position for this intermediate node
      qsizetype midStart = -1;
      const InlineRange midParserRange = localRange(mid.sourceRange(), state.sourceBase);
      if (rangeWithin(midParserRange, openEnd, closeNodeStart) && midParserRange.start >= contentSourceStart) {
        midStart = midParserRange.start;
      }
      if (midStart < 0) {
        midStart = state.sourceText->indexOf(midMd, contentSourceStart);
      }
      if (midStart >= 0 && midStart + midMd.size() <= closeNodeStart) {
        if (midStart > contentSourceStart) {
          appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, contentSourceStart, midStart,
                         state.sourceText->mid(contentSourceStart, midStart - contentSourceStart), true);
        }
        appendInline(state, mid, midStart, midStart + midMd.size(), htmlFormatData);
        contentSourceStart = midStart + midMd.size();
      }
    }

    appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::CloseMarker, closeNodeStart, closeEnd,
                   state.sourceText->mid(closeNodeStart, closeEnd - closeNodeStart), false);
  } else {
    // When not active, render as formatted text — no markers in display text
    // (same pattern as markdown emphasis: content only when inactive, markers appear when cursor enters)

    // Check for image-only content (e.g., <a href="..."><img src="..."></a>)
    // InlineHtmlRenderer produces empty text for <img> since it has no text children.
    // Detect this case and emit an Atom span so the existing image pipeline handles it.
    if (rendered.text.trimmed().isEmpty()) {
      bool appendedImage = false;
      for (int j = index + 1; j < closeIndex; ++j) {
        const InlineNode& mid = inlines[j];
        if (mid.type() == InlineType::HtmlInline) {
          const QStringView midTag = extractOpeningTagName(mid.text());
          if (midTag.compare(u"img", Qt::CaseInsensitive) == 0) {
            appendedImage = appendHtmlImageAtom(state, mid.text(), openStart, closeEnd, openEnd, closeNodeStart) || appendedImage;
          }
        }
      }
      if (appendedImage) {
        searchFrom = closeEnd;
        return closeIndex - index + 1;
      }
    }

    // HtmlContent span (visible rendered text)
    const qsizetype contentDisplayStart = state.displayOffset;
    appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::HtmlContent, openStart, closeEnd,
                   openEnd, closeNodeStart, rendered.text, true);

    // Register format data
    if (!rendered.formatSpans.empty() || !rendered.links.empty()) {
      HtmlInlineFormatData data;
      data.formatSpans = std::move(rendered.formatSpans);
      data.links = std::move(rendered.links);
      data.displayStart = contentDisplayStart;
      htmlFormatData.push_back(std::move(data));
    }

    // Register link ranges
    for (const auto& link : rendered.links) {
      state.linkRanges.push_back({contentDisplayStart + link.start, contentDisplayStart + link.start + link.length, link.href});
    }
  }

  // Advance searchFrom past the entire consumed group
  searchFrom = closeEnd;
  return closeIndex - index + 1;
}

void InlineProjection::appendInline(BuildState& state, const InlineNode& node, qsizetype sourceStart, qsizetype sourceEnd,
                                    QVector<HtmlInlineFormatData>& htmlFormatData) {
  const QString marker = markerForInline(node);
  const qsizetype displayStart = state.displayOffset;
  const qsizetype visibleStart = state.visibleOffset;
  const qsizetype visibleEnd = visibleStart + plainTextForInlines(QVector<InlineNode>{node}).size();
  const bool active = state.projectionState.shouldRevealSourceRange(sourceStart, sourceEnd) ||
                      state.projectionState.shouldRevealVisibleRange(visibleStart, visibleEnd);
  switch (node.type()) {
    case InlineType::Text: {
      const QString source = state.sourceText->mid(sourceStart, sourceEnd - sourceStart);
      const QString& decoded = node.text();
      if (source == decoded) {
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, decoded, true);
        break;
      }
      const QVector<EntitySpan> entities = findHtmlEntities(source);
      if (entities.isEmpty()) {
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, decoded, true);
        break;
      }
      bool aligned = true;
      qsizetype sourcePos = 0;
      qsizetype decodedPos = 0;
      for (const EntitySpan& entity : entities) {
        const QStringView plainSource = QStringView(source).mid(sourcePos, entity.sourceStart - sourcePos);
        if (!textMatchesAt(decoded, decodedPos, plainSource)) {
          aligned = false;
          break;
        }
        decodedPos += plainSource.size();
        if (!textMatchesAt(decoded, decodedPos, QStringView(entity.decodedText))) {
          aligned = false;
          break;
        }
        decodedPos += entity.decodedText.size();
        sourcePos = entity.sourceEnd;
      }
      if (aligned && !textMatchesAt(decoded, decodedPos, QStringView(source).mid(sourcePos))) {
        aligned = false;
      }
      if (!aligned) {
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, decoded, true);
        break;
      }

      sourcePos = 0;
      decodedPos = 0;
      for (const EntitySpan& entity : entities) {
        if (entity.sourceStart > sourcePos) {
          const qsizetype plainLen = entity.sourceStart - sourcePos;
          appendTextSpan(state, node.type(), InlineSpanKind::Text,
                          sourceStart + sourcePos, sourceStart + entity.sourceStart,
                          source.mid(sourcePos, plainLen), true);
          decodedPos += plainLen;
        }
        const qsizetype entitySourceStart = sourceStart + entity.sourceStart;
        const qsizetype entitySourceEnd = sourceStart + entity.sourceEnd;
        const qsizetype entityVisibleStart = visibleStart + decodedPos;
        const qsizetype entityVisibleEnd = entityVisibleStart + entity.decodedText.size();
        const bool revealEntity = state.projectionState.shouldRevealSourceRange(entitySourceStart, entitySourceEnd) ||
                                  state.projectionState.shouldRevealVisibleRange(entityVisibleStart, entityVisibleEnd);

        appendTextSpan(state, node.type(), InlineSpanKind::Text,
                        entitySourceStart, entitySourceEnd,
                        entity.decodedText, true);
        if (revealEntity) {
          appendTextSpan(state, node.type(), InlineSpanKind::HiddenSyntax,
                          entitySourceStart, entitySourceEnd,
                          source.mid(entity.sourceStart, entity.sourceEnd - entity.sourceStart), false);
        }
        sourcePos = entity.sourceEnd;
        decodedPos += entity.decodedText.size();
      }
      if (sourcePos < source.size()) {
        appendTextSpan(state, node.type(), InlineSpanKind::Text,
                        sourceStart + sourcePos, sourceEnd,
                        decoded.mid(decodedPos), true);
      }
      break;
    }
    case InlineType::SoftBreak:
      appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, QStringLiteral(" "), true);
      break;
    case InlineType::LineBreak:
      appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, QStringLiteral("\n"), true);
      break;
    case InlineType::Code:
    case InlineType::InlineMath: {
      InlineRange content = localRange(node.contentRange(), state.sourceBase);
      if (!rangeWithin(content, sourceStart, sourceEnd)) {
        content = InlineRange{qMin(sourceEnd, sourceStart + marker.size()), qMax(sourceStart, sourceEnd - marker.size())};
      }
      const qsizetype contentStart = content.start;
      const qsizetype contentEnd = qMax(contentStart, content.end);
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::OpenMarker, sourceStart, contentStart, state.sourceText->mid(sourceStart, contentStart - sourceStart), false);
      }
      appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, contentStart, contentEnd, node.text(), true);
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::CloseMarker, contentEnd, sourceEnd, state.sourceText->mid(contentEnd, sourceEnd - contentEnd), false);
      }
      break;
    }
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Strikethrough: {
      InlineRange content = localRange(node.contentRange(), state.sourceBase);
      if (!rangeWithin(content, sourceStart, sourceEnd)) {
        content = InlineRange{qMin(sourceEnd, sourceStart + marker.size()), qMax(sourceStart, sourceEnd - marker.size())};
      }
      const qsizetype contentStart = content.start;
      const qsizetype contentEnd = qMax(contentStart, content.end);
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::OpenMarker, sourceStart, contentStart, state.sourceText->mid(sourceStart, contentStart - sourceStart), false);
      }
      const bool previousBold = state.bold;
      const bool previousItalic = state.italic;
      const bool previousStrike = state.strike;
      if (node.type() == InlineType::Strong) {
        state.bold = true;
      } else if (node.type() == InlineType::Emphasis) {
        state.italic = true;
      } else if (node.type() == InlineType::Strikethrough) {
        state.strike = true;
      }
      appendInlines(state, node.children(), contentStart, contentEnd, htmlFormatData);
      state.bold = previousBold;
      state.italic = previousItalic;
      state.strike = previousStrike;
      if (contentStart == contentEnd) {
        appendTextSpan(state, node.type(), InlineSpanKind::EmptyContentSlot, contentStart, contentEnd, QString(), false);
      }
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::CloseMarker, contentEnd, sourceEnd, state.sourceText->mid(contentEnd, sourceEnd - contentEnd), false);
      }
      if (!active && state.spans.size() > 0) {
        const qsizetype displayEnd = state.displayOffset;
        const qsizetype visibleEnd = state.visibleOffset;
        for (InlineProjectionSpan& span : state.spans) {
          if (span.displayStart >= displayStart && span.displayEnd <= displayEnd && span.visibleStart >= visibleStart &&
              span.visibleEnd <= visibleEnd) {
            span.sourceStart = sourceStart;
            span.sourceEnd = sourceEnd;
          }
        }
      }
      break;
    }
    case InlineType::HtmlInline:
      appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, node.text(), true);
      break;
    case InlineType::Link: {
      const QString label = markdownForInlines(node.children());
      if (node.isAutolink()) {
        InlineRange content = localRange(node.contentRange(), state.sourceBase);
        if (!rangeWithin(content, sourceStart, sourceEnd)) {
          content = InlineRange{sourceStart, sourceEnd};
        }
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, content.start, content.end, label, true);
        state.linkRanges.push_back({displayStart, state.displayOffset, node.href()});
        break;
      }
      InlineRange openMarker = localRange(node.openMarkerRange(), state.sourceBase);
      InlineRange content = localRange(node.contentRange(), state.sourceBase);
      const qsizetype contentStart = rangeWithin(openMarker, sourceStart, sourceEnd) ? openMarker.end : qMin(sourceEnd, sourceStart + 1);
      const qsizetype contentEnd = rangeWithin(content, sourceStart, sourceEnd) ? content.end : contentStart;
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::OpenMarker, sourceStart, contentStart, state.sourceText->mid(sourceStart, contentStart - sourceStart), false);
      }
      appendInlines(state, node.children(), contentStart, contentEnd, htmlFormatData);
      for (InlineProjectionSpan& span : state.spans) {
        if (span.displayStart >= displayStart && span.displayEnd <= state.displayOffset) {
          span.type = InlineType::Link;
        }
      }
      state.linkRanges.push_back({displayStart, state.displayOffset, node.href()});
      if (active) {
        appendTextSpan(state, node.type(), InlineSpanKind::HiddenSyntax, contentEnd, sourceEnd, state.sourceText->mid(contentEnd, sourceEnd - contentEnd), false);
      }
      break;
    }
    case InlineType::Image: {
      if (!active) {
        appendTextSpan(state, node.type(), InlineSpanKind::Atom, sourceStart, sourceEnd, node.alt(), true);
        state.spans.last().href = node.href();
        break;
      }
      InlineRange openMarker = localRange(node.openMarkerRange(), state.sourceBase);
      InlineRange content = localRange(node.contentRange(), state.sourceBase);
      const qsizetype labelStart = rangeWithin(openMarker, sourceStart, sourceEnd) ? openMarker.end : sourceStart;
      const qsizetype labelEnd = rangeWithin(content, sourceStart, sourceEnd) ? content.end : labelStart;
      if (labelStart > sourceStart) {
        appendTextSpan(state, node.type(), InlineSpanKind::OpenMarker, sourceStart, labelStart, state.sourceText->mid(sourceStart, labelStart - sourceStart), false);
      }
      appendTextSpan(state, node.type(), InlineSpanKind::Atom, sourceStart, sourceEnd, labelStart, labelEnd, node.alt(), true);
      state.spans.last().href = node.href();
      if (labelEnd < sourceEnd) {
        appendTextSpan(state, node.type(), InlineSpanKind::HiddenSyntax, labelEnd, sourceEnd, state.sourceText->mid(labelEnd, sourceEnd - labelEnd), false);
      }
      break;
    }
    default:
      appendInlines(state, node.children(), sourceStart, sourceEnd, htmlFormatData);
      break;
  }
}

qsizetype InlineProjection::findMarkdown(const QString& sourceText, const QString& markdown, qsizetype searchFrom, qsizetype searchEnd) {
  if (markdown.isEmpty()) {
    return qBound<qsizetype>(0, searchFrom, sourceText.size());
  }
  if (searchFrom < 0 || searchFrom + markdown.size() > searchEnd || searchFrom + markdown.size() > sourceText.size()) {
    return -1;
  }
  return QStringView(sourceText).mid(searchFrom, markdown.size()) == QStringView(markdown) ? searchFrom : -1;
}

bool InlineProjection::offsetInSource(qsizetype sourceOffset) const {
  return sourceOffset >= 0 && sourceOffset <= sourceText_.size();
}

}  // namespace muffin
