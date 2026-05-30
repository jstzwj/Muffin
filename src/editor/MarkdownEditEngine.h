#pragma once

#include "editor/EditorSelectionMapper.h"
#include "editor/MarkdownCommand.h"
#include "editor/RenderedEdit.h"
#include "editor/StyleCommandHandler.h"
#include "model/MarkdownDocument.h"

namespace Muffin {

class MarkdownEditEngine {
public:
    using InlineCommand = MarkdownCommandResult (*)(const QString&, SourceSelection);

    static PatchResult applyRenderedEdit(const MarkdownDocument& document,
                                         const RenderSourceMap& sourceMap,
                                         const QVector<MarkdownBlock>& blocks,
                                         const RenderedEdit& edit);

    static MarkdownCommandResult applyInlineCommand(const QString& markdown,
                                                    SourceSelection selection,
                                                    InlineCommand command);
    static MarkdownCommandResult applyInlineStyleCommand(const MarkdownDocument& document,
                                                         SourceSelection selection,
                                                         StyleCommandHandler::InlineStyle style,
                                                         InlineCommand fallback);
    static MarkdownCommandResult applyInlineStyleCommandToRenderedTarget(const MarkdownDocument& document,
                                                                         const RenderedCommandTarget& target,
                                                                         StyleCommandHandler::InlineStyle style,
                                                                         InlineCommand fallback);
    static MarkdownCommandResult applyInlineStyleCommandToRenderedTarget(const MarkdownDocument& document,
                                                                         const QVector<MarkdownBlock>& blocks,
                                                                         const RenderedCommandTarget& target,
                                                                         StyleCommandHandler::InlineStyle style,
                                                                         InlineCommand fallback);
    static MarkdownCommandResult applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                    const RenderedCommandTarget& target,
                                                                    InlineCommand command);
    static MarkdownCommandResult applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                    const QVector<MarkdownBlock>& blocks,
                                                                    const RenderedCommandTarget& target,
                                                                    InlineCommand command);
    static MarkdownCommandResult applyListCommand(const QString& markdown,
                                                  SourceSelection selection,
                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyListCommand(const MarkdownDocument& document,
                                                  SourceSelection selection,
                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyListCommandToRenderedTarget(const QString& markdown,
                                                                  const RenderedCommandTarget& target,
                                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyListCommandToRenderedTarget(const QString& markdown,
                                                                  const QVector<MarkdownBlock>& blocks,
                                                                  const RenderedCommandTarget& target,
                                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyListCommandToRenderedTarget(const MarkdownDocument& document,
                                                                  const QVector<MarkdownBlock>& blocks,
                                                                  const RenderedCommandTarget& target,
                                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyHeadingCommand(const QString& markdown,
                                                     SourceSelection selection,
                                                     int level);
    static MarkdownCommandResult applyHeadingCommand(const MarkdownDocument& document,
                                                     SourceSelection selection,
                                                     int level);
    static MarkdownCommandResult applyParagraphCommand(const MarkdownDocument& document,
                                                       SourceSelection selection);
    static MarkdownCommandResult applyParagraphCommandToRenderedTarget(const MarkdownDocument& document,
                                                                       const QVector<MarkdownBlock>& blocks,
                                                                       const RenderedCommandTarget& target);
    static MarkdownCommandResult applyQuoteCommand(const MarkdownDocument& document,
                                                   SourceSelection selection);
    static MarkdownCommandResult applyQuoteCommandToRenderedTarget(const MarkdownDocument& document,
                                                                   const QVector<MarkdownBlock>& blocks,
                                                                   const RenderedCommandTarget& target);
    static MarkdownCommandResult applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                     const RenderedCommandTarget& target,
                                                                     int level);
    static MarkdownCommandResult applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                     const QVector<MarkdownBlock>& blocks,
                                                                     const RenderedCommandTarget& target,
                                                                     int level);
    static MarkdownCommandResult applyHeadingCommandToRenderedTarget(const MarkdownDocument& document,
                                                                     const QVector<MarkdownBlock>& blocks,
                                                                     const RenderedCommandTarget& target,
                                                                     int level);
};

} // namespace Muffin
