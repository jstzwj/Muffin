// Regression test for the "content jumps up after Enter near a `---` rule" collapse.
//
// History: the caret-driven VEP synthesized after a thematic break was unstable — its count
// desynced on the isolated-slice re-parse (cmark over-reports the rule's lineEnd, absorbing the
// trailing blanks), so Enter made the top-level block count DROP (oldCount>newCount) and the layout
// reflowed up. The redesign removes that VEP entirely (the rule's afterBlock caret serves the
// gap), so there is no node whose count can desync. This probe rests the caret afterBlock on the
// rule and presses Enter repeatedly; the top-level block count must NEVER drop (a drop = the
// collapse class regressed, in whatever form).
//
//   cmake --build . --config Release --target MuffinSplitAfterRuleProbe
//   ctest -C Release -R SplitAfterRuleProbe --output-on-failure  (or copy exe next to Qt DLLs)
#include "../editor/EditorTestUtils.h"

#include <QApplication>
#include <QKeyEvent>
#include <QString>

#include <cstdio>

namespace {

muffin::MarkdownNode* firstThematicBreak(muffin::MarkdownNode& root) {
  for (const auto& c : root.children()) {
    if (c->type() == muffin::BlockType::ThematicBreak) {
      return c.get();
    }
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);

  // rule + 3 blank lines + a paragraph — the gap the old VEP inhabited, now served by afterBlock.
  const QString doc = QStringLiteral("Intro\n\n---\n\n\n\nAfter\n\nEnd\n");
  muffin::DocumentSession session;
  muffin::EditorView view;
  muffin::EditorController controller;
  controller.attach(&session, &view);
  session.setMarkdownText(doc, false);
  view.setDocument(session.document());
  view.resize(800, 600);
  view.show();
  view.setFocus();
  QCoreApplication::processEvents();

  muffin::MarkdownNode* hr = firstThematicBreak(session.document().root());
  require(hr != nullptr, "document should contain a thematic break");
  muffin::HitTestResult hit;
  hit.zone = muffin::HitTestResult::Zone::BlockAfter;
  hit.blockId = hr->id();
  hit.textNodeId = hr->id();
  controller.activateHit(hit);
  require(controller.selection().cursorPosition().afterBlock, "caret should rest afterBlock on the rule");

  qsizetype prevCount = session.document().root().children().size();
  std::fprintf(stdout, "baseline: %lld blocks; caret afterBlock on rule\n", static_cast<long long>(prevCount));
  bool collapsed = false;
  for (int i = 1; i <= 6; ++i) {
    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\n"));
    QApplication::sendEvent(&view, &ev);
    QCoreApplication::processEvents();
    const qsizetype count = session.document().root().children().size();
    const qsizetype delta = count - prevCount;
    std::fprintf(stdout, "Enter %d: blocks=%lld (d%+lld)%s\n", i, static_cast<long long>(count),
                 static_cast<long long>(delta), delta < 0 ? "  <<< COLLAPSE" : "");
    if (delta < 0) {
      collapsed = true;
    }
    prevCount = count;
  }

  std::fflush(stdout);
  if (collapsed) {
    std::fprintf(stdout, "FAIL: Enter near a rule collapsed top-level blocks (regression).\n");
    return 1;
  }
  std::fprintf(stdout, "PASS: Enter afterBlock on a rule never collapses blocks.\n");
  return 0;
}

