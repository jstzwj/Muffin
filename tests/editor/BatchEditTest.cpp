#include "document/DocumentSession.h"
#include "editor/DocumentSearch.h"
#include "editor/EditorController.h"
#include "editor/EditorView.h"
#include "editor/VirtualSourceEdit.h"
#include "render/DocumentLayout.h"
#include "render/BlockLayout.h"
#include "theme/CssThemeMapper.h"
#include "diagnostics/ThemePerformance.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <cstdlib>
#include <cstdio>

using namespace muffin;
namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::fprintf(stderr, "%s\n", message);
    qCritical() << message;
    std::exit(1);
  }
}
template <class Predicate> void wait(Predicate done) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < 120000) {
    QApplication::processEvents();
    QThread::msleep(1);
  }
  require(done(), "batch operation timed out");
}

void sameTree(const MarkdownNode& actual, const MarkdownNode& expected) {
  const auto a = actual.sourceRange(), b = expected.sourceRange();
  require(actual.type() == expected.type() && actual.literal() == expected.literal() &&
              actual.children().size() == expected.children().size() && a.byteStart == b.byteStart &&
              a.byteEnd == b.byteEnd && a.lineStart == b.lineStart && a.lineEnd == b.lineEnd,
          "batch AST and source mapping must match a complete parse");
  for (size_t i = 0; i < actual.children().size(); ++i)
    sameTree(*actual.children()[i], *expected.children()[i]);
}

void slicedLayoutParity() {
  DocumentSession session;
  session.setMarkdownText(
      "<div align=\"center\">\n\n# One\n\ntext **bold**\n\n</div>\n\n# Two\n\n- first\n  - nested\n\n| A | B |\n| - | - |\n| cell | text |\n",
      false);
  for (const auto& rootCss : {QString(), QString("#write { display:grid; grid-template-columns:1fr 1fr; }")}) {
    auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
        "#write {counter-reset:heading;} h1:before {counter-increment:heading; content:counter(heading) '. ';}" +
            rootCss,
        "batch-parity", {}));
    auto slicedTheme = theme.detachedPrototype();
    DocumentLayout synchronous, sliced;
    synchronous.rebuild(session.document(), theme, 900);
    auto task = sliced.rebuildSteps(session.document(), slicedTheme, 900, {}, {}, DocumentLayout::BuildPolicy::Eager);
    int checkpoints = 0;
    while (task.resumeFor(0))
      ++checkpoints;
    require(checkpoints > 10 && qAbs(synchronous.totalHeight() - sliced.totalHeight()) < 0.01,
            "yielding must preserve CSS layout geometry");
    for (const auto& node : session.document().root().children()) {
      const auto* a = synchronous.block(node->id());
      const auto* b = sliced.block(node->id());
      require(bool(a) == bool(b), "yielding must preserve visible HTML boundaries");
      if (a)
        require(a->rect() == b->rect() && a->headingBeforeText() == b->headingBeforeText(),
                "yielding must preserve boxes and CSS counter state");
    }
  }
}

void slicedHtmlPromotionParity() {
  DocumentSession session;
  session.setMarkdownText("<div align=\"center\">\n\n# Heading\n\nparagraph **bold**\n\n"
                          "<section>\n\n> quote\n>\n> second paragraph\n\n- one\n- two\n\n</section>\n\n</div>\n",
                          false);
  auto theme = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "div::before {content:'Container'; display:block;} section::after {content:'End'; display:block;}",
      "HTML promotion", {}));
  auto slicedTheme = theme.detachedPrototype();
  DocumentLayout eager, sliced;
  eager.rebuild(session.document(), theme, 800);
  sliced.rebuild(session.document(), slicedTheme, 800, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  auto task = sliced.ensureBuiltSteps(0, sliced.slotCount() - 1, slicedTheme);
  int checkpoints = 0;
  while (task.resumeFor(0))
    ++checkpoints;
  require(checkpoints > 5, "HTML promotion yields inside containers, including nested Markdown blocks");
  for (const auto& node : session.document().root().children()) {
    const auto* a = eager.block(node->id());
    const auto* b = sliced.block(node->id());
    require(bool(a) == bool(b) &&
                (!a || (a->rect() == b->rect() && a->positionedPseudoRects() == b->positionedPseudoRects())),
            "cooperative HTML promotion preserves synchronous layout geometry");
  }
  require(qAbs(eager.totalHeight() - sliced.totalHeight()) < 0.01,
          "cooperative HTML promotion preserves the document height");
  auto cancelledTheme = theme.detachedPrototype();
  DocumentLayout cancelled;
  cancelled.rebuild(session.document(), cancelledTheme, 800, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  auto partial = cancelled.ensureBuiltSteps(0, cancelled.slotCount() - 1, cancelledTheme);
  for (int i = 0; i < 8; ++i)
    require(partial.resumeFor(0), "cancel a nested HTML promotion before completion");
  partial = {};
  int releaseSlices = 0;
  while (cancelled.releaseDetails(0))
    ++releaseSlices;
  require(releaseSlices > 2, "cancelled containers release their materialized children in bounded slices");
}

void interruptedBuildsDoNotPublishCachesOrKeepPerfScopes() {
  DocumentSession session;
  session.setMarkdownText("# First\n\n# Second\n\n# Third\n", false);
  const auto prototype = RenderTheme::fromDefinition(CssThemeMapper::fromCss(
      "#write {counter-reset:n;} h1:before {counter-increment:n; content:counter(n) '. ';}", "interrupted", {}));
  auto reference = prototype.detachedPrototype();
  const auto expected = reference.resolveDocumentCounters(session.document());
  require(!expected.isEmpty(), "counter cancellation fixture has generated content");
  auto interrupted = prototype.detachedPrototype();
  auto counters = interrupted.resolveDocumentCounterSteps(session.document());
  require(counters.resumeFor(0), "counter preparation suspends before publishing");
  counters = {};
  require(interrupted.resolveDocumentCounters(session.document()) == expected,
          "retrying an abandoned counter build cannot reuse a partial cache");

  enableThemePerformance(true);
  resetThemePerformance();
  auto firstTheme = prototype.detachedPrototype(), secondTheme = prototype.detachedPrototype();
  DocumentLayout first, second;
  auto a = first.rebuildSteps(session.document(), firstTheme, 800, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  auto b = second.rebuildSteps(session.document(), secondTheme, 800, {}, {}, DocumentLayout::BuildPolicy::Lazy);
  require(a.resumeFor(0) && b.resumeFor(0), "two views can suspend independently");
  // In debug builds reset asserts that no stack-based scope survived a yield.
  resetThemePerformance();
  a = {};
  while (b.resumeFor(0)) {
  }
  {
    ThemePerfScope later(ThemePerfStage::Paint);
  }
  for (const auto& sample : themePerformanceSamples())
    require(sample.exclusiveNs >= 0, "interleaved builds preserve performance-scope nesting");
  enableThemePerformance(false);
  require(secondTheme.resolveDocumentCounters(session.document()) == expected,
          "interleaving and cancelling another view cannot change generated counter text");
}

void literalEditorsFollowMappedSelectionAfterReplacement() {
  DocumentSession session;
  EditorController editor;
  editor.attach(&session, nullptr);
  const QString before = "```\nfirst\n```\n\n```\nsecond\n```\n";
  const QString inserted = "```\nnew\n```\n\n";
  session.setMarkdownText(before, false);
  const auto offset = before.indexOf("second") + 3;
  require(editor.selectSourceRange(offset, offset) && editor.codeFenceController().enterEditMode(),
          "fixture enters the second code fence");
  editor.selectSourceRange(offset, offset);
  session.applyBatchAsync(PieceTable(inserted + before), "insert code fence", offset, offset, offset + inserted.size(),
                          offset + inserted.size());
  wait([&] { return !session.isBatchPending(); });
  require(editor.codeFenceController().currentCodeFenceId() == editor.selection().cursorPosition().blockId,
          "an active literal editor follows the mapped block rather than its previous type index");
  require(editor.inputController().insertText("X"), "typing remains available after batch replacement");
  require(session.markdownText().toString() == QString(inserted + before).replace("second", "secXond"),
          "the first keystroke after replacement edits the mapped code fence");
  editor.undo();
  require(session.markdownText().toString() == inserted + before, "undo restores the post-batch source");
  editor.undo();
  wait([&] { return !session.isBatchPending(); });
  require(session.markdownText().toString() == before && editor.codeFenceController().isEditing() &&
              editor.codeFenceController().currentCodeFenceId() == editor.selection().cursorPosition().blockId,
          "batch undo rebinds the active literal editor to the original source generation");
  require(editor.inputController().insertText("Y") &&
              session.markdownText().toString() == QString(before).replace("second", "secYond"),
          "typing after batch undo uses the restored caret and code fence");
}
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  slicedLayoutParity();
  slicedHtmlPromotionParity();
  interruptedBuildsDoNotPublishCachesOrKeepPerfScopes();
  literalEditorsFollowMappedSelectionAfterReplacement();
  DocumentSession session;
  EditorView view;
  EditorController editor;
  editor.attach(&session, &view);
  view.resize(900, 700);
  QObject::connect(&view, &EditorView::layoutBusyChanged, &session, &DocumentSession::setPresentationBusy);
  QObject::connect(&session, &DocumentSession::parsed, &view, [&] {
    if (!session.lastParseWasLocalEdit() || session.lastChangeWasBatch())
      view.setDocument(session.document(), {}, true);
  });
  const int repetitions = qMax(10000, qEnvironmentVariableIntValue("MUFFIN_BATCH_TEST_REPEATS"));
  const QString before =
      QStringLiteral("## Heading\n\n中文 alpha **bold** and [link](x.md).\n\n").repeated(repetitions);
  const QString after = QString(before).replace("alpha", "replacement");
  session.setMarkdownText(before, false);
  wait([&] { return !view.isLayoutPending(); });
  // The production search worker builds the target's text indices before GUI
  // handoff. Prepare the same immutable inputs outside the heartbeat window;
  // constructing a new PieceTable on the GUI thread would measure test setup.
  const PieceTable beforeSnapshot = session.markdownText(), afterSnapshot(after);
  int beats = 0;
  qint64 longestGap = 0;
  QElapsedTimer clock;
  clock.start();
  const char* phase = "replace";
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] {
    const auto gap = clock.restart();
    longestGap = qMax(longestGap, gap);
    if (gap >= 500)
      std::fprintf(stdout, "batch slow heartbeat phase=%s; gap=%lld ms\n", phase, static_cast<long long>(gap));
    ++beats;
  });
  heartbeat.start(5);
  const auto revision = session.document().revision();
  QElapsedTimer operation;
  operation.start();
  session.applyBatchAsync(afterSnapshot, "Replace All", 3, 30, 3, 36);
  require(session.document().revision() == revision && !session.undoStack().canUndo(),
          "preparing a batch must not publish partial content or history");
  wait([&] { return !session.isBatchPending() && !view.isLayoutPending(); });
  std::fprintf(stdout, "batch input=%lld UTF-16 characters; replace+layout=%lld ms\n",
               static_cast<long long>(before.size()), static_cast<long long>(operation.elapsed()));
  std::fflush(stdout);
  require(session.markdownText().toString() == after, "batch installs prepared text");
  require(session.document().isModified() && beats > 3, "batch preparation and layout yield to the event loop");
  require(view.residentLayoutCount() < 200, "cooperative editor layout remains lazy even under offscreen tests");
  phase = "undo";
  editor.undo();
  require(!editor.canUndo() && !editor.canRedo(), "history replay is serialized during background preparation");
  require(session.markdownText().toString() == after, "undo does not partially mutate the live document");
  wait([&] { return !session.isBatchPending() && !view.isLayoutPending(); });
  require(session.markdownText().toString() == before && !session.document().isModified(),
          "async undo restores save point");
  phase = "redo";
  editor.redo();
  wait([&] { return !session.isBatchPending() && !view.isLayoutPending(); });
  require(session.markdownText().toString() == after && session.document().isModified(), "async redo restores batch");
  phase = "cancel";
  session.applyBatchAsync(beforeSnapshot, "stale", 0, 0, 0, 0);
  operation.restart();
  require(session.applyTextDelta(0, 0, "new edit\n\n", true), "live editing remains available during preparation");
  std::fprintf(stdout, "batch cancellation by editing=%lld ms\n", static_cast<long long>(operation.elapsed()));
  std::fflush(stdout);
  wait([&] { return !session.isBatchPending(); });
  require(session.markdownText().mid(0, 8) == "new edit", "pending batch cannot overwrite newer edits");
  session.applyBatchAsync(beforeSnapshot, "locked", 0, 0, 0, 0);
  session.setEditingReadOnly(true);
  require(!session.isBatchPending(), "locking cancels pending editing");
  session.setEditingReadOnly(false);
  auto* closing = new DocumentSession;
  closing->applyBatchAsync(afterSnapshot, "closed", 0, 0, 0, 0);
  delete closing;
  QApplication::processEvents();
  heartbeat.stop();
  std::fprintf(stdout, "batch heartbeat count=%d; longest gap=%lld ms\n", beats, static_cast<long long>(longestGap));
  require(longestGap < 1000, "a batch must not block the GUI for a second");

  DocumentSession sparse, reference;
  VirtualSourceEdit source;
  source.bindSession(&sparse);
  const auto sparseBefore = QStringLiteral("# Stable\n\nuntouched\n\n") +
                            QStringLiteral("paragraph\n\n").repeated(20000) +
                            QStringLiteral("target phrase\n\nlast paragraph\n");
  auto sparseAfter = sparseBefore;
  sparseAfter.replace("target", "changed");
  sparse.setMarkdownText(sparseBefore, false);
  const auto stableId = sparse.document().root().children().front()->id();
  sparse.applyBatchAsync(PieceTable(sparseAfter), "sparse", 20, 3, 21, 3);
  wait([&] { return !sparse.isBatchPending(); });
  require(sparse.lastParseWasLocalEdit() && sparse.document().root().children().front()->id() == stableId,
          "a small independent window must preserve the rest of the AST");
  require(source.anchorPosition() == 21 && source.cursorPosition() == 3,
          "bound source editors restore directed selections without a MainWindow");
  reference.setMarkdownText(sparseAfter, false);
  sameTree(sparse.document().root(), reference.document().root());
  EditorController history;
  history.attach(&sparse, nullptr);
  history.undo();
  wait([&] { return !sparse.isBatchPending(); });
  require(sparse.markdownText().toString() == sparseBefore && !sparse.document().isModified() &&
              source.anchorPosition() == 20 && source.cursorPosition() == 3,
          "sparse replay restores text, directed selection and save point");
  const auto state = sparse.undoStack().stateId(), revisionBeforeNoop = sparse.document().revision();
  sparse.applyBatchAsync(sparse.markdownText(), "no-op", 0, 0, 0, 0);
  wait([&] { return !sparse.isBatchPending(); });
  require(sparse.undoStack().stateId() == state && sparse.document().revision() == revisionBeforeNoop,
          "identical replacement cannot create history or discard redo");
  for (const auto& text :
       {QString("[label][ref]\n\n[ref]: old.md\n"), QString("<div class='old'>\n\ntext\n\n</div>\n")}) {
    sparse.setMarkdownText(text, false);
    auto changed = text;
    changed.replace("old", "new");
    sparse.applyBatchAsync(PieceTable(changed), "context", 0, 0, 0, 0);
    wait([&] { return !sparse.isBatchPending(); });
    require(!sparse.lastParseWasLocalEdit(), "document-wide semantic dependencies require complete parsing");
    reference.setMarkdownText(changed, false);
    sameTree(sparse.document().root(), reference.document().root());
  }
  DocumentSession reentrant;
  int stalePublications = 0;
  QObject::connect(&reentrant.document(), &MarkdownDocument::documentReset, &reentrant, [&] {
    if (reentrant.lastChangeWasBatch())
      reentrant.setMarkdownText("newer reload\n", false);
  });
  QObject::connect(&reentrant, &DocumentSession::documentTextChanged, &reentrant, [&](const QString& text) {
    if (text == "outdated batch\n")
      ++stalePublications;
  });
  reentrant.applyBatchAsync(PieceTable("outdated batch\n"), "reentrant", 0, 0, 0, 0);
  wait([&] { return !reentrant.isBatchPending(); });
  require(reentrant.markdownText().toString() == "newer reload\n" && stalePublications == 0 &&
              !reentrant.undoStack().canUndo(),
          "reentrant observers cannot publish stale text or history");

  session.setMarkdownText("# Latest document\n\nlatest text\n", false);
  require(view.isLayoutPending(), "new document begins an unpublished layout");
  view.resize(740, 520);
  view.setTheme(RenderTheme::night());
  session.setMarkdownText("# Final document\n\nfinal text\n", false);
  wait([&] { return !view.isLayoutPending(); });
  require(session.isEditingAllowed() && view.nodeRect(session.document().root().children().front()->id()).isValid(),
          "resize, theme change and reload supersede pending layout without stranding editing");
  UndoStack budgeted;
  auto retained = std::make_shared<BatchTextCommand>();
  retained->before = retained->after = PieceTable(QString(3 * 1024 * 1024, u'x'));
  for (int i = 0; i < 70; ++i)
    budgeted.push(EditTransaction("budget", retained));
  require(budgeted.retainedTextBytes() <= 256 * 1024 * 1024, "batch history obeys its retained-text budget");
  int undoCount = 0;
  while (budgeted.canUndo()) {
    budgeted.takeUndo();
    ++undoCount;
  }
  require(undoCount > 1 && undoCount < 70 && !budgeted.isClean(),
          "pruning older snapshots cannot invent a reachable save point");
  return 0;
}
