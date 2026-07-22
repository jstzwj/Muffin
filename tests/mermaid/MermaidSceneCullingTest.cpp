#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidPaintOptions.h"
#include "mermaid/classdiagram/ClassScenePainter.h"
#include "mermaid/scene/FlowScenePainter.h"
#include "mermaid/sequence/SequenceScenePainter.h"
#include "mermaid/state/StateScenePainter.h"

#include <QElapsedTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <cstdlib>
#include <functional>
#include <utility>

using namespace muffin::mermaid;

namespace {

constexpr int kItemCount = 1200;
const QRectF kViewport(0.0, 0.0, 240.0, 320.0);

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

struct PaintResult {
  QImage image;
  MermaidPaintStats stats;
  qint64 elapsedNanoseconds = 0;
};

PaintResult renderScene(
    bool cull,
    const std::function<void(QPainter&, const MermaidPaintOptions&)>& paint) {
  PaintResult result;
  result.image = QImage(kViewport.size().toSize(),
                        QImage::Format_ARGB32_Premultiplied);
  result.image.fill(Qt::transparent);
  MermaidPaintOptions options;
  options.cullToVisibleRect = cull;
  options.visibleSceneRect = kViewport;
  options.overscan = 8.0;
  options.stats = &result.stats;

  QPainter painter(&result.image);
  painter.setClipRect(kViewport);
  QElapsedTimer timer;
  timer.start();
  paint(painter, options);
  result.elapsedNanoseconds = timer.nsecsElapsed();
  painter.end();
  return result;
}

void verifyFamily(
    const QString& family, qsizetype expectedPrimitives,
    const std::function<void(QPainter&, const MermaidPaintOptions&)>& paint) {
  const PaintResult full = renderScene(false, paint);
  const PaintResult culled = renderScene(true, paint);

  require(full.stats.consideredPrimitives == expectedPrimitives &&
              full.stats.paintedPrimitives == expectedPrimitives &&
              full.stats.culledPrimitives == 0,
          QStringLiteral("%1: full/export paint must visit every primitive")
              .arg(family));
  require(culled.stats.consideredPrimitives == expectedPrimitives &&
              culled.stats.paintedPrimitives <= 40 &&
              culled.stats.culledPrimitives >= expectedPrimitives - 40,
          QStringLiteral("%1: viewport paint did not bound expensive work "
                         "(painted %2/%3)")
              .arg(family)
              .arg(culled.stats.paintedPrimitives)
              .arg(expectedPrimitives));
  require(culled.stats.consideredPrimitives ==
              culled.stats.paintedPrimitives + culled.stats.culledPrimitives,
          QStringLiteral("%1: culling counters are inconsistent").arg(family));
  require(full.image == culled.image,
          QStringLiteral("%1: culling changed visible pixels").arg(family));

  qInfo().noquote()
      << QStringLiteral("%1: full=%2 ms, culled=%3 ms, painted=%4/%5")
             .arg(family)
             .arg(full.elapsedNanoseconds / 1000000.0, 0, 'f', 3)
             .arg(culled.elapsedNanoseconds / 1000000.0, 0, 'f', 3)
             .arg(culled.stats.paintedPrimitives)
             .arg(expectedPrimitives);
}

flowscene::FlowScene makeFlowScene() {
  flowscene::FlowScene scene;
  scene.bounds = QRectF(0.0, 0.0, 240.0, kItemCount * 48.0);
  for (int index = 0; index < kItemCount; ++index) {
    const qreal y = 16.0 + index * 48.0;
    flowscene::FlowSceneEdge edge;
    edge.id = QStringLiteral("e%1").arg(index);
    edge.path = QStringLiteral("M 8 %1 L 232 %1").arg(y);
    edge.pathBounds = QRectF(8.0, y - 1.0, 224.0, 2.0);
    edge.stroke = QStringLiteral("#444444");
    edge.strokeWidth = QStringLiteral("1px");
    scene.edges.append(std::move(edge));

    flowscene::FlowSceneNode node;
    node.id = QStringLiteral("n%1").arg(index);
    node.shapeType = QStringLiteral("rect");
    node.shapeKind = QStringLiteral("rect");
    node.cx = 120.0;
    node.cy = y;
    node.width = 96.0;
    node.height = 28.0;
    node.fill = QStringLiteral("#ececff");
    node.stroke = QStringLiteral("#9370db");
    node.strokeWidth = QStringLiteral("1px");
    scene.nodes.append(std::move(node));
  }
  return scene;
}

sequence::SequenceScene makeSequenceScene() {
  sequence::SequenceScene scene;
  scene.bounds = QRectF(0.0, 0.0, 240.0, kItemCount * 48.0);
  for (int index = 0; index < kItemCount; ++index) {
    const qreal y = 16.0 + index * 48.0;
    sequence::SequenceLayoutNote note;
    note.messageIndex = index;
    note.rect = QRectF(72.0, y - 14.0, 96.0, 28.0);
    scene.notes.append(std::move(note));
    scene.noteLabels.append(sequence::SequenceLabelDocument{});

    sequence::SequenceLayoutMessage message;
    message.messageIndex = index;
    message.startX = 8.0;
    message.stopX = 232.0;
    message.lineY = y;
    scene.messages.append(std::move(message));
    scene.messageLabels.append(sequence::SequenceLabelDocument{});
  }
  return scene;
}

classdiagram::ClassScene makeClassScene() {
  classdiagram::ClassScene scene;
  scene.bounds = QRectF(0.0, 0.0, 240.0, kItemCount * 48.0);
  for (int index = 0; index < kItemCount; ++index) {
    const qreal y = 16.0 + index * 48.0;
    classdiagram::ClassSceneEdge edge;
    edge.id = QStringLiteral("e%1").arg(index);
    edge.paths.append(QStringLiteral("M 8 %1 L 232 %1").arg(y));
    edge.pathBounds = QRectF(8.0, y - 1.0, 224.0, 2.0);
    scene.edges.append(std::move(edge));

    classdiagram::ClassSceneNode node;
    node.id = QStringLiteral("n%1").arg(index);
    node.center = QPointF(120.0, y);
    node.size = QSizeF(96.0, 28.0);
    node.localOuter = QRectF(-48.0, -14.0, 96.0, 28.0);
    node.fill = QStringLiteral("#ececff");
    node.stroke = QStringLiteral("#9370db");
    node.textColor = QStringLiteral("#333333");
    scene.nodes.append(std::move(node));
  }
  return scene;
}

state::StateScene makeStateScene() {
  state::StateScene scene;
  scene.bounds = QRectF(0.0, 0.0, 240.0, kItemCount * 48.0);
  for (int index = 0; index < kItemCount; ++index) {
    const qreal y = 16.0 + index * 48.0;
    state::StateSceneEdge edge;
    edge.id = QStringLiteral("e%1").arg(index);
    edge.path = QStringLiteral("M 8 %1 L 232 %1").arg(y);
    edge.points = {QPointF(8.0, y), QPointF(232.0, y)};
    edge.pathBounds = QRectF(8.0, y - 1.0, 224.0, 2.0);
    scene.edges.append(std::move(edge));

    state::StateSceneNode node;
    node.id = QStringLiteral("n%1").arg(index);
    node.shape = QStringLiteral("rect");
    node.bounds = QRectF(72.0, y - 14.0, 96.0, 28.0);
    scene.nodes.append(std::move(node));
  }
  return scene;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  constexpr qsizetype expectedPrimitives = kItemCount * 2;

  const flowscene::FlowScene flow = makeFlowScene();
  verifyFamily(QStringLiteral("flowchart"), expectedPrimitives,
      [&](QPainter& painter, const MermaidPaintOptions& options) {
        flowscene::paintFlowScene(flow, painter, QStringLiteral("Arial"),
                                  flowscene::PaintMode::Color, options);
      });

  const sequence::SequenceScene sequenceScene = makeSequenceScene();
  verifyFamily(QStringLiteral("sequence"), expectedPrimitives,
      [&](QPainter& painter, const MermaidPaintOptions& options) {
        sequence::paintSequenceScene(sequenceScene, painter, options);
      });

  const classdiagram::ClassScene classScene = makeClassScene();
  verifyFamily(QStringLiteral("class"), expectedPrimitives,
      [&](QPainter& painter, const MermaidPaintOptions& options) {
        classdiagram::paintClassScene(
            classScene, painter, classdiagram::ClassPaintMode::Color, options);
      });

  const state::StateScene stateScene = makeStateScene();
  verifyFamily(QStringLiteral("state"), expectedPrimitives,
      [&](QPainter& painter, const MermaidPaintOptions& options) {
        state::paintStateScene(stateScene, painter, options);
      });
  return 0;
}
