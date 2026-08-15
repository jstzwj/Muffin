#pragma once

#include "mermaid/architecture/ArchitectureScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace architecture_test {

using namespace muffin::mermaid;

[[noreturn]] inline void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

inline void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

inline void near(qreal actual, qreal expected, const QString& path,
                 qreal tolerance = 1e-6) {
  require(std::isfinite(actual) && std::isfinite(expected) &&
              std::abs(actual - expected) <= tolerance,
          QStringLiteral("%1: %2 != %3")
              .arg(path)
              .arg(actual, 0, 'g', 17)
              .arg(expected, 0, 'g', 17));
}

inline QVector<qreal> numbers(const QString& source) {
  static const QRegularExpression expression(
      QStringLiteral(R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[-+]?\d+)?)"),
      QRegularExpression::CaseInsensitiveOption);
  QVector<qreal> result;
  auto matches = expression.globalMatch(source);
  while (matches.hasNext())
    result.append(matches.next().captured().toDouble());
  return result;
}

inline qreal px(QString source) {
  source = source.trimmed();
  if (source.endsWith(QLatin1String("px"))) source.chop(2);
  return source.toDouble();
}

inline QColor browserColor(QString source) {
  source = source.trimmed();
  static const QRegularExpression expression(QStringLiteral(
      R"(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*([\d.]+))?)"));
  const auto match = expression.match(source);
  if (match.hasMatch()) {
    QColor color(match.captured(1).toInt(), match.captured(2).toInt(),
                 match.captured(3).toInt());
    if (!match.captured(4).isEmpty())
      color.setAlphaF(match.captured(4).toDouble());
    return color;
  }
  return color::toQColor(source);
}

inline void sameColor(const QString& actual, const QString& expected,
                      const QString& path,
                      color::SvgPaintKind kind = color::SvgPaintKind::Stroke) {
  const auto resolved = color::resolveSvgPaint(actual, kind, Qt::black);
  if (expected.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) {
    require(resolved.none, path + QStringLiteral(": expected no paint"));
    return;
  }
  const QColor browser = browserColor(expected);
  require(!resolved.none && browser.isValid() &&
              resolved.color.rgba() == browser.rgba(),
          path + QStringLiteral(": ") + actual + QStringLiteral(" != ") +
              expected);
}

struct Rendered {
  std::shared_ptr<const architecture::ArchitectureScene> scene;
  editor::MermaidRenderEntry entry;
};

inline Rendered render(const QString& source) {
  editor::MermaidRenderCache cache;
  Rendered result;
  result.entry = cache.getSync(editor::MermaidRenderCache::makeKey(source), source);
  require(result.entry.status == editor::MermaidRenderStatus::Ready &&
              result.entry.scene,
          QStringLiteral("Architecture render failed: ") +
              result.entry.errorMessage);
  result.scene = std::dynamic_pointer_cast<const architecture::ArchitectureScene>(
      result.entry.scene);
  require(bool(result.scene),
          QStringLiteral("Architecture entry has wrong scene type"));
  return result;
}

inline QJsonObject expectedLayer(const QJsonObject& expected,
                                 const QString& className) {
  for (const QJsonValue& value : expected.value(QStringLiteral("layers")).toArray()) {
    const QJsonObject layer = value.toObject();
    if (layer.value(QStringLiteral("class")).toString() == className)
      return layer;
  }
  return {};
}

inline QVector<QJsonObject> expectedPrimitives(const QJsonObject& expected,
                                               const QString& tag,
                                               const QString& className) {
  QVector<QJsonObject> result;
  for (const QJsonValue& value :
       expected.value(QStringLiteral("primitives")).toArray()) {
    const QJsonObject primitive = value.toObject();
    if (primitive.value(QStringLiteral("tag")).toString() == tag &&
        primitive.value(QStringLiteral("attrs")).toObject()
                .value(QStringLiteral("class")).toString() == className)
      result.append(primitive);
  }
  return result;
}

inline void compareRect(const QRectF& actual, const QJsonObject& expected,
                        const QString& path, qreal tolerance = 1e-5) {
  near(actual.x(), expected.value(QStringLiteral("x")).toDouble(),
       path + QStringLiteral("/x"), tolerance);
  near(actual.y(), expected.value(QStringLiteral("y")).toDouble(),
       path + QStringLiteral("/y"), tolerance);
  near(actual.width(), expected.value(QStringLiteral("width")).toDouble(),
       path + QStringLiteral("/width"), tolerance);
  near(actual.height(), expected.value(QStringLiteral("height")).toDouble(),
       path + QStringLiteral("/height"), tolerance);
}

inline qreal float32UlpTolerance(qreal expected, int ulps = 2) {
  const float center = float(expected);
  float lower = center;
  float upper = center;
  for (int i = 0; i < ulps; ++i) {
    lower = std::nextafter(lower, -std::numeric_limits<float>::infinity());
    upper = std::nextafter(upper, std::numeric_limits<float>::infinity());
  }
  return std::max(qreal(center - lower), qreal(upper - center)) + 1e-12;
}

inline void compareGeometryCase(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const Rendered rendered =
      render(fixture.value(QStringLiteral("source")).toString());
  const architecture::ArchitectureScene& scene = *rendered.scene;
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  require(!expected.isEmpty(), id + QStringLiteral("/missing-upstream-result"));

  const QJsonObject root = expected.value(QStringLiteral("root")).toObject();
  const QJsonObject rootAttrs = root.value(QStringLiteral("attrs")).toObject();
  const QVector<qreal> viewBox =
      numbers(rootAttrs.value(QStringLiteral("viewBox")).toString());
  require(viewBox.size() == 4, id + QStringLiteral("/viewBox-shape"));
  near(scene.bounds.x(), viewBox.at(0), id + QStringLiteral("/viewBox/x"),
       float32UlpTolerance(viewBox.at(0)));
  near(scene.bounds.y(), viewBox.at(1), id + QStringLiteral("/viewBox/y"),
       float32UlpTolerance(viewBox.at(1)));
  near(scene.bounds.width(), viewBox.at(2), id + QStringLiteral("/viewBox/w"),
       float32UlpTolerance(viewBox.at(2)));
  near(scene.bounds.height(), viewBox.at(3), id + QStringLiteral("/viewBox/h"),
       float32UlpTolerance(viewBox.at(3)));
  const bool expectedMaxWidth =
      rootAttrs.value(QStringLiteral("width")).toString() == QLatin1String("100%");
  require(scene.useMaxWidth == expectedMaxWidth &&
              rendered.entry.metadata.svgUseMaxWidth == expectedMaxWidth,
          id + QStringLiteral("/useMaxWidth"));

  const QJsonArray nodeChildren =
      expectedLayer(expected, QStringLiteral("architecture-services"))
          .value(QStringLiteral("children")).toArray();
  require(scene.nodes.size() == nodeChildren.size(),
          id + QStringLiteral("/node-count %1 != %2")
                   .arg(scene.nodes.size()).arg(nodeChildren.size()));
  for (qsizetype i = 0; i < nodeChildren.size(); ++i) {
    const QJsonObject oracle = nodeChildren.at(i).toObject();
    const QVector<qreal> transform =
        numbers(oracle.value(QStringLiteral("transform")).toString());
    require(transform.size() == 2,
            id + QStringLiteral("/node/%1/transform").arg(i));
    const architecture::ArchitectureNodeGeometry& node = scene.nodes.at(i);
    near(node.topLeft.x(), transform.at(0),
         id + QStringLiteral("/node/%1/x").arg(i), 1e-6);
    near(node.topLeft.y(), transform.at(1),
         id + QStringLiteral("/node/%1/y").arg(i), 1e-6);
    const bool junction =
        oracle.value(QStringLiteral("class")).toString() ==
        QLatin1String("architecture-junction");
    require((node.kind == architecture::ArchitectureNodeKind::Junction) == junction,
            id + QStringLiteral("/node/%1/kind").arg(i));
  }

  const QVector<QJsonObject> groupRects =
      expectedPrimitives(expected, QStringLiteral("rect"),
                         QStringLiteral("node-bkg"));
  require(scene.groups.size() == groupRects.size(),
          id + QStringLiteral("/group-count"));
  for (qsizetype i = 0; i < groupRects.size(); ++i) {
    const QJsonObject attrs =
        groupRects.at(i).value(QStringLiteral("attrs")).toObject();
    QJsonObject rect{{QStringLiteral("x"), attrs.value(QStringLiteral("x")).toString().toDouble()},
                     {QStringLiteral("y"), attrs.value(QStringLiteral("y")).toString().toDouble()},
                     {QStringLiteral("width"), attrs.value(QStringLiteral("width")).toString().toDouble()},
                     {QStringLiteral("height"), attrs.value(QStringLiteral("height")).toString().toDouble()}};
    compareRect(scene.groups.at(i).rect, rect,
                id + QStringLiteral("/group/%1").arg(i));
  }

  const QVector<QJsonObject> edgePaths =
      expectedPrimitives(expected, QStringLiteral("path"),
                         QStringLiteral("edge"));
  require(scene.edges.size() == edgePaths.size(),
          id + QStringLiteral("/edge-count %1 != %2")
                   .arg(scene.edges.size()).arg(edgePaths.size()));
  for (qsizetype i = 0; i < edgePaths.size(); ++i) {
    const QVector<qreal> actual = numbers(scene.edges.at(i).pathData);
    const QVector<qreal> oracle = numbers(
        edgePaths.at(i).value(QStringLiteral("attrs")).toObject()
            .value(QStringLiteral("d")).toString());
    require(actual.size() == oracle.size(),
            id + QStringLiteral("/edge/%1/path-shape").arg(i));
    for (qsizetype n = 0; n < actual.size(); ++n)
      near(actual.at(n), oracle.at(n),
           id + QStringLiteral("/edge/%1/path/%2").arg(i).arg(n), 1e-6);
  }

  const QVector<QJsonObject> arrowPolygons =
      expectedPrimitives(expected, QStringLiteral("polygon"),
                         QStringLiteral("arrow"));
  QVector<const architecture::ArchitectureArrowGeometry*> arrows;
  for (const auto& edge : scene.edges)
    for (const auto& arrow : edge.arrows) arrows.append(&arrow);
  require(arrows.size() == arrowPolygons.size(),
          id + QStringLiteral("/arrow-count"));
  for (qsizetype i = 0; i < arrows.size(); ++i) {
    const QJsonObject attrs =
        arrowPolygons.at(i).value(QStringLiteral("attrs")).toObject();
    const QVector<qreal> transform =
        numbers(attrs.value(QStringLiteral("transform")).toString());
    const QVector<qreal> points =
        numbers(attrs.value(QStringLiteral("points")).toString());
    require(transform.size() == 2 && points.size() == 6,
            id + QStringLiteral("/arrow/%1/shape").arg(i));
    near(arrows.at(i)->position.x(), transform.at(0),
         id + QStringLiteral("/arrow/%1/x").arg(i));
    near(arrows.at(i)->position.y(), transform.at(1),
         id + QStringLiteral("/arrow/%1/y").arg(i));
    for (int pointIndex = 0; pointIndex < 3; ++pointIndex) {
      near(arrows.at(i)->polygon.at(pointIndex).x(), points.at(pointIndex * 2),
           id + QStringLiteral("/arrow/%1/point/%2/x").arg(i).arg(pointIndex));
      near(arrows.at(i)->polygon.at(pointIndex).y(), points.at(pointIndex * 2 + 1),
           id + QStringLiteral("/arrow/%1/point/%2/y").arg(i).arg(pointIndex));
    }
  }
}

inline void compareConfigCase(const QJsonObject& fixture) {
  const QString id = fixture.value(QStringLiteral("id")).toString();
  const Rendered rendered =
      render(fixture.value(QStringLiteral("source")).toString());
  const architecture::ArchitectureScene& scene = *rendered.scene;
  const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
  require(!expected.isEmpty(), id + QStringLiteral("/missing-upstream-result"));
  compareGeometryCase(fixture);

  const QJsonObject expectedConfig =
      expected.value(QStringLiteral("db")).toObject()
          .value(QStringLiteral("config")).toObject();
  const QVector<QPair<QString, QJsonValue>> actualConfig = {
      {QStringLiteral("useMaxWidth"), scene.config.useMaxWidth},
      {QStringLiteral("padding"), scene.config.padding},
      {QStringLiteral("iconSize"), scene.config.iconSize},
      {QStringLiteral("fontSize"), scene.config.fontSize},
      {QStringLiteral("randomize"), scene.config.randomize},
      {QStringLiteral("nodeSeparation"), scene.config.nodeSeparation},
      {QStringLiteral("idealEdgeLengthMultiplier"), scene.config.idealEdgeLengthMultiplier},
      {QStringLiteral("edgeElasticity"), scene.config.edgeElasticity},
      {QStringLiteral("numIter"), scene.config.numIter},
      {QStringLiteral("seed"), scene.config.seed},
  };
  for (const auto& [key, actual] : actualConfig)
    require(actual == expectedConfig.value(key), id + QStringLiteral("/config/") + key);

  const QVector<QJsonObject> edgePaths =
      expectedPrimitives(expected, QStringLiteral("path"),
                         QStringLiteral("edge"));
  if (!edgePaths.isEmpty()) {
    const QJsonObject computed =
        edgePaths.front().value(QStringLiteral("computed")).toObject();
    sameColor(scene.style.edgeColor,
              computed.value(QStringLiteral("stroke")).toString(),
              id + QStringLiteral("/style/edgeColor"));
    const qreal diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                           std::sqrt(2.0);
    const qreal usedWidth = editor::cssStrokeWidthPx(
        scene.style.edgeWidth,
        editor::pieCssLengthContext(scene.style.fontFamily,
                                    editor::jsNumberValue(scene.config.fontSize)),
        diagonal);
    near(usedWidth, px(computed.value(QStringLiteral("strokeWidth")).toString()),
         id + QStringLiteral("/style/edgeWidth"), .001);
    require(computed.value(QStringLiteral("fontFamily")).toString()
                .contains(editor::firstFontFamily(scene.style.fontFamily),
                          Qt::CaseInsensitive),
            id + QStringLiteral("/style/fontFamily"));
  }
  const QVector<QJsonObject> arrowPolygons =
      expectedPrimitives(expected, QStringLiteral("polygon"),
                         QStringLiteral("arrow"));
  if (!arrowPolygons.isEmpty())
    sameColor(scene.style.arrowColor,
              arrowPolygons.front().value(QStringLiteral("computed")).toObject()
                  .value(QStringLiteral("fill")).toString(),
              id + QStringLiteral("/style/arrowColor"),
              color::SvgPaintKind::Fill);
  const QVector<QJsonObject> groups =
      expectedPrimitives(expected, QStringLiteral("rect"),
                         QStringLiteral("node-bkg"));
  if (!groups.isEmpty()) {
    const QJsonObject computed =
        groups.front().value(QStringLiteral("computed")).toObject();
    sameColor(scene.style.groupBorderColor,
              computed.value(QStringLiteral("stroke")).toString(),
              id + QStringLiteral("/style/groupBorderColor"));
    const qreal diagonal = std::hypot(scene.bounds.width(), scene.bounds.height()) /
                           std::sqrt(2.0);
    near(editor::cssStrokeWidthPx(
             scene.style.groupBorderWidth,
             editor::pieCssLengthContext(
                 scene.style.fontFamily,
                 editor::jsNumberValue(scene.config.fontSize)),
             diagonal),
         px(computed.value(QStringLiteral("strokeWidth")).toString()),
         id + QStringLiteral("/style/groupBorderWidth"), .001);
  }
}

}  // namespace architecture_test
