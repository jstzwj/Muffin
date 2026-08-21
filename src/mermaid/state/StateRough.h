#pragma once

#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"

#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::state {

inline QVector<rough::Drawable> stateRoughNodeDrawables(
    const QString& shape, const QRectF& bounds, quint32 seed,
    qreal strokeWidth = 1.0) {
  QVector<rough::Drawable> result;
  const QPointF center = bounds.center();
  if (shape == QLatin1String("stateStart")) {
    rough::Options options;
    options.seed = seed;
    options.roughness = 0.7;
    options.fill = QStringLiteral("#000");
    options.fillStyle = QStringLiteral("hachure");
    options.fillWeight = 2.0;
    options.hachureAngle = 120.0;
    options.hachureGap = 4.0;
    options.strokeWidth = strokeWidth;
    result.append(rough::ellipse(center.x(), center.y(), bounds.width(),
                                 bounds.height(), options));
    return result;
  }
  if (shape == QLatin1String("stateEnd")) {
    rough::Options outer = rough::nodeOptions(seed, 2.0);
    result.append(rough::ellipse(center.x(), center.y(), bounds.width(),
                                 bounds.height(), outer));
    rough::Options inner = outer;
    inner.fillStyle = QStringLiteral("solid");
    const qreal diameter = bounds.width() * 5.0 / 14.0;
    result.append(rough::ellipse(center.x(), center.y(), diameter, diameter,
                                 inner));
    return result;
  }
  if (shape == QLatin1String("choice")) {
    const QPolygonF diamond{
        QPointF(center.x(), bounds.top()),
        QPointF(bounds.right(), center.y()),
        QPointF(center.x(), bounds.bottom()),
        QPointF(bounds.left(), center.y())};
    result.append(rough::polygon(diamond, rough::nodeOptions(
        seed, qMax<qreal>(1.3, strokeWidth))));
    return result;
  }
  if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
    result.append(rough::roughRectDrawable(
        bounds, seed, qMax<qreal>(1.3, strokeWidth)));
    return result;
  }
  const qreal radius = shape == QLatin1String("note")
      ? 0.0
      : (shape == QLatin1String("rectWithTitle") ? 10.0 : 5.0);
  if (radius > 0.0)
    result.append(rough::path(
        rough::roundedRectPath(bounds, radius),
        rough::nodeOptions(seed, qMax<qreal>(1.3, strokeWidth)), true));
  else
    result.append(rough::roughRectDrawable(
        bounds, seed, qMax<qreal>(1.3, strokeWidth)));
  return result;
}

inline QRectF stateRoughBounds(const QVector<rough::Drawable>& drawables) {  QRectF bounds;
  bool initialized = false;
  for (const rough::Drawable& drawable : drawables) {
    const QRectF current = rough::tightBounds(drawable);
    if (!current.isValid()) continue;
    bounds = initialized ? bounds.united(current) : current;
    initialized = true;
  }
  return bounds;
}

inline QVector<rough::Drawable> stateRoughClusterDrawables(
    const QRectF& bounds, qreal titleHeight, quint32 seed,
    bool alternate = false, const QString& titleFill = QStringLiteral("#000"),
    const QString& bodyFill = QStringLiteral("#000"),
    const QString& stroke = QStringLiteral("#000"),
    qreal strokeWidth = 1.0) {
  QVector<rough::Drawable> result;

  rough::Options outer;
  outer.seed = seed;
  outer.roughness = 0.7;
  outer.fill = titleFill;
  outer.fillStyle = QStringLiteral("solid");
  outer.stroke = stroke;
  outer.strokeWidth = strokeWidth;
  result.append(rough::path(rough::roundedRectPath(bounds, 10.0), outer,
                            true));

  const qreal innerY = bounds.top() + titleHeight + 2.0;
  const qreal innerHeight = std::max<qreal>(
      0.0, bounds.height() - titleHeight - 6.0);
  rough::Options inner;
  inner.seed = seed;
  inner.fill = bodyFill;
  inner.fillStyle = alternate ? QStringLiteral("hachure")
                              : QStringLiteral("solid");
  inner.stroke = stroke;
  inner.strokeWidth = strokeWidth;
  result.append(rough::rectangle(bounds.left(), innerY, bounds.width(),
                                  innerHeight, inner));
  return result;
}

// `--` partition (divider cluster): ONE dashed grey rect — upstream draws
// rc.rectangle(x, y, w, h, { fill: "lightgrey", roughness: 0.5, stroke:
// nodeBorder, seed }) and dashes the stroke via strokeLineDash [5] (the
// painter supplies the dash pattern on its stroke pen).
inline QVector<rough::Drawable> stateRoughDividerDrawables(
    const QRectF& bounds, quint32 seed, const QString& stroke,
    qreal strokeWidth = 1.0) {
  rough::Options options;
  options.seed = seed;
  options.roughness = 0.5;
  options.fill = QStringLiteral("lightgrey");
  options.fillStyle = QStringLiteral("solid");
  options.stroke = stroke;
  options.strokeWidth = strokeWidth;
  return {rough::rectangle(bounds.left(), bounds.top(), bounds.width(),
                           bounds.height(), options)};
}

}  // namespace muffin::mermaid::state
