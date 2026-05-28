#pragma once

#include "editor/MarkdownCommand.h"
#include "parser/AstNode.h"
#include "renderer/MarkdownBlock.h"
#include <QString>
#include <optional>

class QPlainTextEdit;

namespace Muffin {

struct RenderedCommandTarget {
    SourceRange range;
    QString selectedText;
};

class EditorSelectionMapper {
public:
    static SourceSelection sourceSelectionForRenderedTarget(const QString& markdown,
                                                           const RenderedCommandTarget& target,
                                                           bool preferInlineText);
    static SourceSelection sourceSelectionForRenderedTarget(const QString& markdown,
                                                           const QVector<MarkdownBlock>& blocks,
                                                           const RenderedCommandTarget& target,
                                                           bool preferInlineText);
    static SourceSelection sourceSelectionForEditor(const QPlainTextEdit* editor);
    static bool moveSourceCursorToRange(QPlainTextEdit* editor, SourceRange range, bool selectRange);
    static bool moveSourceCursorToInlineText(QPlainTextEdit* editor, SourceRange range, const QString& text);
};

} // namespace Muffin
