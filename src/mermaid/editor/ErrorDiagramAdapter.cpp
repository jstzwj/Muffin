#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidDiagnostic.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/error/ErrorScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QSize>

#include <cmath>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

// The shared getStyles base sheet that styling the error diagram reduces to:
// every other rule in the compiled sheet selects DOM the error renderer never
// emits. Values are the resolved theme variables.
QString errorBaseCss(const flowtheme::FlowThemeVariables& theme) {
  return QStringLiteral(".error-icon { fill: %1; }\n"
                        ".error-text { fill: %2; stroke: %2; }\n")
      .arg(theme.errorBkgColor, theme.errorTextColor);
}

error::ErrorTextCss textCssFromComputed(const csscascade::ElementStyle& computed,
                                        const flowtheme::FlowThemeVariables& theme,
                                        qreal presentationFontSize) {
  error::ErrorTextCss css;
  css.fill = computed.fill;
  css.stroke = computed.stroke;
  css.fontFamily = computed.fontFamily;
  css.fontWeight = computed.fontWeight;
  const CssLengthContext rootContext =
      pieCssLengthContext(firstFontFamily(theme.fontFamily), 16.0);
  const qreal resolved =
      cssFontSizePx(computed.fontSize, rootContext);
  css.fontSize = resolved > 0.0 ? resolved : presentationFontSize;
  // % stroke-width resolves against the viewBox normalized diagonal.
  css.strokeWidthPx = qMax(0.0, cssStrokeWidthPx(
      computed.strokeWidth, rootContext,
      std::sqrt((2412.0 * 2412.0 + 512.0 * 512.0) / 2.0)));
  css.visible = computed.displayed();
  css.opacity = computed.effectiveOpacity;
  return css;
}

struct ErrorDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("error")}; }
  // Upstream's error svg carries no class attribute (errorRenderer builds it
  // with the raw selectSvgElement util, which never stamps the type class).
  QString cssClass() const override { return {}; }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    error::ErrorTextStyle style;
    style.fontFamily = themeVars.fontFamily;
    style.errorBkgColor = themeVars.errorBkgColor;
    style.errorTextColor = themeVars.errorTextColor;

    error::ErrorScene scene = error::buildErrorScene(std::move(style));
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      // The upstream DOM: svg > [style, g (empty scaffold), g > 6 x
      // path.error-icon + 2 x text.error-text] — the content group is a
      // DIRECT child of the svg root (fixture childSummary; :nth-of-type
      // counts the scaffold as g#1 and content as g#2). The texts carry
      // font-size presentation attributes and an inline "text-anchor:
      // middle" style. The <style> element is kept in the tree so
      // :nth-child indexing matches the real document.
      csscascade::ElementStyle rootFallback;
      rootFallback.fill = themeVars.textColor;
      rootFallback.color = themeVars.textColor;
      rootFallback.fontFamily = themeVars.fontFamily;
      rootFallback.fontSize = themeVars.fontSize;
      csscascade::ElementStyle iconFallback = rootFallback;
      iconFallback.fill = themeVars.errorBkgColor;
      csscascade::ElementStyle textFallback = rootFallback;
      textFallback.fill = themeVars.errorTextColor;
      QVector<csscascade::ElementInput> elements;
      elements.append({QStringLiteral("svg"), QString(),
                       QStringLiteral("svg"),
                       QStringLiteral("diagram-root"), {}, {}, rootFallback,
                       QString()});
      elements.append({QStringLiteral("sheet"), QStringLiteral("svg"),
                       QStringLiteral("style"), QString(), {}, {}, rootFallback,
                       QString()});
      elements.append({QStringLiteral("scaffold"), QStringLiteral("svg"),
                       QStringLiteral("g"), QString(), {}, {}, rootFallback,
                       QString()});
      elements.append({QStringLiteral("content"), QStringLiteral("svg"),
                       QStringLiteral("g"), QString(), {}, {}, rootFallback,
                       QString()});
      for (int index = 0; index < scene.iconPaths.size(); ++index)
        elements.append({QStringLiteral("icon%1").arg(index),
                         QStringLiteral("content"), QStringLiteral("path"),
                         QString(), {QStringLiteral("error-icon")}, {},
                         iconFallback, QString()});
      elements.append({QStringLiteral("headline"), QStringLiteral("content"),
                       QStringLiteral("text"), QString(),
                       {QStringLiteral("error-text")}, {}, textFallback,
                       QStringLiteral("text-anchor: middle"),
                       QStringLiteral("font-size: 150px")});
      elements.append({QStringLiteral("version"), QStringLiteral("content"),
                       QStringLiteral("text"), QString(),
                       {QStringLiteral("error-text")}, {}, textFallback,
                       QStringLiteral("text-anchor: middle"),
                       QStringLiteral("font-size: 100px")});
      const QHash<QString, csscascade::ElementStyle> computed =
          csscascade::resolveElements(
              themeCss, elements, errorBaseCss(themeVars));
      // Per-path resolution: the six icons are DOM siblings, so each gets its
      // own computed style (structural selectors can split them).
      scene.css.icons.reserve(scene.iconPaths.size());
      for (int index = 0; index < scene.iconPaths.size(); ++index) {
        const csscascade::ElementStyle icon =
            computed.value(QStringLiteral("icon%1").arg(index), iconFallback);
        error::ErrorIconCss iconCss;
        iconCss.fill = icon.fill;
        iconCss.stroke = icon.stroke;
        iconCss.strokeWidthPx = qMax(0.0, cssStrokeWidthPx(
            icon.strokeWidth,
            pieCssLengthContext(firstFontFamily(themeVars.fontFamily), 16.0),
            std::sqrt((2412.0 * 2412.0 + 512.0 * 512.0) / 2.0)));
        iconCss.visible = icon.displayed();
        iconCss.opacity = icon.effectiveOpacity;
        scene.css.icons.append(iconCss);
      }
      scene.css.headline = textCssFromComputed(
          computed.value(QStringLiteral("headline"), textFallback), themeVars,
          150.0);
      scene.css.version = textCssFromComputed(
          computed.value(QStringLiteral("version"), textFallback), themeVars,
          100.0);
    }

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), themeVars.textColor,
        themeVars.fontFamily, 18.0);
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.roleDescription = QStringLiteral("error");
    metadata.diagramPadding = 0.0;
    metadata.svgUseMaxWidth = true;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(512, 109);
    entry.scene = std::make_shared<const error::ErrorScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

// The "---" diagram: registered second by upstream, its parser exists only to
// throw the frontmatter guidance message. Native: the diagnostic keeps the
// exact upstream text; renderSource()'s fallback wrapper attaches the error
// scene exactly like mermaid.core's Diagram.fromText("error") path.
struct DashDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("---")}; }
  QString cssClass() const override { return {}; }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& /*theme*/) const override {
    MermaidDiagnostic diagnostic;
    diagnostic.diagramType = type;
    diagnostic.stage = QStringLiteral("parser");
    diagnostic.code = QStringLiteral("dash-frontmatter");
    diagnostic.message = QStringLiteral(
        "Diagrams beginning with --- are not valid. If you were trying to use "
        "a YAML front-matter, please ensure that you've correctly opened and "
        "closed the YAML front-matter with un-indented `---` blocks");
    diagnostic.span.offset = 0;
    diagnostic.span.length = pre.code.isEmpty() ? 0 : 3;
    diagnostic.span.line = 1;
    diagnostic.span.column = 1;
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Error;
    entry.diagnostic = diagnostic;
    entry.errorMessage = formatMermaidDiagnostic(diagnostic);
    entry.errorDiagnostic = mermaidDiagnosticToJson(diagnostic);
    return entry;
  }
};

}  // namespace

const Diagram& errorDiagramAdapter() {
  static const ErrorDiagramImpl instance;
  return instance;
}

const Diagram& dashDiagramAdapter() {
  static const DashDiagramImpl instance;
  return instance;
}

}  // namespace muffin::mermaid::editor
