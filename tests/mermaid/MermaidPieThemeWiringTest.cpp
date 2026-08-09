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

#include <QFont>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <algorithm>
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
  // Shared Noto Sans @16 metrics for independently re-deriving ex/ch expectations
  // (sub-px root, capped root). Hoisted so the per-section blocks don't shadow it.
  QFont refFont(QStringLiteral("Noto Sans"));
  refFont.setPixelSize(16);
  const QFontMetricsF refM(refFont);
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
  // cssFontSizePx: a UNIT is required (a bare number incl. exponent like "1e2" or
  //   "25" is invalid -> INHERITS ctx.emPx); em/rem x ctx.emPx; % of parent
  //   (ctx.emPx); vw of 800; ex/ch font-relative; negative/invalid/empty ->
  //   ctx.emPx. ctx.emPx=16 here, so the inherited fallbacks are 16.
  require(approx(editor::cssFontSizePx(QStringLiteral("25px"), ctx), 25.0), "fs 25px");
  require(approx(editor::cssFontSizePx(QStringLiteral("25"), ctx), 16.0), "fs unitless 25 -> ctx.emPx 16");
  require(approx(editor::cssFontSizePx(QStringLiteral("1e2"), ctx), 16.0), "fs bare 1e2 -> ctx.emPx 16 (invalid)");
  require(approx(editor::cssFontSizePx(QStringLiteral("1e2px"), ctx), 100.0), "fs 1e2px -> 100");
  require(approx(editor::cssFontSizePx(QStringLiteral("0px"), ctx), 0.0), "fs 0px (zero)");
  require(approx(editor::cssFontSizePx(QStringLiteral("3em"), ctx), 48.0), "fs 3em -> 48");
  require(approx(editor::cssFontSizePx(QStringLiteral("200%"), ctx), 32.0), "fs 200% -> 32 (x parent 16)");
  require(approx(editor::cssFontSizePx(QStringLiteral("50%"), ctx), 8.0), "fs 50% -> 8");
  require(approx(editor::cssFontSizePx(QStringLiteral("10vw"), ctx), 80.0), "fs 10vw -> 80");
  require(approx(editor::cssFontSizePx(QStringLiteral("10ex"), ctx), 10.0 * ctx.exPx), "fs 10ex -> 10*xHeight");
  require(approx(editor::cssFontSizePx(QStringLiteral("abc"), ctx), 16.0), "fs invalid -> ctx.emPx 16");
  require(approx(editor::cssFontSizePx(QStringLiteral(""), ctx), 16.0), "fs empty -> ctx.emPx 16");
  // ctx.emPx cascade (P1 #2): with a neo-like 14px context the invalid/bare/
  // negative/empty fallback is ctx.emPx (14), and %/em are relative to 14 -- NOT
  // a hardcoded 16. Probed vs 11.16.0 (scripts/probe_mermaid_pie_fontsize_cascade.mjs).
  const muffin::CssLengthContext ctx14 =
      editor::pieCssLengthContext(QStringLiteral("Noto Sans"), 14.0);
  require(approx(editor::cssFontSizePx(QStringLiteral("abc"), ctx14), 14.0), "fs14 invalid -> ctx.emPx 14");
  require(approx(editor::cssFontSizePx(QStringLiteral("25"), ctx14), 14.0), "fs14 bare -> ctx.emPx 14");
  require(approx(editor::cssFontSizePx(QStringLiteral("-2px"), ctx14), 14.0), "fs14 negative -> ctx.emPx 14");
  require(approx(editor::cssFontSizePx(QStringLiteral(""), ctx14), 14.0), "fs14 empty -> ctx.emPx 14");
  require(approx(editor::cssFontSizePx(QStringLiteral("200%"), ctx14), 28.0), "fs14 200% -> 28 (x 14)");
  require(approx(editor::cssFontSizePx(QStringLiteral("3em"), ctx14), 42.0), "fs14 3em -> 42 (x 14)");
  require(approx(editor::cssFontSizePx(QStringLiteral("25px"), ctx14), 25.0), "fs14 valid 25px -> 25");
  // ctx.emPx == 0 is a VALID zero (root fontSize "0px"), NOT coerced to 16: em/%
  // collapse to 0 and ex/ch are 0 (no 0px QFont built). Probed vs 11.16.0.
  const muffin::CssLengthContext ctx0 =
      editor::pieCssLengthContext(QStringLiteral("Noto Sans"), 0.0);
  require(approx(ctx0.emPx, 0.0) && approx(ctx0.exPx, 0.0) && approx(ctx0.chPx, 0.0),
          "ctx0 preserves zero em/ex/ch (no 16 coercion, no 0px QFont)");
  require(approx(editor::cssFontSizePx(QStringLiteral("200%"), ctx0), 0.0), "fs0 200% -> 0");
  require(approx(editor::cssFontSizePx(QStringLiteral("3em"), ctx0), 0.0), "fs0 3em -> 0");
  require(approx(editor::cssFontSizePx(QStringLiteral("abc"), ctx0), 0.0), "fs0 invalid -> 0");
  require(approx(editor::cssFontSizePx(QStringLiteral("25"), ctx0), 0.0), "fs0 bare -> 0");
  require(approx(editor::cssFontSizePx(QStringLiteral("25px"), ctx0), 25.0), "fs0 valid 25px -> 25");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("3em"), ctx0, 500.0), 0.0), "sw0 3em -> 0");
  // ctx.emPx is a SUB-PIXEL positive root (e.g. "0.4px"): NOT zeroed by
  // qRound(0.4)=0. em/% resolve relative to 0.4, and ex/ch are the font's metrics
  // at the ACTUAL 0.4px size -- font metrics scale linearly, so exPx =
  // xHeight(16)*0.4/16 (independently re-derived here from a fresh 16px QFont).
  // Probed vs 11.16.0: root 0.4px + pieStrokeWidth 10ex -> 2.0918px = 10 * exPx.
  const muffin::CssLengthContext ctx04 =
      editor::pieCssLengthContext(QStringLiteral("Noto Sans"), 0.4);
  const qreal scale04 = 0.4 / 16.0;
  require(approx(ctx04.emPx, 0.4), "ctx04 preserves sub-px emPx 0.4");
  require(approx(ctx04.exPx, refM.xHeight() * scale04),
          "ctx04 exPx = xHeight(16)*0.4/16 (linear, not zeroed)");
  require(approx(ctx04.chPx, refM.horizontalAdvance(QChar('0')) * scale04),
          "ctx04 chPx = advance('0')(16)*0.4/16 (linear, not zeroed)");
  require(ctx04.exPx > 0.0 && ctx04.chPx > 0.0, "ctx04 ex/ch non-zero at sub-px root");
  require(approx(editor::cssFontSizePx(QStringLiteral("1em"), ctx04), 0.4), "fs04 1em -> 0.4");
  require(approx(editor::cssFontSizePx(QStringLiteral("200%"), ctx04), 0.8), "fs04 200% -> 0.8");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10ex"), ctx04, 500.0), 10.0 * ctx04.exPx),
          "sw04 10ex -> 10*exPx (sub-px, non-zero)");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("10ch"), ctx04, 500.0), 10.0 * ctx04.chPx),
          "sw04 10ch -> 10*chPx (sub-px, non-zero)");
  // Chromium used-value saturation caps (scripts/probe_mermaid_pie_length_clamp.mjs):
  // font-size computed value caps at 10000px (min(v,10000); NOT floored -- 9999.9 is
  // kept), stroke-width caps at the CSS Typed OM used value 33554428px.
  // Prevents geometry divergence AND int overflow downstream (qRound/setPixelSize).
  // The % branch is capped by the same std::min. ctx.emPx=16, diag=500 here.
  require(approx(editor::cssFontSizePx(QStringLiteral("9999px"), ctx), 9999.0), "fs 9999px (under cap)");
  require(approx(editor::cssFontSizePx(QStringLiteral("10000px"), ctx), 10000.0), "fs 10000px (at cap)");
  require(approx(editor::cssFontSizePx(QStringLiteral("10001px"), ctx), 10000.0), "fs 10001px -> cap 10000");
  require(approx(editor::cssFontSizePx(QStringLiteral("1e9px"), ctx), 10000.0), "fs 1e9px -> cap 10000");
  require(approx(editor::cssFontSizePx(QStringLiteral("2000vw"), ctx), 10000.0), "fs 2000vw (16000) -> cap 10000");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("33554426px"), ctx, diag), 33554426.0), "sw below cap preserved");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("33554428px"), ctx, diag), 33554428.0), "sw at Typed OM cap preserved");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("33554429px"), ctx, diag), 33554428.0), "sw above cap saturated");
  require(approx(editor::cssStrokeWidthPx(QStringLiteral("1e9px"), ctx, diag), 33554428.0), "sw 1e9px -> Chromium cap");
  // parseFontSizeNumber: upstream parseInt (leading int, truncates decimals,
  //   ignores unit); no leading int -> default 2. Parsed as a JS Number (double,
  //   not qint64) so values beyond the qint64 range parse to the nearest double
  //   like JS instead of overflowing -> 2. Drives the outer-ring radius.
  require(approx(editor::parseFontSizeNumber(QStringLiteral("2px")), 2.0), "fsn 2px");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("3em")), 3.0), "fsn 3em -> 3");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("1.7")), 1.0), "fsn 1.7 -> 1 (trunc)");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("abc")), 2.0), "fsn abc -> default 2");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("0")), 0.0), "fsn 0");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("3000000000px")), 3000000000.0),
          "fsn 3000000000px -> 3e9 (no INT_MAX cap)");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("9223372036854775808px")), std::pow(2.0, 63)),
          "fsn 2^63 px -> 2^63 (toDouble, not qint64 overflow -> 2)");
  require(approx(editor::parseFontSizeNumber(QStringLiteral("9007199254740993px")), 9007199254740992.0),
          "fsn 2^53+1 px -> 2^53 (JS Number rounding, not int trunc)");
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

    // pieStrokeWidth % uses the SVG normalized diagonal of the (title-expanded)
    // viewBox. Derive the EXPECTED diagonal INDEPENDENTLY from QFontMetrics (the
    // legend text width + the scene constants) -- NOT from s->bounds -- so this
    // catches a wrong/missing basis (the old assertion read diag back out of the
    // same bounds the adapter used, which was circular and proved nothing). The
    // short title "T" does not expand the viewBox, so bounds == chart+legend.
    const QString swShort = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieStrokeWidth\": \"10%\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* sShort = renderPie(cache, swShort);
    QFont legendFont(sShort->style.fontFamily);
    legendFont.setPixelSize(qRound(sShort->style.legendFontSize));
    const QFontMetrics lfm(legendFont);
    const qreal longestLegend = std::max(
        qreal(lfm.horizontalAdvance(QStringLiteral("A"))),
        qreal(lfm.horizontalAdvance(QStringLiteral("B"))));
    const qreal chartW = sShort->pieWidth + sShort->margin + sShort->legendRectSize +
                         sShort->legendSpacing + longestLegend;
    const qreal expDiagShort =
        std::sqrt(chartW * chartW + sShort->totalHeight * sShort->totalHeight) / std::sqrt(2.0);
    require(approx(sShort->bounds.x(), 0.0) && approx(sShort->bounds.width(), chartW),
            "short title 'T' -> no expansion, bounds = chart+legend width");
    require(approx(sShort->style.sliceStrokeWidth, 0.10 * expDiagShort),
            "prod pieStrokeWidth 10% -> 0.1 x independently-derived SVG diagonal");

    // --- title-driven viewBox expansion (P1 #1, pieRenderer draw() L280-286) ---
    // A title wider than the chart grows the viewBox: viewBoxX = min(0, pieWidth/2
    // - titleW/2) < 0, width = max(chartAndLegendWidth, pieWidth/2 + titleW/2) -
    // viewBoxX. Derive the EXPECTED viewBox from QFontMetrics(title) (NOT from
    // s->bounds) so this catches a missing expansion. Native uses Noto Sans, so
    // the exact width is font-coupled (same stance as the legend); the FORMULA is
    // upstream-verified (scripts/probe_mermaid_pie_viewbox.mjs: viewBoxX/width
    // follow exactly titleLeft/titleRight for a long title).
    const QString longSrc = QStringLiteral(
        "pie title An Extremely Long Pie Chart Title That Exceeds The Chart Width\n\"A\" : 50\n\"B\" : 50");
    const pie::PieScene* sLong = renderPie(cache, longSrc);
    QFont titleFont(sLong->style.fontFamily);
    titleFont.setPixelSize(qRound(sLong->style.titleFontSize));
    const qreal titleW = qreal(QFontMetrics(titleFont).horizontalAdvance(sLong->title));
    const qreal titleLeft = sLong->pieWidth / 2.0 - titleW / 2.0;
    const qreal titleRight = sLong->pieWidth / 2.0 + titleW / 2.0;
    const qreal expX = std::min(0.0, titleLeft);
    const qreal expW = std::max(sLong->totalWidth, titleRight) - expX;
    require(sLong->bounds.x() < 0.0, "long title -> bounds.x()<0 (title drove viewBoxX)");
    require(approx(sLong->bounds.x(), expX), "bounds.x = min(0, pieWidth/2 - titleW/2)");
    require(approx(sLong->bounds.width(), expW), "bounds.width = max(chartW,titleRight) - viewBoxX");
    require(sLong->bounds.width() > sLong->totalWidth, "long title widens bounds beyond chart+legend");
    // The expanded viewBox changes the % stroke basis: a long-title pie's 10%
    // stroke is WIDER than a short-title pie's (larger diagonal). Before the fix
    // the title never expanded the bounds, so both would be equal.
    const QString swLong = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieStrokeWidth\": \"10%\"}}}%%\n pie title An Extremely Long Pie Chart Title That Exceeds The Chart Width\n\"A\" : 50\n\"B\" : 50");
    const qreal longStroke = renderPie(cache, swLong)->style.sliceStrokeWidth;
    require(longStroke > sShort->style.sliceStrokeWidth + 1.0,
            "long title -> larger % stroke basis (title expanded the diagonal)");

    // --- inherited font-size cascade (P1 #2) via the production path ---
    // invalid/bare/negative pieTitleTextSize INHERITS the resolved root font-size
    // (not 16); % and em are relative to it. Probed vs 11.16.0.
    const auto titleFsBlock = [&](const QString& initBlock) {
      const QString src = QStringLiteral("%1\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(initBlock);
      return renderPie(cache, src)->style.titleFontSize;
    };
    require(approx(titleFsBlock(QStringLiteral("%%{init: {\"theme\":\"neo\"}}%%")), 25.0),
            "neo default pieTitleTextSize 25px (pie default, independent of root)");
    require(approx(titleFsBlock(QStringLiteral(
                       "%%{init: {\"theme\":\"neo\",\"themeVariables\": {\"pieTitleTextSize\": \"abc\"}}}%%")), 14.0),
            "neo invalid title size -> inherits root 14");
    require(approx(titleFsBlock(QStringLiteral(
                       "%%{init: {\"theme\":\"neo\",\"themeVariables\": {\"pieTitleTextSize\": \"25\"}}}%%")), 14.0),
            "neo bare title size -> inherits root 14");
    require(approx(titleFsBlock(QStringLiteral(
                       "%%{init: {\"theme\":\"neo\",\"themeVariables\": {\"pieTitleTextSize\": \"-2px\"}}}%%")), 14.0),
            "neo negative title size -> inherits root 14");
    require(approx(titleFsBlock(QStringLiteral(
                       "%%{init: {\"theme\":\"neo\",\"themeVariables\": {\"pieTitleTextSize\": \"200%\"}}}%%")), 28.0),
            "neo 200% title -> 200% of root 14 = 28");
    require(approx(titleFsBlock(QStringLiteral(
                       "%%{init: {\"themeVariables\": {\"fontSize\": \"2em\", \"pieTitleTextSize\": \"200%\"}}}%%")), 64.0),
            "2em root + 200% title -> 200% of 32 = 64");
  }

  // --- 11. root fontSize:"0px" is a VALID zero (not coerced to 16) ---
  // Upstream honors a 0 root: em/%/inherited sizes collapse to 0; a valid px size
  // stays itself. Probed vs 11.16.0 (scripts/probe_mermaid_pie_fontsize_cascade.mjs).
  {
    const auto root0TitleFs = [&](const QString& titleTv) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": {\"fontSize\": \"0px\", %1}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(titleTv);
      return renderPie(cache, src)->style.titleFontSize;
    };
    require(approx(root0TitleFs(QStringLiteral("\"pieTitleTextSize\": \"200%\"")), 0.0),
            "root 0 + title 200% -> 0 (200% of 0)");
    require(approx(root0TitleFs(QStringLiteral("\"pieTitleTextSize\": \"3em\"")), 0.0),
            "root 0 + title 3em -> 0 (3 x 0)");
    require(approx(root0TitleFs(QStringLiteral("\"pieTitleTextSize\": \"abc\"")), 0.0),
            "root 0 + title invalid -> 0 (inherits root 0)");
    require(approx(root0TitleFs(QStringLiteral("\"pieTitleTextSize\": \"25\"")), 0.0),
            "root 0 + title bare 25 -> 0 (inherits root 0)");
    require(approx(root0TitleFs(QStringLiteral("\"pieTitleTextSize\": \"25px\"")), 25.0),
            "root 0 + title valid 25px -> 25 (independent of root)");
    // pieStrokeWidth 3em of root 0 -> 0 (NoPen).
    const QString sw0 = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"fontSize\": \"0px\", \"pieStrokeWidth\": \"3em\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50");
    require(approx(renderPie(cache, sw0)->style.sliceStrokeWidth, 0.0),
            "root 0 + pieStrokeWidth 3em -> 0");
    // A 0 root must not emit a setPixelSize(0) warning (pieCssLengthContext skips
    // the QFont; the painter skips a 0 title/section/legend font).
    static QStringList zeroRootCaptured;
    zeroRootCaptured.clear();
    const auto zeroHandler = [](QtMsgType, const QMessageLogContext&, const QString& msg) { zeroRootCaptured << msg; };
    const auto oldZeroHandler = qInstallMessageHandler(zeroHandler);
    decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(
        QStringLiteral("%%{init: {\"themeVariables\": {\"fontSize\": \"0px\"}}}%%\n pie title T\n\"A\" : 50\n\"B\" : 50"), 1.0).dataUrl);
    qInstallMessageHandler(oldZeroHandler);
    for (const QString& w : zeroRootCaptured)
      require(!w.contains(QStringLiteral("pixel size"), Qt::CaseInsensitive),
              QStringLiteral("root fontSize 0 emitted a pixel-size warning: %1").arg(w));
  }

  // --- 12. a super-long title is NOT clipped to the old fixed 800px drawText rect ---
  // The painter sizes the title rect to the measured width (+ TextDontClip), so a
  // title whose natural advance > 800px renders in full (SVG <text> is never
  // clipped). Scan the painted red title ink span and assert it exceeds 800px.
  {
    const QString longTitle = QStringLiteral("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");  // 80 W's
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"pieTitleTextColor\": \"red\"}}}%%\n pie title %1\n\"A\" : 50\n\"B\" : 50").arg(longTitle);
    const pie::PieScene* s = renderPie(cache, src);
    // Independently confirm the title's natural advance > 800px (else the test
    // would be meaningless -- a <=800px title is never clipped by the old rect).
    QFont titleFont(s->style.fontFamily);
    titleFont.setPixelSize(qRound(s->style.titleFontSize));
    const qreal advance = qreal(QFontMetrics(titleFont).horizontalAdvance(longTitle));
    require(advance > 800.0,
            QStringLiteral("test title advance %1 must exceed 800").arg(advance));
    require(s->titleWidth > 800.0, "scene.titleWidth = measured advance (>800)");
    const QImage img = decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl);
    int minX = img.width(), maxX = -1, minY = img.height(), maxY = -1;
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x) {
        const QColor c = img.pixelColor(x, y);
        if (c.red() > 200 && c.green() < 50 && c.blue() < 50) {
          minX = std::min(minX, x);
          maxX = std::max(maxX, x);
          minY = std::min(minY, y);
          maxY = std::max(maxY, y);
        }
      }
    require(maxX > minX, "red title ink found");
    require(maxX - minX > 800,
            QStringLiteral("title ink span %1 > 800 (not clipped to the fixed rect)").arg(maxX - minX));
    // Upstream's title is an SVG <text y=25> after the group translation, so
    // the no-descender W glyph ink must remain above that baseline. The old
    // QRectF+AlignCenter painter path put this same ink at y=17..34 instead of
    // the browser's y=7..24 while still passing the aggregate pixel IoU.
    require(minY < 12 && maxY <= 25,
            QStringLiteral("title baseline drifted: ink y=%1..%2, expected above SVG y=25")
                .arg(minY)
                .arg(maxY));
  }

  // --- 13. sub-pixel root (0.4px): ex/ch measured at the ACTUAL size, not zeroed ---
  // root "0.4px" + pieStrokeWidth "10ex"/"10ch" resolves to a small NON-zero px.
  // The old qRound(0.4)=0 path zeroed ex/ch -> 0; the linear-scaling fix yields
  // 10 * exPx where exPx = xHeight(16)*0.4/16. Independently re-derived from a
  // fresh 16px QFont (probed vs 11.16.0: browser 10ex -> 2.0918px, 10ch -> 2.09766;
  // native uses Noto Sans so the exact px differs, but the LINEAR FORMULA holds).
  {
    const auto subpxStroke = [&](const QString& sw) {
      const QString src = QStringLiteral(
          "%%{init: {\"themeVariables\": {\"fontSize\": \"0.4px\", \"pieStrokeWidth\": \"%1\"}}}%%\n"
          " pie title T\n\"A\" : 50\n\"B\" : 50").arg(sw);
      return renderPie(cache, src)->style.sliceStrokeWidth;
    };
    const qreal scale = 0.4 / 16.0;
    require(approx(subpxStroke(QStringLiteral("10ex")), 10.0 * refM.xHeight() * scale),
            "root 0.4px + pieStrokeWidth 10ex -> 10*xHeight(16)*0.4/16 (non-zero)");
    require(approx(subpxStroke(QStringLiteral("10ch")), 10.0 * refM.horizontalAdvance(QChar('0')) * scale),
            "root 0.4px + pieStrokeWidth 10ch -> 10*advance('0')(16)*0.4/16 (non-zero)");
    require(approx(subpxStroke(QStringLiteral("1em")), 0.4),
            "root 0.4px + pieStrokeWidth 1em -> 0.4 (em linear in root)");
  }

  // --- 14. Chromium used-value saturation caps on the PRODUCTION path ---
  // font-size caps at 10000px, stroke-width at the Typed OM value 33554428px.
  // A capped root (1e9px -> 10000) feeds em/%/ex/ch children, which re-cap. Probed
  // vs 11.16.0 (scripts/probe_mermaid_pie_length_clamp.mjs); ex/ch independently
  // re-derived from a fresh 16px QFont (Noto Sans != browser trebuchet ms).
  {
    const auto titleFsBlock = [&](const QString& initBlock) {
      const QString src = QStringLiteral("%1\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(initBlock);
      return renderPie(cache, src)->style.titleFontSize;
    };
    const auto swBlock = [&](const QString& initBlock) {
      const QString src = QStringLiteral("%1\n pie title T\n\"A\" : 50\n\"B\" : 50").arg(initBlock);
      return renderPie(cache, src)->style.sliceStrokeWidth;
    };
    // Direct font-size saturation.
    require(approx(titleFsBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"pieTitleTextSize\": \"10000px\"}}}%%")), 10000.0),
            "title 10000px -> 10000 (at cap)");
    require(approx(titleFsBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"pieTitleTextSize\": \"1e9px\"}}}%%")), 10000.0),
            "title 1e9px -> 10000 (capped, no setPixelSize overflow)");
    // Capped root (1e9px -> 10000) feeding em/%: child re-capped to 10000.
    require(approx(titleFsBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"fontSize\": \"1e9px\", \"pieTitleTextSize\": \"3em\"}}}%%")), 10000.0),
            "root 1e9px + title 3em -> 10000 (3*10000 re-capped)");
    require(approx(titleFsBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"fontSize\": \"1e9px\", \"pieTitleTextSize\": \"200%\"}}}%%")), 10000.0),
            "root 1e9px + title 200% -> 10000 (200% of 10000 re-capped)");
    // Direct stroke-width saturation.
    require(approx(swBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"pieStrokeWidth\": \"1e9px\"}}}%%")), 33554428.0),
            "pieStrokeWidth 1e9px -> Chromium Typed OM cap");
    // Capped root feeding ex/ch: linearly scaled at the 10000 root, under the stroke cap.
    const qreal scale10k = 10000.0 / 16.0;
    require(approx(swBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"fontSize\": \"1e9px\", \"pieStrokeWidth\": \"10ex\"}}}%%")),
                    10.0 * refM.xHeight() * scale10k),
            "root 1e9px + pieStrokeWidth 10ex -> 10*xHeight(16)*10000/16 (linear at capped root)");
    require(approx(swBlock(QStringLiteral("%%{init: {\"themeVariables\": {\"fontSize\": \"1e9px\", \"pieStrokeWidth\": \"10ch\"}}}%%")),
                    10.0 * refM.horizontalAdvance(QChar('0')) * scale10k),
            "root 1e9px + pieStrokeWidth 10ch -> 10*advance('0')(16)*10000/16 (linear at capped root)");
  }

  // --- 15. JS config coercion is preserved at the production boundary ---
  // Mermaid consumes these values as JavaScript, not as typed Qt config.
  {
    const auto configured = [&](const QString& json) {
      return renderPie(cache, QStringLiteral("%%{init: {\"pie\": %1}}%%\npie\n\"A\" : 1")
                                  .arg(json));
    };
    require(approx(configured(QStringLiteral("{\"textPosition\":-0.5}"))->textPosition, -0.5),
            "negative textPosition is live");
    require(approx(configured(QStringLiteral("{\"textPosition\":\"0.5\"}"))->textPosition, 0.5),
            "string textPosition uses Number()");
    require(approx(configured(QStringLiteral("{\"donutHole\":\"0.5\"}"))->effectiveDonutHole, 0.5),
            "string donutHole uses Number()");
    require(approx(configured(QStringLiteral("{\"donutHole\":true}"))->effectiveDonutHole, 0.0),
            "boolean true donutHole -> 1 -> outside accepted range -> solid");

    const auto maxWidth = [&](const QString& value) {
      const QString src = QStringLiteral(
          "%%{init: {\"pie\": {\"useMaxWidth\":%1}}}%%\npie\n\"A\" : 1").arg(value);
      return cache.getSync(cache.makeKey(src), src).metadata.svgUseMaxWidth;
    };
    require(!maxWidth(QStringLiteral("0")), "useMaxWidth numeric 0 is falsy");
    require(!maxWidth(QStringLiteral("\"\"")), "useMaxWidth empty string is falsy");
    require(maxWidth(QStringLiteral("\"false\"")), "useMaxWidth non-empty string is truthy");
    require(maxWidth(QStringLiteral("null")), "useMaxWidth null is sanitized to default true");

    const auto highlightClass = [&](const QString& value) {
      const QString src = QStringLiteral(
          "%%{init: {\"pie\": {\"highlightSlice\":%1}}}%%\npie\n\"\" : 1")
                              .arg(value);
      const pie::PieScene* s = renderPie(cache, src);
      return s->slices.first().className;
    };
    require(!highlightClass(QStringLiteral("false")).contains(QStringLiteral("highlighted")),
            "highlightSlice false is not coerced to empty string");
    require(!highlightClass(QStringLiteral("0")).contains(QStringLiteral("highlighted")),
            "highlightSlice zero is not coerced to empty string");
    require(highlightClass(QStringLiteral("null")).contains(QStringLiteral("highlighted")),
            "highlightSlice null falls back to default empty string and matches empty label");
  }

  // --- 16. center/empty canvas formulas reproduce pieRenderer's Math.max ---
  {
    const pie::PieScene* centered = renderPie(
        cache, QStringLiteral("%%{init: {\"pie\": {\"legendPosition\":\"center\"}}}%%\n"
                              "pie title T\n\"A\" : 1\n\"B\" : 1"));
    require(approx(centered->totalWidth, 490.0) && approx(centered->bounds.width(), 490.0),
            "legendPosition center does not add the side legend width");
    const pie::PieScene* empty = renderPie(cache, QStringLiteral("pie"));
    require(std::isinf(empty->longestLegendWidth) && empty->longestLegendWidth < 0.0,
            "empty legend preserves Math.max(...[]) == -Infinity");
    require(approx(empty->bounds.width(), 225.0),
            "empty pie viewBox width is title/right extent 225, not fixed 490/512");
  }

  // --- 17. dark palette's absent pie12 inherits SVG-root fill (#ccc) ---
  {
    const auto darkSlices = [&](int count) {
      QString src = QStringLiteral(
          "%%{init: {\"theme\":\"dark\",\"themeVariables\":{\"pieOpacity\":\"1\"}}}%%\npie");
      for (int i = 0; i < count; ++i)
        src += QStringLiteral("\n\"S%1\" : 1").arg(i + 1);
      return decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl);
    };
    const int c11 = countColor(darkSlices(11), QColor(QStringLiteral("#cccccc")), 5);
    const int c12 = countColor(darkSlices(12), QColor(QStringLiteral("#cccccc")), 5);
    require(c12 > c11 + 3000,
            QStringLiteral("absent dark pie12 fill inherits #ccc (%1 vs %2 pixels)")
                .arg(c12).arg(c11));
  }

  // --- 18. long legend text is baseline-drawn and never clipped at 1000px ---
  {
    const QString label(110, QLatin1Char('W'));
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\":{\"pieLegendTextColor\":\"red\"}}}%%\n"
        "pie\n\"%1\" : 1").arg(label);
    const pie::PieScene* s = renderPie(cache, src);
    require(s->longestLegendWidth > 1000.0, "test legend advance exceeds old 1000px rect");
    const QImage img = decodePng(
        editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl);
    int minX = img.width(), maxX = -1;
    for (int y = 0; y < img.height(); ++y)
      for (int x = 0; x < img.width(); ++x) {
        const QColor c = img.pixelColor(x, y);
        if (c.red() > 200 && c.green() < 50 && c.blue() < 50) {
          minX = std::min(minX, x);
          maxX = std::max(maxX, x);
        }
      }
    require(maxX - minX > 1000,
            QStringLiteral("legend ink span %1 exceeds old clipping rect").arg(maxX - minX));
  }

  // --- 19. fractional fonts remain fractional in measurement and painting ---
  {
    const editor::CssPixelFont f16 = editor::makeCssPixelFont(QStringLiteral("Noto Sans"), 16.0);
    const editor::CssPixelFont f04 = editor::makeCssPixelFont(QStringLiteral("Noto Sans"), 0.4);
    require(f04.scale > 0.0 && f04.horizontalAdvance(QStringLiteral("MMMM")) > 0.0,
            "0.4px font has non-zero scaled metrics");
    require(std::abs(f04.horizontalAdvance(QStringLiteral("MMMM")) /
                         f16.horizontalAdvance(QStringLiteral("MMMM")) - 0.025) < 1e-9,
            "0.4px font advance scales linearly from 16px");
    const pie::PieScene* sub = renderPie(cache, QStringLiteral(
        "%%{init: {\"themeVariables\":{\"pieTitleTextSize\":\"0.4px\"}}}%%\n"
        "pie title MMMM\n\"A\" : 1"));
    require(approx(sub->style.titleFontSize, 0.4) && sub->titleWidth > 0.0,
            "production 0.4px title is measured, not rounded to zero");
  }

  // --- 20. JS Number stringification and CSS font-size keywords ---
  {
    require(editor::jsNumberToString(-0.0) == QStringLiteral("0"), "JS -0 stringifies as 0");
    require(editor::jsNumberToString(1.23456789) == QStringLiteral("1.23456789"),
            "JS shortest finite decimal");
    require(editor::jsNumberToString(1e20) == QStringLiteral("100000000000000000000"),
            "JS uses fixed notation below 1e21");
    require(editor::jsNumberToString(1e21) == QStringLiteral("1e+21"),
            "JS uses scientific notation at 1e21");
    require(editor::jsNumberToString(1e-6) == QStringLiteral("0.000001"),
            "JS uses fixed notation at 1e-6");
    require(editor::jsNumberToString(1e-7) == QStringLiteral("1e-7"),
            "JS uses scientific notation below 1e-6");
    require(approx(editor::cssFontSizePx(QStringLiteral("0"), ctx), 0.0),
            "unitless zero font-size is valid");
    require(approx(editor::cssFontSizePx(QStringLiteral("medium"), ctx14), 16.0),
            "medium keyword is absolute 16px");
    require(approx(editor::cssFontSizePx(QStringLiteral("larger"), ctx), 19.2),
            "larger keyword scales parent");
    require(approx(editor::cssFontSizePx(QStringLiteral("smaller"), ctx), 16.0 / 1.2),
            "smaller keyword scales parent");

    const pie::PieScene* shown = renderPie(
        cache, QStringLiteral("pie showData\n\"A\" : 1.23456789"));
    require(shown->legends.first().text == QStringLiteral("A [1.23456789]"),
            "showData uses JS Number shortest representation");
  }

  qDebug().noquote() << "MermaidPieThemeWiringTest: pie production parity contract passed";
  return 0;
}
