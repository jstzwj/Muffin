#include "editor/EditorSelectionMapper.h"
#include "editor/MarkerVisibilityController.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "renderer/SyntaxTokenIndex.h"
#include "theme/ThemeStylesheet.h"

#include <QApplication>
#include <QTest>

using namespace Muffin;

namespace {

struct RenderedMarkdown {
    ParseResult parsed;
    RenderResult rendered;
};

RenderedMarkdown renderMarkdown(const QString& markdown)
{
    CmarkParser parser;
    RenderedMarkdown result;
    result.parsed = parser.parseDocument(markdown);
    ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
    DocumentRenderer renderer(stylesheet);
    result.rendered = renderer.render(result.parsed.document, result.parsed.mathSpans);
    return result;
}

MarkerVisibilityState stateForCaretInText(const QString& markdown, const QString& text)
{
    RenderedMarkdown rendered = renderMarkdown(markdown);
    const int sourceOffset = markdown.indexOf(text) + 1;
    const SelectionBookmark bookmark =
        EditorSelectionMapper::bookmarkForSourceOffset(rendered.parsed.document, sourceOffset);
    return MarkerVisibilityController::stateForCaret(bookmark, SyntaxTokenIndex(rendered.rendered.syntaxTokens));
}

} // namespace

class TestMarkerVisibilityController : public QObject
{
    Q_OBJECT

private slots:
    void expandsStrongMarkersWhenCaretIsInside()
    {
        const MarkerVisibilityState state =
            stateForCaretInText(QStringLiteral("plain **bold** text"), QStringLiteral("bold"));

        QCOMPARE(state.mode, MarkerVisibilityMode::CaretInsideInline);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(state.visiblePairs.first().opening.source.start, 6);
        QCOMPARE(state.visiblePairs.first().closing.source.start, 12);
    }

    void expandsEmphasisMarkersWhenCaretIsInside()
    {
        const MarkerVisibilityState state =
            stateForCaretInText(QStringLiteral("plain *em* text"), QStringLiteral("em"));

        QCOMPARE(state.mode, MarkerVisibilityMode::CaretInsideInline);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::EmphasisMarker);
    }

    void expandsInlineCodeMarkersWhenCaretIsInside()
    {
        const MarkerVisibilityState state =
            stateForCaretInText(QStringLiteral("plain `code` text"), QStringLiteral("code"));

        QCOMPARE(state.mode, MarkerVisibilityMode::CaretInsideInline);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
    }

    void keepsNormalStateWhenCaretIsOutsideSyntax()
    {
        const MarkerVisibilityState state =
            stateForCaretInText(QStringLiteral("plain **bold** text"), QStringLiteral("plain"));

        QCOMPARE(state.mode, MarkerVisibilityMode::Normal);
        QVERIFY(!state.hasVisibleMarkers());
    }

    void expandsMarkersWhenSelectionIntersectsInlineSyntax()
    {
        const QString markdown = QStringLiteral("before **bold** after");
        RenderedMarkdown rendered = renderMarkdown(markdown);
        const int start = markdown.indexOf(QStringLiteral("old"));
        const int end = markdown.indexOf(QStringLiteral("after"));
        const SelectionRangeBookmark selection =
            EditorSelectionMapper::bookmarkForSourceSelection(rendered.parsed.document, {start, end});

        const MarkerVisibilityState state =
            MarkerVisibilityController::stateForSelection(selection, SyntaxTokenIndex(rendered.rendered.syntaxTokens));

        QCOMPARE(state.mode, MarkerVisibilityMode::SelectionIntersectsMarkerPair);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::StrongMarker);
    }

    void pinsMarkerPairFromClickedOpeningMarker()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        RenderedMarkdown rendered = renderMarkdown(markdown);
        const PinnedMarker marker{{6, 8},
                                  rendered.rendered.syntaxTokens.first().nodeId,
                                  SyntaxTokenSpan::Kind::StrongMarker};

        const MarkerVisibilityState state =
            MarkerVisibilityController::stateForPinnedMarker(marker, SyntaxTokenIndex(rendered.rendered.syntaxTokens));

        QCOMPARE(state.mode, MarkerVisibilityMode::PinnedMarkerPair);
        QCOMPARE(state.pinnedMarker.source.start, 6);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visiblePairs.first().opening.source.start, 6);
        QCOMPARE(state.visiblePairs.first().closing.source.start, 12);
    }

    void ignoresPinnedMarkerWhenSourceDoesNotMatchAPair()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        RenderedMarkdown rendered = renderMarkdown(markdown);
        const PinnedMarker marker{{0, 2},
                                  rendered.rendered.syntaxTokens.first().nodeId,
                                  SyntaxTokenSpan::Kind::StrongMarker};

        const MarkerVisibilityState state =
            MarkerVisibilityController::stateForPinnedMarker(marker, SyntaxTokenIndex(rendered.rendered.syntaxTokens));

        QCOMPARE(state.mode, MarkerVisibilityMode::Normal);
        QVERIFY(!state.hasVisibleMarkers());
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    TestMarkerVisibilityController test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_marker_visibility_controller.moc"
