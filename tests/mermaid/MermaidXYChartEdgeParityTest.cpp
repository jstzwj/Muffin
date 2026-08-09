// Focused XYChart algorithm/edge contracts that are easy to lose while the
// broad SVG fixture remains numerically close.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/xychart/XYChartScene.h"

#include <QFontMetricsF>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

std::shared_ptr<const xychart::XYChartScene> renderScene(
    const QString& source, editor::MermaidRenderEntry* rendered = nullptr) {
  editor::MermaidRenderCache cache;
  editor::MermaidRenderEntry entry = cache.getSync(cache.makeKey(source), source);
  require(entry.status == editor::MermaidRenderStatus::Ready && entry.scene,
          QStringLiteral("XYChart render failed: ") + entry.errorMessage);
  const auto scene =
      std::dynamic_pointer_cast<const xychart::XYChartScene>(entry.scene);
  require(bool(scene), QStringLiteral("XYChart entry has wrong scene type"));
  if (rendered) *rendered = std::move(entry);
  return scene;
}

template <typename T>
QVector<const T*> inGroup(const QVector<T>& values, const QString& group) {
  QVector<const T*> result;
  for (const T& value : values)
    if (value.group == group) result.append(&value);
  return result;
}

QImage paintScene(const xychart::XYChartScene& scene) {
  const QRectF bounds = scene.sceneBounds();
  require(bounds.width() > 0.0 && bounds.height() > 0.0 &&
              bounds.width() <= 4096.0 && bounds.height() <= 4096.0,
          QStringLiteral("Refusing unbounded XYChart edge image"));
  QImage image(qCeil(bounds.width()), qCeil(bounds.height()),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-bounds.left(), -bounds.top());
  scene.paint(painter, MermaidPaintOptions{});
  painter.end();
  return image;
}

int nearColorPixels(const QImage& image, const QColor& expected,
                    int tolerance = 12) {
  int count = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor actual = image.pixelColor(x, y);
      if (actual.alpha() < 24) continue;
      count += std::abs(actual.red() - expected.red()) <= tolerance &&
               std::abs(actual.green() - expected.green()) <= tolerance &&
               std::abs(actual.blue() - expected.blue()) <= tolerance;
    }
  }
  return count;
}

QRect strongRedBounds(const QImage& image) {
  QRect bounds;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.red() > 160 && pixel.green() < 100 && pixel.blue() < 100)
        bounds |= QRect(x, y, 1, 1);
    }
  }
  return bounds;
}

QStringList textValues(const xychart::XYChartScene& scene,
                       const QString& group) {
  QStringList result;
  for (const auto* text : inGroup(scene.texts, group)) result.append(text->text);
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  const auto band = renderScene(QStringLiteral(
      "xychart-beta vertical\n"
      "x-axis [A, B, C]\n"
      "y-axis 0 --> 100\n"
      "bar [20, 50, 80]\n"
      "line [10, 60, 90]"));
  const auto bars = inGroup(band->rects, QStringLiteral("plot/bar-plot-0"));
  const auto lines = inGroup(band->paths, QStringLiteral("plot/line-plot-1"));
  require(bars.size() == 3 && lines.size() == 1 &&
              lines.front()->points.size() == 3,
          QStringLiteral("Band plot topology drifted"));
  const qreal distance = bars.at(1)->rect.center().x() -
                         bars.at(0)->rect.center().x();
  // Mermaid derives bar padding from the effective scale range divided by
  // the category count, not from the distance between adjacent band centers.
  const auto labelFont = editor::makeUnhintedCssPixelFont(
      band->style.fontFamily, band->config.xAxis.labelFontSize);
  const QFontMetricsF labelMetrics(labelFont.font);
  const qreal initialPadding =
      labelMetrics.horizontalAdvance(QStringLiteral("A")) * labelFont.scale / 2.0;
  const qreal initialTickDistance =
      (band->plotBounds.width() - 2.0 * initialPadding) / 3.0;
  const qreal expectedPadding = std::floor(0.7 * initialTickDistance / 2.0);
  const qreal finalTickDistance =
      (band->plotBounds.width() - 2.0 * expectedPadding) / 3.0;
  const qreal expectedBarWidth =
      std::min(2.0 * expectedPadding, finalTickDistance) * 0.95;
  require(std::fabs(bars.front()->rect.width() - expectedBarWidth) < 0.001 &&
              std::fabs(bars.at(2)->rect.center().x() -
                        bars.at(1)->rect.center().x() - distance) < 0.001,
          QStringLiteral("Band outer-padding/bar-width formula drifted"));
  for (int i = 0; i < 3; ++i)
    require(std::fabs(lines.front()->points.at(i).x() -
                      bars.at(i)->rect.center().x()) < 0.001,
            QStringLiteral("Band line/bar centers diverged"));

  const auto linear = renderScene(QStringLiteral(
      "xychart-beta vertical\n"
      "x-axis 0 --> 100\n"
      "y-axis 0 --> 100\n"
      "line [0, 50, 100]"));
  const QStringList xTicks = textValues(*linear, QStringLiteral("x-axis/label"));
  const QStringList yTicks = textValues(*linear, QStringLiteral("y-axis/label"));
  require(xTicks == QStringList({"0", "10", "20", "30", "40", "50",
                                 "60", "70", "80", "90", "100"}),
          QStringLiteral("D3 linear x ticks drifted"));
  require(yTicks == QStringList({"100", "90", "80", "70", "60", "50",
                                 "40", "30", "20", "10", "0"}),
          QStringLiteral("D3 left-axis tick order drifted"));

  const auto equal = renderScene(QStringLiteral(
      "xychart-beta\n"
      "x-axis 5 --> 5\n"
      "y-axis 2 --> 2\n"
      "bar [2]\n"
      "line [2]"));
  const auto equalLine = inGroup(equal->paths, QStringLiteral("plot/line-plot-1"));
  const auto equalBar = inGroup(equal->rects, QStringLiteral("plot/bar-plot-0"));
  require(equalLine.size() == 1 && equalLine.front()->points.size() == 1 &&
              equalBar.size() == 1 &&
              std::isfinite(equalLine.front()->points.front().x()) &&
              std::isfinite(equalLine.front()->points.front().y()) &&
              std::fabs(equalLine.front()->points.front().x() -
                        equal->plotBounds.center().x()) < 0.001 &&
              std::fabs(equalLine.front()->points.front().y() -
                        equal->plotBounds.center().y()) < 0.001,
          QStringLiteral("Equal linear domain must map to plot center"));
  require(!paintScene(*equal).isNull(),
          QStringLiteral("Equal-domain XYChart must remain paintable"));

  const auto truncated = renderScene(QStringLiteral(
      "xychart-beta\n"
      "x-axis [A, B, C]\n"
      "bar [1, 2, 3, 999]\n"
      "line [4, 5]"));
  require(inGroup(truncated->rects, QStringLiteral("plot/bar-plot-0")).size() == 3 &&
              inGroup(truncated->paths, QStringLiteral("plot/line-plot-1"))
                      .front()->points.size() == 3 &&
              !std::isfinite(inGroup(truncated->paths,
                                     QStringLiteral("plot/line-plot-1"))
                                 .front()->points.back().y()),
          QStringLiteral("Band data truncation/short-series semantics drifted"));

  const auto sourceWins = renderScene(QStringLiteral(
      "%%{init: {\"xyChart\": {\"chartOrientation\": \"horizontal\"}}}%%\n"
      "xychart-beta vertical\n"
      "x-axis [A, B]\nbar [1, 2]"));
  require(sourceWins->config.orientation == xychart::XYChartOrientation::Vertical &&
              !inGroup(sourceWins->texts, QStringLiteral("x-axis/label")).isEmpty(),
          QStringLiteral("Source orientation must override config orientation"));

  const auto invalidRotation = renderScene(QStringLiteral(
      "%%{init: {\"xyChart\": {\"xAxis\": {\"labelRotation\": 180}}}}%%\n"
      "xychart-beta\nx-axis [A, B]\nbar [1, 2]"));
  require(std::all_of(invalidRotation->texts.cbegin(),
                      invalidRotation->texts.cend(), [](const auto& text) {
                        return text.group != QLatin1String("x-axis/label") ||
                               text.rotation == 0.0;
                      }),
          QStringLiteral("Out-of-range axis rotation must fall back to zero"));

  const auto dataQuirk = renderScene(QStringLiteral(
      "%%{init: {\"xyChart\": {\"showDataLabel\": true},"
      "\"themeVariables\": {\"xyChart\": {\"dataLabelColor\": \"#ff0000\"}}}}%%\n"
      "xychart-beta\nx-axis [A, B, C]\n"
      "line [91, 82, 73]\nbar [10, 20, 30]\nbar [40, 50, 60]"));
  require(textValues(*dataQuirk,
                     QStringLiteral("plot/bar-plot-1")) ==
              QStringList({"91", "82", "73"}) &&
              textValues(*dataQuirk,
                         QStringLiteral("plot/bar-plot-2")) ==
              QStringList({"91", "82", "73"}),
          QStringLiteral("Mermaid first-plot data-label quirk drifted"));
  require(nearColorPixels(paintScene(*dataQuirk), QColor(Qt::red)) > 30,
          QStringLiteral("Negative data-label font-size must inherit the 16px SVG root font"));

  const auto lineThenBar = renderScene(QStringLiteral(
      "xychart-beta\nx-axis [A, B, C]\ny-axis 0 --> 100\n"
      "line [100, 0, 100]\nbar [80, 80, 80]"));
  require(inGroup(lineThenBar->paths, QStringLiteral("plot/line-plot-0")).front()->paintOrder <
              inGroup(lineThenBar->rects, QStringLiteral("plot/bar-plot-1")).front()->paintOrder,
          QStringLiteral("Source plot order must keep a later bar above an earlier line"));
  const auto barThenLine = renderScene(QStringLiteral(
      "xychart-beta\nx-axis [A, B, C]\ny-axis 0 --> 100\n"
      "bar [80, 80, 80]\nline [100, 0, 100]"));
  require(inGroup(barThenLine->rects, QStringLiteral("plot/bar-plot-0")).front()->paintOrder <
              inGroup(barThenLine->paths, QStringLiteral("plot/line-plot-1")).front()->paintOrder,
          QStringLiteral("Source plot order must keep a later line above an earlier bar"));

  const auto explicitBlankTitle = renderScene(QStringLiteral(
      "---\ntitle: Front Matter\n---\nxychart-beta\ntitle \"   \"\nline [1]"));
  require(explicitBlankTitle->title.isEmpty() &&
              inGroup(explicitBlankTitle->texts,
                      QStringLiteral("chart-title")).isEmpty(),
          QStringLiteral("Explicit whitespace title must suppress the frontmatter title"));

  editor::MermaidRenderCache failureCache;
  const QString shortFirst = QStringLiteral(
      "%%{init: {\"xyChart\": {\"showDataLabel\": true}}}%%\n"
      "xychart-beta\nx-axis [A, B, C]\nline [91]\nbar [10,20,30]");
  const editor::MermaidRenderEntry shortFirstEntry =
      failureCache.getSync(failureCache.makeKey(shortFirst), shortFirst);
  require(shortFirstEntry.status == editor::MermaidRenderStatus::Error &&
              shortFirstEntry.errorMessage.contains(
                  QStringLiteral("Cannot read properties of undefined")),
          QStringLiteral("Short first plot must preserve the upstream data-label runtime error"));

  editor::MermaidRenderEntry inertEntry;
  const auto inert = renderScene(QStringLiteral(
      "%%{init: {\"xyChart\": {\"useMaxWidth\": false}}}%%\n"
      "xychart-beta\nx-axis [A, B]\nbar [1, 2]"), &inertEntry);
  require(inert->bounds == QRectF(0, 0, 700, 500) &&
              inertEntry.metadata.svgUseMaxWidth,
          QStringLiteral("xyChart.useMaxWidth must remain upstream-inert"));

  QString paletteSource = QStringLiteral(
      "%%{init: {\"themeVariables\": {\"xyChart\": {"
      "\"backgroundColor\": \"#010203\", "
      "\"plotColorPalette\": \"#ff0000,#00ff00\", "
      "\"xAxisLineColor\": \"#0000ff\"}}}}%%\n"
      "xychart-beta\nx-axis [A, B]\ny-axis 0 --> 5\n");
  for (int i = 0; i < 4; ++i)
    paletteSource += QStringLiteral("bar [%1, %2]\n").arg(i + 1).arg(i + 2);
  const auto themed = renderScene(paletteSource);
  require(themed->style.plotColorPalette.size() == 2 &&
              inGroup(themed->rects, QStringLiteral("plot/bar-plot-0"))
                      .front()->fill == QLatin1String("#ff0000") &&
              inGroup(themed->rects, QStringLiteral("plot/bar-plot-1"))
                      .front()->fill == QLatin1String("#00ff00") &&
              inGroup(themed->rects, QStringLiteral("plot/bar-plot-2"))
                      .front()->fill == QLatin1String("#ff0000"),
          QStringLiteral("XYChart palette cycling drifted"));
  const auto paintThemed = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"xyChart\": {"
      "\"backgroundColor\": \"#010203\", "
      "\"plotColorPalette\": \"#ff0000,#00ff00\", "
      "\"xAxisLineColor\": \"#0000ff\"}}}}%%\n"
      "xychart-beta\nx-axis [A, B]\ny-axis 0 --> 5\n"
      "bar [1, 2]\nline [4, 3]"));
  const QImage themedImage = paintScene(*paintThemed);
  const int backgroundPixels =
      nearColorPixels(themedImage, QColor(QStringLiteral("#010203")));
  const int redPixels = nearColorPixels(themedImage, QColor(QStringLiteral("#ff0000")));
  const int greenPixels = nearColorPixels(themedImage, QColor(QStringLiteral("#00ff00")));
  const int bluePixels = nearColorPixels(themedImage, QColor(QStringLiteral("#0000ff")));
  require(backgroundPixels > 1000 && redPixels > 100 && greenPixels > 100 &&
              bluePixels > 10,
          QStringLiteral("XYChart theme values did not reach actual paint: bg=%1 red=%2 green=%3 blue=%4")
              .arg(backgroundPixels).arg(redPixels).arg(greenPixels).arg(bluePixels));

  const auto emptyPaint = renderScene(QStringLiteral(
      "%%{init: {\"themeVariables\": {\"xyChart\": {"
      "\"backgroundColor\": \"#ffffff\", \"titleColor\": \"\", "
      "\"plotColorPalette\": \"\"}}}}%%\n"
      "xychart-beta\ntitle T\nx-axis [A]\nbar [1]"));
  require(nearColorPixels(paintScene(*emptyPaint), QColor(Qt::black)) > 100,
          QStringLiteral("Empty SVG fill declarations must inherit black instead of becoming none"));

#ifdef Q_OS_WIN
  // Chrome 151 with Arial 72px places the strong-ink bounds for a hanging
  // "20" at y=anchor..anchor+52. This independently guards the SVG rule
  // that the hanging baseline is 80% of the active font's ascent; the old
  // Noto-specific em constant shifted Arial two pixels upward.
  xychart::XYChartScene hangingArial;
  const int arialId = QFontDatabase::addApplicationFont(
      QStringLiteral("C:/Windows/Fonts/arial.ttf"));
  require(arialId >= 0 &&
              !QFontDatabase::applicationFontFamilies(arialId).isEmpty(),
          QStringLiteral("Chrome-derived Arial oracle requires the Windows Arial font"));
  hangingArial.bounds = QRectF(0, 0, 200, 120);
  hangingArial.style.backgroundColor = QStringLiteral("white");
  hangingArial.style.fontFamily =
      QFontDatabase::applicationFontFamilies(arialId).front();
  hangingArial.texts.append({QStringLiteral("hanging-oracle"),
                             QStringLiteral("20"), QPointF(20, 20),
                             QStringLiteral("#ff0000"), 72.0, 0.0,
                             xychart::XYChartTextAnchor::Start,
                             xychart::XYChartBaseline::Hanging, 0});
  const QRect hangingInk = strongRedBounds(paintScene(hangingArial));
  require(!hangingInk.isEmpty() && std::abs(hangingInk.top() - 20) <= 1 &&
              hangingInk.bottom() >= 70 && hangingInk.bottom() <= 73,
          QStringLiteral("Arial hanging baseline diverged from Chrome: y=%1..%2")
              .arg(hangingInk.top()).arg(hangingInk.bottom()));
#endif

  std::puts("MermaidXYChartEdgeParityTest: D3/band/degenerate/config/paint passed");
  return 0;
}
