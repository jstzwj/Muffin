#include "mermaid/timeline/TimelineScene.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/text/LabelText.h"
#include "mermaid/timeline/TimelineScenePainter.h"
#include "theme/CssCalc.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <QRawFont>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace muffin::mermaid::timeline {

void TimelineScene::paint(QPainter& painter,
                          const MermaidPaintOptions& options) const {
  paintTimelineScene(*this, painter, options);
}

namespace {

struct Extents {
  bool set = false;
  qreal left = 0.0;
  qreal top = 0.0;
  qreal right = 0.0;
  qreal bottom = 0.0;

  void point(qreal x, qreal y) {
    // Invalid SVG geometry attributes do not contribute to getBBox(). Keep
    // surviving text/paths finite instead of letting one NaN poison viewBox.
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (!set) {
      left = right = x;
      top = bottom = y;
      set = true;
      return;
    }
    left = std::min(left, x);
    right = std::max(right, x);
    top = std::min(top, y);
    bottom = std::max(bottom, y);
  }
  void rect(const QRectF& r) {
    if (!std::isfinite(r.left()) || !std::isfinite(r.top()) ||
        !std::isfinite(r.right()) || !std::isfinite(r.bottom()))
      return;
    point(r.left(), r.top());
    point(r.right(), r.bottom());
  }
  QRectF value() const {
    return set ? QRectF(left, top, right - left, bottom - top) : QRectF();
  }
};

struct TextLayout {
  QVector<TimelineTextLine> lines;
  QRectF bounds;
};

// The font one <text> renders with. Upstream measures node heights with a
// transient classless probe text (root chain only) while the final node text
// also carries `.section-N text` rules — themeCSS can move the two apart.
struct TextFont {
  QString family;
  qreal size = 16.0;
  QFont::Weight weight = QFont::Normal;
};

TextFont styleFont(const TimelineSceneStyle& style);

TextFont cssTextFont(const TimelineElementCss& css,
                     const TimelineSceneStyle& style) {
  TextFont font = styleFont(style);
  if (!css.fontFamily.isEmpty()) font.family = css.fontFamily;
  if (css.fontSize >= 0.0) font.size = css.fontSize;
  if (!css.fontWeight.isEmpty())
    font.weight = editor::cssFontWeightToQt(QJsonValue(css.fontWeight),
                                            font.weight);
  font.weight = editor::faceAwareMetricWeight(font.family, font.weight);
  return font;
}

qreal svgLineCoordinate(qreal value) {
  // Invalid SVG length attributes retain their property's initial used value.
  // For x1/y1/x2/y2 that value is zero; unlike an invalid path, the line still
  // participates in getBBox().
  return std::isfinite(value) ? value : 0.0;
}

qreal jsMathMax(qreal a, qreal b) {
  if (std::isnan(a) || std::isnan(b))
    return std::numeric_limits<qreal>::quiet_NaN();
  return std::max(a, b);
}

void includeLine(Extents& extents, const TimelineLineGeometry& line) {
  extents.point(svgLineCoordinate(line.start.x()),
                svgLineCoordinate(line.start.y()));
  extents.point(svgLineCoordinate(line.end.x()),
                svgLineCoordinate(line.end.y()));
}

QStringList cssFontFamilies(const QString& expression) {
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

flowchart::FlowLabelDocument plainDocument(const QString& text,
                                           QFont::Weight weight) {
  flowchart::FlowLabelDocument document;
  document.text = text;
  document.baseWeight = weight;
  return document;
}

QString metricFamily(const QString& family, qreal size, QFont::Weight weight);

qreal svgTextAdvance(const QString& text, const TextFont& font) {
  if (text.isEmpty()) return 0.0;
  const flowchart::FlowLabelDocument document = plainDocument(text, font.weight);
  return flowchart::measureFlowTextAdvanceWidth(
      document, 0, document.text.size(), metricFamily(font.family, font.size,
                                                      font.weight),
      font.size);
}

QStringList splitWithDelimiters(const QString& text) {
  static const QRegularExpression separator(QStringLiteral(
      R"(([\x{0009}-\x{000d}\x{0020}\x{00a0}\x{1680}\x{2000}-\x{200a}\x{2028}\x{2029}\x{202f}\x{205f}\x{3000}\x{feff}]+|<br>))"));
  QStringList result;
  qsizetype start = 0;
  auto it = separator.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();
    result.append(text.mid(start, match.capturedStart() - start));
    result.append(match.captured());
    start = match.capturedEnd();
  }
  result.append(text.mid(start));
  return result;
}

editor::CssPixelFont nodeFont(const QString& family, qreal size,
                              QFont::Weight weight) {
  const QStringList families = cssFontFamilies(family);
  editor::CssPixelFont result = editor::makeUnhintedCssPixelFont(
      families.first(), size);
  if (families.size() > 1) result.font.setFamilies(families);
  result.font.setWeight(weight);
  return result;
}

TextFont styleFont(const TimelineSceneStyle& style) {
  return {style.fontFamily, style.fontSize,
          editor::faceAwareMetricWeight(style.fontFamily,
                                        style.nodeFontWeight)};
}

QRectF titleInkBounds(const editor::CssPixelFont& font, const QString& text,
                       QPointF baseline,
                       const flowchart::FlowLabelFontMetrics& vertical) {
  QRectF ink = QFontMetricsF(font.font).boundingRect(text);
  ink = QRectF(ink.x() * font.scale, ink.y() * font.scale,
               ink.width() * font.scale, ink.height() * font.scale);
  // SVG getBBox includes negative side-bearing and right overhang, but its
  // horizontal box never starts after the text anchor or ends before the
  // advance. QFontMetricsF::boundingRect alone omits those anchor/advance
  // edges for ordinary glyphs such as the leading P in the canonical title.
  const qreal left = std::min<qreal>(0.0, ink.left());
  const qreal right = std::max(font.horizontalAdvance(text), ink.right());
  return QRectF(baseline.x() + left, baseline.y() - vertical.ascent,
                right - left, vertical.height());
}

QString metricFamily(const QString& family, qreal size, QFont::Weight weight) {
  const editor::CssPixelFont font = nodeFont(family, size, weight);
  const QRawFont raw = QRawFont::fromFont(font.font);
  return raw.isValid() && !raw.familyName().isEmpty() ? raw.familyName()
                                                      : cssFontFamilies(family).first();
}

TextLayout layoutText(const QString& text, qreal wrapWidth,
                      const TextFont& font) {
  TextLayout result;
  if (!(font.size > 0.0)) return result;

  const flowchart::FlowLabelFontMetrics vertical =
      flowchart::flowLabelFontBoundingMetrics(
          metricFamily(font.family, font.size, font.weight), font.size,
          font.weight);
  const qreal xHeight = vertical.xHeight;

  QStringList active;
  QVector<QString> tspanTexts(1);
  const QStringList words = splitWithDelimiters(text);
  for (const QString& word : words) {
    active.append(word);
    const QString candidate = active.join(QLatin1Char(' ')).trimmed();
    tspanTexts.last() = candidate;
    if (svgTextAdvance(text::collapsedSvgText(candidate, false), font) > wrapWidth ||
        word == QStringLiteral("<br>")) {
      active.removeLast();
      tspanTexts.last() = active.join(QLatin1Char(' ')).trimmed();
      active = word == QStringLiteral("<br>") ? QStringList{QString()}
                                                : QStringList{word};
      tspanTexts.append(word);
    }
  }

  Extents textExtents;
  qreal y = font.size;
  for (qsizetype i = 0; i < tspanTexts.size(); ++i) {
    if (i > 0) y += font.size * 1.1;
    TimelineTextLine line;
    line.sourceText = tspanTexts.at(i);
    line.visibleText = text::collapsedSvgText(line.sourceText, false);
    const qreal advance = svgTextAdvance(line.visibleText, font);
    line.baseline = QPointF(0.0, y + xHeight / 2.0);
    if (!line.visibleText.isEmpty()) {
      const flowchart::FlowLabelDocument document =
          plainDocument(line.visibleText, font.weight);
      line.logicalBounds = flowchart::measureFlowSvgTextBounds(
          document, metricFamily(font.family, font.size, font.weight),
          font.size);
      // measureFlowSvgTextBounds models createFormattedText's start anchor;
      // Timeline sets text-anchor=middle, whose anchor is the advance center.
      line.logicalBounds.translate(-advance / 2.0,
                                   xHeight / 2.0 +
                                       qreal(i) * font.size * 1.1);
      textExtents.rect(line.logicalBounds);
    }
    result.lines.append(line);
  }
  result.bounds = textExtents.value();
  return result;
}

qreal virtualHeight(const QString& text, qreal width, qreal padding,
                    const TextFont& font, const TimelineSceneStyle& style) {
  const TextLayout layout = layoutText(text, width, font);
  return layout.bounds.height() + style.layoutFontSize * 1.1 * 0.5 + padding;
}

QString n(qreal value) { return editor::jsNumberToString(value); }

qreal finiteSvgNumber(double value) {
  return std::isfinite(value) ? value : 0.0;
}

qreal rawNumber(const QJsonValue& value) {
  return finiteSvgNumber(editor::jsNumberValue(value));
}

struct JsCoordinate {
  bool string = false;
  QString text;
  qreal number = 0.0;
};

JsCoordinate jsCoordinate(qreal value) {
  return {.string = false, .text = {}, .number = value};
}

JsCoordinate jsCoordinate(const QJsonValue& value) {
  if (value.isString())
    return {.string = true, .text = value.toString(), .number = 0.0};
  return jsCoordinate(editor::jsNumberValue(value));
}

QString jsCoordinateString(const JsCoordinate& value) {
  return value.string ? value.text : editor::jsNumberToString(value.number);
}

JsCoordinate jsAdd(JsCoordinate left, JsCoordinate right) {
  if (left.string || right.string)
    return {.string = true,
            .text = jsCoordinateString(left) + jsCoordinateString(right),
            .number = 0.0};
  return jsCoordinate(left.number + right.number);
}

JsCoordinate jsAdd(JsCoordinate left, qreal right) {
  return jsAdd(std::move(left), jsCoordinate(right));
}

JsCoordinate jsAdd(qreal left, const QJsonValue& right) {
  return jsAdd(jsCoordinate(left), jsCoordinate(right));
}

qreal jsCoordinateRawNumber(const JsCoordinate& value) {
  return value.string ? editor::jsNumberValue(QJsonValue(value.text))
                      : value.number;
}

qreal jsCoordinateNumber(const JsCoordinate& value) {
  return finiteSvgNumber(jsCoordinateRawNumber(value));
}

std::optional<qreal> svgCoordinate(const JsCoordinate& value) {
  const qreal number = jsCoordinateRawNumber(value);
  if (!std::isfinite(number)) return std::nullopt;
  return number;
}

QPointF svgTranslate(const JsCoordinate& x, qreal y) {
  const std::optional<qreal> number = svgCoordinate(x);
  return number && std::isfinite(y) ? QPointF(*number, y) : QPointF();
}

QPointF svgTranslate(qreal x, qreal y) {
  return std::isfinite(x) && std::isfinite(y) ? QPointF(x, y) : QPointF();
}

qreal svgAttributeNumber(const QJsonValue& value) {
  if (value.isDouble()) return finiteSvgNumber(value.toDouble());
  if (value.isString()) {
    bool ok = false;
    const qreal result = value.toString().toDouble(&ok);
    return ok ? finiteSvgNumber(result) : 0.0;
  }
  // Boolean/null/object/array values stringify to tokens that SVG number
  // attributes reject. leftMargin's null/object/array cases have already been
  // replaced by the config default at the adapter boundary.
  return 0.0;
}

QString nodePath(qreal width, qreal height, bool redux) {
  if (redux) {
    return QStringLiteral("M0 %1 v%2 h%3 v%4 H0 Z")
        .arg(n(height - 5.0), n(-height + 5.0), n(width), n(height));
  }
  return QStringLiteral("M0 %1 v%2 q0,-5,5,-5 h%3 q5,0,5,5 v%4 H0 Z")
      .arg(n(height - 5.0), n(-height + 10.0), n(width - 10.0),
           n(height - 5.0));
}

bool isRedux(const TimelineSceneStyle& style) {
  return style.themeName.contains(QStringLiteral("redux"), Qt::CaseSensitive);
}
bool isReduxDark(const TimelineSceneStyle& style) {
  return isRedux(style) &&
         style.themeName.contains(QStringLiteral("dark"), Qt::CaseSensitive);
}
bool isReduxColor(const TimelineSceneStyle& style) {
  return isRedux(style) &&
         style.themeName.contains(QStringLiteral("color"), Qt::CaseSensitive);
}
bool isNeo(const TimelineSceneStyle& style) {
  return style.look == QStringLiteral("neo");
}
bool isNeutral(const TimelineSceneStyle& style) {
  return style.themeName == QStringLiteral("neutral");
}

QString at(const QVector<QString>& values, int index) {
  return index >= 0 && index < values.size() ? values.at(index) : QString();
}

void resolveNodePaint(TimelineNodeGeometry& node,
                      const TimelineSceneStyle& style) {
  const qreal limit = style.rawThemeColorLimit;
  node.sectionClassValue =
      std::isfinite(limit) && limit != 0.0
          ? std::fmod(qreal(node.fullSection), limit) - 1.0
          : std::numeric_limits<qreal>::quiet_NaN();
  node.sectionClass = QStringLiteral("section-") +
                      editor::jsNumberToString(node.sectionClassValue);
  node.paletteIndex = -1;
  if (std::isfinite(node.sectionClassValue) &&
      std::floor(node.sectionClassValue) == node.sectionClassValue) {
    const int candidate = int(node.sectionClassValue) + 1;
    if (candidate >= 0 && candidate < style.themeColorRuleCount)
      node.paletteIndex = candidate;
  }

  if (node.paletteIndex < 0) {
    node.fill = style.textColor;
    node.stroke = QStringLiteral("none");
    node.textFill = style.textColor;
    node.dividerVisible = false;
    node.strokeWidth = isRedux(style) ? 1.0 : 0.0;
    node.dividerWidth = 0.0;
    return;
  }

  if (isRedux(style)) {
    if (isReduxColor(style) && node.paletteIndex >= 0) {
      node.stroke = at(style.borderColorArray, node.paletteIndex);
      node.fill = isReduxDark(style) ? style.mainBkg
                                     : at(style.borderColorArray, node.paletteIndex);
    } else {
      node.stroke = style.nodeBorder;
      node.fill = style.mainBkg;
    }
    node.textFill = style.nodeBorder;
    node.strokeWidth = style.strokeWidth;
    node.dividerVisible = false;
  } else if (node.paletteIndex >= 0) {
    node.fill = at(style.cScale, node.paletteIndex);
    node.textFill = at(style.cScaleLabel, node.paletteIndex);
    node.dividerStroke = at(style.cScaleInv, node.paletteIndex);
    node.stroke = QStringLiteral("none");
    node.strokeWidth = 1.0;
    node.dividerWidth = 3.0;
  } else {
    node.fill = style.textColor;
    node.textFill = style.textColor;
    node.dividerStroke = style.textColor;
    node.stroke = QStringLiteral("none");
    node.strokeWidth = 1.0;
    node.dividerWidth = 3.0;
  }

  node.gradientStroke = isNeo(style) && style.useGradient && !isNeutral(style) &&
                        node.paletteIndex >= 0;
  if (node.gradientStroke) {
    node.fill = style.mainBkg;
    node.strokeWidth = 2.0;
    node.dividerStroke.clear();
  }
  // Redux styles attach the drop-shadow filter in both classic and neo looks.
  node.dropShadow = isRedux(style);
}

TimelineNodeGeometry makeNode(TimelineNodeKind kind, const QString& text,
                              const QString& section, int sectionNumber,
                              qreal baseWidth, qreal padding, qreal maxHeight,
                              QPointF position, const TimelineSceneStyle& style,
                              const TextFont& drawnFont,
                              const TimelineCssOverrides::Node* nodeCss,
                              int paintOrder) {
  TimelineNodeGeometry node;
  node.kind = kind;
  node.text = text;
  node.section = section;
  node.sectionNumber = sectionNumber;
  node.fullSection = sectionNumber;
  node.position = position;
  node.rounded = !isRedux(style);
  node.dividerVisible = !isRedux(style);
  node.eventBrightness = kind == TimelineNodeKind::Event;
  node.paintOrder = paintOrder;
  if (nodeCss) {
    node.boxCss = nodeCss->box;
    node.dividerCss = nodeCss->divider;
    node.textCss = nodeCss->text;
  }

  // drawNode re-measures the node with its final text element, whose font can
  // differ from the classless measurement probe under themeCSS.
  const TextLayout textLayout = layoutText(text, baseWidth, drawnFont);
  node.width = baseWidth + 2.0 * padding;
  node.height = jsMathMax(textLayout.bounds.height() +
                              style.layoutFontSize * 1.1 * 0.5 + padding,
                          maxHeight);
  node.pathData = nodePath(node.width, node.height, isRedux(style));
  const qreal textY = isRedux(style)
                          ? (kind == TimelineNodeKind::Event ? padding / 2.0 + 3.0
                                                             : padding)
                          : padding / 2.0;
  node.textOffset = QPointF(node.width / 2.0, textY);
  node.textLines = textLayout.lines;
  node.textBounds = textLayout.bounds.translated(node.position + node.textOffset);
  resolveNodePaint(node, style);
  return node;
}

void includeNode(Extents& extents, const TimelineNodeGeometry& node) {
  // Mermaid measures inside its detached render container, whose getBBox()
  // path never filters by display: a display:none box still contributes to
  // the pre-axis box, the viewBox and therefore every derived coordinate.
  // themeCSS display/visibility only gates painting.
  if (std::isfinite(node.position.x()) &&
      std::isfinite(node.position.y()) && std::isfinite(node.width) &&
      std::isfinite(node.height))
    extents.rect(QRectF(node.position, QSizeF(node.width, node.height)));
  else if (std::isfinite(node.position.x()) &&
           std::isfinite(node.position.y()) && std::isfinite(node.width)) {
    // The rough path contains later finite top-edge commands even when its
    // height interpolation serializes as NaN. Chromium retains that horizontal
    // segment in the path bbox while dropping every NaN-dependent segment.
    extents.point(node.position.x(), node.position.y());
    extents.point(node.position.x() + node.width, node.position.y());
  }
  if (!node.textBounds.isNull()) extents.rect(node.textBounds);
}

QString lineColor(const TimelineSceneStyle& style) {
  if (isRedux(style)) return style.nodeBorder;
  if (style.themeColorRuleCount > 0)
    return at(style.cScaleLabel, style.themeColorRuleCount - 1);
  // No generated stylesheet rule: the SVG presentation stroke="black" wins.
  return QString();
}

std::optional<qreal> resolvedStrokeWidthCss(const QString& raw,
                                            const CssLengthContext& context,
                                            qreal diagonal) {
  const QString value = raw.trimmed();
  const QString lower = value.toLower();
  if (lower == QLatin1String("inherit") || lower == QLatin1String("initial") ||
      lower == QLatin1String("unset") || lower == QLatin1String("revert"))
    return 1.0;
  if (value.endsWith(QLatin1Char('%'))) {
    bool ok = false;
    const qreal number = value.left(value.size() - 1).toDouble(&ok);
    if (ok && std::isfinite(number) && number >= 0.0)
      return editor::cssStrokeWidthPx(value, context, diagonal);
    return std::nullopt;
  }
  const CssLengthResult result = resolveCssLengthToPx(value, context);
  if (result.status != CssLengthStatus::Valid || !std::isfinite(result.px) ||
      result.px < 0.0)
    return std::nullopt;
  return editor::cssStrokeWidthPx(value, context, diagonal);
}

void resolveReduxStrokeWidths(TimelineScene& scene) {
  if (!isRedux(scene.style)) return;
  const TextFont base = styleFont(scene.style);
  CssLengthContext context = editor::pieCssLengthContext(
      metricFamily(base.family, base.size, base.weight),
      scene.style.fontSize);
  context.viewportPx = scene.bounds.size();
  const qreal diagonal =
      std::hypot(scene.bounds.width(), scene.bounds.height()) / std::sqrt(2.0);
  const std::optional<qreal> used = resolvedStrokeWidthCss(
      scene.style.strokeWidthCss, context, diagonal);
  const bool valid = used.has_value();
  const qreal resolved = used.value_or(1.0);
  scene.style.strokeWidth = resolved;
  for (TimelineNodeGeometry& node : scene.nodes)
    node.strokeWidth = valid ? resolved : 1.0;
  for (TimelineLineGeometry& line : scene.lines)
    line.strokeWidth = valid ? resolved : (line.axis ? 4.0 : 2.0);
}

QJsonArray pointJson(const QPointF& point) { return {point.x(), point.y()}; }
QJsonArray rectJson(const QRectF& rect) {
  return {rect.x(), rect.y(), rect.width(), rect.height()};
}

}  // namespace

TimelineScene buildTimelineScene(const TimelineData& data, TimelineConfig config,
                                 TimelineSceneStyle style,
                                 const TimelineCssOverrides* css) {
  TimelineScene scene;
  scene.direction = data.direction;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;
  scene.config = std::move(config);
  scene.style = std::move(style);
  scene.markerDefinitionId = data.direction == TimelineDirection::TopDown
                                 ? QStringLiteral("undefined-arrowhead")
                                 : QStringLiteral("arrowhead");

  // Heights come from the classless measurement probe, whose font follows the
  // root cascade (and any `text {}` rules) under themeCSS.
  const TextFont measureFont =
      css ? cssTextFont(css->measureText, scene.style) : styleFont(scene.style);
  auto nodeCssAt = [&css](qsizetype index) {
    return css && index < css->nodes.size() ? &css->nodes.at(index) : nullptr;
  };
  qsizetype nodeIndex = 0;

  Extents content;
  int order = 0;
  qreal maxSectionHeight = 0.0;
  qreal maxTaskHeight = 0.0;
  for (const QString& section : data.sections) {
    const qreal baseWidth = data.direction == TimelineDirection::LeftToRight
                                ? 150.0
                                : 580.0;
    const qreal padding = data.direction == TimelineDirection::LeftToRight
                              ? 20.0
                              : 5.0;
    const qreal h =
        virtualHeight(section, baseWidth, padding, measureFont, scene.style);
    maxSectionHeight = jsMathMax(
        maxSectionHeight,
        h + (data.direction == TimelineDirection::LeftToRight ? 20.0 : 0.0));
  }

  qreal maxEventStackHeight = 0.0;
  for (const TimelineTask& task : data.tasks) {
    const qreal baseWidth = data.direction == TimelineDirection::LeftToRight
                                ? 150.0
                                : 200.0;
    const qreal padding = data.direction == TimelineDirection::LeftToRight
                              ? 20.0
                              : 5.0;
    // Upstream passes the task object to d3-selection.text() during this
    // pre-measure pass, so every task is measured as "[object Object]".
    const qreal taskH =
        virtualHeight(QStringLiteral("[object Object]"), baseWidth, padding,
                      measureFont, scene.style);
    maxTaskHeight = jsMathMax(
        maxTaskHeight,
        taskH + (data.direction == TimelineDirection::LeftToRight ? 20.0
                                                                   : 0.0));
    qreal stack = 0.0;
    for (const QString& event : task.events) {
      stack += virtualHeight(event,
                             data.direction == TimelineDirection::LeftToRight
                                 ? 150.0
                                 : 300.0,
                             padding, measureFont, scene.style);
    }
    if (!task.events.isEmpty()) stack += (task.events.size() - 1) * 10.0;
    maxEventStackHeight = jsMathMax(maxEventStackHeight, stack);
  }

  const bool hasSections = !data.sections.isEmpty();
  const QString connectorColor = lineColor(scene.style);
  qsizetype connectorIndex = 0;
  const auto connectorCssAt = [&css, &connectorIndex]() {
    return css && connectorIndex < css->connectors.size()
               ? css->connectors.at(connectorIndex++)
               : TimelineElementCss{};
  };
  auto appendNode = [&](TimelineNodeKind kind, const QString& text,
                        const QString& section, int sectionNumber,
                        qreal baseWidth, qreal padding, qreal maxHeight,
                        QPointF position) {
    const TimelineCssOverrides::Node* nodeCss = nodeCssAt(nodeIndex);
    const TextFont drawnFont = nodeCss ? cssTextFont(nodeCss->text, scene.style)
                                       : styleFont(scene.style);
    TimelineNodeGeometry node =
        makeNode(kind, text, section, sectionNumber, baseWidth, padding,
                 maxHeight, position, scene.style, drawnFont, nodeCss, order++);
    scene.nodes.append(node);
    includeNode(content, node);
    ++nodeIndex;
    return node;
  };
  if (data.direction == TimelineDirection::LeftToRight) {
    const qreal left = rawNumber(scene.config.leftMarginRaw);
    JsCoordinate masterX = jsAdd(50.0, scene.config.leftMarginRaw);
    const qreal sectionY = 50.0;

    auto drawTasks = [&](const QVector<TimelineTask>& tasks, int sectionNumber,
                         JsCoordinate startX, bool withoutSections) {
      JsCoordinate x = std::move(startX);
      qreal localMaxTask = maxTaskHeight;
      const qreal taskY = hasSections ? 100.0 + maxSectionHeight : 50.0;
      for (const TimelineTask& task : tasks) {
        const QPointF nodePosition = svgTranslate(x, taskY);
        const TimelineNodeGeometry taskNode =
            appendNode(TimelineNodeKind::Task, task.task, task.section,
                       sectionNumber, 150.0, 20.0, localMaxTask, nodePosition);
        localMaxTask = jsMathMax(localMaxTask, taskNode.height);

        TimelineLineGeometry connector;
        const qreal connectorX = jsCoordinateNumber(jsAdd(x, 95.0));
        connector.start = QPointF(connectorX, taskY + localMaxTask);
        connector.end = QPointF(connectorX,
                               taskY + localMaxTask + 200.0 + maxEventStackHeight);
        connector.stroke = connectorColor;
        connector.strokeWidth = isRedux(scene.style) ? scene.style.strokeWidth : 2.0;
        connector.dashPattern = {5.0, 5.0};
        connector.css = connectorCssAt();
        connector.paintOrder = order++;
        scene.lines.append(connector);
        includeLine(content, connector);

        qreal eventY = taskY + 200.0;
        for (const QString& event : task.events) {
          const TimelineNodeGeometry eventNode =
              appendNode(TimelineNodeKind::Event, event, task.section,
                         sectionNumber, 150.0, 20.0, 50.0,
                         svgTranslate(x, eventY));
          eventY += eventNode.height + 10.0;
        }
        x = jsAdd(std::move(x), 200.0);
        if (withoutSections && !scene.config.disableMulticolor) ++sectionNumber;
      }
    };

    int sectionNumber = 0;
    if (hasSections) {
      for (const QString& section : data.sections) {
        QVector<TimelineTask> selected;
        for (const TimelineTask& task : data.tasks)
          if (task.section == section) selected.append(task);
        const qreal baseWidth = 200.0 * std::max<qsizetype>(selected.size(), 1) - 50.0;
        appendNode(TimelineNodeKind::Section, section, section, sectionNumber,
                   baseWidth, 20.0, maxSectionHeight,
                   svgTranslate(masterX, sectionY));
        if (!selected.isEmpty()) drawTasks(selected, sectionNumber, masterX, false);
        masterX = jsAdd(std::move(masterX),
                        200.0 * std::max<qsizetype>(selected.size(), 1));
        ++sectionNumber;
      }
    } else {
      drawTasks(data.tasks, 0, masterX, true);
    }

    scene.preTitleBounds = content.value();
    const QRectF box = scene.preTitleBounds;
    if (!scene.title.isEmpty() && scene.style.fontSize > 0.0) {
      const TimelineElementCss titleCss =
          css ? css->title : TimelineElementCss{};
      const TextFont baseFont = styleFont(scene.style);
      const flowchart::FlowLabelFontMetrics rootMetrics =
          flowchart::flowLabelFontBoundingMetrics(
              metricFamily(baseFont.family, baseFont.size, baseFont.weight),
              baseFont.size, baseFont.weight);
      const qreal titleSize = titleCss.fontSize >= 0.0
                                  ? titleCss.fontSize
                                  : std::min(rootMetrics.xHeight * 4.0, 10000.0);
      const QString titleFamily = titleCss.fontFamily.isEmpty()
                                      ? scene.style.fontFamily
                                      : titleCss.fontFamily;
      const QFont::Weight titleWeight = editor::faceAwareMetricWeight(
          titleCss.fontFamily.isEmpty() ? scene.style.fontFamily
                                        : titleCss.fontFamily,
          !titleCss.fontWeight.isEmpty()
              ? editor::cssFontWeightToQt(QJsonValue(titleCss.fontWeight),
                                          QFont::Bold)
              : QFont::Bold);
      const editor::CssPixelFont titleFont =
          nodeFont(titleFamily, titleSize, titleWeight);
      const flowchart::FlowLabelFontMetrics titleMetrics =
          flowchart::flowLabelFontBoundingMetrics(
              metricFamily(titleFamily, titleSize, titleWeight), titleSize,
              titleWeight);
      const qreal titleX = isNeo(scene.style)
                               ? jsCoordinateNumber(jsAdd(
                                     box.x() * 2.0,
                                     scene.config.leftMarginRaw))
                                              : box.width() / 2.0 - left;
      scene.titleGeometry.visible = true;
      scene.titleGeometry.text = text::collapsedSvgText(scene.title);
      scene.titleGeometry.baseline = QPointF(titleX, 20.0);
      scene.titleGeometry.fontSize = titleSize;
      scene.titleGeometry.fill = scene.style.textColor;
      scene.titleGeometry.css = titleCss;
      scene.titleGeometry.paintOrder = order++;
      scene.titleGeometry.logicalBounds = titleInkBounds(
          titleFont, scene.titleGeometry.text, scene.titleGeometry.baseline,
          titleMetrics);
      content.rect(scene.titleGeometry.logicalBounds);
    }

    const qreal depth = hasSections ? maxSectionHeight + maxTaskHeight + 150.0
                                    : maxTaskHeight + 100.0;
    TimelineLineGeometry axis;
    axis.start = QPointF(svgAttributeNumber(scene.config.leftMarginRaw), depth);
    axis.end = QPointF(box.width() + 3.0 * left, depth);
    axis.stroke = connectorColor;
    axis.strokeWidth = isRedux(scene.style) ? scene.style.strokeWidth : 4.0;
    axis.axis = true;
    axis.css = css ? css->axis : TimelineElementCss{};
    axis.paintOrder = order++;
    scene.lines.append(axis);
    includeLine(content, axis);
  } else {
    constexpr qreal nodeWidth = 200.0;
    constexpr qreal nodePadding = 5.0;
    constexpr qreal nodeTotalWidth = 210.0;
    constexpr qreal eventWidth = 300.0;
    constexpr qreal eventAxisGap = 50.0;
    constexpr qreal taskAxisGap = 20.0;
    constexpr qreal taskVerticalGap = 30.0;
    const qreal left = rawNumber(scene.config.leftMarginRaw);
    const JsCoordinate masterX = jsAdd(50.0, scene.config.leftMarginRaw);
    qreal masterY = 50.0;
    const qreal contentTopY = masterY;
    const JsCoordinate timelineX =
        jsAdd(jsAdd(masterX, nodeTotalWidth), taskAxisGap);
    const qreal timelineNumber = jsCoordinateNumber(timelineX);
    const qreal sectionWidth = 580.0;
    const qreal taskSpacing = jsMathMax(maxTaskHeight, maxEventStackHeight) + 30.0;

    // TD lowers the axis group beneath all content. Reserve its order now.
    TimelineLineGeometry axis;
    axis.start = QPointF(timelineNumber, 0.0);
    axis.end = axis.start;
    axis.stroke = connectorColor;
    axis.strokeWidth = isRedux(scene.style) ? scene.style.strokeWidth : 4.0;
    axis.axis = true;
    axis.markerResolved = false;  // references #arrowhead; init created #undefined-arrowhead
    axis.css = css ? css->axis : TimelineElementCss{};
    axis.paintOrder = -100000;

    auto drawTasks = [&](const QVector<TimelineTask>& tasks, int sectionNumber,
                         qreal startY, bool withoutSections) {
      qreal y = startY;
      qreal localMaxTask = maxTaskHeight;
      for (const TimelineTask& task : tasks) {
        const TimelineNodeGeometry taskNode =
            appendNode(TimelineNodeKind::Task, task.task, task.section,
                       sectionNumber, nodeWidth, nodePadding, localMaxTask,
                       svgTranslate(jsCoordinateRawNumber(timelineX) -
                                        taskAxisGap - nodeTotalWidth,
                                    y));
        localMaxTask = jsMathMax(localMaxTask, taskNode.height);

        qreal eventY = y;
        for (const QString& event : task.events) {
          const TimelineNodeGeometry eventNode =
              appendNode(TimelineNodeKind::Event, event, task.section,
                         sectionNumber, eventWidth, nodePadding, 0.0,
                         svgTranslate(jsAdd(timelineX, eventAxisGap), eventY));
          TimelineLineGeometry connector;
          connector.start = QPointF(timelineNumber,
                                    eventY + eventNode.height / 2.0);
          connector.end = QPointF(jsCoordinateNumber(
                                      jsAdd(timelineX, eventAxisGap)),
                                  eventY + eventNode.height / 2.0);
          connector.stroke = connectorColor;
          connector.strokeWidth =
              isRedux(scene.style) ? scene.style.strokeWidth : 2.0;
          connector.dashPattern = {5.0, 5.0};
          connector.markerResolved = false;
          connector.css = connectorCssAt();
          connector.paintOrder = order++;
          scene.lines.append(connector);
          includeLine(content, connector);
          eventY += eventNode.height + 10.0;
        }
        y += taskSpacing;
        if (withoutSections && !scene.config.disableMulticolor) ++sectionNumber;
      }
    };

    int sectionNumber = 0;
    if (hasSections) {
      for (const QString& section : data.sections) {
        QVector<TimelineTask> selected;
        for (const TimelineTask& task : data.tasks)
          if (task.section == section) selected.append(task);
        const TimelineNodeGeometry sectionNode =
            appendNode(TimelineNodeKind::Section, section, section,
                       sectionNumber, sectionWidth, nodePadding,
                       maxSectionHeight,
                       svgTranslate(jsCoordinateRawNumber(timelineX) -
                                        nodeTotalWidth - taskAxisGap,
                                    masterY));
        const qreal taskStartY = masterY + sectionNode.height + 20.0;
        if (!selected.isEmpty()) drawTasks(selected, sectionNumber, taskStartY, false);
        const qsizetype count = selected.size();
        masterY += sectionNode.height + 20.0 +
                   taskSpacing * std::max<qsizetype>(count, 1) -
                   (count > 0 ? taskVerticalGap * 2.0 : 0.0);
        ++sectionNumber;
      }
    } else {
      drawTasks(data.tasks, 0, masterY, true);
    }

    scene.preTitleBounds = content.value();
    QRectF box = scene.preTitleBounds;
    if (!scene.title.isEmpty() && scene.style.fontSize > 0.0) {
      const TimelineElementCss titleCss =
          css ? css->title : TimelineElementCss{};
      const TextFont baseFont = styleFont(scene.style);
      const flowchart::FlowLabelFontMetrics rootMetrics =
          flowchart::flowLabelFontBoundingMetrics(
              metricFamily(baseFont.family, baseFont.size, baseFont.weight),
              baseFont.size, baseFont.weight);
      const qreal titleSize = titleCss.fontSize >= 0.0
                                  ? titleCss.fontSize
                                  : std::min(rootMetrics.xHeight * 4.0, 10000.0);
      const QString titleFamily = titleCss.fontFamily.isEmpty()
                                      ? scene.style.fontFamily
                                      : titleCss.fontFamily;
      const QFont::Weight titleWeight = editor::faceAwareMetricWeight(
          titleCss.fontFamily.isEmpty() ? scene.style.fontFamily
                                        : titleCss.fontFamily,
          !titleCss.fontWeight.isEmpty()
              ? editor::cssFontWeightToQt(QJsonValue(titleCss.fontWeight),
                                          QFont::Bold)
              : QFont::Bold);
      const editor::CssPixelFont titleFont =
          nodeFont(titleFamily, titleSize, titleWeight);
      const flowchart::FlowLabelFontMetrics titleMetrics =
          flowchart::flowLabelFontBoundingMetrics(
              metricFamily(titleFamily, titleSize, titleWeight), titleSize,
              titleWeight);
      const qreal titleX = box.width() / 2.0 - left;
      scene.titleGeometry.visible = true;
      scene.titleGeometry.text = text::collapsedSvgText(scene.title);
      scene.titleGeometry.baseline = QPointF(titleX, 20.0);
      scene.titleGeometry.fontSize = titleSize;
      scene.titleGeometry.fill = scene.style.textColor;
      scene.titleGeometry.css = titleCss;
      scene.titleGeometry.paintOrder = order++;
      scene.titleGeometry.logicalBounds = titleInkBounds(
          titleFont, scene.titleGeometry.text, scene.titleGeometry.baseline,
          titleMetrics);
      content.rect(scene.titleGeometry.logicalBounds);
      box = content.value();
    }
    axis.start =
        QPointF(timelineNumber,
                contentTopY - scene.style.layoutFontSize * 2.0);
    axis.end = QPointF(timelineNumber,
                       box.y() + box.height() +
                           scene.style.layoutFontSize * 0.5 + 20.0);
    scene.lines.append(axis);
    includeLine(content, axis);
  }

  scene.contentBounds = content.value();
  scene.bounds = scene.config.invalidPadding
                     ? QRectF(0.0, 0.0, 784.0, 150.0)
                     : scene.contentBounds.adjusted(-scene.config.padding,
                                                    -scene.config.padding,
                                                    scene.config.padding,
                                                    scene.config.padding);
  resolveReduxStrokeWidths(scene);
  return scene;
}

qreal timelineTitleFontSizePx(const TimelineSceneStyle& style) {
  if (!(style.fontSize > 0.0)) return 0.0;
  const TextFont base = styleFont(style);
  const flowchart::FlowLabelFontMetrics metrics =
      flowchart::flowLabelFontBoundingMetrics(
          metricFamily(base.family, base.size, base.weight), base.size,
          base.weight);
  return std::min(metrics.xHeight * 4.0, 10000.0);
}

QJsonObject TimelineScene::toJsonObject() const {
  QJsonObject root;
  root[QStringLiteral("bounds")] = rectJson(bounds);
  root[QStringLiteral("contentBounds")] = rectJson(contentBounds);
  root[QStringLiteral("preTitleBounds")] = rectJson(preTitleBounds);
  root[QStringLiteral("direction")] =
      direction == TimelineDirection::LeftToRight ? QStringLiteral("LR")
                                                   : QStringLiteral("TD");
  root[QStringLiteral("title")] = title;
  root[QStringLiteral("markerDefinitionId")] = markerDefinitionId;

  QJsonArray nodeArray;
  for (const TimelineNodeGeometry& node : nodes) {
    QJsonObject value;
    value[QStringLiteral("kind")] =
        node.kind == TimelineNodeKind::Section
            ? QStringLiteral("section")
            : node.kind == TimelineNodeKind::Event ? QStringLiteral("event")
                                                    : QStringLiteral("task");
    value[QStringLiteral("text")] = node.text;
    value[QStringLiteral("position")] = pointJson(node.position);
    value[QStringLiteral("size")] = QJsonArray{node.width, node.height};
    value[QStringLiteral("textBounds")] = rectJson(node.textBounds);
    value[QStringLiteral("path")] = node.pathData;
    value[QStringLiteral("sectionClass")] = node.sectionClass;
    value[QStringLiteral("paletteIndex")] = node.paletteIndex;
    value[QStringLiteral("fill")] = node.fill;
    value[QStringLiteral("stroke")] = node.stroke;
    value[QStringLiteral("textFill")] = node.textFill;
    value[QStringLiteral("paintOrder")] = node.paintOrder;
    QJsonArray linesJson;
    for (const TimelineTextLine& line : node.textLines) {
      QJsonObject lineValue;
      lineValue[QStringLiteral("source")] = line.sourceText;
      lineValue[QStringLiteral("visible")] = line.visibleText;
      lineValue[QStringLiteral("baseline")] = pointJson(line.baseline);
      linesJson.append(lineValue);
    }
    value[QStringLiteral("textLines")] = linesJson;
    nodeArray.append(value);
  }
  root[QStringLiteral("nodes")] = nodeArray;

  QJsonArray lineArray;
  for (const TimelineLineGeometry& line : lines) {
    QJsonObject value;
    value[QStringLiteral("start")] = pointJson(line.start);
    value[QStringLiteral("end")] = pointJson(line.end);
    value[QStringLiteral("stroke")] = line.stroke;
    value[QStringLiteral("strokeWidth")] = line.strokeWidth;
    value[QStringLiteral("markerResolved")] = line.markerResolved;
    value[QStringLiteral("axis")] = line.axis;
    value[QStringLiteral("paintOrder")] = line.paintOrder;
    lineArray.append(value);
  }
  root[QStringLiteral("lines")] = lineArray;
  return root;
}

}  // namespace muffin::mermaid::timeline
