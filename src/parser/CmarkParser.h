#pragma once
#include "AstTree.h"
#include "MathSpan.h"
#include <QString>
#include <QVector>
#include <memory>

namespace Muffin {

struct ParseResult {
    AstTree ast;
    QVector<MathSpan> mathSpans;
};

class CmarkParser {
public:
    CmarkParser();
    ~CmarkParser();

    AstTree parse(const QString& markdown);
    AstTree parse(const QByteArray& utf8Data);
    ParseResult parseDocument(const QString& markdown);

private:
    void ensureExtensionsRegistered();

    static bool s_extensionsRegistered;
};

} // namespace Muffin
