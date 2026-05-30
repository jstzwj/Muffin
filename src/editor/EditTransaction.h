#pragma once

#include "editor/SourceSelection.h"
#include "model/MarkdownNode.h"

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace Muffin {

enum class EditTransactionKind {
    Unknown,
    RenderedEdit,
    ListIndent,
    ListOutdent
};

enum class EditIntent {
    Unknown,
    Typing,
    Delete,
    Paste,
    SplitBlock,
    MergeBlock,
    FormatToggle,
    HeadingChange,
    ListToggle,
    QuoteToggle,
    Indent,
    Outdent
};

struct NodeSnapshot {
    MarkdownNodeId nodeId = 0;
    MarkdownNodeType type = MarkdownNodeType::Unknown;
    SourceSpan source;
    SourceSpan content;
    MarkdownNodeId parentId = 0;
    MarkdownNodeId previousSiblingId = 0;
    MarkdownNodeId nextSiblingId = 0;
    QVector<MarkdownNodeId> ancestorIds;
    QVector<MarkdownNodeId> childIds;
    QString literal;
};

enum class EditOperationKind {
    Unknown,
    UpdateLiteral,
    MoveNode,
    InsertNode,
    RemoveNode,
    UpdateAttributes
};

struct EditOperation {
    EditOperationKind kind = EditOperationKind::Unknown;
    MarkdownNodeId nodeId = 0;
    NodeSnapshot beforeNode;
    NodeSnapshot afterNode;
};

struct EditTransaction {
    EditTransactionKind kind = EditTransactionKind::Unknown;
    EditIntent intent = EditIntent::Unknown;
    SourceSelection beforeSelection{-1, -1};
    std::optional<SourceSelection> afterSelection;
    QVector<MarkdownNodeId> affectedNodeIds;
    QVector<NodeSnapshot> beforeNodes;
    QVector<NodeSnapshot> afterNodes;
    QVector<EditOperation> operations;
    QString beforeMarkdown;
    QString afterMarkdown;
    QString label;
};

struct EditTransactionValidationResult {
    bool ok = false;
    QStringList errors;
};

} // namespace Muffin
