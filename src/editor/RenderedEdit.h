#pragma once

#include "editor/EditTransaction.h"
#include "editor/EditTransactionValidator.h"
#include "editor/MarkdownCommand.h"
#include "model/MarkdownNode.h"
#include "parser/SourceSpan.h"
#include "renderer/SyntaxTokenSpan.h"

#include <QString>
#include <optional>

namespace Muffin {

enum class RenderedEditOperation {
    InsertText,
    ReplaceSelection,
    Backspace,
    Delete,
    Enter,
    Paste,
    Indent,
    Outdent
};

struct RenderedEdit {
    enum class TargetKind {
        RenderedContent,
        SourceSpan
    };

    RenderedEditOperation operation = RenderedEditOperation::InsertText;
    int renderedStart = -1;
    int renderedEnd = -1;
    QString replacement;
    TargetKind targetKind = TargetKind::RenderedContent;
    SourceSpan sourceSpan;
};

struct RenderedMarkerHit {
    SourceSpan source;
    MarkdownNodeId nodeId = 0;
    SyntaxTokenSpan::Kind kind = SyntaxTokenSpan::Kind::EmphasisMarker;
    QString text;
    bool leadingMarker = true;
    int projectedStart = -1;
    int projectedEnd = -1;
};

struct PatchResult {
    bool ok = false;
    bool changed = false;
    QString text;
    int cursorSourceOffset = -1;
    QString error;
    std::optional<SourceSelection> sourceSelection;
    std::optional<EditTransaction> transaction;
    std::optional<EditTransactionValidationResult> transactionValidation;
};

} // namespace Muffin
