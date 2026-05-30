#pragma once
#include "AstTree.h"
#include "MathSpan.h"
#include "model/MarkdownDocument.h"
#include <QString>
#include <QVector>
#include <memory>

namespace Muffin {

struct ParseResult {
    AstTree ast;
    MarkdownDocument document;
    QVector<MathSpan> mathSpans;
};

class CmarkParser {
public:
    CmarkParser();
    ~CmarkParser();

    AstTree parse(const QString& markdown);
    AstTree parse(const QByteArray& utf8Data);
    ParseResult parseDocument(const QString& markdown);
    ParseResult parseDocument(const QString& markdown, const MarkdownDocument& previousDocument);

private:
    void ensureExtensionsRegistered();

    static bool s_extensionsRegistered;
};

} // namespace Muffin
