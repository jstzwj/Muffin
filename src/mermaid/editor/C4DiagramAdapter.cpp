#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/c4/C4Diagram.h"
#include "mermaid/c4/C4Scene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue valueOr(const QJsonObject& object, const QString& key,
                   const QJsonValue& fallback) {
  const QJsonValue value = object.value(key);
  return value.isUndefined() || value.isNull() ? fallback : value;
}

qreal numberOr(const QJsonObject& object, const QString& key, qreal fallback) {
  const qreal value = jsNumberValue(valueOr(object, key, fallback));
  return std::isfinite(value) ? value : fallback;
}

QString stringOr(const QJsonObject& object, const QString& key,
                 const QString& fallback) {
  const QJsonValue value = valueOr(object, key, fallback);
  return value.isString() ? value.toString() : value.toVariant().toString();
}

const QStringList& shapeTypes() {
  static const QStringList values = {
      QStringLiteral("person"), QStringLiteral("external_person"),
      QStringLiteral("system"), QStringLiteral("external_system"),
      QStringLiteral("system_db"), QStringLiteral("external_system_db"),
      QStringLiteral("system_queue"), QStringLiteral("external_system_queue"),
      QStringLiteral("container"), QStringLiteral("external_container"),
      QStringLiteral("container_db"), QStringLiteral("external_container_db"),
      QStringLiteral("container_queue"), QStringLiteral("external_container_queue"),
      QStringLiteral("component"), QStringLiteral("external_component"),
      QStringLiteral("component_db"), QStringLiteral("external_component_db"),
      QStringLiteral("component_queue"), QStringLiteral("external_component_queue")};
  return values;
}

void addDefaultColors(c4::C4Config& config) {
  const auto add = [&](const QString& type, const QString& background,
                       const QString& border) {
    config.backgroundColors.insert(type, background);
    config.borderColors.insert(type, border);
  };
  add(QStringLiteral("person"), QStringLiteral("#08427B"), QStringLiteral("#073B6F"));
  add(QStringLiteral("external_person"), QStringLiteral("#686868"), QStringLiteral("#8A8A8A"));
  for (const QString& suffix : {QString(), QStringLiteral("_db"), QStringLiteral("_queue")}) {
    add(QStringLiteral("system") + suffix, QStringLiteral("#1168BD"), QStringLiteral("#3C7FC0"));
    add(QStringLiteral("external_system") + suffix, QStringLiteral("#999999"), QStringLiteral("#8A8A8A"));
    add(QStringLiteral("container") + suffix, QStringLiteral("#438DD5"), QStringLiteral("#3C7FC0"));
    add(QStringLiteral("external_container") + suffix, QStringLiteral("#B3B3B3"), QStringLiteral("#A6A6A6"));
    add(QStringLiteral("component") + suffix, QStringLiteral("#85BBF0"), QStringLiteral("#78A8D8"));
    add(QStringLiteral("external_component") + suffix, QStringLiteral("#CCCCCC"), QStringLiteral("#BFBFBF"));
  }
}

c4::C4Config c4Config(const QJsonObject& raw) {
  c4::C4Config config;
  config.useMaxWidth = truthyConfigValue(valueOr(raw, QStringLiteral("useMaxWidth"), true));
  config.diagramMarginX = numberOr(raw, QStringLiteral("diagramMarginX"), 50.0);
  config.diagramMarginY = numberOr(raw, QStringLiteral("diagramMarginY"), 10.0);
  config.c4ShapeMargin = numberOr(raw, QStringLiteral("c4ShapeMargin"), 50.0);
  config.c4ShapePadding = numberOr(raw, QStringLiteral("c4ShapePadding"), 20.0);
  config.width = numberOr(raw, QStringLiteral("width"), 216.0);
  config.height = numberOr(raw, QStringLiteral("height"), 60.0);
  config.boxMargin = numberOr(raw, QStringLiteral("boxMargin"), 10.0);
  config.c4ShapeInRow = qMax(1, qRound(numberOr(raw, QStringLiteral("c4ShapeInRow"), 4.0)));
  config.nextLinePaddingX = numberOr(raw, QStringLiteral("nextLinePaddingX"), 0.0);
  config.c4BoundaryInRow = qMax(1, qRound(numberOr(raw, QStringLiteral("c4BoundaryInRow"), 2.0)));
  config.wrap = truthyConfigValue(valueOr(raw, QStringLiteral("wrap"), true));
  config.wrapPadding = numberOr(raw, QStringLiteral("wrapPadding"), 10.0);
  addDefaultColors(config);
  for (const QString& type : shapeTypes()) {
    c4::C4Font font;
    font.family = stringOr(raw, type + QStringLiteral("FontFamily"),
                           QStringLiteral("\"Open Sans\", sans-serif"));
    font.size = numberOr(raw, type + QStringLiteral("FontSize"), 14.0);
    font.weight = stringOr(raw, type + QStringLiteral("FontWeight"),
                           QStringLiteral("normal"));
    config.fonts.insert(type, font);
    config.backgroundColors[type] = stringOr(
        raw, type + QStringLiteral("_bg_color"), config.backgroundColors.value(type));
    config.borderColors[type] = stringOr(
        raw, type + QStringLiteral("_border_color"), config.borderColors.value(type));
  }
  c4::C4Font boundary;
  boundary.family = stringOr(raw, QStringLiteral("boundaryFontFamily"),
                             QStringLiteral("\"Open Sans\", sans-serif"));
  boundary.size = numberOr(raw, QStringLiteral("boundaryFontSize"), 14.0);
  boundary.weight = stringOr(raw, QStringLiteral("boundaryFontWeight"),
                             QStringLiteral("normal"));
  config.fonts.insert(QStringLiteral("boundary"), boundary);
  c4::C4Font message;
  message.family = stringOr(raw, QStringLiteral("messageFontFamily"),
                            QStringLiteral("\"Open Sans\", sans-serif"));
  message.size = numberOr(raw, QStringLiteral("messageFontSize"), 12.0);
  message.weight = stringOr(raw, QStringLiteral("messageFontWeight"),
                            QStringLiteral("normal"));
  config.fonts.insert(QStringLiteral("message"), message);
  return config;
}

struct C4DiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("c4")}; }
  QString cssClass() const override { return QStringLiteral("c4"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    c4::C4Data data = c4::C4Diagram::parse(
        pre.code,
        truthyConfigValue(valueOr(pre.config, QStringLiteral("wrap"), false)));
    if (data.title.isEmpty() && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);
    c4::C4Config config = c4Config(pre.config.value(QStringLiteral("c4")).toObject());
    c4::C4SceneStyle style;
    style.rootFontFamily = themeVars.fontFamily;
    style.rootFontSize = cssFontSizePx(
        themeVars.fontSize, pieCssLengthContext(style.rootFontFamily, 16.0));
    style.rootFontWeight = themeVars.fontWeight;
    style.rootTextColor = themeVars.textColor;
    c4::C4Scene scene = c4::buildC4Scene(data, std::move(config), std::move(style));
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.rootTextColor, scene.style.rootFontFamily,
        scene.style.rootFontSize);
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;
    // C4 11.16's accTitle grammar action overwrites the visible diagram title
    // instead of populating commonDb. Its SVG therefore has no accessible
    // <title>/aria-labelledby fallback, while accDescr still emits normally.
    metadata.svgEmitAccessibleTitle = !data.accTitle.isEmpty();
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()), qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const c4::C4Scene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& c4DiagramAdapter() {
  static const C4DiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
