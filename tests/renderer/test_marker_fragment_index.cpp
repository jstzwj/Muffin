#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/MarkerFragmentIndex.h"
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

} // namespace

class TestMarkerFragmentIndex : public QObject
{
    Q_OBJECT

private slots:
    void findsMarkerFragmentsForNode()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold** *em*"));
        SyntaxTokenIndex tokenIndex(result.syntaxTokens);
        const QVector<SyntaxMarkerPair> pairs = tokenIndex.markerPairsContainingSourceOffset(3);
        QCOMPARE(pairs.size(), 1);

        MarkerFragmentIndex fragmentIndex(result.fragments);
        const QVector<RenderFragment> fragments = fragmentIndex.markerFragmentsForNode(pairs.first().opening.nodeId);

        QCOMPARE(fragments.size(), 2);
        QCOMPARE(fragments.first().markerKind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(fragments.first().source.start, 0);
        QCOMPARE(fragments.last().source.start, 6);
    }

    void findsMarkerFragmentsForSourceSpan()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerFragmentIndex fragmentIndex(result.fragments);

        const QVector<RenderFragment> fragments = fragmentIndex.markerFragmentsForSourceSpan({0, 2});

        QCOMPARE(fragments.size(), 1);
        QCOMPARE(fragments.first().kind, RenderFragment::Kind::Marker);
        QVERIFY(!fragments.first().visible);
    }

    void findsMarkerFragmentsForPair()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        SyntaxTokenIndex tokenIndex(result.syntaxTokens);
        const QVector<SyntaxMarkerPair> pairs = tokenIndex.markerPairsContainingSourceOffset(3);
        MarkerFragmentIndex fragmentIndex(result.fragments);

        const QVector<RenderFragment> fragments = fragmentIndex.markerFragmentsForPair(pairs.first());

        QCOMPARE(fragments.size(), 2);
        QCOMPARE(fragments.first().source.start, 0);
        QCOMPARE(fragments.last().source.start, 6);
    }

    void findsMarkerFragmentsIntersectingSourceSpan()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold** *em*"));
        MarkerFragmentIndex fragmentIndex(result.fragments);

        const QVector<RenderFragment> fragments = fragmentIndex.markerFragmentsIntersecting({0, 8});

        QCOMPARE(fragments.size(), 2);
        QCOMPARE(fragments.first().markerKind, SyntaxTokenSpan::Kind::StrongMarker);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMarkerFragmentIndex test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_marker_fragment_index.moc"
