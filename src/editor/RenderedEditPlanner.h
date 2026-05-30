#pragma once

#include "editor/RenderedEdit.h"
#include "model/MarkdownDocument.h"
#include "renderer/MarkdownBlock.h"
#include "renderer/RenderSourceMap.h"

#include <optional>

namespace Muffin {

struct RenderedEditPlan {
    enum class Kind {
        ReplaceNodeLiteral,
        ReplaceFormulaNode,
        ReplaceSourceSpan,
        SplitTextNodeIntoParagraphs,
        SplitParagraphAtChildBoundary,
        SplitInlineNodeIntoParagraphs,
        SplitFormattedTextIntoParagraphs,
        SplitInlineCodeIntoParagraphs,
        MergeParagraphs,
        SplitTextNodeIntoHeading,
        MergeTextBlocks,
        SplitTextNodeIntoListItems,
        MergeListItems,
        ExitEmptyListItem,
        DemoteListItem,
        PromoteListItem,
        NoOp
    };

    Kind kind = Kind::ReplaceNodeLiteral;
    MarkdownNodeId primaryNodeId = 0;
    MarkdownNodeId secondaryNodeId = 0;
    QString literal;
    int splitOffset = -1;
    int splitEndOffset = -1;
    int cursorSourceOffset = -1;
    QVector<MarkdownNodeId> nodeIds;
    SourceSpan sourceSpan;
};

class RenderedEditPlanner {
public:
    static std::optional<RenderedEditPlan> plan(const MarkdownDocument& document,
                                                const RenderSourceMap& sourceMap,
                                                const QVector<MarkdownBlock>& blocks,
                                                const RenderedEdit& edit);
};

} // namespace Muffin
