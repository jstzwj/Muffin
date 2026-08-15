#include "mermaid/editor/MermaidDiagrams.h"

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/requirement/RequirementDiagram.h"
#include "mermaid/requirement/RequirementLayout.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidCssCascade.h"
#include "mermaid/theme/MermaidColor.h"

#include <QJsonObject>
#include <QSet>
#include <QSize>
#include <QString>

#include <algorithm>
#include <cmath>
#include <memory>

namespace muffin::mermaid::editor {
namespace {

QFont::Style requirementCssFontStyle(const QString& value) {
  const QString lower = value.trimmed().toLower();
  if (lower == QLatin1String("italic")) return QFont::StyleItalic;
  if (lower.startsWith(QLatin1String("oblique"))) return QFont::StyleOblique;
  return QFont::StyleNormal;
}

QString requirementCssColor(const QColor& value) {
  if (!value.isValid()) return {};
  if (value.alpha() == 255) return value.name(QColor::HexRgb);
  return color::rgba(value.red(), value.green(), value.blue(), value.alphaF());
}

requirement::RequirementTextStyle requirementTextFromCss(
    const csscascade::ElementStyle& value, qreal parentFontSize) {
  requirement::RequirementTextStyle result;
  const CssLengthContext context =
      pieCssLengthContext(value.fontFamily, parentFontSize);
  result.fontFamily = value.fontFamily;
  result.fontSizePx = cssFontSizePx(value.fontSize, context);
  result.fontWeight = cssFontWeightToQt(QJsonValue(value.fontWeight),
                                        QFont::Normal);
  result.fontWeightResolved = true;
  result.fontStyle = requirementCssFontStyle(value.fontStyle);
  const QString lineHeight = value.lineHeight.trimmed();
  if (lineHeight.compare(QLatin1String("normal"), Qt::CaseInsensitive) == 0) {
    result.lineHeightNormal = true;
  } else {
    bool unitless = false;
    const qreal multiplier = lineHeight.toDouble(&unitless);
    result.lineHeightPx = unitless
        ? multiplier * result.fontSizePx
        : cssFontSizePx(lineHeight,
                        pieCssLengthContext(value.fontFamily,
                                            result.fontSizePx));
  }
  auto spacing = [&](const QString& source) {
    if (source.trimmed().compare(QLatin1String("normal"),
                                 Qt::CaseInsensitive) == 0)
      return 0.0;
    return cssFontSizePx(source, pieCssLengthContext(
        value.fontFamily, result.fontSizePx));
  };
  result.letterSpacingPx = spacing(value.letterSpacing);
  result.wordSpacingPx = spacing(value.wordSpacing);
  const QString decoration = value.textDecoration.toLower();
  result.underline = decoration.contains(QLatin1String("underline"));
  result.overline = decoration.contains(QLatin1String("overline"));
  result.strikeOut = decoration.contains(QLatin1String("line-through"));
  const QString transform = value.textTransform.trimmed().toLower();
  if (transform == QLatin1String("uppercase"))
    result.transform = requirement::RequirementTextTransform::UpperCase;
  else if (transform == QLatin1String("lowercase"))
    result.transform = requirement::RequirementTextTransform::LowerCase;
  else if (transform == QLatin1String("capitalize"))
    result.transform = requirement::RequirementTextTransform::Capitalize;
  result.color = color::resolveSvgPaint(
      value.color, color::SvgPaintKind::Text, QColor(Qt::black)).color;
  return result;
}

requirement::RequirementResolvedShapeStyle requirementShapeFromCss(
    const csscascade::ElementStyle& value, qreal parentFontSize,
    qreal diagonal) {
  requirement::RequirementResolvedShapeStyle result;
  result.fill = value.fill;
  result.stroke = value.stroke;
  result.strokeWidth = cssStrokeWidthPx(
      value.strokeWidth,
      pieCssLengthContext(value.fontFamily, parentFontSize), diagonal);
  result.fillOpacity = value.effectiveFillOpacity;
  result.strokeOpacity = value.effectiveStrokeOpacity;
  result.opacity = value.effectiveOpacity;
  result.visible = value.displayed();
  result.hasBox = value.display.compare(
      QLatin1String("none"), Qt::CaseInsensitive) != 0;
  result.rootHasBox = value.hasBox();
  return result;
}

requirement::RequirementComputedElement requirementComputedFromCss(
    const csscascade::ElementStyle& value) {
  requirement::RequirementComputedElement result;
  result.fill = value.fill;
  result.stroke = value.stroke;
  result.strokeWidth = value.strokeWidth;
  result.backgroundColor = value.backgroundColor;
  result.color = value.color;
  result.opacity = cssOpacity(value.opacity);
  result.fillOpacity = cssOpacity(value.fillOpacity);
  result.strokeOpacity = cssOpacity(value.strokeOpacity);
  result.effectiveOpacity = value.effectiveOpacity;
  result.effectiveFillOpacity = value.effectiveFillOpacity;
  result.effectiveStrokeOpacity = value.effectiveStrokeOpacity;
  result.display = value.display;
  result.visibility = value.visibility;
  result.displayed = value.displayed();
  result.hasBox = value.hasBox();
  result.ancestorRenderable = value.ancestorRenderable;
  result.ancestorHasBox = value.ancestorHasBox;
  result.fontFamily = value.fontFamily;
  result.fontSize = value.fontSize;
  result.fontWeight = value.fontWeight;
  result.fontStyle = value.fontStyle;
  return result;
}

QString jsReplaceFirst(QString value, const QString& before,
                       const QString& after = QString()) {
  const qsizetype at = value.indexOf(before);
  if (at >= 0) value.replace(at, before.size(), after);
  return value;
}

QHash<QString, QString> requirementStylesMap(
    const QStringList& declarations) {
  QHash<QString, QString> result;
  for (const QString& declaration : declarations) {
    const QStringList parts = declaration.split(QLatin1Char(':'));
    if (parts.isEmpty()) continue;
    const QString key = parts.first().trimmed();
    if (key.isEmpty()) continue;
    result.insert(key, parts.size() >= 2 ? parts.at(1).trimmed() : QString());
  }
  return result;
}

// requirementDiagram getStyles() (requirementDiagram-TGXJPOKE.mjs:1203-1275),
// ported rule-for-rule in the original order. Rules that match nothing in the
// generated SVG (reqBox/reqTitle/reqLabel/reqLabelBox/req-title-line/
// relationshipLabel) are kept: they participate in the cascade exactly as
// upstream injects them, and themeCSS interplay (specificity/order) must see
// the same sheet. genColor's palette rules lead the sheet like the template
// interpolation does.
QString requirementBaseCss(const flowtheme::FlowThemeVariables& theme,
                           const QString& look, int themeColorLimit) {
  const QString labelColor = theme.nodeTextColor.isEmpty()
      ? theme.textColor : theme.nodeTextColor;
  const QString labelBackground = theme.requirementEdgeLabelBackground
      .value_or(theme.edgeLabelBackground);
  QString genColor;
  if (!theme.borderColorArray.isEmpty()) {
    // `bkgColorArray?.length ? bkgColorArray[i] : ""` — an empty string (or an
    // out-of-range undefined) makes the fill declaration invalid, so the
    // browser drops it; csscascade::validDeclaration treats both the same.
    QString sections;
    for (int i = 0; i < themeColorLimit; ++i) {
      const QString fill = theme.bkgColorArray.isEmpty()
          ? QString() : theme.bkgColorArray.value(i);
      sections += QStringLiteral(
          "[data-look=\"%1\"][data-color-id=\"color-%2\"].node path{"
          "stroke:%3;fill:%4;}"
          "[data-look=\"%1\"][data-color-id=\"color-%2\"].node rect{"
          "stroke:%3;fill:%4;}")
          .arg(look).arg(i).arg(theme.borderColorArray.value(i), fill);
    }
    genColor = std::move(sections);
  }
  // .relationshipLine keeps the literal "1px" unless look==="neo" (which reads
  // the theme strokeWidth); requirementBorderSize is the wrapper rule width.
  return QStringLiteral(
      "%1"
      "marker{fill:%2;stroke:%2;}"
      "marker.cross{stroke:%3;}"
      "svg{font-family:%4;font-size:%5;}"
      ".reqBox{fill:%6;fill-opacity:1.0;stroke:%7;stroke-width:%8;}"
      ".reqTitle,.reqLabel{fill:%9;}"
      ".reqLabelBox{fill:%10;fill-opacity:1.0;}"
      ".req-title-line{stroke:%7;stroke-width:%8;}"
      ".relationshipLine{stroke:%11;stroke-width:%12;}"
      ".relationshipLabel{fill:%13;}"
      ".edgeLabel{background-color:%14;}"
      ".edgeLabel .label rect{fill:%14;}"
      ".edgeLabel .label text{fill:%13;}"
      ".divider{stroke:%15;stroke-width:1;}"
      ".label{font-family:%4;color:%16;}"
      ".label text,span{fill:%16;color:%16;}"
      ".labelBkg{background-color:%17;}")
      .arg(genColor, theme.relationColor, theme.lineColor,
           theme.fontFamily, theme.fontSize,
           theme.requirementBackground, theme.requirementBorderColor,
           theme.requirementBorderSize, theme.requirementTextColor,
           theme.relationLabelBackground, theme.relationColor,
           look == QLatin1String("neo")
               ? QString::number(theme.strokeWidth)
               : QStringLiteral("1px"),
           theme.relationLabelColor, theme.edgeLabelBackground,
           theme.nodeBorder, labelColor, labelBackground);
}

QString requirementInlineStyle(const QStringList& declarations,
                               bool textProperties) {
  static const QSet<QString> text = {
      QStringLiteral("color"), QStringLiteral("font-family"),
      QStringLiteral("font-size"), QStringLiteral("font-weight"),
      QStringLiteral("font-style"), QStringLiteral("line-height"),
      QStringLiteral("letter-spacing"), QStringLiteral("word-spacing"),
      QStringLiteral("text-decoration"), QStringLiteral("text-transform")};
  QStringList result;
  for (const QString& declaration : declarations) {
    const int colon = declaration.indexOf(QLatin1Char(':'));
    const QString property = (colon < 0 ? declaration
                                        : declaration.left(colon))
                                 .trimmed().toLower();
    if (text.contains(property) == textProperties)
      result.append(declaration + QStringLiteral(" !important"));
  }
  return result.join(QLatin1Char(';'));
}

requirement::RequirementLayoutInput resolveRequirementThemeCss(
    requirement::RequirementLayoutInput input,
    const requirement::RequirementScene& fallbackScene,
    requirement::RequirementSceneStyle& sceneStyle,
    const flowtheme::FlowThemeVariables& theme,
    const QString& look, const QString& themeCss) {
  using csscascade::ElementInput;
  using csscascade::ElementStyle;
  QVector<ElementInput> elements;
  ElementStyle root;
  root.fill = sceneStyle.foregroundFallback;
  root.stroke = QStringLiteral("none");
  root.strokeWidth = QStringLiteral("1px");
  root.color = QStringLiteral("black");
  root.fontFamily = sceneStyle.fontFamily;
  root.fontSize = QString::number(sceneStyle.fontSize) + QStringLiteral("px");
  root.fontWeight = QStringLiteral("400");
  root.fontStyle = QStringLiteral("normal");
  root.lineHeight = QStringLiteral("1.5");
  elements.append({QStringLiteral("svg"), {}, QStringLiteral("svg"),
                   QStringLiteral("diagram-root"),
                   {QStringLiteral("requirementDiagram")}, {}, root, {}});
  elements.append({QStringLiteral("root"), QStringLiteral("svg"),
                   QStringLiteral("g"), {}, {QStringLiteral("root")}, {},
                   root, {}});
  elements.append({QStringLiteral("edge-paths"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("edgePaths")},
                   {}, root, {}});
  elements.append({QStringLiteral("edge-labels"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("edgeLabels")},
                   {}, root, {}});
  elements.append({QStringLiteral("nodes"), QStringLiteral("root"),
                   QStringLiteral("g"), {}, {QStringLiteral("nodes")}, {},
                   root, {}});
  elements.append({QStringLiteral("defs"), QStringLiteral("svg"),
                   QStringLiteral("defs"), {}, {}, {}, root, {}});

  QHash<QString, const requirement::RequirementSceneNode*> fallbackNodes;
  for (const auto& node : fallbackScene.nodes)
    fallbackNodes.insert(node.id, &node);
  for (qsizetype index = 0; index < input.nodes.size(); ++index) {
    const auto& node = input.nodes.at(index);
    const auto* rendered = fallbackNodes.value(node.id, nullptr);
    if (!rendered) continue;
    const QString key = QStringLiteral("node-") + node.id;
    ElementStyle group = root;
    group.fill = sceneStyle.foregroundFallback;
    // `shapeSvg.attr("data-color-id", color-${colorIndex % len})` fires only
    // when the theme supplies borderColorArray (chunk-ZGVPDNZ5.mjs:5080) — the
    // same gate as genColor, so the palette rules in the base sheet can reach
    // this node (and themeCSS attribute selectors see the real DOM surface).
    QHash<QString, QString> groupAttributes{{QStringLiteral("data-look"), look}};
    if (!theme.borderColorArray.isEmpty()) {
      groupAttributes.insert(
          QStringLiteral("data-color-id"),
          QStringLiteral("color-%1")
              .arg(index % theme.borderColorArray.size()));
    }
    elements.append({key, QStringLiteral("nodes"), QStringLiteral("g"),
                     QStringLiteral("diagram-root-") + node.id,
                     QStringList{QStringLiteral("node"),
                                 QStringLiteral("default")} + node.cssClasses,
                     groupAttributes,
                     group, {}});
    ElementStyle box = root;
    elements.append({key + QStringLiteral("-box"), key, QStringLiteral("g"),
                     {}, {QStringLiteral("basic"),
                          QStringLiteral("label-container"),
                          QStringLiteral("outer-path")}, {}, box,
                     requirementInlineStyle(node.cssStyles, false)});
    ElementStyle fillPath = root;
    const QString fillValue = rendered->fillNone
        ? QStringLiteral("none") : rendered->fill;
    elements.append({key + QStringLiteral("-box-fill"),
                     key + QStringLiteral("-box"), QStringLiteral("path"),
                     {}, {}, {}, fillPath,
                     QStringLiteral("fill:%1;stroke:none;stroke-width:0px;fill-opacity:%2")
                         .arg(fillValue)
                         .arg(rendered->fillOpacity)});
    ElementStyle strokePath = root;
    const QString strokeValue = rendered->outlineVisible
        ? rendered->outlineStroke : QStringLiteral("none");
    elements.append({key + QStringLiteral("-box-stroke"),
                     key + QStringLiteral("-box"), QStringLiteral("path"),
                     {}, {}, {}, strokePath,
                     QStringLiteral("fill:none;stroke:%1;stroke-width:%2px")
                         .arg(strokeValue)
                         .arg(rendered->strokeWidth)});
    for (qsizetype rowIndex = 0; rowIndex < node.rows.size(); ++rowIndex) {
      ElementStyle label = root;
      const auto& row = rendered->rows.at(rowIndex);
      label.color = row.color.isValid() ? requirementCssColor(row.color)
                                        : sceneStyle.textColor;
      label.fontFamily = row.fontFamily;
      label.fontSize = QString::number(row.fontPixelSize) +
                       QStringLiteral("px");
      label.fontWeight = QString::number(int(
          row.document.baseWeight == QFont::Bold ? 700
                                                  : row.document.baseWeight));
      label.fontStyle = row.document.baseStyle == QFont::StyleNormal
          ? QStringLiteral("normal") : QStringLiteral("italic");
      label.lineHeight = QStringLiteral("1.5");
      const QString rowKey = key + QStringLiteral("-row-%1").arg(rowIndex);
      QString rowInline = requirementInlineStyle(node.cssStyles, true);
      if (rowIndex == 1) rowInline += QStringLiteral(";font-weight:bold");
      elements.append({rowKey, key, QStringLiteral("g"), {},
                       {QStringLiteral("label")}, {}, label,
                       rowInline});
      if (sceneStyle.htmlLabels) {
        const QString converted = jsReplaceFirst(
            rowInline, QStringLiteral("fill:"), QStringLiteral("color:"));
        elements.append({rowKey + QStringLiteral("-fo"), rowKey,
                         QStringLiteral("foreignObject"), {}, {}, {}, label, {}});
        elements.append({rowKey + QStringLiteral("-div"),
                         rowKey + QStringLiteral("-fo"), QStringLiteral("div"),
                         {}, {}, {}, label,
                         converted + QStringLiteral(
                             ";display:table-cell;white-space:nowrap;line-height:1.5")});
        elements.append({rowKey + QStringLiteral("-span"),
                         rowKey + QStringLiteral("-div"), QStringLiteral("span"),
                         {}, {QStringLiteral("nodeLabel"),
                              QStringLiteral("markdown-node-label")}, {}, label,
                         converted});
      }
    }
    if (node.hasBody) {
      // The divider is its OWN element: mermaid's `.divider { stroke: nodeBorder;
      // stroke-width: 1 }` class rule sets the base, and a node `style` statement
      // does NOT reach it (probed vs 11.16.0: `style X stroke-width:4` leaves the
      // divider at 1px — the node style is applied to the box label-container, and
      // the `.divider` class rule beats the inherited value). So the divider base
      // stroke-width is the rule's literal 1 (NOT the box requirementBorderSize
      // 1.3), and the node's cssStyles are NOT attached as inline declarations
      // here (only themeCSS `.divider { ... }` overrides may move it, e.g. 5px).
      ElementStyle divider = root;
      divider.stroke = sceneStyle.dividerColor;
      divider.strokeWidth = QStringLiteral("1px");
      const QString dividerKey = key + QStringLiteral("-divider");
      elements.append({dividerKey, key,
                       QStringLiteral("g"), {},
                       {QStringLiteral("divider")}, {}, divider, {}});
      const QHash<QString, QString> styles =
          requirementStylesMap(node.cssStyles);
      ElementStyle child = root;
      const QString rawStroke = styles.value(QStringLiteral("stroke"));
      child.stroke = rawStroke.isEmpty() ? sceneStyle.dividerColor : rawStroke;
      QString rawWidth = jsReplaceFirst(
          styles.value(QStringLiteral("stroke-width")), QStringLiteral("px"));
      child.strokeWidth = rawWidth.isEmpty() ? QStringLiteral("1.3") : rawWidth;
      child.fill = QStringLiteral("none");
      const QString presentation = QStringLiteral(
          "fill:none;stroke:%1;stroke-width:%2")
          .arg(child.stroke, child.strokeWidth);
      const QString nodeInline = requirementInlineStyle(node.cssStyles, false);
      const bool reqBgTruthy = theme.requirementEdgeLabelBackground.has_value() &&
          !theme.requirementEdgeLabelBackground->isEmpty();
      const bool applyPathStyle = !nodeInline.isEmpty() &&
          look != QLatin1String("handDrawn") &&
          (reqBgTruthy || !theme.borderColorArray.isEmpty());
      elements.append({dividerKey + QStringLiteral("-path-0"), dividerKey,
                       QStringLiteral("path"), {}, {}, {}, child,
                       applyPathStyle ? nodeInline : QString(), presentation});
    }
  }

  QHash<QString, const requirement::RequirementSceneEdge*> fallbackEdges;
  for (const auto& edge : fallbackScene.edges)
    fallbackEdges.insert(edge.id, &edge);
  for (qsizetype index = 0; index < input.edges.size(); ++index) {
    const auto& edge = input.edges.at(index);
    const auto* rendered = fallbackEdges.value(edge.id, nullptr);
    if (!rendered) continue;
    const QString pathKey = QStringLiteral("edge-path-") + edge.id;
    ElementStyle path = root;
    path.fill = QStringLiteral("none");
    path.stroke = sceneStyle.lineColor;
    path.strokeWidth = QStringLiteral("1px");
    elements.append({pathKey, QStringLiteral("edge-paths"),
                     QStringLiteral("path"),
                     QStringLiteral("diagram-root-") + edge.id,
                     {QStringLiteral("edge-thickness-normal"),
                      edge.isContains ? QStringLiteral("edge-pattern-solid")
                                      : QStringLiteral("edge-pattern-dashed"),
                      QStringLiteral("relationshipLine")},
                     {{QStringLiteral("data-id"), edge.id}}, path,
                     edge.isContains ? QStringLiteral("fill:none")
                                     : QStringLiteral("fill:none;stroke-dasharray:10,7")});
    const QString edgeGroup = QStringLiteral("edge-label-") + edge.id;
    elements.append({edgeGroup, QStringLiteral("edge-labels"),
                     QStringLiteral("g"), {},
                     {QStringLiteral("edgeLabel")}, {}, root, {}});
    ElementStyle label = root;
    // `.edgeLabel .label` is the inner <g>. Its COMPUTED background-color is
    // transparent (CSS initial) — the VISIBLE edge-label background lives on the
    // HTML <span> inside the foreignObject (matched by `.edgeLabel`, which sets
    // background-color: edgeLabelBackground), NOT on this <g>. A themeCSS rule on
    // `.edgeLabel .label` itself can set the <g> background (non-rendering for an
    // SVG <g>, but getComputedStyle reports it) without moving the span. So this
    // element's background starts transparent; only the cascade (themeCSS) may
    // override it. The `color` DOES inherit to the span (the span has no own color
    // rule), so it is seeded from edgeLabelColor. (probed vs mermaid 11.16.0)
    label.color = sceneStyle.edgeLabelColor;
    elements.append({edgeGroup + QStringLiteral("-label"), edgeGroup,
                     QStringLiteral("g"), {}, {QStringLiteral("label")},
                     {{QStringLiteral("data-id"), edge.id}}, label, {}});
    if (sceneStyle.htmlLabels) {
      const QString inner = edgeGroup + QStringLiteral("-label");
      elements.append({inner + QStringLiteral("-fo"), inner,
                       QStringLiteral("foreignObject"), {}, {}, {}, label, {}});
      ElementStyle container = label;
      container.backgroundColor = theme.requirementEdgeLabelBackground
          .value_or(theme.edgeLabelBackground);
      elements.append({inner + QStringLiteral("-div"),
                       inner + QStringLiteral("-fo"), QStringLiteral("div"), {},
                       {QStringLiteral("labelBkg")}, {}, container,
                       QStringLiteral(
                           "display:table-cell;white-space:nowrap;line-height:1.5")});
      ElementStyle span = label;
      span.color = sceneStyle.edgeLabelColor;
      span.fill = sceneStyle.edgeLabelColor;
      span.backgroundColor = theme.edgeLabelBackground;
      elements.append({inner + QStringLiteral("-span"),
                       inner + QStringLiteral("-div"), QStringLiteral("span"), {},
                       {QStringLiteral("edgeLabel"),
                        QStringLiteral("markdown-node-label")}, {}, span, {}});
    }
  }
  for (const QString& markerType : {QStringLiteral("requirement_contains"),
                                    QStringLiteral("requirement_arrow")}) {
    ElementStyle marker = root;
    marker.fill = sceneStyle.lineColor;
    marker.stroke = sceneStyle.lineColor;
    elements.append({QStringLiteral("marker-") + markerType,
                     QStringLiteral("defs"), QStringLiteral("marker"),
                     QStringLiteral("diagram-root-") + markerType,
                     markerType == QLatin1String("requirement_contains")
                         ? QStringList{QStringLiteral("cross")}
                         : QStringList{}, {}, marker, {}});
  }

  const auto css = csscascade::resolveElements(
      themeCss, elements,
      requirementBaseCss(theme, look, sceneStyle.themeColorLimit));
  const qreal diagonal = std::hypot(fallbackScene.bounds.width(),
                                    fallbackScene.bounds.height()) /
                         std::sqrt(2.0);
  QHash<QString, qreal> sizes;
  for (const ElementInput& element : elements) {
    const auto value = css.value(element.key);
    const qreal parentSize = sizes.value(element.parentKey,
                                         sceneStyle.fontSize);
    const qreal usedSize = cssFontSizePx(
        value.fontSize, pieCssLengthContext(value.fontFamily, parentSize));
    sizes.insert(element.key, usedSize);
  }
  for (auto& node : input.nodes) {
    const QString key = QStringLiteral("node-") + node.id;
    const auto group = css.value(key);
    node.groupVisible = group.displayed();
    node.groupHasBox = group.display.compare(
        QLatin1String("none"), Qt::CaseInsensitive) != 0;
    node.groupRootHasBox = group.hasBox();
    node.hasResolvedBoxStyle = true;
    const auto wrapper = css.value(key + QStringLiteral("-box"));
    const auto fill = requirementShapeFromCss(
        css.value(key + QStringLiteral("-box-fill")), sizes.value(key),
        diagonal);
    const auto stroke = requirementShapeFromCss(
        css.value(key + QStringLiteral("-box-stroke")), sizes.value(key),
        diagonal);
    node.boxStyle.fill = fill.fill;
    node.boxStyle.stroke = stroke.stroke;
    node.boxStyle.strokeWidth = stroke.strokeWidth;
    node.boxStyle.fillOpacity = fill.fillOpacity;
    node.boxStyle.strokeOpacity = stroke.strokeOpacity;
    node.boxStyle.opacity = wrapper.effectiveOpacity;
    node.boxStyle.visible = group.displayed();
    node.boxStyle.hasBox = wrapper.display.compare(
        QLatin1String("none"), Qt::CaseInsensitive) != 0;
    node.boxStyle.rootHasBox = wrapper.hasBox();
    for (qsizetype rowIndex = 0; rowIndex < node.rows.size(); ++rowIndex) {
      auto& row = node.rows[rowIndex];
      const QString rowKey = key + QStringLiteral("-row-%1").arg(rowIndex);
      const auto wrapperValue = css.value(rowKey);
      const QString paintedKey = sceneStyle.htmlLabels
          ? rowKey + QStringLiteral("-span") : rowKey;
      const auto value = css.value(paintedKey);
      row.wrapperComputed = requirementComputedFromCss(wrapperValue);
      row.paintedTextComputed = requirementComputedFromCss(value);
      row.resolvedStyle = requirementTextFromCss(value, sizes.value(rowKey));
      row.hasResolvedStyle = true;
      row.opacity = value.effectiveOpacity;
      row.visible = value.displayed();
      row.hasBox = value.display.compare(
          QLatin1String("none"), Qt::CaseInsensitive) != 0;
      row.rootHasBox = value.hasBox();
    }
    if (node.hasBody) {
      const QString dividerKey = key + QStringLiteral("-divider");
      node.hasResolvedDividerStyle = true;
      node.dividerStyle = requirementShapeFromCss(
          css.value(dividerKey + QStringLiteral("-path-0")),
          sizes.value(dividerKey),
          diagonal);
      node.dividerWrapperComputed = requirementComputedFromCss(
          css.value(dividerKey));
      node.dividerChildPathComputed = {requirementComputedFromCss(
          css.value(dividerKey + QStringLiteral("-path-0")))};
    }
  }
  for (auto& edge : input.edges) {
    const QString pathKey = QStringLiteral("edge-path-") + edge.id;
    edge.hasResolvedPathStyle = true;
    edge.pathStyle = requirementShapeFromCss(
        css.value(pathKey), sizes.value(QStringLiteral("edge-paths")),
        diagonal);
    const QString outerKey = QStringLiteral("edge-label-") + edge.id;
    const QString labelKey = outerKey + QStringLiteral("-label");
    const QString paintedKey = sceneStyle.htmlLabels
        ? labelKey + QStringLiteral("-span") : labelKey;
    const auto label = css.value(paintedKey);
    edge.outerLabelComputed = requirementComputedFromCss(css.value(outerKey));
    edge.innerLabelComputed = requirementComputedFromCss(css.value(labelKey));
    edge.containerBgComputed = requirementComputedFromCss(
        css.value(sceneStyle.htmlLabels ? labelKey + QStringLiteral("-div")
                                        : labelKey));
    edge.paintedSpanComputed = requirementComputedFromCss(label);
    edge.hasLabelCascade = true;
    edge.resolvedLabel.text = edge.label;
    edge.resolvedLabel.resolvedStyle = requirementTextFromCss(
        label, sizes.value(labelKey));
    edge.resolvedLabel.hasResolvedStyle = true;
    edge.resolvedLabel.opacity = label.effectiveOpacity;
    edge.resolvedLabel.visible = label.displayed();
    edge.resolvedLabel.hasBox = label.display.compare(
        QLatin1String("none"), Qt::CaseInsensitive) != 0;
    edge.resolvedLabel.rootHasBox = label.hasBox();
    edge.labelBackgroundStyle.fill = edge.innerLabelComputed.backgroundColor;
  }
  for (const QString& markerType : {QStringLiteral("requirement_contains"),
                                    QStringLiteral("requirement_arrow")}) {
    const auto value = css.value(QStringLiteral("marker-") + markerType);
    sceneStyle.markerStyles.insert(markerType, requirementShapeFromCss(
        value, sceneStyle.fontSize, diagonal));
  }
  return input;
}

// requirementDiagram behind the Diagram contract. Requirement routes through
// the shared dagre pipeline (no custom layout). The upstream renderer reads
// config.state for spacing (nodeSpacing/rankSpacing/titleTopMargin/useMaxWidth),
// so this adapter mirrors that: it never reads config.requirement (all
// requirement-specific fields are upstream-inert per the config-matrix finding).
struct RequirementDiagramImpl : Diagram {
  QStringList ids() const override { return {QStringLiteral("requirement")}; }
  QString cssClass() const override { return QStringLiteral("requirementDiagram"); }

  MermaidRenderEntry render(const MermaidPreprocessResult& pre, const QString& type,
                            const QString& theme) const override {
    const requirement::RequirementDiagram diagram =
        requirement::RequirementDiagram::parse(pre.code);
    const QString configuredTheme = themeFromConfig(pre.config);
    const flowtheme::FlowThemeVariables themeVars = flowtheme::resolveFlowTheme(
        themeIdFromName(configuredTheme.isEmpty() ? theme : configuredTheme),
        themeOverrides(pre.config));
    requirement::RequirementLayoutInput input =
        requirement::buildRequirementLayoutInput(diagram.data());
    const QString fontFamily = firstFontFamily(themeVars.fontFamily);
    const qreal fontSize = pixelValue(themeVars.fontSize, 16.0);
    const QJsonValue rawHtmlLabels =
        pre.config.value(QStringLiteral("htmlLabels"));
    const bool htmlLabels = rawHtmlLabels.isUndefined() || rawHtmlLabels.isNull()
        ? true : evaluateConfigValue(rawHtmlLabels);
    const QJsonValue rawLook = pre.config.value(QStringLiteral("look"));
    const QString look = rawLook.isString() ? rawLook.toString()
                                             : QStringLiteral("classic");
    // Requirement reads config.state for spacing (RequirementDB.getData →
    // conf = config.state → nodeSpacing/rankSpacing). titleTopMargin likewise.
    const QJsonObject stateConfig = pre.config.value(QStringLiteral("state")).toObject();
    const qreal nodeSpacing = configNumber(stateConfig, QStringLiteral("nodeSpacing"), 50.0);
    const qreal rankSpacing = configNumber(stateConfig, QStringLiteral("rankSpacing"), 50.0);
    requirement::RequirementSceneStyle style;
    style.boxFill = themeVars.mainBkg;
    style.boxStroke = themeVars.border1;
    // requirement label text color = `nodeTextColor || textColor` (requirement
    // styles.js: `.label { color: ${options.nodeTextColor || options.textColor} }`).
    // For the default/forest/neutral themes nodeTextColor is NOT derived
    // (updateColors leaves it empty, so getStyles falls back to textColor) — probed
    // vs mermaid 11.16.0: the injected rule resolves to textColor #333, NOT
    // primaryTextColor #131300 (= invert(primaryColor), which only feeds fields
    // that explicitly read primaryTextColor). Use nodeTextColor when a theme sets
    // it, else textColor.
    style.textColor = themeVars.nodeTextColor.isEmpty()
                          ? themeVars.textColor : themeVars.nodeTextColor;
    style.dividerColor = themeVars.nodeBorder.isEmpty() ? themeVars.border1 : themeVars.nodeBorder;
    style.lineColor = themeVars.lineColor;
    style.edgeLabelFill = themeVars.edgeLabelBackground.isEmpty()
                              ? themeVars.mainBkg : themeVars.edgeLabelBackground;
    style.edgeLabelContainerFill = themeVars.requirementEdgeLabelBackground
                                       .value_or(style.edgeLabelFill);
    style.edgeLabelColor = themeVars.textColor;
    style.titleColor = themeVars.titleColor;
    // requirementBox default border size is 1.3 (userNodeOverrides:
    // strokeWidth = stylesMap["stroke-width"]?.replace("px","") || 1.3) — it is
    // NOT the theme's generic strokeWidth (which defaults to 1), so hardcode the
    // family default. foregroundFallback = the svg-inherited color used when an
    // inline fill/color is invalid (#333 default / #ccc dark = theme textColor).
    style.foregroundFallback = themeVars.textColor;
    style.strokeWidth = 1.3;
    style.fontFamily = fontFamily;
    style.fontSize = fontSize;
    style.lineHeight = fontSize * 1.5;
    style.htmlLabels = htmlLabels;
    // SVG-root base font-weight (the weight bolder/lighter resolve against at the
    // first cascade layer). mermaid's theme does not expose a font-weight variable,
    // so it is the browser default 400 (Normal).
    style.fontWeight = QFont::Normal;
    // Commit 4 colorIndex palette (empty for the 9 standard themes; populated by
    // redux-color / redux-dark-color). The scene cycles node colors by insertion
    // order only when borderColorArray is non-empty.
    style.borderColorArray = themeVars.borderColorArray;
    style.bkgColorArray = themeVars.bkgColorArray;
    // NOTE: upstream ignores user-supplied borderColorArray/bkgColorArray via the
    // %%{init}%% SOURCE entry (only the external mermaid.initialize() API honors
    // them — verified G:/github/req-probe/step4-source-entry-report.json). The
    // built-in redux-color/redux-dark-color palette above is the only source-path
    // palette; custom arrays are not a %%{init}%% parity feature.
    // genColor emits palette rules for color-0..color-(THEME_COLOR_LIMIT-1). The
    // limit defaults to 12 and is user-configurable via themeVariables
    // .THEME_COLOR_LIMIT (top-level init.THEME_COLOR_LIMIT is upstream-ignored).
    // Read the RAW config value with JS Number()+ceil semantics (2.5->3, true->1,
    // false/"abc"->0, null/absent->keep default); fall back to the theme default.
    style.themeColorLimit = jsThemeColorLimit(pre.config).value_or(themeVars.themeColorLimit);
    requirement::RequirementLayoutMeasurements measurements =
        requirement::measureRequirementLayoutInput(input, fontFamily, fontSize,
                                                   QFont::Normal, htmlLabels);
    requirement::RequirementPlacementResult placement =
        requirement::layoutRequirementDiagramDagre(
            input, measurements, nodeSpacing, rankSpacing, fontFamily,
            fontSize, htmlLabels);
    const QString themeCss =
        pre.config.value(QStringLiteral("themeCSS")).toString();
    if (!themeCss.trimmed().isEmpty()) {
      const requirement::RequirementScene fallback =
          requirement::buildRequirementScene(input, measurements, placement,
                                             style);
      input = resolveRequirementThemeCss(std::move(input), fallback, style,
                                         themeVars, look, themeCss);
      measurements = requirement::measureRequirementLayoutInput(
          input, fontFamily, fontSize, QFont::Normal, htmlLabels);
      placement = requirement::layoutRequirementDiagramDagre(
          input, measurements, nodeSpacing, rankSpacing, fontFamily,
          fontSize, htmlLabels);
    }
    // No inline `title` token in requirementDiagram grammar — pass empty so
    // renderMetadata falls back to the frontmatter title (pre.title).
    MermaidRenderMetadata metadata = renderMetadata(
        pre, type, QString(), diagram.data().accTitle,
        diagram.data().accDescription, style.titleColor, style.fontFamily, 18.0,
        configNumber(stateConfig, QStringLiteral("titleTopMargin"), 25.0), 8.0);
    requirement::RequirementScene scene =
        requirement::buildRequirementScene(input, measurements, placement, std::move(style));
    MermaidRenderEntry entry;
    entry.status = MermaidRenderStatus::Ready;
    entry.naturalSize = QSize(qCeil(scene.bounds.width()), qCeil(scene.bounds.height()));
    entry.scene = std::make_shared<const requirement::RequirementScene>(std::move(scene));
    finalizeReadyEntry(entry, std::move(metadata));
    return entry;
  }
};

}  // namespace

const Diagram& requirementDiagramAdapter() {
  static const RequirementDiagramImpl impl;
  return impl;
}

}  // namespace muffin::mermaid::editor
