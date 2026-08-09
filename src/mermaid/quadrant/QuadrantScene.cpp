// Native quadrantChart scene — deterministic layout (quadrantBuilder.ts formula)
// + JSON dump for the grammar/geometry oracles. See QuadrantScene.h.

#include "mermaid/quadrant/QuadrantScene.h"
#include "mermaid/quadrant/QuadrantScenePainter.h"
#include "mermaid/editor/MermaidRenderSupport.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

class QPainter;

namespace muffin::mermaid::quadrant {

void QuadrantScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintQuadrantScene(*this, painter, options);
}

namespace {

double cfgD(const QJsonObject& o, const QString& k, double dflt) {
  const QJsonValue v = o.value(k);
  if (v.isUndefined() || v.isNull()) return dflt;
  return editor::jsNumberValue(v);
}

QString cfgString(const QJsonObject& o, const QString& key, const QString& fallback) {
  const QJsonValue v = o.value(key);
  if (v.isUndefined() || v.isNull()) return fallback;
  return v.isString() ? v.toString() : QStringLiteral("__non_string__");
}

bool cfgTruthy(const QJsonObject& o, const QString& key, bool fallback) {
  const QJsonValue v = o.value(key);
  return v.isUndefined() || v.isNull() ? fallback : editor::truthyConfigValue(v);
}

qreal usedSvgNonNegativeLength(qreal value, qreal invalidFallback) {
  if (!std::isfinite(value) || value < 0.0) return invalidFallback;
  return std::min<qreal>(value, 33554428.0);
}

}  // namespace

QuadrantScene buildQuadrantScene(const QuadrantData& data, const QJsonObject& cfg,
                                 QuadrantSceneStyle style) {
  QuadrantScene s;
  // Copy (not move): Qt's move-assignment swaps, which would leave `style`
  // holding s.style's prior (default) values and corrupt the reads below.
  s.style = style;
  s.title = data.title;
  s.accTitle = data.accTitle;
  s.accDescr = data.accDescr;

  const double chartWidth = cfgD(cfg, QStringLiteral("chartWidth"), 500.0);
  const double chartHeight = cfgD(cfg, QStringLiteral("chartHeight"), 500.0);
  s.chartWidth = chartWidth;
  s.chartHeight = chartHeight;
  const bool cfgShowX = cfgTruthy(cfg, QStringLiteral("showXAxis"), true);
  const bool cfgShowY = cfgTruthy(cfg, QStringLiteral("showYAxis"), true);
  const bool cfgShowTitle = cfgTruthy(cfg, QStringLiteral("showTitle"), true);
  const double titlePadding = cfgD(cfg, QStringLiteral("titlePadding"), 10.0);
  const double titleFontSize = cfgD(cfg, QStringLiteral("titleFontSize"), 20.0);
  const double quadrantPadding = cfgD(cfg, QStringLiteral("quadrantPadding"), 5.0);
  const double xAxisLabelPadding = cfgD(cfg, QStringLiteral("xAxisLabelPadding"), 5.0);
  const double yAxisLabelPadding = cfgD(cfg, QStringLiteral("yAxisLabelPadding"), 5.0);
  const double xAxisLabelFontSize = cfgD(cfg, QStringLiteral("xAxisLabelFontSize"), 16.0);
  const double yAxisLabelFontSize = cfgD(cfg, QStringLiteral("yAxisLabelFontSize"), 16.0);
  const double quadrantLabelFontSize = cfgD(cfg, QStringLiteral("quadrantLabelFontSize"), 16.0);
  const double pointLabelFontSize = cfgD(cfg, QStringLiteral("pointLabelFontSize"), 12.0);
  const double quadrantTextTopPadding = cfgD(cfg, QStringLiteral("quadrantTextTopPadding"), 5.0);
  const double pointTextPadding = cfgD(cfg, QStringLiteral("pointTextPadding"), 5.0);
  const double pointRadius = cfgD(cfg, QStringLiteral("pointRadius"), 5.0);
  const QString xAxisPositionCfg = cfgString(cfg, QStringLiteral("xAxisPosition"), QStringLiteral("top"));
  const QString yAxisPosition = cfgString(cfg, QStringLiteral("yAxisPosition"), QStringLiteral("left"));
  const double internalSW = cfgD(cfg, QStringLiteral("quadrantInternalBorderStrokeWidth"), 1.0);
  const double externalSW = cfgD(cfg, QStringLiteral("quadrantExternalBorderStrokeWidth"), 2.0);

  const bool showX = cfgShowX && (!data.xAxisLeftText.isEmpty() || !data.xAxisRightText.isEmpty());
  const bool showY = cfgShowY && (!data.yAxisBottomText.isEmpty() || !data.yAxisTopText.isEmpty());
  const bool showTitle = cfgShowTitle && !data.title.isEmpty();
  // A chart WITH points forces the x-axis to the bottom.
  const QString xAxisPosition = data.points.size() > 0 ? QStringLiteral("bottom") : xAxisPositionCfg;

  const double xAxisSpaceCalc = xAxisLabelPadding * 2.0 + xAxisLabelFontSize;       // 26
  const double yAxisSpaceCalc = yAxisLabelPadding * 2.0 + yAxisLabelFontSize;       // 26
  const double titleSpaceCalc = titleFontSize + titlePadding * 2.0;                 // 40
  const double xAxisSpaceTop = (xAxisPosition == QStringLiteral("top") && showX) ? xAxisSpaceCalc : 0.0;
  const double xAxisSpaceBottom = (xAxisPosition == QStringLiteral("bottom") && showX) ? xAxisSpaceCalc : 0.0;
  const double yAxisSpaceLeft = (yAxisPosition == QStringLiteral("left") && showY) ? yAxisSpaceCalc : 0.0;
  const double yAxisSpaceRight = (yAxisPosition == QStringLiteral("right") && showY) ? yAxisSpaceCalc : 0.0;
  const double titleSpaceTop = showTitle ? titleSpaceCalc : 0.0;

  const double quadrantLeft = quadrantPadding + yAxisSpaceLeft;
  const double quadrantTop = quadrantPadding + xAxisSpaceTop + titleSpaceTop;
  const double quadrantWidth = chartWidth - quadrantPadding * 2.0 - yAxisSpaceLeft - yAxisSpaceRight;
  const double quadrantHeight = chartHeight - quadrantPadding * 2.0 - xAxisSpaceTop - xAxisSpaceBottom - titleSpaceTop;
  const double halfW = quadrantWidth / 2.0;
  const double halfH = quadrantHeight / 2.0;
  // Quadrants (Q1 top-right, Q2 top-left, Q3 bottom-left, Q4 bottom-right).
  const QString qFills[4] = {style.quadrant1Fill, style.quadrant2Fill, style.quadrant3Fill, style.quadrant4Fill};
  const QString qTextFills[4] = {style.quadrant1TextFill, style.quadrant2TextFill, style.quadrant3TextFill, style.quadrant4TextFill};
  const QString qTexts[4] = {data.quadrant1Text, data.quadrant2Text, data.quadrant3Text, data.quadrant4Text};
  const double qx[4] = {quadrantLeft + halfW, quadrantLeft, quadrantLeft, quadrantLeft + halfW};
  const double qy[4] = {quadrantTop, quadrantTop, quadrantTop + halfH, quadrantTop + halfH};
  for (int i = 0; i < 4; ++i) {
    QuadrantRect r;
    r.x = qx[i]; r.y = qy[i]; r.width = halfW; r.height = halfH;
    r.fill = qFills[i]; r.text = qTexts[i]; r.textFill = qTextFills[i];
    s.quadrants.append(r);
  }

  // Points — REVERSE source order (addPoints prepends). d3 scaleLinear [0,1].
  // Effective styles: classDef (matched by name via :::, last-wins) then the
  // point's inline stylesOpt overrides (inline > classDef > theme).
  for (int idx = data.points.size() - 1; idx >= 0; --idx) {
    const QuadrantPoint& pp = data.points.at(idx);
    const double px = quadrantLeft + pp.x * quadrantWidth;
    const double py = quadrantTop + quadrantHeight - pp.y * quadrantHeight;
    const QuadrantClassDef* cdef = nullptr;
    if (!pp.className.isEmpty())
      for (int ci = data.classDefs.size() - 1; ci >= 0; --ci)
        if (data.classDefs.at(ci).name == pp.className) { cdef = &data.classDefs.at(ci); break; }
    QuadrantStyles eff = cdef ? cdef->styles : QuadrantStyles{};
    if (pp.inlineStyles.radius >= 0) eff.radius = pp.inlineStyles.radius;
    if (!pp.inlineStyles.color.isEmpty()) eff.color = pp.inlineStyles.color;
    if (!pp.inlineStyles.strokeColor.isEmpty()) eff.strokeColor = pp.inlineStyles.strokeColor;
    if (!pp.inlineStyles.strokeWidth.isEmpty()) eff.strokeWidth = pp.inlineStyles.strokeWidth;
    QuadrantPointG g;
    g.x = px;
    g.y = py;
    // SVG circle radius and stroke-width are saturated by Chromium's used-value
    // conversion. Style grammar accepts only non-negative integer radius / Npx.
    g.radius = usedSvgNonNegativeLength(eff.radius >= 0 ? eff.radius : pointRadius,
                                        0.0);
    g.fill = !eff.color.isEmpty() ? eff.color : style.quadrantPointFill;
    g.stroke = !eff.strokeColor.isEmpty() ? eff.strokeColor : style.quadrantPointFill;
    const QString strokeWidth = !eff.strokeWidth.isEmpty() ? eff.strokeWidth
                                                            : QStringLiteral("0px");
    g.strokeWidth = editor::cssStrokeWidthPx(
        strokeWidth, editor::pieCssLengthContext(style.fontFamily, 16.0),
        std::sqrt(chartWidth * chartWidth + chartHeight * chartHeight) / std::sqrt(2.0));
    g.text = pp.label;
    s.points.append(g);
  }

  // Borders (4 external + 2 internal).
  const double he = externalSW / 2.0;
  const auto ext = [&](double x1, double y1, double x2, double y2) {
    s.borders.append({x1, y1, x2, y2, style.quadrantExternalBorderStrokeFill,
                      usedSvgNonNegativeLength(externalSW, 1.0)});
  };
  const auto intr = [&](double x1, double y1, double x2, double y2) {
    s.borders.append({x1, y1, x2, y2, style.quadrantInternalBorderStrokeFill,
                      usedSvgNonNegativeLength(internalSW, 1.0)});
  };
  ext(quadrantLeft - he, quadrantTop, quadrantLeft + quadrantWidth + he, quadrantTop);                  // top
  ext(quadrantLeft + quadrantWidth, quadrantTop + he, quadrantLeft + quadrantWidth,
      quadrantTop + quadrantHeight - he);                                                                // right
  ext(quadrantLeft - he, quadrantTop + quadrantHeight, quadrantLeft + quadrantWidth + he,
      quadrantTop + quadrantHeight);                                                                     // bottom
  ext(quadrantLeft, quadrantTop + he, quadrantLeft, quadrantTop + quadrantHeight - he);                  // left
  intr(quadrantLeft + halfW, quadrantTop + he, quadrantLeft + halfW, quadrantTop + quadrantHeight - he); // vertical
  intr(quadrantLeft + he, quadrantTop + halfH, quadrantLeft + quadrantWidth - he, quadrantTop + halfH);  // horizontal

  // Axis labels.
  const bool drawXMid = !data.xAxisRightText.isEmpty();   // Boolean(xAxisRightText)
  const bool drawYMid = !data.yAxisTopText.isEmpty();     // Boolean(yAxisTopText)
  const double xAxisY = xAxisPosition == QStringLiteral("top")
                            ? xAxisLabelPadding + titleSpaceTop
                            : xAxisLabelPadding + quadrantTop + quadrantHeight + quadrantPadding;
  const auto xLabel = [&](const QString& text, double x) {
    if (text.isEmpty() || !showX) return;
    s.axisLabels.append({text, style.quadrantXAxisTextFill, x, xAxisY,
                         xAxisLabelFontSize, 0, drawXMid});
  };
  xLabel(data.xAxisLeftText, quadrantLeft + (drawXMid ? halfW / 2.0 : 0.0));
  xLabel(data.xAxisRightText, quadrantLeft + halfW + (drawXMid ? halfW / 2.0 : 0.0));
  const double yAxisX = yAxisPosition == QStringLiteral("left")
                            ? yAxisLabelPadding
                            : yAxisLabelPadding + quadrantLeft + quadrantWidth + quadrantPadding;
  const auto yLabel = [&](const QString& text, double y) {
    if (text.isEmpty() || !showY) return;
    s.axisLabels.append({text, style.quadrantYAxisTextFill, yAxisX, y,
                         yAxisLabelFontSize, -90, drawYMid});
  };
  yLabel(data.yAxisBottomText, quadrantTop + quadrantHeight - (drawYMid ? halfH / 2.0 : 0.0));
  yLabel(data.yAxisTopText, quadrantTop + halfH - (drawYMid ? halfH / 2.0 : 0.0));

  // Title.
  if (showTitle) {
    s.titleText = data.title;
    s.titleX = chartWidth / 2.0;
    s.titleY = titlePadding;
  }

  // Quadrant label Y placement depends on whether there are points (middle vs top).
  // (Stored implicitly: the painter computes it; record it in toJson.)
  s.bounds = QRectF(0.0, 0.0, chartWidth, chartHeight);
  // Surface the live config so the painter does not hardcode sizes/paddings.
  s.quadrantLabelFontSize = quadrantLabelFontSize;
  s.pointLabelFontSize = pointLabelFontSize;
  s.xAxisLabelFontSize = xAxisLabelFontSize;
  s.yAxisLabelFontSize = yAxisLabelFontSize;
  s.titleFontSizeCfg = titleFontSize;
  s.quadrantTextTopPadding = quadrantTextTopPadding;
  s.pointTextPadding = pointTextPadding;
  s.pointRadiusCfg = pointRadius;
  return s;
}

namespace {
double r3(double v) { return std::round(v * 1000.0) / 1000.0; }
}  // namespace

QJsonObject QuadrantScene::toJsonObject() const {
  QJsonArray qTexts, pTexts, aTexts;
  for (const auto& q : quadrants) qTexts.append(q.text);
  for (const auto& p : points) pTexts.append(p.text);
  for (const auto& a : axisLabels) aTexts.append(a.text);

  QJsonArray qArr;
  const bool pointsEmpty = points.isEmpty();
  for (const auto& q : quadrants) {
    const double textY = pointsEmpty ? q.y + q.height / 2.0 : q.y + quadrantTextTopPadding;
    QJsonObject o;
    QJsonObject rect;
    rect[QStringLiteral("x")] = r3(q.x);
    rect[QStringLiteral("y")] = r3(q.y);
    rect[QStringLiteral("width")] = r3(q.width);
    rect[QStringLiteral("height")] = r3(q.height);
    rect[QStringLiteral("fill")] = q.fill;
    o[QStringLiteral("rect")] = rect;
    o[QStringLiteral("text")] = q.text;
    o[QStringLiteral("transform")] =
        QStringLiteral("translate(%1, %2) rotate(0)").arg(r3(q.x + q.width / 2.0)).arg(r3(textY));
    qArr.append(o);
  }
  QJsonArray pArr;
  for (const auto& p : points) {
    QJsonObject o;
    o[QStringLiteral("cx")] = r3(p.x);
    o[QStringLiteral("cy")] = r3(p.y);
    o[QStringLiteral("r")] = r3(p.radius);
    o[QStringLiteral("fill")] = p.fill;
    o[QStringLiteral("stroke")] = p.stroke;
    o[QStringLiteral("strokeWidth")] = r3(p.strokeWidth);
    o[QStringLiteral("text")] = p.text;
    o[QStringLiteral("transform")] =
        QStringLiteral("translate(%1, %2) rotate(0)").arg(r3(p.x)).arg(r3(p.y + pointTextPadding));
    pArr.append(o);
  }
  QJsonArray bArr;
  for (const auto& b : borders) {
    QJsonObject o;
    o[QStringLiteral("x1")] = r3(b.x1);
    o[QStringLiteral("y1")] = r3(b.y1);
    o[QStringLiteral("x2")] = r3(b.x2);
    o[QStringLiteral("y2")] = r3(b.y2);
    bArr.append(o);
  }
  QJsonArray aArr;
  for (const auto& a : axisLabels) {
    QJsonObject o;
    o[QStringLiteral("text")] = a.text;
    o[QStringLiteral("transform")] =
        QStringLiteral("translate(%1, %2) rotate(%3)").arg(r3(a.x)).arg(r3(a.y)).arg(a.rotation);
    aArr.append(o);
  }

  QJsonObject root;
  root[QStringLiteral("quadrantTexts")] = qTexts;
  root[QStringLiteral("pointTexts")] = pTexts;
  root[QStringLiteral("axisTexts")] = aTexts;
  root[QStringLiteral("title")] = titleText.isEmpty() ? QJsonValue() : QJsonValue(titleText);
  root[QStringLiteral("quadrantRectCount")] = quadrants.size();
  root[QStringLiteral("pointCount")] = points.size();
  root[QStringLiteral("borderCount")] = borders.size();
  root[QStringLiteral("quadrants")] = qArr;
  root[QStringLiteral("points")] = pArr;
  root[QStringLiteral("borders")] = bArr;
  root[QStringLiteral("axisLabels")] = aArr;
  root[QStringLiteral("bounds")] = QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
  return root;
}

}  // namespace muffin::mermaid::quadrant
