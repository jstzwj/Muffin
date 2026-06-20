#include "projection/InlineProjection.h"

#include "editor/SmartPunctuation.h"
#include "houdini.h"
#include "html/InlineHtmlRenderer.h"

#include <QRegularExpression>

#include <optional>

namespace muffin {
namespace {

// A render-level smart-punctuation fold: a run of N source characters that renders as a single
// display glyph (e.g. "--" -> en-dash, "---" -> em-dash, "..." -> ellipsis). Quotes are 1:1 and do
// not fold. Tracking folds lets the projection emit a separate span per folded token, so the offset
// map stays exact (no per-span linear drift) and edits can act on the whole source token.
struct SmartPunctFold {
  qsizetype displayStart = 0;  // offset within the converted display text
  qsizetype displayEnd = 0;
  qsizetype sourceLength = 0;  // source characters mapped onto [displayStart, displayEnd)
};

struct SmartPunctResult {
  QString text;
  QVector<SmartPunctFold> folds;
};

// Convert ASCII quotes/dashes/ellipsis to their Unicode forms for display, recording each folded
// token. `prev` seeds the opening/closing-quote decision from text emitted before this run and
// carries the backslash-escape state (a char right after '\' stays literal).
SmartPunctResult applySmartPunctForRender(const QString& text, QChar prev, const SmartPunctRenderOptions& opts) {
  SmartPunctResult result;
  QString& out = result.text;
  out.reserve(text.size() + 4);
  if (!opts.convertQuotes && !opts.convertDashes && !opts.convertEllipsis) {
    out = text;
    return result;
  }
  const bool convertQuotes = opts.convertQuotes && (opts.doubleQuoteStyle == 0 || opts.singleQuoteStyle == 0);
  QChar p = prev;
  for (qsizetype i = 0; i < text.size(); ) {
    const QChar c = text.at(i);
    // Backslash escape: the following character stays literal.
    if (p == QLatin1Char('\\')) {
      out += c;
      p = c;
      ++i;
      continue;
    }
    // Dash run: 3+ -> em-dash, 2 -> en-dash, lone '-' stays literal.
    if (opts.convertDashes && c == QLatin1Char('-')) {
      qsizetype run = 0;
      while (i + run < text.size() && text.at(i + run) == QLatin1Char('-')) ++run;
      if (run >= 3) {
        out += smartpunct::kEmDash;
        result.folds.append({out.size() - 1, out.size(), run});
      } else if (run == 2) {
        out += smartpunct::kEnDash;
        result.folds.append({out.size() - 1, out.size(), 2});
      } else {
        out += c;
      }
      i += run;
      p = QLatin1Char('-');
      continue;
    }
    // Ellipsis: "..." -> …
    if (opts.convertEllipsis && c == QLatin1Char('.') && i + 3 <= text.size() &&
        text.at(i + 1) == QLatin1Char('.') && text.at(i + 2) == QLatin1Char('.')) {
      out += smartpunct::kEllipsis;
      result.folds.append({out.size() - 1, out.size(), 3});
      i += 3;
      p = QLatin1Char('.');
      continue;
    }
    // Quotes: 1:1 conversion, no fold.
    if (convertQuotes && c == QLatin1Char('"') && opts.doubleQuoteStyle == 0) {
      out += smartpunct::isOpeningQuoteContext(p) ? smartpunct::kLeftDoubleQuote : smartpunct::kRightDoubleQuote;
      p = c;
      ++i;
      continue;
    }
    if (convertQuotes && c == QLatin1Char('\'') && opts.singleQuoteStyle == 0) {
      out += smartpunct::isOpeningQuoteContext(p) ? smartpunct::kLeftSingleQuote : smartpunct::kRightSingleQuote;
      p = c;
      ++i;
      continue;
    }
    out += c;
    p = c;
    ++i;
  }
  return result;
}

// A source substring that decodes to a shorter visible run, breaking the 1:1
// source/visible correspondence the projection offset-mapping relies on. Covers
// both CommonMark backslash escapes (`\*` -> `*`) and HTML entities
// (`&amp;` -> `&`); the Text-node builder splits the node around these so each
// resulting plain segment keeps a clean 1:1 mapping.
struct DecodeSpan {
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

// CommonMark §2.4: a backslash escapes any ASCII punctuation character. Anything
// else (digits, letters, end of input) leaves the backslash literal, which does
// not change the source/visible length, so it needs no special handling here.
bool isBackslashEscapablePunct(QChar ch) {
  const ushort c = ch.unicode();
  return c >= 0x21 && c <= 0x7e &&
         !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

// Source-ordered, non-overlapping escape and entity runs. An escaped ampersand
// (`\&`) is consumed as an escape, so it never also opens an entity scan.
QVector<DecodeSpan> findDecodeSpans(QStringView source) {
  QVector<DecodeSpan> spans;
  qsizetype i = 0;
  while (i < source.size()) {
    if (source.at(i) == QLatin1Char('\\') && i + 1 < source.size() &&
        isBackslashEscapablePunct(source.at(i + 1))) {
      spans.push_back({i, i + 2, QString(source.at(i + 1))});
      i += 2;
      continue;
    }
    if (source.at(i) == QLatin1Char('&')) {
      qsizetype semi = source.indexOf(QLatin1Char(';'), i + 1);
      if (semi >= 0 && semi - i <= 33) {
        const QString decoded = decodeHtmlEntity(source.mid(i, semi + 1 - i));
        if (!decoded.isEmpty()) {
          spans.push_back({i, semi + 1, decoded});
          i = semi + 1;
          continue;
        }
      }
    }
    ++i;
  }
  return spans;
}

bool textMatchesAt(const QString& decoded, qsizetype decodedPos, QStringView text) {
  return decodedPos >= 0 && decodedPos + text.size() <= decoded.size() && decoded.mid(decodedPos, text.size()) == text;
}

// Half-open [start, end): a caret at `end` sits *past* the inline's closing marker, so it is
// treated as outside. This mirrors overlapsRange (selection) and is what keeps a TRAILING inline
// at end-of-block from staying "active" forever — the caret that rests there has moved on, so its
// markers should hide. The caret at `start` (on the opening marker) still counts as inside.
bool containsOffset(qsizetype start, qsizetype end, qsizetype offset) {
  return offset >= start && offset < end;
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

}  // namespace

int brTagLengthAt(const QString& content, qsizetype offset) {
  const qsizetype size = content.size();
  if (offset < 0 || offset + 4 > size || content.at(offset) != QLatin1Char('<')) {
    return 0;
  }
  const QChar c1 = content.at(offset + 1);
  const QChar c2 = content.at(offset + 2);
  if ((c1 != QLatin1Char('b') && c1 != QLatin1Char('B')) ||
      (c2 != QLatin1Char('r') && c2 != QLatin1Char('R'))) {
    return 0;
  }
  // After "<br": HTML5 allows any whitespace and an optional self-closing slash before '>'.
  // Covers <br>, <br/>, <br />, <br >, <br   >, <br / >. Rejects tags with attributes
  // (<br class=x>) or a different name (<break>) — those fall through to literal text.
  qsizetype i = offset + 3;
  bool seenSlash = false;
  while (i < size) {
    const QChar c = content.at(i);
    if (c == QLatin1Char('>')) {
      return static_cast<int>(i + 1 - offset);
    }
    if (c == QLatin1Char('/') && !seenSlash) {
      seenSlash = true;
      ++i;
      continue;
    }
    if (c.isSpace()) {
      ++i;
      continue;
    }
    return 0;  // an attribute name or other char — not a bare <br> tag
  }
  return 0;  // no closing '>'
}

bool isStandaloneBrTag(const QString& text) {
  const int length = brTagLengthAt(text, 0);
  return length > 0 && length == text.size();
}

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
                                   qreal baseFontSize, qsizetype pendingPrefixLength, SmartPunctRenderOptions smartPunct)
    : sourceText_(std::move(sourceText)), visibleText_(plainTextForInlines(inlines)) {
  BuildState state;
  state.sourceText = &sourceText_;
  state.sourceBase = sourceBase;
  state.projectionState = projectionState;
  state.baseFontSize = baseFontSize;
  state.smartPunct = smartPunct;
  QVector<HtmlInlineFormatData> htmlData;
  if (pendingPrefixLength > 0 && pendingPrefixLength <= sourceText_.size()) {
    // A still-uncommitted fence/math opener: show the marker in the muted "syntax" color the
    // whole time it is being typed (stays visible, never bold).
    const QString prefixText = sourceText_.left(pendingPrefixLength);
    appendTextSpan(state, InlineType::Text, InlineSpanKind::OpenMarker, 0, pendingPrefixLength, prefixText, true);
    appendInlines(state, inlines, pendingPrefixLength, sourceText_.size(), htmlData);
  } else {
    appendInlines(state, inlines, 0, sourceText_.size(), htmlData);
  }
  displayText_ = state.displayText;
  visibleText_ = state.visibleText;
  spans_ = state.spans;
  linkRanges_ = state.linkRanges;
  htmlFormatData_ = std::move(htmlData);
  if (spans_.isEmpty() && !sourceText_.isEmpty()) {
    appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, 0, sourceText_.size(), sourceText_, true);
    displayText_ = state.displayText;
    visibleText_ = state.visibleText;
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

bool InlineProjection::foldedTokenForDeletion(qsizetype offset, int direction, qsizetype& start, qsizetype& end) const {
  for (const InlineProjectionSpan& span : spans_) {
    if (!span.folded) continue;
    if (direction < 0) {  // backspace: caret at the token's end -> remove the whole token
      if (offset == span.sourceEnd) {
        start = span.sourceStart;
        end = span.sourceEnd;
        return true;
      }
    } else {  // delete: caret at the token's start -> remove the whole token forward
      if (offset == span.sourceStart) {
        start = span.sourceStart;
        end = span.sourceEnd;
        return true;
      }
    }
  }
  return false;
}

bool InlineProjection::foldedSpanInterior(qsizetype offset, qsizetype& start, qsizetype& end) const {
  for (const InlineProjectionSpan& span : spans_) {
    if (span.folded && offset > span.sourceStart && offset < span.sourceEnd) {
      start = span.sourceStart;
      end = span.sourceEnd;
      return true;
    }
  }
  return false;
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
    case InlineType::Highlight:
      return QStringLiteral("==");
    case InlineType::Subscript:
      return QStringLiteral("~");
    case InlineType::Superscript:
      return QStringLiteral("^");
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
    case InlineType::Highlight:
      return QStringLiteral("==%1==").arg(markdownForInlines(node.children()));
    case InlineType::Subscript:
      return QStringLiteral("~%1~").arg(markdownForInlines(node.children()));
    case InlineType::Superscript:
      return QStringLiteral("^%1^").arg(markdownForInlines(node.children()));
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
      return node.text();
    case InlineType::HtmlInline:
      // <br> contributes a line break to plain text (column width / row height / copy);
      // other inline HTML keeps its literal source text.
      return isStandaloneBrTag(node.text()) ? QStringLiteral("\n") : node.text();
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
  span.underline = state.underline;
  span.highlight = state.highlight;
  span.subscript = state.subscript;
  span.superscript = state.superscript;
  state.displayText += displayText;
  if (visible) {
    state.visibleText += displayText;
    state.visibleOffset = span.visibleEnd;
  }
  state.displayOffset = span.displayEnd;
  state.spans.push_back(span);
}

void InlineProjection::appendBrLineBreak(BuildState& state, qsizetype sourceStart, qsizetype sourceEnd, const QString& tagText) {
  // Gray markup over the tag (a non-stopping marker), then a hard line break whose source
  // range is zero-width at the tag end. The break is visible (counts in plain/visible text
  // and produces a layout line) but not editable — it has no source span of its own to edit.
  appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::OpenMarker,
                 sourceStart, sourceEnd, tagText, false);
  appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::Text,
                 sourceEnd, sourceEnd, QStringLiteral("\n"), true, false);
}

void InlineProjection::appendSmartPunctTextSpans(BuildState& state, qsizetype sourceStart, qsizetype sourceEnd, const QString& decoded) {
  const QChar prev = state.displayOffset > 0 ? state.displayText.at(state.displayOffset - 1) : QChar();
  const SmartPunctResult result = applySmartPunctForRender(decoded, prev, state.smartPunct);
  const QString& text = result.text;
  const QVector<SmartPunctFold>& folds = result.folds;
  if (folds.isEmpty()) {
    appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, sourceStart, sourceEnd, text, true);
    return;
  }
  // Walk the folds, emitting a 1:1 span for the plain text between them and a folded span
  // (source N / display 1) for each token. Source positions accumulate from sourceStart; plain
  // segments consume equal source/display length, folds consume fold.sourceLength source chars.
  qsizetype displayPos = 0;
  qsizetype sourcePos = sourceStart;
  for (const SmartPunctFold& fold : folds) {
    if (fold.displayStart > displayPos) {
      const qsizetype len = fold.displayStart - displayPos;
      appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, sourcePos, sourcePos + len,
                     text.mid(displayPos, len), true);
      sourcePos += len;
      displayPos = fold.displayStart;
    }
    const qsizetype foldDisplayLen = fold.displayEnd - fold.displayStart;
    appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, sourcePos, sourcePos + fold.sourceLength,
                   text.mid(fold.displayStart, foldDisplayLen), true);
    state.spans.last().folded = true;
    sourcePos += fold.sourceLength;
    displayPos = fold.displayEnd;
  }
  if (displayPos < text.size()) {
    const qsizetype len = text.size() - displayPos;
    appendTextSpan(state, InlineType::Text, InlineSpanKind::Text, sourcePos, sourcePos + len,
                   text.mid(displayPos, len), true);
  }
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

      // Typora-style <br>: gray markup + a hard line break. The break only exists while the
      // tag is intact — cmark parses a corrupted "<br" as plain Text, so it never reaches
      // here and renders literally.
      if (isStandaloneBrTag(node.text())) {
        appendBrLineBreak(state, nodeStart, nodeEnd, node.text());
        searchFrom = nodeEnd;
        continue;
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

struct HtmlFormatEffect {
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool underline = false;
};

std::optional<HtmlFormatEffect> htmlFormatEffectForTag(QStringView tagName) {
  if (tagName == u"b" || tagName == u"bold" || tagName == u"strong")
    return HtmlFormatEffect{true, false, false, false};
  if (tagName == u"i" || tagName == u"italic" || tagName == u"em")
    return HtmlFormatEffect{false, true, false, false};
  if (tagName == u"s" || tagName == u"del")
    return HtmlFormatEffect{false, false, true, false};
  if (tagName == u"u" || tagName == u"underline" || tagName == u"ins")
    return HtmlFormatEffect{false, false, false, true};
  return std::nullopt;
}

void InlineProjection::appendHtmlInlineContent(BuildState& state, const QVector<InlineNode>& inlines,
                                               int startIndex, int endIndex, qsizetype openEnd, qsizetype closeNodeStart,
                                               QVector<HtmlInlineFormatData>& htmlFormatData) {
  qsizetype contentSourceStart = openEnd;
  for (int j = startIndex; j < endIndex; ++j) {
    const InlineNode& mid = inlines[j];
    const QString midMd = markdownForInline(mid);
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
}

int InlineProjection::tryAppendHtmlInlineGroup(BuildState& state, const QVector<InlineNode>& inlines, int index,
                                               qsizetype nodeStart, qsizetype sourceEnd, qsizetype& searchFrom,
                                               QVector<HtmlInlineFormatData>& htmlFormatData) {
  const InlineNode& openNode = inlines[index];
  const QString& openText = openNode.text();

  // <br> is handled in appendInlines (gray markup + line break) and never reaches
  // this grouping path, so only paired renderable tags are considered here.

  // Must be an opening tag with a renderable tag name
  const QStringView tagName = extractOpeningTagName(openText);
  if (tagName.isEmpty() || !html::InlineHtmlRenderer::isRenderableTag(tagName)) {
    return 0;
  }

  // Scan forward for matching closing tag
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

  // Determine if this is a simple formatting tag that can bypass InlineHtmlRenderer.
  // Simple tags (b, i, u, s, strong, em, del, ins, etc.) map to boolean state flags,
  // so Markdown formatting inside them (e.g. <u>**bold**</u>) is preserved.
  const auto formatEffect = htmlFormatEffectForTag(tagName);
  const bool isSimple = formatEffect.has_value();

  // Compute the visible text size for active-state determination.
  qsizetype visibleSize = 0;
  html::InlineHtmlFormatResult rendered;
  if (isSimple) {
    // Simple tags: visible text comes from intermediate nodes only (no HTML markers).
    for (int j = index + 1; j < closeIndex; ++j) {
      visibleSize += plainTextForInline(inlines[j]).size();
    }
  } else {
    // Complex tags: build HTML fragment and render via InlineHtmlRenderer.
    QString htmlFragment;
    for (int j = index; j <= closeIndex; ++j) {
      htmlFragment += inlines[j].text();
    }
    static const html::InlineHtmlRenderer renderer;
    rendered = renderer.render(htmlFragment, state.baseFontSize);
    visibleSize = rendered.text.size();
  }

  // Determine active state (cursor on the HTML group)
  const qsizetype visibleStart = state.visibleOffset;
  const qsizetype visibleEnd = visibleStart + visibleSize;
  const bool active = state.projectionState.shouldRevealSourceRange(openStart, closeEnd) ||
                      state.projectionState.shouldRevealVisibleRange(visibleStart, visibleEnd);

  if (active) {
    // When active, show raw HTML tags as visible text (like marker reveal for **bold**)
    appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::OpenMarker, openStart, openEnd,
                   state.sourceText->mid(openStart, openEnd - openStart), false);

    // Emit the content nodes between open and close tags
    appendHtmlInlineContent(state, inlines, index + 1, closeIndex, openEnd, closeNodeStart, htmlFormatData);

    appendTextSpan(state, InlineType::HtmlInline, InlineSpanKind::CloseMarker, closeNodeStart, closeEnd,
                   state.sourceText->mid(closeNodeStart, closeEnd - closeNodeStart), false);
  } else if (isSimple) {
    // Simple formatting tag inactive: process intermediate nodes through the Markdown
    // pipeline with the HTML format applied as a state flag overlay.
    const bool prevBold = state.bold;
    const bool prevItalic = state.italic;
    const bool prevStrike = state.strike;
    const bool prevUnderline = state.underline;

    if (formatEffect->bold)      state.bold      = true;
    if (formatEffect->italic)    state.italic    = true;
    if (formatEffect->strike)    state.strike    = true;
    if (formatEffect->underline) state.underline = true;

    const qsizetype groupDisplayStart = state.displayOffset;
    const qsizetype groupVisibleStart = state.visibleOffset;

    appendHtmlInlineContent(state, inlines, index + 1, closeIndex, openEnd, closeNodeStart, htmlFormatData);

    // Remap source range of contained spans to cover the full HTML group,
    // so clicking anywhere inside selects the entire construct.
    if (state.spans.size() > 0) {
      const qsizetype groupDisplayEnd = state.displayOffset;
      const qsizetype groupVisibleEnd = state.visibleOffset;
      for (InlineProjectionSpan& span : state.spans) {
        if (span.displayStart >= groupDisplayStart && span.displayEnd <= groupDisplayEnd &&
            span.visibleStart >= groupVisibleStart && span.visibleEnd <= groupVisibleEnd) {
          span.sourceStart = openStart;
          span.sourceEnd = closeEnd;
        }
      }
    }

    state.bold = prevBold;
    state.italic = prevItalic;
    state.strike = prevStrike;
    state.underline = prevUnderline;
  } else {
    // Complex tag inactive: render via InlineHtmlRenderer
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
        appendSmartPunctTextSpans(state, sourceStart, sourceEnd, decoded);
        break;
      }
      // decoded differs from source only via escapes and HTML entities, each of
      // which consumes more source chars than it shows. Split around them so every
      // plain segment keeps a 1:1 source/visible mapping (correct offset math).
      const QVector<DecodeSpan> segments = findDecodeSpans(source);
      if (segments.isEmpty()) {
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, decoded, true);
        break;
      }
      // Sanity-check that the segments actually reconstruct decoded; if something
      // else (a cmark decoding we don't model) is at play, fall back to a single
      // span rather than emit a misaligned projection.
      bool aligned = true;
      qsizetype sourcePos = 0;
      qsizetype decodedPos = 0;
      for (const DecodeSpan& segment : segments) {
        const QStringView plainSource = QStringView(source).mid(sourcePos, segment.sourceStart - sourcePos);
        if (!textMatchesAt(decoded, decodedPos, plainSource)) {
          aligned = false;
          break;
        }
        decodedPos += plainSource.size();
        if (!textMatchesAt(decoded, decodedPos, QStringView(segment.decodedText))) {
          aligned = false;
          break;
        }
        decodedPos += segment.decodedText.size();
        sourcePos = segment.sourceEnd;
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
      for (const DecodeSpan& segment : segments) {
        if (segment.sourceStart > sourcePos) {
          const qsizetype plainLen = segment.sourceStart - sourcePos;
          appendTextSpan(state, node.type(), InlineSpanKind::Text,
                          sourceStart + sourcePos, sourceStart + segment.sourceStart,
                          source.mid(sourcePos, plainLen), true);
          decodedPos += plainLen;
        }
        const qsizetype segmentSourceStart = sourceStart + segment.sourceStart;
        const qsizetype segmentSourceEnd = sourceStart + segment.sourceEnd;
        const qsizetype segmentVisibleStart = visibleStart + decodedPos;
        const qsizetype segmentVisibleEnd = segmentVisibleStart + segment.decodedText.size();
        const bool revealSyntax = state.projectionState.shouldRevealSourceRange(segmentSourceStart, segmentSourceEnd) ||
                                  state.projectionState.shouldRevealVisibleRange(segmentVisibleStart, segmentVisibleEnd);

        appendTextSpan(state, node.type(), InlineSpanKind::Text,
                        segmentSourceStart, segmentSourceEnd,
                        segment.decodedText, true);
        if (revealSyntax) {
          appendTextSpan(state, node.type(), InlineSpanKind::HiddenSyntax,
                          segmentSourceStart, segmentSourceEnd,
                          source.mid(segment.sourceStart, segment.sourceEnd - segment.sourceStart), false);
        }
        sourcePos = segment.sourceEnd;
        decodedPos += segment.decodedText.size();
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
    case InlineType::Strikethrough:
    case InlineType::Highlight:
    case InlineType::Subscript:
    case InlineType::Superscript: {
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
      const bool previousHighlight = state.highlight;
      const bool previousSubscript = state.subscript;
      const bool previousSuperscript = state.superscript;
      if (node.type() == InlineType::Strong) {
        state.bold = true;
      } else if (node.type() == InlineType::Emphasis) {
        state.italic = true;
      } else if (node.type() == InlineType::Strikethrough) {
        state.strike = true;
      } else if (node.type() == InlineType::Highlight) {
        state.highlight = true;
      } else if (node.type() == InlineType::Subscript) {
        state.subscript = true;
      } else if (node.type() == InlineType::Superscript) {
        state.superscript = true;
      }
      appendInlines(state, node.children(), contentStart, contentEnd, htmlFormatData);
      state.bold = previousBold;
      state.italic = previousItalic;
      state.strike = previousStrike;
      state.highlight = previousHighlight;
      state.subscript = previousSubscript;
      state.superscript = previousSuperscript;
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
      // <br> nested inside a paired inline HTML group (<b>..<br>..</b>) reaches this case
      // via appendHtmlInlineContent; emit it the same way as a top-level <br> so plain/
      // visible/display text stays consistent. Other inline HTML keeps its literal text.
      if (isStandaloneBrTag(node.text())) {
        appendBrLineBreak(state, sourceStart, sourceEnd, node.text());
      } else {
        appendTextSpan(state, node.type(), InlineSpanKind::Text, sourceStart, sourceEnd, node.text(), true);
      }
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
