#pragma once

// Shared render-pipeline helpers used by every Diagram implementation —
// theme resolution, config reads, render-metadata construction, and entry
// finalization (docs/mermaid-architecture.md, L0/L3 support layer). Extracted
// from MermaidRenderCache so diagram impls can live in their own translation
// units without re-declaring these.

#include "mermaid/MermaidRenderMetadata.h"
#include "mermaid/theme/FlowTheme.h"

#include <QHash>
#include <QJsonObject>
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
