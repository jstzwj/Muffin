#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/FlowScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
constexpr qreal kGeometryTolerance = 0.02;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool value, const QString& message) {
  if (!value) fail(message);
}

void near(qreal actual, qreal expected, qreal tolerance, const QString& path) {
  if (std::abs(actual - expected) > tolerance)
    fail(QStringLiteral("%1: %2 != %3")
             .arg(path)
             .arg(actual, 0, 'g', 17)
             .arg(expected, 0, 'g', 17));
}

QVector<qreal> numbers(const QString& text) {
  QVector<qreal> result;
  static const QRegularExpression re(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d+)?|\.\d+)(?:e[-+]?\d+)?)"),
      QRegularExpression::CaseInsensitiveOption);
  auto it = re.globalMatch(text);
  while (it.hasNext()) result.append(it.next().captured().toDouble());
  return result;
}

const flowscene::FlowSceneCluster* clusterById(
    const flowscene::FlowScene& scene, const QString& id) {
  const auto it = std::find_if(
      scene.clusters.cbegin(), scene.clusters.cend(), [&](const auto& cluster) {
        return cluster.id == id || id.endsWith(QLatin1Char('-') + cluster.id);
      });
  return it == scene.clusters.cend() ? nullptr : &*it;
}
}  // namespace

int main(int argc, char** argv) {
#if defined(Q_OS_MACOS)
  // The fixture goldens embed the Windows golden host's font stack; macOS
  // (SF/Helvetica) resolves different faces with different metrics.
  // Bundled-font goldens are the eventual closure.
  qWarning("skipped on macOS: goldens embed the Windows golden-host font stack");
  return 0;
#endif
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  require(argc == 2, QStringLiteral("Expected Swimlane geometry fixture"));

  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("6a2a693fb7fbf7c3bfea46f8c43e5cb738c245b6c2a4c63201d7dee0a6cf42f1"),
          QStringLiteral("Swimlane geometry bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("c0071bf69acd0b3c8b4ff72691e4b3772b2e3171c8e6faf120151196c3821643"),
          QStringLiteral("Swimlane geometry provenance changed"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 15, QStringLiteral("Swimlane geometry case count"));

  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    editor::MermaidRenderCache cache;
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready,
            id + QStringLiteral("/ready: ") + entry.errorMessage);
    const auto* scene = dynamic_cast<const flowscene::FlowScene*>(entry.scene.get());
    require(scene, id + QStringLiteral("/scene"));

    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QVector<qreal> viewBox = numbers(
        expected.value(QStringLiteral("root")).toObject()
            .value(QStringLiteral("attrs")).toObject()
            .value(QStringLiteral("viewBox")).toString());
    require(viewBox.size() == 4, id + QStringLiteral("/viewBox"));
    const qreal padding = entry.metadata.diagramPadding;
    near(scene->bounds.x(), viewBox[0] + padding, kGeometryTolerance,
         id + QStringLiteral("/x"));
    near(scene->bounds.y(), viewBox[1] + padding, kGeometryTolerance,
         id + QStringLiteral("/y"));
    near(scene->bounds.width(), viewBox[2] - 2 * padding, kGeometryTolerance,
         id + QStringLiteral("/w"));
    near(scene->bounds.height(), viewBox[3] - 2 * padding, kGeometryTolerance,
         id + QStringLiteral("/h"));

    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    if (!expectedNodes.isEmpty()) {
      require(scene->nodes.size() == expectedNodes.size(),
              id + QStringLiteral("/node-count"));
      for (qsizetype i = 0; i < scene->nodes.size(); ++i) {
        const auto& node = scene->nodes.at(i);
        const QJsonObject oracle = expectedNodes.at(i).toObject();
        const QVector<qreal> transform = numbers(oracle.value(QStringLiteral("transform")).toString());
        const QJsonObject box = oracle.value(QStringLiteral("bbox")).toObject();
        near(node.cx, transform.value(0), kGeometryTolerance,
             id + QStringLiteral("/node%1/x").arg(i));
        near(node.cy, transform.value(1), kGeometryTolerance,
             id + QStringLiteral("/node%1/y").arg(i));
        const QSizeF paintedSize = id.startsWith(QLatin1String("look-hand")) &&
                                           node.paintedBounds.isValid()
            ? node.paintedBounds.size()
            : QSizeF(node.width, node.height);
        near(paintedSize.width(), box.value(QStringLiteral("width")).toDouble(), kGeometryTolerance,
             id + QStringLiteral("/node%1/w").arg(i));
        near(paintedSize.height(), box.value(QStringLiteral("height")).toDouble(), kGeometryTolerance,
             id + QStringLiteral("/node%1/h").arg(i));
      }
    }

    const QJsonArray expectedClusters = expected.value(QStringLiteral("clusters")).toArray();
    require(scene->clusters.size() == expectedClusters.size(),
            id + QStringLiteral("/cluster-count"));
    for (const QJsonValue& clusterValue : expectedClusters) {
      const QJsonObject oracle = clusterValue.toObject();
      const auto* cluster = clusterById(*scene, oracle.value(QStringLiteral("id")).toString());
      require(cluster, id + QStringLiteral("/cluster-id"));
      const QJsonObject box = oracle.value(QStringLiteral("bbox")).toObject();
      const QRectF actual = id.startsWith(QLatin1String("look-hand")) &&
                                  cluster->paintedBounds.isValid()
          ? cluster->paintedBounds
          : QRectF(cluster->cx - cluster->width / 2, cluster->cy - cluster->height / 2,
                   cluster->width, cluster->height);
      near(actual.center().x(), box.value(QStringLiteral("x")).toDouble() +
                                    box.value(QStringLiteral("width")).toDouble() / 2,
           kGeometryTolerance, id + QStringLiteral("/cluster-x"));
      near(actual.center().y(), box.value(QStringLiteral("y")).toDouble() +
                                    box.value(QStringLiteral("height")).toDouble() / 2,
           kGeometryTolerance, id + QStringLiteral("/cluster-y"));
      near(actual.width(), box.value(QStringLiteral("width")).toDouble(),
           kGeometryTolerance, id + QStringLiteral("/cluster-w"));
      near(actual.height(), box.value(QStringLiteral("height")).toDouble(),
           kGeometryTolerance, id + QStringLiteral("/cluster-h"));

      const QJsonArray rects = oracle.value(QStringLiteral("rects")).toArray();
      if (oracle.value(QStringLiteral("class")).toString().split(QLatin1Char(' '))
              .contains(QStringLiteral("swimlane")) && !rects.isEmpty()) {
        const QJsonObject title = rects.last().toObject()
                                      .value(QStringLiteral("bbox")).toObject();
        near(cluster->titleBandSize,
             cluster->titleOnLeft ? title.value(QStringLiteral("width")).toDouble()
                                  : title.value(QStringLiteral("height")).toDouble(),
             kGeometryTolerance, id + QStringLiteral("/title-band"));
      }
    }

    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    require(scene->edges.size() == expectedEdges.size(),
            id + QStringLiteral("/edge-count"));
    for (qsizetype i = 0; i < scene->edges.size(); ++i) {
      const QVector<qreal> actual = numbers(scene->edges.at(i).path);
      const QVector<qreal> oracle = numbers(
          expectedEdges.at(i).toObject().value(QStringLiteral("attrs")).toObject()
              .value(QStringLiteral("d")).toString());
      require(actual.size() == oracle.size(),
              id + QStringLiteral("/edge%1-structure").arg(i));
      for (qsizetype j = 0; j < actual.size(); ++j)
        near(actual[j], oracle[j], 0.001,
             id + QStringLiteral("/edge%1/%2").arg(i).arg(j));
    }
  }

  std::puts("MermaidSwimlaneGeometryOracleTest: 15/15 passed");
  return 0;
}
