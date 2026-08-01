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

namespace muffin::mermaid {
struct MermaidPreprocessResult;
}

namespace muffin::mermaid::editor {

struct MermaidRenderEntry;

flowtheme::FlowThemeId themeIdFromName(const QString& name);
QString themeFromConfig(const QJsonObject& config);
QHash<QString, QString> themeOverrides(const QJsonObject& config);
qreal pixelValue(const QString& value, qreal fallback);
QString firstFontFamily(QString cssFamily);
qreal configNumber(const QJsonObject& object, const QString& key, qreal fallback);

// Parses a CSS font-weight value (number 1..1000, "normal", "bold", or a numeric
// string) into Qt's QFont::Weight. Qt 6 uses the standard CSS 100..900 scale
// (Normal=400, Bold=700), so the value maps near-identity; Qt snaps it to the
// nearest available face, like a browser resolving CSS font-weight. Anything
// absent/null/unparseable yields the fallback.
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
