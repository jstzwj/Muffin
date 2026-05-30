#pragma once

#include "model/MarkdownDocument.h"

#include <QHash>
#include <optional>

namespace Muffin {

struct MarkdownSerializedNodeSpan {
    SourceSpan source;
    SourceSpan content;
};

struct MarkdownSerializationResult {
    QString markdown;
    QHash<MarkdownNodeId, MarkdownSerializedNodeSpan> nodeSpans;

    std::optional<int> sourceOffsetForNodeOffset(MarkdownNodeId nodeId, int offsetInNode) const;
};

class MarkdownSerializer {
public:
    QString serializeDocument(const MarkdownDocument& document) const;
    MarkdownSerializationResult serializeDocumentWithSourceMap(const MarkdownDocument& document) const;
    QString serializeNode(const MarkdownDocument& document, MarkdownNodeId nodeId) const;

private:
    QString serializeBlock(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeInline(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeInlineChildren(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeList(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeListItem(const MarkdownDocument& document, const MarkdownNode& node, int number) const;
    QString serializeBlockQuote(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeTable(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString serializeTableCell(const MarkdownDocument& document, const MarkdownNode& node) const;
    QString sourceSlice(const MarkdownDocument& document, const MarkdownNode& node) const;
};

} // namespace Muffin
