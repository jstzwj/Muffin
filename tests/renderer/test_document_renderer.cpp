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

    void recordsBlockIndex()
    {
        const QString markdown = QStringLiteral("# Title\n\nParagraph");
        CmarkParser parser;
        AstTree tree = parser.parse(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, markdown);

        QCOMPARE(result.blocks.size(), 2);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::Heading);
        QCOMPARE(result.blocks.at(0).source.start, 0);
        QCOMPARE(result.blocks.at(0).content.start, 2);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(result.blocks.at(1).source.start, 9);
    }

    void recordsInlineSyntaxTokens()
    {
        const QString markdown = QStringLiteral("**bold** *em* `code`");
        CmarkParser parser;
        AstTree tree = parser.parse(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, markdown);

        QCOMPARE(result.syntaxTokens.size(), 6);
        QCOMPARE(result.syntaxTokens.at(0).kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(result.syntaxTokens.at(0).source.start, 0);
        QCOMPARE(result.syntaxTokens.at(0).source.end, 2);
        QCOMPARE(result.syntaxTokens.at(2).kind, SyntaxTokenSpan::Kind::EmphasisMarker);
        QCOMPARE(result.syntaxTokens.at(2).source.start, 9);
        QCOMPARE(result.syntaxTokens.at(4).kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
        QCOMPARE(result.syntaxTokens.at(4).source.start, 15);
    }

    void recordsListItemBlockIndex()
    {
        const QString markdown = QStringLiteral("- A\n- B");
        CmarkParser parser;
        AstTree tree = parser.parse(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, markdown);

        QCOMPARE(result.blocks.size(), 2);
        QCOMPARE(result.blocks.at(0).kind, RenderSpan::Kind::List);
        QCOMPARE(result.blocks.at(0).source.start, 0);
        QCOMPARE(result.blocks.at(0).content.start, 2);
        QCOMPARE(result.blocks.at(1).kind, RenderSpan::Kind::List);
        QCOMPARE(result.blocks.at(1).source.start, 4);
        QCOMPARE(result.blocks.at(1).content.start, 6);
    }

    void keepsTableCellsNonEditable()
    {
        const QString markdown = QStringLiteral("| A |\n| - |");
        CmarkParser parser;
        AstTree tree = parser.parse(markdown);
        ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
        DocumentRenderer renderer(stylesheet);

        RenderResult result = renderer.render(tree, markdown);

        for (const RenderSpan& span : result.sourceMap.spans()) {
            QVERIFY(span.kind != RenderSpan::Kind::Text || !span.editable);
        }
    }

};

QTEST_GUILESS_MAIN(TestDocumentRenderer)
#include "test_document_renderer.moc"
