// RequirementScene — scene builder + JSON serialization for the requirementDiagram
// family. Mirrors classdiagram::ClassScene.cpp / er::ErScene.cpp.
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and public functions use
// fully-qualified names.

#include "mermaid/requirement/RequirementScene.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/theme/MermaidColor.h"
#include "theme/CssCalc.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace flowchart = muffin::mermaid::flowchart;
namespace color = muffin::mermaid::color;
namespace req = muffin::mermaid::requirement;
using muffin::CssLengthContext;
using muffin::CssLengthResult;
using muffin::CssLengthStatus;
using muffin::resolveCssLengthToPx;

namespace {

constexpr qreal kPadding = 20.0;
constexpr qreal kGap = 20.0;

// mermaid-cli's default Puppeteer viewport (src/index.js: --width 800, --height
// 600, --scale unset -> deviceScaleFactor 1). Requirement resolves viewport-
// relative CSS units (vw/vh/vmin/vmax) against this so its raster output matches
// the DEFAULT mmdc raster profile; it is NOT a claim of dynamic-SVG or custom
// --width/--height parity. Lives in this Mermaid layer (not generic theme/CSS)
// and is passed explicitly to CssLengthContext.
const QSizeF kMmdcDefaultCssViewport{800.0, 600.0};

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QString cssColor(const QColor& value) {
  if (!value.isValid()) return {};
  if (value.alpha() == 255) return value.name(QColor::HexRgb);
  return color::rgba(value.red(), value.green(), value.blue(), value.alphaF());
}

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r3(r.x())},
          {QStringLiteral("y"), r3(r.y())},
          {QStringLiteral("width"), r3(r.width())},
          {QStringLiteral("height"), r3(r.height())}};
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"), r3(p.x())}, {QStringLiteral("y"), r3(p.y())}};
}

QJsonArray pointsJson(const QVector<QPointF>& pts) {
  QJsonArray a;
  for (const QPointF& p : pts) {
    QJsonArray pair;
    pair.append(r3(p.x()));
    pair.append(r3(p.y()));
    a.append(pair);
  }
  return a;
}

// Bounding rect of edge points (+ 18px margin, matching ClassScene's pointsBounds).
QRectF edgePointsBounds(const QVector<QPointF>& points,
                        const QVector<QVector<QPointF>>& segments) {
  QRectF bounds;
  bool initialized = false;
  auto includePoint = [&](const QPointF& point) {
    const QRectF pixel(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0));
    if (!initialized) {
      bounds = pixel;
      initialized = true;
    } else {
      bounds = bounds.united(pixel);
    }
  };
  for (const QPointF& point : points) includePoint(point);
  for (const QVector<QPointF>& segment : segments)
    for (const QPointF& point : segment) includePoint(point);
  return initialized ? bounds.adjusted(-18.0, -18.0, 18.0, 18.0) : QRectF{};
}

// getStrokeDashArray (chunk-BNCO5QFQ.mjs): split on \s+, single value ->
// [v, v], two values -> [a, b], NaN -> 0. Returns {0,0} for an empty value.
QVector<qreal> getStrokeDashArray(const QString& strokeDasharrayStyle) {
  if (strokeDasharrayStyle.trimmed().isEmpty()) return {0.0, 0.0};
  static const QRegularExpression ws(QStringLiteral("\\s+"));
  const QStringList raw = strokeDasharrayStyle.trimmed().split(ws, Qt::SkipEmptyParts);
  QVector<qreal> arr;
  for (const QString& p : raw) {
    bool ok = false;
    const qreal v = p.trimmed().toDouble(&ok);
    arr.append(ok ? v : 0.0);  // NaN -> 0
  }
  if (arr.size() == 1) return {arr.first(), arr.first()};
  if (arr.size() >= 2) return {arr.at(0), arr.at(1)};
  return {0.0, 0.0};
}

// Layered <paint> resolver for the requirementBox fill / outline / divider.
//
// Upstream resolves these through a 3-layer CSS cascade (probed vs mermaid 11.16.0,
// G:/github/req-probe/step3-cascade-report.json): L2 user inline `style` (unlayered,
// beats the layered L1) -> L1 genColor palette rule (present only when the node has
// a color-id k < THEME_COLOR_LIMIT, and for fill also k < bkgLen) -> L0a theme
// attribute (mainBkg / nodeBorder / dividerColor). The DOM-inherited values L0b
// (fill->foreground, outline->none, divider->dividerColor) are reached by the
// CSS-wide keywords inherit/unset/revert and by L2 garbage/revert-layer with no L1.
//
// A paint value resolves the same at any layer; revert-layer/garbage/absent fall
// through to the next. The one asymmetry the probe mandates: from L2, `absent`
// falls to L0a (mainBkg/nodeBorder) while `garbage`/`revert-layer` fall to L0b
// (foreground/none); from L1, any fall-through lands on L0a.
enum class PaintCat {
  Absent, NoneKw, CurrentColor, Inherit, Initial, Unset, Revert, RevertLayer, Color, Garbage
};
PaintCat categorizePaint(const QString& v) {
  if (v.isEmpty()) return PaintCat::Absent;
  // CSS <paint> values may carry surrounding whitespace (probed vs mermaid 11.16.0:
  // "  #ff0000  " renders red); trim before matching keywords / parsing colors.
  const QString t = v.trimmed();
  const QString l = t.toLower();
  if (l == QLatin1String("none")) return PaintCat::NoneKw;
  if (l == QLatin1String("currentcolor")) return PaintCat::CurrentColor;
  if (l == QLatin1String("inherit")) return PaintCat::Inherit;
  if (l == QLatin1String("initial")) return PaintCat::Initial;
  if (l == QLatin1String("unset")) return PaintCat::Unset;
  if (l == QLatin1String("revert")) return PaintCat::Revert;
  if (l == QLatin1String("revert-layer")) return PaintCat::RevertLayer;
  if (color::isParsableColor(t)) return PaintCat::Color;
  return PaintCat::Garbage;
}
// A resolved paint: `none` <=> NoBrush (fill) / hidden (stroke); else a color.
struct Paint { bool none = false; QString color; };
// Resolve a value AT ONE layer. Returns nullopt for fall-through (revert-layer /
// garbage / absent) so the caller walks to the next layer. initial/inherited are
// this property's initial / DOM-inherited paints (the CSS-wide keywords are
// absolute — independent of which layer declares them).
std::optional<Paint> resolveAtLayer(const QString& v, PaintCat cat, const Paint& initial,
                                    const Paint& inherited) {
  switch (cat) {
    case PaintCat::Color:        return Paint{false, v.trimmed()};  // store trimmed (see categorizePaint)
    case PaintCat::NoneKw:       return Paint{true, {}};
    case PaintCat::CurrentColor: return Paint{false, QStringLiteral("#000000")};
    case PaintCat::Initial:      return initial;
    case PaintCat::Inherit:
    case PaintCat::Unset:
    case PaintCat::Revert:       return inherited;
    case PaintCat::Absent:
    case PaintCat::RevertLayer:
    case PaintCat::Garbage:      return std::nullopt;  // fall through
  }
  return std::nullopt;
}
// Walk the cascade for one property. user/l1Entry are nullopt when that layer has
// no declaration; l0a is the theme-attribute base; initial/inherited the CSS
// initial / DOM-inherited paints for this property.
Paint resolvePaint(const std::optional<QString>& user, const std::optional<QString>& l1Entry,
                   const Paint& l0a, const Paint& initial, const Paint& inherited) {
  const PaintCat userCat = user ? categorizePaint(*user) : PaintCat::Absent;
  if (user) {
    if (auto r = resolveAtLayer(*user, userCat, initial, inherited)) return *r;
  }
  // user is absent/garbage/revert-layer -> consult L1, then the appropriate base.
  if (l1Entry) {
    if (auto r = resolveAtLayer(*l1Entry, categorizePaint(*l1Entry), initial, inherited))
      return *r;
    return l0a;  // L1 present but fell through (entry garbage/revert-layer/absent) -> L0a
  }
  // no L1: absent -> L0a (theme attribute); garbage/revert-layer -> L0b (inherited).
  return (userCat == PaintCat::Absent) ? l0a : inherited;
}

// compileStyles (chunk-BNCO5QFQ.mjs) for the box: build a Map (last value per
// key wins) from the node's event-ordered declaration list — styles2Map mirrors
// `const [key, value] = style.split(":")`, i.e. split on ':' and keep ONLY the
// first two segments (a second ':' is discarded, so "fill:red:blue" -> fill=red)
// — then userNodeOverrides reads fill/stroke/stroke-width/stroke-dasharray.
// Box-outline and divider share strokeWidth and dashArray. labelStyle keys
// (color/font-*/...) are split out by isLabelStyle upstream and do not affect
// the box — they are ignored here (text phase).
//
// SVG <paint> values for fill/stroke are resolved by the layered `resolvePaint`
// helper above (3-layer cascade: user inline -> genColor palette -> theme base),
// probed exhaustively vs mermaid 11.16.0 (G:/github/req-probe/step3-cascade-report.json).
// CSS-wide keywords are ASCII case-insensitive. Key outcomes the resolver reproduces:
//   fill: none->NoBrush; currentColor/initial->black; inherit/unset/revert->
//         foreground; revert-layer/garbage->palette fill (if active) else foreground;
//         absent->palette fill (if active) else mainBkg.
//   outline: none/initial/inherit/unset/revert->hidden; currentColor->black;
//         revert-layer/garbage->palette stroke (if active) else hidden;
//         absent->palette stroke (if active) else nodeBorder.
//   divider: tracks outline EXCEPT inherit/unset/revert->dividerColor (the divider
//         path's inherited stroke) and absent/garbage-with-no-L1->dividerColor. So
//         `stroke:none` hides BOTH outline and divider; `stroke:inherit` hides only
//         the outline. stroke-width / stroke-dasharray / zero-width below are
//         resolved separately and unchanged by the paint resolver.
void resolveBoxStyle(const QStringList& cssStyles, const req::RequirementSceneStyle& base,
                     const CssLengthContext& lengthCtx, req::RequirementSceneNode& node) {
  node.fill = base.boxFill;
  node.fillNone = false;
  node.outlineStroke = base.boxStroke;
  node.outlineVisible = true;
  node.dividerStroke = base.dividerColor;
  node.dividerVisible = true;
  node.strokeWidth = base.strokeWidth;
  node.dashArray = {0.0, 0.0};

  QHash<QString, QString> map;  // styles2Map: last value per key wins (empty if no styles)
  for (const QString& decl : cssStyles) {
    const QStringList parts = decl.split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.first().trimmed().isEmpty()) continue;  // stray token
    const QString key = parts.first().trimmed();
    const QString value = parts.size() >= 2 ? parts.at(1).trimmed() : QString();
    map.insert(key, value);
  }

  // fill / outline / divider paint through the 3-layer cascade. Always run (even
  // with no user style) so an active palette applies to the box. base here is the
  // per-node nodeBase carrying paletteFillEntry/paletteStrokeEntry when the node
  // has a color-id < THEME_COLOR_LIMIT.
  const std::optional<QString> userFill = map.contains(QStringLiteral("fill"))
      ? std::optional<QString>(map.value(QStringLiteral("fill"))) : std::nullopt;
  const std::optional<QString> userStroke = map.contains(QStringLiteral("stroke"))
      ? std::optional<QString>(map.value(QStringLiteral("stroke"))) : std::nullopt;
  const Paint kBlack{false, QStringLiteral("#000000")};
  const Paint kNone{true, {}};
  const Paint kForeground{false, base.foregroundFallback};
  const Paint fillL0a{false, base.boxFill};          // mainBkg
  const Paint outlineL0a{false, base.boxStroke};     // nodeBorder
  const Paint dividerL0a{false, base.dividerColor};  // dividerColor (also L0b for divider)
  // fill: initial=black, inherited=foreground.
  const Paint fp = resolvePaint(userFill, base.paletteFillEntry, fillL0a, kBlack, kForeground);
  node.fillNone = fp.none;
  if (!fp.none) node.fill = fp.color;
  // outline: initial=none, inherited=none (outline path's stroke inherits to none).
  const Paint op = resolvePaint(userStroke, base.paletteStrokeEntry, outlineL0a, kNone, kNone);
  node.outlineVisible = !op.none;
  if (!op.none) node.outlineStroke = op.color;
  // divider: initial=none, inherited=dividerColor (the divider path's stroke).
  const Paint dp = resolvePaint(userStroke, base.paletteStrokeEntry, dividerL0a, kNone, dividerL0a);
  node.dividerVisible = !dp.none;
  if (!dp.none) node.dividerStroke = dp.color;
  // stroke-width — mirrors the upstream cascade (probed vs mermaid 11.16.0 +
  // Chrome; see G:/github/req-probe/step0d-a-report.json). userNodeOverrides
  // passes the value through (`replace("px","")` then `|| default`, which only
  // fills an EMPTY/missing value); the SVG CSS engine then accepts or rejects
  // the (possibly garbage) length, parsing CSS units case-insensitively. The
  // resolver is property-agnostic (no baked-in 1px, negatives returned as
  // Valid(negative px)); this caller applies the stroke-width fallbacks:
  //   missing / empty              -> mermaid family default (base.strokeWidth).
  //   valid negative               -> CSS INITIAL 1.0 (stroke-width rejects <0).
  //   non-empty invalid            -> CSS INITIAL 1.0 (malformed, dropped).
  //   valid >= 0                   -> parsed px (em/rem/ex/ch relative to lengthCtx;
  //                                  vw/vh/vmin/vmax against kMmdcDefaultCssViewport).
  //   valid zero (0/0px/0em/0vw..) -> 0 -> outline + divider NoPen (see below).
  if (map.contains(QStringLiteral("stroke-width"))) {
    const CssLengthResult r =
        resolveCssLengthToPx(map.value(QStringLiteral("stroke-width")), lengthCtx);
    switch (r.status) {
      case CssLengthStatus::Missing:
        node.strokeWidth = base.strokeWidth;  // empty/missing -> family default 1.3
        break;
      case CssLengthStatus::Invalid:
        node.strokeWidth = 1.0;  // non-empty invalid -> CSS initial
        break;
      case CssLengthStatus::Valid:
        // negative -> CSS initial (stroke-width rejects negatives); zero handled
        // by the NoPen check below; positive -> parsed px.
        node.strokeWidth = (r.px < 0.0) ? 1.0 : r.px;
        break;
    }
  }
  // stroke-dasharray.
  if (map.contains(QStringLiteral("stroke-dasharray")))
    node.dashArray = getStrokeDashArray(map.value(QStringLiteral("stroke-dasharray")));

  // A VALID zero width (0/00/000px/0em) -> NoPen for both. SVG stroke-width:0 is
  // invisible, but Qt would otherwise draw a 1px cosmetic hairline at width 0
  // (setWidthF(0) is a device-space hairline). Negatives/invalids already fell
  // back to 1.0 above, so this fires only for an explicit valid zero and does
  // not disturb the stroke-decided visibility.
  if (node.strokeWidth == 0.0) {
    node.outlineVisible = false;
    node.dividerVisible = false;
  }
}

}  // namespace

namespace muffin::mermaid::requirement {

QVector<RequirementMarkerDefinition> requirementMarkerDefinitions() {
  QVector<RequirementMarkerDefinition> result;
  // requirement_contains (Start marker): refX=0, refY=10, 20×20. A circle r=9
  // (cx=cy=10, fill=none) + horizontal line (1,10→19,10) + vertical line
  // (10,1→10,19). chunk-52WLFC77.mjs ~1034-1039.
  RequirementMarkerDefinition contains;
  contains.type = QStringLiteral("requirement_contains");
  contains.suffix = QStringLiteral("Start");
  contains.refX = 0.0;
  contains.refY = 10.0;
  contains.markerWidth = 20.0;
  contains.markerHeight = 20.0;
  contains.isContains = true;
  result.append(contains);
  // requirement_arrow (End marker): refX=20, refY=10, 20×20. Open V:
  // M0,0 L20,10 M20,10 L0,20. chunk-52WLFC77.mjs ~1013-1021.
  RequirementMarkerDefinition arrow;
  arrow.type = QStringLiteral("requirement_arrow");
  arrow.suffix = QStringLiteral("End");
  arrow.refX = 20.0;
  arrow.refY = 10.0;
  arrow.markerWidth = 20.0;
  arrow.markerHeight = 20.0;
  arrow.isContains = false;
  arrow.arrowPath = QStringLiteral("M0,0 L20,10 M20,10 L0,20");
  result.append(arrow);
  return result;
}

RequirementScene buildRequirementScene(
    const RequirementLayoutInput& input,
    const RequirementLayoutMeasurements& measurements,
    const RequirementPlacementResult& placement,
    RequirementSceneStyle style) {
  RequirementScene scene;
  scene.style = std::move(style);
  scene.markers = requirementMarkerDefinitions();
  for (RequirementMarkerDefinition& marker : scene.markers) {
    marker.fill = scene.style.markerFill.isEmpty()
        ? scene.style.lineColor : scene.style.markerFill;
    marker.stroke = scene.style.markerStroke.isEmpty()
        ? scene.style.lineColor : scene.style.markerStroke;
    marker.opacity = scene.style.markerOpacity;
    marker.visible = scene.style.markerVisible;
    const auto resolved = scene.style.markerStyles.constFind(marker.type);
    if (resolved != scene.style.markerStyles.cend()) {
      marker.fill = resolved->fill;
      marker.stroke = resolved->stroke;
      marker.opacity = resolved->opacity;
      marker.visible = resolved->visible;
    }
  }

  QHash<QString, RequirementPlacementNode> placedNodes;
  for (const auto& node : placement.nodes) placedNodes.insert(node.id, node);

  // CSS length context for stroke-width resolution. exPx/chPx must come from the
  // SAME configured font the label text is measured/painted with (FlowLabel's
  // makeFlowLabelFont: MermaidFontRegistry family stack + rounded pixel size +
  // PreferNoHinting) — xHeight / '0' advance are font- and hinting-specific, so
  // reusing that construction avoids drift. emPx = SVG root font
  // (themeVariables.fontSize); remPx = <html> root (16; mermaid leaves it);
  // viewportPx = mmdc default raster profile (passed explicitly — see above).
  const QFont lengthFont =
      flowchart::makeFlowLabelFont(scene.style.fontFamily, scene.style.fontSize);
  const QFontMetricsF lengthMetrics(lengthFont);
  const CssLengthContext lengthCtx{scene.style.fontSize, 16.0, lengthMetrics.xHeight(),
                                   lengthMetrics.horizontalAdvance(QChar('0')),
                                   kMmdcDefaultCssViewport};

  for (int idx = 0; idx < input.nodes.size(); ++idx) {
    const RequirementLayoutNodeInput& node = input.nodes.at(idx);
    if (!placedNodes.contains(node.id)) continue;
    const RequirementPlacementNode placed = placedNodes.value(node.id);
    const RequirementNodeMeasurement measured = measurements.value(node.id);
    RequirementSceneNode rendered;
    rendered.id = node.id;
    rendered.displayType = node.displayType;
    rendered.isElement = node.isElement;
    rendered.center = QPointF(placed.x, placed.y);
    rendered.size = QSizeF(placed.width, placed.height);
    rendered.bodyRowCount = static_cast<int>(std::max<qsizetype>(0, node.rows.size() - 2));
    const qreal totalHeight = placed.height;
    const qreal totalWidth = placed.width;
    rendered.hasDivider = node.hasBody;
    // Mermaid draws the divider at the body top: lineY = y + typeHeight +
    // nameHeight + gap where y = -totalHeight/2 is the box top (no padding) —
    // chunk-ZGVPDNZ5.mjs / chunk-65BZPYT2.mjs. Computed once here from the
    // measurements so the painter does not re-derive it from row centers.
    if (node.hasBody) {
      rendered.dividerY = -totalHeight / 2.0 +
                          measured.typeHeight + measured.nameHeight + kGap;
    }
    // Resolve the box paint (fill/stroke/stroke-width/dash) from the node's
    // event-ordered cssStyles, last-wins over the theme base. The divider
    // follows the explicit stroke/stroke-width and otherwise keeps the theme
    // divider color; both default to the 1.3 requirement border size.
    //
    // colorIndex palette (Commit 4 + review-fixes): when the theme supplies
    // borderColorArray (only the built-in redux-color / redux-dark-color do — user
    // arrays are ignored via the %%{init}%% source entry), genColor's palette CSS
    // layer colors the node by insertion order. buildRequirementLayoutInput appends
    // requirements then elements, so idx here == upstream colorIndex
    // (RequirementDB.getData stamps colorIndex before each push). We don't bake
    // the palette colors into nodeBase here — instead we record the L1 palette
    // DECLARATIONS (paletteStrokeEntry / paletteFillEntry) and let resolveBoxStyle's
    // layered resolver walk them with the user inline + theme base. k = idx %
    // borderLen is the data-color-id index; a genColor rule exists only for
    // k < themeColorLimit (THEME_COLOR_LIMIT), and the fill rule only when
    // k < bkgLen (redux-dark-color has an empty bkg -> no fill rule -> mainBkg).
    RequirementSceneStyle nodeBase = scene.style;
    const QStringList& border = scene.style.borderColorArray;
    if (!border.isEmpty()) {
      const int k = idx % border.size();
      if (k < scene.style.themeColorLimit)
        nodeBase.paletteStrokeEntry = border.at(k);  // `.node path`/`.node rect` rule
      const QStringList& bkg = scene.style.bkgColorArray;
      if (k < scene.style.themeColorLimit && k < bkg.size())
        nodeBase.paletteFillEntry = bkg.at(k);
      // k >= limit: no genColor rule -> both entries stay unset -> resolver uses L0a.
    }
    resolveBoxStyle(node.cssStyles, nodeBase, lengthCtx, rendered);
    // Resolve the text style (Commit 3) from the same event-ordered cssStyles.
    // Pure like resolveBoxStyle; the measure path resolves it again identically.
    rendered.text = resolveRequirementTextStyle(
        node.cssStyles, scene.style.fontFamily, scene.style.fontSize,
        scene.style.fontWeight, scene.style.lineHeight);
    rendered.visible = node.groupVisible;
    rendered.hasBox = node.groupHasBox;
    rendered.rootHasBox = node.groupRootHasBox;
    if (node.hasResolvedBoxStyle) {
      rendered.fill = node.boxStyle.fill;
      rendered.fillNone = node.boxStyle.fill.trimmed().compare(
          QLatin1String("none"), Qt::CaseInsensitive) == 0;
      rendered.outlineStroke = node.boxStyle.stroke;
      rendered.outlineVisible = node.boxStyle.visible &&
          node.boxStyle.stroke.trimmed().compare(
              QLatin1String("none"), Qt::CaseInsensitive) != 0 &&
          node.boxStyle.strokeWidth > 0.0;
      rendered.strokeWidth = node.boxStyle.strokeWidth;
      rendered.fillOpacity = node.boxStyle.fillOpacity;
      rendered.strokeOpacity = node.boxStyle.strokeOpacity;
      rendered.opacity = node.boxStyle.opacity;
      rendered.visible = rendered.visible && node.boxStyle.visible;
      rendered.hasBox = rendered.hasBox && node.boxStyle.hasBox;
      rendered.rootHasBox = rendered.rootHasBox && node.boxStyle.rootHasBox;
    }
    if (node.hasResolvedDividerStyle) {
      rendered.dividerStroke = node.dividerStyle.stroke;
      rendered.dividerVisible = node.dividerStyle.visible &&
          node.dividerStyle.stroke.trimmed().compare(
              QLatin1String("none"), Qt::CaseInsensitive) != 0 &&
          node.dividerStyle.strokeWidth > 0.0;
      rendered.dividerStrokeOpacity = node.dividerStyle.strokeOpacity;
      rendered.dividerOpacity = node.dividerStyle.opacity;
      rendered.dividerStrokeWidth = node.dividerStyle.strokeWidth;
      rendered.dividerRootVisible = node.dividerStyle.visible;
      rendered.dividerWrapperComputed = node.dividerWrapperComputed;
      rendered.dividerChildPaths.clear();
      for (qsizetype pathIndex = 0;
           pathIndex < node.dividerChildPathComputed.size(); ++pathIndex) {
        const RequirementComputedElement& computed =
            node.dividerChildPathComputed.at(pathIndex);
        RequirementPaintedPathStyle painted;
        painted.stroke = computed.stroke;
        painted.strokeWidth = pathIndex == 0
            ? node.dividerStyle.strokeWidth : rendered.dividerStrokeWidth;
        painted.effectiveStrokeOpacity = computed.effectiveStrokeOpacity;
        painted.displayed = computed.displayed;
        rendered.dividerChildPaths.append(std::move(painted));
      }
    }
    if (!node.hasResolvedDividerStyle)
      rendered.dividerStrokeWidth = rendered.strokeWidth;

    // Row positions (relative to node center). Mermaid positions rows at
    // sequential y-offsets, then shifts so the box is centered. Row i's text
    // center Y (relative) = yoffset_i - totalHeight/2 + padding, where
    // totalHeight = the dagre box height.
    // font-size:0 / line-height:0 -> the node collapses to mermaid's 20x20 min box
    // with no text ink (STEP0F §2). Mirror the measure path: skip font/measure
    // work entirely (a 0px QFont violates Qt's positive pixel-size contract, and
    // upstream likewise skips font build/measure/paint). effLineHeight is left at
    // its sentinel when the font is absent so the natural-height branch below
    // never constructs a 0px font; the zero row heights already came back from
    // measure, so the emitted rows are zero-size and paintRow skips them.
    qreal yoffset = 0.0;
    for (qsizetype i = 0; i < node.rows.size(); ++i) {
      const RequirementLayoutRow& row = node.rows.at(i);
      const RequirementTextStyle& rowStyle = row.hasResolvedStyle
          ? row.resolvedStyle : rendered.text;
      const qreal effSize = requirementEffectiveFontSize(
          rowStyle, scene.style.fontSize);
      const QString effFamily = requirementEffectiveFontFamily(
          rowStyle, scene.style.fontFamily);
      qreal effLineHeight = -1.0;
      if (effSize != 0.0) {
        if (rowStyle.lineHeightNormal) {
          effLineHeight = QFontMetricsF(flowchart::makeFlowLabelFont(
              effFamily, effSize, rowStyle.fontWeight,
              rowStyle.fontStyle)).height();
        } else {
          effLineHeight = rowStyle.lineHeightPx >= 0.0
              ? rowStyle.lineHeightPx : effSize * 1.5;
        }
      }
      const bool noText = !row.hasBox || effSize == 0.0 ||
                          effLineHeight == 0.0;
      const qreal rowHeight = measured.rowHeights.value(i, 0.0);
      RequirementSceneRow sceneRow;
      sceneRow.text = row.text;
      sceneRow.bold = row.bold;
      sceneRow.fontPixelSize = effSize;
      sceneRow.fontFamily = effFamily;
      sceneRow.lineHeight = effLineHeight;
      sceneRow.color = rowStyle.color;
      sceneRow.opacity = row.opacity;
      sceneRow.visible = row.visible;
      sceneRow.hasBox = row.hasBox;
      sceneRow.rootHasBox = row.rootHasBox;
      sceneRow.wrapperComputed = row.wrapperComputed;
      sceneRow.paintedTextComputed = row.paintedTextComputed;
      qreal rowWidth = 0.0;  // ink width (0 when text is collapsed -> no paint)
      if (!noText) {
        // Build the row document once (text-transform + Commit-2 fields applied)
        // and reuse it for the width measure and the stored paint document.
        const flowchart::FlowLabelDocument doc =
            measured.rowDocuments.value(i);
        const QRectF rawInk = measured.rowInkBounds.value(i);
        rowWidth = rawInk.width();
        sceneRow.document = doc;
      }
      const QRectF rawInk = measured.rowInkBounds.value(i);
      sceneRow.size = scene.style.htmlLabels
          ? QSizeF(rowWidth, rowHeight) : rawInk.size();
      // Horizontal: type(0) + name(1) are centered (x=0); body rows are
      // left-aligned at -totalWidth/2 + padding/2 (mermaid's translateX for i>=2).
      qreal translateX = -rowWidth / 2.0;
      if (i >= 2) translateX = -totalWidth / 2.0 + kPadding / 2.0;
      const qreal translateY = -rowHeight / 2.0 + yoffset -
                               totalHeight / 2.0 + kPadding;
      const QRectF positioned = rawInk.translated(translateX, translateY);
      const qreal centerX = scene.style.htmlLabels
          ? (i >= 2 ? -totalWidth / 2.0 + kPadding / 2.0 + rowWidth / 2.0
                    : 0.0)
          : positioned.center().x();
      const qreal centerY = scene.style.htmlLabels
          ? yoffset - totalHeight / 2.0 + kPadding
          : positioned.center().y();
      sceneRow.center = QPointF(centerX, centerY);
      rendered.rows.append(std::move(sceneRow));
      yoffset += rowHeight;
      if (i == 1 && node.hasBody) yoffset += kGap;
    }
    scene.nodes.append(std::move(rendered));
  }

  // Edges: deduplicate by id (matching mermaid's shared dagre pipeline, which
  // renders one edge per id). The mermaid DB assigns id `${src}-${dst}-0` to
  // every relation, so same-pair relations collide and the LAST one wins. We
  // iterate in reverse and skip already-seen ids so the last input edge per id
  // is the one rendered.
  QSet<QString> seenEdgeIds;
  QVector<const RequirementLayoutEdgeInput*> edgesInRenderOrder;
  for (auto it = input.edges.rbegin(); it != input.edges.rend(); ++it) {
    if (seenEdgeIds.contains(it->id)) continue;
    seenEdgeIds.insert(it->id);
    edgesInRenderOrder.prepend(&*it);
  }
  for (const RequirementLayoutEdgeInput* edgePtr : edgesInRenderOrder) {
    const RequirementLayoutEdgeInput& edge = *edgePtr;
    const auto found = std::find_if(placement.edges.cbegin(), placement.edges.cend(),
        [&](const auto& value) { return value.id == edge.id; });
    if (found == placement.edges.cend()) continue;
    RequirementSceneEdge rendered;
    rendered.id = edge.id;
    rendered.path = found->path;
    rendered.points = found->points;
    rendered.segments = found->segments;
    rendered.label = edge.label;
    rendered.relationshipType = edge.relationshipType;
    rendered.isContains = edge.isContains;
    rendered.pattern = edge.isContains ? QStringLiteral("solid") : QStringLiteral("dashed");
    rendered.markerStart = edge.isContains ? QStringLiteral("requirement_contains") : QString();
    rendered.markerEnd = edge.isContains ? QString() : QStringLiteral("requirement_arrow");
    rendered.labelPosition = found->labelPosition;
    rendered.stroke = scene.style.lineColor;
    // labelFill models the `.edgeLabel .label` <g> computed background-color.
    // The actual foreignObject background comes from div.labelBkg; the span's
    // background remains separate computed style but emits no pixels in Chrome.
    rendered.labelFill = QStringLiteral("transparent");
    rendered.labelColor = scene.style.edgeLabelColor;
    rendered.labelContainerBg.color = scene.style.edgeLabelContainerFill;
    rendered.labelTextBg.color = scene.style.edgeLabelFill;
    if (edge.hasResolvedPathStyle) {
      rendered.stroke = edge.pathStyle.stroke;
      rendered.strokeWidth = edge.pathStyle.strokeWidth;
      rendered.strokeOpacity = edge.pathStyle.strokeOpacity;
      rendered.opacity = edge.pathStyle.opacity;
      rendered.visible = edge.pathStyle.visible;
      rendered.rootHasBox = edge.pathStyle.rootHasBox;
    }
    if (edge.resolvedLabel.hasResolvedStyle) {
      rendered.labelTextStyle = edge.resolvedLabel.resolvedStyle;
      rendered.labelOpacity = edge.resolvedLabel.opacity;
      rendered.labelVisible = edge.resolvedLabel.visible;
      rendered.labelRootHasBox = edge.resolvedLabel.rootHasBox;
      if (edge.resolvedLabel.resolvedStyle.color.isValid())
        rendered.labelColor = cssColor(
            edge.resolvedLabel.resolvedStyle.color);
    }
    rendered.hasLabelCascade = edge.hasLabelCascade;
    if (edge.hasLabelCascade) {
      rendered.outerLabelComputed = edge.outerLabelComputed;
      rendered.innerLabelComputed = edge.innerLabelComputed;
      rendered.paintedSpanComputed = edge.paintedSpanComputed;
      rendered.labelContainerBg.color = edge.containerBgComputed.backgroundColor;
      rendered.labelContainerBg.effectiveOpacity =
          edge.containerBgComputed.effectiveOpacity;
      rendered.labelContainerBg.displayed = edge.containerBgComputed.displayed;
      rendered.labelTextBg.color = edge.paintedSpanComputed.backgroundColor;
      rendered.labelTextBg.effectiveOpacity =
          edge.paintedSpanComputed.effectiveOpacity;
      rendered.labelTextBg.displayed = edge.paintedSpanComputed.displayed;
    } else {
      rendered.paintedSpanComputed.displayed = rendered.labelVisible;
      rendered.paintedSpanComputed.effectiveOpacity = rendered.labelOpacity;
      rendered.labelContainerBg.displayed = rendered.labelVisible;
      rendered.labelTextBg.displayed = rendered.labelVisible;
      rendered.labelContainerBg.effectiveOpacity = rendered.labelOpacity;
      rendered.labelTextBg.effectiveOpacity = rendered.labelOpacity;
    }
    if (!edge.labelBackgroundStyle.fill.isEmpty())
      rendered.labelFill = edge.labelBackgroundStyle.fill;
    if (!edge.label.isEmpty()) {
      rendered.labelDocument = edge.resolvedLabel.hasResolvedStyle
          ? requirementRowDocument(edge.label, false,
                                   edge.resolvedLabel.resolvedStyle,
                                   scene.style.fontSize,
                                   scene.style.htmlLabels)
          : (scene.style.htmlLabels
                 ? flowchart::parseFlowLabel(edge.label,
                                             QStringLiteral("markdown"))
                 : flowchart::parseFlowSvgLabel(edge.label,
                                                QStringLiteral("markdown")));
      flowchart::FlowTextOptions labelOptions;
      const RequirementTextStyle& labelText =
          edge.resolvedLabel.hasResolvedStyle
              ? edge.resolvedLabel.resolvedStyle
              : rendered.labelTextStyle;
      labelOptions.fontFamily = edge.resolvedLabel.hasResolvedStyle
          ? requirementEffectiveFontFamily(labelText, scene.style.fontFamily)
          : scene.style.fontFamily;
      labelOptions.fontPixelSize = edge.resolvedLabel.hasResolvedStyle
          ? requirementEffectiveFontSize(labelText, scene.style.fontSize)
          : scene.style.fontSize;
      labelOptions.lineHeight = edge.resolvedLabel.hasResolvedStyle &&
              labelText.lineHeightPx >= 0.0
          ? labelText.lineHeightPx : labelOptions.fontPixelSize * 1.5;
      labelOptions.htmlLabels = scene.style.htmlLabels;
      rendered.labelSize = flowchart::measureLabel(
          edge.label, QStringLiteral("markdown"), labelOptions);
      if (rendered.labelPosition) {
        rendered.labelBounds = QRectF(
            *rendered.labelPosition -
                QPointF(rendered.labelSize.width() / 2.0,
                        rendered.labelSize.height() / 2.0),
            rendered.labelSize);
      }
    }
    rendered.pathBounds = edgePointsBounds(found->points, found->segments);
    // If the dagre pipeline returned an empty path, synthesize a basis curve
    // from the points (matching ClassScene's path fallback).
    if (rendered.path.isEmpty() && !rendered.points.isEmpty()) {
      rendered.path = flowchart::d3curve::pathForCurve(rendered.points,
                                                       QStringLiteral("basis"));
    }
    scene.edges.append(std::move(rendered));
  }

  // Scene bounds = union of all node boxes + edge points (mirrors ClassScene).
  bool first = true;
  auto unite = [&](const QRectF& bounds) {
    if (first) {
      scene.bounds = bounds;
      first = false;
    } else {
      scene.bounds = scene.bounds.united(bounds);
    }
  };
  for (const auto& node : scene.nodes) {
    if (node.rootHasBox)
      unite(QRectF(node.center.x() - node.size.width() / 2.0,
                   node.center.y() - node.size.height() / 2.0,
                   node.size.width(), node.size.height()));
  }
  for (const auto& edge : scene.edges) {
    if (!edge.rootHasBox) continue;
    for (const QPointF& point : edge.points)
      unite(QRectF(point, QSizeF(0.0, 0.0)));
    for (const QVector<QPointF>& segment : edge.segments)
      for (const QPointF& point : segment) unite(QRectF(point, QSizeF(0.0, 0.0)));
  }
  return scene;
}

QJsonObject RequirementScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("bounds")] = rectJson(bounds);

  QJsonArray nodesArray;
  for (const RequirementSceneNode& node : nodes) {
    QJsonObject n;
    n[QStringLiteral("id")] = node.id;
    if (!node.displayType.isEmpty())
      n[QStringLiteral("type")] = node.displayType;
    n[QStringLiteral("cx")] = r3(node.center.x());
    n[QStringLiteral("cy")] = r3(node.center.y());
    n[QStringLiteral("width")] = r3(node.size.width());
    n[QStringLiteral("height")] = r3(node.size.height());
    n[QStringLiteral("bodyRows")] = node.bodyRowCount;
    n[QStringLiteral("dividers")] = node.hasDivider ? 1 : 0;
    if (node.hasDivider) n[QStringLiteral("dividerY")] = r3(node.dividerY);
    if (!node.fill.isEmpty())
      n[QStringLiteral("fill")] = node.fill;
    if (!node.outlineStroke.isEmpty())
      n[QStringLiteral("outlineStroke")] = node.outlineStroke;
    if (!node.outlineVisible)
      n[QStringLiteral("outlineVisible")] = false;
    if (!node.dividerStroke.isEmpty())
      n[QStringLiteral("dividerStroke")] = node.dividerStroke;
    if (!node.dividerVisible)
      n[QStringLiteral("dividerVisible")] = false;
    n[QStringLiteral("strokeWidth")] = r3(node.strokeWidth);
    if (!node.dashArray.isEmpty() &&
        (node.dashArray.at(0) != 0.0 || node.dashArray.at(1) != 0.0)) {
      QJsonArray dash;
      dash.append(r3(node.dashArray.at(0)));
      dash.append(r3(node.dashArray.at(1)));
      n[QStringLiteral("dashArray")] = dash;
    }
    nodesArray.append(n);
  }
  o[QStringLiteral("nodes")] = nodesArray;

  QJsonArray edgesArray;
  for (const RequirementSceneEdge& edge : edges) {
    QJsonObject e;
    e[QStringLiteral("id")] = edge.id;
    if (!edge.pattern.isEmpty())
      e[QStringLiteral("pattern")] = edge.pattern;
    if (!edge.markerStart.isEmpty())
      e[QStringLiteral("markerStart")] = edge.markerStart;
    if (!edge.markerEnd.isEmpty())
      e[QStringLiteral("markerEnd")] = edge.markerEnd;
    if (!edge.relationshipType.isEmpty())
      e[QStringLiteral("type")] = edge.relationshipType;
    if (!edge.label.isEmpty())
      e[QStringLiteral("label")] = edge.label;
    e[QStringLiteral("points")] = pointsJson(edge.points);
    edgesArray.append(e);
  }
  o[QStringLiteral("edges")] = edgesArray;

  QJsonArray markersArray;
  for (const RequirementMarkerDefinition& marker : markers) {
    QJsonObject m;
    m[QStringLiteral("type")] = marker.type;
    if (!marker.suffix.isEmpty())
      m[QStringLiteral("suffix")] = marker.suffix;
    markersArray.append(m);
  }
  o[QStringLiteral("markers")] = markersArray;

  return o;
}

SvgMarkerProjection RequirementScene::svgMarkerProjection() const {
  SvgMarkerProjection projection;
  for (const RequirementMarkerDefinition& source : markers) {
    SvgMarkerDefinition definition;
    definition.key = source.type;
    definition.idSuffix = QStringLiteral("_requirement-") + source.type + source.suffix;
    definition.refX = source.refX; definition.refY = source.refY;
    definition.markerWidth = source.markerWidth;
    definition.markerHeight = source.markerHeight;
    if (source.isContains) {
      definition.groupChildren = true;
      SvgMarkerChild circle;
      circle.tag = QStringLiteral("circle"); circle.cx = 10; circle.cy = 10;
      circle.radius = 9; circle.fill = QStringLiteral("none");
      circle.stroke = style.lineColor;
      definition.children.append(circle);
      for (const QVector<qreal>& line : QVector<QVector<qreal>>{
               {1, 10, 19, 10}, {10, 1, 10, 19}}) {
        SvgMarkerChild child;
        child.tag = QStringLiteral("line");
        child.x1 = line.at(0); child.y1 = line.at(1);
        child.x2 = line.at(2); child.y2 = line.at(3);
        child.stroke = style.lineColor;
        definition.children.append(child);
      }
    } else {
      SvgMarkerChild child;
      child.tag = QStringLiteral("path");
      child.path = QStringLiteral("M0,0\n      L20,10\n      M20,10\n      L0,20");
      child.fill = QStringLiteral("none"); child.stroke = style.lineColor;
      definition.children.append(child);
    }
    projection.definitions.append(definition);
  }
  for (const RequirementSceneEdge& source : edges) {
    if (source.markerStart.isEmpty() && source.markerEnd.isEmpty()) continue;
    SvgMarkerEdge edge;
    edge.id = source.id; edge.cssClass = QStringLiteral("relationshipLine");
    edge.path = source.path;
    edge.markerStart = source.markerStart;
    edge.markerEnd = source.markerEnd;
    edge.stroke = style.lineColor; edge.strokeWidth = QStringLiteral("1");
    if (!source.isContains) edge.strokeDasharray = QStringLiteral("10,7");
    projection.edges.append(edge);
  }
  return projection;
}

}  // namespace muffin::mermaid::requirement
