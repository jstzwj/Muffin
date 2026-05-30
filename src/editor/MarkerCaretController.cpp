#include "MarkerCaretController.h"

#include <QFontMetrics>
#include <QtGlobal>

namespace Muffin {

MarkerEditCaret MarkerCaretController::caretForClick(const MarkerProjectionSpan& marker,
                                                     QRect markerRect,
                                                     QPoint clickPoint,
                                                     const QFontMetrics& metrics)
{
    const int offset = offsetForX(marker.text, clickPoint.x() - markerRect.left(), metrics);
    return {marker.fragment.source,
            qBound(marker.fragment.source.start, marker.fragment.source.start + offset, marker.fragment.source.end),
            marker.projectedStart + offset,
            true};
}

QRect MarkerCaretController::caretRect(const MarkerEditCaret& caret,
                                       const MarkerProjectionSpan& marker,
                                       QRect markerRect,
                                       const QFontMetrics& metrics)
{
    if (!caret.isValid() || !marker.fragment.source.isValid()
        || caret.markerSource.start != marker.fragment.source.start
        || caret.markerSource.end != marker.fragment.source.end) {
        return {};
    }

    const int offset = qBound(0, caret.sourceOffset - marker.fragment.source.start, marker.text.size());
    const int x = markerRect.left() + metrics.horizontalAdvance(marker.text.left(offset));
    return {x, markerRect.top(), 1, markerRect.height()};
}

std::optional<SourceSpan> MarkerCaretController::sourceSpanForEdit(const MarkerEditCaret& caret,
                                                                   RenderedEditOperation operation)
{
    if (!caret.isValid()) {
        return std::nullopt;
    }

    const int offset = qBound(caret.markerSource.start, caret.sourceOffset, caret.markerSource.end);
    if (operation == RenderedEditOperation::Backspace) {
        if (offset <= caret.markerSource.start) {
            return std::nullopt;
        }
        return SourceSpan{offset - 1, offset};
    }
    if (operation == RenderedEditOperation::Delete) {
        if (offset >= caret.markerSource.end) {
            return std::nullopt;
        }
        return SourceSpan{offset, offset + 1};
    }
    if (operation == RenderedEditOperation::Enter
        || operation == RenderedEditOperation::Indent
        || operation == RenderedEditOperation::Outdent) {
        return std::nullopt;
    }
    return SourceSpan{offset, offset};
}

MarkerEditCaret MarkerCaretController::caretAfterEdit(const MarkerEditCaret& caret,
                                                      SourceSpan editedSource,
                                                      const QString& replacement)
{
    MarkerEditCaret updated = caret;
    if (!updated.isValid() || !editedSource.isValid()) {
        return updated;
    }

    updated.sourceOffset = editedSource.start + replacement.size();
    updated.sourceOffset = qBound(updated.markerSource.start, updated.sourceOffset, updated.markerSource.end);
    updated.projectedPosition = updated.projectedPosition >= 0
        ? updated.projectedPosition + replacement.size() - (editedSource.end - editedSource.start)
        : -1;
    updated.visible = true;
    return updated;
}

int MarkerCaretController::offsetForX(const QString& text, int relativeX, const QFontMetrics& metrics)
{
    if (text.isEmpty() || relativeX <= 0) {
        return 0;
    }

    for (int i = 0; i < text.size(); ++i) {
        const int left = metrics.horizontalAdvance(text.left(i));
        const int right = metrics.horizontalAdvance(text.left(i + 1));
        if (relativeX < left + (right - left) / 2) {
            return i;
        }
    }
    return text.size();
}

} // namespace Muffin
