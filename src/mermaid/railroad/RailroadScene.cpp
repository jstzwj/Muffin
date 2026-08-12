#include "mermaid/railroad/RailroadScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/railroad/RailroadScenePainter.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/text/ChromiumTextMetrics.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>

namespace muffin::mermaid::railroad {

void RailroadScene::paint(QPainter& painter,
                          const MermaidPaintOptions& options) const {
  paintRailroadScene(*this, painter, options);
}

namespace {

QString number(qreal value) { return editor::jsNumberToString(value); }

QJsonObject pointJson(const QPointF& point) {
  return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

class PathBuilder {
public:
  PathBuilder& moveTo(qreal x, qreal y) {
    d_ += QStringLiteral("M %1 %2 ").arg(number(x), number(y));
    return *this;
  }
  PathBuilder& lineTo(qreal x, qreal y) {
    d_ += QStringLiteral("L %1 %2 ").arg(number(x), number(y));
    return *this;
  }
  PathBuilder& horizontalTo(qreal x) {
    d_ += QStringLiteral("H %1 ").arg(number(x));
    return *this;
  }
  PathBuilder& verticalTo(qreal y) {
    d_ += QStringLiteral("V %1 ").arg(number(y));
    return *this;
  }
  PathBuilder& arcTo(qreal rx, qreal ry, qreal rotation, bool largeArc,
                     bool sweep, qreal x, qreal y) {
    d_ += QStringLiteral("A %1 %2 %3 %4 %5 %6 %7 ")
              .arg(number(rx), number(ry), number(rotation),
                   largeArc ? QStringLiteral("1") : QStringLiteral("0"),
                   sweep ? QStringLiteral("1") : QStringLiteral("0"),
                   number(x), number(y));
    return *this;
  }
  QString build() const { return d_.trimmed(); }

private:
  QString d_;
};

struct TextDimensions {
  qreal width = 0.0;
  qreal height = 0.0;
};

struct LayoutResult {
  qreal width = 0.0;
  qreal height = 0.0;
  qreal up = 0.0;
  qreal down = 0.0;
  QVector<RailroadPrimitive> primitives;
};

QString visibleSvgText(QString text) {
  static const QRegularExpression whitespace(QStringLiteral("[\\t\\n\\f\\r ]+"));
  text.replace(whitespace, QStringLiteral(" "));
  while (!text.isEmpty() &&
         QStringView(u" \t\n\f\r").contains(text.front()))
    text.remove(0, 1);
  while (!text.isEmpty() &&
         QStringView(u" \t\n\f\r").contains(text.back()))
    text.chop(1);
  return text;
}

void translate(QVector<RailroadPrimitive>& primitives, const QPointF& offset) {
  for (RailroadPrimitive& primitive : primitives)
    primitive.translation += offset;
}

class Builder {
public:
  explicit Builder(const RailroadConfig& config) : config_(config) {}

  TextDimensions measure(const QString& text) const {
    const QString visible = visibleSvgText(text);
    qreal advance = 0.0;
    if (const auto shaped = textmetrics::harfBuzzAdvance(
            visible, config_.fontFamily, config_.fontSize))
      advance = std::ceil(*shaped * 64.0 - 1e-9) / 64.0;
    else
      advance = editor::makeUnhintedCssPixelFont(
                  editor::firstFontFamily(config_.fontFamily), config_.fontSize)
                  .horizontalAdvance(visible);
    flowchart::FlowLabelDocument label;
    label.text = visible;
    QRectF chromiumBounds = flowchart::measureChromiumSvgTextBounds(
        label, config_.fontFamily, config_.fontSize, QFont::Normal, 1.0, false,
        true);
    const auto hbInk = textmetrics::harfBuzzInkBounds(
        visible, config_.fontFamily, config_.fontSize);
    // Blink's SVG text bbox snaps a positive leading side-bearing to the
    // current pixel at the observed 17/64px threshold; below it the antialias
    // fringe occupies the preceding pixel.
    if (hbInk && hbInk->left() >= 17.0 / 64.0 &&
        chromiumBounds.left() < 0.0)
      chromiumBounds.setLeft(0.0);
    const qreal chromium = chromiumBounds.width();
    const qreal width = std::max(advance, chromium);
    const auto vertical = flowchart::flowLabelFontBoundingMetrics(
        editor::firstFontFamily(config_.fontFamily), config_.fontSize);
    qreal height = vertical.height();
    if (!(height > 0.0))
      height = std::ceil(config_.fontSize * 1.35);
    return {width, height};
  }

  RailroadPrimitive path(const QString& d) {
    RailroadPrimitive primitive;
    primitive.kind = RailroadPrimitiveKind::Path;
    primitive.cssClass = QStringLiteral("railroad-line");
    primitive.pathData = d;
    primitive.path = scene::parseSvgPath(d);
    primitive.stroke = config_.lineColor;
    primitive.strokeWidth = config_.strokeWidth;
    primitive.paintOrder = nextPaintOrder_++;
    return primitive;
  }

  LayoutResult expression(const RailroadNode& node) {
    switch (node.type) {
      case RailroadNodeType::Terminal: return terminal(node.text);
      case RailroadNodeType::NonTerminal: return nonTerminal(node.text);
      case RailroadNodeType::Sequence: return sequence(node.children);
      case RailroadNodeType::Choice: return choice(node.children);
      case RailroadNodeType::Optional:
        return optional(node.children.isEmpty() ? RailroadNode{} : node.children.front());
      case RailroadNodeType::Repetition:
        return repetition(node.children.isEmpty() ? RailroadNode{} : node.children.front(),
                          node.min);
      case RailroadNodeType::Special: return special(node.text);
    }
    return {};
  }

  RailroadRuleGeometry rule(const RailroadRule& source, qreal y,
                            QVector<RailroadPrimitive>& output) {
    RailroadRuleGeometry geometry;
    geometry.name = source.name;
    geometry.y = y;
    const QString ruleName = source.name + QStringLiteral(" =");
    const qreal nameWidth = measure(ruleName).width + 20.0;
    const qreal definitionX = nameWidth + 20.0;
    LayoutResult result = expression(source.definition);
    const qreal baselineY = std::max<qreal>(20.0, result.up);
    const qreal definitionY = baselineY - result.up;
    translate(result.primitives, QPointF(definitionX, definitionY + y));
    output += std::move(result.primitives);

    RailroadPrimitive name;
    name.kind = RailroadPrimitiveKind::Text;
    name.cssClass = QStringLiteral("railroad-rule-name");
    name.position = QPointF(0.0, baselineY);
    name.translation = QPointF(0.0, y);
    name.text = ruleName;
    name.fill = config_.ruleNameColor;
    name.bold = true;
    name.paintOrder = nextPaintOrder_++;
    output.append(std::move(name));

    RailroadPrimitive start;
    start.kind = RailroadPrimitiveKind::Circle;
    start.cssClass = QStringLiteral("railroad-start");
    start.rect = QRectF(nameWidth - config_.markerRadius,
                        baselineY - config_.markerRadius,
                        config_.markerRadius * 2.0,
                        config_.markerRadius * 2.0);
    start.translation = QPointF(0.0, y);
    start.fill = config_.markerFill;
    start.paintOrder = nextPaintOrder_++;
    output.append(std::move(start));

    const qreal endCenter = definitionX + result.width + 10.0;
    RailroadPrimitive end;
    end.kind = RailroadPrimitiveKind::Circle;
    end.cssClass = QStringLiteral("railroad-end");
    end.rect = QRectF(endCenter - config_.markerRadius,
                      baselineY - config_.markerRadius,
                      config_.markerRadius * 2.0,
                      config_.markerRadius * 2.0);
    end.translation = QPointF(0.0, y);
    end.fill = config_.markerFill;
    end.paintOrder = nextPaintOrder_++;
    output.append(std::move(end));

    RailroadPrimitive firstLine =
        path(PathBuilder().moveTo(nameWidth + config_.markerRadius, baselineY)
                 .lineTo(definitionX, baselineY)
                 .build());
    firstLine.translation = QPointF(0.0, y);
    output.append(std::move(firstLine));
    RailroadPrimitive secondLine =
        path(PathBuilder().moveTo(definitionX + result.width, baselineY)
                 .lineTo(definitionX + result.width + 10.0 - config_.markerRadius,
                         baselineY)
                 .build());
    secondLine.translation = QPointF(0.0, y);
    output.append(std::move(secondLine));

    geometry.baselineY = baselineY;
    geometry.definitionX = definitionX;
    geometry.height = std::max<qreal>(
        40.0, definitionY + result.height + config_.padding * 2.0);
    geometry.width = definitionX + result.width + 10.0 + config_.markerRadius;
    return geometry;
  }

private:
  LayoutResult leafBox(const QString& cssClass, const QString& text,
                       const QString& fill, const QString& stroke, qreal rx,
                       bool dashed = false) {
    const TextDimensions dimensions = measure(text);
    LayoutResult result;
    result.width = dimensions.width + config_.padding * 2.0;
    result.height = dimensions.height + config_.padding * 2.0;
    result.up = result.height / 2.0;
    result.down = result.height / 2.0;

    RailroadPrimitive rect;
    rect.kind = RailroadPrimitiveKind::Rect;
    rect.cssClass = cssClass;
    rect.rect = QRectF(0.0, 0.0, result.width, result.height);
    rect.fill = fill;
    rect.stroke = stroke;
    rect.strokeWidth = config_.strokeWidth;
    rect.rx = rx;
    rect.ry = rx;
    if (dashed) rect.dash = {5.0, 3.0};
    rect.paintOrder = nextPaintOrder_++;
    result.primitives.append(std::move(rect));

    RailroadPrimitive label;
    label.kind = RailroadPrimitiveKind::Text;
    label.cssClass = cssClass;
    label.position = QPointF(result.width / 2.0, result.height / 2.0);
    label.text = text;
    label.fill = cssClass == QLatin1String("railroad-terminal")
                     ? config_.terminalTextColor
                     : config_.nonTerminalTextColor;
    label.middleAnchor = true;
    label.baseline = RailroadTextBaseline::Middle;
    label.paintOrder = nextPaintOrder_++;
    result.primitives.append(std::move(label));
    return result;
  }

  LayoutResult terminal(const QString& value) {
    return leafBox(QStringLiteral("railroad-terminal"), value,
                   config_.terminalFill, config_.terminalStroke, 10.0);
  }

  LayoutResult nonTerminal(const QString& name) {
    return leafBox(QStringLiteral("railroad-nonterminal"), name,
                   config_.nonTerminalFill, config_.nonTerminalStroke, 0.0);
  }

  LayoutResult special(const QString& text) {
    return leafBox(QStringLiteral("railroad-special"),
                   QStringLiteral("? ") + text + QStringLiteral(" ?"),
                   config_.specialFill, config_.specialStroke, 0.0, true);
  }

  LayoutResult sequence(const QVector<RailroadNode>& elements) {
    QVector<LayoutResult> children;
    qreal totalWidth = 0.0;
    qreal maxUp = 0.0;
    qreal maxDown = 0.0;
    for (const RailroadNode& element : elements) {
      LayoutResult child = expression(element);
      totalWidth += child.width;
      maxUp = std::max(maxUp, child.up);
      maxDown = std::max(maxDown, child.down);
      children.append(std::move(child));
    }
    totalWidth += (children.size() - 1) * config_.horizontalSeparation;
    LayoutResult result{totalWidth, maxUp + maxDown, maxUp, maxDown, {}};
    qreal x = 0.0;
    for (qsizetype i = 0; i < children.size(); ++i) {
      LayoutResult& child = children[i];
      const qreal y = maxUp - child.up;
      translate(child.primitives, QPointF(x, y));
      result.primitives += std::move(child.primitives);
      if (i + 1 < children.size()) {
        const qreal first = x + child.width;
        result.primitives.append(
            path(PathBuilder().moveTo(first, maxUp)
                     .lineTo(first + config_.horizontalSeparation, maxUp)
                     .build()));
      }
      x += child.width + config_.horizontalSeparation;
    }
    return result;
  }

  LayoutResult choice(const QVector<RailroadNode>& alternatives) {
    QVector<LayoutResult> children;
    qreal maxWidth = 0.0;
    qreal totalHeight = 0.0;
    for (const RailroadNode& alternative : alternatives) {
      LayoutResult child = expression(alternative);
      maxWidth = std::max(maxWidth, child.width);
      totalHeight += child.height;
      children.append(std::move(child));
    }
    totalHeight += (children.size() - 1) * config_.verticalSeparation;
    const qreal radius = config_.arcRadius;
    const qreal totalWidth = maxWidth + radius * 4.0;
    const qreal centerY = totalHeight / 2.0;
    LayoutResult result{totalWidth, totalHeight, centerY,
                        totalHeight - centerY, {}};
    qreal y = 0.0;
    for (LayoutResult& child : children) {
      const qreal elemCenterY = y + child.up;
      const qreal elemX = radius * 2.0 + (maxWidth - child.width) / 2.0;
      translate(child.primitives, QPointF(elemX, y));
      result.primitives += std::move(child.primitives);
      const bool below = elemCenterY > centerY;

      PathBuilder left;
      if (elemCenterY == centerY) {
        left.moveTo(0.0, centerY).lineTo(elemX, elemCenterY);
      } else {
        left.moveTo(0.0, centerY)
            .arcTo(radius, radius, 0.0, false, below, radius,
                   centerY + (below ? radius : -radius))
            .lineTo(radius, elemCenterY - (below ? radius : -radius))
            .arcTo(radius, radius, 0.0, false, !below, radius * 2.0,
                   elemCenterY)
            .lineTo(elemX, elemCenterY);
      }
      result.primitives.append(path(left.build()));

      const qreal rightStart = elemX + child.width;
      const qreal laneX = totalWidth - radius * 2.0;
      PathBuilder right;
      if (elemCenterY == centerY) {
        right.moveTo(rightStart, elemCenterY).lineTo(totalWidth, centerY);
      } else {
        right.moveTo(rightStart, elemCenterY)
            .lineTo(laneX, elemCenterY)
            .arcTo(radius, radius, 0.0, false, !below,
                   totalWidth - radius,
                   elemCenterY + (below ? -radius : radius))
            .lineTo(totalWidth - radius,
                    centerY + (below ? radius : -radius))
            .arcTo(radius, radius, 0.0, false, below, totalWidth, centerY);
      }
      result.primitives.append(path(right.build()));
      y += child.height + config_.verticalSeparation;
    }
    return result;
  }

  LayoutResult optional(const RailroadNode& element) {
    LayoutResult inner = expression(element);
    const qreal radius = config_.arcRadius;
    const qreal arcHeight = radius * 2.0;
    const qreal totalWidth = inner.width + radius * 4.0;
    const qreal totalHeight = inner.height + arcHeight;
    const qreal elemX = radius * 2.0;
    const qreal elemY = arcHeight;
    const qreal centerY = elemY + inner.up;
    LayoutResult result{totalWidth, totalHeight, centerY,
                        totalHeight - centerY, {}};
    translate(inner.primitives, QPointF(elemX, elemY));
    result.primitives += std::move(inner.primitives);
    result.primitives.append(path(PathBuilder().moveTo(0.0, centerY)
                                      .lineTo(radius * 2.0, centerY)
                                      .build()));
    result.primitives.append(path(PathBuilder().moveTo(elemX + inner.width, centerY)
                                      .lineTo(totalWidth, centerY)
                                      .build()));
    result.primitives.append(path(
        PathBuilder().moveTo(0.0, centerY)
            .arcTo(radius, radius, 0.0, false, false, radius, centerY - radius)
            .lineTo(radius, radius)
            .arcTo(radius, radius, 0.0, false, true, radius * 2.0, 0.0)
            .lineTo(totalWidth - radius * 2.0, 0.0)
            .arcTo(radius, radius, 0.0, false, true, totalWidth - radius, radius)
            .lineTo(totalWidth - radius, centerY - radius)
            .arcTo(radius, radius, 0.0, false, false, totalWidth, centerY)
            .build()));
    return result;
  }

  LayoutResult repetition(const RailroadNode& element, qreal min) {
    LayoutResult inner = expression(element);
    const qreal radius = config_.arcRadius;
    const qreal arcHeight = radius * 2.0;
    const qreal totalWidth = inner.width + radius * 4.0;
    const bool bypass = min == 0.0;
    const qreal totalHeight = inner.height + arcHeight + (bypass ? arcHeight : 0.0);
    const qreal elemX = radius * 2.0;
    const qreal elemY = bypass ? arcHeight : 0.0;
    const qreal centerY = elemY + inner.up;
    LayoutResult result{totalWidth, totalHeight, centerY,
                        totalHeight - centerY, {}};
    translate(inner.primitives, QPointF(elemX, elemY));
    result.primitives += std::move(inner.primitives);
    result.primitives.append(path(PathBuilder().moveTo(0.0, centerY)
                                      .lineTo(radius * 2.0, centerY)
                                      .build()));
    result.primitives.append(path(PathBuilder().moveTo(elemX + inner.width, centerY)
                                      .lineTo(totalWidth, centerY)
                                      .build()));
    const qreal loopY = elemY + inner.height + radius;
    result.primitives.append(path(
        PathBuilder().moveTo(elemX + inner.width, centerY)
            .arcTo(radius, radius, 0.0, false, true,
                   elemX + inner.width + radius, centerY + radius)
            .lineTo(elemX + inner.width + radius, loopY)
            .arcTo(radius, radius, 0.0, false, true, elemX + inner.width,
                   loopY + radius)
            .lineTo(radius * 2.0, loopY + radius)
            .arcTo(radius, radius, 0.0, false, true, radius, loopY)
            .lineTo(radius, centerY + radius)
            .arcTo(radius, radius, 0.0, false, true, radius * 2.0, centerY)
            .build()));
    if (bypass) {
      result.primitives.append(path(
          PathBuilder().moveTo(0.0, centerY)
              .arcTo(radius, radius, 0.0, false, false, radius,
                     centerY - radius)
              .lineTo(radius, radius)
              .arcTo(radius, radius, 0.0, false, true, radius * 2.0, 0.0)
              .lineTo(totalWidth - radius * 2.0, 0.0)
              .arcTo(radius, radius, 0.0, false, true, totalWidth - radius,
                     radius)
              .lineTo(totalWidth - radius, centerY - radius)
              .arcTo(radius, radius, 0.0, false, false, totalWidth, centerY)
              .build()));
    }
    return result;
  }

  const RailroadConfig& config_;
  int nextPaintOrder_ = 0;
};

}  // namespace

RailroadScene buildRailroadScene(const RailroadData& data,
                                 RailroadConfig config) {
  RailroadScene scene;
  scene.config = std::move(config);
  scene.dialect = data.dialect;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  if (data.rules.isEmpty()) {
    scene.bounds = QRectF(0.0, 0.0, 200.0, 100.0);
    scene.rasterBounds = scene.bounds;
    return scene;
  }

  Builder builder(scene.config);
  qreal y = scene.config.padding;
  qreal maxWidth = 0.0;
  for (const RailroadRule& rule : data.rules) {
    RailroadRuleGeometry geometry = builder.rule(rule, y, scene.primitives);
    y += geometry.height + scene.config.verticalSeparation;
    maxWidth = std::max(maxWidth, geometry.width);
    scene.rules.append(std::move(geometry));
  }
  scene.bounds = QRectF(0.0, 0.0, maxWidth + scene.config.padding * 2.0,
                        y + scene.config.padding);
  qreal clientWidth = scene.bounds.width();
  qreal clientHeight = scene.bounds.height();
  if (scene.config.useMaxWidth && clientWidth > 0.0) {
    clientWidth = std::floor(clientWidth * 64.0) / 64.0;
    clientHeight = std::floor(
        clientWidth * scene.bounds.height() / scene.bounds.width() * 64.0) /
        64.0;
  }
  scene.rasterBounds = QRectF(
      scene.bounds.topLeft(), QSizeF(qRound(clientWidth), qRound(clientHeight)));
  return scene;
}

QJsonObject RailroadScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("type")] = QStringLiteral("railroad");
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("rasterBounds")] = rectJson(rasterBounds);
  root[QStringLiteral("useMaxWidth")] = config.useMaxWidth;
  root[QStringLiteral("title")] = title;
  root[QStringLiteral("accTitle")] = accTitle;
  root[QStringLiteral("accDescr")] = accDescr;
  QJsonArray ruleArray;
  for (const RailroadRuleGeometry& rule : rules) {
    ruleArray.append(QJsonObject{{QStringLiteral("name"), rule.name},
                                 {QStringLiteral("y"), rule.y},
                                 {QStringLiteral("width"), rule.width},
                                 {QStringLiteral("height"), rule.height},
                                 {QStringLiteral("baselineY"), rule.baselineY},
                                 {QStringLiteral("definitionX"), rule.definitionX}});
  }
  root[QStringLiteral("rules")] = ruleArray;
  QJsonArray primitiveArray;
  for (const RailroadPrimitive& primitive : primitives) {
    QJsonObject object;
    object[QStringLiteral("kind")] =
        primitive.kind == RailroadPrimitiveKind::Rect
            ? QStringLiteral("rect")
            : primitive.kind == RailroadPrimitiveKind::Circle
                  ? QStringLiteral("circle")
                  : primitive.kind == RailroadPrimitiveKind::Path
                        ? QStringLiteral("path")
                        : QStringLiteral("text");
    object[QStringLiteral("class")] = primitive.cssClass;
    object[QStringLiteral("translation")] = pointJson(primitive.translation);
    object[QStringLiteral("rect")] = rectJson(primitive.rect);
    object[QStringLiteral("pathData")] = primitive.pathData;
    object[QStringLiteral("text")] = primitive.text;
    object[QStringLiteral("position")] = pointJson(primitive.position);
    object[QStringLiteral("fill")] = primitive.fill;
    object[QStringLiteral("stroke")] = primitive.stroke;
    object[QStringLiteral("strokeWidth")] = primitive.strokeWidth;
    object[QStringLiteral("rx")] = primitive.rx;
    object[QStringLiteral("bold")] = primitive.bold;
    object[QStringLiteral("italic")] = primitive.italic;
    object[QStringLiteral("middleAnchor")] = primitive.middleAnchor;
    object[QStringLiteral("middleBaseline")] =
        primitive.baseline == RailroadTextBaseline::Middle;
    object[QStringLiteral("paintOrder")] = primitive.paintOrder;
    QJsonArray dash;
    for (qreal value : primitive.dash) dash.append(value);
    object[QStringLiteral("dash")] = dash;
    primitiveArray.append(std::move(object));
  }
  root[QStringLiteral("primitives")] = primitiveArray;
  return root;
}

}  // namespace muffin::mermaid::railroad
