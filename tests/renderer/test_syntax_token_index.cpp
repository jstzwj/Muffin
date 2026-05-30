#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/SyntaxTokenIndex.h"
#include "theme/ThemeStylesheet.h"

#include <QApplication>
#include <QTest>

using namespace Muffin;

namespace {

RenderResult renderMarkdown(const QString& markdown)
{
    CmarkParser parser;
    ParseResult parsed = parser.parseDocument(markdown);
    ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
    DocumentRenderer renderer(stylesheet);
    return renderer.render(parsed.document, parsed.mathSpans);
}

const SyntaxTokenSpan* firstTokenOfKind(const QVector<SyntaxTokenSpan>& tokens, SyntaxTokenSpan::Kind kind)
{
    for (const SyntaxTokenSpan& token : tokens) {
        if (token.kind == kind) {
            return &token;
        }
    }
    return nullptr;
}

} // namespace

class TestSyntaxTokenIndex : public QObject
{
    Q_OBJECT

private slots:
    void findsInlineMarkerPairsForCursorInsideSyntax()
    {
        const QString markdown = QStringLiteral("**bold** *em* `code`");
        const RenderResult result = renderMarkdown(markdown);
        SyntaxTokenIndex index(result.syntaxTokens);

        const QVector<SyntaxMarkerPair> strongPairs =
            index.markerPairsContainingSourceOffset(markdown.indexOf(QStringLiteral("bold")) + 1);
        QCOMPARE(strongPairs.size(), 1);
        QCOMPARE(strongPairs.first().opening.kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(strongPairs.first().opening.source.start, 0);
        QCOMPARE(strongPairs.first().closing.source.start, 6);
        QVERIFY(strongPairs.first().opening.nodeId != 0);

        const QVector<SyntaxMarkerPair> emphasisPairs =
            index.markerPairsContainingSourceOffset(markdown.indexOf(QStringLiteral("em")) + 1);
        QCOMPARE(emphasisPairs.size(), 1);
        QCOMPARE(emphasisPairs.first().opening.kind, SyntaxTokenSpan::Kind::EmphasisMarker);
        QCOMPARE(emphasisPairs.first().opening.source.start, 9);
        QCOMPARE(emphasisPairs.first().closing.source.start, 12);

        const QVector<SyntaxMarkerPair> codePairs =
            index.markerPairsContainingSourceOffset(markdown.indexOf(QStringLiteral("code")) + 1);
        QCOMPARE(codePairs.size(), 1);
        QCOMPARE(codePairs.first().opening.kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
        QCOMPARE(codePairs.first().opening.source.start, 15);
        QCOMPARE(codePairs.first().closing.source.start, 18);
    }

    void returnsNoMarkerPairOutsideInlineSyntax()
    {
        const QString markdown = QStringLiteral("plain **bold**");
        const RenderResult result = renderMarkdown(markdown);
        SyntaxTokenIndex index(result.syntaxTokens);

        QVERIFY(index.markerPairsContainingSourceOffset(markdown.indexOf(QStringLiteral("plain"))).isEmpty());
    }

    void filtersTokensByNodeKindSpanAndAdjacentOffset()
    {
        const QString markdown = QStringLiteral("**bold** *em*");
        const RenderResult result = renderMarkdown(markdown);
        SyntaxTokenIndex index(result.syntaxTokens);
        const SyntaxTokenSpan* strong = firstTokenOfKind(result.syntaxTokens, SyntaxTokenSpan::Kind::StrongMarker);
        QVERIFY(strong);

        QCOMPARE(index.tokensForNode(strong->nodeId).size(), 2);
        QCOMPARE(index.tokensForKind(SyntaxTokenSpan::Kind::StrongMarker).size(), 2);
        QCOMPARE(index.tokensIntersecting({0, 2}).size(), 1);
        QCOMPARE(index.tokensAdjacentToSourceOffset(2).size(), 1);
    }

    void markerSourceSpansUpdateAfterMarkdownChanges()
    {
        const QString before = QStringLiteral("**old**");
        const QString after = QStringLiteral("**newer**");
        const RenderResult beforeResult = renderMarkdown(before);
        const RenderResult afterResult = renderMarkdown(after);
        SyntaxTokenIndex beforeIndex(beforeResult.syntaxTokens);
        SyntaxTokenIndex afterIndex(afterResult.syntaxTokens);

        const QVector<SyntaxMarkerPair> beforePairs =
            beforeIndex.markerPairsContainingSourceOffset(before.indexOf(QStringLiteral("old")));
        const QVector<SyntaxMarkerPair> afterPairs =
            afterIndex.markerPairsContainingSourceOffset(after.indexOf(QStringLiteral("newer")));

        QCOMPARE(beforePairs.size(), 1);
        QCOMPARE(afterPairs.size(), 1);
        QCOMPARE(beforePairs.first().closing.source.start, 5);
        QCOMPARE(afterPairs.first().closing.source.start, 7);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestSyntaxTokenIndex test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_syntax_token_index.moc"
