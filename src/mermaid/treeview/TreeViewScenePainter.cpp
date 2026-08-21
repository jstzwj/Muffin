#include "mermaid/treeview/TreeViewScenePainter.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/treeview/TreeViewScene.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::treeview {
namespace {

color::SvgPaint rootFill(const QString& raw) {
  const QString value = raw.trimmed();
  if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return {true, {}};
  if (color::isParsableColor(value)) return {false, color::toQColor(value)};
  return {false, Qt::black};
}

color::SvgPaint inheritedTextFill(const QString& raw,
                                  const TreeViewScene& scene) {
  const QString value = raw.trimmed();
  if (value.isEmpty() || !color::isParsableColor(value)) {
    if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
      return {true, {}};
    return rootFill(scene.style.rootTextColor);
  }
  return {false, color::toQColor(value)};
}

color::SvgPaint cssPaint(const QString& raw, color::SvgPaintKind kind,
                         const color::SvgPaint& invalidFallback) {
  const QString value = raw.trimmed();
  if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return {true, {}};
  if (color::isParsableColor(value)) return {false, color::toQColor(value)};
  Q_UNUSED(kind);
  return invalidFallback;
}

void paintText(QPainter& painter, const TreeViewScene& scene,
               const TreeViewTextGeometry& text) {
  if (!text.visible || text.text.isEmpty() || !(text.fontSize > 0.0)) return;
  const color::SvgPaint fill = inheritedTextFill(text.fill, scene);
  if (fill.none) return;
  const QFont font = flowchart::makeFlowLabelFont(
      text.fontFamily.isEmpty() ? scene.style.fontFamily : text.fontFamily,
      text.fontSize, text.fontWeight, text.fontStyle);
  const QFontMetricsF metrics(font);
  painter.save();
  painter.setOpacity(text.opacity);
  painter.setFont(font);
  painter.setPen(fill.color);
  painter.setBrush(Qt::NoBrush);
  painter.drawText(QPointF(text.position.x(),
                           text.position.y() + metrics.xHeight() / 2.0),
                   text.text);
  painter.restore();
}

void paintNode(QPainter& painter, const TreeViewScene& scene,
               const TreeViewNodeGeometry& node) {
  if (node.highlighted && node.highlightVisible) {
    const color::SvgPaint fill = cssPaint(
        node.highlightFill, color::SvgPaintKind::Fill, {false, Qt::black});
    const color::SvgPaint stroke = cssPaint(
        node.highlightStroke, color::SvgPaintKind::Stroke, {true, {}});
    QPainterPath path;
    path.addRoundedRect(node.highlightRect, 3.0, 3.0);
    painter.save();
    QColor fillColor = fill.color;
    fillColor.setAlphaF(fillColor.alphaF() * node.highlightFillOpacity);
    painter.setBrush(fill.none ? QBrush(Qt::NoBrush) : QBrush(fillColor));
    if (stroke.none) {
      painter.setPen(Qt::NoPen);
    } else {
      QColor strokeColor = stroke.color;
      strokeColor.setAlphaF(strokeColor.alphaF() *
                            node.highlightStrokeOpacity);
      QPen pen(strokeColor, node.highlightStrokeWidth);
      pen.setCapStyle(Qt::FlatCap);
      painter.setPen(pen);
    }
    painter.drawPath(path);
    painter.restore();
  }
  // Mermaid 11.16 reserves icon space and leaves icon defs in the serialized
  // SVG, but its final source-entry sanitizer strips the corresponding <use>.
  // Intentionally do not paint an icon here.
  paintText(painter, scene, node.label);
  if (node.hasDescription) paintText(painter, scene, node.description);
}

void paintLine(QPainter& painter, const TreeViewScene& scene,
               const TreeViewLineGeometry& line) {
  if (!line.visible || !(line.strokeWidth > 0.0)) return;
  const color::SvgPaint stroke = cssPaint(
      line.stroke, color::SvgPaintKind::Stroke, {true, {}});
  if (stroke.none) return;
  painter.save();
  QColor strokeColor = stroke.color;
  strokeColor.setAlphaF(strokeColor.alphaF() * line.opacity);
  QPen pen(strokeColor, line.strokeWidth);
  pen.setCapStyle(Qt::FlatCap);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(line.start, line.end);
  painter.restore();
  Q_UNUSED(scene);
}

struct PaintItem {
  bool node = false;
  int index = -1;
  int order = -1;
};

}  // namespace

void paintTreeViewScene(QPainter& painter, const TreeViewScene& scene,
                        const MermaidPaintOptions&) {
  QVector<PaintItem> items;
  items.reserve(scene.nodes.size() + scene.lines.size());
  for (int i = 0; i < scene.nodes.size(); ++i)
    items.append({true, i, scene.nodes.at(i).groupPaintOrder});
  for (int i = 0; i < scene.lines.size(); ++i)
    items.append({false, i, scene.lines.at(i).paintOrder});
  std::stable_sort(items.begin(), items.end(),
                   [](const PaintItem& left, const PaintItem& right) {
                     return left.order < right.order;
                   });

  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  for (const PaintItem& item : items) {
    if (item.node)
      paintNode(painter, scene, scene.nodes.at(item.index));
    else
      paintLine(painter, scene, scene.lines.at(item.index));
  }
  painter.restore();
}

}  // namespace muffin::mermaid::treeview
