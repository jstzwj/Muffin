#pragma once

#include <QString>
#include <QStringList>

class QTextDocument;

namespace Muffin {

struct TextDocumentPatchConsistencyResult {
    bool ok = false;
    QStringList errors;
    QString message() const { return errors.join(QStringLiteral("; ")); }
};

class TextDocumentPatchConsistency {
public:
    static TextDocumentPatchConsistencyResult compare(const QTextDocument& patched, const QTextDocument& full);
};

} // namespace Muffin
