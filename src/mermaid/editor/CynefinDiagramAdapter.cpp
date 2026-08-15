#include "mermaid/editor/MermaidDiagrams.h"

#include "blocks/html/HtmlSanitizer.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/cynefin/CynefinDiagram.h"
#include "mermaid/cynefin/CynefinScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QJsonObject>
#include <QSize>

#include <memory>
#include <utility>

namespace muffin::mermaid::editor {
namespace {

QJsonValue scalar(const QJsonObject &object, const char *key,
                  const QJsonValue &fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  return value.isUndefined() || value.isNull() || value.isArray() ||
                 value.isObject()
             ? fallback
             : value;
}

bool truthy(const QJsonValue &value, bool fallback) {
  if (value.isUndefined() || value.isNull()) return fallback;
  return truthyConfigValue(value);
}

struct CynefinDiagramImpl final : Diagram {
  QStringList ids() const override { return {QStringLiteral("cynefin")}; }
  QString cssClass() const override { return QStringLiteral("cynefin"); }

  MermaidRenderEntry render(const MermaidPreprocessResult &pre,
                            const QString &type,
                            const QString &theme) const override {
    const QString configuredTheme = themeFromConfig(pre.config);
    const QString effectiveTheme = configuredTheme.isEmpty() ? theme : configuredTheme;
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(effectiveTheme), themeOverrides(pre.config));
    const QJsonObject raw = pre.config.value(QStringLiteral("cynefin")).toObject();

    cynefin::CynefinConfig config;
    config.useMaxWidth = scalar(raw, "useMaxWidth", true);
    config.width = scalar(raw, "width", 800.0);
    config.height = scalar(raw, "height", 600.0);
    config.padding = scalar(raw, "padding", 40.0);
    config.showDomainDescriptions = scalar(raw, "showDomainDescriptions", true);
    config.boundaryAmplitude = scalar(raw, "boundaryAmplitude", 8.0);
    config.seed = scalar(raw, "seed", 0.0);

    cynefin::CynefinSceneStyle style;
    style.fontFamily = themeVars.fontFamily;
    const CssLengthContext html = pieCssLengthContext(style.fontFamily, 16.0);
    style.rootFontSize = cssFontSizePx(themeVars.fontSize, html);
    style.domainFontSize = themeVars.cynefin.domainFontSize.toDouble();
    style.itemFontSize = themeVars.cynefin.itemFontSize.toDouble();
    style.boundaryColor = themeVars.cynefin.boundaryColor;
    style.boundaryWidth = themeVars.cynefin.boundaryWidth;
    style.cliffColor = themeVars.cynefin.cliffColor;
    style.cliffWidth = themeVars.cynefin.cliffWidth;
    style.arrowColor = themeVars.cynefin.arrowColor;
    style.arrowWidth = themeVars.cynefin.arrowWidth;
    style.complexBg = themeVars.cynefin.complexBg;
    style.complicatedBg = themeVars.cynefin.complicatedBg;
    style.chaoticBg = themeVars.cynefin.chaoticBg;
    style.clearBg = themeVars.cynefin.clearBg;
    style.confusionBg = themeVars.cynefin.confusionBg;
    style.textColor = themeVars.cynefin.textColor;
    style.labelColor = themeVars.cynefin.labelColor;
    const QJsonObject rawTheme =
        pre.config.value(QStringLiteral("themeVariables")).toObject()
            .value(QStringLiteral("cynefin")).toObject();
    if (rawTheme.contains(QStringLiteral("domainFontSize")))
      style.domainFontSize = scalar(rawTheme, "domainFontSize",
                                    themeVars.cynefin.domainFontSize);
    if (rawTheme.contains(QStringLiteral("itemFontSize")))
      style.itemFontSize = scalar(rawTheme, "itemFontSize",
                                  themeVars.cynefin.itemFontSize);

    cynefin::CynefinData data = cynefin::CynefinDiagram::parse(pre.code);
    if (!data.hasTitleDirective && !pre.title.isEmpty())
      data.title = HtmlSanitizer().sanitizedMermaidText(pre.title);

    // themeCSS: the DOM shape depends only on the parsed data (the viewBox
    // never reacts to the bbox), so the resolveElements tree is built in one
    // pass. The single measurement feedback — the item badge getBBox on the
    // classed text — is consumed by the builder through the overrides.
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    cynefin::CynefinCssOverrides overrides;
    const bool themeCssActive = !themeCss.trimmed().isEmpty();
    if (themeCssActive) {
      using csscascade::ElementInput;
      using csscascade::ElementStyle;
      const bool descriptions = truthy(config.showDomainDescriptions, true);
      const QString quadrantFills[]{
          style.complexBg, style.complicatedBg, style.chaoticBg,
          style.clearBg};
      const QStringList quadrants{QStringLiteral("complex"),
                                  QStringLiteral("complicated"),
                                  QStringLiteral("chaotic"),
                                  QStringLiteral("clear")};
      const QHash<QString, QString> domainFills{
          {QStringLiteral("complex"), style.complexBg},
          {QStringLiteral("complicated"), style.complicatedBg},
          {QStringLiteral("chaotic"), style.chaoticBg},
          {QStringLiteral("clear"), style.clearBg},
          {QStringLiteral("confusion"), style.confusionBg}};
      // The renderer root svg carries the `#id { font-family; font-size;
      // fill: textColor }` rule; every other inheritance flows through it.
      ElementStyle rootStyle;
      rootStyle.fill = themeVars.textColor;
      rootStyle.stroke = QStringLiteral("none");
      rootStyle.strokeWidth = QStringLiteral("1px");
      rootStyle.color = QStringLiteral("black");
      rootStyle.fontFamily = style.fontFamily;
      rootStyle.fontSize = QStringLiteral("%1px")
          .arg(QString::number(style.rootFontSize));
      rootStyle.fontWeight = QStringLiteral("400");
      ElementStyle inheritAll;

      QVector<ElementInput> tree;
      const auto push = [&tree](ElementInput input) {
        tree.append(std::move(input));
      };
      push({QStringLiteral("svg"), {}, QStringLiteral("svg"),
            QStringLiteral("diagram-root"), {}, {}, rootStyle, {}});
      push({QStringLiteral("styleEl"), QStringLiteral("svg"),
            QStringLiteral("style"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("root"), QStringLiteral("svg"),
            QStringLiteral("g"), {}, {}, {}, inheritAll, {}});
      push({QStringLiteral("backgrounds"), QStringLiteral("root"),
            QStringLiteral("g"), {}, {QStringLiteral("cynefin-backgrounds")},
            {}, inheritAll, {}});
      for (int i = 0; i < 4; ++i) {
        push({QStringLiteral("bg-%1").arg(i), QStringLiteral("backgrounds"),
              QStringLiteral("rect"), {}, {QStringLiteral("cynefinDomain")},
              {}, inheritAll, {},
              QStringLiteral("fill:%1;fill-opacity:0.4;stroke:none")
                  .arg(quadrantFills[i])});
      }
      push({QStringLiteral("boundaries"), QStringLiteral("root"),
            QStringLiteral("g"), {}, {QStringLiteral("cynefin-boundaries")},
            {}, inheritAll, {}});
      push({QStringLiteral("boundary-0"), QStringLiteral("boundaries"),
            QStringLiteral("path"), {}, {QStringLiteral("cynefinBoundary")},
            {}, inheritAll, {}, QStringLiteral("fill:none")});
      push({QStringLiteral("boundary-1"), QStringLiteral("boundaries"),
            QStringLiteral("path"), {}, {QStringLiteral("cynefinBoundary")},
            {}, inheritAll, {}, QStringLiteral("fill:none")});
      push({QStringLiteral("cliff"), QStringLiteral("boundaries"),
            QStringLiteral("path"), {}, {QStringLiteral("cynefinCliff")},
            {}, inheritAll, {}, QStringLiteral("fill:none")});
      push({QStringLiteral("confusion"), QStringLiteral("root"),
            QStringLiteral("path"), {}, {QStringLiteral("cynefinConfusion")},
            {}, inheritAll, {},
            QStringLiteral("fill:%1;fill-opacity:0.5")
                .arg(style.confusionBg)});
      push({QStringLiteral("labels"), QStringLiteral("root"),
            QStringLiteral("g"), {}, {QStringLiteral("cynefin-labels")}, {},
            inheritAll, {}});
      for (int i = 0; i < 4; ++i) {
        Q_UNUSED(i);
        push({QStringLiteral("label-%1").arg(i), QStringLiteral("labels"),
              QStringLiteral("text"), {},
              {QStringLiteral("cynefinDomainLabel")}, {}, inheritAll, {},
              QStringLiteral("text-anchor:middle;dominant-baseline:middle")});
      }
      push({QStringLiteral("label-4"), QStringLiteral("labels"),
            QStringLiteral("text"), {},
            {QStringLiteral("cynefinDomainLabel")}, {}, inheritAll, {},
            QStringLiteral("text-anchor:middle;dominant-baseline:middle")});
      if (descriptions) {
        push({QStringLiteral("subtitles"), QStringLiteral("root"),
              QStringLiteral("g"), {}, {QStringLiteral("cynefin-subtitles")},
              {}, inheritAll, {}});
        for (int i = 0; i < 9; ++i) {
          push({QStringLiteral("subtitle-%1").arg(i),
                QStringLiteral("subtitles"), QStringLiteral("text"), {},
                {QStringLiteral("cynefinSubtitle")}, {}, inheritAll, {},
                QStringLiteral(
                    "text-anchor:middle;dominant-baseline:middle")});
        }
      }
      QHash<QString, const cynefin::CynefinDomain*> dataDomains;
      for (const auto &domain : data.domains)
        dataDomains.insert(domain.name, &domain);
      push({QStringLiteral("items"), QStringLiteral("root"),
            QStringLiteral("g"), {}, {QStringLiteral("cynefin-items")}, {},
            inheritAll, {}});
      const QStringList allDomains = quadrants +
          QStringList{QStringLiteral("confusion")};
      int itemSlot = 0;
      for (const QString &domainName : allDomains) {
        const cynefin::CynefinDomain* domain =
            dataDomains.value(domainName, nullptr);
        if (!domain || domain->items.isEmpty()) continue;
        const bool confusion = domainName == QLatin1String("confusion");
        const int visibleCount =
            confusion ? qMin(3, domain->items.size()) : domain->items.size();
        const auto pushBadge = [&](bool overflow) {
          const QString key =
              QStringLiteral("item-%1").arg(itemSlot);
          push({key, QStringLiteral("items"), QStringLiteral("g"), {}, {}, {},
                inheritAll, {}});
          push({key + QLatin1String("-rect"), key, QStringLiteral("rect"), {},
                {overflow ? QStringLiteral("cynefinItemOverflow")
                          : QStringLiteral("cynefinItem")},
                {}, inheritAll, {},
                QStringLiteral("fill:%1;fill-opacity:%2")
                    .arg(domainFills.value(domainName),
                         overflow ? QStringLiteral("0.6")
                                  : QStringLiteral("0.95"))});
          push({key + QLatin1String("-text"), key, QStringLiteral("text"), {},
                {QStringLiteral("cynefinItemText")}, {}, inheritAll, {},
                QStringLiteral(
                    "text-anchor:middle;dominant-baseline:central")});
          ++itemSlot;
        };
        for (int i = 0; i < visibleCount; ++i) pushBadge(false);
        if (confusion && domain->items.size() > 3) pushBadge(true);
      }
      bool hasArrows = false;
      int arrowSlot = 0;
      for (const auto &transition : data.transitions) {
        if (quadrants.contains(transition.from) ||
            transition.from == QLatin1String("confusion")) {
          if (quadrants.contains(transition.to) ||
              transition.to == QLatin1String("confusion")) {
            hasArrows = true;
            break;
          }
        }
      }
      if (hasArrows) {
        push({QStringLiteral("arrows"), QStringLiteral("root"),
              QStringLiteral("g"), {}, {QStringLiteral("cynefin-arrows")},
              {}, inheritAll, {}});
        for (const auto &transition : data.transitions) {
          const bool fromKnown = quadrants.contains(transition.from) ||
                                 transition.from ==
                                     QLatin1String("confusion");
          const bool toKnown = quadrants.contains(transition.to) ||
                               transition.to == QLatin1String("confusion");
          if (!fromKnown || !toKnown || transition.from == transition.to)
            continue;
          const QString key =
              QStringLiteral("arrow-%1").arg(arrowSlot);
          push({key, QStringLiteral("arrows"), QStringLiteral("path"), {},
                {QStringLiteral("cynefinArrowLine")}, {}, inheritAll, {},
                QStringLiteral("fill:none")});
          if (transition.hasLabel) {
            push({key + QLatin1String("-label"), QStringLiteral("arrows"),
                  QStringLiteral("text"), {},
                  {QStringLiteral("cynefinArrowLabel")}, {}, inheritAll, {},
                  QStringLiteral(
                      "text-anchor:middle;dominant-baseline:auto")});
          }
          ++arrowSlot;
        }
      }
      if (!data.title.isEmpty()) {
        push({QStringLiteral("title"), QStringLiteral("root"),
              QStringLiteral("text"), {}, {QStringLiteral("cynefinTitle")},
              {}, inheritAll, {},
              QStringLiteral("text-anchor:middle;dominant-baseline:middle")});
      }
      if (hasArrows) {
        push({QStringLiteral("defs"), QStringLiteral("svg"),
              QStringLiteral("defs"), {}, {}, {}, inheritAll, {}});
        push({QStringLiteral("marker"), QStringLiteral("defs"),
              QStringLiteral("marker"), {}, {}, {}, inheritAll, {}});
        push({QStringLiteral("arrowhead"), QStringLiteral("marker"),
              QStringLiteral("path"), {},
              {QStringLiteral("cynefinArrowHead")}, {}, inheritAll, {}});
      }

      // Live cynefin getStyles sheet with the resolved theme values; the
      // stroke-dasharray declarations ride along for fidelity (the overlay
      // carries no dash model — base geometry already pins them).
      const QString domainPx = QString::number(
          style.domainFontSize.toDouble());
      const QString itemPx = QString::number(style.itemFontSize.toDouble());
      const QString subtitlePx = QString::number(
          style.itemFontSize.toDouble() - 1.0);
      const QString titlePx = QString::number(
          style.domainFontSize.toDouble() + 2.0);
      const QString baseCss = QStringLiteral(
          ".cynefinDomain { stroke: none; }\n"
          ".cynefinDomainLabel { font-size: %1px; font-weight: bold; fill: %2; }\n"
          ".cynefinSubtitle { font-size: %3px; fill: %4; font-style: italic; }\n"
          ".cynefinItem { fill-opacity: 0.95; stroke: %5; stroke-width: 1; }\n"
          ".cynefinItemText { font-size: %6px; fill: %4; }\n"
          ".cynefinItemOverflow { fill-opacity: 0.6; stroke: %5; stroke-width: 1; stroke-dasharray: 3 2; }\n"
          ".cynefinBoundary { stroke: %5; stroke-width: %7; stroke-dasharray: 6 3; }\n"
          ".cynefinCliff { stroke: %8; stroke-width: %9; }\n"
          ".cynefinConfusion { stroke: %5; stroke-width: 1.5; stroke-dasharray: 4 2; }\n"
          ".cynefinArrowLine { stroke: %10; stroke-width: %11; fill: none; }\n"
          ".cynefinArrowHead { fill: %10; stroke: none; }\n"
          ".cynefinArrowLabel { font-size: %3px; fill: %4; }\n"
          ".cynefinTitle { font-size: %12px; font-weight: bold; fill: %2; }\n")
          .arg(domainPx, style.labelColor, subtitlePx, style.textColor,
               style.boundaryColor, itemPx, style.boundaryWidth,
               style.cliffColor, style.cliffWidth, style.arrowColor,
               style.arrowWidth, titlePx);
      const QHash<QString, ElementStyle> css = csscascade::resolveElements(
          themeCss, tree, baseCss);
      const CssLengthContext familyCtx =
          pieCssLengthContext(style.fontFamily, style.rootFontSize);
      const auto convert = [&](const QString& key) {
        cynefin::CynefinElementCss out;
        const ElementStyle& resolved = css.value(key);
        out.fill = resolved.fill;
        out.stroke = resolved.stroke;
        out.strokeWidth = resolved.strokeWidth;
        if (!resolved.fontFamily.trimmed().isEmpty())
          out.fontFamily = firstFontFamily(resolved.fontFamily);
        out.fontSize = cssFontSizePx(resolved.fontSize, familyCtx);
        out.fontWeight = resolved.fontWeight;
        out.fontStyle = resolved.fontStyle;
        out.opacity = resolved.effectiveOpacity;
        // Pure channel opacities — the paint layer multiplies the opacity
        // product in once (the browser keeps fill-opacity and opacity
        // separate).
        out.fillOpacity = cssOpacity(resolved.fillOpacity);
        out.strokeOpacity = cssOpacity(resolved.strokeOpacity);
        out.visible = resolved.displayed();
        out.measures = resolved.display.compare(QStringLiteral("none"),
                                                Qt::CaseInsensitive) != 0;
        return out;
      };
      overrides.active = true;
      for (int i = 0; i < 4; ++i)
        overrides.backgrounds.append(
            convert(QStringLiteral("bg-%1").arg(i)));
      overrides.boundaries.append(convert(QStringLiteral("boundary-0")));
      overrides.boundaries.append(convert(QStringLiteral("boundary-1")));
      overrides.boundaries.append(convert(QStringLiteral("cliff")));
      overrides.confusion = convert(QStringLiteral("confusion"));
      for (int i = 0; i < 5; ++i)
        overrides.labels.append(convert(QStringLiteral("label-%1").arg(i)));
      if (descriptions)
        for (int i = 0; i < 9; ++i)
          overrides.subtitles.append(
              convert(QStringLiteral("subtitle-%1").arg(i)));
      for (int i = 0; i < itemSlot; ++i) {
        cynefin::CynefinCssOverrides::Item slot;
        const QString key = QStringLiteral("item-%1").arg(i);
        slot.rect = convert(key + QLatin1String("-rect"));
        slot.text = convert(key + QLatin1String("-text"));
        overrides.items.append(std::move(slot));
      }
      for (int i = 0; i < arrowSlot; ++i) {
        cynefin::CynefinCssOverrides::Arrow slot;
        const QString key = QStringLiteral("arrow-%1").arg(i);
        slot.line = convert(key);
        slot.label = convert(key + QLatin1String("-label"));
        overrides.arrows.append(std::move(slot));
      }
      overrides.arrowHead = convert(QStringLiteral("arrowhead"));
      if (!data.title.isEmpty()) overrides.title = convert(QStringLiteral("title"));
    }

    cynefin::CynefinScene scene = cynefin::buildCynefinScene(
        data, std::move(config), std::move(style),
        themeCssActive ? &overrides : nullptr);
    MermaidRenderMetadata metadata =
        renderMetadata(pre, type, data.title, data.accTitle, data.accDescr,
                       themeVars.cynefin.labelColor, themeVars.fontFamily,
                       scene.title.fontSize > 0.0 ? scene.title.fontSize : 18.0);
    metadata.title.clear();
    metadata.svgUseMaxWidth = scene.useMaxWidth;

    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qRound(scene.bounds.width()),
                              qRound(scene.bounds.height()));
    entry.scene = std::make_shared<const cynefin::CynefinScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

} // namespace

const Diagram &cynefinDiagramAdapter() {
  static const CynefinDiagramImpl adapter;
  return adapter;
}

} // namespace muffin::mermaid::editor
