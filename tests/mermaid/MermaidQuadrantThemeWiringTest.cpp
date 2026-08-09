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
#include "mermaid/theme/MermaidColor.h"

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

// Count OPAQUE pixels within `tol` per channel of `target` (transparent pixels
// have RGB (0,0,0) and would falsely match black, so they are excluded).
int countNear(const QImage& img, const QColor& target, int tol) {
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

  // --- 5. default-theme HSL borders actually PAINT as the hsl color (not black) ---
  // The default internal/external borders are hsl() strings (e.g.
  // flowchart-theme.json:749). QColor(QString) cannot parse hsl, so before the
  // color::toQColor wiring they painted black. Sample the top external border's
  // midpoint pixel (borders[0] is the horizontal top edge at y=quadrantTop) and
  // scan for the border color -- both must match the resolved hsl, not black.
  {
    const flowtheme::FlowThemeVariables model =
        flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::Default);
    const quadrant::QuadrantScene* s = renderQuad(cache, kSrc);
    const editor::MermaidPngRenderResult result =
        editor::MermaidRenderCache::renderMermaidSourceToPng(kSrc, 1.0);
    const QImage img = decodePng(result.dataUrl);
    require(!img.isNull(), "default border case must render a PNG");
    const QColor expected = muffin::mermaid::color::toQColor(model.quadrantExternalBorderStrokeFill);
    require(expected != Qt::black, "default external border color is a real hsl, not black");
    // Exact pixel at the top-border midpoint (a 2px line; its center is solid).
    require(!s->borders.isEmpty(), "default quadrant has borders");
    const auto& top = s->borders.first();
    const int bx = static_cast<int>(std::round((top.x1 + top.x2) / 2.0));
    const int by = static_cast<int>(std::round(top.y1));
    const QColor sampled = img.pixelColor(bx, by);
    require(std::abs(sampled.red() - expected.red()) <= 16 &&
                std::abs(sampled.green() - expected.green()) <= 16 &&
                std::abs(sampled.blue() - expected.blue()) <= 16,
            QStringLiteral("default top border pixel (%1,%2) = rgb(%3,%4,%5), expected hsl-resolved rgb(%6,%7,%8)")
                .arg(bx).arg(by)
                .arg(sampled.red()).arg(sampled.green()).arg(sampled.blue())
                .arg(expected.red()).arg(expected.green()).arg(expected.blue()));
    // And the full 2px frame must paint the hsl color (not black): the 4 external
    // borders are ~thousands of px, so >800 within tol proves the hsl painted.
    const int borderPixels = countNear(img, expected, 12);
    require(borderPixels > 800,
            QStringLiteral("default HSL borders must paint (got %1 matching px)").arg(borderPixels));
  }

  // --- 6. a functional-color (hsl) source override reaches the painted borders ---
  // Overrides quadrantExternalBorderStrokeFill with an hsl() value; before the
  // color::toQColor wiring this hsl could not parse, so no magenta would paint.
  {
    const QString src = QStringLiteral(
        "%%{init: {\"themeVariables\": {\"quadrantExternalBorderStrokeFill\": \"hsl(300, 100%, 50%)\"}}}%%\n") + kSrc;
    const editor::MermaidPngRenderResult result =
        editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0);
    const QImage img = decodePng(result.dataUrl);
    require(!img.isNull(), "hsl border override case must render a PNG");
    const QColor magenta = QColor::fromHslF(300.0f / 360.0f, 1.0f, 0.5f);
    const int magentaPixels = countNear(img, magenta, 25);
    // The 4 external borders (2px frame, ~thousands of px) paint magenta.
    require(magentaPixels > 800,
            QStringLiteral("hsl(300,100%,50%) external-border override must paint (got %1 px)").arg(magentaPixels));
  }

  // --- 7. SVG <paint> resolution: none / currentColor / invalid border ---
  // (source-entry only; initialize() rejects these). Probed vs 11.16.0
  // (scripts/probe_mermaid_paint_resolution.mjs): fill none->NoBrush,
  // currentColor->black, stroke none/garbage->NoPen.
  {
    const QColor red(255, 0, 0), black(0, 0, 0);
    const auto pngPx = [&](const QString& tvJson, const QColor& target, int tol) {
      const QString src = QStringLiteral("%%{init: {\"themeVariables\": %1}}%%\n").arg(tvJson) + kSrc;
      return countNear(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), target, tol);
    };
    require(pngPx(QStringLiteral("{\"quadrant1Fill\":\"red\"}"), red, 40) > 3000, "quadrant1Fill=red -> red quadrant");
    require(pngPx(QStringLiteral("{\"quadrant1Fill\":\"none\"}"), red, 40) < 200, "quadrant1Fill=none -> no fill");
    require(pngPx(QStringLiteral("{\"quadrant1Fill\":\"currentColor\"}"), black, 5) >
                pngPx(QStringLiteral("{\"quadrant1Fill\":\"none\"}"), black, 5) + 2000,
            "quadrant1Fill=currentColor -> black fill (currentColor=black)");
    const auto borderPx = [&](const QString& colorJson) {
      const QString src =
          QStringLiteral("%%{init: {\"themeVariables\": {\"quadrantExternalBorderStrokeFill\": %1}}}%%\n").arg(colorJson) + kSrc;
      return countNear(decodePng(editor::MermaidRenderCache::renderMermaidSourceToPng(src, 1.0).dataUrl), red, 30);
    };
    require(borderPx(QStringLiteral("\"red\"")) > 500, "red external border present");
    require(borderPx(QStringLiteral("\"none\"")) < 50, "external border none -> no border");
    require(borderPx(QStringLiteral("\"garbage\"")) < 50, "invalid external border -> no border (SVG initial none)");
  }

  qDebug().noquote() << "MermaidQuadrantThemeWiringTest: quadrant adapter consumes resolved theme (+overrides, RGBA, HSL borders, paint)";
  return 0;
}
