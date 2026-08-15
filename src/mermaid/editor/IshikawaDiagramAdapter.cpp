#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/ishikawa/IshikawaDiagram.h"
#include "mermaid/ishikawa/IshikawaScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject& object, const char* key,
                  const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

QFont::Style cssFontStyle(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("italic")) return QFont::StyleItalic;
  if (lower.startsWith(QLatin1String("oblique"))) return QFont::StyleOblique;
  return QFont::StyleNormal;
}

ishikawa::IshikawaTextAnchor textAnchor(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("middle"))
    return ishikawa::IshikawaTextAnchor::Middle;
  if (lower == QLatin1String("end"))
    return ishikawa::IshikawaTextAnchor::End;
  return ishikawa::IshikawaTextAnchor::Start;
}

ishikawa::IshikawaTextBaseline textBaseline(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("middle") ||
      lower == QLatin1String("central"))
    return ishikawa::IshikawaTextBaseline::Middle;
  if (lower == QLatin1String("hanging"))
    return ishikawa::IshikawaTextBaseline::Hanging;
  return ishikawa::IshikawaTextBaseline::Auto;
}

struct IshikawaDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("ishikawa")}; }
  QString cssClass() const override { return QStringLiteral("ishikawa"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw =
        pre.config.value(QStringLiteral("ishikawa")).toObject();
    ishikawa::IshikawaConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", QJsonValue(true));
    config.diagramPadding = scalar(raw, "diagramPadding", QJsonValue(20.0));
    config.handDrawnSeed =
        scalar(pre.config, "handDrawnSeed", QJsonValue(0.0));

    ishikawa::IshikawaSceneStyle style;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    style.fontSize = cssFontSizePx(
        themeVars.fontSize,
        pieCssLengthContext(firstFontFamily(style.fontFamily), 16.0));
    style.lineColor = themeVars.lineColor;
    style.mainBkg = themeVars.mainBkg;
    style.textColor = themeVars.textColor;

    const ishikawa::IshikawaData data =
        ishikawa::IshikawaDiagram::parse(pre.code);
    ishikawa::IshikawaScene scene = ishikawa::buildIshikawaScene(
        data, config, style);
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle rootFallback;
      rootFallback.fill = style.textColor;
      rootFallback.stroke = QStringLiteral("none");
      rootFallback.strokeWidth = QStringLiteral("1px");
      rootFallback.color = QStringLiteral("black");
      rootFallback.fontFamily = style.fontFamily;
      rootFallback.fontSize = QString::number(style.fontSize) +
                              QStringLiteral("px");
      rootFallback.fontWeight = QStringLiteral("400");
      rootFallback.fontStyle = QStringLiteral("normal");

      QHash<QString, ishikawa::IshikawaPrimitiveKind> kinds;
      QHash<QString, int> indexes;
      for (const auto& element : scene.domElements) {
        ElementStyle fallback = rootFallback;
        if (element.primitive) {
          kinds.insert(element.key, element.kind);
          indexes.insert(element.key, element.index);
          switch (element.kind) {
            case ishikawa::IshikawaPrimitiveKind::Line: {
              const auto& value = scene.lines.at(element.index);
              fallback.fill = QStringLiteral("none");
              fallback.stroke = value.stroke;
              fallback.strokeWidth = QString::number(value.strokeWidth) +
                                     QStringLiteral("px");
              break;
            }
            case ishikawa::IshikawaPrimitiveKind::Path: {
              const auto& value = scene.paths.at(element.index);
              fallback.fill = value.fill;
              fallback.stroke = value.stroke;
              fallback.strokeWidth = QString::number(value.strokeWidth) +
                                     QStringLiteral("px");
              break;
            }
            case ishikawa::IshikawaPrimitiveKind::Rect: {
              const auto& value = scene.rects.at(element.index);
              fallback.fill = value.fill;
              fallback.stroke = value.stroke;
              fallback.strokeWidth = QString::number(value.strokeWidth) +
                                     QStringLiteral("px");
              break;
            }
            case ishikawa::IshikawaPrimitiveKind::Text: {
              const auto& value = scene.texts.at(element.index);
              fallback.fill = value.fill;
              fallback.fontFamily = value.fontFamily;
              fallback.fontSize = QString::number(value.fontSize) +
                                  QStringLiteral("px");
              fallback.fontWeight = QString::number(
                  int(value.weight == QFont::DemiBold ? 600 : 400));
              fallback.textAnchor =
                  value.textAnchor == ishikawa::IshikawaTextAnchor::Middle
                      ? QStringLiteral("middle")
                      : value.textAnchor == ishikawa::IshikawaTextAnchor::End
                            ? QStringLiteral("end") : QStringLiteral("start");
              fallback.dominantBaseline =
                  value.baseline == ishikawa::IshikawaTextBaseline::Middle
                      ? QStringLiteral("middle")
                      : value.baseline == ishikawa::IshikawaTextBaseline::Hanging
                            ? QStringLiteral("hanging") : QStringLiteral("auto");
              break;
            }
          }
        }
        elements.append({element.key, element.parentKey, element.tag,
                         element.key == QLatin1String("svg")
                             ? QStringLiteral("diagram-root") : QString(),
                         element.classes, {}, fallback, {}});
      }
      const QString builtInCss = QStringLiteral(
          ".ishikawa .ishikawa-spine,.ishikawa .ishikawa-branch,"
          ".ishikawa .ishikawa-sub-branch{stroke:%1;stroke-width:2;fill:none;}"
          ".ishikawa .ishikawa-sub-branch{stroke-width:1;}"
          ".ishikawa .ishikawa-arrow{fill:%1;}"
          ".ishikawa .ishikawa-head,.ishikawa .ishikawa-label-box{"
          "fill:%2;stroke:%1;stroke-width:2;}"
          ".ishikawa text{font-family:%3;font-size:%4;fill:%5;}"
          ".ishikawa .ishikawa-head-label{font-weight:600;text-anchor:middle;"
          "dominant-baseline:middle;font-size:14px;}"
          ".ishikawa .ishikawa-label{text-anchor:end;}"
          ".ishikawa .ishikawa-label.cause{text-anchor:middle;"
          "dominant-baseline:middle;}"
          ".ishikawa .ishikawa-label.align{text-anchor:end;"
          "dominant-baseline:middle;}"
          ".ishikawa .ishikawa-label.up{dominant-baseline:baseline;}"
          ".ishikawa .ishikawa-label.down{dominant-baseline:hanging;}")
          .arg(style.lineColor, style.mainBkg, style.fontFamily,
               QString::number(style.fontSize) + QStringLiteral("px"),
               style.textColor);
      const auto css = csscascade::resolveElements(themeCss, elements,
                                                    builtInCss);
      const qreal diagonal = std::hypot(scene.bounds.width(),
                                        scene.bounds.height()) /
                             std::sqrt(2.0);
      QHash<QString, qreal> fontSizes;
      for (const ElementInput& element : elements) {
        const auto value = css.value(element.key);
        const qreal parentSize =
            fontSizes.value(element.parentKey, style.fontSize);
        const CssLengthContext context =
            pieCssLengthContext(value.fontFamily, parentSize);
        const qreal usedFontSize = cssFontSizePx(value.fontSize, context);
        fontSizes.insert(element.key, usedFontSize);
        if (element.key == QLatin1String("marker-path")) {
          style.markerFill = value.fill;
          style.markerOpacity = value.effectiveFillOpacity;
          style.markerVisible = value.displayed();
          continue;
        }
        if (!kinds.contains(element.key)) continue;
        if (kinds.value(element.key) ==
            ishikawa::IshikawaPrimitiveKind::Text) {
          ishikawa::IshikawaSceneStyle::Text resolved;
          resolved.fontFamily = value.fontFamily;
          resolved.fontSize = usedFontSize;
          resolved.fontWeight = cssFontWeightToQt(
              QJsonValue(value.fontWeight), QFont::Normal);
          resolved.fontStyle = cssFontStyle(value.fontStyle);
          resolved.textAnchor = textAnchor(value.textAnchor);
          resolved.baseline = textBaseline(value.dominantBaseline);
          resolved.fill = value.fill;
          resolved.opacity = value.effectiveFillOpacity;
          resolved.visible = value.displayed();
          resolved.hasBox =
              value.display.compare(QStringLiteral("none"),
                                    Qt::CaseInsensitive) != 0;
          resolved.rootHasBox = value.hasBox();
          style.textStyles.insert(element.key, std::move(resolved));
        } else {
          ishikawa::IshikawaSceneStyle::Shape resolved;
          resolved.fill = value.fill;
          resolved.stroke = value.stroke;
          resolved.strokeWidth = cssStrokeWidthPx(
              value.strokeWidth, context, diagonal);
          resolved.fillOpacity = value.effectiveFillOpacity;
          resolved.strokeOpacity = value.effectiveStrokeOpacity;
          resolved.visible = value.displayed();
          resolved.hasBox =
              value.display.compare(QStringLiteral("none"),
                                    Qt::CaseInsensitive) != 0;
          resolved.rootHasBox = value.hasBox();
          style.shapeStyles.insert(element.key, std::move(resolved));
        }
      }
      scene = ishikawa::buildIshikawaScene(
          data, std::move(config), std::move(style));
    }

    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), QString(), QString(), scene.style.textColor,
        scene.style.fontFamily, scene.style.fontSize);
    // Ishikawa's root is drawn inside the fish head. Mermaid 11.16 does not
    // project it, frontmatter, or metadata-looking nodes into SVG title/ARIA.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene =
        std::make_shared<const ishikawa::IshikawaScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& ishikawaDiagramAdapter() {
  static const IshikawaDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
