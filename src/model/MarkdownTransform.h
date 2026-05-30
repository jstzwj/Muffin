#pragma once

#include "model/MarkdownDocument.h"

namespace Muffin {

struct DocumentTransformResult {
    MarkdownDocument document;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    QVector<MarkdownNodeId> affectedNodeIds;
};

class MarkdownTransform {
public:
    static MarkdownDocument replaceNodeLiteral(const MarkdownDocument& document, MarkdownNodeId id, const QString& literal);
    static DocumentTransformResult replaceNodeLiteralWithResult(const MarkdownDocument& document,
                                                                MarkdownNodeId id,
                                                                const QString& literal,
                                                                int cursorSourceOffset = -1);
    static MarkdownDocument replaceFormulaNode(const MarkdownDocument& document, MarkdownNodeId id, const QString& replacement);
    static DocumentTransformResult replaceFormulaNodeWithResult(const MarkdownDocument& document,
                                                                MarkdownNodeId id,
                                                                const QString& replacement,
                                                                int cursorSourceOffset = -1);
    static MarkdownDocument splitTextNodeIntoParagraphs(const MarkdownDocument& document,
                                                        MarkdownNodeId id,
                                                        int splitOffset,
                                                        int splitEndOffset = -1);
    static DocumentTransformResult splitTextNodeIntoParagraphsWithResult(const MarkdownDocument& document,
                                                                         MarkdownNodeId id,
                                                                         int splitOffset,
                                                                         int splitEndOffset = -1);
    static MarkdownDocument splitParagraphAtChildBoundary(const MarkdownDocument& document,
                                                          MarkdownNodeId paragraphId,
                                                          int childIndex);
    static DocumentTransformResult splitParagraphAtChildBoundaryWithResult(const MarkdownDocument& document,
                                                                           MarkdownNodeId paragraphId,
                                                                           int childIndex);
    static MarkdownDocument splitInlineNodeIntoParagraphs(const MarkdownDocument& document,
                                                          MarkdownNodeId nodeId,
                                                          int splitOffset,
                                                          int splitEndOffset = -1);
    static DocumentTransformResult splitInlineNodeIntoParagraphsWithResult(const MarkdownDocument& document,
                                                                           MarkdownNodeId nodeId,
                                                                           int splitOffset,
                                                                           int splitEndOffset = -1);
    static MarkdownDocument splitFormattedTextIntoParagraphs(const MarkdownDocument& document,
                                                             MarkdownNodeId textId,
                                                             int splitOffset,
                                                             int splitEndOffset = -1);
    static DocumentTransformResult splitFormattedTextIntoParagraphsWithResult(const MarkdownDocument& document,
                                                                              MarkdownNodeId textId,
                                                                              int splitOffset,
                                                                              int splitEndOffset = -1);
    static MarkdownDocument splitInlineCodeIntoParagraphs(const MarkdownDocument& document,
                                                          MarkdownNodeId codeId,
                                                          int splitOffset,
                                                          int splitEndOffset = -1);
    static DocumentTransformResult splitInlineCodeIntoParagraphsWithResult(const MarkdownDocument& document,
                                                                           MarkdownNodeId codeId,
                                                                           int splitOffset,
                                                                           int splitEndOffset = -1);
    static MarkdownDocument splitTextNodeIntoHeading(const MarkdownDocument& document,
                                                     MarkdownNodeId id,
                                                     int splitOffset,
                                                     int splitEndOffset = -1);
    static DocumentTransformResult splitTextNodeIntoHeadingWithResult(const MarkdownDocument& document,
                                                                      MarkdownNodeId id,
                                                                      int splitOffset,
                                                                      int splitEndOffset = -1);
    static MarkdownDocument mergeParagraphs(const MarkdownDocument& document,
                                            MarkdownNodeId previousParagraphId,
                                            MarkdownNodeId nextParagraphId);
    static DocumentTransformResult mergeParagraphsWithResult(const MarkdownDocument& document,
                                                             MarkdownNodeId previousParagraphId,
                                                             MarkdownNodeId nextParagraphId);
    static MarkdownDocument mergeTextBlocks(const MarkdownDocument& document,
                                            MarkdownNodeId previousBlockId,
                                            MarkdownNodeId nextBlockId);
    static DocumentTransformResult mergeTextBlocksWithResult(const MarkdownDocument& document,
                                                             MarkdownNodeId previousBlockId,
                                                             MarkdownNodeId nextBlockId);
    static MarkdownDocument splitTextNodeIntoListItems(const MarkdownDocument& document,
                                                       MarkdownNodeId id,
                                                       int splitOffset,
                                                       int splitEndOffset = -1);
    static DocumentTransformResult splitTextNodeIntoListItemsWithResult(const MarkdownDocument& document,
                                                                        MarkdownNodeId id,
                                                                        int splitOffset,
                                                                        int splitEndOffset = -1);
    static MarkdownDocument mergeListItems(const MarkdownDocument& document,
                                           MarkdownNodeId previousItemId,
                                           MarkdownNodeId nextItemId);
    static DocumentTransformResult mergeListItemsWithResult(const MarkdownDocument& document,
                                                            MarkdownNodeId previousItemId,
                                                            MarkdownNodeId nextItemId);
    static MarkdownDocument removeEmptyListItem(const MarkdownDocument& document, MarkdownNodeId itemId);
    static DocumentTransformResult removeEmptyListItemWithResult(const MarkdownDocument& document,
                                                                 MarkdownNodeId itemId);
    static MarkdownDocument demoteListItem(const MarkdownDocument& document, MarkdownNodeId itemId);
    static DocumentTransformResult demoteListItemWithResult(const MarkdownDocument& document,
                                                            MarkdownNodeId itemId);
    static MarkdownDocument demoteListItems(const MarkdownDocument& document, const QVector<MarkdownNodeId>& itemIds);
    static DocumentTransformResult demoteListItemsWithResult(const MarkdownDocument& document,
                                                             const QVector<MarkdownNodeId>& itemIds);
    static MarkdownDocument promoteListItem(const MarkdownDocument& document, MarkdownNodeId itemId);
    static DocumentTransformResult promoteListItemWithResult(const MarkdownDocument& document,
                                                             MarkdownNodeId itemId);
    static MarkdownDocument promoteListItems(const MarkdownDocument& document, const QVector<MarkdownNodeId>& itemIds);
    static DocumentTransformResult promoteListItemsWithResult(const MarkdownDocument& document,
                                                              const QVector<MarkdownNodeId>& itemIds);

    static DocumentTransformResult insertNode(MarkdownDocument source,
                                              MarkdownNodeId parentId,
                                              int childIndex,
                                              MarkdownNode node);
    static DocumentTransformResult removeNode(MarkdownDocument source,
                                              MarkdownNodeId nodeId);
    static DocumentTransformResult reparentNode(MarkdownDocument source,
                                                MarkdownNodeId nodeId,
                                                MarkdownNodeId newParentId,
                                                int childIndex);

private:
    static MarkdownNode* mutableNodeById(MarkdownDocument& document, MarkdownNodeId nodeId);
    static void moveTrailingChildren(MarkdownDocument& document,
                                     MarkdownNode& leftParent,
                                     int retainedLastIndex,
                                     MarkdownNode& rightParent);
    static bool mergeAdjacentInlineNodes(MarkdownDocument& document,
                                         MarkdownNode& previousInline,
                                         const MarkdownNode& nextInline,
                                         const QString& boundaryWhitespace);
    static bool mergeFormattedInlineNodes(MarkdownDocument& document,
                                          MarkdownNode& previousInline,
                                          const MarkdownNode& nextInline,
                                          const QString& boundaryWhitespace);
    static bool mergeInlineCodeNodes(MarkdownNode& previousInline,
                                     const MarkdownNode& nextInline,
                                     const QString& boundaryWhitespace);
    static DocumentTransformResult resultWithAnchor(MarkdownDocument document,
                                                    const MarkdownDocument& source,
                                                    MarkdownNodeId originalNodeId,
                                                    MarkdownNodeType anchorType);
    static DocumentTransformResult resultWithFirstMovedChildAnchor(MarkdownDocument document,
                                                                   const MarkdownDocument& source,
                                                                   MarkdownNodeId paragraphId,
                                                                   int childIndex);
    static DocumentTransformResult resultWithParagraphEndAnchor(MarkdownDocument document,
                                                                const MarkdownDocument& source,
                                                                MarkdownNodeId paragraphId);
    static DocumentTransformResult resultWithListItemEndAnchor(MarkdownDocument document,
                                                               const MarkdownDocument& source,
                                                               MarkdownNodeId itemId);
    static DocumentTransformResult resultWithTextBlockEndAnchor(MarkdownDocument document,
                                                                const MarkdownDocument& source,
                                                                MarkdownNodeId blockId);
};

} // namespace Muffin
