#pragma once

#include "model/MarkdownDocument.h"
#include "model/MarkdownSerializer.h"
#include "parser/MathSpan.h"

#include <QVector>

namespace Muffin {

class MarkdownMathSpanBuilder {
public:
    static QVector<MathSpan> build(const MarkdownDocument& document,
                                   const MarkdownSerializationResult& serialization);

private:
    static MathSpan spanForFormulaNode(const MarkdownNode& node,
                                       const MarkdownSerializationResult& serialization);
};

} // namespace Muffin
