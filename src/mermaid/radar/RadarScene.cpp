#include "mermaid/radar/RadarScene.h"
#include "mermaid/radar/RadarScenePainter.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::radar {

void RadarScene::paint(QPainter& painter,
                       const MermaidPaintOptions& paintOptions) const {
  paintRadarScene(*this, painter, paintOptions);
}

namespace {

constexpr qreal kPi = 3.141592653589793238462643383279502884;

QPointF polar(qreal radius, int index, int count) {
  const qreal angle = 2.0 * kPi * index / count - kPi / 2.0;
  return QPointF(radius * std::cos(angle), radius * std::sin(angle));
}

double relativeRadius(double value, double minValue, double maxValue,
                      double radius) {
  const double clipped = std::min(std::max(value, minValue), maxValue);
  return radius * (clipped - minValue) / (maxValue - minValue);
}

QJsonValue numberJson(double value) {
  if (std::isfinite(value)) return value;
  if (std::isnan(value)) return QStringLiteral("NaN");
  return value > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
}

QJsonArray pointJson(const QPointF& point) {
  return {numberJson(point.x()), numberJson(point.y())};
}

QString anchorName(RadarTextAnchor anchor) {
  if (anchor == RadarTextAnchor::Start) return QStringLiteral("start");
  if (anchor == RadarTextAnchor::End) return QStringLiteral("end");
  return QStringLiteral("middle");
}

QString baselineName(RadarBaseline baseline) {
  if (baseline == RadarBaseline::Auto) return QStringLiteral("auto");
  if (baseline == RadarBaseline::Hanging) return QStringLiteral("hanging");
  return QStringLiteral("central");
}

}  // namespace

RadarScene buildRadarScene(const RadarData& data, RadarConfig config,
                           RadarSceneStyle style) {
  RadarScene scene;
  scene.config = config;
  scene.style = std::move(style);
  scene.options = data.options;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  const qreal totalWidth = config.totalWidth.value_or(
      config.width + config.marginLeft + config.marginRight);
  const qreal totalHeight = config.totalHeight.value_or(
      config.height + config.marginTop + config.marginBottom);
  scene.bounds = QRectF(0.0, 0.0, totalWidth, totalHeight);
  scene.center = QPointF(
      config.centerX.value_or(config.marginLeft + config.width / 2.0),
      config.centerY.value_or(config.marginTop + config.height / 2.0));
  scene.radius = std::min(config.width, config.height) / 2.0;

  const int axisCount = data.axes.size();
  const double ringCountValue = std::ceil(data.options.ticks);
  const int ringCount = std::isfinite(ringCountValue) && ringCountValue > 0.0 &&
                                ringCountValue <= std::numeric_limits<int>::max()
                            ? static_cast<int>(ringCountValue)
                            : 0;
  for (int i = 0; i < ringCount; ++i) {
    RadarGraticuleGeometry ring;
    ring.circle = data.options.graticule == QLatin1String("circle");
    ring.radius = scene.radius * (i + 1) / data.options.ticks;
    if (!ring.circle && axisCount > 0)
      for (int j = 0; j < axisCount; ++j)
        ring.points.append(polar(ring.radius, j, axisCount));
    scene.graticules.append(std::move(ring));
  }

  for (int i = 0; i < axisCount; ++i) {
    const qreal angle = 2.0 * kPi * i / axisCount - kPi / 2.0;
    const qreal cosA = std::cos(angle);
    const qreal sinA = std::sin(angle);
    RadarAxisGeometry axis;
    axis.name = data.axes.at(i).name;
    axis.label = data.axes.at(i).label;
    axis.end = QPointF(scene.radius * config.axisScaleFactor * cosA,
                       scene.radius * config.axisScaleFactor * sinA);
    axis.labelPosition = QPointF(
        scene.radius * config.axisLabelFactor * cosA + 4.0 * cosA,
        scene.radius * config.axisLabelFactor * sinA + 4.0 * sinA);
    axis.textAnchor = cosA > 0.01 ? RadarTextAnchor::Start
                      : cosA < -0.01 ? RadarTextAnchor::End
                                     : RadarTextAnchor::Middle;
    axis.baseline = sinA > 0.01 ? RadarBaseline::Hanging
                    : sinA < -0.01 ? RadarBaseline::Auto
                                   : RadarBaseline::Central;
    scene.axes.append(std::move(axis));
  }

  double maxValue = data.options.max;
  if (!data.options.hasMax) {
    maxValue = -std::numeric_limits<double>::infinity();
    for (const RadarCurve& curve : data.curves)
      for (double entry : curve.entries) maxValue = std::max(maxValue, entry);
  }
  for (int index = 0; index < data.curves.size(); ++index) {
    const RadarCurve& source = data.curves.at(index);
    if (source.entries.size() != axisCount) continue;
    RadarCurveGeometry curve;
    curve.name = source.name;
    curve.label = source.label;
    curve.colorIndex = index;
    curve.classGenerated = index >= 0 && index < scene.style.themeColorLimit;
    curve.color = scene.style.palette.value(index);
    curve.polygon = data.options.graticule == QLatin1String("polygon");
    for (int i = 0; i < axisCount; ++i)
      curve.points.append(polar(relativeRadius(source.entries.at(i), data.options.min,
                                               maxValue, scene.radius),
                                i, axisCount));
    if (!curve.polygon && !curve.points.isEmpty()) {
      for (int i = 0; i < curve.points.size(); ++i) {
        const QPointF& p0 = curve.points.at((i - 1 + curve.points.size()) % curve.points.size());
        const QPointF& p1 = curve.points.at(i);
        const QPointF& p2 = curve.points.at((i + 1) % curve.points.size());
        const QPointF& p3 = curve.points.at((i + 2) % curve.points.size());
        curve.cubics.append({p1 + (p2 - p0) * config.curveTension,
                             p2 - (p3 - p1) * config.curveTension, p2});
      }
    }
    scene.curves.append(std::move(curve));
  }

  if (data.options.showLegend) {
    const QPointF first(
        config.legendX.value_or(
            (config.width / 2.0 + config.marginRight) * 3.0 / 4.0),
        config.legendY.value_or(
            -(config.height / 2.0 + config.marginTop) * 3.0 / 4.0));
    for (int index = 0; index < data.curves.size(); ++index) {
      RadarLegendGeometry legend;
      legend.text = data.curves.at(index).label;
      legend.colorIndex = index;
      legend.classGenerated = index >= 0 && index < scene.style.themeColorLimit;
      legend.color = scene.style.palette.value(index);
      legend.position = first + QPointF(0.0, index * 20.0);
      scene.legends.append(std::move(legend));
    }
  }
  return scene;
}

QJsonObject RadarScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
  root[QStringLiteral("center")] = pointJson(center);
  root[QStringLiteral("radius")] = numberJson(radius);
  root[QStringLiteral("title")] = title;
  root[QStringLiteral("graticule")] = options.graticule;
  root[QStringLiteral("ticks")] = numberJson(options.ticks);
  root[QStringLiteral("min")] = numberJson(options.min);
  root[QStringLiteral("max")] = options.hasMax ? numberJson(options.max) : QJsonValue();

  QJsonArray ringArray;
  for (const RadarGraticuleGeometry& ring : graticules) {
    QJsonObject value;
    value[QStringLiteral("shape")] = ring.circle ? QStringLiteral("circle") : QStringLiteral("polygon");
    value[QStringLiteral("radius")] = numberJson(ring.radius);
    QJsonArray points;
    for (const QPointF& point : ring.points) points.append(pointJson(point));
    value[QStringLiteral("points")] = points;
    ringArray.append(value);
  }
  root[QStringLiteral("graticules")] = ringArray;

  QJsonArray axisArray;
  for (const RadarAxisGeometry& axis : axes) {
    QJsonObject value;
    value[QStringLiteral("name")] = axis.name;
    value[QStringLiteral("label")] = axis.label;
    value[QStringLiteral("end")] = pointJson(axis.end);
    value[QStringLiteral("labelPosition")] = pointJson(axis.labelPosition);
    value[QStringLiteral("textAnchor")] = anchorName(axis.textAnchor);
    value[QStringLiteral("baseline")] = baselineName(axis.baseline);
    axisArray.append(value);
  }
  root[QStringLiteral("axes")] = axisArray;

  QJsonArray curveArray;
  for (const RadarCurveGeometry& curve : curves) {
    QJsonObject value;
    value[QStringLiteral("name")] = curve.name;
    value[QStringLiteral("label")] = curve.label;
    value[QStringLiteral("colorIndex")] = curve.colorIndex;
    value[QStringLiteral("classGenerated")] = curve.classGenerated;
    value[QStringLiteral("color")] = curve.color;
    value[QStringLiteral("shape")] = curve.polygon ? QStringLiteral("polygon") : QStringLiteral("curve");
    QJsonArray points;
    for (const QPointF& point : curve.points) points.append(pointJson(point));
    value[QStringLiteral("points")] = points;
    QJsonArray cubics;
    for (const RadarCubicSegment& segment : curve.cubics)
      cubics.append(QJsonArray{pointJson(segment.control1), pointJson(segment.control2),
                               pointJson(segment.end)});
    value[QStringLiteral("cubics")] = cubics;
    curveArray.append(value);
  }
  root[QStringLiteral("curves")] = curveArray;

  QJsonArray legendArray;
  for (const RadarLegendGeometry& legend : legends) {
    QJsonObject value;
    value[QStringLiteral("text")] = legend.text;
    value[QStringLiteral("position")] = pointJson(legend.position);
    value[QStringLiteral("colorIndex")] = legend.colorIndex;
    value[QStringLiteral("classGenerated")] = legend.classGenerated;
    value[QStringLiteral("color")] = legend.color;
    legendArray.append(value);
  }
  root[QStringLiteral("legends")] = legendArray;
  return root;
}

}  // namespace muffin::mermaid::radar
