#include "mermaid/venn/VennScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/venn/VennScenePainter.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace muffin::mermaid::venn {
namespace {

QJsonValue scalar(const QJsonValue& value, const QJsonValue& fallback) {
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

QString keyFor(const QStringList& sets) { return sets.join(QLatin1Char('|')); }

QPainterPath areaPainterPath(const layout::Area& area) {
  QPainterPath path;
  if (area.arcs.isEmpty()) {
    path.moveTo(0.0, 0.0);
    return path;
  }
  if (area.arcs.size() == 1) {
    const layout::Circle& circle = area.arcs.front().circle;
    path.addEllipse(QPointF(circle.x, circle.y), circle.radius, circle.radius);
    return path;
  }

  path.moveTo(area.arcs.front().p2);
  for (const layout::Arc& arc : area.arcs) {
    const qreal start = std::atan2(arc.circle.y - arc.p2.y(),
                                   arc.p2.x() - arc.circle.x) *
                        180.0 / std::numbers::pi_v<qreal>;
    const qreal end = std::atan2(arc.circle.y - arc.p1.y(),
                                 arc.p1.x() - arc.circle.x) *
                      180.0 / std::numbers::pi_v<qreal>;
    qreal sweep = end - start;
    if (arc.sweep) {
      while (sweep > 0.0) sweep -= 360.0;
      if (arc.large && sweep > -180.0) sweep -= 360.0;
      if (!arc.large && sweep < -180.0) sweep += 360.0;
    } else {
      while (sweep < 0.0) sweep += 360.0;
      if (arc.large && sweep < 180.0) sweep += 360.0;
      if (!arc.large && sweep > 180.0) sweep -= 360.0;
    }
    path.arcTo(QRectF(arc.circle.x - arc.circle.radius,
                      arc.circle.y - arc.circle.radius,
                      arc.circle.radius * 2.0, arc.circle.radius * 2.0),
               start, sweep);
  }
  path.closeSubpath();
  return path;
}

QVector<VennSubset> ensurePairwise(QVector<VennSubset> subsets) {
  QSet<QString> keys;
  QMap<QString, double> singletonSizes;
  for (const VennSubset& subset : std::as_const(subsets)) {
    keys.insert(keyFor(subset.sets));
    if (subset.sets.size() == 1)
      singletonSizes.insert(subset.sets.front(), subset.size);
  }
  QVector<VennSubset> synthetic;
  for (const VennSubset& subset : std::as_const(subsets)) {
    if (subset.sets.size() < 3) continue;
    QStringList members = subset.sets;
    std::sort(members.begin(), members.end());
    for (int i = 0; i < members.size() - 1; ++i) {
      for (int j = i + 1; j < members.size(); ++j) {
        const QStringList pair{members.at(i), members.at(j)};
        const QString key = keyFor(pair);
        if (keys.contains(key)) continue;
        keys.insert(key);
        const bool complete = singletonSizes.contains(pair.at(0)) &&
                              singletonSizes.contains(pair.at(1));
        synthetic.append({pair,
                          complete ? std::min(singletonSizes.value(pair.at(0)),
                                              singletonSizes.value(pair.at(1))) /
                                         4.0
                                   : 2.5,
                          QString(), false});
      }
    }
  }
  subsets += synthetic;
  return subsets;
}

QVector<VennSubset> filterZeroSets(const QVector<VennSubset>& subsets) {
  QSet<QString> removed;
  for (const VennSubset& subset : subsets)
    if (subset.sets.size() == 1 && subset.size == 0.0)
      removed.insert(subset.sets.front());
  QVector<VennSubset> result;
  for (const VennSubset& subset : subsets) {
    bool skip = false;
    for (const QString& set : subset.sets)
      if (removed.contains(set)) skip = true;
    if (!skip) result.append(subset);
  }
  return result;
}

QMap<QString, QMap<QString, QString>> stylesByKey(
    const QVector<VennStyleEntry>& entries) {
  QMap<QString, QMap<QString, QString>> result;
  for (const VennStyleEntry& entry : entries) {
    QMap<QString, QString>& values = result[keyFor(entry.targets)];
    for (const auto& declaration : entry.declarations)
      values.insert(declaration.first, declaration.second);
  }
  return result;
}

QStringList cssFamilies(const QString& expression) {
  QStringList result;
  for (QString family : expression.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') &&
          family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') &&
          family.back() == QLatin1Char('\''))))
      family = family.mid(1, family.size() - 2);
    if (!family.isEmpty()) result.append(family);
  }
  if (result.isEmpty()) result.append(QStringLiteral("Noto Sans"));
  return result;
}

editor::CssPixelFont fontFor(const VennSceneStyle& style, qreal size) {
  const QStringList families = cssFamilies(style.fontFamily);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  return font;
}

QStringList wrapLabel(const QString& label, qreal radius,
                      const VennSceneStyle& style, qreal fontSize) {
  QStringList words = label.split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts);
  if (words.isEmpty()) return {QString()};
  const qreal minimumCharacters = qreal(label.size() + words.size()) / 3.0;
  const editor::CssPixelFont font = fontFor(style, fontSize);
  const QFontMetricsF metrics(font.font);
  QStringList lines;
  QStringList line{words.takeFirst()};
  while (!words.isEmpty()) {
    const QString word = words.takeFirst();
    line.append(word);
    const QString joined = line.join(QLatin1Char(' '));
    if (joined.size() > minimumCharacters &&
        metrics.horizontalAdvance(joined) * font.scale > radius) {
      line.removeLast();
      lines.append(line.join(QLatin1Char(' ')));
      line = {word};
    }
  }
  lines.append(line.join(QLatin1Char(' ')));
  return lines;
}

qreal strokeWidth(const QString& value, qreal fallback) {
  if (value.trimmed().isEmpty()) return fallback;
  const QRegularExpression expression(
      QStringLiteral(R"(^\s*([+-]?(?:\d+(?:\.\d+)?|\.\d+)))"));
  const auto match = expression.match(value);
  return match.hasMatch() ? match.captured(1).toDouble() : fallback;
}

qreal opacity(const QString& value, qreal fallback) {
  bool ok = false;
  const qreal parsed = value.toDouble(&ok);
  return ok ? std::clamp(parsed, 0.0, 1.0) : fallback;
}

QJsonObject rectJson(const QRectF& rect) {
  return {{QStringLiteral("x"), rect.x()},
          {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

QJsonObject pointJson(const QPointF& point) {
  return {{QStringLiteral("x"), point.x()},
          {QStringLiteral("y"), point.y()}};
}

}  // namespace

void VennScene::paint(QPainter& painter,
                      const MermaidPaintOptions& options) const {
  paintVennScene(*this, painter, options);
}

QJsonObject VennScene::toJsonObject() const {
  QJsonArray areaJson;
  for (const VennAreaGeometry& area : areas) {
    QJsonArray sets;
    for (const QString& set : area.data.sets) sets.append(set);
    QJsonArray circles;
    for (const layout::Circle& circle : area.circles)
      circles.append(QJsonObject{{QStringLiteral("set"), circle.set},
                                 {QStringLiteral("x"), circle.x},
                                 {QStringLiteral("y"), circle.y},
                                 {QStringLiteral("radius"), circle.radius}});
    QJsonArray lines;
    for (const QString& line : area.label.lines) lines.append(line);
    areaJson.append(QJsonObject{{QStringLiteral("sets"), sets},
                                {QStringLiteral("key"), area.key},
                                {QStringLiteral("class"), area.cssClass},
                                {QStringLiteral("path"), area.rawPath},
                                {QStringLiteral("circles"), circles},
                                {QStringLiteral("text"), pointJson(area.textCenter)},
                                {QStringLiteral("label"), area.label.source},
                                {QStringLiteral("lines"), lines},
                                {QStringLiteral("fill"), area.fill},
                                {QStringLiteral("stroke"), area.stroke},
                                {QStringLiteral("fillOpacity"), area.fillOpacity},
                                {QStringLiteral("strokeOpacity"), area.strokeOpacity},
                                {QStringLiteral("strokeWidth"), area.strokeWidth},
                                {QStringLiteral("rough"), area.rough}});
  }
  QJsonArray nodes;
  for (const VennTextNodeGeometry& node : textNodes)
    nodes.append(QJsonObject{{QStringLiteral("area"), node.areaKey},
                             {QStringLiteral("id"), node.id},
                             {QStringLiteral("text"), node.source},
                             {QStringLiteral("box"), rectJson(node.box)},
                             {QStringLiteral("color"), node.color},
                             {QStringLiteral("fontSize"), node.fontSize}});
  return {{QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("viewBox"), viewBoxAttribute},
          {QStringLiteral("useMaxWidth"), useMaxWidth},
          {QStringLiteral("title"), title},
          {QStringLiteral("titleHeight"), titleHeight},
          {QStringLiteral("areas"), areaJson},
          {QStringLiteral("textNodes"), nodes}};
}

VennScene buildVennScene(const VennData& data, VennConfig config,
                         VennSceneStyle style) {
  VennScene scene;
  scene.config = config;
  scene.style = std::move(style);
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;

  const QJsonValue widthValue = scalar(config.width, 800.0);
  const QJsonValue heightValue = scalar(config.height, 450.0);
  const QJsonValue paddingValue = scalar(config.padding, 8.0);
  const qreal width = editor::jsNumberValue(widthValue);
  const qreal height = editor::jsNumberValue(heightValue);
  scene.useMaxWidth = editor::truthyConfigValue(scalar(config.useMaxWidth, true));
  scene.useDebugLayout =
      editor::truthyConfigValue(scalar(config.useDebugLayout, false));
  scene.scale = width / 1600.0;
  scene.titleHeight = data.title.isEmpty() ? 0.0 : 48.0 * scene.scale;
  scene.viewBoxAttribute =
      QStringLiteral("0 0 %1 %2")
          .arg(editor::jsNumberToString(width), editor::jsNumberToString(height));
  scene.bounds = QRectF(0.0, 0.0, std::isfinite(width) ? qMax<qreal>(0.0, width) : 0.0,
                        std::isfinite(height) ? qMax<qreal>(0.0, height) : 0.0);
  scene.rasterBounds = scene.bounds;
  if (scene.useMaxWidth && (!(width > 0.0) || !(height > 0.0))) {
    // An SVG with a zero/invalid viewBox dimension keeps its raw viewBox but
    // uses the browser's 150px replaced-element fallback height.
    scene.rasterBounds.setHeight(150.0);
  }

  if (!data.title.isEmpty()) {
    scene.titleText.cssClass = QStringLiteral("venn-title");
    scene.titleText.source = data.title;
    scene.titleText.lines = {data.title};
    scene.titleText.position = QPointF(width / 2.0, 32.0 * scene.scale);
    scene.titleText.fontSize = 32.0 * scene.scale;
    scene.titleText.fontFamily = scene.style.fontFamily;
    scene.titleText.fill = scene.style.vennTitleTextColor.isEmpty()
                                   ? scene.style.titleColor
                                   : scene.style.vennTitleTextColor;
  }

  QVector<VennSubset> renderSets = ensurePairwise(data.subsets);
  const QVector<VennSubset> visibleSets = filterZeroSets(renderSets);
  const layout::Result mainLayout = layout::compute(
      visibleSets, width, height - scene.titleHeight, 15.0);
  const layout::Result secondaryLayout = layout::compute(
      renderSets, width, height - scene.titleHeight, paddingValue);
  QMap<QString, layout::Area> secondaryByKey;
  for (const layout::Area& area : secondaryLayout.areas)
    secondaryByKey.insert(keyFor(area.data.sets), area);
  const auto styleMap = stylesByKey(data.styles);
  const bool themeDark = color::isDark(scene.style.background.isEmpty()
                                           ? QStringLiteral("#f4f4f4")
                                           : scene.style.background);
  const bool handDrawn = scene.style.look == QLatin1String("handDrawn");
  int circleIndex = 0;
  for (const layout::Area& source : mainLayout.areas) {
    VennAreaGeometry area;
    area.data = source.data;
    area.key = keyFor(source.data.sets);
    area.circle = source.data.sets.size() == 1;
    area.cssClass = QStringLiteral("venn-area ") +
                    (area.circle ? QStringLiteral("venn-circle")
                                 : QStringLiteral("venn-intersection"));
    if (area.circle)
      area.cssClass += QStringLiteral(" venn-set-%1").arg(circleIndex % 8);
    area.circles = source.circles;
    area.rawPath = layout::intersectionPath(source.circles, -1).trimmed();
    area.path = areaPainterPath(source);
    area.textCenter = QPointF(std::floor(source.text.x()),
                              std::floor(source.text.y()));
    const QMap<QString, QString> custom = styleMap.value(area.key);

    if (area.circle) {
      QString baseColor;
      const QString customFill = custom.value(QStringLiteral("fill"));
      if (!customFill.isEmpty())
        baseColor = customFill;
      else if (!scene.style.colors.isEmpty())
        baseColor = scene.style.colors.at(circleIndex % scene.style.colors.size());
      if (baseColor.isEmpty())
        baseColor = scene.style.primaryColor;
      area.fill = baseColor;
      area.stroke = custom.value(QStringLiteral("stroke"));
      if (area.stroke.isEmpty()) area.stroke = baseColor;
      area.fillOpacity = opacity(custom.value(QStringLiteral("fill-opacity")), 0.1);
      area.strokeOpacity = 0.95;
      area.strokeWidth = strokeWidth(
          custom.value(QStringLiteral("stroke-width")), 5.0 * scene.scale);
      area.label.fill = custom.value(QStringLiteral("color"));
      if (area.label.fill.isEmpty())
        area.label.fill = themeDark ? color::lighten(baseColor, 30.0)
                                    : color::darken(baseColor, 30.0);
      area.rough = handDrawn;
      if (area.rough) {
        const layout::Area secondary = secondaryByKey.value(area.key);
        if (!secondary.circles.isEmpty()) {
          const layout::Circle& circle = secondary.circles.front();
          rough::Options options;
          options.roughness = 0.7;
          options.seed = quint32(editor::jsNumberValue(config.handDrawnSeed));
          options.fill = color::transparentize(baseColor, 0.7);
          options.fillStyle = QStringLiteral("hachure");
          options.fillWeight = 2.0;
          options.hachureGap = 8.0;
          options.hachureAngle = -41.0 + circleIndex * 60.0;
          options.stroke = area.stroke;
          options.strokeWidth = area.strokeWidth;
          area.roughDrawable = rough::ellipse(circle.x, circle.y,
                                              circle.radius * 2.0,
                                              circle.radius * 2.0, options);
        }
      }
      ++circleIndex;
    } else {
      const QString customFill = custom.value(QStringLiteral("fill"));
      area.fill = customFill.isEmpty() ? QStringLiteral("transparent")
                                       : customFill;
      area.stroke = QStringLiteral("none");
      area.fillOpacity = customFill.isEmpty() ? 0.0 : 1.0;
      area.strokeOpacity = 1.0;
      area.strokeWidth = 1.0;
      area.label.fill = custom.value(QStringLiteral("color"));
      if (area.label.fill.isEmpty())
        area.label.fill = scene.style.vennSetTextColor.isEmpty()
                              ? (scene.style.primaryTextColor.isEmpty()
                                     ? scene.style.textColor
                                     : scene.style.primaryTextColor)
                              : scene.style.vennSetTextColor;
      area.rough = handDrawn && !customFill.isEmpty();
      if (area.rough) {
        rough::Options options;
        options.roughness = 0.7;
        options.seed = quint32(editor::jsNumberValue(config.handDrawnSeed));
        options.fill = color::transparentize(customFill, 0.3);
        options.fillStyle = QStringLiteral("cross-hatch");
        options.fillWeight = 2.0;
        options.hachureGap = 6.0;
        options.hachureAngle = 60.0;
        options.stroke = QStringLiteral("none");
        area.roughDrawable = rough::path(area.path, options, true);
      }
    }

    area.label.cssClass = QStringLiteral("label");
    area.label.source = source.data.hasLabel
                            ? source.data.label
                            : area.circle ? source.data.sets.front() : QString();
    area.label.position = area.textCenter;
    area.label.fontSize = 48.0 * scene.scale;
    area.label.fontFamily = scene.style.fontFamily;
    if (!area.label.source.isEmpty()) {
      const qreal radius = area.circles.isEmpty() ? 50.0
                                                   : area.circles.front().radius;
      area.label.lines = wrapLabel(area.label.source, radius, scene.style,
                                   area.label.fontSize);
      area.label.firstDyEm =
          0.35 - qreal(area.label.lines.size() - 1) * 1.1 / 2.0;
    }
    scene.areas.append(std::move(area));
  }

  QMap<QString, QVector<VennTextNode>> nodesByArea;
  QStringList nodeAreaOrder;
  for (const VennTextNode& node : data.textNodes) {
    const QString key = keyFor(node.sets);
    if (!nodesByArea.contains(key)) nodeAreaOrder.append(key);
    nodesByArea[key].append(node);
  }
  for (const QString& key : std::as_const(nodeAreaOrder)) {
    if (!secondaryByKey.contains(key)) continue;
    const QVector<VennTextNode>& areaNodes = nodesByArea.value(key);
    const layout::Area& area = secondaryByKey.value(key);
    const qreal minRadius = area.circles.isEmpty()
                                ? std::numeric_limits<qreal>::infinity()
                                : std::min_element(
                                      area.circles.cbegin(), area.circles.cend(),
                                      [](const auto& left, const auto& right) {
                                        return left.radius < right.radius;
                                      })->radius;
    qreal innerRadius = std::numeric_limits<qreal>::infinity();
    for (const layout::Circle& circle : area.circles)
      innerRadius = std::min(
          innerRadius,
          circle.radius - std::hypot(area.text.x() - circle.x,
                                     area.text.y() - circle.y));
    innerRadius = std::isfinite(innerRadius) ? qMax<qreal>(0.0, innerRadius) : 0.0;
    if (innerRadius == 0.0 && std::isfinite(minRadius)) innerRadius = minRadius * 0.6;
    if (scene.useDebugLayout)
      scene.debugCircles.append(
          {area.text, innerRadius, 1.5 * scene.scale});
    const qreal innerWidth = qMax<qreal>(80.0 * scene.scale, innerRadius * 1.9);
    const qreal innerHeight = qMax<qreal>(60.0 * scene.scale, innerRadius * 1.9);
    const bool hasLabel = area.data.hasLabel && !area.data.label.isEmpty();
    const qreal labelOffsetBase =
        hasLabel ? qMin<qreal>(32.0 * scene.scale, innerRadius * 0.25) : 0.0;
    const qreal labelOffset =
        labelOffsetBase + (areaNodes.size() <= 2 ? 30.0 * scene.scale : 0.0);
    const QPointF start(area.text.x() - innerWidth / 2.0,
                        area.text.y() - innerHeight / 2.0 + labelOffset);
    const int columns = qMax(1, int(std::ceil(std::sqrt(areaNodes.size()))));
    const int rows = qMax(1, int(std::ceil(qreal(areaNodes.size()) / columns)));
    const qreal cellWidth = innerWidth / columns;
    const qreal cellHeight = innerHeight / rows;
    for (int index = 0; index < areaNodes.size(); ++index) {
      const int column = index % columns;
      const int row = index / columns;
      if (scene.useDebugLayout)
        scene.debugCells.append(
            {QRectF(start.x() + cellWidth * column,
                    start.y() + cellHeight * row, cellWidth, cellHeight),
             scene.scale});
      const qreal boxWidth = cellWidth * 0.9;
      const qreal boxHeight = cellHeight * 0.9;
      const QPointF center(start.x() + cellWidth * (column + 0.5),
                           start.y() + cellHeight * (row + 0.5));
      const VennTextNode& source = areaNodes.at(index);
      VennTextNodeGeometry node;
      node.areaKey = key;
      node.id = source.id;
      node.source = source.hasLabel ? source.label : source.id;
      node.box = QRectF(center.x() - boxWidth / 2.0,
                       center.y() - boxHeight / 2.0, boxWidth, boxHeight);
      node.color = styleMap.value(source.id).value(QStringLiteral("color"));
      if (node.color.isEmpty()) node.color = scene.style.vennSetTextColor;
      node.fontSize = 40.0 * scene.scale;
      node.fontFamily = scene.style.fontFamily;
      scene.textNodes.append(std::move(node));
    }
  }
  return scene;
}

}  // namespace muffin::mermaid::venn
