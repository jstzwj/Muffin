#include "document/DocumentSession.h"
#include "document/MarkdownDocument.h"
#include "document/TextSelection.h"
#include "parser/CmarkGfmParser.h"
#include "render/DocumentLayout.h"
#include "render/LayoutPositionIndex.h"
#include "theme/CssThemeMapper.h"
#include "theme/RenderTheme.h"
#include "theme/NodeCssElement.h"
#include "render/InlineLayout.h"
#include "theme/ThemeDefinition.h"

#include <QApplication>
#include <QDebug>

#include <functional>
#include <iostream>

#include "RenderTestUtils.h"

using namespace muffin;

namespace {

void requireSameTopLevelLayout(const DocumentLayout& incremental, const DocumentLayout& full,
                               const MarkdownDocument& document, const QString& label) {
  require(incremental.slotCount() == full.slotCount(), label + QStringLiteral(" block count mismatch"));
  require(qAbs(incremental.totalHeight() - full.totalHeight()) < 0.01,
          label + QStringLiteral(" total height mismatch"));
  for (const auto& child : document.root().children()) {
    const BlockLayout* incrementalBlock = incremental.block(child->id());
    const BlockLayout* fullBlock = full.block(child->id());
    require(incrementalBlock != nullptr, label + QStringLiteral(" incremental block index missing"));
    require(fullBlock != nullptr, label + QStringLiteral(" full block index missing"));
    const QRectF incrementalRect = incrementalBlock->rect();
    const QRectF fullRect = fullBlock->rect();
    require(qAbs(incrementalRect.left() - fullRect.left()) < 0.01, label + QStringLiteral(" block left mismatch"));
    require(qAbs(incrementalRect.top() - fullRect.top()) < 0.01, label + QStringLiteral(" block top mismatch"));
    require(qAbs(incrementalRect.width() - fullRect.width()) < 0.01, label + QStringLiteral(" block width mismatch"));
    require(qAbs(incrementalRect.height() - fullRect.height()) < 0.01,
            label + QStringLiteral(" block height mismatch"));
  }
}

void testIncrementalBlockRebuildContract() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"), false);

  RenderTheme theme = RenderTheme::github();
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const NodeId firstParagraphId = mutableBlockAt(session.document(), 0)->id();
  const NodeId secondParagraphId = mutableBlockAt(session.document(), 1)->id();
  const QRectF firstBefore = layout.block(firstParagraphId)->rect();
  const QRectF secondBefore = layout.block(secondParagraphId)->rect();
  const qreal totalBefore = layout.totalHeight();

  const DocumentLayout::BlockRebuildResult unchangedResult =
      layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(unchangedResult.rebuilt, QStringLiteral("unchanged paragraph rebuild should succeed"));
  require(qFuzzyIsNull(unchangedResult.heightDelta),
          QStringLiteral("unchanged paragraph rebuild should have zero height delta"));
  require(unchangedResult.shiftedRect.isEmpty(), QStringLiteral("zero-height-delta rebuild must not dirty the suffix"));
  require(layout.block(secondParagraphId)->rect().top() == secondBefore.top(),
          QStringLiteral("zero-height-delta rebuild must not move following blocks"));
  require(layout.totalHeight() == totalBefore,
          QStringLiteral("zero-height-delta rebuild must not change total height"));

  require(
      session.applyTextDelta(
          5, 0,
          QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"
                         " alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"),
          true, {LocalEditNodeHint{firstParagraphId, 0, BlockType::Paragraph}}),
      QStringLiteral("paragraph local delta should apply"));
  const DocumentLayout::BlockRebuildResult paragraphResult =
      layout.rebuildBlock(firstParagraphId, session.document(), theme, {});
  require(paragraphResult.rebuilt, QStringLiteral("paragraph block rebuild should succeed"));
  require(paragraphResult.blockId == firstParagraphId, QStringLiteral("paragraph rebuild result id mismatch"));
  require(paragraphResult.oldRect == firstBefore, QStringLiteral("paragraph rebuild old rect mismatch"));
  require(paragraphResult.newRect == layout.block(firstParagraphId)->rect(),
          QStringLiteral("paragraph rebuild new rect mismatch"));
  require(paragraphResult.heightDelta != 0, QStringLiteral("paragraph rebuild should report height delta"));
  require(!paragraphResult.shiftedRect.isEmpty(), QStringLiteral("paragraph rebuild should report shifted rect"));
  require(layout.block(secondParagraphId)->rect().top() != secondBefore.top(),
          QStringLiteral("paragraph rebuild should shift following block"));
  require(layout.totalHeight() != totalBefore, QStringLiteral("paragraph rebuild should update total height"));

  const MarkdownNode* tableCell = findFirstTableCell(session.document().root());
  require(tableCell != nullptr, QStringLiteral("incremental table cell missing"));
  MarkdownNode* table = mutableBlockAt(session.document(), 2);
  require(layout.block(tableCell->id()) != nullptr, QStringLiteral("layout index should include table cell"));
  const QRectF tableBefore = layout.block(table->id())->rect();
  const DocumentLayout::BlockRebuildResult tableResult =
      layout.rebuildBlock(tableCell->id(), session.document(), theme, {});
  require(tableResult.rebuilt, QStringLiteral("table cell rebuild should map to top-level table"));
  require(tableResult.blockId == table->id(), QStringLiteral("table cell rebuild should report table block id"));
  require(tableResult.oldRect == tableBefore, QStringLiteral("table cell rebuild old rect mismatch"));
  require(tableResult.newRect == layout.block(table->id())->rect(),
          QStringLiteral("table cell rebuild new rect mismatch"));
}

void testIncrementalTopLevelRangeRebuildContract() {
  RenderTheme theme = RenderTheme::github();
  auto verifyRangeEdit = [&](const QString& initial, qsizetype sourceStart, qsizetype removedLength,
                             const QString& insertedText, const QString& label) {
    DocumentSession session;
    session.setMarkdownText(initial, false);
    DocumentLayout incremental;
    incremental.rebuild(session.document(), theme, 800.0);

    require(session.applyTextDelta(sourceStart, removedLength, insertedText, true),
            label + QStringLiteral(" local edit should apply"));
    const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
    require(range.isValid(), label + QStringLiteral(" range should be valid"));
    const DocumentLayout::RangeRebuildResult result =
        incremental.rebuildTopLevelRange(range, session.document(), theme, {});
    require(result.rebuilt, label + QStringLiteral(" range rebuild should succeed"));

    DocumentLayout full;
    full.rebuild(session.document(), theme, 800.0);
    requireSameTopLevelLayout(incremental, full, session.document(), label);
  };

  verifyRangeEdit(QStringLiteral("alpha beta\n\ngamma"), 5, 0, QStringLiteral("\n\n"),
                  QStringLiteral("paragraph split"));
  verifyRangeEdit(QStringLiteral("alpha\n\nbeta\n\ngamma"), 5, 2, QStringLiteral(" "),
                  QStringLiteral("paragraph merge"));
  verifyRangeEdit(QStringLiteral("# Heading\n\nalpha\n\nbeta"), 9, 0, QStringLiteral("\n\n"),
                  QStringLiteral("heading boundary insert"));
  verifyRangeEdit(QStringLiteral("alpha\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n\nomega"), 5, 0,
                  QStringLiteral("\n\ninserted"), QStringLiteral("table suffix shift"));
}

void testRetainedSuffixKeepsCurrentSourceCoordinates() {
  const QString initial =
      QStringLiteral("alpha\n\n中文 **bold** [link](https://example.com)\n\n"
                     "- outer **bold**\n  - nested [link](https://example.com)\n\n"
                     "12. ordered\n    1. nested **bold**\n\n"
                     "| A | B |\n| --- | --- |\n| 中文 **bold** | [link](https://example.com) |\n\n"
                     "[ref]: https://example.com \"title\"\n\n[^note]: footnote **body**\n\n> quoted **text**");
  for (const auto policy : {DocumentLayout::BuildPolicy::Eager, DocumentLayout::BuildPolicy::Lazy}) {
    DocumentSession session;
    session.setMarkdownText(initial, false);
    RenderTheme theme = RenderTheme::github();
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800, {}, {}, policy);
    layout.buildAll(theme);
    const auto suffixId = mutableBlockAt(session.document(), 1)->id();
    const auto* retained = layout.block(suffixId);
    const auto* retainedText = retained->inlineLayout();

    const auto verify = [&] {
      RenderTheme freshTheme = RenderTheme::github();
      DocumentLayout full;
      full.rebuild(session.document(), freshTheme, 800);
      // Exercise hit testing before asking for any retained block by id.
      const std::function<void(const BlockLayout&)> compare = [&](const BlockLayout& expected) {
        const auto* text = expected.inlineLayout();
        if (text && !text->visibleText().isEmpty()) {
          const auto point = text->cursorRect(1).translated(expected.inlineTextOrigin()).center();
          const auto hit = layout.hitTest(point, theme);
          const auto freshHit = full.hitTest(point, freshTheme);
          require(hit.blockId == freshHit.blockId && hit.sourceOffset == freshHit.sourceOffset,
                  QStringLiteral("retained suffix hit matches fresh source after edits"));
        }
        const auto* actual = layout.block(expected.nodeId());
        require(actual && actual->contentSourceStart() == expected.contentSourceStart(),
                QStringLiteral("retained text source origin matches fresh layout"));
        require(actual->tableRows().size() == expected.tableRows().size(), "table shape is retained");
        for (size_t row = 0; row < expected.tableRows().size(); ++row) {
          for (size_t col = 0; col < expected.tableRows()[row].cells.size(); ++col) {
            const auto& cell = expected.tableRows()[row].cells[col];
            require(actual->tableRows()[row].cells[col].contentSourceStart == cell.contentSourceStart,
                    "table cell source follows preceding edits");
            const auto point = cell.text.cursorRect(0).translated(cell.textOrigin()).center();
            require(layout.hitTest(point, theme).sourceOffset == full.hitTest(point, freshTheme).sourceOffset,
                    "table cell hit uses the current document source");
          }
        }
        require(actual->definitionSlots().size() == expected.definitionSlots().size(), "definition shape is retained");
        for (qsizetype i = 0; i < expected.definitionSlots().size(); ++i) {
          const auto& slot = expected.definitionSlots()[i];
          const auto& actualSlot = actual->definitionSlots()[i];
          require(actualSlot.sourceStart == slot.sourceStart && actualSlot.sourceEnd == slot.sourceEnd,
                  "definition source follows preceding edits");
        }
        for (const auto& child : expected.children())
          compare(*child);
      };
      for (const auto* block : full.promotedBlocks())
        compare(*block);
      require(layout.block(suffixId) == retained && retained->inlineLayout() == retainedText,
              "source synchronization retains unchanged shaped text");
    };

    // Both growth and shrinkage accumulate while unrelated blocks retain their
    // layouts. UTF-16 surrogate pairs must count in the document's coordinate unit.
    const auto firstId = mutableBlockAt(session.document(), 0)->id();
    for (const auto& edit :
         {QPair<qsizetype, QString>{0, QStringLiteral("中文🙂")}, QPair<qsizetype, QString>{1, QString()},
          QPair<qsizetype, QString>{3, QStringLiteral("X")}}) {
      require(session.applyTextDelta(0, edit.first, edit.second, true,
                                     {LocalEditNodeHint{firstId, 0, BlockType::Paragraph}}),
              "edit preceding paragraph without changing suffix content");
      require(layout.rebuildBlock(firstId, session.document(), theme, {}).rebuilt, "rebuild edited paragraph");
      verify();
    }
    const QString prefix = QStringLiteral("inserted paragraph\n\n");
    for (const bool insert : {true, false}) {
      require(session.applyTextDelta(0, insert ? 0 : prefix.size(), insert ? prefix : QString(), true),
              "insert or remove a preceding block");
      require(
          layout.rebuildTopLevelRange(session.lastLocalTopLevelRangeChange(), session.document(), theme, {}).rebuilt,
          "rebuild structural edit before retained suffix");
      verify();
    }
  }
}

void testLayoutPositionIndexPreservesLazySuffixAcrossSplice() {
  LayoutPositionIndex index;
  const QVector<LayoutPositionToken*> original = index.reset(5);
  index.addSuffix(2, 12.5);

  auto firstReplacement = index.makeToken();
  LayoutPositionToken* firstReplacementPtr = firstReplacement.get();
  auto secondReplacement = index.makeToken();
  LayoutPositionToken* secondReplacementPtr = secondReplacement.get();
  auto replacements = LayoutPositionIndex::merge(std::move(firstReplacement), std::move(secondReplacement));
  index.replace(1, 2, std::move(replacements));

  const QVector<LayoutPositionToken*> expectedOrder{original.at(0), firstReplacementPtr, secondReplacementPtr,
                                                    original.at(3), original.at(4)};
  for (qsizetype i = 0; i < expectedOrder.size(); ++i) {
    require(index.rank(expectedOrder.at(i)) == i, QStringLiteral("layout position rank mismatch at %1").arg(i));
  }
  require(qFuzzyIsNull(index.adjustmentFor(firstReplacementPtr)),
          QStringLiteral("fresh layout token must start without an inherited shift"));
  require(qAbs(index.adjustmentFor(original.at(3)) - 12.5) < 0.001,
          QStringLiteral("unchanged layout suffix must retain its lazy shift through splice"));
  index.addSuffix(1, -4.25);
  index.clearAdjustments();
  for (qsizetype i = 0; i < expectedOrder.size(); ++i) {
    require(index.rank(expectedOrder[i]) == i && qFuzzyIsNull(index.adjustmentFor(expectedOrder[i])),
            QStringLiteral("theme geometry reset preserves identities/order and clears every lazy ancestor shift"));
  }
  index.addSuffix(2, 7);
  index.replace(1, 1, index.makeToken());
  require(qAbs(index.adjustmentFor(original[4]) - 7) < .001,
          QStringLiteral("cleared position tree still supports subsequent shifts and splices"));
}

void testExactCompactCssSemanticSignatures() {
  const auto signature = [](const QString& markdown) {
    DocumentSession session;
    session.setMarkdownText(markdown, false);
    return cssSemanticSignature(session.document().root());
  };
  require(signature("plain **bold** *em* [link](first)") == signature("other **text** *words* [title](first)"),
          QStringLiteral("ordinary glyph edits preserve CSS-visible semantic shape"));
  const QVector<QString> variants{"plain **bold** *em* [link](first)",
                                  "plain *bold* **em** [link](first)",
                                  "plain **bold** *em* [link](second)",
                                  "plain **bold** *em* [link](first) <kbd>A</kbd>",
                                  "plain **bold** *em* [link](first) <kbd class='other'>A</kbd>",
                                  "plain **bold** *em* [link](first)\n\nsecond",
                                  "# heading",
                                  "## heading",
                                  "- item",
                                  "1. item",
                                  "- [ ] item",
                                  "- [x] item",
                                  "> [!NOTE]\n> title",
                                  "> [!TIP]\n> title",
                                  "```cpp\ncode\n```",
                                  "```rust\ncode\n```",
                                  QString("[x](") + QString(127, 'a') + ")",
                                  QString("[x](") + QString(128, 'a') + ")",
                                  QString("[x](") + QString(16384, 'a') + ")",
                                  QStringLiteral("[x](https://example.com/中文)")};
  QSet<QByteArray> shapes;
  for (const auto& variant : variants) {
    const auto shape = signature(variant);
    require(!shapes.contains(shape),
            QStringLiteral("CSS attribute, nesting, enum and variable-length boundaries remain distinct: %1")
                .arg(variant.left(120)));
    shapes.insert(shape);
  }
  require(signature(QString("*x* ").repeated(127)) != signature(QString("*x* ").repeated(128)),
          QStringLiteral("variable-length inline counts cannot alias"));
}

void testThemeRebuildReusesNavigationWithoutStaleGeometry() {
  const QVector<ThemeDefinition> definitions{
      CssThemeMapper::fromCss("#write{counter-reset:n}h1::before{content:counter(n);counter-increment:n}"
                              "p{font-size:13px;margin:7px 0;color:#123456}",
                              "first", {}),
      CssThemeMapper::fromCss("#write{counter-reset:n 8}h1::before{content:counter(n);counter-increment:n 2}"
                              "p{font-size:20px;margin:19px 0;color:#654321}",
                              "second", {}),
      CssThemeMapper::fromCss("#write{display:flex;flex-direction:column;gap:5px}p{font-size:16px}"
                              "li::before{content:counter(n);counter-increment:n}",
                              "formatting", {})};
  DocumentSession session;
  session.setMarkdownText("# Title\n\n[TOC]\n\n" + QString("paragraph **bold** words ").repeated(20) +
                              "\n\n- parent\n  - child\n\nlast paragraph",
                          false);
  DocumentLayout layout;
  auto theme = RenderTheme::fromDefinition(definitions[0]);
  layout.rebuild(session.document(), theme, 440, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  layout.visibleBlocks({0, 0, 440, 250}, theme); // creates lazy suffix adjustments
  int definitionIndex = 0;
  const auto compare = [&] {
    auto fullTheme = RenderTheme::fromDefinition(definitions[definitionIndex]);
    DocumentLayout full;
    full.rebuild(session.document(), fullTheme, 440);
    layout.buildAll(theme);
    requireSameTopLevelLayout(layout, full, session.document(), "theme rebuild");
    for (const auto& node : session.document().root().children()) {
      const auto* actual = layout.block(node->id());
      const auto* expected = full.block(node->id());
      require(layout.topLevelIndexFor(node->id()) == node->siblingIndex(), "reused navigation retains exact ranks");
      if (actual->inlineLayout() && expected->inlineLayout()) {
        require(actual->inlineLayout()->displayText() == expected->inlineLayout()->displayText(),
                "theme-generated text is fresh");
        for (int offset : {0, 3, 15})
          require(actual->inlineLayout()->cursorRectForSourceOffset(offset) ==
                      expected->inlineLayout()->cursorRectForSourceOffset(offset),
                  "font/wrap changes preserve editing coordinates relative to fresh layout");
      }
      if (!node->children().empty())
        require(layout.topLevelBlockIdFor(node->children().front()->id()) == node->id(),
                "nested navigation remains valid");
    }
    require(theme.resolveDocumentCounters(session.document()) == fullTheme.resolveDocumentCounters(session.document()),
            "numbering and generated content match a full counter walk");
  };
  for (int i : {1, 0, 2, 1, 2, 0}) {
    definitionIndex = i;
    auto next = RenderTheme::fromDefinition(definitions[i]);
    next.reuseDocumentSemantics(theme);
    theme = std::move(next);
    layout.rebuild(session.document(), theme, 440, {}, {}, DocumentLayout::BuildPolicy::Lazy);
    compare();
  }
  require(session.applyTextDelta(0, 0, "# Inserted\n\n", true), "insert a numbered heading");
  require(layout.rebuildTopLevelRange(session.lastLocalTopLevelRangeChange(), session.document(), theme, {}).rebuilt,
          "local splice before theme rebuild succeeds");
  layout.rebuild(session.document(), theme, 440, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  compare();
  const auto oldId = session.document().root().children().front()->id();
  session.setMarkdownText("[TOC]\n\n# Reset document\n\n- new parent\n  - new child", false);
  layout.rebuild(session.document(), theme, 440, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  require(layout.topLevelIndexFor(oldId) < 0, "document reset rejects old navigation");
  compare();
  DocumentSession other;
  other.setMarkdownText("[TOC]\n\n# Other document", false);
  DocumentSession original;
  original.setMarkdownText("[TOC]\n\n# Original document", false);
  require(other.document().outlineRevision() == original.document().outlineRevision(),
          "two documents share an outline revision");
  layout.rebuild(original.document(), theme, 440);
  layout.rebuild(other.document(), theme, 440);
  const auto* toc = layout.block(other.document().root().children().front()->id());
  require(toc && toc->isToc() && toc->tocEntries().front().target == other.document().root().children().back()->id(),
          "outline reuse checks document identity as well as revision");
}

void testRangeRebuildAfterLazySuffixShiftMatchesFullLayout() {
  const RenderTheme theme = RenderTheme::github();
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("alpha\n\nbeta\n\ngamma delta\n\nomega"), false);
  DocumentLayout incremental;
  incremental.rebuild(session.document(), theme, 360.0);

  const NodeId firstId = mutableBlockAt(session.document(), 0)->id();
  require(session.applyTextDelta(
              5, 0, QStringLiteral(" alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha alpha"), true,
              {LocalEditNodeHint{firstId, 0, BlockType::Paragraph}}),
          QStringLiteral("height-changing edit should apply"));
  require(incremental.rebuildBlock(firstId, session.document(), theme, {}).rebuilt,
          QStringLiteral("height-changing block rebuild should succeed"));

  const QString current = session.markdownText().toString();
  const qsizetype splitAt = current.indexOf(QStringLiteral("gamma")) + 5;
  require(session.applyTextDelta(splitAt, 0, QStringLiteral("\n\n"), true),
          QStringLiteral("structural split after lazy suffix shift should apply"));
  const TopLevelRangeChange range = session.lastLocalTopLevelRangeChange();
  require(range.isValid(), QStringLiteral("structural split should expose a range change"));
  require(incremental.rebuildTopLevelRange(range, session.document(), theme, {}).rebuilt,
          QStringLiteral("range rebuild after lazy suffix shift should succeed"));

  DocumentLayout full;
  full.rebuild(session.document(), theme, 360.0);
  requireSameTopLevelLayout(incremental, full, session.document(),
                            QStringLiteral("range rebuild after accumulated lazy suffix shift"));
}

} // namespace

// A structural (adjacent-sibling) spacing rule is honored by the structural cascade
// (fast=false) but not the prototype estimate (fast=true). Before the fix, promoteSlot
// left a promoted block's gap at the prototype value while rebuildBlock recomputed it
// structurally — so a Lazy layout's gaps differed from an Eager (structural) layout, and
// clicking a paragraph snapped the gap (the "bottom margin jumps" effect). After the fix,
// promotion snaps to the structural gap via the shared nextTopAfterBuild formula, so
// Lazy+promote matches Eager block-for-block.
void testLazyPromoteMatchesEagerGap() {
  const QString css =
      QStringLiteral("#write { color:#000000; }"
                     "p { margin-bottom: 10px; }"
                     "p + ul { margin-top: 80px; }"); // exaggerated: prototype sees ul margin-top 0, structural sees 80
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("gap"), QString()));

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("para\n\n- one\n- two\n"), false);
  const NodeId paraId = mutableBlockAt(session.document(), 0)->id();
  const NodeId ulId = mutableBlockAt(session.document(), 1)->id();

  DocumentLayout eager;
  eager.rebuild(session.document(), theme, 800.0); // Eager default → structural spacing throughout
  const qreal eagerGap = eager.block(ulId)->rect().top() - eager.block(paraId)->rect().bottom();
  require(qAbs(eagerGap - 80.0) < 1.0,
          QStringLiteral("Eager (structural) must honor p+ul margin-top:80px (gap=%1)").arg(eagerGap));

  DocumentLayout lazy;
  lazy.rebuild(session.document(), theme, 800.0, SelectionRange{}, QString{}, DocumentLayout::BuildPolicy::Lazy);
  lazy.ensureBuilt(0, lazy.slotCount() - 1, theme); // promote every slot → snap gaps to structural

  requireSameTopLevelLayout(lazy, eager, session.document(),
                            QStringLiteral("Lazy+promote must match Eager (structural spacing, no gap jump)"));
}

// A trailing empty line in a (nested) blockquote — what Enter creates at the end of a
// quote line — must render and be caret-able. Before the fix, omitVirtualEmptyParagraph-
// InRenderFlow dropped EVERY blockquote VEP, so the Enter-continuation line had no
// BlockLayout (the caret vanished) and the blockquote height didn't grow (no visible
// change). After the fix the trailing VEP renders.
void testTrailingEmptyNestedBlockquoteLineRenders() {
  const RenderTheme theme = RenderTheme::github();

  DocumentSession session;
  session.setMarkdownText(QStringLiteral("> > Nested quote.\n> >\n> > "), false);
  DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0);

  const MarkdownNode* bq = findFirstBlock(session.document().root(), BlockType::BlockQuote);
  require(bq != nullptr, QStringLiteral("fixture should parse a blockquote"));
  // The trailing `> > ` line is a zero-width paragraph (VEP) inside the quote.
  const std::function<const MarkdownNode*(const MarkdownNode&)> findVep =
      [&](const MarkdownNode& n) -> const MarkdownNode* {
    if (n.type() == BlockType::Paragraph) {
      const SourceRange r = n.sourceRange();
      if (r.byteStart >= 0 && r.byteEnd == r.byteStart) {
        return &n;
      }
    }
    for (const auto& c : n.children()) {
      if (const MarkdownNode* f = findVep(*c)) {
        return f;
      }
    }
    return nullptr;
  };
  const MarkdownNode* vep = findVep(*bq);
  require(vep != nullptr, QStringLiteral("trailing empty line should parse as a VEP"));
  require(layout.block(vep->id()) != nullptr,
          QStringLiteral("trailing blockquote VEP must have a BlockLayout so the caret can land on the new line"));

  // And it adds visible height vs the same quote without the trailing line.
  DocumentSession bare;
  bare.setMarkdownText(QStringLiteral("> > Nested quote."), false);
  DocumentLayout bareLayout;
  bareLayout.rebuild(bare.document(), theme, 800.0);
  const qreal bareHeight = bareLayout.block(mutableBlockAt(bare.document(), 0)->id())->height();
  const qreal withVepHeight = layout.block(mutableBlockAt(session.document(), 0)->id())->height();
  require(withVepHeight > bareHeight + 5.0,
          QStringLiteral("trailing empty line must add visible height (%1 > %2)").arg(withVepHeight).arg(bareHeight));
}

// Pressing Enter at the end of an outer-quote paragraph that is FOLLOWED by more quote
// content (a nested quote) creates a VEP BETWEEN the paragraph and the nested quote — a
// NON-trailing VEP. Before the fix every blockquote VEP was omitted, so this mid-quote
// Enter line was invisible and the caret vanished. After the fix the VEP the caret is on
// renders, wherever it sits. (Separator VEPs with no caret stay omitted — no double-space.)
void testCaretOnMidBlockquoteVepRenders() {
  const RenderTheme theme = RenderTheme::github();
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("> Outer paragraph.\n>\n> \n>\n> > Nested.\n"), false);

  const MarkdownNode* outerBq = findFirstBlock(session.document().root(), BlockType::BlockQuote);
  require(outerBq != nullptr, QStringLiteral("expected an outer blockquote"));
  const std::function<const MarkdownNode*(const MarkdownNode&, const MarkdownNode*)> findMidVep =
      [&](const MarkdownNode& n, const MarkdownNode* bq) -> const MarkdownNode* {
    if (n.type() == BlockType::Paragraph && bq) {
      const SourceRange r = n.sourceRange();
      if (r.byteStart >= 0 && r.byteEnd == r.byteStart) {
        const auto& sibs = bq->children();
        if (sibs.empty() || sibs.back().get() != &n) {
          return &n;
        } // non-trailing
      }
    }
    const MarkdownNode* childBq = (n.type() == BlockType::BlockQuote) ? &n : bq;
    for (const auto& c : n.children()) {
      if (const MarkdownNode* f = findMidVep(*c, childBq)) {
        return f;
      }
    }
    return nullptr;
  };
  const MarkdownNode* vep = findMidVep(*outerBq, outerBq);
  require(vep != nullptr, QStringLiteral("parser should create a non-trailing VEP for the empty `> ` line"));

  // Caret elsewhere → the mid VEP stays omitted (renders no double-space).
  {
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800.0);
    require(layout.block(vep->id()) == nullptr,
            QStringLiteral("non-trailing blockquote VEP must stay omitted when the caret is elsewhere"));
  }
  // Caret ON the VEP → it renders (the new line is visible, the caret lands on it).
  {
    SelectionRange sel;
    sel.focus.blockId = outerBq->id();
    sel.focus.text.nodeId = vep->id();
    sel.focus.text.sourceOffset = vep->sourceRange().byteStart;
    sel.anchor = sel.focus;
    DocumentLayout layout;
    layout.rebuild(session.document(), theme, 800.0, sel, QString(), DocumentLayout::BuildPolicy::Eager);
    require(layout.block(vep->id()) != nullptr,
            QStringLiteral("the caret's VEP (mid-quote Enter line) must render so the new line is visible"));
  }
}

// CSS `rem` is the ROOT em (the html element's font, 16px default), NOT the body em.
// A theme with `body { font-size: 1.5rem }` (→ 24px) must still resolve other `rem`
// values against the root (16): `h2 { font-size: 1.5rem }` = 24px, not 1.5×24 = 36px.
// Before the fix lengthToPx fell back to emPx (= bodyPx) for rem, inflating every rem
// size 1.5× in pixyll (and any theme with a large body font) — the "tall headings" bug.
void testRemResolvesAgainstRootNotBodyEm() {
  const QString css = QStringLiteral("#write { color:#000000; }"
                                     "body { font-size: 1.5rem; }" // bodyPx = 24
                                     "h2 { font-size: 1.5rem; }"); // h2 font must be 24 (root), not 36 (body)
  const RenderTheme theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("rem"), QString()));
  // Assert on the RESOLVED FONT SIZE, not the rendered block height. font-size is pure
  // CSS-unit arithmetic (1.5×16 = 24 vs the bug's 1.5×24 = 36), so it is identical on
  // every platform. A block's pixel HEIGHT, by contrast, depends on the offscreen font
  // metrics (a 24px font renders ~28px tall on Windows but ~36px on macOS), which made
  // a height threshold flake cross-platform. See offscreen-test-harness-broken-font-metrics.
  const ThemeElementStyle* h2 = theme.elementStyle(QStringLiteral("h2"));
  require(
      h2 != nullptr && h2->text.fontSizePx > 0.0,
      QStringLiteral("h2 should resolve an element style (fontSizePx=%1)").arg(h2 ? h2->text.fontSizePx : qreal(-1)));
  require(
      qAbs(h2->text.fontSizePx - 24.0) < 1.0,
      QStringLiteral("h2 1.5rem must resolve root-relative (font-size=%1px, expected 24; the body-em bug gave 36px)")
          .arg(h2->text.fontSizePx));
}

// `#write pre.md-meta-block` is Typora's FRONT-MATTER block. Community themes stuff
// editor-only hacks into it (pixyll: `margin-top:-2010px; padding-top:2000px`) to
// position the meta strip in Typora's editor. Those must NOT leak into real code-fence
// geometry — but `isCodeBlock` matched any `pre`, so the 2000px padding-top (and
// -2010px margin) was applied to every code fence (a one-line fence rendered 2036px
// tall, code pinned to the bottom). isCodeBlock now excludes .md-meta-block.
void testCodeFenceIgnoresMdMetaBlockHack() {
  const QString css =
      QStringLiteral("#write { color:#000000; }"
                     ".md-fences { padding: 8px; }"
                     "pre.md-meta-block { padding-top: 2000px; padding-bottom: 10px; margin-top: -2010px; }");
  const RenderTheme theme =
      RenderTheme::fromDefinition(CssThemeMapper::fromCss(css, QStringLiteral("meta"), QString()));
  require(theme.elementBoxStyle(QStringLiteral("pre")).padding.top() < 50.0,
          QStringLiteral("code-fence padding must ignore pre.md-meta-block hack (top=%1, should be ~8 not 2000)")
              .arg(theme.elementBoxStyle(QStringLiteral("pre")).padding.top()));
}

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testIncrementalBlockRebuildContract);
  RUN_TEST(testIncrementalTopLevelRangeRebuildContract);
  RUN_TEST(testRetainedSuffixKeepsCurrentSourceCoordinates);
  RUN_TEST(testLayoutPositionIndexPreservesLazySuffixAcrossSplice);
  RUN_TEST(testExactCompactCssSemanticSignatures);
  RUN_TEST(testThemeRebuildReusesNavigationWithoutStaleGeometry);
  RUN_TEST(testRangeRebuildAfterLazySuffixShiftMatchesFullLayout);
  RUN_TEST(testLazyPromoteMatchesEagerGap);
  RUN_TEST(testTrailingEmptyNestedBlockquoteLineRenders);
  RUN_TEST(testCaretOnMidBlockquoteVepRenders);
  RUN_TEST(testRemResolvesAgainstRootNotBodyEm);
  RUN_TEST(testCodeFenceIgnoresMdMetaBlockHack);
#undef RUN_TEST
  return 0;
}
