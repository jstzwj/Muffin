#include "MarkdownEditEngine.h"

namespace Muffin {

PatchResult MarkdownEditEngine::applyRenderedEdit(const QString& markdown,
                                                  const RenderSourceMap& sourceMap,
                                                  const RenderedEdit& edit)
{
    return MarkdownPatch::applyRenderedEdit(markdown, sourceMap, edit);
}

PatchResult MarkdownEditEngine::applyRenderedEdit(const QString& markdown,
                                                  const RenderSourceMap& sourceMap,
                                                  const QVector<MarkdownBlock>& blocks,
                                                  const RenderedEdit& edit)
{
    return MarkdownPatch::applyRenderedEdit(markdown, sourceMap, blocks, edit);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommand(const QString& markdown,
                                                             SourceSelection selection,
                                                             InlineCommand command)
{
    return command ? command(markdown, selection) : MarkdownCommandResult{false, markdown, selection, QStringLiteral("Missing markdown command.")};
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                             const RenderedCommandTarget& target,
                                                                             InlineCommand command)
{
    return applyInlineCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, true), command);
}

MarkdownCommandResult MarkdownEditEngine::applyInlineCommandToRenderedTarget(const QString& markdown,
                                                                             const QVector<MarkdownBlock>& blocks,
                                                                             const RenderedCommandTarget& target,
                                                                             InlineCommand command)
{
    return applyInlineCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, true), command);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommand(const QString& markdown,
                                                           SourceSelection selection,
                                                           MarkdownCommand::ListType type)
{
    return MarkdownCommand::applyList(markdown, selection, type);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommandToRenderedTarget(const QString& markdown,
                                                                           const RenderedCommandTarget& target,
                                                                           MarkdownCommand::ListType type)
{
    return applyListCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, false), type);
}

MarkdownCommandResult MarkdownEditEngine::applyListCommandToRenderedTarget(const QString& markdown,
                                                                           const QVector<MarkdownBlock>& blocks,
                                                                           const RenderedCommandTarget& target,
                                                                           MarkdownCommand::ListType type)
{
    return applyListCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, false), type);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommand(const QString& markdown,
                                                              SourceSelection selection,
                                                              int level)
{
    return MarkdownCommand::applyHeading(markdown, selection, level);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                              const RenderedCommandTarget& target,
                                                                              int level)
{
    return applyHeadingCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, target, false), level);
}

MarkdownCommandResult MarkdownEditEngine::applyHeadingCommandToRenderedTarget(const QString& markdown,
                                                                              const QVector<MarkdownBlock>& blocks,
                                                                              const RenderedCommandTarget& target,
                                                                              int level)
{
    return applyHeadingCommand(markdown, EditorSelectionMapper::sourceSelectionForRenderedTarget(markdown, blocks, target, false), level);
}

} // namespace Muffin
