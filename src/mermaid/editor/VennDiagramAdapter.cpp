#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/venn/VennDiagram.h"
#include "mermaid/venn/VennScene.h"

#include <QJsonObject>
#include <QSize>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QFont::Style cssFontStyle(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("italic")) return QFont::StyleItalic;
  if (lower.startsWith(QLatin1String("oblique"))) return QFont::StyleOblique;
  return QFont::StyleNormal;
}

QJsonValue scalar(const QJsonObject& object, const char* key,
                  const QJsonValue& fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

struct VennDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("venn")}; }
  QString cssClass() const override { return QStringLiteral("venn"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre,
                            const QString& type,
                            const QString& theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme =
        configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));

    const QJsonObject raw = pre.config.value(QStringLiteral("venn")).toObject();
    venn::VennConfig config;
    config.width = scalar(raw, "width", QJsonValue(800.0));
    config.height = scalar(raw, "height", QJsonValue(450.0));
    config.padding = scalar(raw, "padding", QJsonValue(8.0));
    config.useMaxWidth = scalar(raw, "useMaxWidth", QJsonValue(true));
    config.useDebugLayout =
        scalar(raw, "useDebugLayout", QJsonValue(false));
    config.handDrawnSeed =
        scalar(pre.config, "handDrawnSeed", QJsonValue(0.0));

    venn::VennSceneStyle style;
    const QJsonValue look = pre.config.value(QStringLiteral("look"));
    style.look = look.isString() ? look.toString() : QStringLiteral("classic");
    style.fontFamily = themeVars.fontFamily;
    style.background = themeVars.background;
    style.primaryColor = themeVars.primaryColor;
    style.primaryTextColor = themeVars.primaryTextColor;
    style.textColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    style.vennTitleTextColor = themeVars.vennTitleTextColor;
    style.vennSetTextColor = themeVars.vennSetTextColor;
    for (const QString& color : themeVars.venn) style.colors.append(color);

    venn::VennData data = venn::VennDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty()) data.title = pre.title;
    if (data.subsets.isEmpty())
      throw std::runtime_error(
          "Cannot read properties of undefined (reading 'set')");

    venn::VennScene scene = venn::buildVennScene(
        data, std::move(config), std::move(style));
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      QVector<ElementInput> elements;
      ElementStyle root;
      root.fill = themeVars.textColor;
      root.stroke = QStringLiteral("none");
      root.strokeWidth = QStringLiteral("1px");
      root.color = QStringLiteral("black");
      root.fontFamily = scene.style.fontFamily;
      root.fontSize = QStringLiteral("16px");
      root.fontWeight = QStringLiteral("400");
      root.fontStyle = QStringLiteral("normal");
      elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                       QStringLiteral("diagram-root"),
                       {QStringLiteral("venn")}, {}, root, {}});
      QString titleKey;
      if (!scene.title.isEmpty()) {
        titleKey = QStringLiteral("title");
        ElementStyle fallback = root;
        fallback.fill = scene.titleText.fill;
        fallback.fontSize = QString::number(scene.titleText.fontSize) +
                            QStringLiteral("px");
        elements.append({titleKey, QStringLiteral("svg"),
                         QStringLiteral("text"), {},
                         {QStringLiteral("venn-title")},
                         {{QStringLiteral("font-size"),
                           QString::number(scene.titleText.fontSize)}},
                         fallback,
                         QStringLiteral("fill:") + scene.titleText.fill});
      }
      elements.append({QStringLiteral("venn-group"), QStringLiteral("svg"),
                       QStringLiteral("g"), {}, {}, {}, root, {}});
      QVector<QString> areaPathKeys(scene.areas.size());
      QVector<QString> areaLabelKeys(scene.areas.size());
      for (qsizetype i = 0; i < scene.areas.size(); ++i) {
        const auto& area = scene.areas.at(i);
        const QString group = QStringLiteral("area-%1").arg(i);
        elements.append({group, QStringLiteral("venn-group"),
                         QStringLiteral("g"), {},
                         area.cssClass.split(QLatin1Char(' '),
                                             Qt::SkipEmptyParts),
                         {}, root, {}});
        ElementStyle path = root;
        path.fill = area.fill;
        path.stroke = area.stroke;
        path.strokeWidth = QString::number(area.strokeWidth) +
                           QStringLiteral("px");
        path.fillOpacity = QString::number(area.fillOpacity);
        path.strokeOpacity = QString::number(area.strokeOpacity);
        const QString pathKey = QStringLiteral("area-path-%1").arg(i);
        areaPathKeys[i] = pathKey;
        const QString pathInline =
            QStringLiteral("fill:%1;fill-opacity:%2;stroke:%3;"
                           "stroke-width:%4px;stroke-opacity:%5")
                .arg(area.fill, QString::number(area.fillOpacity), area.stroke,
                     QString::number(area.strokeWidth),
                     QString::number(area.strokeOpacity));
        elements.append({pathKey, group, QStringLiteral("path"), {}, {}, {},
                         path, pathInline});
        ElementStyle label = root;
        label.fill = area.label.fill;
        label.fontSize = QString::number(area.label.fontSize) +
                         QStringLiteral("px");
        const QString labelKey = QStringLiteral("area-label-%1").arg(i);
        areaLabelKeys[i] = labelKey;
        const QString labelInline =
            QStringLiteral("font-size:%1px;fill:%2")
                .arg(QString::number(area.label.fontSize), area.label.fill);
        elements.append({labelKey, group, QStringLiteral("text"), {},
                         {QStringLiteral("label")}, {}, label, labelInline});
      }

      elements.append({QStringLiteral("text-nodes"),
                       QStringLiteral("venn-group"), QStringLiteral("g"), {},
                       {QStringLiteral("venn-text-nodes")}, {}, root, {}});
      QHash<QString, QString> textAreaKeys;
      QVector<QString> textNodeKeys(scene.textNodes.size());
      for (qsizetype i = 0; i < scene.textNodes.size(); ++i) {
        const auto& node = scene.textNodes.at(i);
        QString areaKey = textAreaKeys.value(node.areaKey);
        if (areaKey.isEmpty()) {
          areaKey = QStringLiteral("text-area-%1").arg(textAreaKeys.size());
          textAreaKeys.insert(node.areaKey, areaKey);
          ElementStyle area = root;
          area.fontSize = QString::number(40.0 * scene.scale) +
                          QStringLiteral("px");
          elements.append({areaKey, QStringLiteral("text-nodes"),
                           QStringLiteral("g"), {},
                           {QStringLiteral("venn-text-area")},
                           {{QStringLiteral("font-size"),
                             QString::number(40.0 * scene.scale)}}, area, {}});
        }
        const QString foreignObject = QStringLiteral("text-fo-%1").arg(i);
        elements.append({foreignObject, areaKey,
                         QStringLiteral("foreignObject"), {},
                         {QStringLiteral("venn-text-node-fo")}, {}, {}, {}});
        ElementStyle span;
        span.color = node.color;
        const QString key = QStringLiteral("text-node-%1").arg(i);
        textNodeKeys[i] = key;
        const QString inlineStyle = node.color.isEmpty()
            ? QStringLiteral("display:flex;width:100%;height:100%")
            : QStringLiteral("display:flex;width:100%;height:100%;color:") +
                  node.color;
        elements.append({key, foreignObject, QStringLiteral("span"), {},
                         {QStringLiteral("venn-text-node")}, {}, span,
                         inlineStyle});
      }

      const QString builtInCss = QStringLiteral(
          ".venn-title{font-size:32px;fill:%1;font-family:%2;}"
          ".venn-circle text{font-size:48px;font-family:%2;}"
          ".venn-intersection text{font-size:48px;fill:%3;font-family:%2;}"
          ".venn-text-node{font-family:%2;color:%3;}")
          .arg(scene.style.vennTitleTextColor, scene.style.fontFamily,
               scene.style.vennSetTextColor);
      const auto css = csscascade::resolveElements(themeCss, elements,
                                                    builtInCss);
      const CssLengthContext rootContext =
          pieCssLengthContext(scene.style.fontFamily, 16.0);
      const qreal diagonal = std::hypot(scene.bounds.width(),
                                        scene.bounds.height()) /
                             std::sqrt(2.0);
      auto projectText = [&](const csscascade::ElementStyle& value,
                             venn::VennTextGeometry& text) {
        text.fontFamily = value.fontFamily;
        text.fontSize = cssFontSizePx(value.fontSize, rootContext);
        text.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                            QFont::Normal);
        text.fontStyle = cssFontStyle(value.fontStyle);
        text.fill = value.fill;
        text.opacity = value.effectiveFillOpacity;
        text.visible = value.displayed();
        text.hasBox = value.hasBox();
      };
      if (!titleKey.isEmpty()) projectText(css.value(titleKey), scene.titleText);
      for (qsizetype i = 0; i < scene.areas.size(); ++i) {
        auto& area = scene.areas[i];
        const auto path = css.value(areaPathKeys.at(i));
        area.fill = path.fill;
        area.stroke = path.stroke;
        area.strokeWidth = cssStrokeWidthPx(path.strokeWidth, rootContext,
                                            diagonal);
        area.fillOpacity = path.effectiveFillOpacity;
        area.strokeOpacity = path.effectiveStrokeOpacity;
        area.pathVisible = path.displayed();
        area.pathHasBox = path.hasBox();
        projectText(css.value(areaLabelKeys.at(i)), area.label);
      }
      for (qsizetype i = 0; i < scene.textNodes.size(); ++i) {
        auto& node = scene.textNodes[i];
        const auto value = css.value(textNodeKeys.at(i));
        node.fontFamily = value.fontFamily;
        node.fontSize = cssFontSizePx(value.fontSize, rootContext);
        node.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                            QFont::Normal);
        node.fontStyle = cssFontStyle(value.fontStyle);
        node.color = value.color;
        node.opacity = value.effectiveOpacity;
        node.visible = value.displayed();
        node.hasBox = value.hasBox();
      }
    }
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, data.title, data.accTitle, data.accDescr,
        scene.style.vennTitleTextColor, scene.style.fontFamily,
        32.0 * scene.scale);
    // Venn owns its visual title and its renderer does not project commonDb
    // title/accessibility fields into SVG metadata in Mermaid 11.16.
    metadata.title.clear();
    metadata.accessibleTitle.clear();
    metadata.accessibleDescription.clear();
    metadata.svgEmitAccessibleTitle = false;
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.rasterBounds.width()),
                              qRound(scene.rasterBounds.height()));
    entry.scene = std::make_shared<const venn::VennScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& vennDiagramAdapter() {
  static const VennDiagramImpl adapter;
  return adapter;
}

}  // namespace muffin::mermaid::editor
