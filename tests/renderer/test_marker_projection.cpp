#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/MarkerFragmentIndex.h"
#include "renderer/MarkerProjection.h"
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

QVector<RenderFragment> visibleFragmentsForOffset(const RenderResult& result, int sourceOffset)
{
    SyntaxTokenIndex tokenIndex(result.syntaxTokens);
    MarkerFragmentIndex fragmentIndex(result.fragments);
    QVector<RenderFragment> fragments;
    for (const SyntaxMarkerPair& pair : tokenIndex.markerPairsContainingSourceOffset(sourceOffset)) {
        fragments.append(fragmentIndex.markerFragmentsForPair(pair));
    }
    return fragments;
}

} // namespace

class TestMarkerProjection : public QObject
{
    Q_OBJECT

private slots:
    void projectsStrongMarkersAroundRenderedContent()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        QCOMPARE(projection.markerSpans().size(), 2);
        QCOMPARE(projection.markerSpans().first().text, QStringLiteral("**"));
        QCOMPARE(projection.markerSpans().first().baseRenderedAnchor, 0);
        QCOMPARE(projection.markerSpans().first().projectedStart, 0);
        QCOMPARE(projection.markerSpans().first().projectedEnd, 2);
        QVERIFY(projection.markerSpans().first().leadingMarker);
        QCOMPARE(projection.markerSpans().last().baseRenderedAnchor, 4);
        QCOMPARE(projection.markerSpans().last().projectedStart, 6);
        QCOMPARE(projection.markerSpans().last().projectedEnd, 8);
        QVERIFY(!projection.markerSpans().last().leadingMarker);
        QCOMPARE(projection.projectedLengthForBaseLength(4), 8);
    }

    void mapsBaseContentPositionToProjectedPosition()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        QCOMPARE(projection.projectedPositionForBasePosition(0), 2);
        QCOMPARE(projection.projectedPositionForBasePosition(2), 4);
        QCOMPARE(projection.projectedPositionForBasePosition(4), 8);
    }

    void resolvesProjectedMarkerPosition()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        const ProjectedRenderPosition opening = projection.resolveProjectedPosition(1);
        QCOMPARE(opening.kind, ProjectedRenderPosition::Kind::Marker);
        QCOMPARE(opening.baseRenderedPosition, 0);
        QCOMPARE(opening.offsetInMarker, 1);
        QCOMPARE(opening.marker.text, QStringLiteral("**"));

        const ProjectedRenderPosition content = projection.resolveProjectedPosition(4);
        QCOMPARE(content.kind, ProjectedRenderPosition::Kind::Content);
        QCOMPARE(content.baseRenderedPosition, 2);
    }

    void findsMarkerAtBaseAnchor()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        const std::optional<MarkerProjectionSpan> opening = projection.markerAtBaseAnchor(0, true);
        QVERIFY(opening.has_value());
        QCOMPARE(opening->fragment.source.start, 0);
        QCOMPARE(opening->fragment.source.end, 2);

        const std::optional<MarkerProjectionSpan> closing = projection.markerAtBaseAnchor(4, false);
        QVERIFY(closing.has_value());
        QCOMPARE(closing->fragment.source.start, 6);
        QCOMPARE(closing->fragment.source.end, 8);

        QVERIFY(!projection.markerAtBaseAnchor(4, true).has_value());
    }

    void mapsProjectedMarkerPositionToSourceOffset()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        QCOMPARE(projection.sourceOffsetForProjectedPosition(0).value_or(-1), 0);
        QCOMPARE(projection.sourceOffsetForProjectedPosition(1).value_or(-1), 1);
        QCOMPARE(projection.sourceOffsetForProjectedPosition(2).value_or(-1), 2);
        QCOMPARE(projection.sourceOffsetForProjectedPosition(6).value_or(-1), 6);
        QCOMPARE(projection.sourceOffsetForProjectedPosition(8).value_or(-1), 8);
        QVERIFY(!projection.sourceOffsetForProjectedPosition(4).has_value());
    }

    void mapsProjectedMarkerRangeToSourceSpan()
    {
        const RenderResult result = renderMarkdown(QStringLiteral("**bold**"));
        MarkerProjection projection(visibleFragmentsForOffset(result, 3), result.sourceMap);

        const std::optional<SourceSpan> opening = projection.sourceSpanForProjectedRange(0, 2);
        QVERIFY(opening.has_value());
        QCOMPARE(opening->start, 0);
        QCOMPARE(opening->end, 2);

        const std::optional<SourceSpan> closing = projection.sourceSpanForProjectedRange(8, 6);
        QVERIFY(closing.has_value());
        QCOMPARE(closing->start, 6);
        QCOMPARE(closing->end, 8);

        QVERIFY(!projection.sourceSpanForProjectedRange(1, 4).has_value());
    }

    void supportsInlineCodeAndEmphasisMarkerText()
    {
        const RenderResult codeResult = renderMarkdown(QStringLiteral("`code`"));
        MarkerProjection codeProjection(visibleFragmentsForOffset(codeResult, 2), codeResult.sourceMap);
        QCOMPARE(codeProjection.markerSpans().size(), 2);
        QCOMPARE(codeProjection.markerSpans().first().text, QStringLiteral("`"));

        const RenderResult emphasisResult = renderMarkdown(QStringLiteral("*em*"));
        MarkerProjection emphasisProjection(visibleFragmentsForOffset(emphasisResult, 2), emphasisResult.sourceMap);
        QCOMPARE(emphasisProjection.markerSpans().size(), 2);
        QCOMPARE(emphasisProjection.markerSpans().first().text, QStringLiteral("*"));
    }

    void ignoresNonMarkerFragments()
    {
        RenderFragment content;
        content.kind = RenderFragment::Kind::Content;
        content.source = {0, 4};
        content.rendered = {0, 4, {0, 4}, {}, RenderSpan::Kind::Text, true, false, {0, 4}, RenderSpan::EditPolicy::LinearText, 1};

        RenderSourceMap sourceMap;
        sourceMap.addSpan(content.rendered);
        MarkerProjection projection({content}, sourceMap);

        QVERIFY(projection.markerSpans().isEmpty());
        QCOMPARE(projection.projectedLengthForBaseLength(4), 4);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMarkerProjection test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_marker_projection.moc"
