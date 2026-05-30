#pragma once

#include "editor/MarkdownCommand.h"
#include "model/MarkdownDocument.h"
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

struct SelectionBookmark {
    enum class Affinity {
        None,
        Backward,
        Forward
    };

    MarkdownNodeId nodeId = 0;
    int sourceOffset = -1;
    int offsetInNode = -1;
    Affinity affinity = Affinity::None;

    bool isValid() const { return nodeId != 0 && sourceOffset >= 0; }
};

struct SelectionRangeBookmark {
    SelectionBookmark anchor;
    SelectionBookmark focus;

    bool isValid() const { return anchor.isValid() && focus.isValid(); }
    bool isCollapsed() const { return isValid() && anchor.sourceOffset == focus.sourceOffset; }
};

struct RenderedSelectionRestoreRequest {
    std::optional<SelectionRangeBookmark> rangeBookmark;
    std::optional<SelectionBookmark> caretBookmark;
    std::optional<int> fallbackSourceOffset;
};

enum class RenderedSelectionRestoreMethod {
    None,
    RangeBookmark,
    CaretBookmark,
    SourceOffset
};

struct RenderedSelectionRestoreResult {
    std::optional<SourceSelection> selection;
    std::optional<int> cursorPosition;
    RenderedSelectionRestoreMethod method = RenderedSelectionRestoreMethod::None;

    bool hasValue() const { return selection.has_value() || cursorPosition.has_value(); }
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
    static SelectionBookmark bookmarkForSourceOffset(const MarkdownDocument& document,
                                                     int sourceOffset,
                                                     SelectionBookmark::Affinity affinity = SelectionBookmark::Affinity::None);
    static SelectionRangeBookmark bookmarkForSourceSelection(const MarkdownDocument& document, SourceSelection selection);
    static std::optional<SourceSelection> sourceSelectionForBookmark(const SelectionRangeBookmark& bookmark);
    static std::optional<SourceSelection> orderedSourceSelectionForBookmark(const SelectionRangeBookmark& bookmark);
    static std::optional<SourceSelection> renderedSelectionForBookmark(const RenderSourceMap& sourceMap,
                                                                       const SelectionRangeBookmark& bookmark);
    static std::optional<SourceSelection> orderedRenderedSelectionForBookmark(const RenderSourceMap& sourceMap,
                                                                              const SelectionRangeBookmark& bookmark);
    static std::optional<int> renderedPositionForBookmark(const RenderSourceMap& sourceMap,
                                                          const SelectionBookmark& bookmark,
                                                          RenderSourceMap::Bias bias = RenderSourceMap::Bias::Backward);
    static RenderedSelectionRestoreResult restoreRenderedSelection(const RenderSourceMap& sourceMap,
                                                                   const RenderedSelectionRestoreRequest& request);
    static bool moveSourceCursorToRange(QPlainTextEdit* editor, SourceRange range, bool selectRange);
    static bool moveSourceCursorToInlineText(QPlainTextEdit* editor, SourceRange range, const QString& text);
};

} // namespace Muffin
