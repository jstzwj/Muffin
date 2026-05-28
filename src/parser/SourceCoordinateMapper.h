#pragma once

#include "parser/AstNode.h"
#include "parser/SourceSpan.h"
#include <QString>
#include <QVector>

namespace Muffin {

class SourceCoordinateMapper {
public:
    explicit SourceCoordinateMapper(QString source = {});

    int offsetForLineColumn(int line, int column) const;
    SourceSpan spanForRange(SourceRange range) const;
    SourceSpan lineSpan(int line) const;
    int lineForOffset(int offset) const;
    int columnForOffset(int offset) const;
    SourceSpan headingContentSpan(SourceRange range, int level) const;

private:
    struct LineMap {
        int utf16Start = 0;
        int utf16End = 0;
        QVector<int> byteOffsetToUtf16Offset;
    };

    QString m_source;
    QVector<LineMap> m_lines;
};

} // namespace Muffin
