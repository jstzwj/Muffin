#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QThreadPool>

#include "../parser/ParserTestUtils.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"

using namespace muffin;

// openDocumentAsync runs a worker-owned parser on a QtConcurrent thread and finishes
// (setMarkdownText + relativize + emit parsed) on the GUI thread via QFutureWatcher. These tests
// spin an event loop (no worker block) to confirm the async open path delivers `parsed`, brackets
// it with `parseBusy`, and supersedes an in-flight parse when a newer open is requested.

namespace {

void testCommittedSourceChanges() {
  DocumentSession session;
  session.setMarkdownText("first paragraph\n\nsecond paragraph\n", false);
  QVector<SourceTextChange> changes;
  QObject::connect(&session, &DocumentSession::sourceTextChanged, &session,
                   [&](const SourceTextChange& change) { changes.append(change); });
  QObject::connect(&session, &DocumentSession::parsed, &session,
                   [&] { require(!changes.isEmpty(), "committed mapping precedes parsed consumers"); });
  require(session.applyTextDelta(5, 0, " longer", true), "local text edit succeeds");
  require(changes.size() == 1 && changes.back().mapOffset(20) == 27, "local commit publishes one exact source mapping");
  require(!session.applyTextDelta(-1, 0, "x", true) && changes.size() == 1,
          "rejected edits never move reading anchors");
  const auto old = session.markdownText().toString();
  auto next = old;
  next.insert(0, "```cpp\nx\n```\n\n");
  require(session.applyMarkdownText(next, true), "full-text fallback succeeds");
  require(changes.size() == 2 && changes.back().mapOffset(old.size() - 3) == next.size() - 3,
          "full-text commits preserve unchanged suffix anchors");
  require(session.applyMarkdownText(next, true) && changes.size() == 2,
          "reparsing unchanged text does not double-map anchors");
  const SourceTextChange separated{{{2, 1, 4}, {20, 3, 1}}};
  require(separated.mapOffset(10) == 13 && separated.mapOffset(30) == 31 && separated.reversed().mapOffset(13) == 10 &&
              separated.reversed().mapOffset(31) == 30 && separated.mapOffset(-1) == -1,
          "separated edits map unchanged interiors and reverse without losing invalid anchors");
  PieceTable fragmented("aaaBBBccc");
  fragmented.replace(3, 6, QStringView(u"XXX"));
  const auto full = SourceTextChange::between(fragmented, QStringView(u"aaaYYYYccc"));
  require(full.edits.size() == 1 && full.edits[0].start == 3 && full.edits[0].removed == 3 &&
              full.edits[0].inserted == 4 && full.mapOffset(7) == 8,
          "full-text mapping compares piece-table chunks without flattening");
}

void drainAsyncParses() {
  for (int i = 0; i < 4; ++i) {
    require(QThreadPool::globalInstance()->waitForDone(10000), "async test workers finish");
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
}

void testCompletedAndSupersededParsesReleaseSources() {
  DocumentSession session;
  QString text = "```text\n" + QString(512 * 1024, QLatin1Char('x')) + "\n```\n";
  require(text.isDetached(), "source starts with one owner");
  session.openDocumentAsync(text);
  drainAsyncParses();
  require(!session.isAsyncParseInProgress(), "successful async parse completes");
  session.newDocument();
  drainAsyncParses();
  require(text.isDetached(), "empty replacement releases the previously published source");

  session.openDocumentAsync(text);
  session.newDocument(); // supersede before the worker can publish on the GUI thread
  drainAsyncParses();
  require(text.isDetached() && session.markdownText().isEmpty(), "obsolete async result releases its source and AST");
  require(session.findChildren<QFutureWatcherBase*>().isEmpty(), "completed per-job watchers are destroyed");
}

void testReentrantOpenKeepsLatestBusyState() {
  DocumentSession session;
  bool reopened = false;
  bool staleIdle = false;
  QObject::connect(&session, &DocumentSession::parsed, &session, [&](qint64) {
    if (!reopened) {
      reopened = true;
      session.openDocumentAsync(QStringLiteral("Second document\n"));
    }
  });
  QObject::connect(&session, &DocumentSession::parseBusy, &session, [&](bool busy) {
    if (!busy && !session.markdownText().toString().contains("Second"))
      staleIdle = true;
  });
  session.openDocumentAsync(QStringLiteral("First document\n"));
  drainAsyncParses();
  require(reopened && !staleIdle && !session.isAsyncParseInProgress(),
          "a reentrant open does not receive an obsolete idle signal");
  require(session.markdownText().toString().contains("Second"), "the reentrant open publishes the latest document");
}

QJsonArray inlineSnapshot(const QVector<InlineNode>& inlines) {
  QJsonArray values;
  for (const auto& node : inlines) {
    const auto r = node.sourceRanges();
    values.append(
        QJsonObject{{"type", int(node.type())},
                    {"text", node.text()},
                    {"marker", node.marker()},
                    {"href", node.href()},
                    {"title", node.title()},
                    {"alt", node.alt()},
                    {"autolink", node.isAutolink()},
                    {"shared", node.usesSharedText()},
                    {"ranges", QJsonArray{qint64(r.source.start), qint64(r.source.end), qint64(r.content.start),
                                          qint64(r.content.end), qint64(r.openMarker.start), qint64(r.openMarker.end),
                                          qint64(r.closeMarker.start), qint64(r.closeMarker.end)}},
                    {"children", inlineSnapshot(node.children())}});
  }
  return values;
}

QJsonObject blockSnapshot(const MarkdownNode& node) {
  const auto r = node.sourceRange();
  QJsonArray children;
  for (const auto& child : node.children())
    children.append(blockSnapshot(*child));
  return {
      {"type", int(node.type())},
      {"literal", node.literal()},
      {"range", QJsonArray{qint64(r.byteStart), qint64(r.byteEnd), r.lineStart, r.lineEnd, r.columnStart, r.columnEnd}},
      {"heading", node.headingLevel()},
      {"language", node.codeLanguage()},
      {"inline_base", qint64(node.inlineAbsoluteDelta())},
      {"inlines", inlineSnapshot(node.inlines())},
      {"children", children}};
}

void testPreparedAsyncDocumentMatchesSyncAndRemainsEditable() {
  const QString text = QString::fromUtf8(
      "---\ntitle: Prepared\n---\n\n# Heading\n\n"
      "Unicode \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80 **bold** and ~~strike~~, &amp; and \\*escaped\\*.\n\n"
      "> Quote with [link](https://example.com) and `code`.\n\n"
      "- Item\n  - Nested $x^2$\n\n"
      "| A | B |\n| --- | --- |\n| escaped \\| pipe | **cell** |\n\n"
      "```cpp\nint x;\n```\n\n[^note]: Footnote\n\nTail paragraph [^note].\n\n###\n\n*\n\n> *\n");
  DocumentSession sync, async;
  sync.setMarkdownText(text, false);
  require(!sync.pendingMarkerOffsets().isEmpty(), "prepared open fixture must include pending markers");
  const auto expected = blockSnapshot(sync.document().root());
  bool publishedPrepared = false;
  QObject::connect(&async.document(), &MarkdownDocument::documentReset,
                   [&] { publishedPrepared = blockSnapshot(async.document().root()) == expected; });
  QEventLoop loop;
  QObject::connect(&async, &DocumentSession::parseBusy, &loop, [&loop](bool busy) {
    if (!busy)
      loop.quit();
  });
  async.openDocumentAsync(text);
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();
  require(!async.isAsyncParseInProgress(), "prepared async open must finish");
  require(publishedPrepared, "documentReset must publish complete coordinates and shared text");
  require(blockSnapshot(async.document().root()) == expected,
          "async preparation must match synchronous AST and coordinates");
  require(async.pendingMarkerOffsets() == sync.pendingMarkerOffsets(), "pending markers must survive preparation");
  for (qsizetype i = 0; i <= text.size(); ++i)
    require(async.document().lineOffsets().lineForOffset(i) == sync.document().lineOffsets().lineForOffset(i),
            "prepared text index must retain line lookup");
  const auto offset = text.indexOf(QStringLiteral("Tail")) + 2;
  require(async.applyTextDelta(offset, 0, QStringLiteral("X"), true), "prepared document must accept local edits");
  require(sync.applyTextDelta(offset, 0, QStringLiteral("X"), true), "synchronous reference must accept local edits");
  require(async.markdownText().toString() == sync.markdownText().toString(),
          "prepared text must remain correct after editing");
  require(blockSnapshot(async.document().root()) == blockSnapshot(sync.document().root()),
          "editing must preserve prepared coordinates");
}

void testParseOptionsWhileOpeningPreservesPendingFile() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("Previous buffer\n"), false);
  const QString pending = QStringLiteral("# Pending file\n\n==highlight==\n");
  session.openDocumentAsync(pending);
  ParseOptions options;
  options.enableHighlight = true;
  session.setParseOptions(options);
  require(session.isAsyncParseInProgress(), "changing options keeps the pending open asynchronous");
  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parseBusy, &loop, [&loop](bool busy) {
    if (!busy) {
      loop.quit();
    }
  });
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();
  require(!session.isAsyncParseInProgress(), "pending file finishes with new options");
  require(session.markdownText().toString() == pending,
          "parse options must not replace the pending file with the previous buffer");
  require(!session.document().isModified(), "pending file remains clean");
}

void testOpenDocumentAsyncEmitsParsed() {
  DocumentSession session;
  int parsedCount = 0;
  QObject::connect(&session, &DocumentSession::parsed, [&parsedCount](qint64) { ++parsedCount; });

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });

  session.openDocumentAsync(QStringLiteral("# Heading\n\nparagraph text\n"));
  QTimer::singleShot(5000, &loop, &QEventLoop::quit); // timeout safeguard
  loop.exec();

  require(parsedCount >= 1, "openDocumentAsync should emit parsed asynchronously");
  require(session.document().root().children().size() >= 1, "async parse should populate the document tree");
  require(session.markdownText().toString().contains(QStringLiteral("Heading")),
          "async parse should preserve the source text");
}

void testOpenDocumentAsyncEmitsParseBusy() {
  DocumentSession session;
  bool sawBusy = false;
  bool sawIdle = false;
  QObject::connect(&session, &DocumentSession::parseBusy, [&](bool busy) {
    if (busy) {
      sawBusy = true;
    } else {
      sawIdle = true;
    }
  });

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });
  session.openDocumentAsync(QStringLiteral("hello\n"));
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();

  require(sawBusy, "openDocumentAsync should signal parseBusy(true) on launch");
  require(sawIdle, "finishAsyncParse should signal parseBusy(false) on completion");
}

void testOpenDocumentAsyncRetainsProfiledMetrics() {
  DocumentSession session;
  session.setFullParsePerformanceMetricsEnabled(true);

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });
  session.openDocumentAsync(QStringLiteral("# Profiled open\n\n"
                                           "Paragraph with **inline content**.\n"));
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();

  const ParsePerformanceMetrics& metrics = session.lastFullParsePerformanceMetrics();
  require(!metrics.isEmpty(), "async full parse should retain structured performance metrics");
  bool sawCmark = false;
  for (const ParsePhasePerformance& phase : metrics.phases) {
    sawCmark = sawCmark || phase.name == QStringLiteral("parse.cmark");
  }
  require(sawCmark, "async full parse metrics should contain the cmark phase");

  session.setFullParsePerformanceMetricsEnabled(false);
  require(session.lastFullParsePerformanceMetrics().isEmpty(),
          "disabling full-parse profiling should clear retained metrics");
}

// A finished worker is not a finished open: its result is installed by a queued GUI-thread slot.
// Keep the GUI thread blocked long enough for a tiny parse to finish and verify that the session
// remains busy until that slot has committed the new document.
void testAsyncStateCoversPendingGuiCommit() {
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("old\n"), false);

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });
  session.openDocumentAsync(QStringLiteral("new\n"));
  QThread::msleep(100);

  require(session.isAsyncParseInProgress(),
          "async open must remain busy until the GUI thread commits the worker result");
  require(session.markdownText().toString() == QStringLiteral("old\n"),
          "the live document should remain unchanged before the GUI commit");

  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();
  require(!session.isAsyncParseInProgress(), "async open should become idle after the GUI commit");
  require(session.markdownText().toString() == QStringLiteral("new\n"),
          "the GUI commit should install the worker result");
}

// A second open supersedes the first: only the latest text survives (the stale worker's result is
// discarded by the generation check in finishAsyncParse).
void testOpenDocumentAsyncSupersedesInFlight() {
  DocumentSession session;
  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });

  session.openDocumentAsync(QStringLiteral("# First document\n"));
  session.openDocumentAsync(QStringLiteral("# Second document\n"));
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  loop.exec();

  require(session.markdownText().toString().contains(QStringLiteral("Second")),
          "the second openDocumentAsync should win (the first was superseded)");
}

// An edit attempted while an async open parse is in flight must be rejected (applyTextDelta returns
// false) so it can't land on the stale pre-open document and supersede (discard) the worker's result
// for the file the user actually opened. Regression guard for the "type during async open loses the
// file" data-loss bug — also covers the applyTextDelta paths that bypass InputController (undo,
// table snapshot, render facade), which a view-only loading mask would NOT stop.
void testApplyTextDeltaRejectedWhileAsyncInFlight() {
  DocumentSession session;
  // Seed stale pre-open content so an erroneously-accepted edit would be visibly wrong below.
  session.setMarkdownText(QStringLiteral("OLD CONTENT THAT MUST NOT SURVIVE\n"), false);

  // A 20k-paragraph document keeps the worker in flight across the synchronous checks below:
  // QtConcurrent::run returns a Running future immediately, and the worker thread can neither finish
  // nor deliver its finished signal until the GUI thread yields to the event loop (these checks do not).
  QString big;
  big.reserve(300000);
  for (int i = 0; i < 20000; ++i) {
    big += QStringLiteral("para %1\n\n").arg(i);
  }

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });
  // parseBusy(false) quits the loop on BOTH success and the discard branch (finishAsyncParse emits
  // it either way), so a regression that discards the worker — and thus never emits parsed — still
  // exits promptly instead of waiting out the timeout.
  QObject::connect(&session, &DocumentSession::parseBusy, [&loop](bool busy) {
    if (!busy)
      loop.quit();
  });
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);

  session.openDocumentAsync(big);

  require(session.isAsyncParseInProgress(),
          "an async open parse should be in flight immediately after openDocumentAsync");
  const bool accepted = session.applyTextDelta(0, 0, QStringLiteral("X"), false, {});
  require(!accepted, "applyTextDelta must be rejected while an async parse is in flight (would clobber the open)");

  loop.exec(); // let the worker finish on the GUI thread

  const QString text = session.markdownText().toString();
  require(text.contains(QStringLiteral("para 19999")),
          "the opened file's content must survive — the rejected edit must not have superseded the worker");
  require(!text.contains(QStringLiteral("OLD CONTENT")),
          "the stale pre-open text must have been replaced by the opened file");
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("DocumentSessionAsyncParseTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testCommittedSourceChanges);
  RUN_TEST(testOpenDocumentAsyncEmitsParsed);
  RUN_TEST(testCompletedAndSupersededParsesReleaseSources);
  RUN_TEST(testReentrantOpenKeepsLatestBusyState);
  RUN_TEST(testOpenDocumentAsyncEmitsParseBusy);
  RUN_TEST(testOpenDocumentAsyncRetainsProfiledMetrics);
  RUN_TEST(testAsyncStateCoversPendingGuiCommit);
  RUN_TEST(testOpenDocumentAsyncSupersedesInFlight);
  RUN_TEST(testApplyTextDeltaRejectedWhileAsyncInFlight);
  RUN_TEST(testParseOptionsWhileOpeningPreservesPendingFile);
  RUN_TEST(testPreparedAsyncDocumentMatchesSyncAndRemainsEditable);
#undef RUN_TEST
  return 0;
}
