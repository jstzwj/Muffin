// Open-memory bench: parses a real on-disk file headless and prints, in emission order, every
// muffin.perf probe together with its working-set snapshot. This shows EXACTLY which parse phase
// eats memory on a large file, and (run on 30MB then 50MB) reveals any super-linear step without
// having to crash on the 100MB file.
//
// Links MuffinCore only (pure Qt Core, no GUI lock). Usage:
//   set MUFFIN_BENCH_FILE=build/stress_50mb.md   (or pass the path as argv[1])
//   ctest -R OpenMemBench --output-on-failure
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"

#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QString>

#include <cstdio>

namespace {

void printPerf(QtMsgType, const QMessageLogContext& ctx, const QString& msg) {
  // Every perf category in the codebase is "muffin.perf". Print verbatim — the ws=<MB> suffix
  // appended by ParsePerfTimer / PerfTimer destructors rides along on each line.
  if (QLatin1String(ctx.category) == QLatin1String("muffin.perf")) {
    std::fprintf(stdout, "[perf] %s\n", msg.toUtf8().constData());
    std::fflush(stdout);
  }
}

void countTree(const muffin::MarkdownNode& node, long long& blocks, long long& inlines) {
  ++blocks;
  inlines += static_cast<long long>(node.inlines().size());
  for (const auto& child : node.children()) {
    if (child) {
      countTree(*child, blocks, inlines);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QLoggingCategory::setFilterRules(QStringLiteral("muffin.perf.debug=true"));
  qInstallMessageHandler(printPerf);

  QString path = QString::fromLocal8Bit(qgetenv("MUFFIN_BENCH_FILE"));
  if (path.isEmpty() && argc > 1) {
    path = QString::fromLocal8Bit(argv[1]);
  }

  QString text;
  if (path.isEmpty()) {
    // Smoke default so the registered ctest passes with no args (just verifies the harness
    // compiles+runs). Override MUFFIN_BENCH_FILE (or pass argv[1]) for a real measurement.
    path = QStringLiteral("(smoke)");
    text = QStringLiteral(
        "# Smoke\n\nA **bold** and _em_ and `code` and [link](http://x) line.\n\n$$E=mc^2$$\n");
  } else {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
      std::fprintf(stderr, "cannot open %s\n", path.toUtf8().constData());
      return 2;
    }
    text = QString::fromUtf8(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  }
  std::fprintf(stdout, "file: %s  (%lld chars)\n", path.toUtf8().constData(),
               static_cast<long long>(text.size()));
  std::fflush(stdout);

  muffin::DocumentSession session;
  session.setMarkdownText(text, false);

  long long blocks = 0;
  long long inlines = 0;
  countTree(session.document().root(), blocks, inlines);
  std::fprintf(stdout, "tree: %lld top-level blocks, %lld total nodes, %lld inlines\n",
               static_cast<long long>(session.document().root().children().size()), blocks, inlines);
  std::fflush(stdout);
  return 0;
}
