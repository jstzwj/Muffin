#include "mermaid/wardley/WardleyScene.h"

#include "mermaid/wardley/WardleyScenePainter.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/text/LabelText.h"

#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextCharFormat>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>

namespace muffin::mermaid::wardley {
namespace {

QStringList cssFontFamilies(const QString &expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

QRectF textBounds(const WardleySceneStyle &style, const QString &source,
                  const QPointF &position, qreal size, const QString &anchor,
                  WardleyTextBaseline baseline, bool bold, qreal rotation) {
  if (source.isEmpty() || !(size > 0.0)) return {};
  const QString text = text::collapsedSvgText(source);
  const QStringList stack = cssFontFamilies(style.fontFamily);
  auto cssFont = editor::makeUnhintedCssPixelFont(stack.first(), size);
  if (stack.size() > 1) cssFont.font.setFamilies(stack);
  cssFont.font.setWeight(bold ? QFont::Bold : QFont::Normal);
  const QFontMetricsF qtMetrics(cssFont.font);
  const qreal advance = qtMetrics.horizontalAdvance(text) * cssFont.scale;
  const QRectF ink = QRectF(qtMetrics.boundingRect(text).topLeft() * cssFont.scale,
                            qtMetrics.boundingRect(text).size() * cssFont.scale);
  qreal x = position.x();
  if (anchor == QLatin1String("middle")) x -= advance / 2.0;
  else if (anchor == QLatin1String("end")) x -= advance;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      style.fontFamily, size, bold ? QFont::Bold : QFont::Normal,
      QFont::StyleNormal);
  qreal baselineY = position.y();
  if (baseline == WardleyTextBaseline::Middle ||
      baseline == WardleyTextBaseline::Central)
    baselineY += metrics.xHeight / 2.0;
  QRectF result(x + ink.x(), baselineY - metrics.ascent,
                std::max(ink.width(), advance), metrics.height());
  // SVGGraphicsElement.getBBox() is reported in the element's local user
  // coordinate system and excludes its own transform. Painting still applies
  // rotation in WardleyScenePainter.
  Q_UNUSED(rotation);
  return result;
}

qreal textAdvance(const WardleySceneStyle& style, const QString& source,
                  qreal size, bool bold = false) {
  const QString text = text::collapsedSvgText(source);
  if (text.isEmpty() || !(size > 0.0)) return 0.0;
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = bold ? QFont::Bold : QFont::Normal;
  const qreal advance = flowchart::measureOpenTypeDesignAdvance(
                            document, style.fontFamily, size)
                            .value_or(0.0);
  // Wardley's annotation box uses SVG getComputedTextLength(), which is
  // already represented by the LayoutUnit-quantized measurement above.
  return std::ceil(advance * 64.0 - 1e-9) / 64.0;
}

QString number(qreal value) { return editor::jsNumberToString(value); }

struct Builder {
  WardleyScene scene;

  WardleyPrimitive &append(WardleyPrimitive primitive) {
    scene.primitives.append(std::move(primitive));
    return scene.primitives.last();
  }

  void rect(const QString &role, const QString &parent, const QRectF &value,
            const QString &fill, const QString &stroke = QStringLiteral("none"),
            qreal strokeWidth = 0.0, qreal radius = 0.0) {
    WardleyPrimitive p;
    p.type = WardleyPrimitiveType::Rect;
    p.role = role;
    p.parentClass = parent;
    p.rect = value;
    p.bounds = value;
    p.fill = fill;
    p.stroke = stroke;
    p.strokeWidth = strokeWidth;
    p.rx = radius;
    append(std::move(p));
  }

  WardleyPrimitive &line(const QString &role, const QString &parent,
                         const QLineF &value, const QString &stroke,
                         qreal width = 1.0, QVector<qreal> dash = {}) {
    WardleyPrimitive p;
    p.type = WardleyPrimitiveType::Line;
    p.role = role;
    p.parentClass = parent;
    p.line = value;
    p.bounds = QRectF(value.p1(), value.p2()).normalized();
    p.stroke = stroke;
    p.strokeWidth = width;
    p.dash = std::move(dash);
    return append(std::move(p));
  }

  void circle(const QString &role, const QString &parent, const QPointF &center,
              qreal radius, const QString &fill, const QString &stroke,
              qreal strokeWidth = 1.0) {
    WardleyPrimitive p;
    p.type = WardleyPrimitiveType::Circle;
    p.role = role;
    p.parentClass = parent;
    p.center = center;
    p.radius = radius;
    p.bounds = QRectF(center.x() - radius, center.y() - radius,
                      radius * 2.0, radius * 2.0);
    p.fill = fill;
    p.stroke = stroke;
    p.strokeWidth = strokeWidth;
    append(std::move(p));
  }

  void path(const QString &role, const QString &parent, QPainterPath value,
            QString pathData, const QString &fill, const QString &stroke,
            qreal strokeWidth = 1.0) {
    WardleyPrimitive p;
    p.type = WardleyPrimitiveType::Path;
    p.role = role;
    p.parentClass = parent;
    p.bounds = value.boundingRect();
    p.path = std::move(value);
    p.pathData = std::move(pathData);
    p.fill = fill;
    p.stroke = stroke;
    p.strokeWidth = strokeWidth;
    append(std::move(p));
  }

  WardleyPrimitive &text(const QString &role, const QString &parent,
                         QString value, const QPointF &position, qreal size,
                         const QString &fill, QString anchor = QStringLiteral("start"),
                         WardleyTextBaseline baseline = WardleyTextBaseline::Auto,
                         bool bold = false, qreal rotation = 0.0) {
    WardleyPrimitive p;
    p.type = WardleyPrimitiveType::Text;
    p.role = role;
    p.parentClass = parent;
    p.text = std::move(value);
    p.position = position;
    p.fontSize = size;
    p.fill = fill;
    p.anchor = std::move(anchor);
    p.baseline = baseline;
    p.bold = bold;
    p.rotation = rotation;
    p.bounds = textBounds(scene.style, p.text, p.position, p.fontSize,
                          p.anchor, p.baseline, p.bold, p.rotation);
    return append(std::move(p));
  }
};

QString nodeParentClass(const WardleyNode &node) {
  return QStringLiteral("wardley-node wardley-node--") + node.className;
}

} // namespace

WardleyScene buildWardleyScene(const WardleyData &data, WardleyConfig config,
                               WardleySceneStyle style) {
  Builder out;
  out.scene.config = std::move(config);
  out.scene.style = std::move(style);
  const auto &cfg = out.scene.config;
  const auto &theme = out.scene.style;
  const qreal width = data.size ? data.size->width() : cfg.width;
  const qreal height = data.size ? data.size->height() : cfg.height;
  const qreal chartWidth = width - cfg.padding * 2.0;
  const qreal chartHeight = height - cfg.padding * 2.0;
  const qreal squareSize = cfg.nodeRadius * 1.6;
  const auto projectX = [&](qreal value) {
    return cfg.padding + (value / 100.0) * chartWidth;
  };
  const auto projectY = [&](qreal value) {
    return height - cfg.padding - (value / 100.0) * chartHeight;
  };

  out.scene.bounds = QRectF(0.0, 0.0, width, height);
  out.scene.contentBounds = out.scene.bounds;
  out.scene.viewBoxAttribute = QStringLiteral("0 0 %1 %2").arg(number(width), number(height));
  out.scene.useMaxWidth = cfg.useMaxWidth;

  out.rect(QStringLiteral("wardley-background"), QStringLiteral("wardley-map"),
           QRectF(0.0, 0.0, width, height), theme.backgroundColor);

  if (!data.title.isEmpty())
    out.text(QStringLiteral("wardley-title"), QStringLiteral("wardley-map"),
             data.title, QPointF(width / 2.0, cfg.padding / 2.0),
             cfg.axisFontSize * 1.05, theme.axisTextColor,
             QStringLiteral("middle"), WardleyTextBaseline::Middle, true);

  const QString axes = QStringLiteral("wardley-axes");
  out.line({}, axes,
           QLineF(cfg.padding, height - cfg.padding,
                  width - cfg.padding, height - cfg.padding),
           theme.axisColor);
  out.line({}, axes,
           QLineF(cfg.padding, cfg.padding, cfg.padding, height - cfg.padding),
           theme.axisColor);
  out.text(QStringLiteral("wardley-axis-label wardley-axis-label-x"), axes,
           QStringLiteral("Evolution"),
           QPointF(cfg.padding + chartWidth / 2.0,
                   height - cfg.padding / 4.0),
           cfg.axisFontSize, theme.axisTextColor, QStringLiteral("middle"),
           WardleyTextBaseline::Auto, true);
  out.text(QStringLiteral("wardley-axis-label wardley-axis-label-y"), axes,
           QStringLiteral("Visibility"),
           QPointF(cfg.padding / 3.0, cfg.padding + chartHeight / 2.0),
           cfg.axisFontSize, theme.axisTextColor, QStringLiteral("middle"),
           WardleyTextBaseline::Auto, true, -90.0);

  QVector<QString> stages = data.axes.stages;
  if (stages.isEmpty())
    stages = {QStringLiteral("Genesis"), QStringLiteral("Custom Built"),
              QStringLiteral("Product"), QStringLiteral("Commodity")};
  QVector<QPair<qreal, qreal>> stagePositions;
  if (data.axes.stageBoundaries.size() == stages.size()) {
    qreal previous = 0.0;
    for (qreal boundary : data.axes.stageBoundaries) {
      stagePositions.append({previous, boundary});
      previous = boundary;
    }
  } else {
    for (qsizetype i = 0; i < stages.size(); ++i)
      stagePositions.append({qreal(i) / stages.size(), qreal(i + 1) / stages.size()});
  }
  for (qsizetype i = 0; i < stages.size(); ++i) {
    const qreal start = cfg.padding + stagePositions.at(i).first * chartWidth;
    const qreal end = cfg.padding + stagePositions.at(i).second * chartWidth;
    if (i > 0) {
      auto &divider = out.line({}, QStringLiteral("wardley-stages"),
                               QLineF(start, cfg.padding, start, height - cfg.padding),
                               QStringLiteral("#000"), 1.0, {5.0, 5.0});
      divider.opacity = 0.8;
    }
    out.text(QStringLiteral("wardley-stage-label"), QStringLiteral("wardley-stages"),
             stages.at(i), QPointF((start + end) / 2.0,
                                   height - cfg.padding / 1.5),
             cfg.axisFontSize - 2.0, theme.axisTextColor,
             QStringLiteral("middle"));
  }

  if (cfg.showGrid) {
    for (int i = 1; i < 4; ++i) {
      const qreal ratio = i / 4.0;
      const qreal x = cfg.padding + chartWidth * ratio;
      out.line({}, QStringLiteral("wardley-grid"),
               QLineF(x, cfg.padding, x, height - cfg.padding),
               theme.gridColor, 1.0, {2.0, 6.0});
      const qreal y = height - cfg.padding - chartHeight * ratio;
      out.line({}, QStringLiteral("wardley-grid"),
               QLineF(cfg.padding, y, width - cfg.padding, y),
               theme.gridColor, 1.0, {2.0, 6.0});
    }
  }

  QHash<QString, QPointF> positions;
  QHash<QString, const WardleyNode *> nodes;
  for (const WardleyNode &node : data.nodes) {
    positions.insert(node.id, QPointF(projectX(node.x), projectY(node.y)));
    nodes.insert(node.id, &node);
  }

  QVector<WardleyPrimitive> pipelineBoxes;
  QVector<WardleyPrimitive> pipelineLinks;
  for (const WardleyPipeline &pipeline : data.pipelines) {
    if (pipeline.componentIds.isEmpty()) continue;
    QVector<QString> sorted = pipeline.componentIds;
    std::sort(sorted.begin(), sorted.end(), [&](const QString &a, const QString &b) {
      return nodes.value(a)->x < nodes.value(b)->x;
    });
    for (qsizetype i = 0; i + 1 < sorted.size(); ++i) {
      WardleyPrimitive p;
      p.type = WardleyPrimitiveType::Line;
      p.role = QStringLiteral("wardley-pipeline-evolution-link");
      p.parentClass = QStringLiteral("wardley-pipeline-links");
      p.line = QLineF(positions.value(sorted.at(i)), positions.value(sorted.at(i + 1)));
      p.bounds = QRectF(p.line.p1(), p.line.p2()).normalized();
      p.stroke = theme.linkStroke;
      p.strokeWidth = 1.0;
      p.dash = {4.0, 4.0};
      pipelineLinks.append(std::move(p));
    }
    qreal minX = std::numeric_limits<qreal>::infinity();
    qreal maxX = -std::numeric_limits<qreal>::infinity();
    qreal y = 0.0;
    for (const QString &id : pipeline.componentIds) {
      if (!positions.contains(id)) continue;
      minX = std::min(minX, positions.value(id).x());
      maxX = std::max(maxX, positions.value(id).x());
      y = positions.value(id).y();
    }
    if (std::isfinite(minX) && std::isfinite(maxX)) {
      const qreal boxHeight = cfg.nodeRadius * 4.0;
      const qreal top = y - boxHeight / 2.0;
      if (positions.contains(pipeline.nodeId))
        positions[pipeline.nodeId] = QPointF((minX + maxX) / 2.0,
                                             top - squareSize / 6.0);
      WardleyPrimitive p;
      p.type = WardleyPrimitiveType::Rect;
      p.role = QStringLiteral("wardley-pipeline-box");
      p.parentClass = QStringLiteral("wardley-pipelines");
      p.rect = QRectF(minX - 15.0, top, maxX - minX + 30.0, boxHeight);
      p.bounds = p.rect;
      p.rx = 4.0;
      p.fill = QStringLiteral("none");
      p.stroke = theme.axisColor;
      p.strokeWidth = 1.5;
      pipelineBoxes.append(std::move(p));
    }
  }
  for (auto &p : pipelineBoxes) out.append(std::move(p));
  for (auto &p : pipelineLinks) out.append(std::move(p));

  QHash<QString, QSet<QString>> pipelineMap;
  for (const WardleyPipeline &pipeline : data.pipelines) {
    QSet<QString> componentIds;
    for (const QString &componentId : pipeline.componentIds)
      componentIds.insert(componentId);
    pipelineMap.insert(pipeline.nodeId, std::move(componentIds));
  }
  QVector<const WardleyLink *> validLinks;
  for (const WardleyLink &link : data.links) {
    if (!positions.contains(link.source) || !positions.contains(link.target)) continue;
    if (pipelineMap.value(link.target).contains(link.source)) continue;
    validLinks.append(&link);
  }
  for (const WardleyLink *link : validLinks) {
    const QPointF source = positions.value(link->source);
    const QPointF target = positions.value(link->target);
    const WardleyNode *sourceNode = nodes.value(link->source);
    const WardleyNode *targetNode = nodes.value(link->target);
    const qreal sourceRadius = sourceNode && sourceNode->isPipelineParent
        ? squareSize / std::sqrt(2.0) : cfg.nodeRadius;
    const qreal targetRadius = targetNode && targetNode->isPipelineParent
        ? squareSize / std::sqrt(2.0) : cfg.nodeRadius;
    const QPointF delta = target - source;
    const qreal distance = std::hypot(delta.x(), delta.y());
    QPointF p1 = source;
    QPointF p2 = target;
    if (distance != 0.0) {
      p1 += delta / distance * sourceRadius;
      p2 -= delta / distance * targetRadius;
    } else {
      p1 = p2 = QPointF();
    }
    auto &primitive = out.line(
        link->dashed ? QStringLiteral("wardley-link wardley-link--dashed")
                     : QStringLiteral("wardley-link"),
        QStringLiteral("wardley-links"), QLineF(p1, p2), theme.linkStroke,
        1.0, link->dashed ? QVector<qreal>{6.0, 6.0} : QVector<qreal>{});
    primitive.markerEnd = link->flow == QLatin1String("forward") ||
                          link->flow == QLatin1String("bidirectional");
    primitive.markerStart = link->flow == QLatin1String("backward") ||
                            link->flow == QLatin1String("bidirectional");
    primitive.markerColor = theme.linkStroke;
    primitive.markerSize = 5.0;
  }
  for (const WardleyLink *link : validLinks) {
    if (link->label.isEmpty()) continue;
    const QPointF source = positions.value(link->source);
    const QPointF target = positions.value(link->target);
    const QPointF delta = target - source;
    const qreal distance = std::hypot(delta.x(), delta.y());
    QPointF label = (source + target) / 2.0;
    if (distance != 0.0) label += QPointF(delta.y(), -delta.x()) / distance * 8.0;
    qreal angle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
    if (angle > 90.0 || angle < -90.0) angle += 180.0;
    out.text(QStringLiteral("wardley-link-label"), QStringLiteral("wardley-links"),
             link->label, label, cfg.labelFontSize, theme.axisTextColor,
             QStringLiteral("middle"), WardleyTextBaseline::Middle, false, angle);
  }

  for (const WardleyTrend &trend : data.trends) {
    if (!positions.contains(trend.nodeId)) continue;
    const QPointF origin = positions.value(trend.nodeId);
    const QPointF target(projectX(trend.targetX), projectY(trend.targetY));
    const QPointF delta = target - origin;
    const qreal distance = std::hypot(delta.x(), delta.y());
    const QPointF end = distance > cfg.nodeRadius + 2.0
        ? target - delta / distance * (cfg.nodeRadius + 2.0) : target;
    auto &primitive = out.line(QStringLiteral("wardley-trend"),
                               QStringLiteral("wardley-trends"),
                               QLineF(origin, end), theme.evolutionStroke,
                               1.0, {4.0, 4.0});
    primitive.markerEnd = true;
    primitive.markerColor = theme.evolutionStroke;
    primitive.markerSize = 6.0;
  }

  for (const WardleyNode &node : data.nodes) {
    const QPointF pos = positions.value(node.id);
    const QString parent = nodeParentClass(node);
    if (node.sourceStrategy == QLatin1String("outsource"))
      out.circle(QStringLiteral("wardley-outsource-overlay"), parent, pos,
                 cfg.nodeRadius * 2.0, QStringLiteral("#666"), theme.componentStroke);
    if (node.sourceStrategy == QLatin1String("buy"))
      out.circle(QStringLiteral("wardley-buy-overlay"), parent, pos,
                 cfg.nodeRadius * 2.0, QStringLiteral("#ccc"), theme.componentStroke);
    if (node.sourceStrategy == QLatin1String("build"))
      out.circle(QStringLiteral("wardley-build-overlay"), parent, pos,
                 cfg.nodeRadius * 2.0, QStringLiteral("#eee"), QStringLiteral("#000"));
    if (node.sourceStrategy == QLatin1String("market")) {
      out.circle(QStringLiteral("wardley-market-overlay"), parent, pos,
                 cfg.nodeRadius * 2.0, QStringLiteral("white"), theme.componentStroke);
      const qreal triangle = cfg.nodeRadius * 1.2;
      const qreal dx = triangle * std::cos(M_PI / 6.0);
      const qreal dy = triangle * std::sin(M_PI / 6.0);
      const QPointF top(pos.x(), pos.y() - triangle);
      const QPointF left(pos.x() - dx, pos.y() + dy);
      const QPointF right(pos.x() + dx, pos.y() + dy);
      out.line(QStringLiteral("wardley-market-line"), parent, QLineF(top, left), theme.componentStroke);
      out.line(QStringLiteral("wardley-market-line"), parent, QLineF(left, right), theme.componentStroke);
      out.line(QStringLiteral("wardley-market-line"), parent, QLineF(right, top), theme.componentStroke);
      const qreal dot = cfg.nodeRadius * 0.7;
      out.circle(QStringLiteral("wardley-market-dot"), parent, top, dot,
                 QStringLiteral("white"), theme.componentStroke, 2.0);
      out.circle(QStringLiteral("wardley-market-dot"), parent, left, dot,
                 QStringLiteral("white"), theme.componentStroke, 2.0);
      out.circle(QStringLiteral("wardley-market-dot"), parent, right, dot,
                 QStringLiteral("white"), theme.componentStroke, 2.0);
    } else if (!node.isPipelineParent && node.className != QLatin1String("anchor")) {
      out.circle({}, parent, pos, cfg.nodeRadius, theme.componentFill,
                 theme.componentStroke);
    }
    if (node.isPipelineParent)
      out.rect({}, parent,
               QRectF(pos.x() - squareSize / 2.0, pos.y() - squareSize / 2.0,
                      squareSize, squareSize),
               theme.componentFill, theme.componentStroke, 1.0);
    if (node.inertia) {
      qreal x = node.isPipelineParent ? squareSize / 2.0 + 15.0
                                      : cfg.nodeRadius + 15.0;
      if (!node.sourceStrategy.isEmpty()) x += cfg.nodeRadius + 10.0;
      const qreal lineHeight = node.isPipelineParent ? squareSize : cfg.nodeRadius * 2.0;
      out.line(QStringLiteral("wardley-inertia"), parent,
               QLineF(pos.x() + x, pos.y() - lineHeight / 2.0,
                      pos.x() + x, pos.y() + lineHeight / 2.0),
               theme.componentStroke, 6.0);
    }
    qreal labelX = pos.x();
    qreal labelY = pos.y();
    QString anchor = QStringLiteral("start");
    WardleyTextBaseline baseline = WardleyTextBaseline::Auto;
    bool bold = false;
    if (node.className == QLatin1String("anchor")) {
      labelX += node.labelOffsetX.value_or(0.0);
      labelY += node.labelOffsetY.value_or(-3.0);
      anchor = QStringLiteral("middle");
      baseline = WardleyTextBaseline::Middle;
      bold = true;
    } else {
      qreal xOffset = cfg.nodeLabelOffset;
      qreal yOffset = -cfg.nodeLabelOffset;
      if (!node.sourceStrategy.isEmpty()) { xOffset += 10.0; yOffset -= 10.0; }
      labelX += node.labelOffsetX.value_or(xOffset);
      labelY += node.labelOffsetY.value_or(yOffset);
    }
    out.text(QStringLiteral("wardley-node-label"), parent, node.label,
             QPointF(labelX, labelY), cfg.labelFontSize,
             node.className == QLatin1String("anchor")
                 ? QStringLiteral("#000")
                 : theme.componentLabelColor,
             anchor, baseline, bold);
  }

  if (!data.annotations.isEmpty()) {
    for (const WardleyAnnotation &annotation : data.annotations) {
      const QPointF pos(projectX(annotation.coordinate.x()),
                        projectY(annotation.coordinate.y()));
      out.circle({}, QStringLiteral("wardley-annotation"), pos, 10.0,
                 theme.annotationFill, theme.annotationStroke, 1.5);
      out.text({}, QStringLiteral("wardley-annotation"), QString::number(annotation.number),
               pos, 10.0, theme.annotationTextColor, QStringLiteral("middle"),
               WardleyTextBaseline::Central, true);
    }
    if (data.annotationsBox) {
      qreal boxX = projectX(data.annotationsBox->x());
      qreal boxY = projectY(data.annotationsBox->y());
      QVector<const WardleyAnnotation *> sorted;
      for (const WardleyAnnotation &annotation : data.annotations)
        if (!annotation.text.isEmpty()) sorted.append(&annotation);
      std::sort(sorted.begin(), sorted.end(), [](const auto *a, const auto *b) {
        return a->number < b->number;
      });
      if (!sorted.isEmpty()) {
        qreal maxWidth = 0.0;
        qreal maxHeight = 0.0;
        for (const auto *annotation : sorted) {
          const QString label =
              QStringLiteral("%1. %2").arg(annotation->number).arg(annotation->text);
          auto &text = out.text({}, QStringLiteral("wardley-annotations-box"),
                                label,
                                QPointF(), 11.0, theme.annotationTextColor,
                                QStringLiteral("start"), WardleyTextBaseline::Middle);
          maxWidth = std::max(maxWidth, textAdvance(theme, label, 11.0));
          maxHeight = std::max(maxHeight, text.bounds.height());
          out.scene.primitives.removeLast();
        }
        const qreal boxWidth = maxWidth + 20.0 + 105.0;
        const qreal boxHeight = sorted.size() * 16.0 + 20.0 + maxHeight / 2.0;
        boxX = std::max(cfg.padding, std::min(boxX, width - cfg.padding - boxWidth));
        boxY = std::max(cfg.padding, std::min(boxY, height - cfg.padding - boxHeight));
        out.rect({}, QStringLiteral("wardley-annotations-box"),
                 QRectF(boxX, boxY, boxWidth, boxHeight), theme.annotationFill,
                 theme.annotationStroke, 1.5, 4.0);
        for (qsizetype i = 0; i < sorted.size(); ++i)
          out.text({}, QStringLiteral("wardley-annotations-box"),
                   QStringLiteral("%1. %2").arg(sorted.at(i)->number).arg(sorted.at(i)->text),
                   QPointF(boxX + 10.0, boxY + 10.0 + (i + 1) * 16.0),
                   11.0, theme.annotationTextColor, QStringLiteral("start"),
                   WardleyTextBaseline::Middle);
      }
    }
  }

  for (const WardleyNote &note : data.notes)
    out.text({}, QStringLiteral("wardley-notes"), note.text,
             QPointF(projectX(note.coordinate.x()), projectY(note.coordinate.y())),
             11.0, theme.axisTextColor, QStringLiteral("start"),
             WardleyTextBaseline::Auto, true);

  const auto addArrow = [&](const WardleyAccelerator &arrow, bool left) {
    const qreal x = projectX(arrow.coordinate.x());
    const qreal y = projectY(arrow.coordinate.y());
    QPainterPath path;
    QString pathData;
    if (!left) {
      path.moveTo(x, y - 15.0); path.lineTo(x + 40.0, y - 15.0);
      path.lineTo(x + 40.0, y - 23.0); path.lineTo(x + 60.0, y);
      path.lineTo(x + 40.0, y + 23.0); path.lineTo(x + 40.0, y + 15.0);
      path.lineTo(x, y + 15.0);
      pathData = QStringLiteral("M %1 %2 L %3 %4 L %3 %5 L %6 %7 "
                                "L %3 %8 L %3 %9 L %1 %9 Z")
                     .arg(number(x), number(y - 15.0), number(x + 40.0),
                          number(y - 15.0), number(y - 23.0),
                          number(x + 60.0), number(y), number(y + 23.0),
                          number(y + 15.0));
    } else {
      path.moveTo(x + 60.0, y - 15.0); path.lineTo(x + 20.0, y - 15.0);
      path.lineTo(x + 20.0, y - 23.0); path.lineTo(x, y);
      path.lineTo(x + 20.0, y + 23.0); path.lineTo(x + 20.0, y + 15.0);
      path.lineTo(x + 60.0, y + 15.0);
      pathData = QStringLiteral("M %1 %2 L %3 %2 L %3 %4 L %5 %6 "
                                "L %3 %7 L %3 %8 L %1 %8 Z")
                     .arg(number(x + 60.0), number(y - 15.0),
                          number(x + 20.0), number(y - 23.0), number(x),
                          number(y), number(y + 23.0), number(y + 15.0));
    }
    path.closeSubpath();
    out.path({}, left ? QStringLiteral("wardley-deaccelerators")
                      : QStringLiteral("wardley-accelerators"),
             path, pathData, QStringLiteral("white"), theme.componentStroke, 1.0);
    out.text({}, left ? QStringLiteral("wardley-deaccelerators")
                      : QStringLiteral("wardley-accelerators"),
             arrow.name, QPointF(x + 30.0, y + 30.0), 10.0,
             theme.axisTextColor, QStringLiteral("middle"),
             WardleyTextBaseline::Auto, true);
  };
  for (const auto &arrow : data.accelerators) addArrow(arrow, false);
  for (const auto &arrow : data.deaccelerators) addArrow(arrow, true);
  return out.scene;
}

void WardleyScene::paint(QPainter &painter,
                         const MermaidPaintOptions &options) const {
  paintWardleyScene(*this, painter, options);
}

QJsonObject WardleyScene::toJsonObject() const {
  QJsonObject root;
  root.insert(QStringLiteral("type"), QStringLiteral("wardley"));
  root.insert(QStringLiteral("viewBox"), viewBoxAttribute);
  root.insert(QStringLiteral("useMaxWidth"), useMaxWidth);
  QJsonArray values;
  for (const WardleyPrimitive &p : primitives) {
    QJsonObject value;
    static const char *types[] = {"rect", "line", "circle", "path", "text"};
    value.insert(QStringLiteral("tag"), QLatin1String(types[int(p.type)]));
    value.insert(QStringLiteral("role"), p.role);
    value.insert(QStringLiteral("parentClass"), p.parentClass);
    value.insert(QStringLiteral("text"), p.text);
    value.insert(QStringLiteral("x"), p.position.x());
    value.insert(QStringLiteral("y"), p.position.y());
    value.insert(QStringLiteral("fill"), p.fill);
    value.insert(QStringLiteral("stroke"), p.stroke);
    values.append(value);
  }
  root.insert(QStringLiteral("primitives"), values);
  return root;
}

} // namespace muffin::mermaid::wardley
