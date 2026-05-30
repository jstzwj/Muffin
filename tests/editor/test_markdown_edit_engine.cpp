#include "editor/MarkdownEditEngine.h"
#include "parser/CmarkParser.h"
#include "renderer/DocumentRenderer.h"
#include "theme/Theme.h"
#include "theme/ThemeStylesheet.h"

#include <QTest>

using namespace Muffin;

namespace {

struct RenderedFixture {
    ParseResult parsed;
    RenderResult rendered;
};

RenderedFixture renderMarkdown(const QString& markdown)
{
    CmarkParser parser;
    RenderedFixture fixture{parser.parseDocument(markdown), {}};
    ThemeStylesheet stylesheet(Theme::preset(ThemePreset::Github));
    DocumentRenderer renderer(stylesheet);
    fixture.rendered = renderer.render(fixture.parsed.document, fixture.parsed.mathSpans);
    return fixture;
}

int renderedPositionForSourceOffset(const RenderSourceMap& map, int sourceOffset, RenderSourceMap::Bias bias = RenderSourceMap::Bias::Forward)
{
    const std::optional<int> position = map.renderedPositionForSourceOffset(sourceOffset, bias);
    return position.value_or(-1);
}

MarkdownBlock emptyListBlock(const QVector<MarkdownBlock>& blocks)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.kind == RenderSpan::Kind::List && block.content.isValid() && block.content.start == block.content.end) {
            return block;
        }
    }
    return {};
}

const NodeSnapshot* snapshotById(const QVector<NodeSnapshot>& snapshots, MarkdownNodeId nodeId)
{
    for (const NodeSnapshot& snapshot : snapshots) {
        if (snapshot.nodeId == nodeId) {
            return &snapshot;
        }
    }
    return nullptr;
}

MarkdownNodeId firstCodeBlockNodeId(const MarkdownDocument& document)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::CodeBlock) {
            return node.id;
        }
    }
    return 0;
}

} // namespace

class TestMarkdownEditEngine : public QObject
{
    Q_OBJECT

private slots:
    void appliesRenderedEditThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("# Title\n\nHello world"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 15);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 20, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("Qt")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("# Title\n\nHello Qt"));
        QCOMPARE(result.cursorSourceOffset, 17);
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QCOMPARE(result.transaction->kind, EditTransactionKind::RenderedEdit);
        QCOMPARE(result.transaction->beforeMarkdown, QStringLiteral("# Title\n\nHello world"));
        QCOMPARE(result.transaction->afterMarkdown, result.text);
        QCOMPARE(result.transaction->beforeNodes.size(), 1);
        QCOMPARE(result.transaction->afterNodes.size(), 1);
        QCOMPARE(result.transaction->beforeNodes.first().nodeId, result.transaction->affectedNodeIds.first());
        QCOMPARE(result.transaction->afterNodes.first().nodeId, result.transaction->affectedNodeIds.first());
        QCOMPARE(result.transaction->beforeNodes.first().literal, QStringLiteral("Hello world"));
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("Hello Qt"));
        QCOMPARE(result.transaction->operations.size(), 1);
        QCOMPARE(result.transaction->operations.first().kind, EditOperationKind::UpdateLiteral);
        QCOMPARE(result.transaction->operations.first().nodeId, result.transaction->affectedNodeIds.first());
        QCOMPARE(result.transaction->operations.first().beforeNode.literal, QStringLiteral("Hello world"));
        QCOMPARE(result.transaction->operations.first().afterNode.literal, QStringLiteral("Hello Qt"));
        QCOMPARE(result.transaction->beforeNodes.first().content.start, 9);
        QCOMPARE(result.transaction->afterNodes.first().content.end, result.text.size());
        QCOMPARE(result.transaction->beforeNodes.first().previousSiblingId, static_cast<MarkdownNodeId>(0));
        QCOMPARE(result.transaction->beforeNodes.first().nextSiblingId, static_cast<MarkdownNodeId>(0));
        QCOMPARE(result.transaction->beforeNodes.first().ancestorIds.size(), 2);
        QCOMPARE(result.transaction->beforeNodes.first().ancestorIds.first(), result.transaction->beforeNodes.first().parentId);
        QCOMPARE(result.transaction->afterNodes.first().ancestorIds, result.transaction->beforeNodes.first().ancestorIds);
    }

    void appliesRenderedEditInsideCodeBlockContent()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedStart >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::InsertText, renderedStart, renderedStart, QStringLiteral("X")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```cpp\noXld\n```"));
        QCOMPARE(result.cursorSourceOffset, contentStart + 2);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->affectedNodeIds, QVector<MarkdownNodeId>{firstCodeBlockNodeId(fixture.parsed.document)});
        QCOMPARE(result.transaction->beforeNodes.first().literal, QStringLiteral("old\n"));
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("oXld\n"));
        QCOMPARE(result.transaction->beforeNodes.first().content.start, contentStart);
        QCOMPARE(result.transaction->afterNodes.first().content.start, contentStart);
    }

    void appliesEnterInsideCodeBlockAsLiteralNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nab\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("ab"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```cpp\na\nb\n```"));
        QCOMPARE(result.cursorSourceOffset, contentStart + 2);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("a\nb\n"));
    }

    void appliesRenderedEditInsideEmptyCodeBlock()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QChar('\n')) + 1;
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::InsertText, renderedPosition, renderedPosition, QStringLiteral("a")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```\na\n```"));
        QCOMPARE(result.cursorSourceOffset, contentStart + 1);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->affectedNodeIds, QVector<MarkdownNodeId>{firstCodeBlockNodeId(fixture.parsed.document)});
        QCOMPARE(result.transaction->beforeNodes.first().literal, QString());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("a"));
        QCOMPARE(result.transaction->beforeNodes.first().content.start, contentStart);
        QCOMPARE(result.transaction->beforeNodes.first().content.end, contentStart);
        QCOMPARE(result.transaction->afterNodes.first().content.start, contentStart);
        QCOMPARE(result.transaction->afterNodes.first().content.end, contentStart + 1);
    }

    void appliesMultilinePasteInsideCodeBlock()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Paste, renderedPosition, renderedPosition, QStringLiteral("A\nB")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```cpp\noA\nBld\n```"));
        QCOMPARE(result.cursorSourceOffset, contentStart + 4);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("oA\nBld\n"));
    }

    void replacesCodeBlockContentSelectionWithMultilineText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                contentStart + 3,
                                                                RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("new\nline")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```\nnew\nline\n```"));
        QCOMPARE(result.cursorSourceOffset, contentStart + 8);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("new\nline\n"));
    }

    void backspaceDeletesCodeBlockInternalNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\na\nb\n```"));
        const int bOffset = fixture.parsed.document.source().indexOf(QStringLiteral("b"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, bOffset);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```\nab\n```"));
        QCOMPARE(result.cursorSourceOffset, bOffset - 1);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("ab\n"));
    }

    void deleteDeletesCodeBlockInternalNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\na\nb\n```"));
        const int newlineOffset = fixture.parsed.document.source().indexOf(QStringLiteral("\nb"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                     newlineOffset,
                                                                     RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("```\nab\n```"));
        QCOMPARE(result.cursorSourceOffset, newlineOffset);
        QVERIFY(result.transaction.has_value());
        QCOMPARE(result.transaction->afterNodes.first().literal, QStringLiteral("ab\n"));
    }

    void keepsCodeBlockFenceOnBoundaryBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(result.text, fixture.parsed.document.source());
        QCOMPARE(result.cursorSourceOffset, contentStart);
        QVERIFY(!result.transaction.has_value());
    }

    void keepsCodeBlockFenceOnBoundaryDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentEnd = fixture.parsed.document.source().indexOf(QStringLiteral("\n```"),
                                                                        fixture.parsed.document.source().indexOf(QStringLiteral("old")));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                     contentEnd,
                                                                     RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(result.text, fixture.parsed.document.source());
        QCOMPARE(result.cursorSourceOffset, contentEnd);
        QVERIFY(!result.transaction.has_value());
    }

    void appliesSourceSpanEditToMarkerText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("plain **bold** text"));

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete,
             -1,
             -1,
             {},
             RenderedEdit::TargetKind::SourceSpan,
             {6, 7}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("plain *bold** text"));
        QCOMPARE(result.cursorSourceOffset, 6);
        QVERIFY(result.sourceSelection.has_value());
        QCOMPARE(result.sourceSelection->start, 6);
        QCOMPARE(result.sourceSelection->end, 6);
        QVERIFY(!result.transaction.has_value());
    }

    void insertsIntoSourceSpanMarkerCaret()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("plain **bold** text"));

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::InsertText,
             -1,
             -1,
             QStringLiteral("_"),
             RenderedEdit::TargetKind::SourceSpan,
             {7, 7}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("plain *_*bold** text"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void replacesHeadingTextThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("# Hello"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 2);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("Title")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("# Title"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void acceptsMultilineReplacementThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 0);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 5, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("Hi\r\nthere")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hi\nthere"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void replacesAcrossAdjacentPlainSpansThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("i")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hio"));
        QCOMPARE(result.cursorSourceOffset, 2);
    }

    void editsFormattedInlineTextThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**hi**"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 2);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("yo")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("**yo**"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void editsInlineCodeThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("`code`"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 2);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("ut")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("`cute`"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void replacesFormulaAtomThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("a$x+y$b"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("$z$")});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("a$z$b"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void rejectsMixedTextAndFormulaAtomReplacementThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("a$x+y$b"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 0);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("z")});

        QVERIFY(!result.ok);
        QCOMPARE(result.text, QStringLiteral("a$x+y$b"));
    }

    void splitsParagraphThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("# Title\n\nHello world"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 14);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("# Title\n\nHello\n\n world"));
        QCOMPARE(result.cursorSourceOffset, 16);
    }

    void splitsParagraphAtInlineChildBoundaryThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello \n\n**bold**"));
        QCOMPARE(result.cursorSourceOffset, 10);
    }

    void splitsPlainTextInsideMultiInlineParagraphThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bold** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hel\n\nlo **bold** tail"));
        QCOMPARE(result.cursorSourceOffset, 5);
    }

    void mergesParagraphAtInlineChildBoundaryThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello \n\n**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bold**"));
        QCOMPARE(result.cursorSourceOffset, 5);
    }

    void mergesPlainTextMultiInlineSplitWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hel\n\nlo **bold** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 5);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bold** tail"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesPlainTextMultiInlineSplitWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hel\n\nlo **bold** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bold** tail"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void splitsStrongTextThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("**bo**\n\n**ld**"));
        QCOMPARE(result.cursorSourceOffset, 10);
    }

    void splitsStrongTextWithParagraphSiblingsThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bold** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bo**\n\n**ld** tail"));
        QCOMPARE(result.cursorSourceOffset, 16);
    }

    void splitsEmphasisTextThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello *bold* tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 9);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello *bo*\n\n*ld* tail"));
        QCOMPARE(result.cursorSourceOffset, 13);
    }

    void splitsLinkTextThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello [bold](url) tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 9);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello [bo](url)\n\n[ld](url) tail"));
        QCOMPARE(result.cursorSourceOffset, 18);
    }

    void splitsInlineCodeThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello `bold` tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello `bo`\n\n`ld` tail"));
        QCOMPARE(result.cursorSourceOffset, 13);
    }

    void splitsBlockquoteParagraphThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("> Hello world"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("> Hello\n> \n>  world"));
        QCOMPARE(result.cursorSourceOffset, 13);
    }

    void splitsHeadingThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("## AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("## A\n\n## B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void restoresCursorAfterSplittingRepeatedHeadingText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("## AB\n\n## AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 11);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("## AB\n\n## A\n\n## B"));
        QCOMPARE(result.cursorSourceOffset, 16);
    }

    void splitsParagraphAtSelectionStartThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedStart, renderedEnd, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("H\n\no"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void splitsListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B"));
        QCOMPARE(result.cursorSourceOffset, 6);
    }

    void splitsOrderedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("9. AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("9. A\n10. B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void splitsTaskListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- [ ] A\n- [ ] B"));
        QCOMPARE(result.cursorSourceOffset, 14);
    }

    void splitsCheckedTaskListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [x] AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- [x] A\n- [ ] B"));
        QCOMPARE(result.cursorSourceOffset, 14);
    }

    void demotesListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n  - B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void demotesListItemWithNestedChildrenThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n  - B\n    - C"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void demotesOrderedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. A\n   1. B"));
        QCOMPARE(result.cursorSourceOffset, 12);
    }

    void demotesTaskListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] A\n- [ ] B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 14);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- [ ] A\n  - [ ] B"));
        QCOMPARE(result.cursorSourceOffset, 17);
    }

    void demotesSelectedListItemsThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n- C"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedStart, renderedEnd, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n  - B\n  - C"));
        QCOMPARE(result.cursorSourceOffset, 15);
        QVERIFY(result.sourceSelection.has_value());
        QCOMPARE(result.sourceSelection->start, 8);
        QCOMPARE(result.sourceSelection->end, 15);
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QCOMPARE(result.transaction->kind, EditTransactionKind::ListIndent);
        QCOMPARE(result.transaction->affectedNodeIds.size(), 2);
        QCOMPARE(result.transaction->afterSelection->start, result.sourceSelection->start);
        QCOMPARE(result.transaction->afterSelection->end, result.sourceSelection->end);
        QCOMPARE(result.transaction->beforeMarkdown, QStringLiteral("- A\n- B\n- C"));
        QCOMPARE(result.transaction->afterMarkdown, result.text);
        QCOMPARE(result.transaction->beforeNodes.size(), 2);
        QCOMPARE(result.transaction->afterNodes.size(), 2);
        for (MarkdownNodeId nodeId : result.transaction->affectedNodeIds) {
            const NodeSnapshot* before = snapshotById(result.transaction->beforeNodes, nodeId);
            const NodeSnapshot* after = snapshotById(result.transaction->afterNodes, nodeId);
            QVERIFY(before);
            QVERIFY(after);
            QCOMPARE(before->type, MarkdownNodeType::ListItem);
            QCOMPARE(after->type, MarkdownNodeType::ListItem);
            QVERIFY(before->parentId != after->parentId);
            QVERIFY(after->source.isValid());
        }
        const NodeSnapshot* beforeB = snapshotById(result.transaction->beforeNodes, result.transaction->affectedNodeIds.at(0));
        const NodeSnapshot* beforeC = snapshotById(result.transaction->beforeNodes, result.transaction->affectedNodeIds.at(1));
        const NodeSnapshot* afterB = snapshotById(result.transaction->afterNodes, result.transaction->affectedNodeIds.at(0));
        const NodeSnapshot* afterC = snapshotById(result.transaction->afterNodes, result.transaction->affectedNodeIds.at(1));
        QVERIFY(beforeB);
        QVERIFY(beforeC);
        QVERIFY(afterB);
        QVERIFY(afterC);
        QCOMPARE(beforeB->nextSiblingId, beforeC->nodeId);
        QCOMPARE(beforeC->previousSiblingId, beforeB->nodeId);
        QCOMPARE(afterB->nextSiblingId, afterC->nodeId);
        QCOMPARE(afterC->previousSiblingId, afterB->nodeId);
        QVERIFY(afterB->ancestorIds.size() > beforeB->ancestorIds.size());
        QVERIFY(afterB->ancestorIds.contains(afterB->parentId));
        QCOMPARE(result.transaction->operations.size(), 2);
        for (const EditOperation& operation : result.transaction->operations) {
            QCOMPARE(operation.kind, EditOperationKind::MoveNode);
            QVERIFY(result.transaction->affectedNodeIds.contains(operation.nodeId));
            QVERIFY(operation.beforeNode.parentId != operation.afterNode.parentId);
        }
    }

    void promotesNestedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void promotesNestedListItemAndAdoptsFollowingSiblingsThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B\n  - C"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void promotesNestedOrderedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n   1. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 11);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. A\n2. B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void promotesNestedTaskListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] A\n  - [ ] B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 16);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- [ ] A\n- [ ] B"));
        QCOMPARE(result.cursorSourceOffset, 15);
    }

    void promotesSelectedNestedListItemsThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - C"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 15, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedStart, renderedEnd, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B\n- C"));
        QCOMPARE(result.cursorSourceOffset, 11);
        QVERIFY(result.sourceSelection.has_value());
        QCOMPARE(result.sourceSelection->start, 6);
        QCOMPARE(result.sourceSelection->end, 11);
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QCOMPARE(result.transaction->kind, EditTransactionKind::ListOutdent);
        QCOMPARE(result.transaction->affectedNodeIds.size(), 2);
        QCOMPARE(result.transaction->afterSelection->start, result.sourceSelection->start);
        QCOMPARE(result.transaction->afterSelection->end, result.sourceSelection->end);
        QCOMPARE(result.transaction->beforeNodes.size(), 2);
        QCOMPARE(result.transaction->afterNodes.size(), 2);
        for (MarkdownNodeId nodeId : result.transaction->affectedNodeIds) {
            const NodeSnapshot* before = snapshotById(result.transaction->beforeNodes, nodeId);
            const NodeSnapshot* after = snapshotById(result.transaction->afterNodes, nodeId);
            QVERIFY(before);
            QVERIFY(after);
            QCOMPARE(before->type, MarkdownNodeType::ListItem);
            QCOMPARE(after->type, MarkdownNodeType::ListItem);
            QVERIFY(before->parentId != after->parentId);
            QVERIFY(after->source.isValid());
        }
        const NodeSnapshot* beforeB = snapshotById(result.transaction->beforeNodes, result.transaction->affectedNodeIds.at(0));
        const NodeSnapshot* beforeC = snapshotById(result.transaction->beforeNodes, result.transaction->affectedNodeIds.at(1));
        const NodeSnapshot* afterB = snapshotById(result.transaction->afterNodes, result.transaction->affectedNodeIds.at(0));
        const NodeSnapshot* afterC = snapshotById(result.transaction->afterNodes, result.transaction->affectedNodeIds.at(1));
        QVERIFY(beforeB);
        QVERIFY(beforeC);
        QVERIFY(afterB);
        QVERIFY(afterC);
        QCOMPARE(beforeB->nextSiblingId, beforeC->nodeId);
        QCOMPARE(beforeC->previousSiblingId, beforeB->nodeId);
        QCOMPARE(afterB->nextSiblingId, afterC->nodeId);
        QCOMPARE(afterC->previousSiblingId, afterB->nodeId);
        QVERIFY(afterB->ancestorIds.size() < beforeB->ancestorIds.size());
        QVERIFY(afterB->ancestorIds.contains(afterB->parentId));
        QCOMPARE(result.transaction->operations.size(), 2);
        for (const EditOperation& operation : result.transaction->operations) {
            QCOMPARE(operation.kind, EditOperationKind::MoveNode);
            QVERIFY(result.transaction->affectedNodeIds.contains(operation.nodeId));
            QVERIFY(operation.beforeNode.parentId != operation.afterNode.parentId);
        }
    }

    void splitsListItemAndMovesNestedListThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- AB\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B\n  - C"));
        QCOMPARE(result.cursorSourceOffset, 6);
    }

    void exitsEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void exitsNestedEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - "));
        const MarkdownBlock nestedBlock = emptyListBlock(fixture.rendered.blocks);
        QVERIFY(nestedBlock.source.isValid());
        const int renderedPosition = nestedBlock.renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n  - B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void exitsOnlyEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- "));
        QCOMPARE(fixture.rendered.blocks.size(), 1);
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QString());
        QCOMPARE(result.cursorSourceOffset, 0);
    }

    void exitsEmptyOrderedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. A"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void backspaceExitsEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void backspaceExitsNestedEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - "));
        const MarkdownBlock nestedBlock = emptyListBlock(fixture.rendered.blocks);
        QVERIFY(nestedBlock.source.isValid());
        const int renderedPosition = nestedBlock.renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n  - B"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void backspaceExitsOnlyEmptyListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- "));
        QCOMPARE(fixture.rendered.blocks.size(), 1);
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QString());
        QCOMPARE(result.cursorSourceOffset, 0);
    }

    void backspaceExitsEmptyOrderedListItemThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. A"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void mergesParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello\n\nworld"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Helloworld"));
        QCOMPARE(result.cursorSourceOffset, 5);
    }

    void mergesBlockquoteParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("> Hello\n> \n> world"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 12);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("> Helloworld"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void mergesStrongParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bo**\n\n**ld** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 16);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bold** tail"));
        QCOMPARE(result.cursorSourceOffset, 10);
    }

    void mergesStrongParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bo**\n\n**ld** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 12, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello **bold** tail"));
        QCOMPARE(result.cursorSourceOffset, 10);
    }

    void mergesEmphasisParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("*bo*\n\n*ld*"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("*bold*"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesEmphasisParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("*bo*\n\n*ld*"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("*bold*"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesLinkParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello [bo](url)\n\n[ld](url) tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 18);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello [bold](url) tail"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void mergesLinkParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello [bo](url)\n\n[ld](url) tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 15, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello [bold](url) tail"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void mergesInlineCodeParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello `bo`\n\n`ld` tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 14);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello `bold` tail"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void mergesInlineCodeParagraphsWithSiblingsAndDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello `bo`\n\n`ld` tail"));
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedEnd;
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Hello `bold` tail"));
        QCOMPARE(result.cursorSourceOffset, 9);
    }

    void mergesInlineCodeParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("`bo`\n\n`ld`"));
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedEnd;
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("`bold`"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void splitsLooseListItemParagraphThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- Alpha\n\n  Beta"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- Al\n\n  pha\n\n  Beta"));
        QCOMPARE(result.cursorSourceOffset, 8);
    }

    void mergesLooseListItemParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- Alpha\n\n  Beta"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 11);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AlphaBeta"));
        QCOMPARE(result.cursorSourceOffset, 7);
    }

    void mergesListItemsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesListItemsAndKeepsNestedListWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB\n  - C"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesListItemsAndKeepsLooseParagraphsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n\n  C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB\n\n  C"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesOrderedListItemsWithBackspaceThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. AB"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void mergesParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello\n\nworld"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 5, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("Helloworld"));
        QCOMPARE(result.cursorSourceOffset, 5);
    }

    void mergesParagraphAndHeadingWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("A\n\n## B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("AB"));
        QCOMPARE(result.cursorSourceOffset, 1);
    }

    void mergesHeadingAndParagraphWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("## A\n\nB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("## AB"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void deleteBeforeTableIsNoOpThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("A\n\n| x |\n| - |"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(!result.changed);
        QCOMPARE(result.text, QStringLiteral("A\n\n| x |\n| - |"));
        QCOMPARE(result.cursorSourceOffset, 1);
    }

    void mergesListItemsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesListItemsAndKeepsNestedListWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB\n  - C"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesListItemsAndKeepsLooseParagraphsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n\n  C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB\n\n  C"));
        QCOMPARE(result.cursorSourceOffset, 3);
    }

    void mergesOrderedListItemsWithDeleteThroughMarkdownDocumentSerializer()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("1. AB"));
        QCOMPARE(result.cursorSourceOffset, 4);
    }

    void appliesInlineCommandToRenderedTarget()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyInlineCommandToRenderedTarget(
            QStringLiteral("Hello world"), {{1, 1, 1, 11}, QStringLiteral("world")}, &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello **world**"));
    }

    void appliesStructuredInlineStyleCommandToRenderedTarget()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{1, 1, 1, 11}, QStringLiteral("world")},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello **world**"));
    }

    void appliesStructuredInlineStyleUnwrapToRenderedTarget()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{1, 1, 1, 13}, QStringLiteral("world")},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
        QCOMPARE(result.selection.start, 11);
        QCOMPARE(result.selection.end, 11);
    }

    void appliesStructuredStrikethroughCommandToRenderedTarget()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{1, 1, 1, 11}, QStringLiteral("world")},
            StyleCommandHandler::InlineStyle::Strikethrough,
            &MarkdownCommand::toggleStrikethrough);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello ~~world~~"));
    }

    void appliesInlineStyleCommandToRenderedTargetFallbackWhenUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello *world*"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{1, 1, 1, 12}, QStringLiteral("world")},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello ***world***"));
    }

    void appliesStructuredInlineStyleCommandBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {6, 11},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello **world**"));
    }

    void restoresCursorAfterSingleTextWrapWithRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("world world world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {6, 11},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("world **world** world"));
        QCOMPARE(result.selection.start, 13);
        QCOMPARE(result.selection.end, 13);
    }

    void appliesStructuredInlineStyleCommandUnwrapBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {8, 13},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello world"));
    }

    void restoresCursorAfterStructuredInlineStyleWithRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("world **world** world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {8, 13},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("world world world"));
        QCOMPARE(result.selection.start, 11);
        QCOMPARE(result.selection.end, 11);
    }

    void restoresCursorAfterExactUnwrapWithRepeatedStyledText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**world** **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {12, 17},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("**world** world"));
        QCOMPARE(result.selection.start, 15);
        QCOMPARE(result.selection.end, 15);
    }

    void appliesStructuredInlineStyleCommandPartialUnwrapBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {9, 11},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello **w**or**ld**"));
        QCOMPARE(result.selection.start, 13);
        QCOMPARE(result.selection.end, 13);
    }

    void restoresCursorAfterPartialUnwrapWithRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**world** **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {13, 15},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("**world** **w**or**ld**"));
        QCOMPARE(result.selection.start, 17);
        QCOMPARE(result.selection.end, 17);
    }

    void appliesStructuredInlineStyleCommandAcrossInlineSiblingsBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello *world* again"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {3, 15},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hel**lo *world* a**gain"));
    }

    void restoresCursorAfterSiblingWrapWithRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("foo *bar* baz foo *bar* baz"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {14, 26},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("foo *bar* baz **foo *bar* ba**z"));
        QCOMPARE(result.selection.start, 28);
        QCOMPARE(result.selection.end, 28);
    }

    void appliesStructuredInlineStyleCommandAcrossOverlappingStrongBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **world**"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {3, 10},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hel**lo world**"));
        QCOMPARE(result.selection.start, 10);
        QCOMPARE(result.selection.end, 10);
    }

    void restoresCursorAfterOverlappingStrongWithRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("foo **bar** baz foo **bar** baz"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {18, 24},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("foo **bar** baz fo**o bar** baz"));
        QCOMPARE(result.selection.start, 24);
        QCOMPARE(result.selection.end, 24);
    }

    void appliesStructuredStrikethroughCommandBeforeFallback()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {6, 11},
            StyleCommandHandler::InlineStyle::Strikethrough,
            &MarkdownCommand::toggleStrikethrough);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hello ~~world~~"));
        QCOMPARE(result.selection.start, 13);
        QCOMPARE(result.selection.end, 13);
    }

    void appliesInlineStyleCommandFallbackWhenStructuredEditUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello *world*"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            fixture.parsed.document,
            {3, 10},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Hel**lo *wor**ld*"));
    }

    void appliesListCommandToRenderedTarget()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyListCommandToRenderedTarget(
            QStringLiteral("A\nB"), {{1, 1, 2, 1}, {}}, MarkdownCommand::ListType::Unordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("- A\n- B"));
    }

    void appliesStructuredListCommandToRenderedRepeatedParagraph()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Title\n\nTitle"));

        MarkdownCommandResult result = MarkdownEditEngine::applyListCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 6}, QStringLiteral("Title")},
            MarkdownCommand::ListType::Unordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Title\n\n- Title"));
        QCOMPARE(result.selection.start, 14);
        QCOMPARE(result.selection.end, 14);
    }

    void convertsRenderedRepeatedListTargetToOrdered()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n\n- A"));

        MarkdownCommandResult result = MarkdownEditEngine::applyListCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 2}, QStringLiteral("A")},
            MarkdownCommand::ListType::Ordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("1. A\n2. A"));
        QCOMPARE(result.selection.start, 9);
        QCOMPARE(result.selection.end, 9);
    }

    void appliesLegacyListCommandFallbackWhenStructuredUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("> A"));

        MarkdownCommandResult result = MarkdownEditEngine::applyListCommand(
            fixture.parsed.document, {2, 3}, MarkdownCommand::ListType::Unordered);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("- > A"));
    }

    void appliesLegacyHeadingCommandFallbackWhenStructuredUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nx\n```"));

        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommand(
            fixture.parsed.document, {7, 8}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("```cpp\n## x\n```"));
        QCOMPARE(result.error, QString());
    }

    void appliesLegacyParagraphCommandFallbackWhenStructuredUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\n# x\n```"));

        MarkdownCommandResult result = MarkdownEditEngine::applyParagraphCommand(
            fixture.parsed.document, {8, 11});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("```cpp\nx\n```"));
        QCOMPARE(result.error, QString());
    }

    void appliesLegacyQuoteCommandFallbackWhenStructuredUnsupported()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nx\n```"));

        MarkdownCommandResult result = MarkdownEditEngine::applyQuoteCommand(
            fixture.parsed.document, {7, 8});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("```cpp\n> x\n```"));
        QCOMPARE(result.error, QString());
    }

    void appliesHeadingCommandToRenderedTargetUsingBlockContent()
    {
        QVector<MarkdownBlock> blocks{{{0, 7}, {2, 7}, {1, 1, 1, 7}, 0, 5, RenderSpan::Kind::Heading, true}};

        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommandToRenderedTarget(
            QStringLiteral("# Title"), blocks, {{1, 1, 1, 7}, {}}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("## Title"));
    }

    void appliesHeadingCommandToSourceSelection()
    {
        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommand(
            QStringLiteral("Title"), {0, 5}, 2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("## Title"));
    }

    void appliesStructuredHeadingCommandToRenderedRepeatedText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Title\n\nTitle"));

        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 6}, QStringLiteral("Title")},
            2);

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("Title\n\n## Title"));
        QCOMPARE(result.selection.start, 15);
        QCOMPARE(result.selection.end, 15);
    }

    void appliesStructuredParagraphCommandToRenderedRepeatedHeading()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("## Title\n\n## Title"));

        MarkdownCommandResult result = MarkdownEditEngine::applyParagraphCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 6}, QStringLiteral("Title")});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("## Title\n\nTitle"));
        QCOMPARE(result.selection.start, 15);
        QCOMPARE(result.selection.end, 15);
    }

    void appliesStructuredQuoteCommandToRenderedRepeatedParagraph()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("A\n\nA"));

        MarkdownCommandResult result = MarkdownEditEngine::applyQuoteCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 2}, QStringLiteral("A")});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("A\n\n> A"));
        QCOMPARE(result.selection.start, 6);
        QCOMPARE(result.selection.end, 6);
    }

    void appliesStructuredQuoteCommandToRenderedRepeatedList()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n\n- A"));

        MarkdownCommandResult result = MarkdownEditEngine::applyQuoteCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 2}, QStringLiteral("A")});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("- A\n\n> - A"));
        QCOMPARE(result.selection.start, 10);
        QCOMPARE(result.selection.end, 10);
    }

    void togglesStructuredQuoteCommandOffForRenderedRepeatedQuote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("> A\n\n> A"));

        MarkdownCommandResult result = MarkdownEditEngine::applyQuoteCommandToRenderedTarget(
            fixture.parsed.document,
            fixture.rendered.blocks,
            {{3, 1, 3, 2}, QStringLiteral("A")});

        QVERIFY(result.changed);
        QCOMPARE(result.markdown, QStringLiteral("> A\n\nA"));
        QCOMPARE(result.selection.start, 6);
        QCOMPARE(result.selection.end, 6);
    }

    void splitBoldTextHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("**bo**\n\n**ld**"));
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QVERIFY(result.transaction->affectedNodeIds.size() >= 4);
        QCOMPARE(result.transaction->intent, EditIntent::SplitBlock);
        bool hasInsert = false;
        bool hasUpdate = false;
        for (const EditOperation& op : result.transaction->operations) {
            if (op.kind == EditOperationKind::InsertNode) hasInsert = true;
            if (op.kind == EditOperationKind::UpdateLiteral) hasUpdate = true;
        }
        QVERIFY(hasInsert);
        QVERIFY(hasUpdate);
    }

    void splitItalicTextHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("*italic*"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("*ita*\n\n*lic*"));
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QVERIFY(result.transaction->affectedNodeIds.size() >= 4);
    }

    void mergeBoldParagraphsHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**bo**\n\n**ld**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("**bold**"));
        QVERIFY(result.transaction.has_value());
        if (result.transactionValidation.has_value() && !result.transactionValidation->ok) {
            QFAIL(qPrintable(QStringLiteral("Transaction validation failed: ") + result.transactionValidation->errors.join(QStringLiteral("; "))));
        }
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
        QCOMPARE(result.transaction->intent, EditIntent::Delete);
    }

    void mergePlainParagraphsHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello\n\nWorld"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("HelloWorld"));
        QVERIFY(result.transaction.has_value());
        if (result.transactionValidation.has_value() && !result.transactionValidation->ok) {
            QFAIL(qPrintable(QStringLiteral("Transaction validation failed: ") + result.transactionValidation->errors.join(QStringLiteral("; "))));
        }
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
    }

    void splitHeadingHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("## AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("## A\n\n## B"));
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
    }

    void mergeHeadingParagraphHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("A\n\n## B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("AB"));
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
    }

    void splitListItemHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- A\n- B"));
        QVERIFY(result.transaction.has_value());
        QVERIFY(result.transactionValidation.has_value());
        QVERIFY2(result.transactionValidation->ok, qPrintable(result.transactionValidation->errors.join(QStringLiteral("; "))));
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
    }

    void mergeListItemsHasCompleteAffectedNodeIds()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        PatchResult result = MarkdownEditEngine::applyRenderedEdit(fixture.parsed.document, fixture.rendered.sourceMap, fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(result.ok);
        QVERIFY(result.changed);
        QCOMPARE(result.text, QStringLiteral("- AB"));
        QVERIFY(result.transaction.has_value());
        if (result.transactionValidation.has_value() && !result.transactionValidation->ok) {
            QFAIL(qPrintable(QStringLiteral("Transaction validation failed: ") + result.transactionValidation->errors.join(QStringLiteral("; "))));
        }
        QVERIFY(result.transaction->affectedNodeIds.size() >= 2);
    }
};

QTEST_MAIN(TestMarkdownEditEngine)
#include "test_markdown_edit_engine.moc"
