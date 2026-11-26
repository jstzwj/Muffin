// erDiagram's painter: the tables erBox draws, the relationship lines with
// their crow's-foot markers, and the labels over them, in the order Mermaid's
// document holds them, each in the paint its style resolved to. Per
// IMPLEMENTATION_SPEC.md this .cpp defines its free function with a
// fully-qualified name and keeps its helpers in an anonymous namespace.

#include "mermaid/erdiagram/ErScenePainter.h"

#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/rough/RoughOps.h"
#include "mermaid/scene/EdgeMarkerPaint.h"
#include "mermaid/scene/NeoShadow.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <optional>

namespace er = muffin::mermaid::er;
namespace flowchart = muffin::mermaid::flowchart;
namespace rough = muffin::mermaid::rough;
namespace scene = muffin::mermaid::scene;

namespace {

// The pen an SVG element has before any stylesheet: flat caps, and miter
// joins up to four times the stroke width.
QPen svgPen(const QColor& color, qreal width) {
  QPen pen(color, width);
  pen.setCapStyle(Qt::FlatCap);
  pen.setJoinStyle(Qt::MiterJoin);
  pen.setMiterLimit(4.0);
  return pen;
}

// A paint the scene resolved, as Chrome computes it: nothing where the value
// is empty, `none`, or no color.
std::optional<QColor> colorOf(const QString& value) {
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0 ||
      !muffin::mermaid::color::isParsableColor(value))
    return std::nullopt;
  return muffin::mermaid::color::toQColor(value);
}

// Whether a stroke is the gradient the neo look names.
bool isGradient(const QString& paint) {
  const QString value = paint.trimmed();
  return value.startsWith(QLatin1String("url(")) && value.contains(QLatin1String("er-gradient"));
}

// The gradient over a shape's own box, from its left edge to its right. A box
// with no width of its own, a vertical divider's, lies past the gradient's
// end: everything of it is the last color.
QBrush gradientBrush(const er::ErGradient& gradient, const QRectF& box) {
  QLinearGradient brush(box.left(), 0.0, box.right(), 0.0);
  const QColor start = colorOf(gradient.start).value_or(QColor(Qt::transparent));
  const QColor stop = colorOf(gradient.stop).value_or(QColor(Qt::transparent));
  brush.setColorAt(0.0, box.width() > 0.0 ? start : stop);
  brush.setColorAt(1.0, stop);
  return QBrush(brush);
}

// What a stroke-dasharray paints, in lengths of the stroke's own width as
// QPen takes them; empty where the stroke is solid.
QVector<qreal> dashPattern(const QString& dasharray, qreal strokeWidth) {
  const QString value = dasharray.trimmed();
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0 || !(strokeWidth > 0.0))
    return {};
  QVector<qreal> lengths;
  static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
  for (QString token : value.split(separators, Qt::SkipEmptyParts)) {
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
      token.chop(2);
    bool ok = false;
    const qreal length = token.toDouble(&ok);
    if (!ok)
      return {};
    lengths.append(length);
  }
  // The dash the neo look writes along a dashed line: "0 0", then as many
  // "2 2" as fit in its length, then "0". SVG repeats a list of an odd
  // length, which turns the phase of its second half: the dashes run from
  // the start of the line, a gap of 4 follows the last whole pair, and the
  // line, shorter than that half, ends inside the gap.
  if (lengths.size() >= 5 && lengths.size() % 2 == 1 && lengths.first() == 0.0 && lengths.at(1) == 0.0 &&
      lengths.last() == 0.0) {
    const int pairs = (lengths.size() - 3) / 2;
    const bool generated = std::all_of(lengths.cbegin() + 2, lengths.cend() - 1, [](qreal length) { return length == 2.0; });
    if (generated) {
      QVector<qreal> pattern;
      for (int dash = 0; dash < pairs - 1; ++dash)
        pattern << 2.0 << 2.0;
      // The last dash before the gap, and the gap of 4.
      pattern << 2.0 << 4.0;
      for (qreal& length : pattern)
        length /= strokeWidth;
      return pattern;
    }
  }
  if (lengths.size() % 2 != 0)
    lengths += lengths;
  // A list with no gap in it is a solid line, as one of zeros only is.
  bool gap = false;
  for (int index = 1; index < lengths.size(); index += 2)
    gap = gap || lengths.at(index) > 0.0;
  if (!gap)
    return {};
  for (qreal& length : lengths)
    length /= strokeWidth;
  return lengths;
}

// The stroke of an element: nothing where its style leaves it unstroked. A
// gradient is laid over `box`, the box of the shape that takes it.
std::optional<QPen> strokePen(const er::ErPaint& paint, const QRectF& box, const er::ErSceneStyle& style) {
  if (!(paint.strokeWidth > 0.0))
    return std::nullopt;
  QPen pen;
  if (isGradient(paint.stroke)) {
    // A gradient nothing defines paints nothing, as a reference to no
    // element does.
    if (style.gradient.start.isEmpty() || style.gradient.stop.isEmpty())
      return std::nullopt;
    pen = svgPen(QColor(Qt::black), paint.strokeWidth);
    pen.setBrush(gradientBrush(style.gradient, box));
  } else {
    const std::optional<QColor> color = colorOf(paint.stroke);
    if (!color)
      return std::nullopt;
    pen = svgPen(*color, paint.strokeWidth);
  }
  const QVector<qreal> dash = dashPattern(paint.strokeDasharray, paint.strokeWidth);
  if (!dash.isEmpty())
    pen.setDashPattern(dash);
  return pen;
}

// rough.js strokes a rectangle as four separate edges, so its corners have no
// join: the flat ends of the edges leave a notch in each outer corner.
void strokeRectEdges(QPainter& painter, const QRectF& rect) {
  painter.drawLine(rect.topLeft(), rect.topRight());
  painter.drawLine(rect.topRight(), rect.bottomRight());
  painter.drawLine(rect.bottomRight(), rect.bottomLeft());
  painter.drawLine(rect.bottomLeft(), rect.topLeft());
}

// A rectangle as Mermaid draws it for an entity: filled, then stroked, each
// with the paint the element's style resolved to. A table's is rough.js's:
// one path fills and another strokes its four edges, each painted once, with
// no join at the corners. Where the stylesheet strokes the filling path too,
// its outline is a second stroke over them, corners included.
void paintRect(QPainter& painter, const QRectF& rect, const er::ErPaint& paint, const er::ErSceneStyle& style,
               bool rough) {
  if (const std::optional<QColor> color = colorOf(paint.fill))
    painter.fillRect(rect, *color);
  painter.setBrush(Qt::NoBrush);
  const std::optional<QPen> pen = strokePen(paint, rect, style);
  if (!pen)
    return;
  painter.setPen(*pen);
  if (!rough) {
    painter.drawRect(rect);
    return;
  }
  strokeRectEdges(painter, rect);
  if (paint.joined)
    painter.drawRect(rect);
}

// A text in its own style and color. An HTML label is as many line boxes as
// it has lines, each as high as its line height. An SVG text has its first
// baseline a font size below the top of its box, and takes the box as it is.
void paintText(QPainter& painter, const er::ErSceneText& text, flowchart::FlowLabelAlign align,
               bool centerVertically = true) {
  if (text.document.text.isEmpty())
    return;
  const er::ErTextStyle& style = text.style;
  qreal lineHeight = style.lineHeight();
  if (text.svgText) {
    lineHeight = flowchart::flowLabelFontBoundingMetrics(style.fontFamily, style.fontSize, style.fontWeight,
                                                         style.italic ? QFont::StyleItalic : QFont::StyleNormal)
                     .height();
    centerVertically = false;
  }
  flowchart::paintFlowLabel(painter, text.document, text.textBox, style.fontFamily, style.fontSize, lineHeight,
                            colorOf(style.color).value_or(QColor(Qt::transparent)), centerVertically, align);
}

// The shadow the neo look's filter casts of a table: what its rectangle
// paints, fill and stroke, blurred and offset.
void paintTableShadow(QPainter& painter, const er::ErSceneEntity& entity) {
  const scene::NeoShadow shadow = scene::parseNeoShadow(entity.filter);
  if (!shadow.valid)
    return;
  const std::optional<QColor> stroke = colorOf(entity.table.stroke);
  scene::ShadowChannels channels;
  channels.fillAlpha = colorOf(entity.table.fill).value_or(QColor(Qt::transparent)).alphaF();
  channels.strokeAlpha =
      (stroke || isGradient(entity.table.stroke)) ? stroke.value_or(QColor(Qt::black)).alphaF() : 0.0;
  QPainterPath outline;
  outline.addRect(entity.bounds);
  scene::paintNeoShadow(painter, {{outline, entity.table.strokeWidth, channels}}, shadow);
}

// A shape as rough.js draws it under the handDrawn look: the sketch of its
// fill, lines in the fill's color as wide as rough.js's fillWeight, and its
// outline.
void paintRoughShape(QPainter& painter, const er::ErRoughShape& shape, const er::ErPaint& paint,
                     const er::ErSceneStyle& style) {
  QPen sketch(Qt::NoPen);
  if (paint.fillWidth > 0.0) {
    if (const std::optional<QColor> color = colorOf(paint.fill))
      sketch = svgPen(*color, paint.fillWidth);
  }
  const std::optional<QPen> outline = strokePen(paint, shape.bounds, style);
  painter.setBrush(Qt::NoBrush);
  for (const rough::OpSet& set : shape.drawable.sets) {
    if (set.type == rough::OpSetType::FillSketch && sketch.style() != Qt::NoPen) {
      painter.setPen(sketch);
      painter.drawPath(rough::toPainterPath(set));
    } else if (set.type == rough::OpSetType::Path && outline) {
      painter.setPen(*outline);
      painter.drawPath(rough::toPainterPath(set));
    }
  }
}

void paintEntity(QPainter& painter, const er::ErSceneEntity& entity, const er::ErSceneStyle& style) {
  paintTableShadow(painter, entity);
  if (entity.rough) {
    // In the order the drawn shapes lie in the document: the table, its
    // rows, their texts, and the dividers over them.
    paintRoughShape(painter, entity.rough->table, entity.table, style);
    if (entity.rows.isEmpty()) {
      paintText(painter, entity.name, flowchart::FlowLabelAlign::Center);
      return;
    }
    for (qsizetype index = 0; index < entity.rows.size(); ++index)
      paintRoughShape(painter, entity.rough->rows.at(index), entity.rows.at(index).paint, style);
    for (const er::ErSceneRow& row : entity.rows) {
      for (const er::ErSceneText* cell : {&row.type, &row.name, &row.keys, &row.comment})
        paintText(painter, *cell, flowchart::FlowLabelAlign::Left);
    }
    // erBox appends the line under the name twice, as two elements of the
    // document: the same drawing twice, whose antialiasing adds up.
    for (qsizetype index = 0; index < entity.rough->dividers.size(); ++index) {
      for (int pass = 0; pass < (index == 0 ? entity.nameDividerPasses : 1); ++pass)
        paintRoughShape(painter, entity.rough->dividers.at(index), entity.dividerPaint, style);
    }
    return;
  }
  // An entity without attributes: a `.node rect`, with its name centered.
  if (entity.rows.isEmpty()) {
    paintRect(painter, entity.bounds, entity.table, style, false);
    paintText(painter, entity.name, flowchart::FlowLabelAlign::Center);
    return;
  }

  // A table, in the order erBox appends it: the table, its rows, their texts,
  // and the dividers over them.
  paintRect(painter, entity.bounds, entity.table, style, true);
  for (const er::ErSceneRow& row : entity.rows)
    paintRect(painter, row.rect, row.paint, style, true);
  for (const er::ErSceneRow& row : entity.rows) {
    for (const er::ErSceneText* cell : {&row.type, &row.name, &row.keys, &row.comment})
      paintText(painter, *cell, flowchart::FlowLabelAlign::Left);
  }
  // A divider is a polygon a hundredth of a pixel thick. Where the stylesheet
  // strokes the filling path too, the stroke runs round its ends, half a
  // stroke beyond the line on either side.
  painter.setBrush(Qt::NoBrush);
  for (int index = 0; index < entity.dividers.size(); ++index) {
    const QLineF divider = entity.dividers.at(index);
    QRectF box = QRectF(divider.p1(), divider.p2()).normalized();
    if (box.width() <= 0.0)
      box.setWidth(0.0001);
    if (box.height() <= 0.0)
      box.setHeight(0.0001);
    const std::optional<QPen> pen = strokePen(entity.dividerPaint, box, style);
    if (!pen)
      continue;
    QPen linePen = *pen;
    const int passes = index == 0 ? entity.nameDividerPasses : 1;
    for (int pass = 0; pass < passes; ++pass) {
      linePen.setCapStyle(Qt::FlatCap);
      painter.setPen(linePen);
      painter.drawLine(divider);
      if (entity.dividerPaint.joined) {
        linePen.setCapStyle(Qt::SquareCap);
        painter.setPen(linePen);
        painter.drawLine(divider);
      }
    }
  }
}

// A crow's-foot marker on a line end: the marker's reference point on the
// end, its x axis along the line, and cut off at its viewport, which is as
// many stroke widths wide and high as the marker is declared, or in user
// units under the neo look.
void paintMarker(QPainter& painter, const er::ErMarker& marker, const QPointF& at, qreal angle,
                 const er::ErMarkerPaint& paint, std::optional<qreal> lineWidth) {
  painter.save();
  scene::applyMarkerTransform(painter, at, angle, marker.refX, marker.refY);
  const qreal scale = lineWidth ? *lineWidth : 1.0;
  painter.setClipRect(QRectF(0.0, 0.0, marker.width * scale, marker.height * scale), Qt::IntersectClip);
  // The ring of the "zero" markers is filled over the end of the line, and
  // stroked like the rest of the marker.
  if (marker.hasRing) {
    const std::optional<QColor> ringStroke = colorOf(paint.ringStroke);
    painter.setPen(ringStroke ? svgPen(*ringStroke, paint.strokeWidth) : QPen(Qt::NoPen));
    painter.setBrush(colorOf(paint.ringFill).value_or(QColor(Qt::transparent)));
    painter.drawEllipse(QPointF(marker.ringX, marker.ringY), marker.ringRadius, marker.ringRadius);
  }
  // `.marker`: a stroke in the line's color, whatever the line's own.
  const std::optional<QColor> stroke = colorOf(paint.stroke);
  painter.setPen(stroke ? svgPen(*stroke, paint.strokeWidth) : QPen(Qt::NoPen));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(scene::parseSvgPath(marker.path));
  painter.restore();
}

} // namespace

void muffin::mermaid::er::paintErScene(const ErScene& scene, QPainter& painter, const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);

  // Lines first, so the tables cover them where they meet.
  for (const ErSceneRelationship& relationship : scene.relationships) {
    // The handDrawn look draws the path by rough.js, and its markers sit on
    // the drawn path's own ends.
    const QPainterPath path = scene::parseSvgPath(relationship.path);
    const QRectF drawn =
        relationship.rough ? QRectF(relationship.rough->bounds) : QRectF(path.boundingRect());
    if ((!relationship.rough && path.elementCount() < 2) || drawn.isNull() ||
        !mermaidPrimitiveIsVisible(drawn, options))
      continue;
    painter.setBrush(Qt::NoBrush);
    if (const std::optional<QPen> pen = strokePen(relationship.line, drawn, scene.style)) {
      painter.setPen(*pen);
      if (relationship.rough) {
        for (const rough::OpSet& set : relationship.rough->drawable.sets)
          if (set.type == rough::OpSetType::Path)
            painter.drawPath(rough::toPainterPath(set));
      } else {
        painter.drawPath(path);
      }
    }
    if (!options.paintEdgeMarkers)
      continue;
    // The marker at the start is turned the way the path leaves it; the one
    // at the end the way the path arrives.
    const ErPathAnchor start =
        relationship.rough ? er::roughPathStart(*relationship.rough)
                           : ErPathAnchor{path.elementAt(0), scene::tangentAngleDeg(scene::pathStartTangent(path))};
    const ErPathAnchor end =
        relationship.rough ? er::roughPathEnd(*relationship.rough)
                           : ErPathAnchor{path.elementAt(path.elementCount() - 1),
                                          scene::tangentAngleDeg(scene::pathEndTangent(path))};
    // A marker's viewport is scaled by the line's stroke width, as a marker's
    // is, unless the neo look sizes it in user units.
    const std::optional<qreal> lineWidth =
        scene.style.neo ? std::nullopt : std::optional<qreal>(relationship.line.strokeWidth);
    if (relationship.cardA) {
      if (const std::optional<ErMarker> marker = erMarker(*relationship.cardA, true, scene.style.neo))
        paintMarker(painter, *marker, start.point, start.angleDeg,
                    scene.markerPaint.value(erMarkerKey(*relationship.cardA, true)), lineWidth);
    }
    if (relationship.cardB) {
      if (const std::optional<ErMarker> marker = erMarker(*relationship.cardB, false, scene.style.neo))
        paintMarker(painter, *marker, end.point, end.angleDeg,
                    scene.markerPaint.value(erMarkerKey(*relationship.cardB, false)), lineWidth);
    }
  }

  // The labels over the lines.
  for (const ErSceneRelationship& relationship : scene.relationships) {
    if (relationship.labelText.document.text.isEmpty() ||
        !mermaidPrimitiveIsVisible(relationship.labelText.rect, options))
      continue;
    painter.fillRect(relationship.labelText.rect, relationship.labelBackground);
    paintText(painter, relationship.labelText, flowchart::FlowLabelAlign::Center);
  }

  // The handDrawn look draws a copy of every entity behind its shapes.
  for (const ErSceneEntity& background : scene.backgrounds) {
    if (mermaidPrimitiveIsVisible(background.bounds, options))
      paintEntity(painter, background, scene.style);
  }

  for (const ErSceneEntity& entity : scene.entities) {
    if (mermaidPrimitiveIsVisible(entity.bounds, options))
      paintEntity(painter, entity, scene.style);
  }
}

void muffin::mermaid::er::ErScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintErScene(*this, painter, options);
}
