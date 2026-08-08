// Quadrant adapter theme-wiring: verifies the PRODUCTION path
// (MermaidRenderCache -> QuadrantDiagramAdapter -> QuadrantScene) consumes the
// resolved FlowThemeVariables for every quadrant fill / text fill / point fill /
// axis + title text / border instead of the old default/dark special-casing.
// Complements the theme-MODEL tests in MermaidThemeTest.
//
// Covers (Codex adapter spec): (1) default-theme scene.style == resolved model;
// (2) a non-default (dark) theme via a self-declaring source (no hardcoded dark
// block); (3) source-entry `%%{init}%%` overrides for quadrant fill / text /
// border reach scene.style; (4) an override reaches the ACTUAL painted RGBA.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/quadrant/QuadrantScene.h"
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

QImage decodePng(const QString& dataUrl) {
  const qsizetype comma = dataUrl.indexOf(QLatin1Char(','));
  QImage img;
  img.loadFromData(QByteArray::fromBase64(dataUrl.mid(comma + 1).toLatin1()), "PNG");
  return img;
}

const quadrant::QuadrantScene* renderQuad(editor::MermaidRenderCache& cache, const QString& source) {
  const auto entry = cache.getSync(cache.makeKey(source), source);
  const auto* scene = dynamic_cast<const quadrant::QuadrantScene*>(entry.scene.get());
  require(entry.status == editor::MermaidRenderStatus::Ready && scene != nullptr,
          QStringLiteral("quadrant render failed: ") + entry.errorMessage);
  return scene;
}

const QString kSrc = QStringLiteral(
    "quadrantChart\ntitle T\nx-axis L --> R\ny-axis B --> T\n"
    "quadrant-1 Q1\nquadrant-2 Q2\nquadrant-3 Q3\nquadrant-4 Q4\n\"P\": [0.5, 0.5]");
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  (void)argv;

  editor::MermaidRenderCache cache;

  // --- 1. default theme: every scene.style field == resolved model ---
  {
    const flowtheme::FlowThemeVariables model =
        flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default);
    const quadrant::QuadrantScene* s = renderQuad(cache, kSrc);
    const quadrant::QuadrantSceneStyle& st = s->style;
    require(st.quadrant1Fill == model.quadrant[0] && st.quadrant2Fill == model.quadrant[1] &&
                st.quadrant3Fill == model.quadrant[2] && st.quadrant4Fill == model.quadrant[3],
            "default quadrant fills wired from themeVars.quadrant[]");
    require(st.quadrant1TextFill == model.quadrantText[0] && st.quadrant2TextFill == model.quadrantText[1] &&
                st.quadrant3TextFill == model.quadrantText[2] && st.quadrant4TextFill == model.quadrantText[3],
            "default quadrant text fills wired from themeVars.quadrantText[]");
    require(st.quadrantPointFill == model.quadrantPointFill, "default quadrantPointFill wired (NaN string verbatim)");
    require(st.quadrantPointTextFill == model.quadrantPointTextFill, "default quadrantPointTextFill wired");
    require(st.quadrantXAxisTextFill == model.quadrantXAxisTextFill, "default quadrantXAxisTextFill wired");
    require(st.quadrantYAxisTextFill == model.quadrantYAxisTextFill, "default quadrantYAxisTextFill wired");
    require(st.quadrantInternalBorderStrokeFill == model.quadrantInternalBorderStrokeFill,
            "default quadrantInternalBorderStrokeFill wired");
    require(st.quadrantExternalBorderStrokeFill == model.quadrantExternalBorderStrokeFill,
            "default quadrantExternalBorderStrokeFill wired");
    require(st.quadrantTitleFill == model.quadrantTitleFill, "default quadrantTitleFill wired");
  }

  // --- 2. dark theme (self-declaring): fields == resolved Dark model (no
  //         hardcoded dark block; the old if(dark) literals are gone) ---
  {
    const flowtheme::FlowThemeVariables model =
        flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Dark);
    const QString src = QStringLiteral("%%{init: {\"theme\":\"dark\"}}%%\n") + kSrc;
    const quadrant::QuadrantScene* s = renderQuad(cache, src);
    const quadrant::QuadrantSceneStyle& st = s->style;
    require(st.quadrant1Fill == model.quadrant[0], "dark quadrant1Fill from model (not hardcoded)");
    require(st.quadrantPointFill == model.quadrantPointFill, "dark quadrantPointFill from model");
    require(st.quadrantExternalBorderStrokeFill == model.quadrantExternalBorderStrokeFill,
            "dark external border from model");
    require(st.quadrantTitleFill == model.quadrantTitleFill, "dark title fill from model");
  }

  // --- 3. source-entry overrides reach scene.style (fill / text / border) ---
  {
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {"
        "\"quadrant1Fill\": \"#abcdef\", \"quadrant1TextFill\": \"#111111\", "
        "\"quadrantExternalBorderStrokeFill\": \"#222222\", "
        "\"quadrantInternalBorderStrokeFill\": \"#333333\", "
        "\"quadrantTitleFill\": \"#444444\"}}}%%\n") + kSrc;
    const quadrant::QuadrantScene* s = renderQuad(cache, src);
    const quadrant::QuadrantSceneStyle& st = s->style;
    require(st.quadrant1Fill == QLatin1String("#abcdef"), "override quadrant1Fill reached scene");
    require(st.quadrant1TextFill == QLatin1String("#111111"), "override quadrant1TextFill reached scene");
    require(st.quadrantExternalBorderStrokeFill == QLatin1String("#222222"),
            "override external border reached scene");
    require(st.quadrantInternalBorderStrokeFill == QLatin1String("#333333"),
            "override internal border reached scene");
    require(st.quadrantTitleFill == QLatin1String("#444444"), "override quadrantTitleFill reached scene");
  }

  // --- 4. an override reaches the ACTUAL painted RGBA (quadrant-1 fill red) ---
  {
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"quadrant1Fill\": \"#ff0000\"}}}%%\n") + kSrc;
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
    // quadrant-1 is a full quadrant rect (~quarter of a 500x500 chart); 3000 is
    // a safe floor proving the fill painted, not just reached the scene struct.
    require(redPixels > 3000,
            QStringLiteral("quadrant1Fill=#ff0000 must paint red pixels (got %1)").arg(redPixels));
  }

  qDebug().noquote() << "MermaidQuadrantThemeWiringTest: quadrant adapter consumes resolved theme (+overrides, RGBA)";
  return 0;
}
