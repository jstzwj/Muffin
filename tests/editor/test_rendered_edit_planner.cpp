#include "editor/RenderedEditPlanner.h"
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

MarkdownNodeId firstTextNodeIdWithLiteral(const MarkdownDocument& document, const QString& literal)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::Text && node.literal == literal) {
            return node.id;
        }
    }
    return 0;
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

class TestRenderedEditPlanner : public QObject
{
    Q_OBJECT

private slots:
    void plansTextReplacement()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("# Title\n\nHello world"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 15);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 20, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("Qt")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("Hello world")));
        QCOMPARE(plan->literal, QStringLiteral("Hello Qt"));
        QCOMPARE(plan->cursorSourceOffset, 17);
    }

    void plansSourceSpanReplacementForMarkerEdit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("plain **bold** text"));

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete,
             -1,
             -1,
             {},
             RenderedEdit::TargetKind::SourceSpan,
             {6, 7}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceSourceSpan);
        QCOMPARE(plan->sourceSpan.start, 6);
        QCOMPARE(plan->sourceSpan.end, 7);
        QCOMPARE(plan->cursorSourceOffset, 6);
        QVERIFY(plan->primaryNodeId != 0);
    }

    void plansFormulaNodeReplacement()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("a$x+y$b"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 1);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("$z$")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceFormulaNode);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->literal, QStringLiteral("$z$"));
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void plansCodeBlockContentReplacement()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedStart >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::InsertText, renderedStart, renderedStart, QStringLiteral("X")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("oXld\n"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 2);
    }

    void plansCodeBlockContentSelectionReplacement()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                contentStart + 3,
                                                                RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("new")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("new\n"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 3);
    }

    void plansCodeBlockContentSelectionReplacementWithMultilineText()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                contentStart + 3,
                                                                RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::ReplaceSelection, renderedStart, renderedEnd, QStringLiteral("new\nline")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("new\nline\n"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 8);
    }

    void plansEmptyCodeBlockInsertion()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QChar('\n')) + 1;
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::InsertText, renderedPosition, renderedPosition, QStringLiteral("a")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("a"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 1);
    }

    void plansCodeBlockMultilinePaste()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Paste, renderedPosition, renderedPosition, QStringLiteral("A\nB")});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("oA\nBld\n"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 4);
    }

    void plansCodeBlockEnterAsLiteralNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```cpp\nab\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("ab"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart + 1);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("a\nb\n"));
        QCOMPARE(plan->cursorSourceOffset, contentStart + 2);
    }

    void plansCodeBlockBackspaceDeletingInternalNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\na\nb\n```"));
        const int bOffset = fixture.parsed.document.source().indexOf(QStringLiteral("b"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, bOffset);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("ab\n"));
        QCOMPARE(plan->cursorSourceOffset, bOffset - 1);
    }

    void plansCodeBlockDeleteDeletingInternalNewline()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\na\nb\n```"));
        const int newlineOffset = fixture.parsed.document.source().indexOf(QStringLiteral("\nb"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                     newlineOffset,
                                                                     RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ReplaceNodeLiteral);
        QCOMPARE(plan->primaryNodeId, firstCodeBlockNodeId(fixture.parsed.document));
        QCOMPARE(plan->literal, QStringLiteral("ab\n"));
        QCOMPARE(plan->cursorSourceOffset, newlineOffset);
    }

    void plansCodeBlockStartBackspaceAsNoOp()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentStart = fixture.parsed.document.source().indexOf(QStringLiteral("old"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, contentStart);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::NoOp);
        QCOMPARE(plan->cursorSourceOffset, contentStart);
    }

    void plansCodeBlockEndDeleteAsNoOp()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("```\nold\n```"));
        const int contentEnd = fixture.parsed.document.source().indexOf(QStringLiteral("\n```"),
                                                                        fixture.parsed.document.source().indexOf(QStringLiteral("old")));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap,
                                                                     contentEnd,
                                                                     RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::NoOp);
        QCOMPARE(plan->cursorSourceOffset, contentEnd);
    }

    void plansParagraphSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("# Title\n\nHello world"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 14);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoParagraphs);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("Hello world")));
        QCOMPARE(plan->splitOffset, 5);
        QCOMPARE(plan->cursorSourceOffset, 16);
    }

    void plansParagraphSplitAtInlineChildBoundary()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitParagraphAtChildBoundary);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 8);
    }

    void plansListItemSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoListItems);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("AB")));
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 6);
    }

    void plansOrderedListItemSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoListItems);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("AB")));
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 8);
    }

    void plansOrderedListItemSplitWithWiderNextMarker()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("9. AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoListItems);
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 9);
    }

    void plansTaskListItemSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoListItems);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("AB")));
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 14);
    }

    void plansCheckedTaskListItemSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [x] AB"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitTextNodeIntoListItems);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("AB")));
        QCOMPARE(plan->splitOffset, 1);
        QCOMPARE(plan->cursorSourceOffset, 14);
    }

    void plansListItemDemote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::DemoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 8);
    }

    void plansOrderedListItemDemote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::DemoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 11);
    }

    void plansTaskListItemDemote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] A\n- [ ] B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 14);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::DemoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 16);
    }

    void plansSelectedListItemsDemote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n- C"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Indent, renderedStart, renderedEnd, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::DemoteListItem);
        QCOMPARE(plan->nodeIds.size(), 2);
    }

    void plansNestedListItemPromote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::PromoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 6);
    }

    void plansNestedOrderedListItemPromote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n   1. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 11);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::PromoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 8);
    }

    void plansNestedTaskListItemPromote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- [ ] A\n  - [ ] B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 16);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::PromoteListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 14);
    }

    void plansSelectedNestedListItemsPromote()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - C"));
        const int renderedStart = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        const int renderedEnd = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 15, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedStart >= 0);
        QVERIFY(renderedEnd >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Outdent, renderedStart, renderedEnd, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::PromoteListItem);
        QCOMPARE(plan->nodeIds.size(), 2);
    }

    void plansExitEmptyListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 3);
    }

    void plansExitNestedEmptyListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n  - B\n  - "));
        const MarkdownBlock nestedBlock = emptyListBlock(fixture.rendered.blocks);
        QVERIFY(nestedBlock.source.isValid());
        const int renderedPosition = nestedBlock.renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 9);
    }

    void plansExitOnlyEmptyListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- "));
        QCOMPARE(fixture.rendered.blocks.size(), 1);
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 0);
    }

    void plansExitEmptyOrderedListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void plansBackspaceExitEmptyListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 3);
    }

    void plansBackspaceExitOnlyEmptyListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- "));
        QCOMPARE(fixture.rendered.blocks.size(), 1);
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 0);
    }

    void plansBackspaceExitEmptyOrderedListItem()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. "));
        QCOMPARE(fixture.rendered.blocks.size(), 2);
        const int renderedPosition = fixture.rendered.blocks.at(1).renderedStart;

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::ExitEmptyListItem);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void plansParagraphMergeWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello\n\nworld"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 7);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QVERIFY(plan->primaryNodeId != plan->secondaryNodeId);
        QCOMPARE(plan->cursorSourceOffset, 5);
    }

    void plansBlockquoteParagraphMergeWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("> Hello\n> \n> world"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 12);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QVERIFY(plan->primaryNodeId != plan->secondaryNodeId);
        QCOMPARE(plan->cursorSourceOffset, 7);
    }

    void plansParagraphMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello\n\nworld"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 5, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 5);
    }

    void plansParagraphMergeAtInlineChildBoundaryWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello \n\n**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 6);
    }

    void plansParagraphMergeAtInlineChildBoundaryWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello \n\n**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 6);
    }

    void plansFormattedParagraphMergeWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bo**\n\n**ld** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 16);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 12);
    }

    void plansFormattedParagraphMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("*bo*\n\n*ld*"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void plansListItemMergeWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 6);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeListItems);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 3);
    }

    void plansOrderedListItemMergeWithBackspace()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 8);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Backspace, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeListItems);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void plansListItemMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeListItems);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 3);
    }

    void plansComplexListItemMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("- A\n- B\n  - C"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeListItems);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 3);
    }

    void plansOrderedListItemMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("1. A\n2. B"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4, RenderSourceMap::Bias::Backward);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeListItems);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }

    void rejectsComplexInlineSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello ``wo`rld``"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 10);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(!plan.has_value());
    }

    void plansStrongTextSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("**bold**"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("bold")));
        QCOMPARE(plan->splitOffset, 2);
        QCOMPARE(plan->cursorSourceOffset, 8);
    }

    void plansEmphasisTextSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("*bold*"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("bold")));
        QCOMPARE(plan->splitOffset, 2);
        QCOMPARE(plan->cursorSourceOffset, 6);
    }

    void plansLinkTextSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("[bold](url)"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("bold")));
        QCOMPARE(plan->splitOffset, 2);
        QCOMPARE(plan->cursorSourceOffset, 11);
    }

    void plansInlineCodeSplit()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("`bold`"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 4);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QCOMPARE(plan->splitOffset, 2);
        QCOMPARE(plan->cursorSourceOffset, 7);
    }

    void plansPlainTextSplitInsideMultiInlineParagraph()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("Hello **bold** tail"));
        const int renderedPosition = renderedPositionForSourceOffset(fixture.rendered.sourceMap, 3);
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Enter, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::SplitInlineNodeIntoParagraphs);
        QCOMPARE(plan->primaryNodeId, firstTextNodeIdWithLiteral(fixture.parsed.document, QStringLiteral("Hello ")));
        QCOMPARE(plan->splitOffset, 3);
        QCOMPARE(plan->cursorSourceOffset, 5);
    }

    void plansInlineCodeParagraphMergeWithDelete()
    {
        RenderedFixture fixture = renderMarkdown(QStringLiteral("`bo`\n\n`ld`"));
        const int renderedPosition = fixture.rendered.blocks.at(0).renderedEnd;
        QVERIFY(renderedPosition >= 0);

        std::optional<RenderedEditPlan> plan = RenderedEditPlanner::plan(
            fixture.parsed.document,
            fixture.rendered.sourceMap,
            fixture.rendered.blocks,
            {RenderedEditOperation::Delete, renderedPosition, renderedPosition, {}});

        QVERIFY(plan.has_value());
        QCOMPARE(plan->kind, RenderedEditPlan::Kind::MergeParagraphs);
        QVERIFY(plan->primaryNodeId != 0);
        QVERIFY(plan->secondaryNodeId != 0);
        QCOMPARE(plan->cursorSourceOffset, 4);
    }
};

QTEST_MAIN(TestRenderedEditPlanner)
#include "test_rendered_edit_planner.moc"
