// Regression test for the "third dash freezes the editor" bug.
//
// Typing the 3rd dash turns paragraph "--" into a thematic break (---). A thematic break hosts no
// inline text, so the post-edit caret used to be unresolvable: cursorAfterEdit failed, the caret was
// cleared, and (because the caret was invalid) applyLocalEdit took the snapshot-undo path whose
// collectPendingMarkerOffsets walks every paragraph — plus an O(document) selection-clear chain.
// On a 112k-block document this was ~49 s per keystroke. The fix lands the caret block-after the
// non-text block so it stays resolvable, collapsing all three O(document) phases back to the normal
// TextDeltaCommand typing path.
//
// This test types three dashes into an empty paragraph of a large document and asserts the 3rd dash
// behaves like normal typing:
//   1. It must NOT trigger collectPendingMarkerOffsets (the snapshot-undo smoking gun — fires iff the
//      caret was invalid). Binary and size/machine independent.
//   2. Its sync time must be within a small multiple of the normal-typing baseline (dashes 1 & 2).
//      Pre-fix this ratio was ~80×; post-fix ~1-3×.
//
//   cmake --build --preset conan-release --target MuffinThirdDashProbe
//   ctest --preset conan-release -R ThirdDashProbe --output-on-failure
//   MUFFIN_BENCH_SIZE_MB=8 MuffinThirdDashProbe.exe   # manual diagnostic dump of the 3rd-dash probes
#include "../editor/EditorTestUtils.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>

namespace {

QMutex g_perfMutex;
QStringList g_perfLines;

void perfCapture(QtMsgType, const QMessageLogContext& ctx, const QString& msg) {
  if (QString::fromUtf8(ctx.category) != QLatin1String("muffin.perf")) {
    return;
  }
  QMutexLocker locker(&g_perfMutex);
  g_perfLines.append(msg);
}

muffin::MarkdownNode* findEmptyParagraph(muffin::MarkdownNode* node) {
  if (!node) {
    return nullptr;
  }
  if (node->type() == muffin::BlockType::Paragraph) {
    const muffin::SourceRange r = node->sourceRange();
    if (r.byteStart == r.byteEnd) {
      return node;
    }
  }
  muffin::MarkdownNode* found = nullptr;
  for (const auto& c : node->children()) {
    if (muffin::MarkdownNode* f = findEmptyParagraph(c.get())) {
      found = f;
    }
  }
  return found;
}

// Big doc with a guaranteed empty paragraph: the two blank lines after the first block yield a VEP
// (virtual empty paragraph) — a real, typeable paragraph node, the exact caret landing the user
// starts from. Every 5th block is a nested blockquote+list so the total node count matches a real
// document (the cursor-walk cost scales with total nodes, not just top-level blocks).
QString makeBigDoc(qsizetype targetBytes) {
  QString doc;
  doc.reserve(targetBytes + 256);
  doc += QStringLiteral("Start paragraph one with some words here.\n\n\n");
  static const QString unit = QStringLiteral(
      "Filler paragraph with **bold** and _em_ and `code` and [link](http://example.com/x) text.\n\n");
  static const QString nested = QStringLiteral(
      "> Blockquote line one with **bold** and `code` inline.\n>\n> - nested list item alpha\n"
      "> - nested list item beta with [link](http://example.com/y)\n>\n> more quote text here.\n\n");
  int i = 0;
  while (doc.size() < targetBytes) {
    doc += (i++ % 5 == 0) ? nested : unit;
  }
  return doc;
}

struct KeystrokeResult {
  double syncMs = 0.0;
  bool firedCollectPendingMarkerOffsets = false;
  double cursorAfterEditMs = 0.0;  // the O(total-nodes) tree-walk signal
};

// Extract the millisecond value from a captured probe line "<label> <ms> ms", or -1 if absent.
double probeMs(const QStringList& lines, const char* label) {
  const QString prefix = QString::fromLatin1(label) + QLatin1Char(' ');
  for (const QString& line : lines) {
    if (line.startsWith(prefix)) {
      const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
      if (parts.size() >= 2) {
        bool ok = false;
        const double v = parts.at(1).toDouble(&ok);
        if (ok) {
          return v;
        }
      }
    }
  }
  return -1.0;
}

// Send one keypress; capture the synchronous applyLocalEdit wall time, whether the snapshot-undo
// path (collectPendingMarkerOffsets) fired, and the cursorAfterEdit cost (the tree-walk signal).
KeystrokeResult pressAndCapture(QObject* target, int key, const QString& text) {
  g_perfLines.clear();
  QElapsedTimer wall;
  wall.start();
  QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
  QApplication::sendEvent(target, &event);
  const double syncMs = wall.nsecsElapsed() / 1000000.0;
  // Flush the queued structural refresh between keystrokes, the way the real event loop does.
  QCoreApplication::processEvents();

  QMutexLocker locker(&g_perfMutex);
  const QStringList snapshot = g_perfLines;
  locker.unlock();

  KeystrokeResult result;
  result.syncMs = syncMs;
  result.cursorAfterEditMs = probeMs(snapshot, "input.applyLocalEdit.cursorAfterEdit");
  for (const QString& line : snapshot) {
    if (line.startsWith(QLatin1String("input.collectPendingMarkerOffsets"))) {
      result.firedCollectPendingMarkerOffsets = true;
      break;
    }
  }
  return result;
}

KeystrokeResult typeAndCapture(QObject* target, QChar ch) {
  return pressAndCapture(target, static_cast<int>(ch.unicode()), QString(ch));
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QApplication app(argc, argv);

  const bool diagnostic = !qgetenv("MUFFIN_PROBE_DUMP").isEmpty();
  QLoggingCategory::setFilterRules(QStringLiteral("muffin.perf.debug=true"));
  qInstallMessageHandler(perfCapture);

  const QByteArray sizeEnv = qgetenv("MUFFIN_BENCH_SIZE_MB");
  const qsizetype sizeMb = sizeEnv.isEmpty() ? 2 : sizeEnv.toLongLong();

  muffin::DocumentSession session;
  muffin::EditorView view;
  muffin::EditorController controller;
  controller.attach(&session, &view);

  const QString doc = makeBigDoc(sizeMb * 1024 * 1024);
  session.setMarkdownText(doc, false);
  view.setDocument(session.document());
  view.resize(800, 480);
  view.show();
  view.setFocus();
  QCoreApplication::processEvents();

  const qsizetype topLevel = session.document().root().children().size();
  muffin::MarkdownNode* empty = findEmptyParagraph(&session.document().root());
  std::fprintf(stdout, "doc: %lld bytes, %lld top-level blocks; empty-paragraph VEP %s\n",
               static_cast<long long>(doc.size()), static_cast<long long>(topLevel),
               empty ? "found" : "NOT FOUND");
  if (!empty) {
    std::fprintf(stdout, "FAIL: no empty paragraph to type into — adjust makeBigDoc.\n");
    return 1;
  }

  setCursor(controller.selection(), empty, 0);

  // Three dashes: 1 & 2 stay paragraphs (normal typing baseline); 3 turns "--" into a thematic break.
  const KeystrokeResult dashes[3] = {
      typeAndCapture(&view, QLatin1Char('-')),
      typeAndCapture(&view, QLatin1Char('-')),
      typeAndCapture(&view, QLatin1Char('-')),
  };
  const bool madeThematicBreak = session.markdownText().toString().contains(QStringLiteral("---"));
  // Enter after the rule (caret is block-after) → insertBlockAfterCurrentBlock (count=0, inserts
  // "\n\n"), the path that froze ~50 s pre-fix: it passes preferLaterEmptyAtOffset=true, so the
  // unpruned nodeAtContentSourceOffset walked the whole tree without early-return.
  const KeystrokeResult enterAfterRule = pressAndCapture(&view, Qt::Key_Return, QStringLiteral("\n"));

  // === reproduce the user's real scenario: Enter repeatedly right after the rule ===
  // The single Enter above passes the regression, but pressing Enter 2-3 more times froze the
  // editor and an empty line appeared then vanished on the next Enter. Dump caret zone + source
  // after each Enter to see whether the caret ever leaves BlockAfter and whether blanks collapse.
  auto dumpEnter = [&](int idx, const KeystrokeResult& r) {
    const muffin::CursorPosition c = controller.selection().cursorPosition();
    const muffin::HitTestResult hit = controller.selection().currentHit();
    const QString src = session.markdownText().toString();
    const int rulePos = src.indexOf(QStringLiteral("---"));
    const int from = qMax(0, rulePos - 2);
    const int len = static_cast<int>(qMin<qsizetype>(60, src.size() - from));
    QString snippet = src.mid(from, len);
    snippet.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    const char* markers = r.firedCollectPendingMarkerOffsets ? "YES" : "no";
    const char* zone = hit.zone == muffin::HitTestResult::Zone::BlockAfter ? "BlockAfter" : "other";
    const int afterBlock = c.afterBlock ? 1 : 0;
    const long long off = static_cast<long long>(c.text.sourceOffset);
    std::fprintf(stdout,
        "enter+%d: sync=%7.2f cursorAE=%7.2f markers=%s | afterBlock=%d blockId=%s off=%lld zone=%s\n",
        idx, r.syncMs, r.cursorAfterEditMs, markers, afterBlock,
        c.blockId.toString().toUtf8().constData(), off, zone);
    std::fprintf(stdout, "         src@rule: [%s]\n", snippet.toUtf8().constData());
  };
  if (diagnostic) {
    dumpEnter(1, enterAfterRule);
  }
  for (int i = 2; i <= 4; ++i) {
    KeystrokeResult r = pressAndCapture(&view, Qt::Key_Return, QStringLiteral("\n"));
    if (diagnostic) {
      dumpEnter(i, r);
    }
  }

  const double baseline = std::max(dashes[0].syncMs, dashes[1].syncMs);
  const double ratio = baseline > 0.0 ? dashes[2].syncMs / baseline : 0.0;
  const double walkBaseline = std::max(dashes[0].cursorAfterEditMs, std::max(dashes[1].cursorAfterEditMs, 1.0));

  std::fprintf(stdout, "dash1=%.2f ms  dash2=%.2f ms  dash3(thematic-break)=%.2f ms  (ratio=%.1fx baseline)\n",
               dashes[0].syncMs, dashes[1].syncMs, dashes[2].syncMs, ratio);
  std::fprintf(stdout, "cursorAfterEdit (tree-walk signal): dash1=%.2f dash2=%.2f dash3=%.2f ms\n",
               dashes[0].cursorAfterEditMs, dashes[1].cursorAfterEditMs, dashes[2].cursorAfterEditMs);
  std::fprintf(stdout, "collectPendingMarkerOffsets fired on dash3: %s  (snapshot-undo path = caret was invalid)\n",
               dashes[2].firedCollectPendingMarkerOffsets ? "YES" : "no");
  std::fprintf(stdout, "enter-after-rule: sync=%.2f ms  cursorAfterEdit=%.2f ms  (insertBlockAfterCurrentBlock path)\n",
               enterAfterRule.syncMs, enterAfterRule.cursorAfterEditMs);
  std::fprintf(stdout, "source now contains a thematic break: %s\n", madeThematicBreak ? "YES" : "no");

  if (diagnostic) {
    std::fprintf(stdout, "\n=== dash-3 probes ===\n");
    QMutexLocker locker(&g_perfMutex);
    for (const QString& line : g_perfLines) {
      std::fprintf(stdout, "  muffin.perf: %s\n", line.toUtf8().constData());
    }
  }

  bool ok = true;
  if (!madeThematicBreak) {
    std::fprintf(stdout, "FAIL: the 3rd dash did not produce a thematic break — scenario not exercised.\n");
    ok = false;
  }
  if (dashes[2].firedCollectPendingMarkerOffsets) {
    std::fprintf(stdout,
                 "FAIL: 3rd dash took the snapshot-undo path (caret was unresolvable). "
                 "Expected the block-after caret to keep it on the TextDeltaCommand path.\n");
    ok = false;
  }
  // The tree-walk signal: resolving an offset on the thematic break must NOT recurse the whole
  // document. Pre-fix cursorForSourceOffset walked every node when the offset matched no text block
  // — ~250ms here at 2MB, ~24s on a 112k-block doc (3 calls/keystroke = the 47s freeze).
  constexpr double kMaxCursorAfterEdit = 100.0;
  if (dashes[2].cursorAfterEditMs > kMaxCursorAfterEdit ||
      dashes[2].cursorAfterEditMs > 10.0 * walkBaseline) {
    std::fprintf(stdout,
                 "FAIL: 3rd dash cursorAfterEdit=%.2f ms — cursorForSourceOffset is still walking the "
                 "whole tree to resolve an offset on the thematic break.\n",
                 dashes[2].cursorAfterEditMs);
    ok = false;
  }
  constexpr double kMaxRatio = 20.0;
  if (baseline > 0.0 && ratio > kMaxRatio) {
    std::fprintf(stdout,
                 "FAIL: 3rd dash is %.1fx normal typing (cap %.0fx) — still pathological.\n",
                 ratio, kMaxRatio);
    ok = false;
  }
  // The insertBlockAfterCurrentBlock path (Enter after the rule): preferLaterEmptyAtOffset=true used
  // to make nodeAtContentSourceOffset walk the whole tree without early-return (~50s freeze). The
  // source-range prune must keep this on the normal-typing path too.
  if (enterAfterRule.cursorAfterEditMs > kMaxCursorAfterEdit ||
      enterAfterRule.syncMs > kMaxRatio * baseline) {
    std::fprintf(stdout,
                 "FAIL: enter-after-rule cursorAfterEdit=%.2f ms sync=%.2f ms — the "
                 "preferLaterEmptyAtOffset walk is still O(document).\n",
                 enterAfterRule.cursorAfterEditMs, enterAfterRule.syncMs);
    ok = false;
  }
  // insertBlockAfterCurrentBlock used to pass structureEdit=true, forcing the O(document) snapshot
  // undo (full-doc toString ×3 + collectPendingMarkerOffsets ×2). It now uses the cheap
  // TextDeltaCommand path, so the marker scan must not fire.
  if (enterAfterRule.firedCollectPendingMarkerOffsets) {
    std::fprintf(stdout,
                 "FAIL: enter-after-rule fired collectPendingMarkerOffsets — still on the "
                 "snapshot-undo path (structureEdit should be false for a plain text insert).\n");
    ok = false;
  }

  std::fprintf(stdout, "%s\n", ok ? "PASS: 3rd dash is back to normal-typing cost." : "FAIL");
  std::fflush(stdout);
  return ok ? 0 : 1;
}
