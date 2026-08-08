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
#include <QString>

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
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  (void)argv;

  // --- numeric helpers match the probed browser semantics ---
  // parseCssPx: px or unitless (== px), 0 accepted; em/invalid/empty/negative -> fallback.
  require(approx(editor::parseCssPx(QStringLiteral("2px"), 9.0), 2.0), "parseCssPx 2px");
  require(approx(editor::parseCssPx(QStringLiteral("2"), 9.0), 2.0), "parseCssPx unitless 2");
  require(approx(editor::parseCssPx(QStringLiteral("0px"), 9.0), 0.0), "parseCssPx 0px (zero)");
  require(approx(editor::parseCssPx(QStringLiteral("0"), 9.0), 0.0), "parseCssPx 0 (zero)");
  require(approx(editor::parseCssPx(QStringLiteral("1.7"), 9.0), 1.7), "parseCssPx unitless 1.7");
  require(approx(editor::parseCssPx(QStringLiteral("25px"), 9.0), 25.0), "parseCssPx 25px");
  require(approx(editor::parseCssPx(QStringLiteral("3em"), 9.0), 9.0), "parseCssPx em -> fallback");
  require(approx(editor::parseCssPx(QStringLiteral("abc"), 9.0), 9.0), "parseCssPx invalid -> fallback");
  require(approx(editor::parseCssPx(QStringLiteral(""), 9.0), 9.0), "parseCssPx empty -> fallback");
  require(approx(editor::parseCssPx(QStringLiteral("-2px"), 9.0), 9.0), "parseCssPx negative -> fallback");
  // opacityValue: unitless, clamped to [0,1]; invalid/empty -> fallback.
  require(approx(editor::opacityValue(QStringLiteral("0.7"), 0.5), 0.7), "opacity 0.7");
  require(approx(editor::opacityValue(QStringLiteral("0"), 0.5), 0.0), "opacity 0");
  require(approx(editor::opacityValue(QStringLiteral("1.7"), 0.5), 1.0), "opacity 1.7 clamp");
  require(approx(editor::opacityValue(QStringLiteral("-0.5"), 0.5), 0.0), "opacity -0.5 clamp");
  require(approx(editor::opacityValue(QStringLiteral("abc"), 0.5), 0.5), "opacity invalid -> fallback");
  require(approx(editor::opacityValue(QStringLiteral(""), 0.5), 0.5), "opacity empty -> fallback");

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
    require(approx(st.outerStrokeWidth, 7.0), "override pieOuterStrokeWidth reached scene");
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

  qDebug().noquote() << "MermaidPieThemeWiringTest: pie adapter consumes resolved theme (+overrides, RGBA, TCL)";
  return 0;
}
