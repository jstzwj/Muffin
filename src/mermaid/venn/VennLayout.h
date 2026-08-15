#pragma once

#include "mermaid/venn/VennDiagram.h"

#include <QPointF>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace muffin::mermaid::venn::layout {

struct Circle {
  QString set;
  double x = 0.0;
  double y = 0.0;
  double radius = 0.0;
};

struct Arc {
  Circle circle;
  double width = 0.0;
  QPointF p1;
  QPointF p2;
  bool large = false;
  bool sweep = true;
};

struct Area {
  VennSubset data;
  QVector<Circle> circles;
  QVector<Arc> arcs;
  QPointF text;
  bool textDisjoint = false;
  QString path;
  QString distinctPath;
};

struct Result {
  QVector<Circle> circles;
  QVector<Area> areas;
};

// Native port of @upsetjs/venn.js 2.0.0. The implementation intentionally
// preserves its ordering, stopping thresholds, normalization, and path
// formatting because Mermaid 11.16 exposes all four in SVG geometry.
Result compute(const QVector<VennSubset>& data, double width, double height,
               double padding);
Result compute(const QVector<VennSubset>& data, double width, double height,
               const QJsonValue& padding);

double intersectionArea(const QVector<Circle>& circles,
                        QVector<Arc>* arcs = nullptr);
QString intersectionPath(const QVector<Circle>& circles,
                         int roundDigits = -1);

}  // namespace muffin::mermaid::venn::layout
