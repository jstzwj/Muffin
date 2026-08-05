// Native pie scene geometry. Computes the deterministic D3 pie/arc layout that
// reproduces mermaid 11.16.0 byte-for-byte (verified against the frozen
// tests/fixtures/mermaid/pie-geometry.json oracle — all slice pathD strings,
// angles, radii and counts match). See PieScene.h.

#include "mermaid/pie/PieScene.h"
#include "mermaid/pie/PieScenePainter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>

#include <cmath>

class QPainter;

namespace muffin::mermaid::pie {

void PieScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintPieScene(*this, painter, options);
}

namespace {

constexpr double kPi = 3.14159265358979323846;

// d3-path 3-decimal serializer: round to 3 decimals, strip trailing zeros,
// "-0" -> "0". Mirrors scripts/verify_pie_pathd.mjs (fmt), which reproduces all
// 26 frozen slice paths byte-for-byte.
QString fmt(qreal v) {
  if (std::fabs(v) < 5e-4) return QStringLiteral("0");
  const double r = std::round(v * 1000.0) / 1000.0;
  if (r == 0.0) return QStringLiteral("0");
  QString s = QString::number(r, 'f', 3);
  while (s.endsWith(QLatin1Char('0'))) s.chop(1);
  if (s.endsWith(QLatin1Char('.'))) s.chop(1);
  return s;
}

// d3 canvas-convention point at angle a (radians): (r*sin(a), -r*cos(a)).
// Angle 0 = 12 o'clock; positive = clockwise (SVG y-down). This is bit-for-bit
// the computation order d3.pie/d3.arc use (a = k*cumulative, k = 2pi/sum).
struct XY {
  double x;
  double y;
};
XY d3Point(double r, double a) {
  return {r * std::sin(a), -r * std::cos(a)};
}
QString fmtXY(const XY& pt) {
  return fmt(pt.x) + QLatin1Char(',') + fmt(pt.y);
}

// Generate the d3 arc path `d` for a slice, reproducing d3-path's serialization
// exactly: M(outerStart) A outer sweep=1 [L(innerEnd) A inner sweep=0] Z, with a
// full-circle split into two semicircle arcs when the span is >= 360 deg.
QString arcPath(double startA, double endA, double outerR, double innerR) {
  const double span = endA - startA;
  const bool full = span >= 2.0 * kPi - 1e-9;
  const XY oStart = d3Point(outerR, startA);
  const XY oEnd = d3Point(outerR, endA);
  if (full) {
    const XY opp = d3Point(outerR, startA + kPi);
    if (innerR == 0.0) {
      return QStringLiteral("M%1A%2,%2,0,1,1,%3A%2,%2,0,1,1,%4Z")
          .arg(fmtXY(oStart), fmt(outerR), fmtXY(opp), fmtXY(oStart));
    }
    const XY iStart = d3Point(innerR, startA);
    const XY iOpp = d3Point(innerR, startA + kPi);
    return QStringLiteral("M%1A%2,%2,0,1,1,%3A%2,%2,0,1,1,%4L%5A%6,%6,0,1,0,%7A%6,%6,0,1,0,%8Z")
        .arg(fmtXY(oStart), fmt(outerR), fmtXY(opp), fmtXY(oStart), fmtXY(iStart),
             fmt(innerR), fmtXY(iOpp), fmtXY(iStart));
  }
  const QString la = span > kPi ? QStringLiteral("1") : QStringLiteral("0");
  QString d = QStringLiteral("M%1A%2,%2,0,%3,1,%4")
                  .arg(fmtXY(oStart), fmt(outerR), la, fmtXY(oEnd));
  if (innerR == 0.0) return d + QStringLiteral("L0,0Z");
  const XY iEnd = d3Point(innerR, endA);
  const XY iStart = d3Point(innerR, startA);
  return d + QStringLiteral("L%1A%2,%2,0,%3,0,%4Z")
                 .arg(fmtXY(iEnd), fmt(innerR), la, fmtXY(iStart));
}

// Round to 3 decimals as a numeric JSON value (mirrors the oracle's number()).
double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

// (value/origSum*100).toFixed(0) — JS Number.prototype.toFixed rounds half away
// from zero for positive values; the slice values are all >= 0 here.
QString percentLabel(double value, double origSum) {
  if (origSum <= 0.0) return QStringLiteral("0%");
  return QString::number(std::round(value / origSum * 100.0), 'f', 0) + QLatin1Char('%');
}

}  // namespace

QString formatPieCoord(qreal v) { return fmt(v); }

PieScene buildPieScene(const PieData& data, const PieConfig& config, PieSceneStyle style) {
  PieScene scene;
  scene.style = std::move(style);

  // Fixed chart constants (pieRenderer.ts draw()).
  scene.margin = 40.0;
  scene.height = 450.0;
  scene.pieWidth = 450.0;
  scene.centerX = scene.pieWidth / 2.0;   // 225
  scene.centerY = scene.height / 2.0;     // 225
  scene.legendRectSize = 18.0;
  scene.legendSpacing = 4.0;
  scene.legendHeight = scene.legendRectSize + scene.legendSpacing;  // 22

  // Resolved config.
  scene.textPosition = config.textPosition;
  scene.donutHole = config.donutHole;
  // donutHole clamps to (0, 0.9] (pieRenderer line 161).
  scene.effectiveDonutHole =
      (config.donutHole > 0.0 && config.donutHole <= 0.9) ? config.donutHole : 0.0;
  scene.legendPosition = config.legendPosition.isEmpty() ? QStringLiteral("right")
                                                         : config.legendPosition;
  scene.highlightSlice = config.highlightSlice;
  scene.showData = data.showData;
  scene.useMaxWidth = config.useMaxWidth;
  scene.title = data.title;
  scene.accTitle = data.accTitle;
  scene.accDescr = data.accDescr;

  scene.radius = std::min(scene.pieWidth, scene.height) / 2.0 - scene.margin;  // 185
  scene.outerRingRadius = scene.radius + scene.style.outerStrokeWidth / 2.0;   // 186
  scene.donutInnerRadius = scene.effectiveDonutHole * scene.radius;            // 0 solid
  scene.labelRadius = scene.radius * scene.textPosition;                       // 138.75

  const QVector<PieSection>& sections = data.sections;
  scene.originalSum = pieOriginalSum(sections);

  // Pre-d3pie <1% filter: keep sections whose share of the ORIGINAL sum is
  // >= 1%. d3pie then normalizes over the FILTERED (survivor) sum. The global
  // section index is carried so each slice keeps its scaleOrdinal color slot
  // (filtered slices still consume their palette index).
  struct Drawn {
    const PieSection* section;
    int globalIndex;
  };
  QVector<Drawn> drawn;
  if (scene.originalSum > 0.0) {
    for (int i = 0; i < sections.size(); ++i) {
      const PieSection& s = sections.at(i);
      if (s.value / scene.originalSum * 100.0 >= 1.0) drawn.append({&s, i});
    }
  }
  scene.filteredSum = 0.0;
  for (const Drawn& d : drawn) scene.filteredSum += d.section->value;

  // Legends: ALL sections, source order (scaleOrdinal domain).
  for (int i = 0; i < sections.size(); ++i) {
    const PieSection& s = sections.at(i);
    PieLegendEntry entry;
    entry.label = s.label;
    entry.text = scene.showData
                     ? QStringLiteral("%1 [%2]").arg(s.label).arg(QString::number(s.value))
                     : s.label;
    entry.colorIndex = scene.style.palette.isEmpty() ? 0 : i % scene.style.palette.size();
    entry.fill =
        scene.style.palette.isEmpty() ? QString() : scene.style.palette.at(entry.colorIndex);
    scene.legends.append(entry);
  }

  // Slice arcs (the byte-parity geometry).
  if (scene.filteredSum > 0.0) {
    const double k = 2.0 * kPi / scene.filteredSum;
    double cum = 0.0;
    for (const Drawn& dr : drawn) {
      const PieSection& s = *dr.section;
      const double a0 = k * cum;
      cum += s.value;
      const double a1 = k * cum;
      PieSliceGeometry g;
      g.label = s.label;
      g.value = s.value;
      g.colorIndex = scene.style.palette.isEmpty() ? 0
                      : dr.globalIndex % scene.style.palette.size();
      g.fill = scene.style.palette.isEmpty() ? QString()
                                             : scene.style.palette.at(g.colorIndex);
      g.startAngleDeg = round3(-90.0 + (a0 / (2.0 * kPi)) * 360.0);
      g.endAngleDeg = round3(-90.0 + (a1 / (2.0 * kPi)) * 360.0);
      g.midAngleDeg = round3((g.startAngleDeg + g.endAngleDeg) / 2.0);
      g.outerRadius = round3(scene.radius);
      g.innerRadius = round3(scene.donutInnerRadius);
      g.labelRadius = round3(scene.labelRadius);
      g.percentage = percentLabel(s.value, scene.originalSum);
      g.rawPercentage = round3(scene.originalSum > 0.0 ? s.value / scene.originalSum * 100.0 : 0.0);
      // Label centroid = d3 labelArc centroid = (labelR*sin(aMid), -labelR*cos(aMid)).
      const double aMid = (a0 + a1) / 2.0;
      const XY c = d3Point(scene.labelRadius, aMid);
      g.centroidX = c.x;
      g.centroidY = c.y;
      g.pathD = arcPath(a0, a1, scene.radius, scene.donutInnerRadius);
      if (scene.highlightSlice == QStringLiteral("hover"))
        g.className += QStringLiteral(" highlightedOnHover");
      else if (scene.highlightSlice == s.label)  // "" == "" : empty highlight hits empty-label slices
        g.className += QStringLiteral(" highlighted");
      scene.slices.append(g);
    }
  }

  // Canvas width (font-coupled): right legend -> pieWidth + margin + legend block.
  // The exact legend text width is measured in the painter; here we use a
  // reasonable default so the scene bounds are non-trivial before paint.
  scene.totalWidth = scene.pieWidth + scene.margin;  // refined in the painter
  scene.bounds = QRectF(0.0, 0.0, scene.totalWidth, scene.height);
  return scene;
}

QJsonObject PieScene::toJsonObject() const {
  QJsonObject constants;
  constants[QStringLiteral("margin")] = margin;
  constants[QStringLiteral("height")] = height;
  constants[QStringLiteral("pieWidth")] = pieWidth;
  constants[QStringLiteral("centerX")] = centerX;
  constants[QStringLiteral("centerY")] = centerY;
  constants[QStringLiteral("radius")] = radius;
  constants[QStringLiteral("legendRectSize")] = legendRectSize;
  constants[QStringLiteral("legendSpacing")] = legendSpacing;
  constants[QStringLiteral("legendHeight")] = legendHeight;

  QJsonObject configJson;
  configJson[QStringLiteral("textPosition")] = textPosition;
  configJson[QStringLiteral("donutHole")] = donutHole;
  configJson[QStringLiteral("effectiveDonutHole")] = effectiveDonutHole;

  QJsonObject chartCenter;
  chartCenter[QStringLiteral("x")] = centerX;
  chartCenter[QStringLiteral("y")] = centerY;

  QJsonArray legendsArray;
  for (const PieLegendEntry& e : legends) {
    QJsonObject o;
    o[QStringLiteral("text")] = e.text;
    legendsArray.append(o);
  }

  QJsonArray slicesArray;
  for (const PieSliceGeometry& s : slices) {
    QJsonObject o;
    o[QStringLiteral("label")] = s.label;
    o[QStringLiteral("value")] = s.value;
    o[QStringLiteral("percentage")] = s.percentage;
    o[QStringLiteral("rawPercentage")] = s.rawPercentage;
    o[QStringLiteral("startAngleDeg")] = s.startAngleDeg;
    o[QStringLiteral("endAngleDeg")] = s.endAngleDeg;
    o[QStringLiteral("midAngleDeg")] = s.midAngleDeg;
    o[QStringLiteral("outerRadius")] = s.outerRadius;
    o[QStringLiteral("innerRadius")] = s.innerRadius;
    o[QStringLiteral("donutInnerRadius")] = s.innerRadius;
    o[QStringLiteral("labelRadius")] = s.labelRadius;
    o[QStringLiteral("centroidX")] = round3(s.centroidX);
    o[QStringLiteral("centroidY")] = round3(s.centroidY);
    o[QStringLiteral("pathD")] = s.pathD;
    o[QStringLiteral("fill")] = s.fill;
    o[QStringLiteral("cls")] = s.className;
    o[QStringLiteral("labelTransform")] =
        QStringLiteral("translate(%1,%2)").arg(QString::number(s.centroidX, 'g', 17),
                                                QString::number(s.centroidY, 'g', 17));
    slicesArray.append(o);
  }

  QJsonObject sums;
  sums[QStringLiteral("original")] = round3(originalSum);
  sums[QStringLiteral("filtered")] = round3(filteredSum);

  QJsonObject expected;
  expected[QStringLiteral("constants")] = constants;
  expected[QStringLiteral("config")] = configJson;
  expected[QStringLiteral("outerRingRadius")] = outerRingRadius;
  expected[QStringLiteral("chartCenter")] = chartCenter;
  expected[QStringLiteral("title")] = title;
  expected[QStringLiteral("accTitle")] = accTitle.isEmpty() ? QJsonValue() : QJsonValue(accTitle);
  expected[QStringLiteral("accDescr")] = accDescr.isEmpty() ? QJsonValue() : QJsonValue(accDescr);
  expected[QStringLiteral("legendCount")] = legends.size();
  expected[QStringLiteral("legends")] = legendsArray;
  expected[QStringLiteral("sliceCount")] = slices.size();
  expected[QStringLiteral("slices")] = slicesArray;
  expected[QStringLiteral("sums")] = sums;
  expected[QStringLiteral("bounds")] =
      QJsonArray{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
  return expected;
}

}  // namespace muffin::mermaid::pie
