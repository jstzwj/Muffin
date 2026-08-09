#pragma once

// Shared render-pipeline helpers used by every Diagram implementation —
// theme resolution, config reads, render-metadata construction, and entry
// finalization (docs/mermaid-architecture.md, L0/L3 support layer). Extracted
// from MermaidRenderCache so diagram impls can live in their own translation
// units without re-declaring these.

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/theme/FlowTheme.h"
#include "theme/CssCalc.h"

#include <QFont>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

namespace muffin::mermaid {
struct MermaidPreprocessResult;
}

namespace muffin::mermaid::editor {

struct MermaidRenderEntry;

flowtheme::FlowThemeId themeIdFromName(const QString& name);
QString themeFromConfig(const QJsonObject& config);
QHash<QString, QString> themeOverrides(const QJsonObject& config);

// genColor rule count for THEME_COLOR_LIMIT, replicating upstream's JS
// `for (let i = 0; i < THEME_COLOR_LIMIT; i++)`: the count of non-negative
// integers < Number(TCL), i.e. ceil of a positive finite Number() of the RAW
// themeVariables value. Returns nullopt when the key is absent (caller uses the
// theme default); 0 for NaN / non-positive / unconvertible (incl. Number(null)=0,
// Number("abc")=NaN). Number(bool): true->1, false->0; the bool vs string-"true"
// distinction needs the raw JSON type, so this reads pre.config directly rather
// than the string-flattened themeOverrides.
std::optional<int> jsThemeColorLimit(const QJsonObject& config);

qreal pixelValue(const QString& value, qreal fallback);

// A CssLengthContext for the Pie family, mirroring requirement's layerCtx
// (RequirementTextStyle.cpp:107): emPx = inherited SVG root font-size, remPx =
// 16 (browser default <html> root), exPx/chPx from the actual Pie font's
// QFontMetricsF, viewport = mmdc's default 800x600 raster profile (vw/vh/vmin/
// vmax). NOT the neutral CssLengthContext{} placeholder. A valid zero (or sub-px)
// emPx is PRESERVED (upstream honors fontSize:"0px" -- em/%/inherited collapse to
// 0); it is NOT coerced to 16. ex/ch are 0 at a 0 root (no 0px QFont built).
CssLengthContext pieCssLengthContext(const QString& fontFamily, qreal emPx);

// CSS <length> -> px for SVG stroke-width. Delegates to resolveCssLengthToPx
// (full unit space) against `ctx`; a percentage resolves to N/100 of the SVG
// normalized diagonal `diagonalPx` (sqrt(w^2+h^2)/sqrt(2) of the SVG viewport --
// probed, deferred to paint, so the caller passes the scene bounds diagonal).
// A valid length (incl. 0) is returned; a negative or missing/invalid value
// yields the CSS initial (1). Probed: "2px"->2, "1.7"->1.7, "0"->0, "3em"->48,
// "10vw"->80, "abc"->1. A 0 result means the caller paints NoPen.
qreal cssStrokeWidthPx(const QString& value, const CssLengthContext& ctx, qreal diagonalPx);

// CSS opacity: a number or percentage, clamped to [0,1]; missing/invalid/
// non-finite -> CSS initial (1). Probed: "0.7"->0.7, "50%"->0.5, "150%"->1,
// "0"->0, "-0.5"->0, "abc"->1.
qreal cssOpacity(const QString& value);

// CSS font-size -> px against `ctx`. A percentage resolves to N/100 of the
// parent font-size (ctx.emPx). A BARE number (full CSS <number>, incl.
// exponent: "1e2" or "25") is invalid for font-size -> the element INHERITS the
// parent font-size (ctx.emPx), not a hardcoded 16; em/rem scale by emPx; a
// negative length is invalid -> inherited (ctx.emPx). ctx.emPx MUST be the
// resolved SVG root font-size (themeVariables.fontSize, resolved against the
// 16px <html> root) -- build it via a root pass: pieCssLengthContext(f, 16) ->
// cssFontSizePx(themeVars.fontSize, rootCtx) -> pieCssLengthContext(f, rootFs).
// Probed vs 11.16.0: neo root 14 -> "25"/"abc"/"-2px"/"" all inherit 14; "2em"
// root + "200%" -> 64; "25px"->25, "1e2"->16(default ctx), "1e2px"->100, "0px"->0.
qreal cssFontSizePx(const QString& value, const CssLengthContext& ctx);

// Replicates upstream parseFontSize()[0] ?? 2 (pieDiagram-ENE6RG2P.mjs:157):
// parseInt(value, 10) of the LEADING integer (truncates decimals, ignores any
// unit), defaulting to 2 when there is no leading integer. Upstream uses this
// numeric prefix -- NOT the CSS paint width -- for the outer-circle radius
// (r = radius + n/2), so it must be tracked separately from cssStrokeWidthPx.
qreal parseFontSizeNumber(const QString& value);
// Overload preserving the JSON type: upstream parseFontSize() returns a NUMBER
// input verbatim (1.7 -> 1.7) but parseInt-truncates a STRING ("1.7" -> 1). The
// model's themeOverrides flattens numbers to strings, so the geom must be read
// from the RAW QJsonValue: isDouble -> the number; isString -> parseInt; else
// (absent) -> parseFontSizeNumber(fallbackString). Probed: 1.7 -> r 185.85,
// "1.7" -> r 185.5, "2px" -> r 186.
qreal parseFontSizeNumber(const QJsonValue& raw, const QString& fallbackString);

QString firstFontFamily(QString cssFamily);
qreal configNumber(const QJsonObject& object, const QString& key, qreal fallback);

// Parses a CSS font-weight value into Qt's QFont::Weight. Qt 6 uses the standard
// CSS 100..900 scale (Normal=400, Bold=700), so the value maps near-identity.
// Accepts "normal" (400), "bold" (700), "bolder" (700) and "lighter" (100) —
// bolder/lighter resolve against the inherited normal — and a number or numeric
// string in the valid CSS range 1..1000. Anything absent/null/out-of-range
// (0, 1001) or unparseable yields the fallback, matching a browser's fall-back
// to normal. Qt snaps the value to the nearest available face.
QFont::Weight cssFontWeightToQt(const QJsonValue& value, QFont::Weight fallback);

// JS-style truthiness for a config value. Mirrors mermaid setConf()'s
// `if (cnf.fontWeight)` gate, where a truthy global value overrides the
// per-label fields: numbers != 0 and non-empty strings are truthy; null,
// undefined, 0 and "" are falsy.
bool truthyConfigValue(const QJsonValue& value);

MermaidRenderMetadata renderMetadata(const MermaidPreprocessResult& pre,
                                     const QString& diagramType,
                                     const QString& diagramTitle,
                                     const QString& accessibleTitle,
                                     const QString& accessibleDescription,
                                     const QString& titleColor,
                                     const QString& fontFamily,
                                     qreal titleFontSize,
                                     qreal titleTopMargin = 25.0,
                                     qreal diagramPadding = 0.0);

void finalizeReadyEntry(MermaidRenderEntry& entry, MermaidRenderMetadata metadata);

}  // namespace muffin::mermaid::editor
