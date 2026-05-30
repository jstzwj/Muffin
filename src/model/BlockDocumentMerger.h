#pragma once

#include "model/MarkdownDocument.h"
#include "parser/BlockReparser.h"

#include <QStringList>

namespace Muffin {

struct BlockMergeResult {
    bool ok = false;
    MarkdownDocument document;
    MarkdownNodeId replacedNodeId = 0;
    QStringList errors;
};

struct BlockMergeConsistencyResult {
    bool ok = false;
    QStringList errors;
};

class BlockDocumentMerger {
public:
    static BlockMergeResult mergeReparsedBlock(const MarkdownDocument& previousDocument,
                                               const BlockReparseResult& reparseResult);
    static BlockMergeConsistencyResult compareMergedBlockToFullParse(const MarkdownDocument& mergedDocument,
                                                                     const MarkdownDocument& fullDocument,
                                                                     MarkdownNodeId mergedBlockId,
                                                                     SourceSpan blockSource);

private:
    static const MarkdownNode* localTopLevelBlock(const MarkdownDocument& document);
    static const MarkdownNode* blockBySource(const MarkdownDocument& document, SourceSpan source);
    static QString textLiteralForBlock(const MarkdownDocument& document, const MarkdownNode& block);
    static void collectSubtreeIds(const MarkdownDocument& document, MarkdownNodeId nodeId, QVector<MarkdownNodeId>& ids);
    static MarkdownNode rebaseNode(MarkdownNode node, int sourceOffset);
    static SourceSpan rebaseSpan(SourceSpan span, int sourceOffset);
};

} // namespace Muffin
