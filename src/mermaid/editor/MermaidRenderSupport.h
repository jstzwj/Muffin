#pragma once

// Shared render-pipeline helpers used by every Diagram implementation —
// theme resolution, config reads, render-metadata construction, and entry
// finalization (docs/mermaid-architecture.md, L0/L3 support layer). Extracted
// from MermaidRenderCache so diagram impls can live in their own translation
// units without re-declaring these.

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/theme/FlowTheme.h"

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

// Browser-faithful CSS length parser for themeVariables that upstream emits as
// raw CSS and lets the browser resolve (pie stroke widths / text sizes). Unlike
// pixelValue (which requires a "px" suffix and rejects 0), this mirrors the
// browser: a bare number or an "Npx" value resolves to N pixels, 0 is accepted
// (a 0-width stroke paints nothing), and anything else (em/pt/% units, invalid
// text, empty, negative) falls back. Probed vs mermaid 11.16.0
// (scripts/probe_mermaid_pie_scalars.mjs): "2px"->2, "2"->2, "0"->0, "0px"->0,
// "1.7"->1.7; "3em" (browser resolves font-relative) and "abc" (browser CSS
// initial 1) fall back here -- documented divergences for exotic/garbage input.
qreal parseCssPx(const QString& value, qreal fallback);

// Browser-faithful CSS opacity parser: a unitless number clamped to [0,1].
// Probed vs mermaid 11.16.0: "0.7"->0.7, "0"->0, "1.7"->1 (clamp), "-0.5"->0
// (clamp). A non-numeric/empty value falls back (the browser would use the CSS
// initial 1.0; falling back to the theme default is a documented divergence for
// garbage input).
qreal opacityValue(const QString& value, qreal fallback);

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
