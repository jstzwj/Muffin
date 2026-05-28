#pragma once

#include "parser/MathSpan.h"
#include "parser/AstTree.h"
#include <QString>
#include <QVector>

namespace Muffin {

class MathDelimiterScanner {
public:
    static QVector<MathSpan> scan(const QString& markdown, const AstTree& tree);
};

} // namespace Muffin
