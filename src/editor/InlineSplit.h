#pragma once

#include <QString>
#include <QStringView>

namespace muffin {

class MarkdownNode;

struct InlineSplitPlan {
  QString closeBeforeBreak;
  QString reopenAfterBreak;

  bool isEmpty() const;
  QString wrap(QString blockBreak) const;
};

// Builds a split plan from the parser's exact inline ranges. Only wrappers whose
// content strictly contains cursorSourceOffset are continued across the break.
InlineSplitPlan inlineSplitPlanAt(
    const MarkdownNode& editableNode,
    QStringView contentSource,
    qsizetype contentSourceStart,
    qsizetype cursorSourceOffset);

// Selection-aware variant: closes wrappers active at selectionSourceStart and
// reopens wrappers active at selectionSourceEnd. The collapsed case is
// equivalent to inlineSplitPlanAt().
InlineSplitPlan inlineSplitPlanForRange(
    const MarkdownNode& editableNode,
    QStringView contentSource,
    qsizetype contentSourceStart,
    qsizetype selectionSourceStart,
    qsizetype selectionSourceEnd);

// Trims one whitespace character adjacent to the split and keeps the split out
// of the middle of a repeated delimiter token. May modify source in place.
qsizetype normalizeSplitOffset(QString& source, qsizetype offset);

}  // namespace muffin
