#pragma once

// Renders a FlowScene to a QImage via QPainter (milestone F3). This is the
// single paint path the editor (milestone I) and image/PDF export will share;
// screen vs export differ only in the viewport transform. The painter reads
// ONLY the scene — it never touches FlowDB or re-runs layout.
//
// Pixel fidelity: non-text geometry (node fills/strokes, edges, markers,
// clusters, background) is rendered with the scene's resolved colours and
// geometry, matching mermaid's SVG closely. Text (node labels are foreignObject
// in mermaid 11.16, edge labels are <text>) is rendered with QPainter text —
// rasterization differs from Chrome, so the Level-3 pixel test samples interior
// colours (exact) + compares alpha silhouettes (AA tolerance) rather than
// requiring byte-exact text.

#include "mermaid/scene/FlowScene.h"

#include <QImage>
#include <QtGlobal>

class QPainter;

namespace muffin::mermaid::flowscene {

// Paint mode. `Color` renders the scene with its resolved colours (the editor/
// export path). `CategoryMask` renders the SAME geometry in the SAME draw order
// but with flat reserved category colours (below), antialiasing OFF — this is
// the Level-3 comparison's per-pixel classifier. Because both modes traverse the
// identical shape dispatch + draw order, the mask can never drift from what the
// colour path actually rasterizes.
enum class PaintMode { Color, CategoryMask };

// Reserved category colours for PaintMode::CategoryMask. Fully opaque, distinct,
// with the low byte as the category id so the comparison engine decodes a mask
// pixel by its RGB. A mask pixel that was never painted is transparent = EMPTY.
constexpr QRgb kCatNode = 0xFF000101u;
constexpr QRgb kCatCluster = 0xFF000102u;
constexpr QRgb kCatEdge = 0xFF000103u;
constexpr QRgb kCatEdgeLabelBg = 0xFF000104u;
constexpr QRgb kCatText = 0xFF000105u;
constexpr QRgb kCatShadow = 0xFF000106u;
constexpr QRgb kCatBoundary = 0xFF000107u;

// Paint the scene onto an already-configured painter. The painter's transform
// maps scene coordinates to device coordinates (the caller sets the viewport).
// `fontFamily` overrides the scene's per-label fontFamily (the rendered font is
// a config concern — mermaid's config fontFamily overrides the theme's; the
// pixel golden uses Arial).
void paintFlowScene(const FlowScene& scene, QPainter& painter,
                    const QString& fontFamily = QStringLiteral("Arial"),
                    PaintMode mode = PaintMode::Color);

// Convenience: render the scene to a fresh QImage at the given DPR. The image
// is sized to scene.bounds + padding, with scene coords translated so the
// top-left of the bounds is at (padding, padding).
QImage renderFlowSceneToImage(const FlowScene& scene, qreal dpr = 1.0, qreal padding = 8.0,
                              const QString& fontFamily = QStringLiteral("Arial"));

}  // namespace muffin::mermaid::flowscene
