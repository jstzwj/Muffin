// Parser-level regression test for stable, correctly-counted virtual empty paragraphs around a
// thematic break.
//
// Two invariants must hold for the rule's gap to be a clean, caret-addressable target:
//
//   (a) The VEP count in the gap after a rule is the normal blankLines/2 — NOT force-suppressed to
//       zero. An earlier redesign suppressed VEPs after a rule entirely, which left Enter after a
//       rule with no renderable empty paragraph to land in. That suppression was reverted; this
//       test guards against it creeping back.
//
//   (b) cmark-gfm over-reports a ThematicBreak's end_line (it absorbs trailing blank lines), so
//       annotateSourceOffsets must clamp the rule's byteEnd to its own content line. Without the
//       clamp, byteEnd reaches into the blank gap and topLevelBlockAtOffset (inclusive, first-match-
//       wins) resolves every gap offset to the rule instead of the VEPs there — the post-Enter caret
//       lands on the wrong line. Combined with the endLine gap logic in insertVirtualEmptyParagraphs,
//       this keeps the VEP count stable across the isolated-slice re-parse (no "content jumps up").
//
//   cmake --build . --config Release --target MuffinThematicBreakRangeProbe
//   ctest -C Release -R ThematicBreakRangeProbe --output-on-failure
#include "../editor/EditorTestUtils.h"

#include <QApplication>
#include <QString>

#include <cstdio>

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);

  muffin::DocumentSession session;
  // `---` on line 3 with 5 trailing blank lines (lines 4..8) before "After" on line 9.
  // 5 blank lines ⇒ blankLines/2 = 2 virtual empty paragraphs after the rule.
  session.setMarkdownText(QStringLiteral("Intro\n\n---\n\n\n\n\n\nAfter\n\nEnd\n"), false);

  // Locate the rule.
  muffin::MarkdownNode* rule = nullptr;
  for (const auto& c : session.document().root().children()) {
    if (c->type() == muffin::BlockType::ThematicBreak) {
      rule = c.get();
      break;
    }
  }
  require(rule != nullptr, "expected a thematic break in the document");

  const muffin::SourceRange ruleRange = rule->sourceRange();

  // (a) Count the virtual empty paragraphs between the rule and the first real paragraph after it.
  //     5 blank lines ⇒ exactly 2 VEPs (the normal blankLines/2 rule, not suppressed).
  bool afterRule = false;
  int vepCount = 0;
  muffin::MarkdownNode* afterBlock = nullptr;
  for (const auto& c : session.document().root().children()) {
    if (c.get() == rule) {
      afterRule = true;
      continue;
    }
    if (!afterRule) continue;
    const muffin::SourceRange r = c->sourceRange();
    if (c->type() == muffin::BlockType::Paragraph && r.byteEnd > r.byteStart) {
      afterBlock = c.get();  // the first real (non-empty) paragraph after the rule = "After"
      break;
    }
    if (c->type() == muffin::BlockType::Paragraph && r.byteStart == r.byteEnd) {
      ++vepCount;
    }
  }
  require(afterBlock != nullptr, "expected a real paragraph after the rule");
  require(vepCount == 2, "5 blank lines after a rule must yield 2 virtual empty paragraphs (blankLines/2)");

  // (b) The rule's byteEnd is clamped to its content line: it ends strictly before the first VEP /
  //     the following block, never absorbing the trailing blank gap.
  require(ruleRange.byteEnd <= afterBlock->sourceRange().byteStart,
          "thematic break byteEnd must not reach into the trailing blank gap");

  // Leading blank gap: topLevelBlockAtOffset must resolve offsets in the blank region BEFORE all
  // content to the first child, not the historical last-block fallback (a source caret in the
  // leading blanks used to scroll/render the document END). Leading VEPs are zero-width
  // (byteStart == byteEnd), so the offsets BETWEEN them exercise the fallback path.
  muffin::DocumentSession leading;
  leading.setMarkdownText(QStringLiteral("\n\n\n\nFirst\n\nSecond\n"), false);
  const auto& leadingChildren = leading.document().root().children();
  require(leadingChildren.size() >= 4, "leading fixture should have VEPs plus two blocks");
  for (const qsizetype offset : {qsizetype(1), qsizetype(3)}) {
    muffin::MarkdownNode* resolved = leading.document().topLevelBlockAtOffset(offset);
    require(resolved == leadingChildren.front().get(),
            "offset in the leading blank region must resolve to the first child, not the last block");
  }

  std::fprintf(stdout,
               "PASS: rule bytes [%lld..%lld] (clamped); %d stable VEP%s after it (blankLines/2).\n",
               static_cast<long long>(ruleRange.byteStart), static_cast<long long>(ruleRange.byteEnd),
               vepCount, vepCount == 1 ? "" : "s");
  return 0;
}
