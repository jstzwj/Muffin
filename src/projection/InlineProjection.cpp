#include "projection/InlineProjection.h"

#include "houdini.h"

namespace muffin {
namespace {

bool isAutolinkInline(const InlineNode& node, const QString& label) {
  return node.type() == InlineType::Link && node.title().isEmpty() &&
         (label == node.href() || QStringLiteral("http://%1").arg(label) == node.href() ||
          QStringLiteral("mailto:%1").arg(label) == node.href());
}

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

InlineProjection::InlineProjection(const QVector<InlineNode>& inlines, QString sourceText, InlineProjectionState projectionState, qsizetype sourceBase)
    : sourceText_(std::move(sourceText)), visibleText_(plainTextForInlines(inlines)) {
  BuildState state;
  state.sourceText = &sourceText_;
  state.sourceBase = sourceBase;
  state.projectionState = projectionState;
  appendInlines(state, inlines, 0, sourceText_.size());
  displayText_ = state.displayText;
  visibleText_ = state.visibleText;
  spans_ = state.spans;
  linkRanges_ = state.linkRanges;
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
      if (isAutolinkInline(node, label)) {
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

void InlineProjection::appendInlines(BuildState& state, const QVector<InlineNode>& inlines, qsizetype sourceStart, qsizetype sourceEnd) {
  qsizetype searchFrom = sourceStart;
  for (const InlineNode& node : inlines) {
    const QString markdown = markdownForInline(node);
    qsizetype nodeStart = -1;
    qsizetype nodeEnd = -1;

    // Use parser-provided source positions when available. They are the only
    // unambiguous identity for repeated or non-canonically written inline nodes.
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
      // (e.g. different marker characters like __ vs **). Try locating the node's
      // plain text content within the remaining source range so it isn't silently
      // dropped from the projection.
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
    appendInline(state, node, nodeStart, nodeEnd);
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

void InlineProjection::appendInline(BuildState& state, const InlineNode& node, qsizetype sourceStart, qsizetype sourceEnd) {
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
      appendInlines(state, node.children(), contentStart, contentEnd);
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
      if (isAutolinkInline(node, label)) {
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
      appendInlines(state, node.children(), contentStart, contentEnd);
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
      appendInlines(state, node.children(), sourceStart, sourceEnd);
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
