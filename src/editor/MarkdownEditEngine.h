#pragma once

#include "editor/EditorSelectionMapper.h"
#include "editor/MarkdownCommand.h"
#include "editor/MarkdownPatch.h"

namespace Muffin {

class MarkdownEditEngine {
public:
    using InlineCommand = MarkdownCommandResult (*)(const QString&, SourceSelection);

    static PatchResult applyRenderedEdit(const QString& markdown,
                                         const RenderSourceMap& sourceMap,
                                         const RenderedEdit& edit);
    static PatchResult applyRenderedEdit(const QString& markdown,
                                         const RenderSourceMap& sourceMap,
                                         const QVector<MarkdownBlock>& blocks,
                                         const RenderedEdit& edit);

    static MarkdownCommandResult applyInlineCommand(const QString& markdown,
                                                    SourceSelection selection,
                                                    InlineCommand command);
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
    static MarkdownCommandResult applyListCommandToRenderedTarget(const QString& markdown,
                                                                  const RenderedCommandTarget& target,
                                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyListCommandToRenderedTarget(const QString& markdown,
                                                                  const QVector<MarkdownBlock>& blocks,
                                                                  const RenderedCommandTarget& target,
                                                                  MarkdownCommand::ListType type);
    static MarkdownCommandResult applyHeadingCommand(const QString& markdown,
                                                     SourceSelection selection,
                                                     int level);
    static MarkdownCommandResult applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                     const RenderedCommandTarget& target,
                                                                     int level);
    static MarkdownCommandResult applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                     const QVector<MarkdownBlock>& blocks,
                                                                     const RenderedCommandTarget& target,
                                                                     int level);
};

} // namespace Muffin
