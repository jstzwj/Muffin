#include "editor/MarkerCaretController.h"

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QTest>

using namespace Muffin;

namespace {

MarkerProjectionSpan strongMarkerSpan()
{
    RenderFragment fragment;
    fragment.nodeId = 10;
    fragment.source = {6, 8};
    fragment.kind = RenderFragment::Kind::Marker;
    fragment.markerKind = SyntaxTokenSpan::Kind::StrongMarker;

    MarkerProjectionSpan marker;
    marker.fragment = fragment;
    marker.text = QStringLiteral("**");
    marker.baseRenderedAnchor = 0;
    marker.projectedStart = 0;
    marker.projectedEnd = 2;
    marker.leadingMarker = true;
    return marker;
}

} // namespace

class TestMarkerCaretController : public QObject
{
    Q_OBJECT

private slots:
    void clickBeforeFirstMarkerCharacterPlacesCaretAtStart()
    {
        const QFontMetrics metrics(QFont(QStringLiteral("Consolas"), 10));
        const MarkerProjectionSpan marker = strongMarkerSpan();
        const QRect markerRect(20, 10, metrics.horizontalAdvance(marker.text), metrics.height());

        const MarkerEditCaret caret =
            MarkerCaretController::caretForClick(marker, markerRect, QPoint(markerRect.left(), markerRect.center().y()), metrics);

        QVERIFY(caret.isValid());
        QCOMPARE(caret.markerSource.start, 6);
        QCOMPARE(caret.sourceOffset, 6);
        QCOMPARE(caret.projectedPosition, 0);
        QVERIFY(caret.visible);
    }

    void clickNearSecondMarkerCharacterPlacesCaretBetweenCharacters()
    {
        const QFontMetrics metrics(QFont(QStringLiteral("Consolas"), 10));
        const MarkerProjectionSpan marker = strongMarkerSpan();
        const QRect markerRect(20, 10, metrics.horizontalAdvance(marker.text), metrics.height());
        const int firstCharWidth = metrics.horizontalAdvance(QStringLiteral("*"));

        const MarkerEditCaret caret = MarkerCaretController::caretForClick(
            marker,
            markerRect,
            QPoint(markerRect.left() + firstCharWidth, markerRect.center().y()),
            metrics);

        QVERIFY(caret.isValid());
        QCOMPARE(caret.sourceOffset, 7);
        QCOMPARE(caret.projectedPosition, 1);
    }

    void sourceSpanForBackspaceDeletesPreviousMarkerCharacter()
    {
        const MarkerEditCaret caret{{6, 8}, 7, 1, true};

        const std::optional<SourceSpan> span =
            MarkerCaretController::sourceSpanForEdit(caret, RenderedEditOperation::Backspace);

        QVERIFY(span.has_value());
        QCOMPARE(span->start, 6);
        QCOMPARE(span->end, 7);
    }

    void sourceSpanForDeleteDeletesNextMarkerCharacter()
    {
        const MarkerEditCaret caret{{6, 8}, 7, 1, true};

        const std::optional<SourceSpan> span =
            MarkerCaretController::sourceSpanForEdit(caret, RenderedEditOperation::Delete);

        QVERIFY(span.has_value());
        QCOMPARE(span->start, 7);
        QCOMPARE(span->end, 8);
    }

    void caretRectUsesSourceOffsetInsideMarker()
    {
        const QFontMetrics metrics(QFont(QStringLiteral("Consolas"), 10));
        const MarkerProjectionSpan marker = strongMarkerSpan();
        const QRect markerRect(20, 10, metrics.horizontalAdvance(marker.text), metrics.height());
        const MarkerEditCaret caret{{6, 8}, 7, 1, true};

        const QRect caretRect = MarkerCaretController::caretRect(caret, marker, markerRect, metrics);

        QVERIFY(caretRect.isValid());
        QCOMPARE(caretRect.left(), markerRect.left() + metrics.horizontalAdvance(QStringLiteral("*")));
        QCOMPARE(caretRect.top(), markerRect.top());
        QCOMPARE(caretRect.height(), markerRect.height());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMarkerCaretController test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_marker_caret_controller.moc"
