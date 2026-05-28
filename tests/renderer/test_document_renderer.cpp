#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "theme/Theme.h"
#include "theme/ThemeStylesheet.h"

#include <QTest>

using namespace Muffin;

class TestDocumentRenderer : public QObject
{
    Q_OBJECT

private slots:
    void headingDoesNotStartWithEmptyBlock()
    {
        CmarkParser parser;
        AstTree tree = parser.parse(QStringLiteral("# Title"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, QStringLiteral("# Title"));

        QVERIFY(result.document);
        QCOMPARE(result.document->toPlainText(), QStringLiteral("Title"));
    }

    void paragraphDoesNotStartWithEmptyBlock()
    {
        CmarkParser parser;
        AstTree tree = parser.parse(QStringLiteral("Hello"));
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, QStringLiteral("Hello"));

        QVERIFY(result.document);
        QCOMPARE(result.document->toPlainText(), QStringLiteral("Hello"));
    }

};

QTEST_GUILESS_MAIN(TestDocumentRenderer)
#include "test_document_renderer.moc"
