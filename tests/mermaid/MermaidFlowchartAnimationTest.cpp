#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/scene/FlowScene.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/theme/FlowTheme.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QImage paintAt(const flowscene::FlowScene& scene, qreal timeSeconds) {
  constexpr qreal padding = 12.0;
  QImage image(qCeil(scene.bounds.width() + 2.0 * padding),
               qCeil(scene.bounds.height() + 2.0 * padding),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(padding - scene.bounds.left(),
                    padding - scene.bounds.top());
  MermaidPaintOptions options;
  options.animationTimeSeconds = timeSeconds;
  flowscene::paintFlowScene(scene, painter, QStringLiteral("Arial"),
                            flowscene::PaintMode::Color, options);
  return image;
}

qsizetype differentPixels(const QImage& left, const QImage& right) {
  require(left.size() == right.size(), QStringLiteral("Image sizes differ"));
  qsizetype count = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      if (left.pixel(x, y) != right.pixel(x, y)) ++count;
    }
  }
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const flowchart::Flowchart chart = flowchart::Flowchart::parse(
      QStringLiteral(
          "flowchart LR\n"
          "A fast@--> B\n"
          "fast@{ animate: true }\n"
          "B slow@--> C\n"
          "slow@{ animation: slow }\n"
          "C --> D"));
  const QMap<QString, QSizeF> sizes =
      flowchart::measureFlowchartNodes(chart.data());
  const flowchart::FlowLayoutResult layout =
      flowchart::layoutFlowchartNodes(chart.data(), sizes);
  const flowscene::FlowScene scene = flowscene::buildFlowScene(
      chart.data(), layout,
      flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default));

  const auto fast = std::find_if(
      scene.edges.cbegin(), scene.edges.cend(),
      [](const auto& edge) { return edge.id == QLatin1String("fast"); });
  const auto slow = std::find_if(
      scene.edges.cbegin(), scene.edges.cend(),
      [](const auto& edge) { return edge.id == QLatin1String("slow"); });
  require(fast != scene.edges.cend() && fast->animated &&
              fast->animation == QLatin1String("fast"),
          QStringLiteral("animate:true did not resolve to Mermaid fast animation"));
  require(slow != scene.edges.cend() && slow->animated &&
              slow->animation == QLatin1String("slow"),
          QStringLiteral("explicit slow animation was not retained in the scene"));

  const QImage staticExportA = flowscene::renderFlowSceneToImage(scene);
  const QImage staticExportB = flowscene::renderFlowSceneToImage(scene);
  const QImage initialFrame = paintAt(scene, 0.0);
  const QImage laterFrame = paintAt(scene, 1.0);
  require(staticExportA == staticExportB,
          QStringLiteral("default flowchart export is not deterministic"));
  require(differentPixels(initialFrame, laterFrame) > 20,
          QStringLiteral("animated edge dash phase did not advance"));

  qDebug().noquote()
      << "MermaidFlowchartAnimationTest: fast/slow projection, live dash phase, and static export passed";
  return 0;
}
