// Pie adapter theme-wiring: verifies the PRODUCTION path (MermaidRenderCache ->
// PieDiagramAdapter -> PieScene) consumes the resolved FlowThemeVariables --
// pie1..pie12 palette, the pie*TextColor keys, stroke colors/widths, opacity,
// and the three text sizes -- instead of the old default/dark hardcoded
// palettes. This is the adapter/render-level complement to the theme-MODEL
// tests in MermaidThemeTest (which only exercise resolveFlowTheme/get/set).
//
// Covers (Codex adapter spec): (1) default-theme scene fields == resolved model;
// (2) a non-default (dark) theme via a self-declaring source; (3) source-entry
// `%%{init}%%` overrides for palette / text color / stroke / opacity reach
// scene.style; (4) dark pie12 empty at TCL=12 and = cScale12 (#010029) at TCL=13;
// (5) an override reaches the ACTUAL painted RGBA; (6) the parseCssPx / opacityValue
// numeric helpers match the probed browser semantics (scripts/probe_mermaid_pie_scalars.mjs).
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/pie/PieScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
bool approx(qreal a, qreal b) { return std::abs(a - b) < 1e-9; }

QImage decodePng(const QString& dataUrl) {
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage img;
  img.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return img;
}

const pie::PieScene* renderPie(editor::MermaidRenderCache& cache, const QString& source) {
  const auto entry = cache.getSync(cache.makeKey(source), source);
  const auto* scene = dynamic_cast<const pie::PieScene*>(entry.scene.get());
  require(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr,
          QStringLiteral("pie render failed: ") + entry.errorMessage);
  return scene;
}
// Count OPAQUE pixels within `tol` per channel of `target`. Transparent pixels
// have RGB (0,0,0) and would falsely match black, so they are excluded.
int countColor(const QImage& img, const QColor& target, int tol) {
  int count = 0;
  for (int y = 0; y < img.height(); ++y)
    for (int x = 0; x < img.width(); ++x) {
      const QColor c = img.pixelColor(x, y);
      if (c.alpha() < 128) continue;
      if (std::abs(c.red() - target.red()) <= tol && std::abs(c.green() - target.green()) <= tol &&
          std::abs(c.blue() - target.blue()) <= tol)
        ++count;
    }
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  (void)argv;

  // --- numeric helpers match the probed browser CSS semantics ---
  // (scripts/probe_mermaid_pie_scalars.mjs + probe_mermaid_css_units.mjs). A REAL
  // CssLengthContext (800x600 viewport + pie-font ex/ch), not the placeholder.
  const muffin::CssLengthContext ctx = editor::pieCssLengthContext(QStringLiteral("Noto Sans"), 16.0);
  const qreal diag = 500.0;  // test SVG diagonal for stroke-width %
  // cssStrokeWidthPx: px/unitless==px, em x16, pt x4/3, vw/vh of 800x600, ex/ch
  //   font-relative, % of the diagonal; 0 ok; missing/invalid/negative -> 1.
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("2px"), ctx, diag), 2.0), "sw 2px");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("2"), ctx, diag), 2.0), "sw unitless 2");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("0px"), ctx, diag), 0.0), "sw 0px (zero)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("0"), ctx, diag), 0.0), "sw 0 (zero)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("1.7"), ctx, diag), 1.7), "sw unitless 1.7");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("3em"), ctx, diag), 48.0), "sw 3em -> 48 (x16)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("1.5pt"), ctx, diag), 2.0), "sw 1.5pt -> 2 (x96/72)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("1e1px"), ctx, diag), 10.0), "sw 1e1px -> 10");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10vw"), ctx, diag), 80.0), "sw 10vw -> 80 (800 viewport)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10vh"), ctx, diag), 60.0), "sw 10vh -> 60 (600 viewport)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10vmin"), ctx, diag), 60.0), "sw 10vmin -> 60");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10vmax"), ctx, diag), 80.0), "sw 10vmax -> 80");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10ex"), ctx, diag), 10.0 * ctx.exPx), "sw 10ex -> 10*xHeight");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10ch"), ctx, diag), 10.0 * ctx.chPx), "sw 10ch -> 10*advance('0')");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("50%"), ctx, diag), 250.0), "sw 50% -> 250 (x diagonal)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("abc"), ctx, diag), 1.0), "sw invalid -> 1");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral(""), ctx, diag), 1.0), "sw empty -> 1");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("-2px"), ctx, diag), 1.0), "sw negative -> 1");
  // cssOpacity: a number OR percentage, clamped [0,1]; invalid/non-finite -> 1.
  require(approx(editor::cssOpacity(QStringLiteral("0.7")), 0.7), "opacity 0.7");
  require(approx(editor::cssOpacity(QStringLiteral("0")), 0.0), "opacity 0");
  require(approx(editor::cssOpacity(QStringLiteral("1.7")), 1.0), "opacity 1.7 clamp");
  require(approx(editor::cssOpacity(QStringLiteral("-0.5")), 0.0), "opacity -0.5 clamp");
  require(approx(editor::cssOpacity(QStringLiteral("50%")), 0.5), "opacity 50% -> 0.5");
  require(approx(editor::cssOpacity(QStringLiteral("150%")), 1.0), "opacity 150% -> 1 clamp");
  require(approx(editor::cssOpacity(QStringLiteral("abc")), 1.0), "opacity invalid -> 1");
  require(approx(editor::cssOpacity(QStringLiteral("")), 1.0), "opacity empty -> 1");
  // cssFontSizePx: a UNIT is required (a bare number incl. exponent like "1e2" is
  //   invalid -> inherited 16); em x16; % of parent (emPx); vw of 800; ex/ch
  //   font-relative; negative/invalid/empty -> 16.
  require(approx(editor::cssFontSizePx(QStringLiteral("25px"), ctx), 25.0), "fs 25px");
  require(approx(editor::cssFontSizePx(QStringLiteral("25"), ctx), 16.0), "fs unitless 25 -> 16");
  require(approx(editor::cssFontSizePx(QStringLiteral("1e2"), ctx), 16.0), "fs bare 1e2 -> 16 (invalid font-size)");
  require(approx(editor::cssFontSizePx(QStringLiteral("1e2px"), ctx), 100.0), "fs 1e2px -> 100");
  require(approx(editor::cssFontSizePx(QStringLiteral("0px"), ctx), 0.0), "fs 0px (zero)");
  require(approx(editor::cssFontSizePx(QStringLiteral("3em"), ctx), 48.0), "fs 3em -> 48");
  require(approx(editor::cssFontSizePx(QStringLiteral("200%"), ctx), 32.0), "fs 200% -> 32 (x parent 16)");
  require(approx(editor::cssFontSizePx(QStringLiteral("50%"), ctx), 8.0), "fs 50% -> 8");
  require(approx(editor::cssFontSizePx(QStringLiteral("10vw"), ctx), 80.0), "fs 10vw -> 80");
  require(approx(editor::cssFontSizePx(QStringLiteral("10ex"), ctx), 10.0 * ctx.exPx), "fs 10ex -> 10*xHeight");
  require(approx(editor::cssFontSizePx(QStringLiteral("abc"), ctx), 16.0), "fs invalid -> 16");
  require(approx(editor::cssFontSizePx(QStringLiteral(""), ctx), 16.0), "fs empty -> 16");
  // parseFontSizeNumber: upstream parseInt (leading int, truncates decimals,
  //   ignores unit); no leading int -> default 2. toLongLong (not toInt) so
  //   values > INT_MAX parse like JS parseInt. Drives the outer-ring radius.
  require(approx(editor::parseFontSizeNumber(QStringLiteral("2px")), 2.0), "fsn 2px");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("3em")), 3.0), "fsn 3em -> 3");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("1.7")), 1.0), "fsn 1.7 -> 1 (trunc)");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("abc")), 2.0), "fsn abc -> default 2");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("0")), 0.0), "fsn 0");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("3000000000px")), 3000000000.0),
          "fsn 3000000000px -> 3e9 (no INT_MAX cap)");
  // parseFontSizeNumber JSON-type branch: upstream returns a NUMBER verbatim but
  // parseInt-truncates a STRING. Probed: number 1.7 -> r 185.85; "1.7" -> r 185.5.
  require(approx(editor::parseFontSizeNumber(QJsonValue(1.7), QStringLiteral("2px")), 1.7),
          "fsn JSON number 1.7 -> 1.7");
  require(approx(editor::parseFontSizeNumber(QJsonValue(QStringLiteral("1.7")), QStringLiteral("2px")), 1.0),
          "fsn JSON string \"1.7\" -> 1 (parseInt)");
  require(approx(editor::parseFontSizeNumber(QJsonValue(QJsonValue::Undefined), QStringLiteral("2px")), 2.0),
          "fsn absent -> fallback parseFontSizeNumber(\"2px\")=2");

  editor::MermaidRenderCache cache;

  // --- 1. default theme: scene.style == resolved model, all fields wired ---
  {
    const flowtheme::FlowThemeVariables model =
        flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default);
    const QString src = QStringLiteral("pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* s = renderPie(cache, src);
    const pie::PieSceneStyle& st = s->style;
    require(st.palette.size() == 12, "default palette size 12");
    for (int i = 0; i < 12; ++i)
      require(st.palette.at(i) == model.pie[i],
              QStringLiteral("default palette%1 = themeVars.pie%1").arg(i));
    require(st.outerStrokeColor == model.pieOuterStrokeColor, "default outerStrokeColor wired");
    require(st.sliceStrokeColor == model.pieStrokeColor, "default sliceStrokeColor wired");
    require(st.titleColor == model.pieTitleTextColor, "default titleColor = pieTitleTextColor");
    require(st.sectionTextColor == model.pieSectionTextColor, "default sectionTextColor wired");
    require(st.legendTextColor == model.pieLegendTextColor, "default legendTextColor wired");
    require(approx(st.outerStrokeWidth, 2.0), "default outerStrokeWidth 2.0");
    require(approx(st.outerStrokeWidthGeom, 2.0), "default outerStrokeWidthGeom 2.0 (parseFontSize)");
    require(approx(st.sliceStrokeWidth, 2.0), "default sliceStrokeWidth 2.0");
    require(approx(st.pieOpacity, 0.7), "default pieOpacity 0.7");
    require(approx(st.titleFontSize, 25.0), "default titleFontSize 25.0");
    require(approx(st.sectionFontSize, 17.0), "default sectionFontSize 17.0");
    require(approx(st.legendFontSize, 17.0), "default legendFontSize 17.0");
  }

  // --- 2. dark theme (self-declaring): palette[11] EMPTY (upstream contract) ---
  {
    const QString src = QStringLiteral(
        "%%{init: {\"theme\":\"dark\"}}%%\npie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* s = renderPie(cache, src);
    require(s->style.palette.at(0) == QLatin1String("#0b0000"), "dark palette1");
    require(s->style.palette.at(11).isEmpty(),
            "dark palette12 EMPTY at TCL=12 (upstream dark only emits pie1..pie11)");
  }

  // --- 3. source-entry overrides reach scene.style ---
  {
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {"
        "\"pie1\": \"#abcdef\", \"pieStrokeWidth\": \"5px\", "
        "\"pieOuterStrokeWidth\": \"7px\", \"pieOpacity\": \"0.4\", "
        "\"pieTitleTextSize\": \"30px\", \"pieSectionTextColor\": \"#123456\", "
        "\"pieStrokeColor\": \"#ff0000\"}}}%%\n"
        "pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* s = renderPie(cache, src);
    const pie::PieSceneStyle& st = s->style;
    require(st.palette.at(0) == QLatin1String("#abcdef"), "override pie1 reached palette");
    require(approx(st.sliceStrokeWidth, 5.0), "override pieStrokeWidth reached scene");
    require(approx(st.outerStrokeWidth, 7.0), "override pieOuterStrokeWidth (paint) reached scene");
    require(approx(st.outerStrokeWidthGeom, 7.0), "override pieOuterStrokeWidthGeom (parseFontSize) reached scene");
    require(approx(st.pieOpacity, 0.4), "override pieOpacity reached scene");
    require(approx(st.titleFontSize, 30.0), "override pieTitleTextSize reached scene");
    require(st.sectionTextColor == QLatin1String("#123456"), "override pieSectionTextColor reached scene");
    require(st.sliceStrokeColor == QLatin1String("#ff0000"), "override pieStrokeColor reached scene");
  }

  // --- 4. THEME_COLOR_LIMIT flows through to the palette in production ---
  // dark TCL=13 -> pie12 = cScale12 (#010029); TCL=12 (default) -> pie12 empty.
  {
    const QString src13 = QStringLiteral(
        "%%{init: {\"theme\":\"dark\",\"themeVariables\": {\"THEME_COLOR_LIMIT\": 13}}}%%\n"
        "pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* s13 = renderPie(cache, src13);
    require(s13->style.palette.at(11) == QLatin1String("#010029"),
            "dark TCL=13 palette12 = cScale12 #010029");
  }

  // --- 5. an override reaches the ACTUAL painted RGBA (pie1 red, fully opaque) ---
  {
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pie1\": \"#ff0000\", \"pieOpacity\": \"1.0\"}}}%%\n"
        "pie title T\n\"A\" : 50\n\"B\" : 50");
    const editor::MermaidPngRenderResult result =
        editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0);
    const QImage img = decodePng(result.dataUrl);
    require(!img.isNull(), "override RGBA case must render a PNG");
    int redPixels = 0;
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x) {
        const QColor c = img.pixelColor(x, y);
        if (c.red() > 200 && c.green() < 50 && c.blue() < 50) ++redPixels;
      }
    // Slice A is ~half the pie (radius 185 -> ~50k px); 5000 is a safe floor that
    // proves the pie1 fill actually painted, not just reached the scene struct.
    require(redPixels > 5000,
            QStringLiteral("pie1=#ff0000 must paint red pixels (got %1)").arg(redPixels));
  }

  // --- 6. outer-circle radius geometry: JSON NUMBER vs STRING ---
  // upstream parseFontSize() returns a number verbatim but parseInt-truncates a
  // string; the geom is read from the RAW QJsonValue. Probed r: 1.7 -> 185.85,
  // "1.7" -> 185.5 (radius 185 + geom/2).
  {
    const QString num = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieOuterStrokeWidth\": 1.7}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* sn = renderPie(cache, num);
    require(approx(sn->style.outerStrokeWidthGeom, 1.7), "number 1.7 -> geom 1.7 (verbatim)");
    const QString str = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieOuterStrokeWidth\": \"1.7\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* ss = renderPie(cache, str);
    require(approx(ss->style.outerStrokeWidthGeom, 1.0), "string \"1.7\" -> geom 1 (parseInt trunc)");
  }

  // --- 7. zero stroke-width paints NO stroke (Qt would draw a 1px cosmetic) ---
  {
    const QColor red(255, 0, 0);
    const auto redRingPx = [&](const QString& width) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": {\"pieOuterStrokeColor\": \"red\", \"pieOuterStrokeWidth\": %1}}}%%\n"
          " pie title T\n\"A\" : 50\n\"B\" : 50").arg(width);
      return countColor(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), red, 40);
    };
    require(redRingPx(QStringLiteral("\"0\"")) < 50, "pieOuterStrokeWidth 0 -> no ring ink");
    require(redRingPx(QStringLiteral("\"2px\"")) > 500, "pieOuterStrokeWidth 2px -> red ring present");
  }

  // --- 8. zero font-size paints NO text + emits no setPixelSize(0) warning ---
  {
    const QColor red(255, 0, 0);
    const auto redTextPx = [&](const QString& tvJson) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": %1}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(tvJson);
      return countColor(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), red, 40);
    };
    require(redTextPx(QStringLiteral("{\"pieTitleTextColor\":\"red\",\"pieTitleTextSize\":\"0px\"}")) < 10,
            "zero title font -> no title text");
    require(redTextPx(QStringLiteral("{\"pieTitleTextColor\":\"red\",\"pieTitleTextSize\":\"25px\"}")) > 10,
            "title font 25 -> title text present");
    require(redTextPx(QStringLiteral("{\"pieSectionTextColor\":\"red\",\"pieSectionTextSize\":\"0px\"}")) < 10,
            "zero section font -> no section text");
    require(redTextPx(QStringLiteral("{\"pieSectionTextColor\":\"red\",\"pieSectionTextSize\":\"17px\"}")) > 10,
            "section font 17 -> section text present");
    require(redTextPx(QStringLiteral("{\"pieLegendTextColor\":\"red\",\"pieLegendTextSize\":\"0px\"}")) < 10,
            "zero legend font -> no legend text");
    require(redTextPx(QStringLiteral("{\"pieLegendTextColor\":\"red\",\"pieLegendTextSize\":\"17px\"}")) > 10,
            "legend font 17 -> legend text present");
    // No setPixelSize(0) warning for any zero font size.
    static QStringList captured;
    captured.clear();
    const auto handler = [](QtMsgType, const QMessageLogContext&, const QString& msg) { captured << msg; };
    const auto oldHandler = qInstallMessageHandler(handler);
    const QString allZero = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieTitleTextSize\": \"0px\", \"pieSectionTextSize\": \"0px\", "
        "\"pieLegendTextSize\": \"0px\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(allZero, 1.0).dataUrl);
    qInstallMessageHandler(oldHandler);
    for (const QString& w : captured)
      require(!w.contains(QStringLiteral("pixel size"), Qt::CaseInsensitive),
              QStringLiteral("zero font size emitted a pixel-size warning: %1").arg(w));
  }

  // --- 9. SVG <paint> resolution: none / currentColor / invalid stroke ---
  // (source-entry only; initialize() rejects these). Probed vs 11.16.0
  // (scripts/probe_mermaid_paint_resolution.mjs).
  {
    const QColor red(255, 0, 0), black(0, 0, 0);
    const auto pngPx = [&](const QString& tvJson, const QColor& target, int tol) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": %1}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(tvJson);
      return countColor(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), target, tol);
    };
    // fill: none -> NoBrush (no slice fill); currentColor -> black slice fill.
    require(pngPx(QStringLiteral("{\"pie1\":\"red\"}"), red, 40) > 5000, "pie1=red -> red slice");
    require(pngPx(QStringLiteral("{\"pie1\":\"none\"}"), red, 40) < 200, "pie1=none -> no slice fill");
    // opacity 1.0 so the fill is pure black regardless of the canvas background.
    const int blackCurrent = pngPx(QStringLiteral("{\"pie1\":\"currentColor\",\"pieOpacity\":\"1.0\"}"), black, 5);
    const int blackNone = pngPx(QStringLiteral("{\"pie1\":\"none\",\"pieOpacity\":\"1.0\"}"), black, 5);
    require(blackCurrent > blackNone + 5000, "pie1=currentColor -> black slice fill (currentColor=black)");
    // stroke (outer ring): none / invalid -> no stroke; a real color draws it.
    const auto ringPx = [&](const QString& color) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": {\"pieOuterStrokeColor\": %1, \"pieOuterStrokeWidth\": \"2px\"}}}%%\n"
          " pie title T\n\"A\" : 50\n\"B\" : 50").arg(color);
      return countColor(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), red, 40);
    };
    require(ringPx(QStringLiteral("\"red\"")) > 500, "outer stroke red present");
    require(ringPx(QStringLiteral("\"none\"")) < 50, "outer stroke none -> no stroke");
    require(ringPx(QStringLiteral("\"garbage\"")) < 50, "invalid outer stroke -> no stroke (SVG initial none)");
  }

  // --- 10. source-entry CSS units reach the scene (production path) ---
  // ex/ch/vw/vh/% and the bare-exponent font-size, through MermaidRenderCache.
  {
    const auto titleFs = [&](const QString& jsonSize) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": {\"pieTitleTextSize\": %1}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(jsonSize);
      return renderPie(cache, src)->style.titleFontSize;
    };
    require(approx(titleFs(QStringLiteral("\"10vw\"")), 80.0), "prod pieTitleTextSize 10vw -> 80");
    require(approx(titleFs(QStringLiteral("\"10vh\"")), 60.0), "prod pieTitleTextSize 10vh -> 60");
    require(approx(titleFs(QStringLiteral("\"200%\"")), 32.0), "prod pieTitleTextSize 200% -> 32");
    require(approx(titleFs(QStringLiteral("\"1e2\"")), 16.0), "prod pieTitleTextSize bare 1e2 -> 16 (invalid)");
    require(approx(titleFs(QStringLiteral("\"1e2px\"")), 100.0), "prod pieTitleTextSize 1e2px -> 100");
    require(titleFs(QStringLiteral("\"10ex\"")) > 50.0, "prod pieTitleTextSize 10ex -> font-relative (>50)");
    // stroke-width % uses the scene-bounds normalized diagonal.
    const QString sw = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieStrokeWidth\": \"10%\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* s = renderPie(cache, sw);
    const qreal diag = std::sqrt(s->bounds.width() * s->bounds.width() +
                                 s->bounds.height() * s->bounds.height()) /
                       std::sqrt(2.0);
    require(approx(s->style.sliceStrokeWidth, 0.10 * diag),
            "prod pieStrokeWidth 10% -> 0.1 x SVG diagonal");
  }

  qDebug().noquote() << "MermaidPieThemeWiringTest: pie adapter consumes resolved theme (+overrides, RGBA, TCL, zero, paint, units)";
  return 0;
}
