#pragma once

#include "theme/RenderTheme.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace muffin {

// Paints captured CSS decorations (element background gradients, ::before/::after
// rules, #write texture overlay) against the host layout's box geometry.
namespace DecorationPainter {

// Context for pseudo-element positioning (what the host is and where its text
// sits). Fields are optional; painters fall back to the rect when unset.
struct PaintContext {
  int headingLevel = 0;                     // 1..6 when the host is a heading
  QString codeLanguage;                     // code-fence language, for ::before attr()
  QFont font;                               // host font (sizes icons / content text)
  QRectF textBounds;                        // visual text bounds after QTextLayout alignment
  // Hover animation phase (0..1) for the host block, from the HoverAnimator. 0 ⇒
  // base (not hovered); drives hover-state pseudo geometry such as a widening
  // ::after underline (phycat `h1:hover::after { width:100% }`).
  qreal hoverPhase = 0.0;
  // Focus animation phase (0..1), from the FocusAnimator. Drives `:focus`-state
  // pseudo geometry (e.g. `h1:focus::after { width:100% }`), applied after the
  // hover widening so the two compose.
  qreal focusPhase = 0.0;
};

// Paint ::before/::after decorations for a block host: heading ::after underline
// bar + trailing icon, blockquote ::before content glyph, etc. Call where natural
// (typically after the block's text). No-op when the host has no pseudo rules.
void paintPseudoDecorations(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect, const PaintContext& ctx);

// True when the host declares a background gradient (lets e.g. the thematic-break
// painter swap its plain line for a gradient bar).
bool hasElementBackground(const RenderTheme& theme, const QString& host);

// Paint an `hr`-style gradient bar across `rect` (thin, vertically centred).
void paintHrGradient(QPainter& painter, const RenderTheme& theme, const QRectF& rect);

// `#write::before` full-page texture overlay: a tiled mask pattern tinted with
// the rule's background-colour at the rule's opacity. Call after the page card is
// drawn, clipped to the page rect. No-op when #write has no ::before texture.
void paintWriteTexture(QPainter& painter, const RenderTheme& theme, const QRectF& pageRect);

// Paint an SVG icon (from a `url(data:image/svg+xml,…)` declaration) into `target`.
// When `recolour` is true the SVG is treated as an alpha mask and filled with
// `tint` (a `mask:`-based icon such as phycat's link ::before, whose SVG carries
// no fill of its own); otherwise the SVG renders with its embedded fill.
void paintIcon(QPainter& painter, const QByteArray& svgData, const QRectF& target, const QColor& tint = QColor(), bool recolour = false);

// Shared used-value and primitive painting helpers for generated inline
// pseudo-elements. Keeping these here prevents the inline layout and block
// decoration paths from resolving the same CSS length differently.
qreal pseudoUsedLength(const PseudoElementRule& rule, const QString& property, qreal basis, qreal fallback, qreal zoom);
void paintPseudoIconBox(QPainter& painter, const PseudoElementRule& rule, const QRectF& box,
                        const QColor& tint, qreal zoom);

// `:hover` box-shadow glow around a block, scaled by `phase` (0..1, from the
// HoverAnimator). No-op when the host declares no hover glow. Drawn BEFORE the
// block so its content sits on top. Reuses the concentric-shell blur approach.
void paintBlockHoverGlow(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect, qreal phase);

// Low-level concentric-shell glow: `alpha` (0..1) scales the whole glow. Shared
// by the hover glow (alpha = phase) and the @keyframes glow (alpha = 1, the
// keyframe sample already carries the animated colour/blur).
void paintGlow(QPainter& painter, const QRectF& rect, const QColor& color, qreal blur, qreal alpha);

// Paint the first CSS outset box-shadow with its complete geometry. Unlike the
// deliberately subdued interactive glow above, this preserves the CSS alpha and
// spread because it is part of the theme's static box appearance.
void paintBoxShadow(QPainter& painter, const QRectF& rect, qreal borderRadius, const QColor& color, qreal offsetX, qreal offsetY,
                    qreal blur, qreal spread);

}  // namespace DecorationPainter

}  // namespace muffin
