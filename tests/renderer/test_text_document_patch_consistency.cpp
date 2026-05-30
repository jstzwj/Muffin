#include "renderer/TextDocumentPatchConsistency.h"

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

using namespace Muffin;

class TestTextDocumentPatchConsistency : public QObject
{
    Q_OBJECT

private slots:
    void acceptsMatchingDocuments()
    {
        QTextDocument patched;
        patched.setPlainText(QStringLiteral("Hello"));
        QTextDocument full;
        full.setPlainText(QStringLiteral("Hello"));

        TextDocumentPatchConsistencyResult result = TextDocumentPatchConsistency::compare(patched, full);

        QVERIFY2(result.ok, qPrintable(result.message()));
    }

    void rejectsBlockTextMismatch()
    {
        QTextDocument patched;
        patched.setPlainText(QStringLiteral("Hello"));
        QTextDocument full;
        full.setPlainText(QStringLiteral("World"));

        TextDocumentPatchConsistencyResult result = TextDocumentPatchConsistency::compare(patched, full);

        QVERIFY(!result.ok);
        QVERIFY(result.message().contains(QStringLiteral("text")));
    }

    void rejectsBlockCountMismatch()
    {
        QTextDocument patched;
        patched.setPlainText(QStringLiteral("Hello"));
        QTextDocument full;
        full.setPlainText(QStringLiteral("Hello\nWorld"));

        TextDocumentPatchConsistencyResult result = TextDocumentPatchConsistency::compare(patched, full);

        QVERIFY(!result.ok);
        QVERIFY(result.message().contains(QStringLiteral("Block count")));
    }

    void rejectsListStateMismatch()
    {
        QTextDocument patched;
        patched.setPlainText(QStringLiteral("Item"));
        QTextDocument full;
        QTextCursor cursor(&full);
        cursor.insertText(QStringLiteral("Item"));
        QTextListFormat listFormat;
        listFormat.setStyle(QTextListFormat::ListDisc);
        cursor.createList(listFormat);

        TextDocumentPatchConsistencyResult result = TextDocumentPatchConsistency::compare(patched, full);

        QVERIFY(!result.ok);
        QVERIFY(result.message().contains(QStringLiteral("list state")));
    }
};

QTEST_GUILESS_MAIN(TestTextDocumentPatchConsistency)
#include "test_text_document_patch_consistency.moc"
