// Render-layer memory bench: parse a large file then build DocumentLayout (the render index + slot
// path the GUI runs AFTER parse), to measure how much the render layer adds on top of the parse
// tree and isolate whether a large file OOMs at parse or at layout. Links MuffinUi; runs offscreen.
//   MUFFIN_BENCH_FILE=<abs> ctest --preset conan-release -R OpenRenderMemBench --extra-verbose
#include "diagnostics/ProcessMemory.h"
#include "document/DocumentSession.h"
#include "document/MarkdownNode.h"
#include "render/DocumentLayout.h"
#include "theme/RenderTheme.h"

#include <QApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QString>

#include <cstdio>

namespace {

void logWs(const char* label) {
  std::fprintf(stdout, "[mem] %-30s ws=%lldMB\n", label,
               static_cast<long long>(muffin::diag::workingSetBytes() >> 20));
  std::fflush(stdout);
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
  // Force offscreen so the bench runs anywhere (headless CI, no display) without depending on the
  // global test harness setting QT_QPA_PLATFORM. Memory footprint is platform-agnostic; only font/
  // glyph metrics differ offscreen, and this bench measures working set, not rendering geometry.
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QLoggingCategory::setFilterRules(QStringLiteral("muffin.perf.debug=true"));

  QString path = QString::fromLocal8Bit(qgetenv("MUFFIN_BENCH_FILE"));
  if (path.isEmpty() && argc > 1) {
    path = QString::fromLocal8Bit(argv[1]);
  }
  QString text;
  if (path.isEmpty()) {
    path = QStringLiteral("(smoke)");
    text = QStringLiteral("# Smoke\n\nA **bold** and `code` and [link](http://x) line.\n");
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
  logWs("start");

  muffin::DocumentSession session;
  session.setMarkdownText(text, false);
  logWs("after parse (DocumentSession)");

  long long blocks = 0;
  long long inlines = 0;
  countTree(session.document().root(), blocks, inlines);
  std::fprintf(stdout, "tree: %lld top-level, %lld nodes, %lld inlines\n",
               static_cast<long long>(session.document().root().children().size()), blocks, inlines);

  muffin::RenderTheme theme = muffin::RenderTheme::github();
  muffin::DocumentLayout layout;
  layout.rebuild(session.document(), theme, 800.0, {}, {},
                 muffin::DocumentLayout::BuildPolicy::Lazy);
  logWs("after DocumentLayout.rebuild (Lazy)");

  // promote the first few slots the way ensureVisibleBuilt would, to measure a visible window.
  layout.ensureBuilt(0, 30, theme);
  logWs("after ensureBuilt(0..30)");

  std::fflush(stdout);
  return 0;
}
