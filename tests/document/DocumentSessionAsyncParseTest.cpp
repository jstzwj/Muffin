#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"

#include "../parser/ParserTestUtils.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

using namespace muffin;

// openDocumentAsync runs parser_.parseDocument on a QtConcurrent worker thread and finishes
// (setMarkdownText + relativize + emit parsed) on the GUI thread via QFutureWatcher. These tests
// spin an event loop (no worker block) to confirm the async open path delivers `parsed`, brackets
// it with `parseBusy`, and supersedes an in-flight parse when a newer open is requested.

namespace {

void testOpenDocumentAsyncEmitsParsed() {
  DocumentSession session;
  int parsedCount = 0;
  QObject::connect(&session, &DocumentSession::parsed, [&parsedCount](qint64) { ++parsedCount; });

  QEventLoop loop;
  QObject::connect(&session, &DocumentSession::parsed, [&loop](qint64) { loop.quit(); });

  session.openDocumentAsync(QStringLiteral("# Heading\n\nparagraph text\n"));
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);  // timeout safeguard
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
  QObject::connect(&session, &DocumentSession::parseBusy, [&loop](bool busy) { if (!busy) loop.quit(); });
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);

  session.openDocumentAsync(big);

  require(session.isAsyncParseInProgress(),
          "an async open parse should be in flight immediately after openDocumentAsync");
  const bool accepted = session.applyTextDelta(0, 0, QStringLiteral("X"), false, {});
  require(!accepted,
          "applyTextDelta must be rejected while an async parse is in flight (would clobber the open)");

  loop.exec();  // let the worker finish on the GUI thread

  const QString text = session.markdownText().toString();
  require(text.contains(QStringLiteral("para 19999")),
          "the opened file's content must survive — the rejected edit must not have superseded the worker");
  require(!text.contains(QStringLiteral("OLD CONTENT")),
          "the stale pre-open text must have been replaced by the opened file");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication::setOrganizationName(QStringLiteral("MuffinTest"));
  QCoreApplication::setApplicationName(QStringLiteral("DocumentSessionAsyncParseTest"));
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testOpenDocumentAsyncEmitsParsed);
  RUN_TEST(testOpenDocumentAsyncEmitsParseBusy);
  RUN_TEST(testOpenDocumentAsyncSupersedesInFlight);
  RUN_TEST(testApplyTextDeltaRejectedWhileAsyncInFlight);
#undef RUN_TEST
  return 0;
}
