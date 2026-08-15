#include "mermaid/ishikawa/IshikawaScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/ishikawa/IshikawaScenePainter.h"

#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonObject>
#include <QRawFont>
#include <QRegularExpression>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace muffin::mermaid::ishikawa {
namespace {

constexpr qreal kSpineBaseLength = 250.0;
constexpr qreal kBoneStub = 30.0;
constexpr qreal kBoneBase = 60.0;
constexpr qreal kBonePerChild = 5.0;
constexpr qreal kAngle = 82.0 * 3.141592653589793238462643383279502884 / 180.0;
const qreal kCosA = std::cos(kAngle);
const qreal kSinA = std::sin(kAngle);

struct Extents {
  bool set = false;
  qreal left = 0.0;
  qreal top = 0.0;
  qreal right = 0.0;
  qreal bottom = 0.0;

  void point(const QPointF& point) {
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return;
    if (!set) {
      left = right = point.x();
      top = bottom = point.y();
      set = true;
      return;
    }
    left = std::min(left, point.x());
    right = std::max(right, point.x());
    top = std::min(top, point.y());
    bottom = std::max(bottom, point.y());
  }
  void rect(const QRectF& rect) {
    if (!std::isfinite(rect.left()) || !std::isfinite(rect.top()) ||
        !std::isfinite(rect.right()) || !std::isfinite(rect.bottom()))
      return;
    point(rect.topLeft());
    point(rect.bottomRight());
  }
  QRectF value() const {
    return set ? QRectF(left, top, right - left, bottom - top) : QRectF();
  }
};

QStringList cssFontFamilies(const QString& expression) {
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

editor::CssPixelFont textFont(const QString& familyExpression, qreal size,
                              QFont::Weight weight,
                              QFont::Style fontStyle = QFont::StyleNormal) {
  const QStringList families = cssFontFamilies(familyExpression);
  editor::CssPixelFont font =
      editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) font.font.setFamilies(families);
  font.font.setWeight(weight);
  font.font.setStyle(fontStyle);
  return font;
}

QString metricFamily(const QString& familyExpression, qreal size,
                     QFont::Weight weight, QFont::Style fontStyle) {
  const editor::CssPixelFont font =
      textFont(familyExpression, size, weight, fontStyle);
  const QRawFont raw = QRawFont::fromFont(font.font);
  return raw.isValid() && !raw.familyName().isEmpty()
             ? raw.familyName()
             : cssFontFamilies(familyExpression).first();
}

QString visibleSvgText(QString value) {
  static const QRegularExpression whitespace(
      QStringLiteral(R"([\x{0009}-\x{000d}\x{0020}]+)"));
  value.replace(whitespace, QStringLiteral(" "));
  while (value.startsWith(QLatin1Char(' '))) value.removeFirst();
  while (value.endsWith(QLatin1Char(' '))) value.chop(1);
  return value;
}

QStringList splitLines(const QString& text) {
  static const QRegularExpression breaks(
      QStringLiteral(R"(<br\s*\/?>|\n)"),
      QRegularExpression::CaseInsensitiveOption);
  return text.split(breaks, Qt::KeepEmptyParts);
}

QString wrapText(const QString& text, int maxChars) {
  if (text.size() <= maxChars) return text;
  static const QRegularExpression whitespace(QStringLiteral(
      R"([\x{0009}-\x{000d}\x{0020}\x{00a0}\x{1680}\x{2000}-\x{200a}\x{2028}\x{2029}\x{202f}\x{205f}\x{3000}\x{feff}]+)"));
  QStringList lines;
  const QStringList words = text.split(whitespace, Qt::SkipEmptyParts);
  for (const QString& word : words) {
    if (!lines.isEmpty() &&
        lines.back().size() + 1 + word.size() <= maxChars)
      lines.back() += QLatin1Char(' ') + word;
    else
      lines.append(word);
  }
  return lines.join(QLatin1Char('\n'));
}

QRectF roughBounds(const rough::Drawable& drawable) {
  return rough::tightBounds(drawable);
}

void translateRoughDrawable(rough::Drawable& drawable, qreal dx, qreal dy) {
  for (rough::OpSet& set : drawable.sets) {
    for (rough::Op& op : set.ops) {
      for (qsizetype i = 0; i + 1 < op.data.size(); i += 2) {
        op.data[i] += dx;
        op.data[i + 1] += dy;
      }
    }
  }
}

QRectF textBounds(const IshikawaTextGeometry& text,
                  const IshikawaSceneStyle& style, qreal deviceScale) {
  if (!(deviceScale > 0.0) || !std::isfinite(deviceScale))
    deviceScale = 1.0;
  if (!std::isfinite(text.fontSize) || text.fontSize < 0.0) return {};
  if (text.fontSize == 0.0) {
    // SVG keeps a zero-sized text bbox at the text anchor. Ishikawa uses that
    // coordinate while reducing each cause pair's next spine position.
    return QRectF(text.anchor.x() + text.translation.x(),
                  text.firstY + text.translation.y(), 0.0, 0.0);
  }
  if (!text.hasBox) return {};
  const QString family = text.fontFamily.isEmpty() ? style.fontFamily
                                                    : text.fontFamily;
  const editor::CssPixelFont font =
      textFont(family, text.fontSize, text.weight, text.fontStyle);
  const QFontMetricsF qmetrics(font.font);
  const flowchart::FlowLabelFontMetrics metrics =
      flowchart::flowLabelFontBoundingMetrics(
          metricFamily(family, text.fontSize, text.weight, text.fontStyle),
          text.fontSize,
          text.weight);
  Extents extents;
  for (int index = 0; index < text.lines.size(); ++index) {
    const QString visible = visibleSvgText(text.lines.at(index));
    if (visible.isEmpty()) continue;
    const qreal anchorY = text.firstY + index * text.lineStep;
    flowchart::FlowLabelDocument document;
    document.text = visible;
    const QString metric =
        metricFamily(family, text.fontSize, text.weight, text.fontStyle);
    const QRectF layoutInk = flowchart::measureChromiumSvgTextLayoutBounds(
        document, metric, text.fontSize, deviceScale);
    QRectF ink = flowchart::measureChromiumSvgTextBounds(
        document, metric, text.fontSize, text.weight, deviceScale);
    if (text.fontStyle != QFont::StyleNormal) {
      const editor::CssPixelFont regular =
          textFont(family, text.fontSize, QFont::Normal, QFont::StyleNormal);
      QTextLayout regularLayout(visible, regular.font);
      QTextOption regularOption;
      regularOption.setUseDesignMetrics(true);
      regularLayout.setTextOption(regularOption);
      regularLayout.beginLayout();
      QTextLine regularLine = regularLayout.createLine();
      if (regularLine.isValid())
        regularLine.setLineWidth(std::numeric_limits<qreal>::max());
      regularLayout.endLayout();
      QPainterPath regularPath;
      if (regularLine.isValid()) {
        for (const QGlyphRun& run : regularLine.glyphRuns(
                 0, -1, QTextLayout::RetrieveAll)) {
          const auto glyphs = run.glyphIndexes();
          const auto positions = run.positions();
          for (qsizetype glyph = 0;
               glyph < std::min(glyphs.size(), positions.size()); ++glyph) {
            QPainterPath path = run.rawFont().pathForGlyph(glyphs.at(glyph));
            path.translate(positions.at(glyph));
            regularPath.addPath(path);
          }
        }
      }
      // The Mermaid fixture exposes only Noto Sans Regular. Blink therefore
      // synthesizes CSS italic by skewing that regular outline while retaining
      // the unskewed ShapeResult advance cell. The cell fixes the left edge;
      // only the skewed glyph fringe may extend the right edge.
      QTransform skew;
      skew.shear(0.208, 0.0);
      const qreal italicRight =
          skew.map(regularPath).boundingRect().right() * regular.scale;
      ink.setLeft(0.0);
      ink.setRight(std::max(layoutInk.width(), italicRight));
    }
    if (!ink.isValid()) {
      ink = qmetrics.boundingRect(visible);
      ink = QRectF(ink.x() * font.scale, ink.y() * font.scale,
                    ink.width() * font.scale, ink.height() * font.scale);
    }
    qreal advance = layoutInk.width();
    qreal left = ink.left();
    if (text.textAnchor == IshikawaTextAnchor::Middle) left -= advance / 2.0;
    else if (text.textAnchor == IshikawaTextAnchor::End) left -= advance;

    qreal baseline = anchorY;
    if (text.baseline == IshikawaTextBaseline::Middle)
      baseline += metrics.xHeight / (2.0 * deviceScale);
    else if (text.baseline == IshikawaTextBaseline::Hanging)
      baseline += metrics.ascent * 0.8 / deviceScale;
    extents.rect(QRectF(text.anchor.x() + left + text.translation.x(),
                        baseline - metrics.ascent / deviceScale +
                            text.translation.y(),
                        ink.width(), metrics.height() / deviceScale));
  }
  return extents.value();
}

int appendText(IshikawaScene& scene, IshikawaTextGeometry text) {
  if (text.domKey.isEmpty())
    text.domKey = QStringLiteral("text-%1").arg(scene.texts.size());
  const auto resolved = scene.style.textStyles.constFind(text.domKey);
  if (resolved != scene.style.textStyles.cend()) {
    text.fontFamily = resolved->fontFamily;
    text.fontSize = resolved->fontSize;
    text.weight = resolved->fontWeight;
    text.fontStyle = resolved->fontStyle;
    text.textAnchor = resolved->textAnchor;
    text.baseline = resolved->baseline;
    text.fill = resolved->fill;
    text.opacity = resolved->opacity;
    text.visible = resolved->visible;
    text.hasBox = resolved->hasBox;
    text.rootHasBox = resolved->rootHasBox;
  } else {
    text.fontFamily = scene.style.fontFamily;
    text.fill = scene.style.textColor;
  }
  text.layoutBounds = textBounds(text, scene.style, 1.0);
  text.bounds = text.layoutBounds;
  const int index = scene.texts.size();
  scene.texts.append(std::move(text));
  scene.paintOrder.append({IshikawaPrimitiveKind::Text, index});
  const auto& stored = scene.texts.back();
  scene.domElements.append({stored.domKey, stored.parentKey,
                            QStringLiteral("text"),
                            stored.className.split(QLatin1Char(' '),
                                                   Qt::SkipEmptyParts),
                            IshikawaPrimitiveKind::Text, index, true});
  return index;
}

QString appendGroup(IshikawaScene& scene, const QString& parent,
                    const QStringList& classes, const QString& prefix) {
  const QString key = QStringLiteral("%1-%2").arg(prefix).arg(
      scene.domElements.size());
  scene.domElements.append({key, parent, QStringLiteral("g"), classes,
                            IshikawaPrimitiveKind::Line, -1, false});
  return key;
}

rough::Options roughOptions(const IshikawaScene& scene, bool filled,
                            const QString& stroke, qreal strokeWidth,
                            const QString& fill = QString()) {
  rough::Options options;
  options.roughness = filled ? 1.5 : 1.5;
  options.seed = quint32(editor::jsNumberValue(scene.config.handDrawnSeed));
  options.stroke = stroke;
  options.strokeWidth = strokeWidth;
  if (filled) {
    options.fill = fill;
    options.fillStyle = QStringLiteral("hachure");
    options.fillWeight = 2.5;
    options.hachureGap = 5.0;
  }
  return options;
}

int appendLine(IshikawaScene& scene, const QString& className,
               const QLineF& value, const QString& parentKey) {
  IshikawaLineGeometry line;
  line.domKey = QStringLiteral("line-%1").arg(scene.lines.size());
  line.parentKey = parentKey;
  line.className = className;
  line.line = value;
  line.stroke = scene.style.lineColor;
  line.strokeWidth = className == QLatin1String("ishikawa-sub-branch")
                         ? 1.0 : 2.0;
  const auto resolved = scene.style.shapeStyles.constFind(line.domKey);
  if (resolved != scene.style.shapeStyles.cend()) {
    line.stroke = resolved->stroke;
    line.strokeWidth = resolved->strokeWidth;
    line.strokeOpacity = resolved->strokeOpacity;
    line.visible = resolved->visible;
    line.hasBox = resolved->hasBox;
    line.rootHasBox = resolved->rootHasBox;
  }
  line.rough = scene.style.look == QLatin1String("handDrawn");
  if (line.rough)
    line.roughDrawable =
        rough::line(value.x1(), value.y1(), value.x2(), value.y2(),
                    roughOptions(scene, false, line.stroke,
                                 line.strokeWidth));
  const int index = scene.lines.size();
  scene.lines.append(std::move(line));
  scene.paintOrder.append({IshikawaPrimitiveKind::Line, index});
  const auto& stored = scene.lines.back();
  scene.domElements.append({stored.domKey, parentKey,
                            stored.rough ? QStringLiteral("g")
                                         : QStringLiteral("line"),
                            {className}, IshikawaPrimitiveKind::Line,
                            index, true});
  return index;
}

int appendPath(IshikawaScene& scene, const QString& className,
               const QPainterPath& value, bool filled,
               const QString& parentKey) {
  IshikawaPathGeometry path;
  path.domKey = QStringLiteral("path-%1").arg(scene.paths.size());
  path.parentKey = parentKey;
  path.className = className;
  path.path = value;
  path.fill = filled ? scene.style.mainBkg : scene.style.lineColor;
  path.stroke = scene.style.lineColor;
  path.strokeWidth = className.isEmpty() ? 1.0 : 2.0;
  const auto resolved = scene.style.shapeStyles.constFind(path.domKey);
  if (resolved != scene.style.shapeStyles.cend()) {
    path.fill = resolved->fill;
    path.stroke = resolved->stroke;
    path.strokeWidth = resolved->strokeWidth;
    path.fillOpacity = resolved->fillOpacity;
    path.strokeOpacity = resolved->strokeOpacity;
    path.visible = resolved->visible;
    path.hasBox = resolved->hasBox;
    path.rootHasBox = resolved->rootHasBox;
  }
  path.rough = scene.style.look == QLatin1String("handDrawn");
  if (path.rough)
    path.roughDrawable = rough::path(
        value, roughOptions(scene, filled, path.stroke, path.strokeWidth,
                            path.fill), true);
  const int index = scene.paths.size();
  scene.paths.append(std::move(path));
  scene.paintOrder.append({IshikawaPrimitiveKind::Path, index});
  const auto& stored = scene.paths.back();
  scene.domElements.append({stored.domKey, parentKey,
                            stored.rough ? QStringLiteral("g")
                                         : QStringLiteral("path"),
                            className.split(QLatin1Char(' '),
                                            Qt::SkipEmptyParts),
                            IshikawaPrimitiveKind::Path, index, true});
  return index;
}

void appendRoughArrow(IshikawaScene& scene, qreal x, qreal y, qreal dx,
                      qreal dy, const QString& parentKey) {
  const qreal length = std::hypot(dx, dy);
  if (!(length > 0.0)) return;
  const qreal ux = dx / length;
  const qreal uy = dy / length;
  const qreal size = 6.0;
  const qreal px = -uy * size;
  const qreal py = ux * size;
  QPainterPath arrow;
  arrow.moveTo(x, y);
  arrow.lineTo(x - ux * size * 2.0 + px,
               y - uy * size * 2.0 + py);
  arrow.lineTo(x - ux * size * 2.0 - px,
               y - uy * size * 2.0 - py);
  arrow.closeSubpath();

  IshikawaPathGeometry geometry;
  geometry.domKey = QStringLiteral("path-%1").arg(scene.paths.size());
  geometry.parentKey = parentKey;
  geometry.path = arrow;
  geometry.rough = true;
  geometry.fill = scene.style.lineColor;
  geometry.stroke = scene.style.lineColor;
  geometry.strokeWidth = 1.0;
  rough::Options options;
  options.roughness = 1.0;
  options.seed = quint32(editor::jsNumberValue(scene.config.handDrawnSeed));
  options.fill = scene.style.lineColor;
  options.fillStyle = QStringLiteral("solid");
  options.stroke = scene.style.lineColor;
  options.strokeWidth = 1.0;
  geometry.roughDrawable = rough::path(arrow, options, true);
  const int index = scene.paths.size();
  scene.paths.append(std::move(geometry));
  scene.paintOrder.append({IshikawaPrimitiveKind::Path, index});
  scene.domElements.append({scene.paths.back().domKey, parentKey,
                            QStringLiteral("g"), {},
                            IshikawaPrimitiveKind::Path, index, true});
}

int appendRect(IshikawaScene& scene, const QString& className,
               const QRectF& value, const QString& parentKey) {
  IshikawaRectGeometry rect;
  rect.domKey = QStringLiteral("rect-%1").arg(scene.rects.size());
  rect.parentKey = parentKey;
  rect.className = className;
  rect.rect = value;
  rect.fill = scene.style.mainBkg;
  rect.stroke = scene.style.lineColor;
  rect.strokeWidth = 2.0;
  const auto resolved = scene.style.shapeStyles.constFind(rect.domKey);
  if (resolved != scene.style.shapeStyles.cend()) {
    rect.fill = resolved->fill;
    rect.stroke = resolved->stroke;
    rect.strokeWidth = resolved->strokeWidth;
    rect.fillOpacity = resolved->fillOpacity;
    rect.strokeOpacity = resolved->strokeOpacity;
    rect.visible = resolved->visible;
    rect.hasBox = resolved->hasBox;
    rect.rootHasBox = resolved->rootHasBox;
  }
  rect.rough = scene.style.look == QLatin1String("handDrawn");
  if (rect.rough)
    rect.roughDrawable = rough::rectangle(
        value.x(), value.y(), value.width(), value.height(),
        roughOptions(scene, true, rect.stroke, rect.strokeWidth, rect.fill));
  const int index = scene.rects.size();
  scene.rects.append(std::move(rect));
  scene.paintOrder.append({IshikawaPrimitiveKind::Rect, index});
  const auto& stored = scene.rects.back();
  scene.domElements.append({stored.domKey, parentKey,
                            stored.rough ? QStringLiteral("g")
                                         : QStringLiteral("rect"),
                            {className}, IshikawaPrimitiveKind::Rect,
                            index, true});
  return index;
}

struct SideStats {
  int total = 0;
  int maximum = 0;
};

int descendantCount(const IshikawaNode& node) {
  int result = 0;
  for (const IshikawaNode& child : node.children)
    result += 1 + descendantCount(child);
  return result;
}

SideStats sideStats(const QVector<const IshikawaNode*>& nodes) {
  SideStats result;
  for (const IshikawaNode* node : nodes) {
    const int descendants = descendantCount(*node);
    result.total += descendants;
    result.maximum = std::max(result.maximum, descendants);
  }
  return result;
}

struct LabelEntry {
  QString text;
  int depth = 0;
  int parentIndex = -1;
  int childCount = 0;
};

struct FlattenedTree {
  QVector<LabelEntry> entries;
  QVector<int> yOrder;
};

FlattenedTree flattenTree(const QVector<IshikawaNode>& children,
                          int direction) {
  FlattenedTree result;
  std::function<void(const QVector<IshikawaNode>&, int, int)> walk;
  walk = [&](const QVector<IshikawaNode>& nodes, int parent, int depth) {
    QVector<int> order;
    order.reserve(nodes.size());
    if (direction < 0)
      for (int i = nodes.size() - 1; i >= 0; --i) order.append(i);
    else
      for (int i = 0; i < nodes.size(); ++i) order.append(i);
    for (int sourceIndex : order) {
      const IshikawaNode& child = nodes.at(sourceIndex);
      const int index = result.entries.size();
      result.entries.append({wrapText(child.text, 15), depth, parent,
                             int(child.children.size())});
      if (depth % 2 == 0) {
        result.yOrder.append(index);
        if (!child.children.isEmpty())
          walk(child.children, index, depth + 1);
      } else {
        if (!child.children.isEmpty())
          walk(child.children, index, depth + 1);
        result.yOrder.append(index);
      }
    }
  };
  walk(children, -1, 2);
  return result;
}

struct BoneInfo {
  qreal x0 = 0.0;
  qreal y0 = 0.0;
  qreal x1 = 0.0;
  qreal y1 = 0.0;
  int childCount = 0;
  int childrenDrawn = 0;
};

qreal lerp(qreal a, qreal b, qreal t) { return a + (b - a) * t; }

int drawMultilineText(IshikawaScene& scene, QString text, qreal x, qreal y,
                      QString className, IshikawaTextAnchor anchor,
                      IshikawaTextBaseline baseline, qreal layoutFontSize,
                      qreal paintFontSize,
                      QFont::Weight weight, const QString& parentKey) {
  IshikawaTextGeometry geometry;
  geometry.parentKey = parentKey;
  geometry.domKey = QStringLiteral("text-%1").arg(scene.texts.size());
  geometry.className = std::move(className);
  geometry.lines = splitLines(text);
  geometry.source = geometry.lines.join(QString());
  geometry.anchor = QPointF(x, y);
  geometry.firstY =
      y - ((geometry.lines.size() - 1) * layoutFontSize * 1.05) / 2.0;
  geometry.lineStep = layoutFontSize * 1.05;
  geometry.fontSize = paintFontSize;
  geometry.weight = weight;
  geometry.textAnchor = anchor;
  geometry.baseline = baseline;
  return appendText(scene, std::move(geometry));
}

void drawHead(IshikawaScene& scene, qreal x, qreal y, const QString& label,
              qreal layoutFontSize, const QString& parentKey) {
  const QString groupKey = appendGroup(
      scene, parentKey, {QStringLiteral("ishikawa-head-group")},
      QStringLiteral("head-group"));
  const int maxChars =
      std::max(6, int(std::floor(110.0 / (layoutFontSize * 0.6))));
  const int textIndex = drawMultilineText(
      scene, wrapText(label, maxChars), 0.0, 0.0,
      QStringLiteral("ishikawa-head-label"), IshikawaTextAnchor::Middle,
      IshikawaTextBaseline::Middle, layoutFontSize, 14.0,
      QFont::DemiBold, groupKey);
  IshikawaTextGeometry& text = scene.texts[textIndex];
  const QRectF original = text.layoutBounds;
  const qreal width = std::max(60.0, original.width() + 6.0);
  const qreal height = std::max(40.0, original.height() * 2.0 + 40.0);
  text.translation =
      QPointF((width - original.width()) / 2.0 - original.x() + 3.0,
              -original.y() - original.height() / 2.0);
  text.translation += QPointF(x, y);
  text.layoutBounds = textBounds(text, scene.style, 1.0);
  text.bounds = text.layoutBounds;

  // RoughJS generates the path in the head group's local coordinates. The
  // group transform is applied afterwards; hachure rotation and scan-line
  // rounding are coordinate-sensitive, so generating directly at (x, y)
  // produces a different fill even though the outline is translation-invariant.
  QPainterPath head;
  head.moveTo(0.0, -height / 2.0);
  head.lineTo(0.0, height / 2.0);
  head.quadTo(width * 2.4, 0.0, 0.0, -height / 2.0);
  head.closeSubpath();
  const int pathIndex =
      appendPath(scene, QStringLiteral("ishikawa-head"), head, true,
                 groupKey);
  QTransform headTransform;
  headTransform.translate(x, y);
  scene.paths[pathIndex].path = headTransform.map(scene.paths[pathIndex].path);
  if (scene.paths[pathIndex].rough)
    translateRoughDrawable(scene.paths[pathIndex].roughDrawable, x, y);
  // insert(':first-child') puts the head before its text in the DOM/paint order.
  const IshikawaPaintEntry pathEntry = scene.paintOrder.takeLast();
  const int textOrder = std::find_if(
      scene.paintOrder.begin(), scene.paintOrder.end(),
      [&](const IshikawaPaintEntry& entry) {
        return entry.kind == IshikawaPrimitiveKind::Text &&
               entry.index == textIndex;
      }) - scene.paintOrder.begin();
  scene.paintOrder.insert(textOrder, pathEntry);
  const auto domPath = std::find_if(
      scene.domElements.begin(), scene.domElements.end(),
      [&](const IshikawaDomElement& element) {
        return element.primitive &&
               element.kind == IshikawaPrimitiveKind::Path &&
               element.index == pathIndex;
      });
  const auto domText = std::find_if(
      scene.domElements.begin(), scene.domElements.end(),
      [&](const IshikawaDomElement& element) {
        return element.primitive &&
               element.kind == IshikawaPrimitiveKind::Text &&
               element.index == textIndex;
      });
  if (domPath != scene.domElements.end() && domText != scene.domElements.end() &&
      domPath > domText) {
    const IshikawaDomElement value = *domPath;
    scene.domElements.erase(domPath);
    const auto target = std::find_if(
        scene.domElements.begin(), scene.domElements.end(),
        [&](const IshikawaDomElement& element) {
          return element.primitive &&
                 element.kind == IshikawaPrimitiveKind::Text &&
                 element.index == textIndex;
        });
    scene.domElements.insert(target, value);
  }
  Q_UNUSED(pathIndex);
}

void drawCauseLabel(IshikawaScene& scene, const QString& text, qreal x,
                    qreal y, int direction, qreal layoutFontSize,
                    const QString& parentKey) {
  const QString groupKey = appendGroup(
      scene, parentKey, {QStringLiteral("ishikawa-label-group")},
      QStringLiteral("label-group"));
  const int textIndex = drawMultilineText(
      scene, text, x, y + 11.0 * direction,
      QStringLiteral("ishikawa-label cause"), IshikawaTextAnchor::Middle,
      IshikawaTextBaseline::Middle, layoutFontSize, scene.style.fontSize,
      QFont::Normal, groupKey);
  const QRectF bounds = scene.texts.at(textIndex).layoutBounds;
  appendRect(scene, QStringLiteral("ishikawa-label-box"),
             bounds.adjusted(-20.0, -2.0, 20.0, 2.0), groupKey);
  const IshikawaPaintEntry rectEntry = scene.paintOrder.takeLast();
  const int textOrder = std::find_if(
      scene.paintOrder.begin(), scene.paintOrder.end(),
      [&](const IshikawaPaintEntry& entry) {
        return entry.kind == IshikawaPrimitiveKind::Text &&
               entry.index == textIndex;
      }) - scene.paintOrder.begin();
  scene.paintOrder.insert(textOrder, rectEntry);
  const auto domRect = std::find_if(
      scene.domElements.begin(), scene.domElements.end(),
      [&](const IshikawaDomElement& element) {
        return element.primitive &&
               element.kind == IshikawaPrimitiveKind::Rect &&
               element.index == scene.rects.size() - 1;
      });
  const auto domText = std::find_if(
      scene.domElements.begin(), scene.domElements.end(),
      [&](const IshikawaDomElement& element) {
        return element.primitive &&
               element.kind == IshikawaPrimitiveKind::Text &&
               element.index == textIndex;
      });
  if (domRect != scene.domElements.end() && domText != scene.domElements.end() &&
      domRect > domText) {
    const IshikawaDomElement value = *domRect;
    scene.domElements.erase(domRect);
    const auto target = std::find_if(
        scene.domElements.begin(), scene.domElements.end(),
        [&](const IshikawaDomElement& element) {
          return element.primitive &&
                 element.kind == IshikawaPrimitiveKind::Text &&
                 element.index == textIndex;
        });
    scene.domElements.insert(target, value);
  }
}

void drawBranch(IshikawaScene& scene, const IshikawaNode& node,
                qreal startX, qreal startY, int direction, qreal length,
                qreal layoutFontSize, const QString& parentKey) {
  const qreal lineLength = length * (node.children.isEmpty() ? 0.2 : 1.0);
  const qreal dx = -kCosA * lineLength;
  const qreal dy = kSinA * lineLength * direction;
  const qreal endX = startX + dx;
  const qreal endY = startY + dy;
  const int branch =
      appendLine(scene, QStringLiteral("ishikawa-branch"),
                 QLineF(startX, startY, endX, endY), parentKey);
  scene.lines[branch].markerStart = true;
  if (scene.style.look == QLatin1String("handDrawn"))
    appendRoughArrow(scene, startX, startY, startX - endX, startY - endY,
                     parentKey);
  drawCauseLabel(scene, node.text, endX, endY, direction, layoutFontSize,
                 parentKey);
  if (node.children.isEmpty()) return;

  const FlattenedTree flat = flattenTree(node.children, direction);
  QVector<qreal> ys(flat.entries.size());
  for (int slot = 0; slot < flat.yOrder.size(); ++slot)
    ys[flat.yOrder.at(slot)] =
        startY + dy * qreal(slot + 1) / qreal(flat.entries.size() + 1);

  std::map<int, BoneInfo> bones;
  bones.emplace(-1, BoneInfo{startX, startY, endX, endY,
                             int(node.children.size()), 0});
  const qreal diagonalX = -kCosA;
  const qreal diagonalY = kSinA * direction;
  for (int index = 0; index < flat.entries.size(); ++index) {
    const LabelEntry& entry = flat.entries.at(index);
    const QString groupKey = appendGroup(
        scene, parentKey, {QStringLiteral("ishikawa-sub-group")},
        QStringLiteral("sub-group"));
    BoneInfo& parent = bones.at(entry.parentIndex);
    const qreal y = ys.at(index);
    qreal x0 = 0.0;
    qreal y0 = 0.0;
    qreal x1 = 0.0;
    QString className;
    IshikawaTextBaseline baseline = IshikawaTextBaseline::Middle;
    if (entry.depth % 2 == 0) {
      const qreal parentDy = parent.y1 - parent.y0;
      x0 = lerp(parent.x0, parent.x1,
                parentDy != 0.0 ? (y - parent.y0) / parentDy : 0.5);
      y0 = y;
      x1 = x0 - (entry.childCount > 0
                     ? kBoneBase + entry.childCount * kBonePerChild
                     : kBoneStub);
      className = QStringLiteral("ishikawa-label align");
    } else {
      const int childIndex = parent.childrenDrawn++;
      x0 = lerp(parent.x0, parent.x1,
                qreal(parent.childCount - childIndex) /
                    qreal(parent.childCount + 1));
      y0 = parent.y0;
      x1 = x0 + diagonalX * ((y - y0) / diagonalY);
      className = direction < 0 ? QStringLiteral("ishikawa-label up")
                                : QStringLiteral("ishikawa-label down");
      baseline = direction < 0 ? IshikawaTextBaseline::Auto
                               : IshikawaTextBaseline::Hanging;
    }
    const int lineIndex =
        appendLine(scene, QStringLiteral("ishikawa-sub-branch"),
                   QLineF(x0, y0, x1, y), groupKey);
    scene.lines[lineIndex].markerStart = true;
    if (scene.style.look == QLatin1String("handDrawn")) {
      if (entry.depth % 2 == 0)
        appendRoughArrow(scene, x0, y, 1.0, 0.0, groupKey);
      else
        appendRoughArrow(scene, x0, y0, x0 - x1, y0 - y, groupKey);
    }
    drawMultilineText(scene, entry.text, x1, y, className,
                      IshikawaTextAnchor::End, baseline, layoutFontSize,
                      scene.style.fontSize, QFont::Normal, groupKey);
    if (entry.childCount > 0)
      bones.emplace(index,
                    BoneInfo{x0, y0, x1, y, entry.childCount, 0});
  }
}

void includeSceneGeometry(IshikawaScene& scene) {
  Extents extents;
  for (const IshikawaLineGeometry& line : scene.lines) {
    if (!line.rootHasBox) continue;
    if (line.rough)
      extents.rect(roughBounds(line.roughDrawable));
    else {
      extents.point(line.line.p1());
      extents.point(line.line.p2());
    }
  }
  for (const IshikawaPathGeometry& path : scene.paths) {
    if (!path.rootHasBox) continue;
    extents.rect(path.rough ? roughBounds(path.roughDrawable)
                            : path.path.boundingRect());
  }
  for (const IshikawaRectGeometry& rect : scene.rects) {
    if (!rect.rootHasBox) continue;
    extents.rect(rect.rough ? roughBounds(rect.roughDrawable) : rect.rect);
  }
  for (const IshikawaTextGeometry& text : scene.texts)
    if (text.rootHasBox) extents.rect(text.layoutBounds);
  scene.contentBounds = extents.value();
  scene.bounds = scene.contentBounds.adjusted(-scene.padding, -scene.padding,
                                               scene.padding, scene.padding);
}

qreal renderedSvgScale(const IshikawaScene& scene) {
  const qreal width = scene.bounds.width();
  const qreal height = scene.bounds.height();
  if (!(width > 0.0) || !(height > 0.0) || !std::isfinite(width) ||
      !std::isfinite(height))
    return 1.0;
  const qreal clientWidth = std::floor(width * 64.0) / 64.0;
  const qreal clientHeight = scene.useMaxWidth
      ? std::floor(clientWidth * height / width * 64.0) / 64.0
      : std::floor(height * 64.0) / 64.0;
  if (!(clientWidth > 0.0) || !(clientHeight > 0.0)) return 1.0;
  return std::min(clientWidth / width, clientHeight / height);
}

QString primitiveName(IshikawaPrimitiveKind kind) {
  switch (kind) {
    case IshikawaPrimitiveKind::Line:
      return QStringLiteral("line");
    case IshikawaPrimitiveKind::Path:
      return QStringLiteral("path");
    case IshikawaPrimitiveKind::Rect:
      return QStringLiteral("rect");
    case IshikawaPrimitiveKind::Text:
      return QStringLiteral("text");
  }
  return {};
}

QJsonArray rectJson(const QRectF& rect) {
  return {rect.x(), rect.y(), rect.width(), rect.height()};
}

}  // namespace

void IshikawaScene::paint(QPainter& painter,
                          const MermaidPaintOptions& options) const {
  paintIshikawaScene(*this, painter, options);
}

IshikawaScene buildIshikawaScene(const IshikawaData& data,
                                  IshikawaConfig config,
                                  IshikawaSceneStyle style) {
  IshikawaScene scene;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.useMaxWidth = editor::truthyConfigValue(scene.config.useMaxWidth);
  const QJsonValue paddingValue =
      scene.config.diagramPadding.isUndefined() ||
              scene.config.diagramPadding.isNull() ||
              scene.config.diagramPadding.isArray() ||
              scene.config.diagramPadding.isObject()
          ? QJsonValue(20.0)
          : scene.config.diagramPadding;
  scene.padding = editor::jsNumberValue(paddingValue);
  if (!data.hasRoot) return scene;
  scene.domElements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                            {}, IshikawaPrimitiveKind::Line, -1, false});
  scene.domElements.append({QStringLiteral("ishikawa"), QStringLiteral("svg"),
                            QStringLiteral("g"),
                            {QStringLiteral("ishikawa")},
                            IshikawaPrimitiveKind::Line, -1, false});
  const QString rootKey = QStringLiteral("ishikawa");
  if (scene.style.look != QLatin1String("handDrawn")) {
    scene.domElements.append({QStringLiteral("defs"), rootKey,
                              QStringLiteral("defs"), {},
                              IshikawaPrimitiveKind::Line, -1, false});
    scene.domElements.append({QStringLiteral("marker"), QStringLiteral("defs"),
                              QStringLiteral("marker"), {},
                              IshikawaPrimitiveKind::Line, -1, false});
    scene.domElements.append({QStringLiteral("marker-path"),
                              QStringLiteral("marker"), QStringLiteral("path"),
                              {QStringLiteral("ishikawa-arrow")},
                              IshikawaPrimitiveKind::Path, -1, false});
  }

  qreal spineX = 0.0;
  qreal spineY = kSpineBaseLength;
  int spineLine = -1;
  if (scene.style.look != QLatin1String("handDrawn"))
    spineLine = appendLine(scene, QStringLiteral("ishikawa-spine"),
                           QLineF(spineX, spineY, spineX, spineY), rootKey);
  drawHead(scene, spineX, spineY, data.root.text,
           scene.style.layoutFontSize, rootKey);
  if (data.root.children.isEmpty()) {
    if (scene.style.look == QLatin1String("handDrawn"))
      appendLine(scene, QStringLiteral("ishikawa-spine"),
                 QLineF(spineX, spineY, spineX, spineY), rootKey);
    includeSceneGeometry(scene);
    for (IshikawaTextGeometry& text : scene.texts)
      text.bounds = textBounds(text, scene.style, renderedSvgScale(scene));
    return scene;
  }

  spineX -= 20.0;
  QVector<const IshikawaNode*> upper;
  QVector<const IshikawaNode*> lower;
  for (int i = 0; i < data.root.children.size(); ++i)
    (i % 2 == 0 ? upper : lower).append(&data.root.children.at(i));
  const SideStats upperStats = sideStats(upper);
  const SideStats lowerStats = sideStats(lower);
  const int descendantTotal = upperStats.total + lowerStats.total;
  qreal upperLength = kSpineBaseLength;
  qreal lowerLength = kSpineBaseLength;
  if (descendantTotal > 0) {
    const qreal pool = kSpineBaseLength * 2.0;
    const qreal minimum = kSpineBaseLength * 0.3;
    upperLength =
        std::max(minimum, pool * upperStats.total / descendantTotal);
    lowerLength =
        std::max(minimum, pool * lowerStats.total / descendantTotal);
  }
  const qreal minimumSpacing = scene.style.layoutFontSize * 2.0;
  upperLength =
      std::max(upperLength, upperStats.maximum * minimumSpacing);
  lowerLength =
      std::max(lowerLength, lowerStats.maximum * minimumSpacing);
  spineY = std::max(upperLength, kSpineBaseLength);

  // drawHead created both the head path and text at the initial Y. Moving the
  // SVG group updates all its children together.
  const qreal headMove = spineY - kSpineBaseLength;
  for (IshikawaPathGeometry& path : scene.paths) {
    QTransform transform;
    transform.translate(0.0, headMove);
    path.path = transform.map(path.path);
    if (path.rough)
      translateRoughDrawable(path.roughDrawable, 0.0, headMove);
  }
  if (!scene.texts.isEmpty()) {
    scene.texts.front().translation.ry() += headMove;
    scene.texts.front().layoutBounds =
        textBounds(scene.texts.front(), scene.style, 1.0);
    scene.texts.front().bounds = scene.texts.front().layoutBounds;
  }
  if (spineLine >= 0)
    scene.lines[spineLine].line =
        QLineF(0.0, spineY, 0.0, spineY);

  const int pairCount = (data.root.children.size() + 1) / 2;
  for (int pair = 0; pair < pairCount; ++pair) {
    const QString pairKey = appendGroup(
        scene, rootKey, {QStringLiteral("ishikawa-pair")},
        QStringLiteral("pair"));
    const int textStart = scene.texts.size();
    const int upperIndex = pair * 2;
    const int lowerIndex = upperIndex + 1;
    if (upperIndex < data.root.children.size())
      drawBranch(scene, data.root.children.at(upperIndex), spineX, spineY,
                 -1, upperLength, scene.style.layoutFontSize, pairKey);
    if (lowerIndex < data.root.children.size())
      drawBranch(scene, data.root.children.at(lowerIndex), spineX, spineY,
                 1, lowerLength, scene.style.layoutFontSize, pairKey);
    qreal left = std::numeric_limits<qreal>::infinity();
    for (int i = textStart; i < scene.texts.size(); ++i)
      left = std::min(left, scene.texts.at(i).layoutBounds.left());
    spineX = left;
  }

  if (scene.style.look == QLatin1String("handDrawn"))
    appendLine(scene, QStringLiteral("ishikawa-spine"),
               QLineF(spineX, spineY, 0.0, spineY), rootKey);
  else
    scene.lines[spineLine].line =
        QLineF(spineX, spineY, 0.0, spineY);
  includeSceneGeometry(scene);
  const qreal visualScale = renderedSvgScale(scene);
  for (IshikawaTextGeometry& text : scene.texts)
    text.bounds = textBounds(text, scene.style, visualScale);
  return scene;
}

QJsonObject IshikawaScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("contentBounds")] = rectJson(contentBounds);
  root[QStringLiteral("useMaxWidth")] = useMaxWidth;
  root[QStringLiteral("padding")] = padding;
  QJsonArray lineArray;
  for (const auto& line : lines)
    lineArray.append(
        QJsonObject{{QStringLiteral("class"), line.className},
                    {QStringLiteral("line"),
                     QJsonArray{line.line.x1(), line.line.y1(),
                                line.line.x2(), line.line.y2()}},
                    {QStringLiteral("markerStart"), line.markerStart},
                    {QStringLiteral("rough"), line.rough}});
  root[QStringLiteral("lines")] = lineArray;
  QJsonArray pathArray;
  for (const auto& path : paths)
    pathArray.append(
        QJsonObject{{QStringLiteral("class"), path.className},
                    {QStringLiteral("bounds"),
                     rectJson(path.rough ? roughBounds(path.roughDrawable)
                                         : path.path.boundingRect())},
                    {QStringLiteral("rough"), path.rough}});
  root[QStringLiteral("paths")] = pathArray;
  QJsonArray rectArray;
  for (const auto& rect : rects)
    rectArray.append(
        QJsonObject{{QStringLiteral("class"), rect.className},
                    {QStringLiteral("rect"), rectJson(rect.rect)},
                    {QStringLiteral("rough"), rect.rough}});
  root[QStringLiteral("rects")] = rectArray;
  QJsonArray textArray;
  for (const auto& text : texts)
    textArray.append(
        QJsonObject{{QStringLiteral("class"), text.className},
                    {QStringLiteral("source"), text.source},
                    {QStringLiteral("bounds"), rectJson(text.bounds)}});
  root[QStringLiteral("texts")] = textArray;
  QJsonArray order;
  for (const auto& entry : paintOrder)
    order.append(primitiveName(entry.kind) + QLatin1Char(':') +
                 QString::number(entry.index));
  root[QStringLiteral("paintOrder")] = order;
  return root;
}

}  // namespace muffin::mermaid::ishikawa
