#include "mermaid/sankey/SankeyScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/sankey/SankeyScenePainter.h"
#include "mermaid/text/ChromiumTextMetrics.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace muffin::mermaid::sankey {
namespace {

struct WorkLink;
struct WorkNode {
  QString id;
  int index = 0;
  int depth = 0;
  int height = 0;
  int layer = 0;
  double value = 0.0;
  double x0 = 0.0, x1 = 0.0, y0 = 0.0, y1 = 0.0;
  QVector<int> sourceLinks;
  QVector<int> targetLinks;
};
struct WorkLink {
  int index = 0;
  int source = 0;
  int target = 0;
  double value = 0.0;
  double width = 0.0;
  double y0 = 0.0, y1 = 0.0;
};

QString jsString(const QJsonValue &value, const QString &fallback = {}) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  if (value.isString())
    return value.toString();
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isDouble())
    return QString::number(value.toDouble(), 'g', 15);
  return fallback;
}

bool jsTruthy(const QJsonValue &value, bool fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  if (value.isBool())
    return value.toBool();
  if (value.isDouble())
    return value.toDouble() != 0.0 && !std::isnan(value.toDouble());
  if (value.isString())
    return !value.toString().isEmpty();
  return true;
}

double jsNumber(const QJsonValue &value, double fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  return editor::jsNumberValue(value);
}

double d3Sum(const QVector<int> &indexes, const QVector<WorkLink> &links) {
  double result = 0.0;
  for (int index : indexes) {
    const double value = links.at(index).value;
    if (value != 0.0 && !std::isnan(value))
      result += value;
  }
  return result;
}

QString collapsedSvgText(QString value) {
  value.replace(
      QRegularExpression(QStringLiteral(R"([\x{0009}-\x{000D}\x{0020}]+)")),
      QStringLiteral(" "));
  return value.trimmed();
}

QString svgNumber(double value) { return QString::number(value, 'g', 17); }

QRectF labelBounds(const SankeySceneStyle &style, const QString &text,
                   const QPointF &position, const QString &anchor,
                   double dyEm) {
  const auto font = editor::makeUnhintedCssPixelFont(style.fontFamily, 14.0);
  const QFontMetricsF metrics(font.font);
  const QString visible = collapsedSvgText(text);
  const double qtWidth = metrics.horizontalAdvance(visible) * font.scale;
  const double shaped =
      textmetrics::harfBuzzAdvance(visible, style.fontFamily, 14.0)
          .value_or(qtWidth);
  const double width = std::ceil(shaped * 64.0) / 64.0;
  double left = position.x();
  if (anchor == QLatin1String("end"))
    left -= width;
  else if (anchor == QLatin1String("middle"))
    left -= width / 2.0;
  // Bundled Noto Sans follows Blink's hhea metrics at 14 CSS pixels: 19px high,
  // with the alphabetic top 15px above the baseline.
  const double top = position.y() + dyEm * 14.0 - 15.0;
  return QRectF(left, top, width, 19.0);
}

QJsonObject rectJson(const QRectF &r) {
  return {{QStringLiteral("x"), r.x()},
          {QStringLiteral("y"), r.y()},
          {QStringLiteral("width"), r.width()},
          {QStringLiteral("height"), r.height()}};
}

} // namespace

void SankeyScene::paint(QPainter &painter,
                        const MermaidPaintOptions &options) const {
  paintSankeyScene(*this, painter, options);
}

QJsonObject SankeyScene::toJsonObject() const {
  QJsonArray nodeValues;
  for (const auto &node : nodes)
    nodeValues.append(
        QJsonObject{{QStringLiteral("id"), node.id},
                    {QStringLiteral("layer"), node.layer},
                    {QStringLiteral("value"), node.value},
                    {QStringLiteral("rect"),
                     rectJson(QRectF(node.x0, node.y0, node.x1 - node.x0,
                                     node.y1 - node.y0))},
                    {QStringLiteral("fill"), node.color}});
  QJsonArray linkValues;
  for (const auto &link : links)
    linkValues.append(QJsonObject{{QStringLiteral("source"), link.source},
                                  {QStringLiteral("target"), link.target},
                                  {QStringLiteral("width"), link.width},
                                  {QStringLiteral("path"), link.pathData},
                                  {QStringLiteral("stroke"), link.stroke}});
  QJsonArray labelValues;
  for (const auto &label : labels)
    labelValues.append(
        QJsonObject{{QStringLiteral("node"), label.node},
                    {QStringLiteral("text"), label.text},
                    {QStringLiteral("x"), label.position.x()},
                    {QStringLiteral("y"), label.position.y()},
                    {QStringLiteral("anchor"), label.anchor},
                    {QStringLiteral("background"), label.backgroundLayer}});
  return {{QStringLiteral("type"), QStringLiteral("sankey")},
          {QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("nodes"), nodeValues},
          {QStringLiteral("links"), linkValues},
          {QStringLiteral("labels"), labelValues}};
}

SankeyScene buildSankeyScene(const SankeyData &data, SankeyConfig config,
                             SankeySceneStyle style) {
  // Direct C++ port of d3-sankey 0.12.3. Keep operation order stable: tiny
  // changes here alter collision ordering and therefore observable SVG paths.
  SankeyScene scene;
  scene.style = std::move(style);
  const double width = jsNumber(config.width, 600.0);
  const double height = jsNumber(config.height, 400.0);
  const double nodeWidth = jsNumber(config.nodeWidth, 10.0);
  const bool showValues = jsTruthy(config.showValues, true);
  // This addition intentionally precedes d3's numeric coercion.
  QJsonValue effectivePadding;
  if (config.nodePadding.isString())
    effectivePadding =
        config.nodePadding.toString() + QString::number(showValues ? 15 : 0);
  else
    effectivePadding =
        jsNumber(config.nodePadding, 12.0) + (showValues ? 15.0 : 0.0);
  const double requestedPadding = jsNumber(effectivePadding, 27.0);
  scene.configuredWidth = width;
  scene.configuredHeight = height;
  scene.useMaxWidth = jsTruthy(config.useMaxWidth, true);
  const QString alignment =
      jsString(config.nodeAlignment, QStringLiteral("justify"));
  const QString labelStyle =
      jsString(config.labelStyle, QStringLiteral("legacy"));
  scene.outlinedLabels = labelStyle == QLatin1String("outlined");

  QVector<WorkNode> nodes;
  QHash<QString, int> nodeIndexes;
  for (qsizetype i = 0; i < data.nodes.size(); ++i) {
    nodes.append({data.nodes.at(i).id, int(i)});
    nodeIndexes.insert(data.nodes.at(i).id, int(i));
  }
  QVector<WorkLink> links;
  for (qsizetype i = 0; i < data.links.size(); ++i) {
    const auto &input = data.links.at(i);
    WorkLink link{int(i), nodeIndexes.value(input.source),
                  nodeIndexes.value(input.target), input.value};
    links.append(link);
    nodes[link.source].sourceLinks.append(int(i));
    nodes[link.target].targetLinks.append(int(i));
  }

  const int n = nodes.size();
  QVector<int> current(n), next;
  current.resize(n);
  std::iota(current.begin(), current.end(), 0);
  int depth = 0;
  while (!current.isEmpty()) {
    QHash<int, bool> seen;
    next.clear();
    for (int nodeIndex : current) {
      nodes[nodeIndex].depth = depth;
      for (int linkIndex : nodes[nodeIndex].sourceLinks) {
        const int target = links[linkIndex].target;
        if (!seen.contains(target)) {
          seen.insert(target, true);
          next.append(target);
        }
      }
    }
    if (++depth > n)
      throw SankeyParseError(SankeyErrorKind::Runtime, 1, 1, QString(),
                             QStringLiteral("circular link"));
    current = next;
  }
  current.resize(n);
  std::iota(current.begin(), current.end(), 0);
  int nodeHeight = 0;
  while (!current.isEmpty()) {
    QHash<int, bool> seen;
    next.clear();
    for (int nodeIndex : current) {
      nodes[nodeIndex].height = nodeHeight;
      for (int linkIndex : nodes[nodeIndex].targetLinks) {
        const int source = links[linkIndex].source;
        if (!seen.contains(source)) {
          seen.insert(source, true);
          next.append(source);
        }
      }
    }
    if (++nodeHeight > n)
      throw SankeyParseError(SankeyErrorKind::Runtime, 1, 1, QString(),
                             QStringLiteral("circular link"));
    current = next;
  }
  for (auto &node : nodes)
    node.value = std::max(d3Sum(node.sourceLinks, links),
                          d3Sum(node.targetLinks, links));

  int maxDepth = 0;
  for (const auto &node : nodes)
    maxDepth = std::max(maxDepth, node.depth);
  const int layerCount = maxDepth + 1;
  const double kx = (width - nodeWidth) / (layerCount - 1);
  QVector<QVector<int>> columns(layerCount);
  for (auto &node : nodes) {
    double aligned = node.depth;
    if (alignment == QLatin1String("right"))
      aligned = layerCount - 1 - node.height;
    else if (alignment == QLatin1String("justify"))
      aligned = node.sourceLinks.isEmpty() ? layerCount - 1 : node.depth;
    else if (alignment == QLatin1String("center")) {
      if (!node.targetLinks.isEmpty())
        aligned = node.depth;
      else if (!node.sourceLinks.isEmpty()) {
        aligned = std::numeric_limits<double>::infinity();
        for (int linkIndex : node.sourceLinks)
          aligned =
              std::min(aligned, double(nodes[links[linkIndex].target].depth));
        aligned -= 1.0;
      } else
        aligned = 0.0;
    }
    const int layer =
        std::max(0, std::min(layerCount - 1, int(std::floor(aligned))));
    node.layer = layer;
    node.x0 = layer * kx;
    node.x1 = node.x0 + nodeWidth;
    columns[layer].append(node.index);
  }
  int maxColumn = 0;
  for (const auto &column : columns)
    maxColumn = std::max(maxColumn, int(column.size()));
  double padding = std::min(requestedPadding, height / (maxColumn - 1));
  double ky = std::numeric_limits<double>::infinity();
  for (const auto &column : columns) {
    double sum = 0.0;
    for (int nodeIndex : column) {
      const double value = nodes[nodeIndex].value;
      if (value != 0.0 && !std::isnan(value))
        sum += value;
    }
    const double candidate = (height - (column.size() - 1) * padding) / sum;
    if (!std::isnan(candidate))
      ky = std::min(ky, candidate);
  }

  auto ascendingBreadth = [&](int a, int b) {
    return nodes[a].y0 < nodes[b].y0;
  };
  auto sortNodeLinks = [&](int nodeIndex) {
    auto &node = nodes[nodeIndex];
    for (int incoming : node.targetLinks) {
      auto &source = nodes[links[incoming].source].sourceLinks;
      std::stable_sort(source.begin(), source.end(), [&](int a, int b) {
        const double d = nodes[links[a].target].y0 - nodes[links[b].target].y0;
        return d != 0.0 ? d < 0.0 : links[a].index < links[b].index;
      });
    }
    for (int outgoing : node.sourceLinks) {
      auto &target = nodes[links[outgoing].target].targetLinks;
      std::stable_sort(target.begin(), target.end(), [&](int a, int b) {
        const double d = nodes[links[a].source].y0 - nodes[links[b].source].y0;
        return d != 0.0 ? d < 0.0 : links[a].index < links[b].index;
      });
    }
  };
  auto reorderLinks = [&] {
    for (auto &node : nodes) {
      std::stable_sort(
          node.sourceLinks.begin(), node.sourceLinks.end(), [&](int a, int b) {
            const double d =
                nodes[links[a].target].y0 - nodes[links[b].target].y0;
            return d != 0.0 ? d < 0.0 : links[a].index < links[b].index;
          });
      std::stable_sort(
          node.targetLinks.begin(), node.targetLinks.end(), [&](int a, int b) {
            const double d =
                nodes[links[a].source].y0 - nodes[links[b].source].y0;
            return d != 0.0 ? d < 0.0 : links[a].index < links[b].index;
          });
    }
  };
  for (const auto &column : columns) {
    double y = 0.0;
    for (int nodeIndex : column) {
      auto &node = nodes[nodeIndex];
      node.y0 = y;
      node.y1 = y + node.value * ky;
      y = node.y1 + padding;
      for (int linkIndex : node.sourceLinks)
        links[linkIndex].width = links[linkIndex].value * ky;
    }
    y = (height - y + padding) / (column.size() + 1);
    for (qsizetype i = 0; i < column.size(); ++i) {
      nodes[column[i]].y0 += y * (i + 1);
      nodes[column[i]].y1 += y * (i + 1);
    }
  }
  reorderLinks();

  auto targetTop = [&](const WorkNode &source, const WorkNode &target) {
    double y = source.y0 - (source.sourceLinks.size() - 1) * padding / 2.0;
    for (int index : source.sourceLinks) {
      if (links[index].target == target.index)
        break;
      y += links[index].width + padding;
    }
    for (int index : target.targetLinks) {
      if (links[index].source == source.index)
        break;
      y -= links[index].width;
    }
    return y;
  };
  auto sourceTop = [&](const WorkNode &source, const WorkNode &target) {
    double y = target.y0 - (target.targetLinks.size() - 1) * padding / 2.0;
    for (int index : target.targetLinks) {
      if (links[index].source == source.index)
        break;
      y += links[index].width + padding;
    }
    for (int index : source.sourceLinks) {
      if (links[index].target == target.index)
        break;
      y -= links[index].width;
    }
    return y;
  };
  auto resolve = [&](QVector<int> &column, double alpha) {
    const int middle = column.size() >> 1;
    const int subject = column[middle];
    auto bottomToTop = [&](double y, int i) {
      for (; i >= 0; --i) {
        auto &node = nodes[column[i]];
        double d = (node.y1 - y) * alpha;
        if (d > 1e-6) {
          node.y0 -= d;
          node.y1 -= d;
        }
        y = node.y0 - padding;
      }
    };
    auto topToBottom = [&](double y, int i) {
      for (; i < column.size(); ++i) {
        auto &node = nodes[column[i]];
        double d = (y - node.y0) * alpha;
        if (d > 1e-6) {
          node.y0 += d;
          node.y1 += d;
        }
        y = node.y1 + padding;
      }
    };
    bottomToTop(nodes[subject].y0 - padding, middle - 1);
    topToBottom(nodes[subject].y1 + padding, middle + 1);
    bottomToTop(height, column.size() - 1);
    topToBottom(0.0, 0);
  };
  for (int iteration = 0; iteration < 6; ++iteration) {
    const double alpha = std::pow(0.99, iteration);
    const double beta = std::max(1.0 - alpha, (iteration + 1) / 6.0);
    for (int i = columns.size() - 2; i >= 0; --i) {
      auto &column = columns[i];
      for (int nodeIndex : column) {
        auto &source = nodes[nodeIndex];
        double y = 0, w = 0;
        for (int li : source.sourceLinks) {
          auto &link = links[li];
          auto &target = nodes[link.target];
          double v = link.value * (target.layer - source.layer);
          y += sourceTop(source, target) * v;
          w += v;
        }
        if (!(w > 0))
          continue;
        double d = (y / w - source.y0) * alpha;
        source.y0 += d;
        source.y1 += d;
        sortNodeLinks(nodeIndex);
      }
      std::stable_sort(column.begin(), column.end(), ascendingBreadth);
      resolve(column, beta);
    }
    for (int i = 1; i < columns.size(); ++i) {
      auto &column = columns[i];
      for (int nodeIndex : column) {
        auto &target = nodes[nodeIndex];
        double y = 0, w = 0;
        for (int li : target.targetLinks) {
          auto &link = links[li];
          auto &source = nodes[link.source];
          double v = link.value * (target.layer - source.layer);
          y += targetTop(source, target) * v;
          w += v;
        }
        if (!(w > 0))
          continue;
        double d = (y / w - target.y0) * alpha;
        target.y0 += d;
        target.y1 += d;
        sortNodeLinks(nodeIndex);
      }
      std::stable_sort(column.begin(), column.end(), ascendingBreadth);
      resolve(column, beta);
    }
  }
  for (const auto &node : nodes) {
    double sy = node.y0, ty = node.y0;
    for (int li : node.sourceLinks) {
      links[li].y0 = sy + links[li].width / 2;
      sy += links[li].width;
    }
    for (int li : node.targetLinks) {
      links[li].y1 = ty + links[li].width / 2;
      ty += links[li].width;
    }
  }

  static const QStringList palette = {
      QStringLiteral("#4e79a7"), QStringLiteral("#f28e2c"),
      QStringLiteral("#e15759"), QStringLiteral("#76b7b2"),
      QStringLiteral("#59a14f"), QStringLiteral("#edc949"),
      QStringLiteral("#af7aa1"), QStringLiteral("#ff9da7"),
      QStringLiteral("#9c755f"), QStringLiteral("#bab0ab")};
  QHash<QString, QString> colors;
  int colorIndex = 0;
  for (const auto &node : nodes) {
    QString color = config.nodeColors.value(node.id).toString();
    if (color.isEmpty())
      color = palette.at(colorIndex++ % palette.size());
    colors.insert(node.id, color);
    scene.nodes.append({node.id, node.index, node.depth, node.height,
                        node.layer, node.value, node.x0, node.x1, node.y0,
                        node.y1, color});
  }

  int centralLayer = 0;
  double maxValue = 0;
  for (const auto &node : nodes)
    if (node.value > maxValue) {
      maxValue = node.value;
      centralLayer = node.layer;
    }
  const QString prefix = jsString(config.prefix),
                suffix = jsString(config.suffix);
  QVector<SankeyLabelGeometry> backgroundLabels, foregroundLabels;
  for (const auto &node : nodes) {
    QString text = node.id;
    if (showValues)
      text += QLatin1Char('\n') + prefix +
              QString::number(std::round(node.value * 100) / 100, 'g', 15) +
              suffix;
    QString anchor;
    double x;
    if (scene.outlinedLabels) {
      if (node.layer < centralLayer) {
        x = node.x0 - 6;
        anchor = QStringLiteral("end");
      } else {
        x = node.x1 + 6;
        anchor = QStringLiteral("start");
      }
    } else if (node.x0 < width / 2) {
      x = node.x1 + 6;
      anchor = QStringLiteral("start");
    } else {
      x = node.x0 - 6;
      anchor = QStringLiteral("end");
    }
    QPointF pos(x, (node.y1 + node.y0) / 2);
    QRectF box =
        labelBounds(scene.style, text, pos, anchor, showValues ? 0 : .35);
    if (scene.outlinedLabels)
      backgroundLabels.append({node.index, text, pos, anchor,
                               showValues ? 0 : .35, box, true, true, QString(),
                               scene.style.mainBkg.isEmpty()
                                   ? scene.style.background
                                   : scene.style.mainBkg,
                               4});
    foregroundLabels.append({node.index, text, pos, anchor,
                             showValues ? 0 : .35, box, scene.outlinedLabels,
                             false, scene.style.textColor, QString(), 0});
  }
  scene.labels = std::move(backgroundLabels);
  scene.labels += foregroundLabels;

  const QString linkColor =
      jsString(config.linkColor, QStringLiteral("gradient"));
  for (const auto &link : links) {
    const auto &source = nodes[link.source];
    const auto &target = nodes[link.target];
    const double middle = (source.x1 + target.x0) / 2;
    QPainterPath path(QPointF(source.x1, link.y0));
    path.cubicTo(middle, link.y0, middle, link.y1, target.x0, link.y1);
    QString stroke;
    if (linkColor == QLatin1String("gradient"))
      stroke = QStringLiteral("gradient");
    else if (linkColor == QLatin1String("source"))
      stroke = colors.value(source.id);
    else if (linkColor == QLatin1String("target"))
      stroke = colors.value(target.id);
    else
      stroke = linkColor;
    scene.links.append(
        {link.index, link.source, link.target, link.value,
         std::max(1.0, link.width), link.y0, link.y1, path,
         QStringLiteral("M%1,%2C%3,%2,%3,%4,%5,%4")
             .arg(svgNumber(source.x1), svgNumber(link.y0), svgNumber(middle),
                  svgNumber(link.y1), svgNumber(target.x0)),
         stroke, colors.value(source.id), colors.value(target.id)});
  }

  QRectF bounds;
  bool hasBounds = false;
  auto include = [&](const QRectF &rect) {
    if (!rect.isValid() && rect.width() == 0 && rect.height() == 0)
      return;
    if (!hasBounds) {
      bounds = rect;
      hasBounds = true;
    } else
      bounds = bounds.united(rect);
  };
  for (const auto &node : scene.nodes) {
    const QRectF rect(node.x0, node.y0, node.x1 - node.x0, node.y1 - node.y0);
    if (rect.width() > 0.0 && rect.height() > 0.0)
      include(rect);
  }
  for (const auto &link : scene.links)
    include(link.path.boundingRect());
  for (const auto &label : scene.labels)
    include(label.bounds);
  scene.bounds = bounds;
  scene.rasterBounds =
      QRectF(bounds.topLeft(),
             QSizeF(qRound(bounds.width()), qRound(bounds.height())));
  scene.viewBoxAttribute =
      QStringLiteral("%1 %2 %3 %4")
          .arg(svgNumber(bounds.x()), svgNumber(bounds.y()),
               svgNumber(bounds.width()), svgNumber(bounds.height()));
  return scene;
}

} // namespace muffin::mermaid::sankey
