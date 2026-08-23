#include "mermaid/cynefin/CynefinScene.h"

#include "mermaid/cynefin/CynefinScenePainter.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/text/ChromiumTextMetrics.h"
#include "mermaid/text/LabelText.h"

#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace muffin::mermaid::cynefin {
namespace {

struct Layout {
  qreal cx = 0.0;
  qreal cy = 0.0;
  qreal x = 0.0;
  qreal y = 0.0;
  qreal w = 0.0;
  qreal h = 0.0;
};

struct JsAddResult {
  QString text;
  double number = 0.0;
};

double jsNumber(const QJsonValue &value, double fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  return editor::jsNumberValue(value);
}

QString primitiveString(const QJsonValue &value, const QString &fallback) {
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

JsAddResult addPadding(const QJsonValue &dimension, double fallback,
                       double twicePadding) {
  if (dimension.isString()) {
    const QString text = dimension.toString() + editor::jsNumberToString(twicePadding);
    return {text, editor::jsNumberValue(QJsonValue(text))};
  }
  const double number = jsNumber(dimension, fallback) + twicePadding;
  return {editor::jsNumberToString(number), number};
}

bool truthy(const QJsonValue &value, bool fallback) {
  if (value.isUndefined() || value.isNull())
    return fallback;
  return editor::truthyConfigValue(value);
}

bool svgNumber(const QJsonValue &value, double fallback, double &result) {
  if (value.isUndefined() || value.isNull()) {
    result = fallback;
    return true;
  }
  if (value.isDouble()) {
    result = value.toDouble();
    return std::isfinite(result);
  }
  if (value.isString()) {
    bool ok = false;
    result = value.toString().toDouble(&ok);
    return ok && std::isfinite(result);
  }
  return false;
}

QString cssValue(const QJsonValue &value) {
  return primitiveString(value, QString());
}

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

qreal fontSize(const QJsonValue &value, qreal inherited) {
  const CssLengthContext root =
      editor::pieCssLengthContext(QStringLiteral("Noto Sans"), inherited);
  return editor::cssFontSizePx(cssValue(value) + QStringLiteral("px"), root);
}

qreal derivedFontSize(const QJsonValue &value, qreal delta, qreal inherited,
                      bool addition) {
  QString expression;
  if (addition && value.isString())
    expression = value.toString() + editor::jsNumberToString(delta);
  else
    expression = editor::jsNumberToString(jsNumber(value, 0.0) + delta);
  const CssLengthContext root =
      editor::pieCssLengthContext(QStringLiteral("Noto Sans"), inherited);
  return editor::cssFontSizePx(expression + QStringLiteral("px"), root);
}

editor::CssPixelFont textFont(const CynefinSceneStyle &style, qreal size,
                              bool bold, bool italic) {
  const QStringList families = cssFontFamilies(style.fontFamily);
  auto result = editor::makeUnhintedCssPixelFont(families.first(), size);
  if (families.size() > 1) result.font.setFamilies(families);
  result.font.setWeight(bold ? QFont::Bold : QFont::Normal);
  result.font.setItalic(italic);
  return result;
}

qreal textAdvance(const CynefinSceneStyle &style, const QString &source,
                  qreal size, bool bold = false, bool italic = false) {
  if (!(size > 0.0)) return 0.0;
  const QString text = text::collapsedSvgText(source);
  const auto font = textFont(style, size, bold, italic);
  const qreal qt = QFontMetricsF(font.font).horizontalAdvance(text) * font.scale;
  qreal shaped = textmetrics::harfBuzzAdvance(text, style.fontFamily, size)
                     .value_or(qt);
  if (bold)
    shaped += text.size() * size / 60.0;
  return shaped;
}

QRectF textBounds(const CynefinSceneStyle &style, const QString &source,
                  const QPointF &position, qreal size, const QString &anchor,
                  CynefinTextBaseline baseline, bool bold = false,
                  bool italic = false) {
  if (source.isEmpty() || !(size > 0.0)) return {};
  const QString text = text::collapsedSvgText(source);
  flowchart::FlowLabelDocument document;
  document.text = text;
  if (bold || italic) {
    QTextCharFormat format;
    if (bold) format.setFontWeight(QFont::Bold);
    if (italic) format.setFontItalic(true);
    document.formats.append({0, int(text.size()), format});
  }
  const qreal advance = flowchart::measureFlowTextAdvanceWidth(
      document, 0, text.size(), style.fontFamily, size);
  const QRectF ink = bold || italic
      ? flowchart::measureFlowSvgTextBounds(document, style.fontFamily, size)
      : flowchart::measureChromiumSvgTextBounds(
            document, style.fontFamily, size, QFont::Normal);
  qreal originX = std::isfinite(position.x()) ? position.x() : 0.0;
  if (anchor == QLatin1String("middle")) originX -= advance / 2.0;
  else if (anchor == QLatin1String("end")) originX -= advance;
  const auto metrics = flowchart::flowLabelFontBoundingMetrics(
      style.fontFamily, size, bold ? QFont::Bold : QFont::Normal,
      italic ? QFont::StyleItalic : QFont::StyleNormal);
  qreal baselineY = std::isfinite(position.y()) ? position.y() : 0.0;
  if (baseline == CynefinTextBaseline::Middle ||
      baseline == CynefinTextBaseline::Central)
    baselineY += metrics.xHeight / 2.0;
  return QRectF(originX + ink.x(), baselineY - metrics.ascent,
                ink.width(), metrics.height());
}

uint32_t imul(uint32_t a, uint32_t b) {
  return static_cast<uint32_t>(static_cast<uint64_t>(a) * b);
}

double seededRandom(double seed) {
  uint32_t t = static_cast<uint32_t>(static_cast<int64_t>(std::trunc(seed + 1831565813.0)));
  t = imul(t ^ (t >> 15), t | 1u);
  t ^= t + imul(t ^ (t >> 7), t | 61u);
  return static_cast<double>((t ^ (t >> 14))) / 4294967296.0;
}

int32_t hashString(const QString &value) {
  uint32_t hash = 0;
  for (QChar c : value)
    hash = (hash << 5) - hash + c.unicode();
  return static_cast<int32_t>(hash);
}

double resolvedSeed(const QJsonValue &raw, const QString &id) {
  if (raw.isDouble() && std::isfinite(raw.toDouble()) && raw.toDouble() != 0.0)
    return raw.toDouble();
  return hashString(id);
}

QString number(double value) { return editor::jsNumberToString(value); }

CynefinPathGeometry foldPath(double width, double height, double seed,
                             double amplitude) {
  struct Point { double x; double y; };
  QVector<Point> points;
  const double cx = width / 2.0;
  const double segmentHeight = height / 7.0;
  for (int i = 0; i <= 7; ++i) {
    const double jitter = seededRandom(seed + i * 17.0) * amplitude * 2.0 - amplitude;
    points.append({cx + jitter, i * segmentHeight});
  }
  CynefinPathGeometry result;
  result.role = QStringLiteral("boundary");
  result.path.moveTo(points[0].x, points[0].y);
  result.pathData = QStringLiteral("M%1,%2").arg(number(points[0].x), number(points[0].y));
  for (int i = 0; i < 7; ++i) {
    const auto p0 = points[i];
    const auto p1 = points[i + 1];
    const double middle = (p0.y + p1.y) / 2.0;
    const double direction = i % 2 == 0 ? 1.0 : -1.0;
    const double offset = amplitude * 1.5 * direction *
                          seededRandom(seed + i * 31.0 + 7.0);
    const QPointF c1(p0.x + offset, middle);
    const QPointF c2(p1.x - offset, middle);
    result.path.cubicTo(c1, c2, QPointF(p1.x, p1.y));
    result.pathData += QStringLiteral(" C%1,%2 %3,%4 %5,%6")
        .arg(number(c1.x()), number(c1.y()), number(c2.x()), number(c2.y()),
             number(p1.x), number(p1.y));
  }
  return result;
}

CynefinPathGeometry horizontalPath(double width, double height, double seed,
                                   double amplitude) {
  struct Point { double x; double y; };
  QVector<Point> points;
  const double cy = height / 2.0;
  const double segmentWidth = width / 7.0;
  for (int i = 0; i <= 7; ++i) {
    const double jitter = seededRandom(seed + i * 23.0) * amplitude * 2.0 - amplitude;
    points.append({i * segmentWidth, cy + jitter});
  }
  CynefinPathGeometry result;
  result.role = QStringLiteral("boundary");
  result.path.moveTo(points[0].x, points[0].y);
  result.pathData = QStringLiteral("M%1,%2").arg(number(points[0].x), number(points[0].y));
  for (int i = 0; i < 7; ++i) {
    const auto p0 = points[i];
    const auto p1 = points[i + 1];
    const double middle = (p0.x + p1.x) / 2.0;
    const double direction = i % 2 == 0 ? 1.0 : -1.0;
    const double offset = amplitude * 1.5 * direction *
                          seededRandom(seed + i * 37.0 + 11.0);
    const QPointF c1(middle, p0.y + offset);
    const QPointF c2(middle, p1.y - offset);
    result.path.cubicTo(c1, c2, QPointF(p1.x, p1.y));
    result.pathData += QStringLiteral(" C%1,%2 %3,%4 %5,%6")
        .arg(number(c1.x()), number(c1.y()), number(c2.x()), number(c2.y()),
             number(p1.x), number(p1.y));
  }
  return result;
}

CynefinPathGeometry cliffPath(double width, double height) {
  const double cx = width / 2.0;
  const double top = height * 0.5;
  const double bottom = height;
  const double amplitude = width * 0.03;
  const QPointF p0(cx, top);
  const QPointF c1(cx + amplitude, top + (bottom - top) * 0.2);
  const QPointF c2(cx - amplitude * 1.5, top + (bottom - top) * 0.55);
  const QPointF p1(cx + amplitude * 0.5, top + (bottom - top) * 0.75);
  const QPointF c3(cx - amplitude, top + (bottom - top) * 0.85);
  const QPointF c4(cx + amplitude * 0.3, top + (bottom - top) * 0.95);
  const QPointF p2(cx, bottom);
  CynefinPathGeometry result;
  result.role = QStringLiteral("cliff");
  result.path.moveTo(p0);
  result.path.cubicTo(c1, c2, p1);
  result.path.cubicTo(c3, c4, p2);
  result.pathData = QStringLiteral("M%1,%2 C%3,%4 %5,%6 %7,%8 C%9,%10 %11,%12 %13,%14")
      .arg(number(p0.x()), number(p0.y()), number(c1.x()), number(c1.y()),
           number(c2.x()), number(c2.y()), number(p1.x()), number(p1.y()),
           number(c3.x()), number(c3.y()), number(c4.x()), number(c4.y()),
           number(p2.x()), number(p2.y()));
  return result;
}

QJsonObject rectJson(const QRectF &rect) {
  return {{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
          {QStringLiteral("width"), rect.width()},
          {QStringLiteral("height"), rect.height()}};
}

QJsonObject pointJson(const QPointF &point) {
  return {{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}};
}

} // namespace

void CynefinScene::paint(QPainter &painter,
                         const MermaidPaintOptions &options) const {
  paintCynefinScene(*this, painter, options);
}

QJsonObject CynefinScene::toJsonObject() const {
  QJsonArray backgroundValues;
  for (const auto &background : backgrounds)
    backgroundValues.append(QJsonObject{{QStringLiteral("domain"), background.domain},
                                        {QStringLiteral("rect"), rectJson(background.rect)},
                                        {QStringLiteral("fill"), background.fill}});
  QJsonArray boundaryValues;
  for (const auto &boundary : boundaries)
    boundaryValues.append(QJsonObject{{QStringLiteral("role"), boundary.role},
                                      {QStringLiteral("path"), boundary.pathData},
                                      {QStringLiteral("stroke"), boundary.stroke},
                                      {QStringLiteral("strokeWidth"), boundary.strokeWidth}});
  QJsonArray itemValues;
  for (const auto &item : items)
    itemValues.append(QJsonObject{{QStringLiteral("domain"), item.domain},
                                  {QStringLiteral("translation"), pointJson(item.translation)},
                                  {QStringLiteral("rect"), rectJson(item.rect.rect)},
                                  {QStringLiteral("text"), item.text.text},
                                  {QStringLiteral("overflow"), item.overflow}});
  QJsonArray arrowValues;
  for (const auto &arrow : arrows)
    arrowValues.append(QJsonObject{{QStringLiteral("from"), arrow.from},
                                   {QStringLiteral("to"), arrow.to},
                                   {QStringLiteral("path"), arrow.pathData},
                                   {QStringLiteral("label"), arrow.label.text}});
  return {{QStringLiteral("type"), QStringLiteral("cynefin")},
          {QStringLiteral("bounds"), rectJson(bounds)},
          {QStringLiteral("contentBounds"), rectJson(contentBounds)},
          {QStringLiteral("backgrounds"), backgroundValues},
          {QStringLiteral("boundaries"), boundaryValues},
          {QStringLiteral("confusion"), confusion.pathData},
          {QStringLiteral("items"), itemValues},
          {QStringLiteral("arrows"), arrowValues},
          {QStringLiteral("title"), title.text}};
}

CynefinScene buildCynefinScene(const CynefinData &data, CynefinConfig config,
                               CynefinSceneStyle style,
                               const CynefinCssOverrides *css) {
  CynefinScene scene;
  scene.style = std::move(style);
  const double width = jsNumber(config.width, 800.0);
  const double height = jsNumber(config.height, 600.0);
  const double padding = jsNumber(config.padding, 40.0);
  scene.configuredWidth = width;
  scene.configuredHeight = height;
  scene.configuredPadding = padding;
  scene.useMaxWidth = truthy(config.useMaxWidth, true);
  const JsAddResult totalWidth = addPadding(config.width, 800.0, padding * 2.0);
  const JsAddResult totalHeight = addPadding(config.height, 600.0, padding * 2.0);
  scene.viewBoxAttribute = QStringLiteral("0 0 %1 %2")
      .arg(totalWidth.text, totalHeight.text);
  const double usedWidth = std::isfinite(totalWidth.number) ? qMax(0.0, totalWidth.number) : 0.0;
  const double usedHeight = std::isfinite(totalHeight.number) ? qMax(0.0, totalHeight.number) : 0.0;
  scene.bounds = QRectF(0.0, 0.0, usedWidth, usedHeight);
  scene.contentBounds = QRectF(0.0, 0.0, width, height);

  const QString paddingText = primitiveString(config.padding, QStringLiteral("40"));
  scene.rootTransformAttribute = QStringLiteral("translate(%1, %1)").arg(paddingText);
  double usedPadding = 0.0;
  if (svgNumber(config.padding, 40.0, usedPadding))
    scene.rootTranslation = QPointF(usedPadding, usedPadding);

  const double halfWidth = width / 2.0;
  const double halfHeight = height / 2.0;
  const QHash<QString, Layout> layouts{
      {QStringLiteral("complex"), {halfWidth / 2.0, halfHeight / 2.0, 0.0, 0.0, halfWidth, halfHeight}},
      {QStringLiteral("complicated"), {halfWidth + halfWidth / 2.0, halfHeight / 2.0, halfWidth, 0.0, halfWidth, halfHeight}},
      {QStringLiteral("chaotic"), {halfWidth / 2.0, halfHeight + halfHeight / 2.0, 0.0, halfHeight, halfWidth, halfHeight}},
      {QStringLiteral("clear"), {halfWidth + halfWidth / 2.0, halfHeight + halfHeight / 2.0, halfWidth, halfHeight, halfWidth, halfHeight}},
      {QStringLiteral("confusion"), {halfWidth, halfHeight, halfWidth * 0.7, halfHeight * 0.7, halfWidth * 0.6, halfHeight * 0.6}}};
  const QHash<QString, QString> backgrounds{
      {QStringLiteral("complex"), scene.style.complexBg},
      {QStringLiteral("complicated"), scene.style.complicatedBg},
      {QStringLiteral("chaotic"), scene.style.chaoticBg},
      {QStringLiteral("clear"), scene.style.clearBg},
      {QStringLiteral("confusion"), scene.style.confusionBg}};
  const QStringList quadrants{QStringLiteral("complex"), QStringLiteral("complicated"),
                              QStringLiteral("chaotic"), QStringLiteral("clear")};
  const bool cssActive = css && css->active;
  for (const QString &domain : quadrants) {
    const auto layout = layouts.value(domain);
    scene.backgrounds.append({QStringLiteral("background"), domain,
                              QRectF(layout.x, layout.y, layout.w, layout.h),
                              0.0, backgrounds.value(domain), 0.4});
  }
  if (cssActive)
    for (qsizetype i = 0; i < scene.backgrounds.size() &&
                         i < css->backgrounds.size(); ++i)
      scene.backgrounds[i].css = css->backgrounds.at(i);

  const double amplitude = jsNumber(config.boundaryAmplitude, 8.0);
  const double seed = resolvedSeed(config.seed, config.svgId);
  CynefinPathGeometry fold = foldPath(width, height, seed, amplitude);
  fold.stroke = scene.style.boundaryColor;
  fold.strokeWidth = editor::cssStrokeWidthPx(scene.style.boundaryWidth,
      editor::pieCssLengthContext(scene.style.fontFamily, scene.style.rootFontSize),
      std::hypot(width, height) / std::sqrt(2.0));
  fold.fill = QStringLiteral("none");
  fold.dash = {6.0, 3.0};
  scene.boundaries.append(std::move(fold));
  CynefinPathGeometry horizontal = horizontalPath(width, height, seed + 100.0, amplitude);
  horizontal.stroke = scene.style.boundaryColor;
  horizontal.strokeWidth = scene.boundaries.first().strokeWidth;
  horizontal.fill = QStringLiteral("none");
  horizontal.dash = {6.0, 3.0};
  scene.boundaries.append(std::move(horizontal));
  CynefinPathGeometry cliff = cliffPath(width, height);
  cliff.stroke = scene.style.cliffColor;
  cliff.strokeWidth = editor::cssStrokeWidthPx(scene.style.cliffWidth,
      editor::pieCssLengthContext(scene.style.fontFamily, scene.style.rootFontSize),
      std::hypot(width, height) / std::sqrt(2.0));
  cliff.fill = QStringLiteral("none");
  scene.boundaries.append(std::move(cliff));

  const double rx = width * 0.15;
  const double ry = height * 0.15;
  scene.confusion.role = QStringLiteral("confusion");
  scene.confusion.domain = QStringLiteral("confusion");
  scene.confusion.path.addEllipse(QRectF(width / 2.0 - rx, height / 2.0 - ry,
                                        rx * 2.0, ry * 2.0));
  scene.confusion.pathData = QStringLiteral("M%1,%2 A%3,%4 0 1,1 %5,%2 A%3,%4 0 1,1 %1,%2 Z")
      .arg(number(width / 2.0 - rx), number(height / 2.0), number(rx),
           number(ry), number(width / 2.0 + rx));
  scene.confusion.fill = scene.style.confusionBg;
  scene.confusion.fillOpacity = 0.5;
  scene.confusion.stroke = scene.style.boundaryColor;
  scene.confusion.strokeWidth = 1.5;
  scene.confusion.dash = {4.0, 2.0};
  if (cssActive) {
    for (qsizetype i = 0; i < scene.boundaries.size() &&
                         i < css->boundaries.size(); ++i)
      scene.boundaries[i].css = css->boundaries.at(i);
    scene.confusion.css = css->confusion;
  }

  const bool descriptions = truthy(config.showDomainDescriptions, true);
  const qreal domainSize = fontSize(scene.style.domainFontSize, scene.style.rootFontSize);
  const qreal itemSize = fontSize(scene.style.itemFontSize, scene.style.rootFontSize);
  const qreal subtitleSize = derivedFontSize(scene.style.itemFontSize, -1.0,
                                             scene.style.rootFontSize, false);
  const auto addText = [&](QVector<CynefinTextGeometry> &target, QString role,
                           QString textValue, QPointF position, qreal size,
                           QString fill, bool bold, bool italic,
                           CynefinTextBaseline baseline) {
    CynefinTextGeometry text;
    text.role = std::move(role);
    text.text = std::move(textValue);
    text.position = position;
    text.fontSize = size;
    text.fill = std::move(fill);
    text.bold = bold;
    text.italic = italic;
    text.baseline = baseline;
    text.bounds = textBounds(scene.style, text.text, position, size, text.anchor,
                             baseline, bold, italic);
    target.append(std::move(text));
  };
  const auto displayName = [](const QString &domain) {
    QString value = domain;
    if (!value.isEmpty()) value[0] = value.at(0).toUpper();
    return value;
  };
  for (const QString &domain : quadrants) {
    const auto layout = layouts.value(domain);
    addText(scene.labels, QStringLiteral("domain-label"), displayName(domain),
            QPointF(layout.cx, descriptions ? layout.cy - 30.0 : layout.cy),
            domainSize, scene.style.labelColor, true, false,
            CynefinTextBaseline::Middle);
  }
  addText(scene.labels, QStringLiteral("domain-label"), QStringLiteral("Confusion"),
          QPointF(width / 2.0, descriptions ? height / 2.0 - 10.0 : height / 2.0),
          domainSize, scene.style.labelColor, true, false,
          CynefinTextBaseline::Middle);

  if (descriptions) {
    const QHash<QString, QPair<QString, QString>> meta{
        {QStringLiteral("complex"), {QStringLiteral("Probe \u2192 Sense \u2192 Respond"), QStringLiteral("Emergent Practices")}},
        {QStringLiteral("complicated"), {QStringLiteral("Sense \u2192 Analyse \u2192 Respond"), QStringLiteral("Good Practices")}},
        {QStringLiteral("clear"), {QStringLiteral("Sense \u2192 Categorise \u2192 Respond"), QStringLiteral("Best Practices")}},
        {QStringLiteral("chaotic"), {QStringLiteral("Act \u2192 Sense \u2192 Respond"), QStringLiteral("Novel Practices")}}};
    for (const QString &domain : quadrants) {
      const auto layout = layouts.value(domain);
      addText(scene.subtitles, QStringLiteral("subtitle"), meta.value(domain).first,
              QPointF(layout.cx, layout.cy - 10.0), subtitleSize,
              scene.style.textColor, false, true, CynefinTextBaseline::Middle);
      addText(scene.subtitles, QStringLiteral("subtitle"), meta.value(domain).second,
              QPointF(layout.cx, layout.cy + 5.0), subtitleSize,
              scene.style.textColor, false, true, CynefinTextBaseline::Middle);
    }
    addText(scene.subtitles, QStringLiteral("subtitle"), QStringLiteral("Disorder"),
            QPointF(width / 2.0, height / 2.0 + 8.0), subtitleSize,
            scene.style.textColor, false, true, CynefinTextBaseline::Middle);
  }
  if (cssActive) {
    for (qsizetype i = 0; i < scene.labels.size() && i < css->labels.size(); ++i)
      scene.labels[i].css = css->labels.at(i);
    for (qsizetype i = 0; i < scene.subtitles.size() &&
                         i < css->subtitles.size(); ++i)
      scene.subtitles[i].css = css->subtitles.at(i);
  }

  QHash<QString, const CynefinDomain *> dataDomains;
  for (const auto &domain : data.domains)
    dataDomains.insert(domain.name, &domain);
  const QStringList allDomains = quadrants + QStringList{QStringLiteral("confusion")};
  qsizetype cssItemSlot = 0;
  for (const QString &domainName : allDomains) {
    const CynefinDomain *domain = dataDomains.value(domainName, nullptr);
    if (!domain || domain->items.isEmpty()) continue;
    const Layout layout = layouts.value(domainName);
    const bool confusion = domainName == QLatin1String("confusion");
    const int visibleCount = confusion ? qMin(3, domain->items.size()) : domain->items.size();
    const double startY = layout.cy + (confusion ? (descriptions ? 22.0 : 14.0)
                                                   : (descriptions ? 25.0 : 15.0));
    const auto addItem = [&](const QString &label, int index, bool overflow) {
      const QString visible = text::collapsedSvgText(label);
      double measured = label.size() * 7.0;
      const CynefinElementCss *textCss = nullptr;
      const CynefinElementCss *rectCss = nullptr;
      if (cssActive && cssItemSlot < css->items.size()) {
        textCss = &css->items.at(cssItemSlot).text;
        rectCss = &css->items.at(cssItemSlot).rect;
      }
      // The badge getBBox runs on the classed text element, so the themeCSS
      // font feeds the measurement; own display:none zeroes the bbox and
      // keeps the length * 7 fallback. The stored text bounds always use the
      // css font — the final element renders with it even when hidden.
      CynefinSceneStyle textFontStyle = scene.style;
      qreal textFontSize = itemSize;
      bool textFontBold = false;
      bool textFontItalic = false;
      if (textCss) {
        if (!textCss->fontFamily.trimmed().isEmpty())
          textFontStyle.fontFamily = textCss->fontFamily;
        if (textCss->fontSize >= 0.0) textFontSize = textCss->fontSize;
        const QString weight = textCss->fontWeight.trimmed().toLower();
        textFontBold = weight == QLatin1String("bold") ||
                       weight == QLatin1String("bolder") ||
                       weight.toInt() >= 700;
        textFontItalic = textCss->fontStyle.trimmed().toLower() ==
                         QLatin1String("italic");
      }
      if (!textCss || textCss->measures) {
        const double actual = textBounds(
            textFontStyle, visible, QPointF(), textFontSize,
            QStringLiteral("start"), CynefinTextBaseline::Central,
            textFontBold, textFontItalic).width();
        if (actual > 0.0) measured = actual;
      }
      const double badgeWidth = measured + 20.0;
      CynefinItemGeometry item;
      item.domain = domainName;
      item.translation = QPointF(layout.cx - badgeWidth / 2.0,
                                 startY + index * 30.0);
      item.overflow = overflow;
      item.rect.role = overflow ? QStringLiteral("item-overflow")
                                : QStringLiteral("item");
      item.rect.domain = domainName;
      item.rect.rect = QRectF(0.0, 0.0, badgeWidth, 26.0);
      item.rect.radius = 4.0;
      item.rect.fill = backgrounds.value(domainName);
      item.rect.fillOpacity = overflow ? 0.6 : 0.95;
      item.rect.stroke = scene.style.boundaryColor;
      item.rect.strokeWidth = 1.0;
      if (overflow) item.rect.dash = {3.0, 2.0};
      if (rectCss) item.rect.css = *rectCss;
      item.text.role = QStringLiteral("item-text");
      item.text.text = label;
      item.text.position = QPointF(badgeWidth / 2.0, 13.0);
      item.text.fontSize = itemSize;
      item.text.fill = scene.style.textColor;
      item.text.baseline = CynefinTextBaseline::Central;
      if (textCss) item.text.css = *textCss;
      item.text.bounds = textBounds(textFontStyle, label, item.text.position,
                                    textFontSize, item.text.anchor,
                                    item.text.baseline, textFontBold,
                                    textFontItalic);
      scene.items.append(std::move(item));
      ++cssItemSlot;
    };
    for (int i = 0; i < visibleCount; ++i)
      addItem(domain->items.at(i).label, i, false);
    if (confusion && domain->items.size() > 3)
      addItem(QStringLiteral("+%1 more").arg(domain->items.size() - 3),
              visibleCount, true);
  }

  const qreal arrowWidth = editor::cssStrokeWidthPx(scene.style.arrowWidth,
      editor::pieCssLengthContext(scene.style.fontFamily, scene.style.rootFontSize),
      std::hypot(width, height) / std::sqrt(2.0));
  qsizetype cssArrowSlot = 0;
  for (const auto &transition : data.transitions) {
    if (!layouts.contains(transition.from) || !layouts.contains(transition.to))
      continue;
    const auto from = layouts.value(transition.from);
    const auto to = layouts.value(transition.to);
    CynefinArrowGeometry arrow;
    arrow.from = transition.from;
    arrow.to = transition.to;
    arrow.start = QPointF(from.cx, from.cy);
    arrow.end = QPointF(to.cx, to.cy);
    const QPointF delta = arrow.end - arrow.start;
    const double length = std::hypot(delta.x(), delta.y());
    const QPointF middle = (arrow.start + arrow.end) / 2.0;
    const double offset = length * 0.15;
    arrow.control = QPointF(middle.x() - delta.y() / length * offset,
                            middle.y() + delta.x() / length * offset);
    arrow.path.moveTo(arrow.start);
    arrow.path.quadTo(arrow.control, arrow.end);
    arrow.pathData = QStringLiteral("M%1,%2 Q%3,%4 %5,%6")
        .arg(number(arrow.start.x()), number(arrow.start.y()),
             number(arrow.control.x()), number(arrow.control.y()),
             number(arrow.end.x()), number(arrow.end.y()));
    arrow.stroke = scene.style.arrowColor;
    arrow.strokeWidth = arrowWidth;
    if (cssActive && cssArrowSlot < css->arrows.size()) {
      arrow.css = css->arrows.at(cssArrowSlot).line;
      if (transition.hasLabel)
        arrow.label.css = css->arrows.at(cssArrowSlot).label;
    }
    if (transition.hasLabel) {
      arrow.label.role = QStringLiteral("arrow-label");
      arrow.label.text = transition.label;
      arrow.label.position = arrow.control + QPointF(0.0, -6.0);
      arrow.label.fontSize = subtitleSize;
      arrow.label.fill = scene.style.textColor;
      arrow.label.baseline = CynefinTextBaseline::Auto;
      arrow.label.bounds = textBounds(scene.style, arrow.label.text,
                                      arrow.label.position, subtitleSize,
                                      arrow.label.anchor,
                                      CynefinTextBaseline::Auto);
    }
    scene.arrows.append(std::move(arrow));
    ++cssArrowSlot;
  }
  if (cssActive) scene.arrowHeadCss = css->arrowHead;

  if (!data.title.isEmpty()) {
    scene.title.role = QStringLiteral("title");
    scene.title.text = data.title;
    scene.title.position = QPointF(width / 2.0, -padding / 2.0);
    scene.title.fontSize = derivedFontSize(scene.style.domainFontSize, 2.0,
                                           scene.style.rootFontSize, true);
    scene.title.bold = true;
    scene.title.fill = scene.style.labelColor;
    scene.title.baseline = CynefinTextBaseline::Middle;
    if (cssActive) scene.title.css = css->title;
    scene.title.bounds = textBounds(scene.style, scene.title.text,
                                    scene.title.position, scene.title.fontSize,
                                    scene.title.anchor, scene.title.baseline, true);
  }
  return scene;
}

} // namespace muffin::mermaid::cynefin
