#include "mermaid/treemap/TreemapScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/text/ChromiumTextMetrics.h"
#include "mermaid/treemap/TreemapScenePainter.h"

#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace muffin::mermaid::treemap {
namespace {

struct WorkNode {
  int dataIndex = -1;
  int parent = -1;
  int depth = 0;
  int height = 0;
  QVector<int> children;
  double value = 0.0;
  double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};

double jsNumber(const QJsonValue &value, double fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  return editor::jsNumberValue(value);
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

QString jsString(const QJsonValue &value, const QString &fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  if (value.isString())
    return value.toString();
  if (value.isBool())
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  if (value.isDouble())
    return editor::jsNumberToString(value.toDouble());
  return fallback;
}

double jsRound(double value) { return std::floor(value + 0.5); }

QString visibleSvgText(QString text) {
  text.replace(QRegularExpression(QStringLiteral(R"([\t\n\r\f ]+)")),
               QStringLiteral(" "));
  while (text.startsWith(QLatin1Char(' ')))
    text.remove(0, 1);
  while (text.endsWith(QLatin1Char(' ')))
    text.chop(1);
  return text;
}

editor::CssPixelFont textFont(const TreemapSceneStyle &style, qreal size,
                              bool bold, bool italic) {
  auto font = editor::makeUnhintedCssPixelFont(
      editor::firstFontFamily(style.fontFamily), size);
  font.font.setWeight(bold ? QFont::Bold : QFont::Normal);
  font.font.setItalic(italic);
  return font;
}

double advance(const TreemapSceneStyle &style, const QString &text,
               qreal size, bool bold = false, bool italic = false) {
  const QString visible = visibleSvgText(text);
  const auto font = textFont(style, size, bold, italic);
  const double qt = QFontMetricsF(font.font).horizontalAdvance(visible) * font.scale;
  const double shaped =
      textmetrics::harfBuzzAdvance(visible, style.fontFamily, size).value_or(qt);
  // Blink's synthetic-bold shaping expands each glyph advance by 1/60em when
  // only the bundled regular face is available. Qt emboldens the outline but
  // keeps the regular advances, so account for that observable SVG width.
  if (bold)
    return shaped + visible.size() * size / 60.0;
  if (!bold && !italic)
    return shaped;
  return qt;
}

QRectF textBounds(const TreemapSceneStyle &style, const QString &text,
                  const QPointF &position, qreal size, const QString &anchor,
                  TreemapTextBaseline baseline, bool bold = false,
                  bool italic = false) {
  if (text.isEmpty() || !(size > 0.0))
    return {};
  const auto font = textFont(style, size, bold, italic);
  const QFontMetricsF metrics(font.font);
  const QString visible = visibleSvgText(text);
  const double shapedAdvance = advance(style, visible, size, bold, italic);
  QRectF ink = metrics.boundingRect(visible);
  ink = QRectF(ink.x() * font.scale, ink.y() * font.scale,
               ink.width() * font.scale, ink.height() * font.scale);
  double originX = position.x();
  if (anchor == QLatin1String("middle"))
    originX -= shapedAdvance / 2.0;
  else if (anchor == QLatin1String("end"))
    originX -= shapedAdvance;
  const auto vertical = flowchart::flowLabelFontBoundingMetrics(
      style.fontFamily, size, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  const double baselineY = position.y() +
      (baseline == TreemapTextBaseline::Middle
           ? vertical.xHeight / 2.0
           : vertical.ascent * 0.8);
  return QRectF(originX + ink.x(), baselineY - vertical.ascent,
                ink.width(), vertical.height());
}

QString styleValue(const QStringList &styles, const QString &key) {
  QString result;
  for (const QString &declaration : styles) {
    const int colon = declaration.indexOf(QLatin1Char(':'));
    if (colon < 0)
      continue;
    if (declaration.left(colon).trimmed().compare(key, Qt::CaseInsensitive) == 0)
      result = declaration.mid(colon + 1).trimmed();
  }
  return result;
}

class Ordinal {
public:
  explicit Ordinal(QStringList range) : range_(std::move(range)) {}
  QString get(const QString &key) {
    auto it = index_.constFind(key);
    if (it == index_.cend()) {
      const int next = index_.size();
      index_.insert(key, next);
      return range_.isEmpty() ? QString() : range_.at(next % range_.size());
    }
    return range_.isEmpty() ? QString() : range_.at(*it % range_.size());
  }
private:
  QStringList range_;
  QHash<QString, int> index_;
};

void dice(QVector<WorkNode> &nodes, const QVector<int> &children,
          double parentValue, double x0, double y0, double x1, double y1) {
  const double k = parentValue ? (x1 - x0) / parentValue : 0.0;
  for (int child : children) {
    auto &node = nodes[child];
    node.y0 = y0; node.y1 = y1; node.x0 = x0;
    x0 += node.value * k;
    node.x1 = x0;
  }
}

void slice(QVector<WorkNode> &nodes, const QVector<int> &children,
           double parentValue, double x0, double y0, double x1, double y1) {
  const double k = parentValue ? (y1 - y0) / parentValue : 0.0;
  for (int child : children) {
    auto &node = nodes[child];
    node.x0 = x0; node.x1 = x1; node.y0 = y0;
    y0 += node.value * k;
    node.y1 = y0;
  }
}

void squarify(QVector<WorkNode> &nodes, int parentIndex, double x0, double y0,
              double x1, double y1) {
  const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
  const auto children = nodes.at(parentIndex).children;
  const int n = children.size();
  int i0 = 0, i1 = 0;
  double value = nodes.at(parentIndex).value;
  while (i0 < n) {
    const double dx = x1 - x0, dy = y1 - y0;
    double sumValue;
    do {
      sumValue = nodes.at(children.at(i1++)).value;
    } while (!sumValue && i1 < n);
    double minValue = sumValue, maxValue = sumValue;
    const double alpha = std::max(dy / dx, dx / dy) / (value * phi);
    double beta = sumValue * sumValue * alpha;
    double minRatio = std::max(maxValue / beta, beta / minValue);
    for (; i1 < n; ++i1) {
      const double nodeValue = nodes.at(children.at(i1)).value;
      sumValue += nodeValue;
      if (nodeValue < minValue) minValue = nodeValue;
      if (nodeValue > maxValue) maxValue = nodeValue;
      beta = sumValue * sumValue * alpha;
      const double newRatio = std::max(maxValue / beta, beta / minValue);
      if (newRatio > minRatio) {
        sumValue -= nodeValue;
        break;
      }
      minRatio = newRatio;
    }
    const QVector<int> row = children.mid(i0, i1 - i0);
    if (dx < dy) {
      const double nextY = value ? y0 + dy * sumValue / value : y1;
      dice(nodes, row, sumValue, x0, y0, x1, nextY);
      y0 = nextY;
    } else {
      const double nextX = value ? x0 + dx * sumValue / value : x1;
      slice(nodes, row, sumValue, x0, y0, nextX, y1);
      x0 = nextX;
    }
    value -= sumValue;
    i0 = i1;
  }
}

QString commaNumber(double value) {
  if (!std::isfinite(value))
    return QString();
  const qint64 integer = static_cast<qint64>(std::trunc(value));
  QString number = QString::number(std::abs(integer));
  for (int i = number.size() - 3; i > 0; i -= 3)
    number.insert(i, QLatin1Char(','));
  if (integer < 0)
    number.prepend(QLatin1Char('-'));
  const double fraction = std::abs(value - std::trunc(value));
  if (fraction > 1e-12) {
    QString suffix = QString::number(fraction, 'f', 12).mid(1);
    while (suffix.endsWith(QLatin1Char('0'))) suffix.chop(1);
    number += suffix;
  }
  return number;
}

QString formatValue(double value, const QString &format) {
  if (!std::isfinite(value) || value == 0.0)
    return {};
  if (format == QLatin1String("$0,0"))
    return QLatin1Char('$') + commaNumber(value);
  if (format.startsWith(QLatin1Char('$')) && format.contains(QLatin1Char(','))) {
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral(R"(\.(\d+))")).match(format);
    if (match.hasMatch()) {
      const int precision = match.captured(1).toInt();
      if (precision == 2 && std::abs(value) >= 100.0)
        return QStringLiteral("$") + QString::number(value, 'e', 0)
                   .replace(QStringLiteral("e+0"), QStringLiteral("e+"));
    }
    return QLatin1Char('$') + commaNumber(value);
  }
  if (format.startsWith(QLatin1Char('$')))
    return QLatin1Char('$') + commaNumber(value);
  if (format == QLatin1String(".1%"))
    return QString::number(value * 100.0, 'f', 1) + QLatin1Char('%');
  if (format == QLatin1String(",") || format.isEmpty() ||
      format == QLatin1String("["))
    return commaNumber(value);
  return commaNumber(value);
}

QJsonObject rectJson(const QRectF &rect) {
  return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

} // namespace

void TreemapScene::paint(QPainter &painter,
                         const MermaidPaintOptions &options) const {
  paintTreemapScene(*this, painter, options);
}

QJsonObject TreemapScene::toJsonObject() const {
  QJsonArray sectionValues;
  for (const auto &section : sections)
    sectionValues.append(QJsonObject{{QStringLiteral("node"), section.node},
                                     {QStringLiteral("depth"), section.depth},
                                     {QStringLiteral("rect"), rectJson(section.rect)},
                                     {QStringLiteral("fill"), section.fill},
                                     {QStringLiteral("stroke"), section.stroke},
                                     {QStringLiteral("label"), section.label.text},
                                     {QStringLiteral("value"), section.value.text}});
  QJsonArray leafValues;
  for (const auto &leaf : leaves)
    leafValues.append(QJsonObject{{QStringLiteral("node"), leaf.node},
                                  {QStringLiteral("rect"), rectJson(leaf.rect)},
                                  {QStringLiteral("fill"), leaf.fill},
                                  {QStringLiteral("stroke"), leaf.stroke},
                                  {QStringLiteral("label"), leaf.label.text},
                                  {QStringLiteral("labelFontSize"), leaf.label.fontSize},
                                  {QStringLiteral("value"), leaf.value.text},
                                  {QStringLiteral("valueFontSize"), leaf.value.fontSize}});
  return {{QStringLiteral("type"), QStringLiteral("treemap")},
          {QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("contentBounds"), rectJson(contentBounds)},
          {QStringLiteral("sections"), sectionValues},
          {QStringLiteral("leaves"), leafValues}};
}

TreemapScene buildTreemapScene(const TreemapData &data, TreemapConfig config,
                               TreemapSceneStyle style) {
  TreemapScene scene;
  scene.style = std::move(style);
  const double widthValue = jsNumber(config.nodeWidth, 100.0);
  const double heightValue = jsNumber(config.nodeHeight, 40.0);
  const double width = widthValue ? widthValue * 10.0 : 960.0;
  const double height = heightValue ? heightValue * 10.0 : 500.0;
  const double innerPadding = jsNumber(config.padding, 10.0);
  const bool showValues = !(config.showValues.isBool() && !config.showValues.toBool());
  const QString valueFormat = jsString(config.valueFormat, QStringLiteral(","));
  const double titleHeight = data.title.isEmpty() ? 0.0 : 30.0;
  scene.configuredWidth = width;
  scene.configuredHeight = height + titleHeight;
  scene.useMaxWidth = jsTruthy(config.useMaxWidth, true);

  QVector<WorkNode> nodes;
  nodes.append({.dataIndex = -1, .parent = -1, .depth = 0});
  std::function<int(int, int, int)> add = [&](int dataIndex, int parent,
                                              int depth) {
    const int index = nodes.size();
    WorkNode work;
    work.dataIndex = dataIndex;
    work.parent = parent;
    work.depth = depth;
    nodes.append(work);
    for (int child : data.nodes.at(dataIndex).children) {
      const int childIndex = add(child, index, depth + 1);
      nodes[index].children.append(childIndex);
    }
    return index;
  };
  for (int root : data.roots) {
    const int rootIndex = add(root, 0, 1);
    nodes[0].children.append(rootIndex);
  }

  std::function<void(int)> sum = [&](int index) {
    double total = 0.0;
    int heightBelow = 0;
    for (int child : nodes.at(index).children) {
      sum(child);
      total += nodes.at(child).value;
      heightBelow = qMax(heightBelow, nodes.at(child).height + 1);
    }
    if (nodes.at(index).dataIndex >= 0) {
      const auto &datum = data.nodes.at(nodes.at(index).dataIndex);
      const double own = datum.hasValue && std::isfinite(datum.value)
                             ? datum.value : 0.0;
      total += own;
    }
    nodes[index].value = total;
    nodes[index].height = heightBelow;
  };
  sum(0);

  std::function<void(int)> sort = [&](int index) {
    auto &children = nodes[index].children;
    std::stable_sort(children.begin(), children.end(), [&](int a, int b) {
      return nodes.at(a).value > nodes.at(b).value;
    });
    for (int child : children) sort(child);
  };
  sort(0);

  nodes[0].x0 = nodes[0].y0 = 0.0;
  nodes[0].x1 = width; nodes[0].y1 = height;
  QVector<double> paddingStack{0.0};
  std::function<void(int)> position = [&](int index) {
    auto &node = nodes[index];
    const double inherited = paddingStack.value(node.depth, 0.0);
    double x0 = node.x0 + inherited, y0 = node.y0 + inherited;
    double x1 = node.x1 - inherited, y1 = node.y1 - inherited;
    if (x1 < x0) x0 = x1 = (x0 + x1) / 2.0;
    if (y1 < y0) y0 = y1 = (y0 + y1) / 2.0;
    node.x0 = x0; node.y0 = y0; node.x1 = x1; node.y1 = y1;
    if (!node.children.isEmpty()) {
      const double p = innerPadding / 2.0;
      if (paddingStack.size() <= node.depth + 1)
        paddingStack.resize(node.depth + 2);
      paddingStack[node.depth + 1] = p;
      x0 += 10.0 - p;
      y0 += 35.0 - p;
      x1 -= 10.0 - p;
      y1 -= 10.0 - p;
      if (x1 < x0) x0 = x1 = (x0 + x1) / 2.0;
      if (y1 < y0) y0 = y1 = (y0 + y1) / 2.0;
      squarify(nodes, index, x0, y0, x1, y1);
    }
    for (int child : node.children)
      position(child);
  };
  position(0);
  for (auto &node : nodes) {
    node.x0 = jsRound(node.x0); node.y0 = jsRound(node.y0);
    node.x1 = jsRound(node.x1); node.y1 = jsRound(node.y1);
  }

  QStringList colors{QStringLiteral("transparent")};
  QStringList peers{QStringLiteral("transparent")};
  QStringList labels;
  for (int i = 0; i < 12; ++i) {
    colors.append(scene.style.cScale[i]);
    peers.append(scene.style.cScalePeer[i]);
    labels.append(scene.style.cScaleLabel[i]);
  }
  Ordinal colorScale(colors), peerScale(peers), labelScale(labels);
  QVector<int> preorder;
  QVector<int> stack{0};
  while (!stack.isEmpty()) {
    const int index = stack.takeLast();
    preorder.append(index);
    const auto &children = nodes.at(index).children;
    for (int i = children.size() - 1; i >= 0; --i)
      stack.append(children.at(i));
  }

  for (int index : preorder) {
    const auto &work = nodes.at(index);
    if (work.children.isEmpty())
      continue;
    const TreemapNode *datum = work.dataIndex >= 0 ? &data.nodes.at(work.dataIndex) : nullptr;
    const QString name = datum ? datum->name : QString();
    TreemapSectionGeometry section;
    section.node = work.dataIndex;
    section.depth = work.depth;
    section.rect = QRectF(work.x0, work.y0 + titleHeight,
                          work.x1 - work.x0, work.y1 - work.y0);
    section.fill = colorScale.get(name);
    section.stroke = peerScale.get(name);
    section.classSelector = datum ? datum->classSelector : QString();
    const QStringList custom = datum ? datum->cssCompiledStyles : QStringList();
    if (!styleValue(custom, QStringLiteral("fill")).isEmpty())
      section.fill = styleValue(custom, QStringLiteral("fill"));
    if (!styleValue(custom, QStringLiteral("stroke")).isEmpty())
      section.stroke = styleValue(custom, QStringLiteral("stroke"));
    if (!styleValue(custom, QStringLiteral("stroke-width")).isEmpty())
      section.strokeWidth = editor::pixelValue(
          styleValue(custom, QStringLiteral("stroke-width")), 2.0);
    section.label.role = QStringLiteral("section-label");
    section.label.text = work.depth == 0 ? QString() : name;
    section.label.position = section.rect.topLeft() + QPointF(6.0, 12.5);
    section.label.fontSize = 12.0; section.label.bold = true;
    section.label.fill = work.depth == 0 ? scene.style.textColor
                                         : labelScale.get(name);
    if (!styleValue(custom, QStringLiteral("color")).isEmpty())
      section.label.fill = styleValue(custom, QStringLiteral("color"));
    section.label.clip = QRectF(section.rect.x(), section.rect.y(),
                                qMax(0.0, section.rect.width() - 12.0), 25.0);
    if (work.depth > 0) {
      const double available = qMax(15.0, showValues && work.value
                                              ? section.rect.width() - 56.0
                                              : section.rect.width() - 12.0);
      QString shown = name;
      if (advance(scene.style, shown, 12.0, true) > available) {
        while (!shown.isEmpty()) {
          shown.chop(1);
          const QString candidate = shown + QStringLiteral("...");
          if (advance(scene.style, candidate, 12.0, true) <= available) {
            shown = candidate;
            break;
          }
        }
        if (shown.isEmpty() && advance(scene.style, QStringLiteral("..."), 12.0, true) <= available)
          shown = QStringLiteral("...");
      }
      section.label.text = shown;
    }
    section.label.visible = work.depth > 0 && !section.label.text.isEmpty();
    section.label.bounds = textBounds(scene.style, section.label.text,
                                      section.label.position, 12.0,
                                      QStringLiteral("start"),
                                      TreemapTextBaseline::Middle, true);
    section.value.role = QStringLiteral("section-value");
    section.value.text = showValues ? formatValue(work.value, valueFormat) : QString();
    section.value.position = section.rect.topLeft() +
                             QPointF(section.rect.width() - 10.0, 12.5);
    section.value.fontSize = 10.0; section.value.italic = true;
    section.value.anchor = QStringLiteral("end");
    section.value.fill = section.label.fill;
    section.value.visible = work.depth > 0 && !section.value.text.isEmpty();
    section.value.bounds = textBounds(scene.style, section.value.text,
                                      section.value.position, 10.0,
                                      QStringLiteral("end"),
                                      TreemapTextBaseline::Middle, false, true);
    scene.sections.append(std::move(section));
  }

  QVector<int> leafIndexes;
  for (int index : preorder)
    if (nodes.at(index).children.isEmpty())
      leafIndexes.append(index);
  const bool complex = leafIndexes.size() > 20;
  const int baseLabel = complex ? 16 : 38;
  const int baseValue = complex ? 14 : 28;
  const int minLabel = complex ? 4 : 8;
  const int minValue = complex ? 4 : 6;
  const int labelPadding = complex ? 2 : 4;
  const int threshold = complex ? 8 : 10;
  const int spacing = complex ? 1 : 2;
  for (int index : leafIndexes) {
    const auto &work = nodes.at(index);
    const TreemapNode *datum = work.dataIndex >= 0 ? &data.nodes.at(work.dataIndex) : nullptr;
    const QString name = datum ? datum->name : QString();
    const QString parentName = work.parent >= 0
        ? (nodes.at(work.parent).dataIndex >= 0
               ? data.nodes.at(nodes.at(work.parent).dataIndex).name
               : QString())
        : name;
    TreemapLeafGeometry leaf;
    leaf.node = work.dataIndex;
    leaf.rect = QRectF(work.x0, work.y0 + titleHeight,
                       work.x1 - work.x0, work.y1 - work.y0);
    leaf.clip = QRectF(leaf.rect.x(), leaf.rect.y(),
                       qMax(0.0, leaf.rect.width() - 4.0),
                       qMax(0.0, leaf.rect.height() - 4.0));
    leaf.fill = leaf.stroke = colorScale.get(parentName);
    leaf.classSelector = datum ? datum->classSelector : QString();
    const QStringList custom = datum ? datum->cssCompiledStyles : QStringList();
    if (!styleValue(custom, QStringLiteral("fill")).isEmpty())
      leaf.fill = styleValue(custom, QStringLiteral("fill"));
    if (!styleValue(custom, QStringLiteral("stroke")).isEmpty())
      leaf.stroke = styleValue(custom, QStringLiteral("stroke"));
    if (!styleValue(custom, QStringLiteral("stroke-width")).isEmpty())
      leaf.strokeWidth = editor::pixelValue(
          styleValue(custom, QStringLiteral("stroke-width")), 3.0);
    const QString textFill = !styleValue(custom, QStringLiteral("color")).isEmpty()
                                 ? styleValue(custom, QStringLiteral("color"))
                                 : labelScale.get(name);
    leaf.label.role = QStringLiteral("leaf-label");
    leaf.label.text = name;
    leaf.label.position = leaf.rect.center();
    leaf.label.anchor = QStringLiteral("middle");
    leaf.label.fill = textFill;
    leaf.label.fontSize = baseLabel;
    const double availableWidth = leaf.rect.width() - 2.0 * labelPadding;
    const double availableHeight = leaf.rect.height() - 2.0 * labelPadding;
    if (availableWidth < threshold || availableHeight < threshold) {
      leaf.label.visible = false;
    } else {
      int current = baseLabel;
      while (advance(scene.style, name, current) > availableWidth && current > minLabel)
        --current;
      int prospective = qBound(minValue, qRound(current * 0.6), baseValue);
      while (current + spacing + prospective > availableHeight && current > minLabel) {
        --current;
        prospective = qBound(minValue, qRound(current * 0.6), baseValue);
      }
      leaf.label.fontSize = current;
      leaf.label.visible = complex
          ? current >= minLabel && availableHeight >= minLabel
          : advance(scene.style, name, current) <= availableWidth &&
                current >= minLabel && availableHeight >= current;
    }
    leaf.label.bounds = leaf.label.visible
        ? textBounds(scene.style, name, leaf.label.position, leaf.label.fontSize,
                     QStringLiteral("middle"), TreemapTextBaseline::Middle)
        : QRectF();
    leaf.value.role = QStringLiteral("leaf-value");
    leaf.value.text = showValues ? formatValue(work.value, valueFormat) : QString();
    leaf.value.anchor = QStringLiteral("middle");
    leaf.value.baseline = TreemapTextBaseline::Hanging;
    leaf.value.fill = textFill;
    leaf.value.fontSize = leaf.label.visible
        ? qBound(minValue, qRound(leaf.label.fontSize * 0.6), baseValue)
        : baseValue;
    leaf.value.position = QPointF(leaf.rect.center().x(),
                                  leaf.rect.center().y() + leaf.label.fontSize / 2.0 + spacing);
    const double maxBottom = leaf.rect.bottom() + 1.0 - 4.0;
    leaf.value.visible = showValues && leaf.label.visible && !leaf.value.text.isEmpty() &&
                         advance(scene.style, leaf.value.text, leaf.value.fontSize) <= availableWidth &&
                         leaf.value.position.y() + leaf.value.fontSize <= maxBottom &&
                         leaf.value.fontSize >= minValue;
    leaf.value.bounds = leaf.value.visible
        ? textBounds(scene.style, leaf.value.text, leaf.value.position,
                     leaf.value.fontSize, QStringLiteral("middle"),
                     TreemapTextBaseline::Hanging)
        : QRectF();
    scene.leaves.append(std::move(leaf));
  }

  if (!data.title.isEmpty()) {
    scene.title.role = QStringLiteral("title");
    scene.title.text = data.title;
    scene.title.position = QPointF(width / 2.0, 15.0);
    scene.title.anchor = QStringLiteral("middle");
    scene.title.fontSize = scene.style.titleFontSize;
    scene.title.fill = scene.style.titleColor;
    scene.title.bounds = textBounds(scene.style, data.title, scene.title.position,
                                    scene.title.fontSize, QStringLiteral("middle"),
                                    TreemapTextBaseline::Middle);
  }

  bool first = true;
  auto include = [&](const QRectF &rect) {
    if (rect.isEmpty()) return;
    if (first) { scene.contentBounds = rect; first = false; }
    else scene.contentBounds = scene.contentBounds.united(rect);
  };
  for (const auto &section : scene.sections)
    if (section.depth > 0) include(section.rect);
  for (const auto &leaf : scene.leaves) include(leaf.rect);
  include(scene.title.bounds);
  if (first) scene.contentBounds = QRectF(0.0, titleHeight, width, height);
  const double diagramPadding = jsNumber(config.diagramPadding, 8.0);
  scene.bounds = scene.contentBounds.adjusted(-diagramPadding, -diagramPadding,
                                              diagramPadding, diagramPadding);
  scene.rasterBounds = scene.bounds;
  scene.viewBoxAttribute = QStringLiteral("%1 %2 %3 %4")
      .arg(scene.bounds.x(), 0, 'g', 15)
      .arg(scene.bounds.y(), 0, 'g', 15)
      .arg(scene.bounds.width(), 0, 'g', 15)
      .arg(scene.bounds.height(), 0, 'g', 15);
  return scene;
}

} // namespace muffin::mermaid::treemap
