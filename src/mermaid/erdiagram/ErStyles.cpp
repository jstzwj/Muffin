#include "mermaid/erdiagram/ErStyles.h"

#include "mermaid/erdiagram/ErRough.h"

#include "mermaid/editor/MermaidRenderSupport.h"
#include "mermaid/scene/NeoShadow.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"
#include "mermaid/theme/MermaidCssCascade.h"

#include <QHash>
#include <QJsonValue>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStringList>

#include <cmath>

// erDiagram's style: the elements of its document through the stylesheet
// cascade. Per IMPLEMENTATION_SPEC.md this .cpp defines its free functions
// with fully-qualified names and keeps its helpers in an anonymous namespace.

namespace css = muffin::mermaid::csscascade;
namespace flowchart = muffin::mermaid::flowchart;
namespace flowtheme = muffin::mermaid::flowtheme;
namespace scene = muffin::mermaid::scene;
namespace style = muffin::mermaid::style;

namespace {

using muffin::mermaid::er::ErCardinality;
using muffin::mermaid::er::ErPaint;
using muffin::mermaid::er::ErStyleInput;
using muffin::mermaid::er::ErTextStyle;
using muffin::mermaid::er::ErLayoutEntityInput;
using muffin::mermaid::er::ErRoughEntity;
using muffin::mermaid::er::ErRoughOptions;

namespace er = muffin::mermaid::er;

constexpr qreal kTableStroke = 1.3;         // rough.js's default
constexpr qreal kRelationshipFontSize = 14; // `.edgeLabel .label`
// The gradient element the neo look's strokes name, in place of the one
// Mermaid defines under the document's own id.
constexpr char kGradientReference[] = "url(#er-gradient)";

bool isNone(const QString& value) {
  const QString trimmed = value.trimmed();
  return trimmed.isEmpty() || trimmed.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0;
}

// A stroke dash array as the port draws it: its lengths, "5 5", or empty
// where the stroke is solid or the value is not a plain list of lengths.
QString dashOf(const QString& value) {
  static const QRegularExpression separators(QStringLiteral("[\\s,]+"));
  QStringList lengths;
  bool solid = true;
  for (QString token : value.trimmed().split(separators, Qt::SkipEmptyParts)) {
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive))
      token.chop(2);
    bool ok = false;
    const qreal length = token.toDouble(&ok);
    if (!ok)
      return {};
    solid = solid && length == 0.0;
    lengths.append(token);
  }
  return solid ? QString() : lengths.join(QLatin1Char(' '));
}

qreal widthOf(const css::ElementStyle& element) {
  return muffin::mermaid::editor::cssStrokeWidthPx(element.strokeWidth, {}, 0.0);
}

// A color as the cascade resolves it, or invalid where nothing paints.
QColor paintOf(const QString& value) {
  if (isNone(value) || value.trimmed().compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0 ||
      !muffin::mermaid::color::isParsableColor(value))
    return {};
  return muffin::mermaid::color::toQColor(value);
}

// What Mermaid writes inline on an element: each declaration important, as
// erBox and drawRect write the classDefs and the inline styles.
QString important(const QStringList& declarations) {
  QStringList written;
  for (const QString& declaration : declarations)
    written.append(declaration + QStringLiteral(" !important"));
  return written.join(QLatin1Char(';'));
}

// The compiled declarations of an entity, as erBox sorts them: its label
// styles go to every label of the entity, and its node styles to its paths.
struct CompiledStyles {
  QHash<QString, QString> declared; // by property
  QString labelStyles;
  QString nodeStyles;
  QString strokeStyles; // the node styles that stroke
};

// What an entity declares for a property, as rough.js reads it for a shape's
// options; `fallback` where it says nothing.
QString declaredOr(const CompiledStyles& styles, const char* property, const QString& fallback) {
  return styles.declared.value(QLatin1String(property), fallback);
}

CompiledStyles compiledStyles(const QString& cssClasses, const QVector<style::ClassDef>& classDefs,
                              const QStringList& inlineStyles) {
  // erDB gives every entity the class "default", and adds the classes of the
  // `class` statements to it.
  QStringList classes{QStringLiteral("default")};
  for (const QString& name : cssClasses.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    classes.append(name);
  CompiledStyles result;
  QStringList labels, nodes, strokes;
  for (const QString& declaration : style::compiledStyles(classes, classDefs, inlineStyles)) {
    const int colon = declaration.indexOf(QLatin1Char(':'));
    const QString key = declaration.left(colon).trimmed();
    result.declared.insert(key, declaration.mid(colon + 1).trimmed());
    if (style::isLabelStyle(key))
      labels.append(declaration);
    else {
      nodes.append(declaration);
      if (key.contains(QLatin1String("stroke")))
        strokes.append(declaration);
    }
  }
  result.labelStyles = important(labels);
  result.nodeStyles = important(nodes);
  result.strokeStyles = important(strokes);
  return result;
}

// The paint of a rough.js group: a path that fills and a path that strokes,
// as Chrome paints them together. Where there is only one path it is both.
ErPaint groupPaint(const css::ElementStyle& fill, const css::ElementStyle& stroke) {
  ErPaint result;
  result.fill = !isNone(fill.fill) ? fill.fill : (!isNone(stroke.fill) ? stroke.fill : QString());
  const bool fillStrokes = !isNone(fill.stroke), strokeStrokes = !isNone(stroke.stroke);
  const css::ElementStyle* stroking = nullptr;
  if (fillStrokes && strokeStrokes)
    stroking = widthOf(fill) > widthOf(stroke) ? &fill : &stroke;
  else if (fillStrokes)
    stroking = &fill;
  else if (strokeStrokes)
    stroking = &stroke;
  if (stroking) {
    result.stroke = stroking->stroke;
    result.strokeWidth = widthOf(*stroking);
    result.strokeDasharray = dashOf(stroking->strokeDasharray);
  } else {
    result.strokeWidth = 0.0;
  }
  // The stroke of the filling path follows the outline, corners included.
  result.joined = fillStrokes && widthOf(fill) > 0.0;
  return result;
}

ErPaint singlePaint(const css::ElementStyle& element) {
  ErPaint result;
  result.fill = element.fill;
  result.stroke = element.stroke;
  // Nothing strokes: no width either.
  result.strokeWidth = isNone(element.stroke) ? 0.0 : widthOf(element);
  result.strokeDasharray = dashOf(element.strokeDasharray);
  return result;
}

// `line-height` as a factor of a font size, as erBox's `1.5` is: a number is
// one, a length or a percentage is divided by the font size, and `normal` is
// the font's own box.
qreal lineHeightFactor(const QString& value, const QString& fontFamily, qreal fontSize) {
  const QString token = value.trimmed();
  bool ok = false;
  const qreal number = token.toDouble(&ok);
  if (ok)
    return number > 0.0 ? number : 1.5;
  if (fontSize > 0.0) {
    const qreal length = muffin::mermaid::editor::pixelValue(token, -1.0);
    if (length >= 0.0)
      return length / fontSize;
    static const QRegularExpression percent(QStringLiteral("^([\\d.]+)\\s*%$"));
    const auto match = percent.match(token);
    if (match.hasMatch())
      return match.capturedView(1).toDouble() / 100.0;
  }
  return flowchart::flowLabelFontBoundingMetrics(fontFamily, fontSize).height() / fontSize;
}

// The text style a resolved text element declares, over the base. An HTML
// text takes its color from `color`, an SVG one from `fill`, which is what
// createText writes its label style's `color` as.
ErTextStyle textStyle(const css::ElementStyle& element, const ErTextStyle& base, qreal parentSize, bool htmlLabels) {
  ErTextStyle result = base;
  if (!element.fontFamily.isEmpty() && element.fontFamily.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0)
    result.fontFamily = muffin::mermaid::editor::firstFontFamily(element.fontFamily);
  const qreal size = muffin::mermaid::editor::cssFontSizePx(
      element.fontSize, muffin::mermaid::editor::pieCssLengthContext(element.fontFamily, parentSize));
  if (size > 0.0)
    result.fontSize = size;
  result.fontWeight = muffin::mermaid::editor::cssFontWeightToQt(QJsonValue(element.fontWeight), base.fontWeight);
  result.italic = element.fontStyle.contains(QLatin1String("italic"), Qt::CaseInsensitive) ||
                  element.fontStyle.contains(QLatin1String("oblique"), Qt::CaseInsensitive);
  if (!element.lineHeight.isEmpty() && element.lineHeight.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0)
    result.lineHeightFactor = lineHeightFactor(element.lineHeight, result.fontFamily, result.fontSize);
  const QString& color = htmlLabels ? element.color : element.fill;
  if (!isNone(color) && color.compare(QLatin1String("inherit"), Qt::CaseInsensitive) != 0)
    result.color = color;
  return result;
}

// A label group and, under it, the box createText makes for its text. An
// edge label's box is `div.labelBkg`, with a background of its own.
void addLabel(QVector<css::ElementInput>& elements, const QString& key, const QString& parent,
              const QStringList& classes, const css::ElementStyle& base, const QString& inlineStyle, bool htmlLabels,
              bool markdownNodeLabel, const QString& background, bool edgeLabel = false) {
  elements.append({key, parent, QStringLiteral("g"), {}, classes, {}, base, inlineStyle, {}});
  const QString text = key + (htmlLabels ? QStringLiteral("-p") : QStringLiteral("-text"));
  if (htmlLabels) {
    // Mermaid's own, unimportant, on the box: the line box is 1.5 lines high.
    css::ElementStyle box = base;
    box.lineHeight = QStringLiteral("1.5");
    elements.append({key + QStringLiteral("-fo"), key, QStringLiteral("foreignObject"), {}, {}, {}, base, {}, {}});
    elements.append({key + QStringLiteral("-div"),
                     key + QStringLiteral("-fo"),
                     QStringLiteral("div"),
                     {},
                     edgeLabel ? QStringList{QStringLiteral("labelBkg")} : QStringList{},
                     {},
                     box,
                     inlineStyle,
                     QStringLiteral("display:table-cell;white-space:nowrap;line-height:1.5")});
    elements.append({key + QStringLiteral("-span"),
                     key + QStringLiteral("-div"),
                     QStringLiteral("span"),
                     {},
                     edgeLabel ? QStringList{QStringLiteral("edgeLabel"), QStringLiteral("markdown-node-label")}
                     : markdownNodeLabel
                         ? QStringList{QStringLiteral("nodeLabel"), QStringLiteral("markdown-node-label")}
                         : QStringList{QStringLiteral("nodeLabel")},
                     {},
                     box,
                     inlineStyle,
                     {}});
    // Mermaid writes the label style on the group, the box and the span, and
    // the text inherits it. The cascade inherits what the stylesheet's rules
    // give an ancestor, not its inline style, so the text takes it too.
    elements.append({text, key + QStringLiteral("-span"), QStringLiteral("p"), {}, {}, {}, box, inlineStyle, {}});
    return;
  }
  elements.append({key + QStringLiteral("-rect"),
                   key,
                   QStringLiteral("rect"),
                   {},
                   {QStringLiteral("background")},
                   {},
                   base,
                   {},
                   QStringLiteral("fill:%1;stroke:none").arg(background)});
  elements.append({text,
                   key,
                   QStringLiteral("text"),
                   {},
                   {},
                   {},
                   base,
                   QString(inlineStyle).replace(QStringLiteral("color:"), QStringLiteral("fill:")),
                   {}});
}

// The pair of paths rough.js draws a shape by: one that fills and one that
// strokes, each with the attributes rough.js gives it: the shape's options.
void addPaths(QVector<css::ElementInput>& elements, const QString& group, const QString& fill, const QString& stroke,
              const QString& strokeWidth, const QString& inlineStyle) {
  if (!isNone(fill))
    elements.append({group + QStringLiteral("-fill"),
                     group,
                     QStringLiteral("path"),
                     {},
                     {},
                     {},
                     {},
                     inlineStyle,
                     QStringLiteral("fill:%1;stroke:none;stroke-width:0").arg(fill)});
  elements.append(
      {group + QStringLiteral("-stroke"),
       group,
       QStringLiteral("path"),
       {},
       {},
       {},
       {},
       inlineStyle,
       QStringLiteral("fill:none;stroke:%1;stroke-width:%2;stroke-dasharray:0 0").arg(stroke, strokeWidth)});
}

// A rough.js group's paint; the same path fills and strokes where there is
// no fill path.
ErPaint groupPaintOf(const QHash<QString, css::ElementStyle>& styles, const QString& group, bool filled) {
  const css::ElementStyle& stroke = styles.value(group + QStringLiteral("-stroke"));
  return filled ? groupPaint(styles.value(group + QStringLiteral("-fill")), stroke) : groupPaint(stroke, stroke);
}

// What an element takes from its parent where nothing declares it on it:
// the properties SVG inherits.
void inherit(css::ElementStyle& fallback, const css::ElementStyle& parent) {
  fallback.fontFamily = parent.fontFamily;
  fallback.fontSize = parent.fontSize;
  fallback.fontWeight = parent.fontWeight;
  fallback.fontStyle = parent.fontStyle;
  fallback.lineHeight = parent.lineHeight;
  fallback.color = parent.color;
  fallback.fill = parent.fill;
  fallback.stroke = parent.stroke;
  fallback.strokeWidth = parent.strokeWidth;
  fallback.strokeDasharray = parent.strokeDasharray;
}

// Resolves the document twice: what a rule or an inline style gives an
// element is inherited by what is under it, as a browser does, while the
// cascade alone inherits only what the stylesheet's rules say.
QHash<QString, css::ElementStyle> resolve(const QVector<css::ElementInput>& elements, const QString& baseCss,
                                          const QString& themeCss) {
  const QHash<QString, css::ElementStyle> styles = css::resolveElements(themeCss, elements, baseCss);
  QVector<css::ElementInput> inherited = elements;
  for (css::ElementInput& element : inherited)
    if (!element.parentKey.isEmpty() && styles.contains(element.parentKey))
      inherit(element.fallback, styles.value(element.parentKey));
  return css::resolveElements(themeCss, inherited, baseCss);
}

// The font size each element's text is at, in px, for the lengths relative
// to it.
QHash<QString, qreal> fontSizes(const QVector<css::ElementInput>& elements,
                                const QHash<QString, css::ElementStyle>& styles, qreal rootSize) {
  QHash<QString, qreal> sizes;
  for (const css::ElementInput& element : elements) {
    const css::ElementStyle& style = styles.value(element.key);
    sizes.insert(element.key, muffin::mermaid::editor::cssFontSizePx(
                                  style.fontSize, muffin::mermaid::editor::pieCssLengthContext(
                                                      style.fontFamily, sizes.value(element.parentKey, rootSize))));
  }
  return sizes;
}

// The classes of an entity's group: rough.js draws it under the handDrawn
// look, and its own classes follow either way.
QStringList entityClasses(const ErLayoutEntityInput& entity, bool handDrawn) {
  QStringList classes{handDrawn ? QStringLiteral("rough-node") : QStringLiteral("node"), QStringLiteral("default")};
  for (const QString& name : entity.cssClasses.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    classes.append(name);
  return classes;
}

// The options of an entity's shapes under the handDrawn look, as
// userNodeOverrides makes them of the theme and of what the entity declares.
ErRoughEntity roughEntity(const CompiledStyles& styles, const flowtheme::FlowThemeVariables& theme, quint32 seed) {
  ErRoughEntity rough;
  rough.table.fill = declaredOr(styles, "fill", theme.mainBkg);
  rough.table.stroke = declaredOr(styles, "stroke", theme.nodeBorder);
  rough.table.strokeWidth = er::roughStrokeWidth(declaredOr(styles, "stroke-width", QString()));
  rough.table.strokeDasharray = er::roughStrokeDasharray(styles.declared.value(QLatin1String("stroke-dasharray")));
  rough.table.seed = seed;
  // A row takes the theme's colors for its fill and the border for its stroke,
  // whatever a style says of the entity; the stroke's width is the entity's.
  rough.oddRows = rough.evenRows = rough.table;
  rough.oddRows.fill = theme.rowOdd;
  rough.evenRows.fill = theme.rowEven;
  rough.oddRows.stroke = rough.evenRows.stroke = theme.nodeBorder;
  return rough;
}

// The paths rough.js draws a shape by, each with the attributes rough.js gives
// it: the sketch of the fill, then the outline.
void addRoughPaths(QVector<css::ElementInput>& elements, const QString& group, const ErRoughOptions& options) {
  if (!options.fill.isEmpty())
    elements.append({group + QStringLiteral("-sketch"),
                     group,
                     QStringLiteral("path"),
                     {},
                     {},
                     {},
                     {},
                     {},
                     QStringLiteral("fill:none;stroke:%1;stroke-width:4;stroke-dasharray:0 0").arg(options.fill)});
  if (!options.stroke.isEmpty())
    elements.append({group + QStringLiteral("-outline"),
                     group,
                     QStringLiteral("path"),
                     {},
                     {},
                     {},
                     {},
                     {},
                     QStringLiteral("fill:none;stroke:%1;stroke-width:%2;stroke-dasharray:%3")
                         .arg(options.stroke, QString::number(options.strokeWidth), options.strokeDasharray)});
}

// The paint of a shape rough.js draws: its fill is the color of the sketch's
// lines, and its stroke the outline's.
ErPaint roughPaintOf(const QHash<QString, css::ElementStyle>& styles, const QString& group) {
  ErPaint result;
  if (styles.contains(group + QStringLiteral("-sketch"))) {
    const css::ElementStyle& sketch = styles.value(group + QStringLiteral("-sketch"));
    result.fill = sketch.stroke;
    result.fillWidth = widthOf(sketch);
  }
  if (styles.contains(group + QStringLiteral("-outline"))) {
    const css::ElementStyle& outline = styles.value(group + QStringLiteral("-outline"));
    result.stroke = outline.stroke;
    result.strokeWidth = widthOf(outline);
    result.strokeDasharray = dashOf(outline.strokeDasharray);
  } else {
    result.strokeWidth = 0.0;
  }
  return result;
}

// The copy erBox draws behind an entity's shapes under the handDrawn look:
// the entity as the classic look draws it, from its classes and two styles of
// its own, which stroke nothing and fill with the background.
void addBackground(QVector<css::ElementInput>& elements, const QString& key, const ErLayoutEntityInput& entity,
                   const ErStyleInput& input, const css::ElementStyle& root) {
  const flowtheme::FlowThemeVariables& theme = input.theme;
  const CompiledStyles copy =
      compiledStyles(entity.cssClasses, input.classDefs,
                     {QStringLiteral("stroke: none"), QStringLiteral("fill: ") + input.theme.background});
  const auto shapePath = [&](const QString& group, const QString& fill, const QString& inlineStyle) {
    // A row the theme has no fill for is drawn by its outline alone.
    const bool filled = !isNone(fill);
    elements.append({group + (filled ? QStringLiteral("-fill") : QStringLiteral("-stroke")),
                     group,
                     QStringLiteral("path"),
                     {},
                     {},
                     {},
                     {},
                     inlineStyle,
                     filled ? QStringLiteral("fill:%1;stroke:none;stroke-width:0").arg(fill)
                            : QStringLiteral("fill:none;stroke:%1;stroke-width:%2")
                                  .arg(declaredOr(copy, "stroke", theme.nodeBorder), QString::number(kTableStroke))});
  };
  elements.append(
      {key, QStringLiteral("nodes"), QStringLiteral("g"), {}, entityClasses(entity, false), {}, root, {}, {}});
  if (entity.attributes.isEmpty()) {
    elements.append({key + QStringLiteral("-rect"),
                     key,
                     QStringLiteral("rect"),
                     {},
                     {QStringLiteral("basic"), QStringLiteral("label-container")},
                     {},
                     {},
                     copy.nodeStyles,
                     QStringLiteral("fill:%1;stroke:%2;stroke-width:%3")
                         .arg(declaredOr(copy, "fill", theme.mainBkg), declaredOr(copy, "stroke", theme.nodeBorder),
                              QString::number(kTableStroke))});
    addLabel(elements, key + QStringLiteral("-label"), key, {QStringLiteral("label")}, root, copy.labelStyles,
             input.htmlLabels, true, theme.mainBkg);
    return;
  }
  elements.append(
      {key + QStringLiteral("-outer"), key, QStringLiteral("g"), {}, {QStringLiteral("outer-path")}, {}, root, {}, {}});
  shapePath(key + QStringLiteral("-outer"), declaredOr(copy, "fill", theme.mainBkg), copy.strokeStyles);
  for (qsizetype row = 0; row < entity.attributes.size(); ++row) {
    const bool odd = row % 2 == 0;
    const QString group = key + QStringLiteral("-row") + QString::number(row);
    elements.append({group,
                     key,
                     QStringLiteral("g"),
                     {},
                     {QStringLiteral("row-rect-") + (odd ? QStringLiteral("odd") : QStringLiteral("even"))},
                     {},
                     root,
                     {},
                     {}});
    // The even rows carry the whole node style, whose last fill is the
    // background; the odd rows the stroke styles alone, and keep their color.
    // Where the theme has no row color, an even row's outline is what takes
    // the background.
    shapePath(group, odd ? theme.rowOdd : theme.rowEven, odd ? copy.strokeStyles : copy.nodeStyles);
  }
  elements.append(
      {key + QStringLiteral("-divider"), key, QStringLiteral("g"), {}, {QStringLiteral("divider")}, {}, root, {}, {}});
  shapePath(key + QStringLiteral("-divider"), declaredOr(copy, "fill", theme.mainBkg), copy.strokeStyles);
  static const char* const kinds[] = {"name", "attribute-type", "attribute-name", "attribute-keys",
                                      "attribute-comment"};
  for (qsizetype kind = 0; kind < 5; ++kind)
    addLabel(elements, key + QStringLiteral("-l") + QString::number(kind), key,
             {QStringLiteral("label"), QString::fromLatin1(kinds[kind])}, root, copy.labelStyles, input.htmlLabels,
             false, theme.mainBkg);
}

// The root of the document and, under it, the groups Mermaid keeps its
// nodes, its lines, their labels and its definitions in.
void addDocument(QVector<css::ElementInput>& elements, const css::ElementStyle& root) {
  elements.append({QStringLiteral("svg"),
                   {},
                   QStringLiteral("svg"),
                   QStringLiteral("diagram-root"),
                   {QStringLiteral("erDiagram")},
                   {},
                   root,
                   {},
                   {}});
  for (const char* group : {"root", "nodes", "edge-paths", "edge-labels", "defs"}) {
    const QString name = QString::fromLatin1(group);
    elements.append({name,
                     name == QLatin1String("root") ? QStringLiteral("svg") : QStringLiteral("root"),
                     QStringLiteral("g"),
                     {},
                     {name},
                     {},
                     root,
                     {},
                     {}});
  }
}

// `filter:` as the neo look's rules write it from the theme's dropShadow
// variable: the variable itself, with the filter element the redux themes
// name under the document's own id; `none` where the theme has none.
QString dropShadowFilter(const QString& dropShadow) {
  QString value = dropShadow.trimmed();
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return QStringLiteral("none");
  value.replace(QStringLiteral("url(#drop-shadow)"), QStringLiteral("url(#er-drop-shadow)"));
  return value;
}

// The theme's dropShadow as a `filter:` the cascade reads: a `drop-shadow()`
// as the theme writes it, and the filter element the redux themes name as the
// feDropShadow the renderer defines for it; `none` where the theme has none.
QString dropShadowFilter(const QString& dropShadow, bool darkTheme) {
  QString value = dropShadow.trimmed();
  while (value.endsWith(QLatin1Char(';')))
    value.chop(1);
  if (value.isEmpty() || value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
    return QStringLiteral("none");
  return scene::dropShadowText(value, darkTheme);
}

// A color as it comes out of an SVG `<filter>`: Chrome runs the primitives of
// a filter element in linear RGB at eight bits to a channel, and the colors
// of the filtered element, passed through and back, change a little
// (#111113 comes back #0d0d16, #28253d #26263d).
QString throughFilterElement(const QString& paint) {
  if (isNone(paint) || !muffin::mermaid::color::isParsableColor(paint))
    return paint;
  const QColor color = muffin::mermaid::color::toQColor(paint);
  const auto channel = [](int value) {
    const qreal s = value / 255.0;
    const qreal linear = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    const qreal quantized = std::round(linear * 255.0) / 255.0;
    const qreal back = quantized <= 0.0031308 ? 12.92 * quantized : 1.055 * std::pow(quantized, 1.0 / 2.4) - 0.055;
    return qBound(0, qRound(255.0 * back), 255);
  };
  const QColor shifted(channel(color.red()), channel(color.green()), channel(color.blue()), color.alpha());
  return shifted.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
}

// The table of an entity whose filter is a filter element's, as the filter
// passes it: only what the filter takes in changes, the table's own two paths.
void throughFilterElement(ErPaint& table) {
  table.fill = throughFilterElement(table.fill);
  table.stroke = throughFilterElement(table.stroke);
}

// Whether two `filter:` values are the same, whitespace and case aside.
bool sameFilter(const QString& a, const QString& b) {
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  const auto squeeze = [](QString value) {
    value.remove(whitespace);
    return value.toLower();
  };
  return squeeze(a) == squeeze(b);
}

} // namespace

QString muffin::mermaid::er::erBaseCss(const ErStyleInput& input) {
  const flowtheme::FlowThemeVariables& theme = input.theme;
  const bool neo = input.look == QLatin1String("neo");
  const QString textColor = theme.nodeTextColor.isEmpty() ? theme.textColor : theme.nodeTextColor;
  // A color as Chrome computes it from what the stylesheet writes: a theme's
  // tertiary color is an `hsl()` with a negative hue, which not every parser
  // of a stylesheet reads.
  const auto chrome = [](const QString& css) {
    const QColor color = muffin::mermaid::color::toQColor(css);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF());
  };
  // `.labelBkg`: the color themes name a background, the others fade their
  // tertiary color to half.
  const QString labelBackgroundCss = chrome(input.colorTheme && !theme.erEdgeLabelBackground.isEmpty()
                                                ? theme.erEdgeLabelBackground
                                                : muffin::mermaid::color::rgba(theme.tertiaryColor, 0.5));
  const QString strokeWidth = QString::number(theme.strokeWidth) + QStringLiteral("px");
  QStringList rules{
      // The common sheet, ahead of the diagram's own rules.
      QStringLiteral(".edge-thickness-normal{stroke-width:%1}").arg(strokeWidth),
      QStringLiteral(".edge-pattern-solid{stroke-dasharray:0}"),
      QStringLiteral(".edge-pattern-dashed{stroke-dasharray:3}"),
      QStringLiteral(".edge-pattern-dotted{stroke-dasharray:2}"),
      QStringLiteral(".marker{fill:%1;stroke:%1}").arg(theme.lineColor),
      QStringLiteral("p{margin:0}"),
      // erDiagram's own.
      QStringLiteral(".label{font-family:%1;color:%2}").arg(input.fontFamily, textColor),
      QStringLiteral(".edgeLabel .label{fill:%1;font-size:14px}").arg(theme.nodeBorder),
      QStringLiteral(".labelBkg{background-color:%1}").arg(labelBackgroundCss),
      QStringLiteral(".edgeLabel{background-color:%1}").arg(theme.edgeLabelBackground),
      QStringLiteral(".node rect,.node circle,.node ellipse,.node polygon{fill:%1;stroke:%2;stroke-width:%3}")
          .arg(theme.mainBkg, theme.nodeBorder, neo ? strokeWidth : QStringLiteral("1px")),
      QStringLiteral(".relationshipLine{stroke:%1;stroke-width:%2;fill:none}")
          .arg(theme.lineColor, neo ? strokeWidth : QStringLiteral("1px")),
      QStringLiteral(".marker{fill:none !important;stroke:%1 !important;stroke-width:1}").arg(theme.lineColor),
      QStringLiteral(".edge-pattern-solid{stroke-dasharray:0}"),
      QStringLiteral(".edge-pattern-dashed{stroke-dasharray:8,8}"),
      // The label of an SVG edge: its text, and the rectangle behind it.
      QStringLiteral(".edgeLabel .label rect{fill:%1}").arg(theme.edgeLabelBackground),
      QStringLiteral(".edgeLabel .label text{fill:%1}").arg(textColor),
      // The neo look's label background is the tertiary color for every theme.
      QStringLiteral("[data-look=neo].labelBkg{background-color:%1}")
          .arg(chrome(muffin::mermaid::color::rgba(theme.tertiaryColor, 0.5))),
  };
  // The color themes paint each entity from their palettes, by its order.
  const int limit = qMin(input.themeColorLimit, int(theme.borderColorArray.size()));
  for (int index = 0; index < limit; ++index) {
    const QString fill =
        index < theme.bkgColorArray.size() ? QStringLiteral(";fill:%1").arg(theme.bkgColorArray.at(index)) : QString();
    const QString entity = QStringLiteral("[data-look=%1][data-color-id=color-%2].node").arg(input.look).arg(index);
    rules.append(QStringLiteral("%1 path{stroke:%2%3}").arg(entity, theme.borderColorArray.at(index), fill));
    rules.append(QStringLiteral("%1 rect{stroke:%2%3}").arg(entity, theme.borderColorArray.at(index), fill));
  }
  // The common sheet, behind the diagram's own rules: under the neo look
  // every path of a node is stroked, the filling ones too, and the table
  // casts the theme's shadow.
  if (neo) {
    const QString stroke = input.useGradient ? QString::fromLatin1(kGradientReference) : theme.nodeBorder;
    const QString shadow = dropShadowFilter(theme.dropShadow, input.darkTheme);
    rules.append(
        QStringLiteral(
            "[data-look=neo].node rect,[data-look=neo].cluster rect,[data-look=neo].node polygon{stroke:%1;filter:%2}")
            .arg(stroke, shadow));
    rules.append(QStringLiteral("[data-look=neo].node path{stroke:%1;stroke-width:%2px}")
                     .arg(stroke, QString::number(theme.strokeWidth)));
    rules.append(QStringLiteral("[data-look=neo].node .outer-path{filter:%1}").arg(shadow));
  }
  return rules.join(QLatin1Char('\n'));
}

muffin::mermaid::er::ErDocumentStyle muffin::mermaid::er::resolveErDocumentStyle(const ErLayoutInput& layout,
                                                                                 const ErStyleInput& input) {
  const flowtheme::FlowThemeVariables& theme = input.theme;
  const bool neo = input.look == QLatin1String("neo");
  const QString textColor = theme.nodeTextColor.isEmpty() ? theme.textColor : theme.nodeTextColor;
  const qreal rootSize = muffin::mermaid::editor::pixelValue(theme.fontSize, 16.0);

  // The stylesheet's fonts: the root's, and `.edgeLabel .label`'s.
  ErTextStyle base;
  base.fontFamily = muffin::mermaid::editor::firstFontFamily(input.fontFamily);
  base.fontSize = rootSize;
  base.color = textColor;
  ErTextStyle baseRelationship = base;
  baseRelationship.fontSize = kRelationshipFontSize;

  css::ElementStyle root;
  root.fontFamily = base.fontFamily;
  root.fontSize = QString::number(rootSize) + QStringLiteral("px");
  root.color = textColor;
  root.fill = textColor;
  root.strokeWidth = QStringLiteral("1px");

  QVector<css::ElementInput> elements;
  addDocument(elements, root);

  // A marker and what is in it: the stylesheet leaves the marker unfilled.
  // The neo look fills the ring with the background and gives the children
  // the theme's stroke width of their own.
  css::ElementStyle markerBase = root;
  markerBase.fill = QStringLiteral("none");
  markerBase.stroke = theme.lineColor;
  css::ElementStyle ringBase = markerBase;
  ringBase.fill = QStringLiteral("white");
  const QString markerStrokeWidth = QString::number(theme.strokeWidth);
  const QString ringFill = neo && !theme.mainBkg.isEmpty() ? theme.mainBkg : QStringLiteral("white");
  for (const ErCardinality card :
       {ErCardinality::ExactlyOne, ErCardinality::ZeroOrOne, ErCardinality::OneOrMore, ErCardinality::ZeroOrMore}) {
    for (bool start : {true, false}) {
      const QString key = QStringLiteral("m-") + muffin::mermaid::er::erMarkerKey(card, start);
      QHash<QString, QString> attributes;
      if (neo)
        attributes.insert(QStringLiteral("markerUnits"), QStringLiteral("userSpaceOnUse"));
      elements.append({key,
                       QStringLiteral("defs"),
                       QStringLiteral("marker"),
                       {},
                       {QStringLiteral("marker"), muffin::mermaid::er::erMarkerName(card), QStringLiteral("er")},
                       attributes,
                       markerBase,
                       {},
                       {}});
      elements.append({key + QStringLiteral("-path"),
                       key,
                       QStringLiteral("path"),
                       {},
                       {},
                       {},
                       markerBase,
                       {},
                       neo ? QStringLiteral("stroke-width:") + markerStrokeWidth : QString()});
      if (muffin::mermaid::er::erMarker(card, start)->hasRing)
        elements.append({key + QStringLiteral("-circle"),
                         key,
                         QStringLiteral("circle"),
                         {},
                         {},
                         {},
                         ringBase,
                         {},
                         neo ? QStringLiteral("fill:%1;stroke-width:%2").arg(ringFill, markerStrokeWidth)
                             : QStringLiteral("fill:white")});
    }
  }

  const auto& entities = layout.entities;
  const bool handDrawn = input.look == QLatin1String("handDrawn");
  // The rough.js options are settled as the elements are listed, and read
  // back with the paints once the cascade has run.
  muffin::mermaid::er::ErDocumentStyle document;
  for (qsizetype index = 0; index < entities.size(); ++index) {
    const auto& entity = entities.at(index);
    const QString key = QStringLiteral("n") + QString::number(index);
    const CompiledStyles styles = compiledStyles(entity.cssClasses, input.classDefs, entity.styles);
    QHash<QString, QString> attributes{{QStringLiteral("data-look"), input.look}};
    if (input.colorTheme && index < input.themeColorLimit)
      attributes.insert(QStringLiteral("data-color-id"), QStringLiteral("color-%1").arg(index));
    elements.append({key,
                     QStringLiteral("nodes"),
                     QStringLiteral("g"),
                     {},
                     entityClasses(entity, handDrawn),
                     attributes,
                     root,
                     {},
                     {}});
    if (handDrawn) {
      // rough.js draws the shapes from options the entity's classDef and
      // inline style reach. No style is written on them, so a classDef acts
      // through the options alone.
      const ErRoughEntity rough = roughEntity(styles, theme, input.handDrawnSeed);
      document.rough.insert(entity.id, rough);
      const QString shape =
          entity.attributes.isEmpty() ? key + QStringLiteral("-shape") : key + QStringLiteral("-outer");
      elements.append({shape,
                       key,
                       QStringLiteral("g"),
                       {},
                       entity.attributes.isEmpty()
                           ? QStringList{QStringLiteral("basic"), QStringLiteral("label-container")}
                           : QStringList{QStringLiteral("outer-path")},
                       {},
                       root,
                       {},
                       {}});
      addRoughPaths(elements, shape, rough.table);
      if (entity.attributes.isEmpty()) {
        // drawRect: the rough group stands in for the rectangle, and its
        // label is under the entity.
        addLabel(elements, key + QStringLiteral("-label"), key, {QStringLiteral("label")}, root, styles.labelStyles,
                 input.htmlLabels, true, theme.mainBkg);
      } else {
        for (qsizetype row = 0; row < entity.attributes.size(); ++row) {
          const bool odd = row % 2 == 0;
          const QString group = key + QStringLiteral("-row") + QString::number(row);
          elements.append({group,
                           key,
                           QStringLiteral("g"),
                           {},
                           {QStringLiteral("row-rect-") + (odd ? QStringLiteral("odd") : QStringLiteral("even"))},
                           {},
                           root,
                           {},
                           {}});
          addRoughPaths(elements, group, odd ? rough.oddRows : rough.evenRows);
        }
        elements.append({key + QStringLiteral("-divider"),
                         key,
                         QStringLiteral("g"),
                         {},
                         {QStringLiteral("divider")},
                         {},
                         root,
                         {},
                         {}});
        addRoughPaths(elements, key + QStringLiteral("-divider"), rough.table);
        static const char* const kinds[] = {"name", "attribute-type", "attribute-name", "attribute-keys",
                                            "attribute-comment"};
        for (qsizetype kind = 0; kind < 5; ++kind)
          addLabel(elements, key + QStringLiteral("-l") + QString::number(kind), key,
                   {QStringLiteral("label"), QString::fromLatin1(kinds[kind])}, root, styles.labelStyles,
                   input.htmlLabels, false, theme.mainBkg);
      }
      addBackground(elements, key + QStringLiteral("-bg"), entity, input, root);
      continue;
    }
    // What a path takes inline: the stroke styles, and the whole node style
    // where the theme writes it so.
    const QString pathStyle = input.nodeStylesReachEveryPath ? styles.nodeStyles : styles.strokeStyles;
    if (entity.attributes.isEmpty()) {
      // drawRect: a rectangle with the whole node style, and its label.
      elements.append(
          {key + QStringLiteral("-rect"),
           key,
           QStringLiteral("rect"),
           {},
           {QStringLiteral("basic"), QStringLiteral("label-container")},
           {},
           {},
           styles.nodeStyles,
           QStringLiteral("fill:%1;stroke:%2;stroke-width:%3")
               .arg(declaredOr(styles, "fill", theme.mainBkg), declaredOr(styles, "stroke", theme.nodeBorder),
                    declaredOr(styles, "stroke-width", QString::number(kTableStroke)))});
      addLabel(elements, key + QStringLiteral("-label"), key, {QStringLiteral("label")}, root, styles.labelStyles,
               input.htmlLabels, true, theme.mainBkg);
      continue;
    }
    elements.append({key + QStringLiteral("-outer"),
                     key,
                     QStringLiteral("g"),
                     {},
                     {QStringLiteral("outer-path")},
                     {},
                     root,
                     {},
                     {}});
    const QString tableStroke = declaredOr(styles, "stroke", theme.nodeBorder);
    addPaths(elements, key + QStringLiteral("-outer"), declaredOr(styles, "fill", theme.mainBkg), tableStroke,
             declaredOr(styles, "stroke-width", QString::number(kTableStroke)), pathStyle);
    for (qsizetype row = 0; row < entity.attributes.size(); ++row) {
      const bool odd = row % 2 == 0;
      const QString group = key + QStringLiteral("-row") + QString::number(row);
      elements.append({group,
                       key,
                       QStringLiteral("g"),
                       {},
                       {QStringLiteral("row-rect-") + (odd ? QStringLiteral("odd") : QStringLiteral("even"))},
                       {},
                       root,
                       {},
                       {}});
      // The even rows carry the whole node style too, where the theme writes
      // the stroke styles elsewhere.
      // A row's fill and stroke are the theme's; the stroke's width is the entity's.
      addPaths(elements, group, odd ? theme.rowOdd : theme.rowEven, theme.nodeBorder,
               declaredOr(styles, "stroke-width", QString::number(kTableStroke)),
               !odd && !input.nodeStylesReachEveryPath ? styles.nodeStyles : pathStyle);
    }
    elements.append({key + QStringLiteral("-divider"),
                     key,
                     QStringLiteral("g"),
                     {},
                     {QStringLiteral("divider")},
                     {},
                     root,
                     {},
                     {}});
    addPaths(elements, key + QStringLiteral("-divider"), declaredOr(styles, "fill", theme.mainBkg), tableStroke,
             declaredOr(styles, "stroke-width", QString::number(kTableStroke)), pathStyle);
    // The entity's five kinds of text, by the class of their label.
    static const char* const kinds[] = {"name", "attribute-type", "attribute-name", "attribute-keys",
                                        "attribute-comment"};
    for (qsizetype kind = 0; kind < 5; ++kind)
      addLabel(elements, key + QStringLiteral("-l") + QString::number(kind), key,
               {QStringLiteral("label"), QString::fromLatin1(kinds[kind])}, root, styles.labelStyles, input.htmlLabels,
               false, theme.mainBkg);
  }

  css::ElementStyle line = root;
  line.fill = QStringLiteral("none");
  line.stroke = theme.lineColor;
  css::ElementStyle labelBox = root;
  labelBox.fill = theme.nodeBorder;
  labelBox.fontSize = QString::number(kRelationshipFontSize) + QStringLiteral("px");
  const auto& relationships = layout.relationships;
  for (qsizetype index = 0; index <= relationships.size(); ++index) {
    // One label more than there are relationships: the last stands for the
    // texts of them all, in a diagram that has none.
    const bool stands = index == relationships.size();
    const QString key = QStringLiteral("e") + QString::number(index);
    if (!stands) {
      const auto& relationship = relationships.at(index);
      elements.append(
          {key,
           QStringLiteral("edge-paths"),
           QStringLiteral("path"),
           {},
           {QStringLiteral("edge-thickness-normal"),
            relationship.identifying ? QStringLiteral("edge-pattern-solid") : QStringLiteral("edge-pattern-dashed"),
            QStringLiteral("relationshipLine")},
           {},
           line,
           {},
           {}});
    }
    elements.append({key + QStringLiteral("-group"),
                     QStringLiteral("edge-labels"),
                     QStringLiteral("g"),
                     {},
                     {QStringLiteral("edgeLabel")},
                     {},
                     root,
                     {},
                     {}});
    addLabel(elements, key + QStringLiteral("-label"), key + QStringLiteral("-group"), {QStringLiteral("label")},
             labelBox, {}, input.relationshipHtmlLabels, true, theme.edgeLabelBackground, true);
  }

  const QHash<QString, css::ElementStyle> styles =
      resolve(elements, muffin::mermaid::er::erBaseCss(input), input.themeCss);
  const QHash<QString, qreal> sizes = fontSizes(elements, styles, rootSize);
  const auto textOf = [&](const QString& label, const ErTextStyle& over, bool htmlLabels) {
    const QString text = label + (htmlLabels ? QStringLiteral("-p") : QStringLiteral("-text"));
    return textStyle(styles.value(text), over,
                     sizes.value(htmlLabels ? label + QStringLiteral("-span") : label, rootSize), htmlLabels);
  };
  // The redux themes filter a table through a filter element, which passes
  // the table's own colors through linear RGB.
  const bool elementFilter = theme.dropShadow.trimmed().startsWith(QLatin1String("url("));
  const QString elementShadow = elementFilter ? dropShadowFilter(theme.dropShadow, input.darkTheme) : QString();

  document.neo = neo;
  document.handDrawn = handDrawn;
  if (input.useGradient) {
    document.gradient.start = input.gradientStart;
    document.gradient.stop = input.gradientStop;
  }
  document.text.table = base;
  document.text.htmlLabels = input.htmlLabels;
  document.text.relationshipHtmlLabels = input.relationshipHtmlLabels;
  document.text.relationship =
      textOf(QStringLiteral("e") + QString::number(relationships.size()) + QStringLiteral("-label"), baseRelationship,
             input.relationshipHtmlLabels);
  for (qsizetype index = 0; index < entities.size(); ++index) {
    const auto& entity = entities.at(index);
    const QString key = QStringLiteral("n") + QString::number(index);
    auto& texts = document.text.entities[entity.id];
    if (entity.attributes.isEmpty()) {
      texts.name = textOf(key + QStringLiteral("-label"), base, input.htmlLabels);
      texts.type = texts.attributeName = texts.keys = texts.comment = base;
    } else {
      texts.name = textOf(key + QStringLiteral("-l0"), base, input.htmlLabels);
      texts.type = textOf(key + QStringLiteral("-l1"), base, input.htmlLabels);
      texts.attributeName = textOf(key + QStringLiteral("-l2"), base, input.htmlLabels);
      texts.keys = textOf(key + QStringLiteral("-l3"), base, input.htmlLabels);
      texts.comment = textOf(key + QStringLiteral("-l4"), base, input.htmlLabels);
    }

    const bool attributeless = entity.attributes.isEmpty();
    auto& paint = document.paint.entities[entity.id];
    if (handDrawn) {
      paint.table =
          roughPaintOf(styles, attributeless ? key + QStringLiteral("-shape") : key + QStringLiteral("-outer"));
      paint.oddRows = paint.evenRows = paint.dividers = paint.table;
      if (!attributeless) {
        paint.oddRows = roughPaintOf(styles, key + QStringLiteral("-row0"));
        paint.evenRows = entities.at(index).attributes.size() > 1 ? roughPaintOf(styles, key + QStringLiteral("-row1"))
                                                                  : paint.oddRows;
        paint.dividers = roughPaintOf(styles, key + QStringLiteral("-divider"));
      }
      paint.filter = QStringLiteral("none");
      // The copy behind the shapes, and how its texts are set.
      const QString bg = key + QStringLiteral("-bg");
      auto& copy = document.paint.backgrounds[entity.id];
      copy.table = attributeless ? singlePaint(styles.value(bg + QStringLiteral("-rect")))
                                 : groupPaintOf(styles, bg + QStringLiteral("-outer"), true);
      copy.oddRows = attributeless ? copy.table : groupPaintOf(styles, bg + QStringLiteral("-row0"), true);
      copy.evenRows = attributeless || entities.at(index).attributes.size() < 2
                          ? copy.oddRows
                          : groupPaintOf(styles, bg + QStringLiteral("-row1"), true);
      copy.dividers = attributeless ? copy.table : groupPaintOf(styles, bg + QStringLiteral("-divider"), true);
      document.text.backgrounds.insert(
          entity.id, attributeless ? ErEntityTextStyle{textOf(bg + QStringLiteral("-label"), base, input.htmlLabels),
                                                       base, base, base, base}
                                   : ErEntityTextStyle{textOf(bg + QStringLiteral("-l0"), base, input.htmlLabels),
                                                       textOf(bg + QStringLiteral("-l1"), base, input.htmlLabels),
                                                       textOf(bg + QStringLiteral("-l2"), base, input.htmlLabels),
                                                       textOf(bg + QStringLiteral("-l3"), base, input.htmlLabels),
                                                       textOf(bg + QStringLiteral("-l4"), base, input.htmlLabels)});
      continue;
    }
    if (attributeless) {
      paint.table = singlePaint(styles.value(key + QStringLiteral("-rect")));
      paint.oddRows = paint.evenRows = paint.dividers = paint.table;
      // The filter is the rectangle's own.
      paint.filter = styles.value(key + QStringLiteral("-rect")).filter;
      if (sameFilter(paint.filter, elementShadow))
        throughFilterElement(paint.table);
      continue;
    }
    paint.table = groupPaintOf(styles, key + QStringLiteral("-outer"), true);
    paint.oddRows = groupPaintOf(styles, key + QStringLiteral("-row0"), !isNone(theme.rowOdd));
    paint.evenRows = entities.at(index).attributes.size() > 1
                         ? groupPaintOf(styles, key + QStringLiteral("-row1"), !isNone(theme.rowEven))
                         : paint.oddRows;
    paint.dividers = groupPaintOf(styles, key + QStringLiteral("-divider"), true);
    // The filter is the group's: what it filters is both its paths, and
    // nothing of the rows and the dividers, which are groups of their own.
    paint.filter = styles.value(key + QStringLiteral("-outer")).filter;
    if (sameFilter(paint.filter, elementShadow))
      throughFilterElement(paint.table);
  }
  for (qsizetype index = 0; index < relationships.size(); ++index) {
    const QString key = QStringLiteral("e") + QString::number(index);
    auto& paint = document.paint.relationships[relationships.at(index).id];
    paint.line = singlePaint(styles.value(key));
    const css::ElementStyle& box = styles.value(
        key + (input.relationshipHtmlLabels ? QStringLiteral("-label-div") : QStringLiteral("-label-rect")));
    paint.labelBackground = paintOf(input.relationshipHtmlLabels ? box.backgroundColor : box.fill);
  }
  for (const ErCardinality card :
       {ErCardinality::ExactlyOne, ErCardinality::ZeroOrOne, ErCardinality::OneOrMore, ErCardinality::ZeroOrMore}) {
    for (bool start : {true, false}) {
      const QString key = QStringLiteral("m-") + muffin::mermaid::er::erMarkerKey(card, start);
      const css::ElementStyle& path = styles.value(key + QStringLiteral("-path"));
      const css::ElementStyle& ring = styles.value(key + QStringLiteral("-circle"));
      document.paint.markers.insert(muffin::mermaid::er::erMarkerKey(card, start),
                                    {widthOf(path), path.stroke, ring.fill, ring.stroke});
    }
  }
  return document;
}

void muffin::mermaid::er::resolveErLineDashes(ErDocumentStyle& document, const ErLayoutInput& layout,
                                              const ErPlacementResult& placement, const ErStyleInput& input) {
  if (input.look != QLatin1String("neo"))
    return;
  const flowtheme::FlowThemeVariables& theme = input.theme;
  const qreal rootSize = muffin::mermaid::editor::pixelValue(theme.fontSize, 16.0);

  css::ElementStyle root;
  root.fontFamily = muffin::mermaid::editor::firstFontFamily(input.fontFamily);
  root.fontSize = QString::number(rootSize) + QStringLiteral("px");
  root.color = theme.nodeTextColor.isEmpty() ? theme.textColor : theme.nodeTextColor;
  root.fill = root.color;
  root.strokeWidth = QStringLiteral("1px");
  css::ElementStyle line = root;
  line.fill = QStringLiteral("none");
  line.stroke = theme.lineColor;

  QHash<QString, const ErLayoutRelationshipInput*> relationshipsById;
  for (const ErLayoutRelationshipInput& relationship : layout.relationships)
    relationshipsById.insert(relationship.id, &relationship);

  QVector<css::ElementInput> elements;
  addDocument(elements, root);
  QVector<QPair<QString, QString>> paths; // each path's key, and its relationship's id
  for (const ErPlacementRelationship& placed : placement.relationships) {
    const ErLayoutRelationshipInput* source = relationshipsById.value(placed.id);
    if (!source)
      continue;
    // insertEdge: along the length of the path, once it is drawn. A crow's
    // foot takes no room of the line's, so the dash runs end to end.
    const qreal length = scene::parseSvgPath(placed.path).length();
    QString dash = QStringLiteral("0 0 ");
    if (source->identifying) {
      dash += QString::number(length, 'g', 17) + QStringLiteral(" 0");
    } else {
      QStringList pairs;
      for (int pair = 0; pair < std::floor(length / 4.0); ++pair)
        pairs.append(QStringLiteral("2 2"));
      dash += pairs.join(QLatin1Char(' ')) + QStringLiteral(" 0");
    }
    const QString key = muffin::mermaid::er::erRelationshipKey(placed.id, placed.loopIndex);
    elements.append(
        {key,
         QStringLiteral("edge-paths"),
         QStringLiteral("path"),
         {},
         {QStringLiteral("edge-thickness-normal"),
          source->identifying ? QStringLiteral("edge-pattern-solid") : QStringLiteral("edge-pattern-dashed"),
          QStringLiteral("relationshipLine")},
         {},
         line,
         QStringLiteral("stroke-dasharray: %1; stroke-dashoffset: 0;").arg(dash),
         {}});
    paths.append({key, placed.id});
  }
  const QHash<QString, css::ElementStyle> styles =
      resolve(elements, muffin::mermaid::er::erBaseCss(input), input.themeCss);
  for (const auto& [key, id] : paths) {
    // The rest of the line's paint was resolved before its path existed.
    ErRelationshipPaint paint = document.paint.relationships.value(id);
    paint.line.strokeDasharray = dashOf(styles.value(key).strokeDasharray);
    document.paint.relationships.insert(key, paint);
  }
}
