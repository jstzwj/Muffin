#pragma once

#include "editor/MarkdownCommand.h"
#include "model/MarkdownDocument.h"

#include <optional>

namespace Muffin {

class StyleCommandHandler {
public:
    enum class InlineStyle {
        Strong,
        Emphasis,
        InlineCode,
        Strikethrough
    };

    struct StyleCommandResult {
        MarkdownDocument document;
        MarkdownNodeId anchorNodeId = 0;
        int anchorOffsetInNode = -1;
        QVector<MarkdownNodeId> affectedNodeIds;
    };

    static std::optional<MarkdownDocument> toggleInlineStyle(const MarkdownDocument& document,
                                                             SourceSelection selection,
                                                             InlineStyle style);
    static std::optional<StyleCommandResult> toggleInlineStyleWithSelection(const MarkdownDocument& document,
                                                                            SourceSelection selection,
                                                                            InlineStyle style);

private:
    struct InlineTransform;

    static std::optional<StyleCommandResult> unwrapStyledSelection(const MarkdownDocument& document,
                                                                   int selectionStart,
                                                                   int selectionEnd,
                                                                   MarkdownNodeType styleType);
    static std::optional<StyleCommandResult> unwrapPartialStyledSelection(const MarkdownDocument& document,
                                                                          int selectionStart,
                                                                          int selectionEnd,
                                                                          MarkdownNodeType styleType);
    static std::optional<StyleCommandResult> ensureOverlappingSelectionStyled(const MarkdownDocument& document,
                                                                              int selectionStart,
                                                                              int selectionEnd,
                                                                              MarkdownNodeType styleType);
    static std::optional<StyleCommandResult> wrapSelectionAcrossInlineSiblings(const MarkdownDocument& document,
                                                                               int selectionStart,
                                                                               int selectionEnd,
                                                                               MarkdownNodeType styleType);
};

} // namespace Muffin
