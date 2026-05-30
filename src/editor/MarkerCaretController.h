#pragma once

#include "editor/RenderedEdit.h"
#include "parser/SourceSpan.h"
#include "renderer/MarkerProjection.h"

#include <QPoint>
#include <QRect>
#include <optional>

class QFontMetrics;

namespace Muffin {

struct MarkerEditCaret {
    SourceSpan markerSource;
    int sourceOffset = -1;
    int projectedPosition = -1;
    bool visible = false;

    bool isValid() const
    {
        return markerSource.isValid() && sourceOffset >= markerSource.start && sourceOffset <= markerSource.end;
    }
};

class MarkerCaretController {
public:
    static MarkerEditCaret caretForClick(const MarkerProjectionSpan& marker,
                                         QRect markerRect,
                                         QPoint clickPoint,
                                         const QFontMetrics& metrics);
    static QRect caretRect(const MarkerEditCaret& caret,
                           const MarkerProjectionSpan& marker,
                           QRect markerRect,
                           const QFontMetrics& metrics);
    static std::optional<SourceSpan> sourceSpanForEdit(const MarkerEditCaret& caret,
                                                       RenderedEditOperation operation);
    static MarkerEditCaret caretAfterEdit(const MarkerEditCaret& caret,
                                          SourceSpan editedSource,
                                          const QString& replacement);

private:
    static int offsetForX(const QString& text, int relativeX, const QFontMetrics& metrics);
};

} // namespace Muffin
