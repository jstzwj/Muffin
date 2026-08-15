#include "mermaid/classdiagram/ClassScene.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

namespace muffin::mermaid::classdiagram {
namespace {

QRectF pointsBounds(const QVector<QPointF>& points,
                    const QVector<QVector<QPointF>>& segments = {}) {
  QRectF bounds;
  bool initialized = false;
  auto includePoint = [&](const QPointF& point) {
    const QRectF pixel(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0));
    if (!initialized) {
      bounds = pixel;
      initialized = true;
    } else {
      bounds = bounds.united(pixel);
    }
  };
  for (const QPointF& point : points) includePoint(point);
  for (const QVector<QPointF>& segment : segments)
    for (const QPointF& point : segment) includePoint(point);
  return initialized ? bounds.adjusted(-18.0, -18.0, 18.0, 18.0) : QRectF{};
}

qreal markerOffset(const QString& type) {
  if (type == QLatin1String("aggregation") ||
      type == QLatin1String("extension") ||
      type == QLatin1String("composition")) return 17.25;
  if (type == QLatin1String("dependency")) return 6.0;
  if (type == QLatin1String("lollipop")) return 13.5;
  return 0.0;
}

QVector<QPointF> applyMarkerOffsets(const QVector<QPointF>& points,
                                    const ClassLayoutEdgeInput& edge) {
  QVector<QPointF> rendered = points;
  if (points.size() < 2) return rendered;
  const auto moveToward = [](const QPointF& from, const QPointF& toward,
                             qreal distance) {
    const QPointF delta = toward - from;
    const qreal length = std::hypot(delta.x(), delta.y());
    return qFuzzyIsNull(length) ? from : from + delta * (distance / length);
  };
  const qreal startOffset = markerOffset(edge.arrowTypeStart);
  const qreal endOffset = markerOffset(edge.arrowTypeEnd);
  if (startOffset > 0.0)
    rendered.first() = moveToward(points.first(), points.at(1), startOffset);
  if (endOffset > 0.0)
    rendered.last() = moveToward(points.last(), points.at(points.size() - 2), endOffset);

  // lineWithOffset.ts also prevents a control point from entering the marker's
  // square clearance area. Keep x/y independent, matching the two d3 accessors.
  const bool left = points.first().x() < points.last().x();
  const bool down = points.first().y() < points.last().y();
  for (qsizetype i = 0; i < points.size(); ++i) {
    const QPointF point = points.at(i);
    qreal xAdjustment = 0.0;
    qreal yAdjustment = 0.0;
    const qreal endDx = std::abs(point.x() - points.last().x());
    const qreal endDy = std::abs(point.y() - points.last().y());
    const qreal startDx = std::abs(point.x() - points.first().x());
    const qreal startDy = std::abs(point.y() - points.first().y());
    if (endOffset > 0.0 && endDx < endOffset && endDx > 0.0 && endDy < endOffset) {
      qreal adjustment = endOffset + 1.0 - endDx;
      adjustment *= left ? -1.0 : 1.0;
      xAdjustment -= adjustment;
    }
    if (startOffset > 0.0 && startDx < startOffset && startDx > 0.0 && startDy < startOffset) {
      qreal adjustment = startOffset + 1.0 - startDx;
      adjustment *= left ? -1.0 : 1.0;
      xAdjustment += adjustment;
    }
    if (endOffset > 0.0 && endDy < endOffset && endDy > 0.0 && endDx < endOffset) {
      qreal adjustment = endOffset + 1.0 - endDy;
      adjustment *= down ? 1.0 : -1.0;
      yAdjustment -= adjustment;
    }
    if (startOffset > 0.0 && startDy < startOffset && startDy > 0.0 && startDx < startOffset) {
      qreal adjustment = startOffset + 1.0 - startDy;
      adjustment *= down ? 1.0 : -1.0;
      yAdjustment += adjustment;
    }
    // Endpoint marker offsets were already applied as a vector above. The
    // clearance terms apply independently to every point, including endpoints.
    rendered[i] += QPointF(xAdjustment, yAdjustment);
  }
  return rendered;
}

QPointF pointAtDistance(const QVector<QPointF>& points, qreal distance) {
  if (points.isEmpty()) return {};
  qreal consumed = 0.0;
  for (qsizetype i = 1; i < points.size(); ++i) {
    const QPointF delta = points.at(i) - points.at(i - 1);
    const qreal length = std::hypot(delta.x(), delta.y());
    if (consumed + length >= distance && length > 0.0)
      return points.at(i - 1) + delta * ((distance - consumed) / length);
    consumed += length;
  }
  return points.last();
}

QPointF terminalPosition(const QVector<QPointF>& source, bool start,
                         bool right, bool hasMarker) {
  QVector<QPointF> points = source;
  if (!start) std::reverse(points.begin(), points.end());
  if (points.size() < 2) return points.value(0);
  const qreal markerSize = hasMarker ? 10.0 : 0.0;
  const QPointF center = pointAtDistance(points, 25.0 + markerSize);
  const qreal distance = 10.0 + markerSize * 0.5;
  const qreal angle = std::atan2(points.first().y() - center.y(),
                                 points.first().x() - center.x());
  qreal phase = angle;
  qreal correction = 0.0;
  if (start && !right) phase += std::numbers::pi_v<qreal>;
  if (!start && right) { phase -= std::numbers::pi_v<qreal>; correction = -5.0; }
  if (!start && !right) correction = -5.0;
  return QPointF(std::sin(phase) * distance +
                     (points.first().x() + center.x()) / 2.0 + correction,
                 -std::cos(phase) * distance +
                     (points.first().y() + center.y()) / 2.0 + correction);
}

flowchart::FlowLabelDocument prepareLabel(const QString& text, qreal fontSize,
                                          bool bold = false,
                                          const QString& cssStyle = {},
                                          bool svgText = false) {
  auto document = svgText
      ? flowchart::parseFlowSvgLabel(text, QStringLiteral("markdown"))
      : flowchart::parseFlowLabel(text, QStringLiteral("markdown"), true);
  if (!svgText)
    document.formattingContext =
        flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
  if ((bold || !cssStyle.isEmpty()) && !document.text.isEmpty()) {
    QTextCharFormat format;
    if (bold) format.setFontWeight(QFont::Bold);
    if (cssStyle.contains(QStringLiteral("font-style:italic")))
      format.setFontItalic(true);
    if (cssStyle.contains(QStringLiteral("text-decoration:underline")))
      format.setFontUnderline(true);
    document.formats.append({0, static_cast<int>(document.text.size()), format});
  }
  flowchart::prepareFlowLabelMath(document, fontSize);
  return document;
}

QVector<ClassSceneLabel> compartmentLabels(
    const QVector<QString>& texts,
    const QVector<ClassTextMeasurement>& measurements,
    const ClassCompartmentGeometry& geometry, const QVector<QString>& styles,
    const ClassSceneStyle& sceneStyle, qreal textPadding,
    bool bold = false) {
  QVector<ClassSceneLabel> labels;
  qreal offset = 0.0;
  for (qsizetype i = 0; i < texts.size(); ++i) {
    const ClassTextMeasurement measured = measurements.value(i);
    const qsizetype lineCount = std::max<qsizetype>(1, measured.lineCount);
    const qreal labelOffset =
        -measured.bounds.height() / (2.0 * lineCount) + offset;
    ClassSceneLabel label;
    label.text = texts.at(i);
    label.cssStyle = styles.value(i);
    label.size = measured.bounds.size();
    label.svgText = measured.svgText;
    label.center = geometry.translation +
        QPointF(measured.bounds.center().x(),
                measured.bounds.center().y() + labelOffset);
    label.document = prepareLabel(label.text, sceneStyle.fontSize, bold,
                                  label.cssStyle, label.svgText);
    labels.append(std::move(label));
    offset += measured.bounds.height() + textPadding;
  }
  return labels;
}

void applyNodeStyles(ClassSceneNode& node, const ClassSceneStyle& sceneStyle) {
  const bool note = node.shape == QLatin1String("note");
  node.fill = note ? sceneStyle.noteFill : sceneStyle.classFill;
  node.stroke = note ? sceneStyle.noteStroke : sceneStyle.classStroke;
  node.textColor = note ? sceneStyle.noteTextColor : sceneStyle.textColor;
  node.strokeWidth = sceneStyle.strokeWidth;
  const auto apply = [&](const QString& declaration) {
    const int colon = declaration.indexOf(QLatin1Char(':'));
    if (colon < 0) return;
    const QString key = declaration.left(colon).trimmed();
    QString value = declaration.mid(colon + 1).trimmed();
    value.remove(QStringLiteral("!important"), Qt::CaseInsensitive);
    value = value.trimmed();
    if (key == QLatin1String("fill")) node.fill = value;
    else if (key == QLatin1String("stroke")) node.stroke = value;
    else if (key == QLatin1String("color")) node.textColor = value;
    else if (key == QLatin1String("stroke-width")) {
      if (value.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) value.chop(2);
      bool ok = false;
      const qreal width = value.toDouble(&ok);
      if (ok && width >= 0.0) node.strokeWidth = width;
    }
  };
  for (const QString& declaration : node.styles) apply(declaration);
}

ClassMarkerDefinition marker(QString type, QString suffix, qreal refX,
                             qreal refY, qreal width, qreal height,
                             QString units, QString viewBox,
                             ClassMarkerChild child) {
  ClassMarkerDefinition value;
  value.type = std::move(type);
  value.suffix = std::move(suffix);
  value.cssClass = QStringLiteral("marker %1 class").arg(value.type);
  value.refX = refX; value.refY = refY;
  value.markerWidth = width; value.markerHeight = height;
  value.markerUnits = std::move(units); value.viewBox = std::move(viewBox);
  value.child = std::move(child);
  return value;
}

ClassMarkerChild pathChild(QString path, QString style = {}) {
  ClassMarkerChild child;
  child.tag = QStringLiteral("path"); child.path = std::move(path);
  child.style = std::move(style);
  return child;
}

ClassMarkerChild polygonChild(QString points, QString style) {
  ClassMarkerChild child;
  child.tag = QStringLiteral("polygon"); child.points = std::move(points);
  child.style = std::move(style);
  return child;
}

}  // namespace

QVector<ClassMarkerDefinition> classMarkerDefinitions() {
  QVector<ClassMarkerDefinition> result;
  const auto diamonds = [&](const QString& type, bool filled) {
    const QString path = QStringLiteral("M 18,7 L9,13 L1,7 L9,1 Z");
    const QString marginStyle = filled ? QStringLiteral("stroke-width: 0;")
                                       : QStringLiteral("stroke-width: 2;");
    result.append(marker(type, type + QStringLiteral("Start"), 18, 7, 190, 240, {}, {}, pathChild(path)));
    result.append(marker(type, type + QStringLiteral("End"), 1, 7, 20, 28, {}, {}, pathChild(path)));
    result.append(marker(type, type + QStringLiteral("Start-margin"), 15, 7, 190, 240,
                         QStringLiteral("userSpaceOnUse"), {}, pathChild(path, marginStyle)));
    result.append(marker(type, type + QStringLiteral("End-margin"), filled ? 3.5 : 1, 7, 20, 28,
                         QStringLiteral("userSpaceOnUse"), {}, pathChild(path, marginStyle)));
  };
  diamonds(QStringLiteral("aggregation"), false);
  result.append(marker(QStringLiteral("extension"), QStringLiteral("extensionStart"), 18, 7, 20, 28,
                       QStringLiteral("userSpaceOnUse"), {}, pathChild(QStringLiteral("M 1,7 L18,13 V 1 Z"))));
  result.append(marker(QStringLiteral("extension"), QStringLiteral("extensionEnd"), 1, 7, 20, 28,
                       {}, {}, pathChild(QStringLiteral("M 1,1 V 13 L18,7 Z"))));
  result.append(marker(QStringLiteral("extension"), QStringLiteral("extensionStart-margin"), 18, 7, 20, 28,
                       QStringLiteral("userSpaceOnUse"), QStringLiteral("0 0 20 14"),
                       polygonChild(QStringLiteral("10,7 18,13 18,1"), QStringLiteral("stroke-width: 2; stroke-dasharray: 0;"))));
  result.append(marker(QStringLiteral("extension"), QStringLiteral("extensionEnd-margin"), 9, 7, 20, 28,
                       QStringLiteral("userSpaceOnUse"), QStringLiteral("0 0 20 14"),
                       polygonChild(QStringLiteral("10,1 10,13 18,7"), QStringLiteral("stroke-width: 2; stroke-dasharray: 0;"))));
  diamonds(QStringLiteral("composition"), true);
  const QString dependencyStart = QStringLiteral("M 5,7 L9,13 L1,7 L9,1 Z");
  const QString dependencyEnd = QStringLiteral("M 18,7 L9,13 L14,7 L9,1 Z");
  result.append(marker(QStringLiteral("dependency"), QStringLiteral("dependencyStart"), 6, 7, 190, 240, {}, {}, pathChild(dependencyStart)));
  result.append(marker(QStringLiteral("dependency"), QStringLiteral("dependencyEnd"), 13, 7, 20, 28, {}, {}, pathChild(dependencyEnd)));
  result.append(marker(QStringLiteral("dependency"), QStringLiteral("dependencyStart-margin"), 4, 7, 190, 240,
                       QStringLiteral("userSpaceOnUse"), {}, pathChild(dependencyStart, QStringLiteral("stroke-width: 0;"))));
  result.append(marker(QStringLiteral("dependency"), QStringLiteral("dependencyEnd-margin"), 16, 7, 20, 28,
                       QStringLiteral("userSpaceOnUse"), {}, pathChild(dependencyEnd, QStringLiteral("stroke-width: 0;"))));
  for (const auto& variant : QVector<std::tuple<QString, qreal, qreal, QString>>{
           {QStringLiteral("lollipopStart"), 13, 0, {}},
           {QStringLiteral("lollipopEnd"), 1, 0, {}},
           {QStringLiteral("lollipopStart-margin"), 13, 2, QStringLiteral("userSpaceOnUse")},
           {QStringLiteral("lollipopEnd-margin"), 1, 2, QStringLiteral("userSpaceOnUse")}}) {
    ClassMarkerChild child;
    child.tag = QStringLiteral("circle"); child.cx = 7; child.cy = 7;
    child.radius = 6; child.fill = QStringLiteral("transparent");
    if (std::get<2>(variant) > 0) child.strokeWidth = QString::number(std::get<2>(variant));
    result.append(marker(QStringLiteral("lollipop"), std::get<0>(variant), std::get<1>(variant),
                         7, 190, 240, std::get<3>(variant), {}, std::move(child)));
  }
  return result;
}

ClassScene buildClassScene(const ClassLayoutInput& input,
                           const QVector<ClassBoxGeometry>& boxes,
                           const ClassLayoutMeasurements& measurements,
                           const ClassPlacementResult& placement,
                           ClassSceneStyle style,
                           const QVector<style::ClassDef>& classDefs,
                           const style::ThemeDefaults& themeDefaults,
                           bool handDrawn, quint32 handDrawnSeed) {
  ClassScene scene;
  scene.style = std::move(style);
  scene.handDrawn = handDrawn;
  scene.handDrawnSeed = handDrawnSeed;
  scene.markers = classMarkerDefinitions();
  QHash<QString, ClassPlacementNode> placedNodes;
  for (const auto& node : placement.nodes) placedNodes.insert(node.id, node);
  QHash<QString, ClassBoxGeometry> boxesById;
  for (const auto& box : boxes) boxesById.insert(box.id, box);

  for (const auto& cluster : placement.clusters) {
    const auto semantic = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
        [&](const auto& node) { return node.id == cluster.id; });
    ClassSceneCluster rendered;
    rendered.id = cluster.id;
    rendered.label = semantic == input.nodes.cend() ? cluster.id : semantic->label;
    rendered.bounds = QRectF(cluster.x - cluster.width / 2.0,
                             cluster.y - cluster.height / 2.0,
                             cluster.width, cluster.height);
    const QSizeF logicalSize(
        cluster.logicalWidth > 0.0 ? cluster.logicalWidth : cluster.width,
        cluster.logicalHeight > 0.0 ? cluster.logicalHeight : cluster.height);
    const QRectF logicalBounds(
        QPointF(cluster.x - logicalSize.width() / 2.0,
                cluster.y - logicalSize.height() / 2.0), logicalSize);
    if (!rendered.label.isEmpty()) {
      rendered.titleLabel.text = rendered.label;
      rendered.titleLabel.document =
          prepareLabel(rendered.label, scene.style.fontSize);
      rendered.titleLabel.size = flowchart::measureFlowLabel(
          rendered.titleLabel.document, scene.style.fontFamily,
          scene.style.fontSize, scene.style.lineHeight);
      rendered.titleLabel.center = QPointF(
          rendered.bounds.center().x(),
          logicalBounds.top() + rendered.titleLabel.size.height() / 2.0);
    }
    if (handDrawn) {
      rough::Options options = rough::nodeOptions(handDrawnSeed, 1.3);
      options.fillWeight = 3.0;
      const QPointF roughFrame = placement.sourceOrigin + QPointF(8.0, 8.0);
      rendered.roughDrawable = rough::translatedDrawable(
          rough::roughRoundedRectPathDrawable(
              logicalBounds.translated(roughFrame), 0.0, options),
          -roughFrame);
      rendered.paintedBounds = rough::tightBounds(rendered.roughDrawable);
    }
    scene.clusters.append(std::move(rendered));
  }
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.isGroup || !placedNodes.contains(node.id)) continue;
    const ClassPlacementNode placed = placedNodes.value(node.id);
    ClassSceneNode rendered;
    rendered.id = node.id; rendered.shape = node.shape;
    rendered.cssClasses = node.cssClasses; rendered.cssStyles = node.cssStyles;
    rendered.styles = node.styles; rendered.center = {placed.x, placed.y};
    applyNodeStyles(rendered, scene.style);
    rendered.size = {placed.width, placed.height};
    if (node.shape == QLatin1String("classBox") && boxesById.contains(node.id)) {
      const ClassBoxGeometry box = boxesById.value(node.id);
      const ClassNodeMeasurements measured = measurements.value(node.id);
      rendered.localOuter = box.outerRect;
      rendered.localDividers = box.dividers;
      QVector<QString> annotationTexts;
      if (!node.annotations.isEmpty())
        annotationTexts.append(QString(QChar(0x00ab)) + node.annotations.first() + QChar(0x00bb));
      rendered.annotationLabels = compartmentLabels(annotationTexts, measured.annotations,
          box.annotation, {}, scene.style, measured.textPadding);
      rendered.nameLabels = compartmentLabels(
          {classBoxLabelMarkup(node.label, node.text)}, measured.labels,
          box.label, {}, scene.style, measured.textPadding, true);
      QVector<QString> memberTexts, memberStyles, methodTexts, methodStyles;
      for (const auto& value : node.members) { memberTexts.append(value.text); memberStyles.append(value.cssStyle); }
      for (const auto& value : node.methods) { methodTexts.append(value.text); methodStyles.append(value.cssStyle); }
      rendered.memberLabels = compartmentLabels(memberTexts, measured.members,
          box.members, memberStyles, scene.style, measured.textPadding);
      rendered.methodLabels = compartmentLabels(methodTexts, measured.methods,
          box.methods, methodStyles, scene.style, measured.textPadding);
    } else {
      rendered.localOuter = QRectF(-placed.width / 2.0, -placed.height / 2.0,
                                  placed.width, placed.height);
      ClassSceneLabel label;
      label.text = node.label; label.center = {};
      const qreal padding = node.padding.value_or(0.0);
      label.size = QSizeF(std::max(0.0, placed.width - padding * 2.0),
                          std::max(0.0, placed.height - padding * 2.0));
      label.document = prepareLabel(label.text, scene.style.fontSize);
      rendered.nameLabels.append(std::move(label));
    }
    if (handDrawn) {
      const rough::Drawable localOuter = rough::roughRectDrawable(
          rendered.localOuter, handDrawnSeed);
      const QRectF localOuterBounds = rough::tightBounds(localOuter);
      rendered.roughDrawables.append(rough::translatedDrawable(
          localOuter, rendered.center));
      rendered.paintedBounds = rough::tightBounds(
          rendered.roughDrawables.last());
      for (const QRectF& divider : rendered.localDividers) {
        // classBox reads rect2.getBBox() after RoughJS has generated the
        // outer rectangle, then uses that observable bbox for both divider
        // endpoints. It is deliberately wider/narrower than the nominal box
        // depending on the handDrawnSeed.
        const QRectF line(localOuterBounds.left(), divider.top(),
                          localOuterBounds.width(), divider.height());
        rendered.roughDrawables.append(rough::translatedDrawable(
            rough::roughNodeLineDrawable(
            QPointF(line.left(), line.top()),
            QPointF(line.right(), line.bottom()), handDrawnSeed),
            rendered.center));
        rendered.paintedBounds = rendered.paintedBounds.united(
            rough::tightBounds(rendered.roughDrawables.last()));
      }
    }
    scene.nodes.append(std::move(rendered));
  }

  for (const ClassLayoutEdgeInput& edge : input.edges) {
    const auto found = std::find_if(placement.edges.cbegin(), placement.edges.cend(),
        [&](const auto& value) { return value.id == edge.id; });
    if (found == placement.edges.cend()) continue;
    ClassSceneEdge rendered;
    rendered.id = edge.id; rendered.pattern = edge.pattern;
    rendered.classes = edge.classes; rendered.markerStart = edge.arrowTypeStart;
    rendered.markerEnd = edge.arrowTypeEnd; rendered.points = found->points;
    rendered.segments = found->segments;
    if (rendered.segments.isEmpty()) {
      rendered.renderedPoints = applyMarkerOffsets(rendered.points, edge);
      rendered.path = flowchart::d3curve::pathForCurve(rendered.renderedPoints,
                                                       QStringLiteral("basis"));
      rendered.paths.append(rendered.path);
    } else {
      for (qsizetype index = 0; index < rendered.segments.size(); ++index) {
        ClassLayoutEdgeInput segmentEdge = edge;
        if (index != 0) segmentEdge.arrowTypeStart = QStringLiteral("none");
        if (index + 1 != rendered.segments.size())
          segmentEdge.arrowTypeEnd = QStringLiteral("none");
        QVector<QPointF> segment = applyMarkerOffsets(
            rendered.segments.at(index), segmentEdge);
        rendered.paths.append(flowchart::d3curve::pathForCurve(
            segment, QStringLiteral("basis")));
        rendered.renderedSegments.append(std::move(segment));
      }
      rendered.path = rendered.paths.join(QLatin1Char(' '));
    }
    rendered.label = edge.label; rendered.labelPosition = found->labelPosition;
    rendered.style = edge.style; rendered.labelStyle = edge.labelStyle;
    // Resolve linkStyle / edge classDef via the shared cascade. pattern="normal"
    // (class relationship pattern dash is applied painter-side; linkStyle wins).
    QStringList linkStyles = style::compiledClassStyles(
        edge.classes.split(QLatin1Char(' '), Qt::SkipEmptyParts), classDefs);
    linkStyles += edge.style;
    const style::ResolvedEdgeStyle resolvedEdge = style::resolveEdgeStyle(
        QStringLiteral("normal"), linkStyles, false, QString(), themeDefaults);
    rendered.stroke = resolvedEdge.stroke;
    rendered.strokeWidth = resolvedEdge.strokeWidth;
    rendered.strokeDasharray = resolvedEdge.strokeDasharray;
    rendered.pathBounds = pointsBounds(rendered.renderedPoints,
                                       rendered.renderedSegments);
    if (!rendered.label.isEmpty() && rendered.labelPosition) {
      rendered.labelDocument = prepareLabel(rendered.label, scene.style.fontSize);
      rendered.labelSize = flowchart::measureFlowLabel(
          rendered.labelDocument, scene.style.fontFamily,
          scene.style.fontSize, scene.style.lineHeight);
      rendered.labelBounds = QRectF(
          *rendered.labelPosition -
              QPointF(rendered.labelSize.width() / 2.0,
                      rendered.labelSize.height() / 2.0),
          rendered.labelSize);
    }
    if (!edge.startLabelRight.isEmpty()) {
      ClassSceneTerminalLabel terminal;
      terminal.text = edge.startLabelRight;
      const QVector<QPointF>& terminalPoints = rendered.segments.isEmpty()
          ? rendered.points : rendered.segments.first();
      terminal.center = terminalPosition(terminalPoints, true, true,
                                         !edge.arrowTypeStart.isEmpty());
      terminal.document = prepareLabel(terminal.text, scene.style.fontSize);
      terminal.size = flowchart::measureFlowLabel(
          terminal.document, scene.style.fontFamily, 11.0, 12.0);
      rendered.startLabelRight = std::move(terminal);
    }
    if (!edge.endLabelLeft.isEmpty()) {
      ClassSceneTerminalLabel terminal;
      terminal.text = edge.endLabelLeft;
      const QVector<QPointF>& terminalPoints = rendered.segments.isEmpty()
          ? rendered.points : rendered.segments.last();
      terminal.center = terminalPosition(terminalPoints, false, false,
                                         !edge.arrowTypeEnd.isEmpty());
      terminal.document = prepareLabel(terminal.text, scene.style.fontSize);
      terminal.size = flowchart::measureFlowLabel(
          terminal.document, scene.style.fontFamily, 11.0, 12.0);
      rendered.endLabelLeft = std::move(terminal);
    }
    if (handDrawn) {
      for (const QString& path : rendered.paths)
        rendered.roughDrawables.append(rough::roughEdgeDrawable(
            scene::parseSvgPath(path), handDrawnSeed));
      if (!rendered.roughDrawables.isEmpty()) {
        rendered.pathBounds = rough::tightBounds(
            rendered.roughDrawables.first());
        for (qsizetype i = 1; i < rendered.roughDrawables.size(); ++i)
          rendered.pathBounds = rendered.pathBounds.united(
              rough::tightBounds(rendered.roughDrawables.at(i)));
      }
    }
    scene.edges.append(std::move(rendered));
  }

  bool first = true;
  auto unite = [&](const QRectF& bounds) {
    if (first) { scene.bounds = bounds; first = false; }
    else scene.bounds = scene.bounds.united(bounds);
  };
  for (const auto& cluster : scene.clusters)
    unite(handDrawn && cluster.paintedBounds.isValid()
              ? cluster.paintedBounds : cluster.bounds);
  for (const auto& node : scene.nodes)
    unite(handDrawn && node.paintedBounds.isValid()
              ? node.paintedBounds
              : QRectF(node.center.x() - node.size.width() / 2.0,
                       node.center.y() - node.size.height() / 2.0,
                       node.size.width(), node.size.height()));
  for (const auto& edge : scene.edges) {
    if (handDrawn && edge.pathBounds.isValid()) {
      unite(edge.pathBounds);
      continue;
    }
    for (const QPointF& point : edge.renderedPoints)
      unite(QRectF(point, QSizeF(0, 0)));
    for (const QVector<QPointF>& segment : edge.renderedSegments)
      for (const QPointF& point : segment) unite(QRectF(point, QSizeF(0, 0)));
  }
  return scene;
}

namespace {

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r3(r.x())},
          {QStringLiteral("y"), r3(r.y())},
          {QStringLiteral("width"), r3(r.width())},
          {QStringLiteral("height"), r3(r.height())}};
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"), r3(p.x())}, {QStringLiteral("y"), r3(p.y())}};
}

QJsonArray pointsJson(const QVector<QPointF>& pts) {
  QJsonArray a;
  for (const QPointF& p : pts) {
    QJsonArray pair;
    pair.append(r3(p.x()));
    pair.append(r3(p.y()));
    a.append(pair);
  }
  return a;
}

}  // namespace

QJsonObject ClassScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("bounds")] = rectJson(bounds);
  if (handDrawn) {
    o[QStringLiteral("handDrawn")] = true;
    o[QStringLiteral("handDrawnSeed")] = static_cast<int>(handDrawnSeed);
  }

  QJsonArray clustersArray;
  for (const ClassSceneCluster& cluster : clusters) {
    QJsonObject c;
    c[QStringLiteral("id")] = cluster.id;
    if (!cluster.label.isEmpty())
      c[QStringLiteral("label")] = cluster.label;
    c[QStringLiteral("bounds")] = rectJson(cluster.bounds);
    clustersArray.append(c);
  }
  o[QStringLiteral("clusters")] = clustersArray;

  QJsonArray edgesArray;
  for (const ClassSceneEdge& edge : edges) {
    QJsonObject e;
    e[QStringLiteral("id")] = edge.id;
    if (!edge.pattern.isEmpty())
      e[QStringLiteral("pattern")] = edge.pattern;
    if (!edge.markerStart.isEmpty())
      e[QStringLiteral("markerStart")] = edge.markerStart;
    if (!edge.markerEnd.isEmpty())
      e[QStringLiteral("markerEnd")] = edge.markerEnd;
    e[QStringLiteral("path")] = edge.path;
    e[QStringLiteral("points")] = pointsJson(edge.points);
    if (!edge.label.isEmpty())
      e[QStringLiteral("label")] = edge.label;
    if (edge.labelPosition.has_value())
      e[QStringLiteral("labelPosition")] = pointJson(*edge.labelPosition);
    edgesArray.append(e);
  }
  o[QStringLiteral("edges")] = edgesArray;

  QJsonArray nodesArray;
  for (const ClassSceneNode& node : nodes) {
    QJsonObject n;
    n[QStringLiteral("id")] = node.id;
    if (!node.shape.isEmpty())
      n[QStringLiteral("shape")] = node.shape;
    n[QStringLiteral("cx")] = r3(node.center.x());
    n[QStringLiteral("cy")] = r3(node.center.y());
    n[QStringLiteral("width")] = r3(node.size.width());
    n[QStringLiteral("height")] = r3(node.size.height());
    if (!node.fill.isEmpty())
      n[QStringLiteral("fill")] = node.fill;
    if (!node.stroke.isEmpty())
      n[QStringLiteral("stroke")] = node.stroke;
    if (!node.textColor.isEmpty())
      n[QStringLiteral("textColor")] = node.textColor;
    n[QStringLiteral("strokeWidth")] = r3(node.strokeWidth);
    n[QStringLiteral("dividers")] = static_cast<int>(node.localDividers.size());
    nodesArray.append(n);
  }
  o[QStringLiteral("nodes")] = nodesArray;

  QJsonArray markersArray;
  for (const ClassMarkerDefinition& marker : markers) {
    QJsonObject m;
    m[QStringLiteral("type")] = marker.type;
    if (!marker.suffix.isEmpty())
      m[QStringLiteral("suffix")] = marker.suffix;
    markersArray.append(m);
  }
  o[QStringLiteral("markers")] = markersArray;

  return o;
}

SvgMarkerProjection ClassScene::svgMarkerProjection() const {
  SvgMarkerProjection projection;
  for (const ClassMarkerDefinition& marker : markers) {
    SvgMarkerDefinition definition;
    definition.key = marker.suffix;
    definition.idSuffix = QStringLiteral("_class-") + marker.suffix;
    definition.viewBox = marker.viewBox;
    definition.refX = marker.refX; definition.refY = marker.refY;
    definition.markerWidth = marker.markerWidth;
    definition.markerHeight = marker.markerHeight;
    definition.markerUnits = marker.markerUnits;
    definition.orient = marker.orient;
    SvgMarkerChild child;
    child.tag = marker.child.tag;
    child.path = marker.child.path;
    child.points = marker.child.points;
    child.cx = marker.child.cx; child.cy = marker.child.cy;
    child.radius = marker.child.radius;
    child.fill = marker.child.fill;
    child.strokeWidth = marker.child.strokeWidth;
    child.style = marker.child.style;
    if (marker.type == QLatin1String("composition") &&
        marker.suffix == QLatin1String("compositionStart-margin"))
      child.viewBox = QStringLiteral("0 0 15 15");
    const bool filled = marker.type == QLatin1String("composition") ||
                        marker.type == QLatin1String("dependency");
    if (child.fill.isEmpty()) child.fill = filled ? style.lineColor
                                                  : QStringLiteral("none");
    child.stroke = style.lineColor;
    definition.children.append(child);
    projection.definitions.append(definition);
  }
  for (const ClassSceneEdge& source : edges) {
    if (source.markerStart.isEmpty() && source.markerEnd.isEmpty()) continue;
    const qsizetype segmentCount = source.paths.isEmpty() ? 1 : source.paths.size();
    const auto append = [&](const QString& path, qsizetype segment) {
      SvgMarkerEdge edge;
      edge.id = segment == 0 ? source.id
          : source.id + QStringLiteral("-%1").arg(segment);
      edge.cssClass = QStringLiteral("relation");
      edge.path = path;
      if (segment == 0 && !source.markerStart.isEmpty())
        edge.markerStart = source.markerStart + QStringLiteral("Start");
      if (segment + 1 == segmentCount && !source.markerEnd.isEmpty())
        edge.markerEnd = source.markerEnd + QStringLiteral("End");
      edge.stroke = source.stroke.isEmpty() ? style.lineColor : source.stroke;
      edge.strokeWidth = source.strokeWidth;
      edge.strokeDasharray = source.strokeDasharray;
      projection.edges.append(edge);
    };
    if (!source.paths.isEmpty()) {
      for (qsizetype i = 0; i < source.paths.size(); ++i) append(source.paths.at(i), i);
    } else {
      append(source.path, 0);
    }
  }
  return projection;
}

}  // namespace muffin::mermaid::classdiagram
