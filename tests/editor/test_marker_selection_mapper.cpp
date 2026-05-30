#include "editor/MarkerSelectionMapper.h"

#include <QTest>

using namespace Muffin;

namespace {

MarkerProjection projectionForStrongMarker()
{
    RenderSourceMap sourceMap;
    RenderSpan nodeSpan;
    nodeSpan.nodeId = 10;
    nodeSpan.source = {6, 14};
    nodeSpan.renderedStart = 0;
    nodeSpan.renderedEnd = 4;
    nodeSpan.kind = RenderSpan::Kind::Strong;
    sourceMap.addSpan(nodeSpan);

    RenderFragment opening;
    opening.nodeId = 10;
    opening.source = {6, 8};
    opening.kind = RenderFragment::Kind::Marker;
    opening.markerKind = SyntaxTokenSpan::Kind::StrongMarker;

    RenderFragment closing;
    closing.nodeId = 10;
    closing.source = {12, 14};
    closing.kind = RenderFragment::Kind::Marker;
    closing.markerKind = SyntaxTokenSpan::Kind::StrongMarker;

    return MarkerProjection({opening, closing}, sourceMap);
}

} // namespace

class TestMarkerSelectionMapper : public QObject
{
    Q_OBJECT

private slots:
    void mapsProjectedRangeInsideOneMarkerToSourceSpan()
    {
        const MarkerProjection projection = projectionForStrongMarker();

        const std::optional<MarkerSelection> selection =
            MarkerSelectionMapper::selectionForProjectedRange(projection, 0, 2);

        QVERIFY(selection.has_value());
        QCOMPARE(selection->kind, MarkerSelection::Kind::SingleMarker);
        QCOMPARE(selection->source.start, 6);
        QCOMPARE(selection->source.end, 8);
        QCOMPARE(selection->projectedStart, 0);
        QCOMPARE(selection->projectedEnd, 2);
    }

    void mapsWholeMarkerPairWithContentToNodeSourceSpan()
    {
        const MarkerProjection projection = projectionForStrongMarker();

        const std::optional<MarkerSelection> selection =
            MarkerSelectionMapper::selectionForProjectedRange(projection, 0, 8);

        QVERIFY(selection.has_value());
        QCOMPARE(selection->kind, MarkerSelection::Kind::MarkerPairWithContent);
        QCOMPARE(selection->source.start, 6);
        QCOMPARE(selection->source.end, 14);
        QCOMPARE(selection->projectedStart, 0);
        QCOMPARE(selection->projectedEnd, 8);
    }

    void rejectsCollapsedSelection()
    {
        const MarkerProjection projection = projectionForStrongMarker();

        const std::optional<MarkerSelection> selection =
            MarkerSelectionMapper::selectionForProjectedRange(projection, 1, 1);

        QVERIFY(!selection.has_value());
    }

    void rejectsSelectionAcrossContentAndMarker()
    {
        const MarkerProjection projection = projectionForStrongMarker();

        const std::optional<MarkerSelection> selection =
            MarkerSelectionMapper::selectionForProjectedRange(projection, 1, 4);

        QVERIFY(!selection.has_value());
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestMarkerSelectionMapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_marker_selection_mapper.moc"
