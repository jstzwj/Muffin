#include "mermaid/theme/MermaidCssCascade.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/theme/FlowStyleResolve.h"
#include "mermaid/theme/MermaidColor.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeParser.h"

#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace muffin::mermaid::csscascade {
namespace {

bool cssWide(const QString& value) {
  const QString lower = value.trimmed().toLower();
  return lower == QLatin1String("inherit") || lower == QLatin1String("initial") ||
         lower == QLatin1String("unset") || lower == QLatin1String("revert") ||
         lower == QLatin1String("revert-layer");
}

bool validPaint(const QString& value) {
  const QString text = value.trimmed();
  const QString lower = text.toLower();
  return cssWide(text) || lower == QLatin1String("none") ||
         lower == QLatin1String("currentcolor") ||
         (lower.startsWith(QLatin1String("url(")) &&
          lower.endsWith(QLatin1Char(')'))) ||
         color::isParsableColor(text);
}

bool validDeclaration(const CssDeclaration& declaration) {
  const QString& property = declaration.property;
  const QString value = declaration.value.trimmed();
  if (property.startsWith(QLatin1String("--"))) return true;
  if (value.contains(QLatin1String("var("), Qt::CaseInsensitive)) return true;
  if (property == QLatin1String("fill") || property == QLatin1String("stroke") ||
      property == QLatin1String("color") || property == QLatin1String("background") ||
      property == QLatin1String("background-color"))
    return validPaint(value);
  if (property == QLatin1String("stroke-width")) {
    if (cssWide(value) || value == QLatin1String("0")) return true;
    static const QRegularExpression re(QStringLiteral(
        R"(^[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?(?:px|em|rem|ex|ch|pt|pc|in|cm|mm|q|vw|vh|vmin|vmax|%)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
  }
  if (property == QLatin1String("font-size")) {
    if (cssWide(value)) return true;
    static const QRegularExpression re(QStringLiteral(
        R"(^[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?(?:px|em|rem|ex|ch|pt|pc|in|cm|mm|q|vw|vh|vmin|vmax|%)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
  }
  if (property == QLatin1String("line-height") ||
      property == QLatin1String("letter-spacing") ||
      property == QLatin1String("word-spacing")) {
    if (cssWide(value) || value.compare(QLatin1String("normal"),
                                        Qt::CaseInsensitive) == 0)
      return true;
    static const QRegularExpression re(QStringLiteral(
        R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?(?:px|em|rem|ex|ch|pt|pc|in|cm|mm|q|vw|vh|vmin|vmax|%)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
  }
  if (property == QLatin1String("text-decoration") ||
      property == QLatin1String("text-transform") ||
      property == QLatin1String("background-color"))
    return true;
  if (property == QLatin1String("font-weight")) {
    const QString lower = value.toLower();
    if (cssWide(value) || lower == QLatin1String("normal") ||
        lower == QLatin1String("bold") || lower == QLatin1String("bolder") ||
        lower == QLatin1String("lighter")) return true;
    bool ok = false;
    const int weight = value.toInt(&ok);
    return ok && weight >= 1 && weight <= 1000;
  }
  if (property == QLatin1String("font-style")) {
    const QString lower = value.toLower();
    return cssWide(value) || lower == QLatin1String("normal") ||
           lower == QLatin1String("italic") ||
           lower == QLatin1String("oblique") ||
           lower.startsWith(QLatin1String("oblique "));
  }
  if (property == QLatin1String("text-anchor")) {
    const QString lower = value.toLower();
    return cssWide(value) || lower == QLatin1String("start") ||
           lower == QLatin1String("middle") || lower == QLatin1String("end");
  }
  if (property == QLatin1String("dominant-baseline")) return true;
  if (property == QLatin1String("opacity") ||
      property == QLatin1String("fill-opacity") ||
      property == QLatin1String("stroke-opacity")) {
    if (cssWide(value)) return true;
    static const QRegularExpression re(QStringLiteral(
        R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?%?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
  }
  if (property == QLatin1String("display")) {
    static const QSet<QString> values = {
        QStringLiteral("none"), QStringLiteral("inline"),
        QStringLiteral("block"), QStringLiteral("inline-block"),
        QStringLiteral("contents"), QStringLiteral("flex"),
        QStringLiteral("grid"), QStringLiteral("table")};
    return cssWide(value) || values.contains(value.toLower());
  }
  if (property == QLatin1String("visibility")) {
    const QString lower = value.toLower();
    return cssWide(value) || lower == QLatin1String("visible") ||
           lower == QLatin1String("hidden") || lower == QLatin1String("collapse");
  }
  return true;
}

std::vector<CssDeclaration> validDeclarations(
    const std::vector<CssDeclaration>& input) {
  std::vector<CssDeclaration> result;
  for (const CssDeclaration& declaration : input)
    if (validDeclaration(declaration)) result.push_back(declaration);
  return result;
}

CssThemeSheet flowSheet(const flowchart::FlowchartData& data,
                        const flowtheme::FlowThemeVariables& theme,
                        const QString& themeCss,
                        bool swimlane) {
  const QString builtIn = QStringLiteral(
      ".label { font-family:%1; color:%2; }"
      ".node rect,.node circle,.node ellipse,.node polygon,.node path {"
      "fill:%3;stroke:%4;stroke-width:%5px;}"
      ".node .label { color:%6; font-family:%1; font-size:%7; }"
      ".flowchart-link,.edgePath .path { stroke:%8;stroke-width:%5px;fill:none;}"
      ".edgeLabel { color:%2;font-family:%1;font-size:%7;background-color:%9;}"
      ".cluster rect { fill:%10;stroke:%11;stroke-width:1px;}"
      ".cluster-label { font-family:%1;font-size:%7;}"
      ".cluster-label text,.cluster-label span { color:%12;fill:%12;}")
      .arg(theme.fontFamily, theme.textColor, theme.mainBkg,
           theme.nodeBorder, QString::number(theme.strokeWidth),
           theme.nodeTextColor.isEmpty() ? theme.textColor : theme.nodeTextColor,
           theme.fontSize, theme.lineColor, theme.edgeLabelBackground,
           theme.clusterBkg, theme.clusterBorder, theme.titleColor);
  CssThemeSheet sheet = CssThemeParser::parse(builtIn, {});
  if (swimlane) {
    const CssThemeSheet extra = CssThemeParser::parse(
        QStringLiteral(".swimlane.cluster rect{stroke:%1 !important;}"
                       "[data-look=neo].cluster rect{filter:none;}")
            .arg(theme.clusterBorder), {});
    for (const CssRule& rule : extra.rules()) sheet.addRule(rule);
  }
  if (!themeCss.trimmed().isEmpty()) {
    const CssThemeSheet user = CssThemeParser::parse(themeCss, {});
    for (const CssRule& input : user.rules()) {
      CssRule rule = input;
      rule.selectors.clear();
      for (const QString& selector : input.selectors)
        rule.selectors.append(QStringLiteral("#diagram-root ") + selector);
      rule.declarations = validDeclarations(input.declarations);
      if (!rule.selectors.isEmpty() && !rule.declarations.empty())
        sheet.addRule(std::move(rule));
    }
  }

  // createCssStyles appends classDef rules after themeCSS. They are author
  // stylesheet declarations with !important, not element inline styles.
  for (const flowchart::FlowClass& definition : data.classes) {
    std::vector<CssDeclaration> declarations;
    for (const QString& source : definition.styles) {
      auto parsed = CssThemeParser::parseDeclarations(source);
      for (CssDeclaration& declaration : parsed) {
        declaration.important = true;
        if (validDeclaration(declaration)) declarations.push_back(declaration);
      }
    }
    if (!declarations.empty()) {
      CssRule rule;
      rule.selectors = {
          QStringLiteral("#diagram-root .") + definition.id + QStringLiteral(" > *"),
          QStringLiteral("#diagram-root .") + definition.id + QStringLiteral(" span")};
      rule.declarations = declarations;
      sheet.addRule(std::move(rule));
    }
    std::vector<CssDeclaration> textDeclarations;
    for (const QString& source : definition.textStyles) {
      auto parsed = CssThemeParser::parseDeclarations(source);
      for (CssDeclaration declaration : parsed) {
        if (declaration.property == QLatin1String("color"))
          declaration.property = QStringLiteral("fill");
        declaration.important = true;
        if (validDeclaration(declaration)) textDeclarations.push_back(declaration);
      }
    }
    if (!textDeclarations.empty()) {
      CssRule rule;
      rule.selectors = {QStringLiteral("#diagram-root .") + definition.id +
                        QStringLiteral(" tspan")};
      rule.declarations = std::move(textDeclarations);
      sheet.addRule(std::move(rule));
    }
  }
  return sheet;
}

std::vector<CssDeclaration> inlineStyle(QString text) {
  return validDeclarations(CssThemeParser::parseDeclarations(text));
}

bool svgInheritedProperty(const QString& property) {
  static const QSet<QString> inherited = {
      QStringLiteral("color"), QStringLiteral("fill"),
      QStringLiteral("stroke"), QStringLiteral("stroke-width"),
      QStringLiteral("fill-opacity"), QStringLiteral("stroke-opacity"),
      QStringLiteral("font-family"), QStringLiteral("font-size"),
      QStringLiteral("font-weight"), QStringLiteral("font-style"),
      QStringLiteral("line-height"), QStringLiteral("letter-spacing"),
      QStringLiteral("word-spacing"),
      QStringLiteral("text-anchor"), QStringLiteral("dominant-baseline"),
      QStringLiteral("visibility")};
  return inherited.contains(property);
}

QString initialValue(const QString& property, const QString& fallback) {
  if (property == QLatin1String("fill") || property == QLatin1String("color"))
    return QStringLiteral("black");
  if (property == QLatin1String("stroke")) return QStringLiteral("none");
  if (property == QLatin1String("stroke-width")) return QStringLiteral("1px");
  if (property == QLatin1String("font-size")) return QStringLiteral("16px");
  if (property == QLatin1String("font-family")) return QStringLiteral("serif");
  if (property == QLatin1String("font-weight")) return QStringLiteral("400");
  if (property == QLatin1String("font-style")) return QStringLiteral("normal");
  if (property == QLatin1String("line-height")) return QStringLiteral("normal");
  if (property == QLatin1String("letter-spacing")) return QStringLiteral("normal");
  if (property == QLatin1String("word-spacing")) return QStringLiteral("0px");
  if (property == QLatin1String("text-decoration")) return QStringLiteral("none");
  if (property == QLatin1String("text-transform")) return QStringLiteral("none");
  if (property == QLatin1String("background-color"))
    return QStringLiteral("transparent");
  if (property == QLatin1String("text-anchor")) return QStringLiteral("start");
  if (property == QLatin1String("dominant-baseline")) return QStringLiteral("auto");
  if (property == QLatin1String("display")) return QStringLiteral("inline");
  if (property == QLatin1String("visibility")) return QStringLiteral("visible");
  if (property == QLatin1String("opacity")) return QStringLiteral("1");
  if (property == QLatin1String("fill-opacity") ||
      property == QLatin1String("stroke-opacity"))
    return QStringLiteral("1");
  return fallback;
}

QString inheritedValue(const ElementStyle& parent, const QString& property,
                       const QString& fallback) {
  if (property == QLatin1String("fill")) return parent.fill;
  if (property == QLatin1String("stroke")) return parent.stroke;
  if (property == QLatin1String("stroke-width")) return parent.strokeWidth;
  if (property == QLatin1String("color")) return parent.color;
  if (property == QLatin1String("font-family")) return parent.fontFamily;
  if (property == QLatin1String("font-size")) return parent.fontSize;
  if (property == QLatin1String("font-weight")) return parent.fontWeight;
  if (property == QLatin1String("font-style")) return parent.fontStyle;
  if (property == QLatin1String("line-height")) return parent.lineHeight;
  if (property == QLatin1String("letter-spacing")) return parent.letterSpacing;
  if (property == QLatin1String("word-spacing")) return parent.wordSpacing;
  if (property == QLatin1String("text-anchor")) return parent.textAnchor;
  if (property == QLatin1String("dominant-baseline"))
    return parent.dominantBaseline;
  if (property == QLatin1String("visibility")) return parent.visibility;
  if (property == QLatin1String("opacity")) return parent.opacity;
  if (property == QLatin1String("fill-opacity")) return parent.fillOpacity;
  if (property == QLatin1String("stroke-opacity")) return parent.strokeOpacity;
  if (property == QLatin1String("mix-blend-mode")) return parent.mixBlendMode;
  if (property == QLatin1String("display")) return parent.display;
  return fallback;
}

QString validFallbackValue(const QString& property, const QString& candidate,
                          const ElementStyle* parent) {
  CssDeclaration declaration;
  declaration.property = property;
  declaration.value = candidate;
  if (!candidate.trimmed().isEmpty() && validDeclaration(declaration))
    return candidate;
  if (svgInheritedProperty(property) && parent)
    return inheritedValue(*parent, property, candidate);
  return initialValue(property, candidate);
}

QString value(const CssComputedStyle& style, const QString& property,
              const QString& fallback, const ElementStyle* parent) {
  const QString resolved = style.resolvedValue(property);
  if (!resolved.isEmpty()) {
    const QString lower = resolved.trimmed().toLower();
    if (lower == QLatin1String("inherit")) {
      if (!parent) return initialValue(property, fallback);
      return inheritedValue(*parent, property, fallback);
    }
    if (lower == QLatin1String("unset")) {
      if (svgInheritedProperty(property) && parent)
        return inheritedValue(*parent, property, fallback);
      return initialValue(property, fallback);
    }
    if (lower == QLatin1String("revert") || lower == QLatin1String("revert-layer"))
      return fallback;
    if (lower == QLatin1String("initial")) return initialValue(property, fallback);
    return resolved;
  }
  if (style.rawValue(property).contains(QLatin1String("var("),
                                        Qt::CaseInsensitive))
    return fallback;
  if (!style.hasProperty(property)) {
    // CSS inheritance: with no declaration on this element (and none inherited
    // through the engine's rule-only ancestor walk), an inherited property
    // takes the parent's *computed* value — which may itself come from the
    // parent's inline style, presentation attributes or theme fallback, none
    // of which the engine's parentStyleFor can see. visibility is the
    // load-bearing case: a parent `visibility:hidden` must reach children
    // that don't redeclare it, while a child `visibility:visible` declaration
    // still recovers (it wins as a matching rule above).
    if (property == QLatin1String("visibility") && parent)
      return parent->visibility;
    return fallback;
  }
  if (property == QLatin1String("fill") || property == QLatin1String("color"))
    return QStringLiteral("inherit");
  if (property == QLatin1String("stroke")) return QStringLiteral("none");
  if (property == QLatin1String("stroke-width")) return QStringLiteral("1px");
  return fallback;
}

ElementStyle project(const CssComputedStyle& style,
                     const ElementStyle& fallback,
                     const ElementStyle* parent = nullptr) {
  ElementStyle usedFallback = fallback;
  usedFallback.fill = validFallbackValue(QStringLiteral("fill"), fallback.fill, parent);
  usedFallback.stroke = validFallbackValue(QStringLiteral("stroke"), fallback.stroke, parent);
  usedFallback.strokeWidth = validFallbackValue(
      QStringLiteral("stroke-width"), fallback.strokeWidth, parent);
  usedFallback.color = validFallbackValue(QStringLiteral("color"), fallback.color, parent);
  usedFallback.fontSize = validFallbackValue(
      QStringLiteral("font-size"), fallback.fontSize, parent);
  usedFallback.fontWeight = validFallbackValue(
      QStringLiteral("font-weight"), fallback.fontWeight, parent);
  usedFallback.fontStyle = validFallbackValue(
      QStringLiteral("font-style"), fallback.fontStyle, parent);
  usedFallback.lineHeight = validFallbackValue(
      QStringLiteral("line-height"), fallback.lineHeight, parent);
  usedFallback.letterSpacing = validFallbackValue(
      QStringLiteral("letter-spacing"), fallback.letterSpacing, parent);
  usedFallback.wordSpacing = validFallbackValue(
      QStringLiteral("word-spacing"), fallback.wordSpacing, parent);
  usedFallback.textAnchor = validFallbackValue(
      QStringLiteral("text-anchor"), fallback.textAnchor, parent);
  usedFallback.dominantBaseline = validFallbackValue(
      QStringLiteral("dominant-baseline"), fallback.dominantBaseline, parent);
  usedFallback.display = validFallbackValue(
      QStringLiteral("display"), fallback.display, parent);
  usedFallback.visibility = validFallbackValue(
      QStringLiteral("visibility"), fallback.visibility, parent);
  usedFallback.opacity = validFallbackValue(
      QStringLiteral("opacity"), fallback.opacity, parent);
  usedFallback.fillOpacity = validFallbackValue(
      QStringLiteral("fill-opacity"), fallback.fillOpacity, parent);
  usedFallback.strokeOpacity = validFallbackValue(
      QStringLiteral("stroke-opacity"), fallback.strokeOpacity, parent);
  ElementStyle result;
  result.fill = value(style, QStringLiteral("fill"), usedFallback.fill, parent);
  result.stroke = value(style, QStringLiteral("stroke"), usedFallback.stroke, parent);
  result.strokeWidth = value(style, QStringLiteral("stroke-width"), usedFallback.strokeWidth, parent);
  result.color = value(style, QStringLiteral("color"), usedFallback.color, parent);
  result.fontFamily = value(style, QStringLiteral("font-family"), usedFallback.fontFamily, parent);
  result.fontSize = value(style, QStringLiteral("font-size"), usedFallback.fontSize, parent);
  result.fontWeight = value(style, QStringLiteral("font-weight"), usedFallback.fontWeight, parent);
  result.fontStyle = value(style, QStringLiteral("font-style"), usedFallback.fontStyle, parent);
  result.lineHeight = value(style, QStringLiteral("line-height"),
                            usedFallback.lineHeight, parent);
  result.letterSpacing = value(style, QStringLiteral("letter-spacing"),
                               usedFallback.letterSpacing, parent);
  result.wordSpacing = value(style, QStringLiteral("word-spacing"),
                             usedFallback.wordSpacing, parent);
  result.textDecoration = value(style, QStringLiteral("text-decoration"),
                                fallback.textDecoration, parent);
  result.textTransform = value(style, QStringLiteral("text-transform"),
                               fallback.textTransform, parent);
  result.backgroundColor = value(style, QStringLiteral("background-color"),
                                 fallback.backgroundColor, parent);
  result.textAnchor = value(style, QStringLiteral("text-anchor"),
                            usedFallback.textAnchor, parent);
  result.dominantBaseline = value(style, QStringLiteral("dominant-baseline"),
                                  usedFallback.dominantBaseline, parent);
  result.display = value(style, QStringLiteral("display"), usedFallback.display, parent);
  result.visibility = value(style, QStringLiteral("visibility"), usedFallback.visibility, parent);
  result.opacity = value(style, QStringLiteral("opacity"), usedFallback.opacity, parent);
  result.fillOpacity = value(style, QStringLiteral("fill-opacity"),
                             usedFallback.fillOpacity, parent);
  result.strokeOpacity = value(style, QStringLiteral("stroke-opacity"),
                               usedFallback.strokeOpacity, parent);
  result.mixBlendMode = value(style, QStringLiteral("mix-blend-mode"),
                              fallback.mixBlendMode, parent);
  const QString usedColor =
      result.color.trimmed().compare(QLatin1String("currentColor"),
                                     Qt::CaseInsensitive) == 0
          ? QStringLiteral("black")
          : result.color;
  if (result.fill.trimmed().compare(QLatin1String("currentColor"),
                                    Qt::CaseInsensitive) == 0)
    result.fill = usedColor;
  if (result.stroke.trimmed().compare(QLatin1String("currentColor"),
                                      Qt::CaseInsensitive) == 0)
    result.stroke = usedColor;
  // Only `display:none` on an ancestor hard-suppresses the subtree. A parent's
  // `visibility:hidden` is NOT an ancestor suppression: visibility is an
  // inherited property, so it reaches children through normal inheritance
  // (see value()) and a child can override it back to `visible`.
  result.ancestorRenderable =
      !parent || (parent->ancestorRenderable &&
                  parent->display.compare(QStringLiteral("none"),
                                          Qt::CaseInsensitive) != 0);
  result.ancestorHasBox = !parent || parent->hasBox();
  result.effectiveOpacity =
      (parent ? parent->effectiveOpacity : 1.0) * editor::cssOpacity(result.opacity);
  result.effectiveFillOpacity =
      result.effectiveOpacity * editor::cssOpacity(result.fillOpacity);
  result.effectiveStrokeOpacity =
      result.effectiveOpacity * editor::cssOpacity(result.strokeOpacity);
  return result;
}

void linkSiblings(QVector<CssElement>& elements) {
  for (qsizetype index = 0; index < elements.size(); ++index) {
    elements[index].childIndex = static_cast<int>(index);
    elements[index].typeIndex = static_cast<int>(index);
    elements[index].previousSibling = index > 0 ? &elements[index - 1] : nullptr;
    elements[index].nextSibling = index + 1 < elements.size()
        ? &elements[index + 1] : nullptr;
  }
}

CssThemeSheet scopedSheet(const QString& css) {
  CssThemeSheet sheet;
  if (css.trimmed().isEmpty()) return sheet;
  const CssThemeSheet user = CssThemeParser::parse(css, {});
  for (const CssRule& input : user.rules()) {
    CssRule rule = input;
    rule.selectors.clear();
    for (const QString& selector : input.selectors)
      rule.selectors.append(QStringLiteral("#diagram-root ") + selector);
    rule.declarations = validDeclarations(input.declarations);
    if (!rule.selectors.isEmpty() && !rule.declarations.empty())
      sheet.addRule(std::move(rule));
  }
  return sheet;
}

}  // namespace

QHash<QString, ElementStyle> resolveElements(
    const QString& themeCss, const QVector<ElementInput>& inputs,
    const QString& builtInCss) {
  QHash<QString, ElementStyle> result;
  if (inputs.isEmpty()) return result;

  CssThemeSheet sheet = scopedSheet(builtInCss);
  const CssThemeSheet userSheet = scopedSheet(themeCss);
  for (const CssRule& rule : userSheet.rules()) sheet.addRule(rule);
  const CssComputedStyleEngine engine(sheet);
  QVector<CssElement> elements(inputs.size());
  QHash<QString, qsizetype> indexes;
  indexes.reserve(inputs.size());
  for (qsizetype index = 0; index < inputs.size(); ++index) {
    const ElementInput& input = inputs.at(index);
    CssElement& element = elements[index];
    element.tag = input.tag;
    element.id = input.id;
    element.classes = input.classes;
    element.attributes = input.attributes;
    indexes.insert(input.key, index);
  }
  for (qsizetype index = 0; index < inputs.size(); ++index) {
    const QString& parentKey = inputs.at(index).parentKey;
    const auto parent = indexes.constFind(parentKey);
    if (parent != indexes.cend()) elements[index].parent = &elements[parent.value()];
  }

  QHash<const CssElement*, QVector<qsizetype>> children;
  for (qsizetype index = 0; index < elements.size(); ++index)
    children[elements[index].parent].append(index);
  for (auto it = children.cbegin(); it != children.cend(); ++it) {
    const QVector<qsizetype>& siblings = it.value();
    QHash<QString, int> typeIndexes;
    for (qsizetype position = 0; position < siblings.size(); ++position) {
      CssElement& element = elements[siblings.at(position)];
      element.childIndex = static_cast<int>(position);
      element.typeIndex = typeIndexes.value(element.tag, 0);
      typeIndexes[element.tag] = element.typeIndex + 1;
      if (position > 0)
        element.previousSibling = &elements[siblings.at(position - 1)];
      if (position + 1 < siblings.size())
        element.nextSibling = &elements[siblings.at(position + 1)];
    }
  }

  for (qsizetype index = 0; index < inputs.size(); ++index) {
    const ElementInput& input = inputs.at(index);
    const CssComputedStyle computed = engine.styleFor(
        elements.at(index), {}, inlineStyle(input.inlineStyle),
        inlineStyle(input.presentationStyle));
    const auto parent = result.constFind(input.parentKey);
    const ElementStyle* parentStyle =
        parent == result.cend() ? nullptr : &parent.value();
    result.insert(input.key, project(computed, input.fallback, parentStyle));
  }
  return result;
}

FlowchartProjection resolveFlowchart(
    const flowchart::FlowchartData& data,
    const flowtheme::FlowThemeVariables& theme,
    const QString& themeCss,
    bool swimlane,
    const QString& look,
    bool htmlLabels) {
  const CssThemeSheet sheet = flowSheet(data, theme, themeCss, swimlane);
  const CssComputedStyleEngine engine(sheet);
  FlowchartProjection result;

  CssElement svg;
  svg.tag = QStringLiteral("svg");
  svg.id = QStringLiteral("diagram-root");
  svg.classes = {QStringLiteral("flowchart")};
  CssElement root;
  root.tag = QStringLiteral("g");
  root.classes = {QStringLiteral("root")};
  root.parent = &svg;
  CssElement nodes;
  nodes.tag = QStringLiteral("g");
  nodes.classes = {QStringLiteral("nodes")};
  nodes.parent = &root;

  QVector<CssElement> groups(data.vertices.size());
  for (qsizetype index = 0; index < data.vertices.size(); ++index) {
    const flowchart::FlowVertex& vertex = data.vertices.at(index);
    CssElement& group = groups[index];
    group.tag = QStringLiteral("g");
    group.id = QStringLiteral("diagram-root-") + vertex.domId;
    group.classes = {QStringLiteral("node"), QStringLiteral("default")};
    group.classes.append(vertex.classes);
    group.parent = &nodes;
  }
  linkSiblings(groups);

  ElementStyle nodeFallback;
  nodeFallback.fill = theme.mainBkg;
  nodeFallback.stroke = theme.nodeBorder;
  nodeFallback.strokeWidth =
      QString::number(theme.strokeWidth) + QStringLiteral("px");
  nodeFallback.color = theme.nodeTextColor.isEmpty()
                           ? theme.textColor : theme.nodeTextColor;
  nodeFallback.fontFamily = theme.fontFamily;
  nodeFallback.fontSize = theme.fontSize;

  for (qsizetype index = 0; index < data.vertices.size(); ++index) {
    const flowchart::FlowVertex& vertex = data.vertices.at(index);
    CssElement& group = groups[index];
    CssElement shape;
    shape.tag = QStringLiteral("rect");
    shape.classes = {QStringLiteral("basic"), QStringLiteral("label-container")};
    shape.parent = &group;
    shape.childIndex = 0;
    shape.typeIndex = 0;
    CssElement label;
    label.tag = QStringLiteral("g");
    label.classes = {QStringLiteral("label")};
    label.parent = &group;
    label.childIndex = 1;
    label.typeIndex = 0;
    shape.nextSibling = &label;
    label.previousSibling = &shape;

    const flowstyle::ResolvedNodeStyle resolved =
        flowstyle::resolveNodeStyle(vertex, data.classes, theme);
    ElementStyle inlineFallback = nodeFallback;
    inlineFallback.fill = resolved.fill;
    inlineFallback.stroke = resolved.stroke;
    inlineFallback.strokeWidth = resolved.strokeWidth;
    inlineFallback.color = resolved.color;
    inlineFallback.fontFamily = resolved.fontFamily;
    inlineFallback.fontSize = resolved.fontSize;
    inlineFallback.fontWeight = resolved.fontWeight;
    const CssComputedStyle groupStyle = engine.styleFor(group);
    ElementStyle groupFallback = nodeFallback;
    groupFallback.fill = theme.textColor;
    groupFallback.stroke = QStringLiteral("none");
    groupFallback.strokeWidth = QStringLiteral("1px");
    groupFallback.color = QStringLiteral("black");
    ElementStyle groupProjection = project(groupStyle, groupFallback);
    const CssComputedStyle shapeStyle = engine.styleFor(
        shape, {}, inlineStyle(resolved.nodeStyles));
    const CssComputedStyle labelStyle = engine.styleFor(
        label, {}, inlineStyle(resolved.labelStyles));
    ElementStyle shapeFallback = inlineFallback;
    if (shapeStyle.rawValue(QStringLiteral("fill")).contains(
            QLatin1String("var("), Qt::CaseInsensitive) &&
        shapeStyle.resolvedValue(QStringLiteral("fill")).isEmpty())
      shapeFallback.fill = groupProjection.fill;
    if (shapeStyle.rawValue(QStringLiteral("stroke")).contains(
            QLatin1String("var("), Qt::CaseInsensitive) &&
        shapeStyle.resolvedValue(QStringLiteral("stroke")).isEmpty())
      shapeFallback.stroke = QStringLiteral("none");
    // The shape and label are DOM children of the node group: pass the group's
    // projection so ancestor display:none, ancestor opacity and computed-value
    // inheritance flow through project() instead of ad-hoc field copies (a
    // child `visibility:visible` must recover from a hidden group).
    ElementStyle shapeProjection = project(shapeStyle, shapeFallback, &groupProjection);
    ElementStyle labelFallback = inlineFallback;
    const auto inheritedFromGroup = [&](const QString& property,
                                        QString& fallback) {
      const QString raw = labelStyle.rawValue(property).trimmed().toLower();
      if (raw == QLatin1String("inherit") || raw == QLatin1String("unset") ||
          raw == QLatin1String("revert") || raw == QLatin1String("revert-layer")) {
        if (property == QLatin1String("color")) fallback = groupProjection.color;
        else if (property == QLatin1String("font-family")) fallback = groupProjection.fontFamily;
        else if (property == QLatin1String("font-size")) fallback = groupProjection.fontSize;
        else if (property == QLatin1String("font-weight")) fallback = groupProjection.fontWeight;
      }
    };
    inheritedFromGroup(QStringLiteral("color"), labelFallback.color);
    inheritedFromGroup(QStringLiteral("font-family"), labelFallback.fontFamily);
    inheritedFromGroup(QStringLiteral("font-size"), labelFallback.fontSize);
    inheritedFromGroup(QStringLiteral("font-weight"), labelFallback.fontWeight);
    if (labelStyle.rawValue(QStringLiteral("color")).contains(
            QLatin1String("var("), Qt::CaseInsensitive) &&
        labelStyle.resolvedValue(QStringLiteral("color")).isEmpty())
      labelFallback.color = groupProjection.color;
    ElementStyle labelProjection = project(labelStyle, labelFallback, &groupProjection);
    result.nodes.insert(vertex.id, shapeProjection);
    result.nodeLabels.insert(vertex.id, labelProjection);
  }

  CssElement edgePaths;
  edgePaths.tag = QStringLiteral("g");
  edgePaths.classes = {QStringLiteral("edgePaths")};
  edgePaths.parent = &root;
  CssElement edgeLabels;
  edgeLabels.tag = QStringLiteral("g");
  edgeLabels.classes = {QStringLiteral("edgeLabels")};
  edgeLabels.parent = &root;
  // Wrapper projections for the edge-path groups and the edge-labels container:
  // themeCSS on `g.edgePath` / `g.edgeLabels` (opacity, display, visibility)
  // must compose onto the painted children like any other group ancestor.
  const ElementStyle edgePathsProjection = project(
      engine.styleFor(edgePaths), ElementStyle{});
  const ElementStyle edgeLabelsProjection = project(
      engine.styleFor(edgeLabels), ElementStyle{});
  for (qsizetype index = 0; index < data.edges.size(); ++index) {
    const flowchart::FlowEdge& edge = data.edges.at(index);
    CssElement group;
    group.tag = QStringLiteral("g");
    group.classes = {QStringLiteral("edgePath")};
    group.parent = &edgePaths;
    group.childIndex = static_cast<int>(index);
    CssElement path;
    path.tag = QStringLiteral("path");
    path.classes = {QStringLiteral("path"), QStringLiteral("flowchart-link")};
    path.parent = &group;
    const flowstyle::ResolvedEdgeStyle fallback =
        flowstyle::resolveEdgeStyle(edge, theme);
    ElementStyle edgeFallback;
    edgeFallback.stroke = fallback.stroke;
    edgeFallback.strokeWidth = fallback.strokeWidth;
    edgeFallback.fill = fallback.fill;
    edgeFallback.color = theme.textColor;
    edgeFallback.fontFamily = theme.fontFamily;
    edgeFallback.fontSize = theme.fontSize;
    const ElementStyle groupProjection = project(
        engine.styleFor(group), ElementStyle{}, &edgePathsProjection);
    result.edges.insert(edge.id, project(
        engine.styleFor(path), edgeFallback, &groupProjection));

    CssElement label;
    label.tag = QStringLiteral("g");
    label.classes = {QStringLiteral("edgeLabel")};
    label.parent = &edgeLabels;
    label.childIndex = static_cast<int>(index);
    ElementStyle labelFallback = edgeFallback;
    labelFallback.color = theme.textColor;
    result.edgeLabels.insert(edge.id, project(
        engine.styleFor(label), labelFallback, &edgeLabelsProjection));
  }

  CssElement clusters;
  clusters.tag = QStringLiteral("g");
  clusters.classes = {QStringLiteral("clusters")};
  clusters.parent = &root;
  QVector<CssElement> clusterGroups(data.subgraphs.size());
  for (qsizetype index = 0; index < data.subgraphs.size(); ++index) {
    const flowchart::FlowSubgraph& subgraph = data.subgraphs.at(index);
    CssElement& group = clusterGroups[index];
    group.tag = QStringLiteral("g");
    group.classes = {QStringLiteral("cluster")};
    if (swimlane) group.classes.append(QStringLiteral("swimlane"));
    group.classes.append(subgraph.classes);
    if (swimlane)
      group.attributes.insert(QStringLiteral("data-look"), look);
    group.parent = &clusters;

    // insertCluster(..., ":first-child") reverses Swimlane source order in
    // the SVG. Keep the complete sibling graph so structural selectors see
    // the same DOM, including :last-child and adjacent-sibling combinators.
    group.childIndex = static_cast<int>(swimlane
        ? data.subgraphs.size() - index - 1 : index);
    const qsizetype previous = swimlane ? index + 1 : index - 1;
    const qsizetype next = swimlane ? index - 1 : index + 1;
    if (previous >= 0 && previous < clusterGroups.size())
      group.previousSibling = &clusterGroups[previous];
    if (next >= 0 && next < clusterGroups.size())
      group.nextSibling = &clusterGroups[next];
  }
  for (qsizetype index = 0; index < data.subgraphs.size(); ++index) {
    const flowchart::FlowSubgraph& subgraph = data.subgraphs.at(index);
    CssElement& group = clusterGroups[index];
    ElementStyle groupFallback;
    groupFallback.fill = theme.textColor;
    groupFallback.stroke = QStringLiteral("none");
    groupFallback.strokeWidth = QStringLiteral("1px");
    groupFallback.color = QStringLiteral("black");
    groupFallback.fontFamily = theme.fontFamily;
    groupFallback.fontSize = theme.fontSize;
    const ElementStyle groupProjection =
        project(engine.styleFor(group), groupFallback);
    result.clusterGroups.insert(subgraph.id, groupProjection);
    CssElement body;
    body.tag = QStringLiteral("rect");
    body.classes = swimlane ? QStringList{QStringLiteral("swimlane-body")}
                            : QStringList{};
    body.parent = &group;
    body.childIndex = 0;
    body.typeIndex = 0;
    CssElement title;
    if (swimlane) {
      title.tag = QStringLiteral("rect");
      title.classes = {QStringLiteral("swimlane-title")};
      title.parent = &group;
      title.childIndex = 1;
      title.typeIndex = 1;
      body.nextSibling = &title;
      title.previousSibling = &body;
    }
    ElementStyle clusterFallback;
    clusterFallback.fill = theme.clusterBkg;
    clusterFallback.stroke = theme.clusterBorder;
    clusterFallback.strokeWidth = QStringLiteral("1px");
    clusterFallback.color = theme.titleColor;
    clusterFallback.fontFamily = theme.fontFamily;
    clusterFallback.fontSize = theme.fontSize;
    const QStringList classStyles =
        flowstyle::compiledClassStyles(subgraph.classes, data.classes);
    const QString nodeInline = classStyles.join(QLatin1Char(';'));
    ElementStyle bodyFallback = clusterFallback;
    if (swimlane) bodyFallback.fill = QStringLiteral("none");
    ElementStyle bodyProjection = project(
        engine.styleFor(body, {}, inlineStyle(nodeInline)), bodyFallback,
        &groupProjection);
    result.clusters.insert(subgraph.id, bodyProjection);
    if (swimlane) {
      result.swimlaneBodies.insert(subgraph.id, bodyProjection);
      ElementStyle titleProjection = project(
          engine.styleFor(title, {}, inlineStyle(nodeInline)),
          clusterFallback, &groupProjection);
      result.swimlaneTitles.insert(subgraph.id, titleProjection);
    }
    CssElement label;
    label.tag = QStringLiteral("g");
    label.classes = {QStringLiteral("cluster-label")};
    if (swimlane) label.classes.append(QStringLiteral("swimlane-label"));
    label.parent = &group;
    label.childIndex = swimlane ? 2 : 1;
    if (swimlane) {
      title.nextSibling = &label;
      label.previousSibling = &title;
    } else {
      body.nextSibling = &label;
      label.previousSibling = &body;
    }
    ElementStyle labelFallback = clusterFallback;
    labelFallback.color = QStringLiteral("black");
    const ElementStyle labelGroupProjection =
        project(engine.styleFor(label), labelFallback, &groupProjection);
    CssElement labelText;
    labelText.tag = htmlLabels ? QStringLiteral("span")
                               : QStringLiteral("text");
    labelText.parent = &label;
    labelText.childIndex = 0;
    ElementStyle textFallback = labelFallback;
    textFallback.color = labelGroupProjection.color;
    textFallback.fontFamily = labelGroupProjection.fontFamily;
    textFallback.fontSize = labelGroupProjection.fontSize;
    textFallback.fontWeight = labelGroupProjection.fontWeight;
    ElementStyle labelProjection =
        project(engine.styleFor(labelText), textFallback, &labelGroupProjection);
    result.clusterLabels.insert(subgraph.id, labelProjection);
  }
  return result;
}

}  // namespace muffin::mermaid::csscascade
