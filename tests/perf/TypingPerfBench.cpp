// Benchmark: per-keystroke phase breakdown when typing inside a paragraph of a large document.
//
// Links MuffinCore only (pure Qt Core — no GUI, no RESOURCE_LOCK). It drives
// DocumentSession::applyTextDelta on a synthetic document rich in inline formatting (so the
// node/inline count — and thus the shiftRanges suffix cost — is realistic), captures the
// muffin.perf probes emitted by DocumentSession/MarkdownDocument, and reports median/max/sum
// per phase.
//
// Defaults are deliberately tiny so the registered ctest stays a fast smoke check that the
// harness compiles and runs. For a real measurement, override the env vars:
//   MUFFIN_BENCH_SIZE_MB=20 MUFFIN_BENCH_ITERS=400 ctest -R TypingPerfBench --output-on-failure
//
// Scenarios:
//   NEAR TOP — insert at offset 0 every keystroke. The whole document suffix must be shifted
//              and the flat-string memmove touches nearly the whole buffer (worst case).
//   MID DOC  — insert at the document midpoint. Half the suffix shifts (realistic).
//   NEAR END — insert inside the last top-level block. No suffix to shift, near-zero memmove
//              (best case; isolates the fixed floor: slice parse + demote + index).
#include "document/DocumentSession.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "document/PendingBlockMarker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>

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

// Each probe prints "<label> <ms> ms" (PerfTimer uses nospace with literal spaces).
bool parsePerfLine(const QString& line, QString& label, double& ms) {
  const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.size() < 2) {
    return false;
  }
  bool ok = false;
  const double value = parts.at(1).toDouble(&ok);
  if (!ok) {
    return false;
  }
  label = parts.at(0);
  ms = value;
  return true;
}

struct Row {
  QString label;
  double median = 0;
  double max = 0;
  double sum = 0;
  std::size_t count = 0;
};

void report(const char* scenario, qsizetype docBytes, qsizetype blockCount, qsizetype inlineCount, int iters) {
  std::map<QString, std::vector<double>> byLabel;
  for (const QString& line : g_perfLines) {
    QString label;
    double ms = 0;
    if (!parsePerfLine(line, label, ms)) {
      continue;
    }
    byLabel[label].push_back(ms);
  }

  std::fprintf(stdout, "\n=== %s  (doc=%lldB, %lld top-level blocks, %lld inlines, %d iters) ===\n",
               scenario, static_cast<long long>(docBytes), static_cast<long long>(blockCount),
               static_cast<long long>(inlineCount), iters);
  std::fprintf(stdout, "%-34s %8s %8s %10s %6s\n", "phase", "median", "max", "sum(ms)", "n");

  std::vector<Row> rows;
  for (auto& [label, samples] : byLabel) {
    std::sort(samples.begin(), samples.end());
    Row r;
    r.label = label;
    r.median = samples.at(samples.size() / 2);
    r.max = samples.back();
    for (double v : samples) {
      r.sum += v;
    }
    r.count = samples.size();
    rows.push_back(std::move(r));
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.sum > b.sum; });
  for (const Row& r : rows) {
    std::fprintf(stdout, "%-34s %8.3f %8.3f %10.2f %6zu\n", r.label.toUtf8().constData(),
                 r.median, r.max, r.sum, r.count);
  }
  std::fflush(stdout);
}

void countTree(const muffin::MarkdownNode& node, qsizetype& blocks, qsizetype& inlines) {
  ++blocks;
  inlines += static_cast<qsizetype>(node.inlines().size());
  for (const auto& child : node.children()) {
    if (child) {
      countTree(*child, blocks, inlines);
    }
  }
}

// Paragraphs rich in inline formatting (**bold**, _em_, `code`, [links](…)) so the inline count
// — and therefore the shiftRanges recursion cost — matches a real document.
QString makeBigDoc(qsizetype targetBytes) {
  static const char* units[] = {
      "This **bold** and _italic_ and `code` and [link](http://example.com/x) text.\n\n",
      "Another *em* phrase with **strong** words plus a [ref][1] inline node here.\n\n",
      "Plain sentence with `monospace` and **emphasis** scattered across the line.\n\n",
      "A line with [a link](https://site.example/path?q=1) and _underlined_ bits.\n\n",
  };
  QString doc;
  doc.reserve(targetBytes + 256);
  int i = 0;
  while (doc.size() < targetBytes) {
    doc += QString::fromLatin1(units[i++ % 4]);
  }
  return doc;
}

void typeAt(muffin::DocumentSession& session, qsizetype offset, int iters) {
  g_perfLines.clear();
  for (int k = 0; k < iters; ++k) {
    session.applyTextDelta(offset, 0, QStringLiteral("x"), true);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const QByteArray sizeEnv = qgetenv("MUFFIN_BENCH_SIZE_MB");
  const qsizetype sizeMb = sizeEnv.isEmpty() ? 1 : sizeEnv.toLongLong();
  const QByteArray itersEnv = qgetenv("MUFFIN_BENCH_ITERS");
  const int iters = itersEnv.isEmpty() ? 30 : itersEnv.toInt();

  QLoggingCategory::setFilterRules(QStringLiteral("muffin.perf.debug=true"));
  qInstallMessageHandler(perfCapture);

  muffin::DocumentSession session;
  // MUFFIN_BENCH_FILE: parse a real on-disk file (tables/code/math/lists) instead of the synthetic
  // inline-dense paragraph doc. Measures the true open path on the actual file the user opens.
  const QString doc = [&] {
    const QByteArray fileEnv = qgetenv("MUFFIN_BENCH_FILE");
    if (fileEnv.isEmpty()) {
      return makeBigDoc(sizeMb * 1024 * 1024);
    }
    QFile f(QString::fromLocal8Bit(fileEnv));
    f.open(QIODevice::ReadOnly);
    QString t = QString::fromUtf8(f.readAll());
    t.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return t;
  }();
  QElapsedTimer setup;
  setup.start();
  session.setMarkdownText(doc, false);
  std::fprintf(stdout, "setup (full parse of %lldB): %.1f ms\n",
               static_cast<long long>(doc.size()), setup.elapsed() / 1.0);

  qsizetype blocks = 0;
  qsizetype inlines = 0;
  countTree(session.document().root(), blocks, inlines);
  std::fprintf(stdout, "tree: %lld top-level blocks, %lld total nodes, %lld inlines\n",
               static_cast<long long>(session.document().root().children().size()),
               static_cast<long long>(blocks), static_cast<long long>(inlines));

  // Report the OPEN-parse phase breakdown before any typing probe clears the buffer — these are the
  // 100MB setup parse.* / session.* values, which the per-scenario reports would otherwise drop.
  report("SETUP (open parse of full doc)", doc.size(), blocks, inlines, 1);

  const auto& topBlocks = session.document().root().children();
  const qsizetype lastBlockStart = topBlocks.empty() ? 0 : topBlocks.back()->sourceRange().byteStart;
  const qsizetype midOffset = session.markdownText().toString().size() / 2;

  typeAt(session, 0, iters);
  report("NEAR TOP (insert @0, worst case)", doc.size(), blocks, inlines, iters);

  typeAt(session, midOffset, iters);
  report("MID DOC (insert @mid)", session.markdownText().toString().size(), blocks, inlines, iters);

  typeAt(session, lastBlockStart + 1, iters);
  report("NEAR END (inside last block, best case)", session.markdownText().toString().size(), blocks, inlines, iters);

  // Isolated cost of the InputController-path O(doc) operation that the session bench above
  // BYPASSES (it calls session.applyTextDelta directly, skipping InputController). This runs once
  // per keystroke in the real app ON TOP of session.localParse, so it explains the gap between the
  // bench (~tens of ms) and perceived lag. (BlockLayoutBuilder::setMarkdownText also copies the
  // whole text per rebuildBlock — ~the memmove cost — but its header pulls Qt GUI, unreachable here.)
  {
    const QString md = session.markdownText().toString();
    const muffin::MarkdownNode& root = session.document().root();
    const int reps = 5;

    double pendingMin = 1e9;
    for (int i = 0; i < reps; ++i) {
      QElapsedTimer t;
      t.start();
      volatile auto offsets = muffin::collectPendingMarkerOffsets(md, root);
      (void)offsets;
      pendingMin = std::min(pendingMin, t.elapsed() / 1.0);
    }

    std::fprintf(stdout, "\n=== ISOLATED InputController O(doc) op (min of %d) ===\n", reps);
    std::fprintf(stdout, "collectPendingMarkerOffsets (every keystroke):  %.2f ms\n", pendingMin);
  }

  std::fflush(stdout);
  return 0;
}
