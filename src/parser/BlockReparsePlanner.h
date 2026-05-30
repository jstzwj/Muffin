#pragma once

#include "model/MarkdownDocument.h"
#include "parser/SourceSpan.h"

#include <QString>
#include <QVector>

namespace Muffin {

struct BlockParseRange {
    SourceSpan editedSource;
    SourceSpan expandedSource;
    QVector<MarkdownNodeId> affectedBlockIds;
    MarkdownNodeType targetBlockType = MarkdownNodeType::Document;
    bool requiresFullReparse = true;
    QString reason;

    bool canReparseLocally() const
    {
        return !requiresFullReparse && expandedSource.isValid() && !affectedBlockIds.isEmpty();
    }
};

class BlockReparsePlanner {
public:
    static BlockParseRange planForEdit(const MarkdownDocument& document,
                                       SourceSpan editedSource,
                                       const QString& replacement);

private:
    static const MarkdownNode* localReparseCandidateForEdit(const MarkdownDocument& document, SourceSpan editedSource);
    static const MarkdownNode* expandToSimpleWrapper(const MarkdownDocument& document, const MarkdownNode& block);
    static bool hasFullReparseAncestor(const MarkdownDocument& document, const MarkdownNode& block);
    static bool editInsideCodeBlockContent(const MarkdownNode& block, SourceSpan editedSource);
    static bool replacementChangesBlockShape(const QString& replacement);
    static bool editTouchesBlockBoundary(const MarkdownNode& block, SourceSpan editedSource);
    static bool isLocallyReparsableBlock(MarkdownNodeType type);
    static bool isKnownFullReparseBlock(MarkdownNodeType type);
};

} // namespace Muffin
