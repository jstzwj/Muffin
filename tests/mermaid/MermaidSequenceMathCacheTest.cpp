#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/sequence/SequenceScenePainter.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QGuiApplication>

#include <cstdlib>

namespace {

[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

QString stressSource(int messageCount) {
  QString source = QStringLiteral(
      "sequenceDiagram\nparticipant A\nparticipant B\n");
  source.reserve(source.size() + messageCount * 50);
  for (int index = 0; index < messageCount; ++index) {
    source += QStringLiteral(
                  "A->>B: $$\\hat{\\frac{x_%1}{\\sqrt{y_%2}}}$$\n")
                  .arg(index)
                  .arg(index + 1);
  }
  return source;
}

}  // namespace

int main(int argc, char** argv) {
  if (qgetenv("QT_QPA_PLATFORM").isEmpty())
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  using namespace muffin::mermaid;
  constexpr int kMessageCount = 160;
  constexpr qint64 kTimeLimitMs = 2000;
  const QString source = stressSource(kMessageCount);
  editor::MermaidRenderCache cache;
  const auto key = editor::MermaidRenderCache::makeKey(source);

  QElapsedTimer buildTimer;
  buildTimer.start();
  const editor::MermaidRenderEntry entry = cache.getSync(key, source);
  const qint64 buildMs = buildTimer.elapsed();
  const auto* sequenceScene = dynamic_cast<const sequence::SequenceScene*>(entry.scene.get());
  require(entry.status == editor::MermaidRenderStatus::Ready &&
              sequenceScene != nullptr,
          QStringLiteral("sequence Math cache stress scene failed: %1")
              .arg(entry.errorMessage));
  require(buildMs < kTimeLimitMs,
          QStringLiteral("sequence Math first build exceeded %1 ms: %2 ms")
              .arg(kTimeLimitMs)
              .arg(buildMs));
  require(sequenceScene->messageLabels.size() == kMessageCount,
          QStringLiteral("sequence Math stress message count drifted"));

  QVector<const flowchart::FlowLabelPreparedMath*> prepared;
  prepared.reserve(kMessageCount);
  for (const auto& label : sequenceScene->messageLabels) {
    require(label.richText.math.size() == 1 &&
                label.richText.math.front().prepared,
            QStringLiteral("stress label is missing its prepared operation"));
    prepared.push_back(label.richText.math.front().prepared.get());
  }

  QElapsedTimer repaintTimer;
  repaintTimer.start();
  const QImage first = sequence::renderSequenceSceneToImage(
      *sequenceScene, 1.0, 0.0);
  const QImage second = sequence::renderSequenceSceneToImage(
      *sequenceScene, 1.0, 0.0);
  const QImage third = sequence::renderSequenceSceneToImage(
      *sequenceScene, 1.0, 0.0);
  const qint64 repaintMs = repaintTimer.elapsed();
  require(repaintMs < kTimeLimitMs,
          QStringLiteral("three sequence Math repaints exceeded %1 ms: %2 ms")
              .arg(kTimeLimitMs)
              .arg(repaintMs));
  require(!first.isNull() && first == second && second == third,
          QStringLiteral("prepared sequence Math repaint is not deterministic"));

  const auto cached = cache.getSync(key, source);
  require(cached.scene == entry.scene,
          QStringLiteral("sequence Math cache hit replaced the scene"));
  const QImage highDpr = sequence::renderSequenceSceneToImage(
      *sequenceScene, 1.5, 0.0);
  require(!highDpr.isNull(),
          QStringLiteral("prepared sequence Math failed at 1.5x DPR"));
  for (qsizetype index = 0; index < prepared.size(); ++index) {
    require(sequenceScene->messageLabels.at(index)
                    .richText.math.front().prepared.get() == prepared.at(index),
            QStringLiteral("paint or DPR change rebuilt operation %1").arg(index));
  }

  qInfo() << "MermaidSequenceMathCacheTest:" << kMessageCount
          << "prepared messages, build" << buildMs << "ms, three repaints"
          << repaintMs << "ms";
  return 0;
}
