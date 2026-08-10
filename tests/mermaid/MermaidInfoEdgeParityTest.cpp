#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/info/InfoScene.h"

#include <QGuiApplication>
#include <QImage>

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
editor::MermaidRenderEntry render(editor::MermaidRenderCache& cache,
                                  const QString& source) {
  return cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  editor::MermaidRenderCache cache;

  const auto plain = render(cache, QStringLiteral("info"));
  const auto show = render(cache, QStringLiteral("info showInfo"));
  require(plain.status == editor::MermaidRenderStatus::Ready &&
              show.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Info variants must render"));
  require(plain.scene->toJsonObject() == show.scene->toJsonObject(),
          QStringLiteral("showInfo is renderer-inert"));

  const auto metadata = render(
      cache, QStringLiteral("---\ntitle: Front\n---\ninfo\ntitle Inline\n"
                            "accTitle: AT\naccDescr: AD"));
  require(metadata.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("Info metadata source must render"));
  require(metadata.metadata.title.isEmpty() &&
              metadata.metadata.accessibleTitle.isEmpty() &&
              metadata.metadata.accessibleDescription.isEmpty(),
          QStringLiteral("Info metadata must remain renderer-inert"));

  const auto invalid = render(cache, QStringLiteral("info\nunknown"));
  require(invalid.status == editor::MermaidRenderStatus::Error,
          QStringLiteral("Invalid Info source must fail"));
  require(invalid.diagnostic.stage == QLatin1String("parse") &&
              invalid.diagnostic.code == QLatin1String("info-lexer-error") &&
              invalid.diagnostic.span.line == 2 &&
              invalid.diagnostic.span.column == 1,
          QStringLiteral("Info lexer diagnostic drifted"));

  const auto png = editor::MermaidRenderCache::renderMermaidSourceToPng(
      QStringLiteral("info"), 1.0);
  QImage pngImage;
  const qsizetype comma = png.dataUrl.indexOf(QLatin1Char(','));
  if (comma >= 0)
    pngImage.loadFromData(
        QByteArray::fromBase64(png.dataUrl.mid(comma + 1).toLatin1()), "PNG");
  require(pngImage.size() == QSize(400, 150),
          QStringLiteral("Info replaced-element raster size drifted"));
  const auto svg = editor::MermaidRenderCache::renderMermaidSourceToSvg(
      QStringLiteral("info"), 0);
  require(svg.svg.contains(QByteArrayLiteral("aria-roledescription=\"info\"")) &&
              !svg.svg.contains(QByteArrayLiteral("viewBox=")) &&
              !svg.svg.contains(QByteArrayLiteral("<title")) &&
              !svg.svg.contains(QByteArrayLiteral("<desc")),
          QStringLiteral("Info SVG accessibility contract drifted: ") +
              QString::fromUtf8(svg.svg.left(800)));
  return 0;
}
