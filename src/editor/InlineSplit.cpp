#include "editor/InlineSplit.h"

#include "document/InlineNode.h"
#include "document/MarkdownNode.h"

#include <QVector>

namespace muffin {
namespace {

struct WrapperSyntax {
  QString open;
  QString close;
};

bool canContinueAcrossBlockBreak(const InlineNode& node) {
  switch (node.type()) {
    case InlineType::Emphasis:
    case InlineType::Strong:
    case InlineType::Code:
    case InlineType::Strikethrough:
    case InlineType::InlineMath:
    case InlineType::Highlight:
    case InlineType::Subscript:
    case InlineType::Superscript:
      return true;
    case InlineType::Link:
      return !node.isAutolink();
    default:
      return false;
  }
}

QString sourceSlice(QStringView source, InlineRange range, qsizetype sourceBase) {
  if (!range.isValid()) {
    return {};
  }
  const qsizetype start = range.start - sourceBase;
  const qsizetype end = range.end - sourceBase;
  if (start < 0 || end < start || end > source.size()) {
    return {};
  }
  return source.mid(start, end - start).toString();
}

void collectActiveWrappers(
    const QVector<InlineNode>& inlines,
    QStringView source,
    qsizetype sourceBase,
    qsizetype cursor,
    QVector<WrapperSyntax>& wrappers) {
  for (const InlineNode& node : inlines) {
    const InlineRange sourceRange = node.sourceRange();
    const InlineRange contentRange = node.contentRange();
    const bool containsCursor =
        sourceRange.isValid() && contentRange.isValid() &&
        sourceRange.start <= contentRange.start &&
        contentRange.end <= sourceRange.end &&
        cursor > contentRange.start && cursor < contentRange.end;

    if (containsCursor && canContinueAcrossBlockBreak(node)) {
      const QString open = sourceSlice(
          source, InlineRange{sourceRange.start, contentRange.start}, sourceBase);
      const QString close = sourceSlice(
          source, InlineRange{contentRange.end, sourceRange.end}, sourceBase);
      if (!open.isEmpty() && !close.isEmpty()) {
        wrappers.push_back(WrapperSyntax{open, close});
      }
    }

    if (!node.children().isEmpty()) {
      collectActiveWrappers(node.children(), source, sourceBase, cursor, wrappers);
    }
  }
}

}  // namespace

bool InlineSplitPlan::isEmpty() const {
  return closeBeforeBreak.isEmpty() && reopenAfterBreak.isEmpty();
}

QString InlineSplitPlan::wrap(QString blockBreak) const {
  if (isEmpty()) {
    return blockBreak;
  }
  return closeBeforeBreak + blockBreak + reopenAfterBreak;
}

InlineSplitPlan inlineSplitPlanAt(
    const MarkdownNode& editableNode,
    QStringView contentSource,
    qsizetype contentSourceStart,
    qsizetype cursorSourceOffset) {
  return inlineSplitPlanForRange(
      editableNode,
      contentSource,
      contentSourceStart,
      cursorSourceOffset,
      cursorSourceOffset);
}

InlineSplitPlan inlineSplitPlanForRange(
    const MarkdownNode& editableNode,
    QStringView contentSource,
    qsizetype contentSourceStart,
    qsizetype selectionSourceStart,
    qsizetype selectionSourceEnd) {
  InlineSplitPlan plan;
  const MarkdownNode* topLevel = editableNode.topLevelBlock();
  const qsizetype contentSourceEnd = contentSourceStart + contentSource.size();
  if (!topLevel || contentSourceStart < 0 ||
      selectionSourceStart < contentSourceStart ||
      selectionSourceEnd < selectionSourceStart ||
      selectionSourceEnd > contentSourceEnd) {
    return plan;
  }

  // Inline ranges are block-relative; contentSource and the selection are absolute.
  const qsizetype topLevelStart = topLevel->sourceRange().byteStart;
  const qsizetype sourceBase = contentSourceStart - topLevelStart;
  const qsizetype selectionStart = selectionSourceStart - topLevelStart;
  const qsizetype selectionEnd = selectionSourceEnd - topLevelStart;

  QVector<WrapperSyntax> startWrappers;
  collectActiveWrappers(
      editableNode.inlines(), contentSource, sourceBase, selectionStart, startWrappers);
  QVector<WrapperSyntax> endWrappers;
  collectActiveWrappers(
      editableNode.inlines(), contentSource, sourceBase, selectionEnd, endWrappers);

  for (const WrapperSyntax& wrapper : endWrappers) {
    plan.reopenAfterBreak += wrapper.open;
  }
  for (auto it = startWrappers.crbegin(); it != startWrappers.crend(); ++it) {
    plan.closeBeforeBreak += it->close;
  }
  return plan;
}

qsizetype normalizeSplitOffset(QString& source, qsizetype offset) {
  offset = qBound<qsizetype>(0, offset, source.size());
  if (offset > 0 && offset < source.size()) {
    const QChar previous = source.at(offset - 1);
    const QChar next = source.at(offset);
    if ((previous == QLatin1Char('*') && next == QLatin1Char('*')) ||
        (previous == QLatin1Char('`') && next == QLatin1Char('`')) ||
        (previous == QLatin1Char('$') && next == QLatin1Char('$'))) {
      --offset;
    }
  }
  if (offset < source.size() && source.at(offset).isSpace()) {
    source.remove(offset, 1);
  } else if (offset > 0 && source.at(offset - 1).isSpace()) {
    source.remove(offset - 1, 1);
    --offset;
  }
  return offset;
}

}  // namespace muffin
