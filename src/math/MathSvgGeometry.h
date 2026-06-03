#pragma once

#include <QHash>
#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace muffin::math {

class MathSvgGeometry {
public:
  static bool hasPath(const QString& name);
  static QString path(const QString& name);
  static QPainterPath painterPath(const QString& name, QRectF target);
  static QPainterPath painterPathFromSvgPath(const QString& svgPath, QRectF viewBox, QRectF target);
  static QString innerPath(const QString& name, int height);
  static QString tallDelimiterPath(const QString& label, int midHeight);
  static QString sqrtPath(const QString& size, qreal extraVinculum, int viewBoxHeight);
  static bool loaded();

private:
  static void ensureLoaded();
};

}  // namespace muffin::math
