#include "mermaid/state/StateScenePainter.h"

#include "mermaid/rough/RoughPaint.h"
#include "mermaid/scene/SvgPathParse.h"
#include "mermaid/theme/MermaidColor.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace muffin::mermaid::state {
namespace {
QColor color(const QString& value) { return mermaid::color::toQColor(value); }

// A resolved CSS paint channel. "none" (or an unparsable stroke) disables the
// pen/brush instead of degrading to black — the SVG used-value contract the
// browser applies to `fill: none` / `stroke: none`.
struct PaintSlot {
  bool none = false;
  QColor value;
  bool paints() const { return !none && value.alphaF() > 0.0; }
};
PaintSlot paintSlot(const QString& css, const QString& fallback,
                    mermaid::color::SvgPaintKind kind, const QColor& inherited) {
  const QString& source = !css.isEmpty() ? css : fallback;
  const mermaid::color::SvgPaint paint =
      mermaid::color::resolveSvgPaint(source, kind, inherited);
  return {paint.none, paint.color};
}

// Channel composite alpha for an SVG shape: color alpha × effective element
// opacity × the channel's own fill-/stroke-opacity — each factor applied
// EXACTLY ONCE (the adapter stores the per-channel factors raw; the cascade
// engine's effective{Fill,Stroke}Opacity would re-fold the element opacity
// and square it). Folding into the slot keeps the direct paint and the neo
// shadow mask on the same numbers.
QColor withChannelOpacity(const QColor& value, qreal factor) {
  QColor result = value;
  result.setAlphaF(result.alphaF() * factor);
  return result;
}
QColor withFillOpacity(const QColor& value, const StateElementCss& css) {
  return withChannelOpacity(value, css.opacity * css.fillOpacity);
}
QColor withStrokeOpacity(const QColor& value, const StateElementCss& css) {
  return withChannelOpacity(value, css.opacity * css.strokeOpacity);
}
// HTML label channels (span/p): the text color composites with the element
// opacity only — fill-opacity does not exist for HTML elements.
QColor withElementOpacity(const QColor& value, const StateElementCss& css) {
  return withChannelOpacity(value, css.opacity);
}

// A shadow channel's contribution: the channel-folded slot's alpha IS the
// filter input's composite alpha (the caller folded color alpha × element
// opacity × the channel factor into the slot).
qreal shadowChannelAlpha(const PaintSlot& slot, bool paints) {
  return paints && !slot.none ? slot.value.alphaF() : 0.0;
}
qreal combineSourceOver(qreal a, qreal b) { return a + b * (1.0 - a); }

QPainterPath edgePath(const StateSceneEdge& edge) {
  if (!edge.path.isEmpty()) return scene::parseSvgPath(edge.path);
  QPainterPath path;
  const QVector<QPointF> points = !edge.points.isEmpty() ? edge.points
      : edge.segments.isEmpty() ? QVector<QPointF>{} : edge.segments.first();
  if (points.isEmpty()) return path;
  path.moveTo(points.first());
  for (qsizetype i = 1; i < points.size(); ++i) path.lineTo(points.at(i));
  return path;
}

// QPen dash entries are multiples of the pen width; CSS stroke-dasharray is
// in user units. SVG strokes are butt-capped by default — SquareCap would
// extend every dash by half the width on both ends.
QPen dashedPen(const QColor& strokeColor, qreal width, const QString& dasharray) {
  QPen pen(strokeColor, width);
  QVector<qreal> dash;
  for (const QString& token : dasharray.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    bool ok = false;
    const qreal d = token.trimmed().toDouble(&ok);
    if (ok && d > 0.0) dash.append(d);
  }
  if (!dash.isEmpty() && width > 0.0) {
    for (qreal& entry : dash) entry /= width;
    pen.setDashPattern(dash);
    pen.setCapStyle(Qt::FlatCap);
  }
  return pen;
}

// ---- neo look drop-shadow ------------------------------------------------
//
// The common 11.16 sheet applies the theme's dropShadow variable as a
// `filter:` on `[data-look="neo"].node rect` / `.node circle` /
// `.node .outer-path` (state adds `.statediagram-cluster rect.outer`). CSS
// blur radius maps to a Gaussian with sigma = radius / 2 (Blink converts
// drop-shadow blur to feDropShadow stdDeviation the same way).
struct NeoShadow {
  bool valid = false;
  qreal dx = 0.0, dy = 0.0, sigma = 0.0;
  QColor tint;
};
NeoShadow parseNeoShadow(const QString& css) {
  NeoShadow shadow;
  const QString trimmed = css.trimmed();
  if (!trimmed.startsWith(QLatin1String("drop-shadow")))
    return shadow;  // "url(#drop-shadow)"-style themes keep flat semantics
  const int open = trimmed.indexOf(QLatin1Char('('));
  const int close = trimmed.lastIndexOf(QLatin1Char(')'));
  if (open < 0 || close <= open) return shadow;
  const QStringList parts = trimmed.mid(open + 1, close - open - 1).split(
      QLatin1Char(' '), Qt::SkipEmptyParts);
  qreal lengths[3] = {0.0, 0.0, 0.0};
  int lengthsSeen = 0;
  QString colorText;
  for (const QString& part : parts) {
    QString token = part;
    if (token.endsWith(QLatin1String("px"), Qt::CaseInsensitive)) token.chop(2);
    bool ok = false;
    const qreal value = token.toDouble(&ok);
    if (ok && lengthsSeen < 3) {
      lengths[lengthsSeen++] = value;
    } else {
      colorText += colorText.isEmpty() ? part : (QLatin1Char(' ') + part);
    }
  }
  if (lengthsSeen < 2) return shadow;
  shadow.valid = true;
  shadow.dx = lengths[0];
  shadow.dy = lengths[1];
  shadow.sigma = (lengthsSeen > 2 ? lengths[2] : 0.0) / 2.0;
  shadow.tint = colorText.isEmpty() ? QColor(Qt::black) : color(colorText);
  return shadow;
}

// Paints the element silhouette blurred by `shadow.sigma`, offset by
// (dx, dy), in `shadow.tint` — the raster equivalent of filter:
// drop-shadow(). The filter input is the element's ACTUAL rendering: each
// paint channel contributes its region at its resolved alpha (color alpha ×
// fill-/stroke-opacity × element opacity, source-over combined where the
// regions overlap); a channel that paints nothing contributes nothing, and
// when neither paints (fill:none;stroke:none, or the element is hidden)
// the filter output is empty — exactly like the browser.
struct ShadowChannels {
  qreal fillAlpha = 0.0;
  qreal strokeAlpha = 0.0;
  bool paints() const { return fillAlpha > 0.0 || strokeAlpha > 0.0; }
};
// One region contribution to the filter input: a path, its stroke width, and
// the per-channel alphas of ITS OWN rendering. A filtered GROUP (stateEnd's
// g.outer-path) is several contributions source-over combined in ONE mask —
// collapsing them into global scalars would let a visible inner dot cast a
// full ring-shaped shadow when the ring itself paints nothing.
struct ShadowPart {
  QPainterPath path;
  qreal strokeWidth = 0.0;
  ShadowChannels channels;
};
void paintNeoShadow(QPainter& painter, const QVector<ShadowPart>& parts,
                    const NeoShadow& shadow) {
  if (!shadow.valid || parts.isEmpty()) return;
  const qreal margin = 3.0 * shadow.sigma + std::abs(shadow.dx) +
                       std::abs(shadow.dy) + 2.0;
  QRectF silhouette;
  bool any = false;
  for (const ShadowPart& part : parts) {
    if (part.path.isEmpty() || !part.channels.paints()) continue;
    QRectF partBounds = part.path.boundingRect();
    if (part.channels.strokeAlpha > 0.0 && part.strokeWidth > 0.0) {
      QPainterPathStroker stroker;
      stroker.setWidth(part.strokeWidth);
      stroker.setCapStyle(Qt::FlatCap);
      stroker.setJoinStyle(Qt::MiterJoin);
      partBounds = partBounds.united(
          stroker.createStroke(part.path).boundingRect());
    }
    silhouette = any ? silhouette.united(partBounds) : partBounds;
    any = true;
  }
  if (!any || !silhouette.isValid()) return;
  const QRectF padded = silhouette.adjusted(-margin, -margin, margin, margin);
  QImage mask(qMax(1, qCeil(padded.width())), qMax(1, qCeil(padded.height())),
              QImage::Format_Grayscale8);
  mask.fill(0);
  {
    QPainter maskPainter(&mask);
    maskPainter.translate(-padded.topLeft());
    maskPainter.setPen(Qt::NoPen);
    // The mask stores SOURCE-OVER COVERAGE in the grayscale value: each
    // channel paints white at its own alpha (fill region, then stroke
    // region), so overlaps accumulate like the element's real rendering.
    for (const ShadowPart& part : parts) {
      if (part.path.isEmpty()) continue;
      if (part.channels.fillAlpha > 0.0) {
        maskPainter.setOpacity(part.channels.fillAlpha);
        maskPainter.setBrush(Qt::white);
        maskPainter.drawPath(part.path);
      }
      if (part.channels.strokeAlpha > 0.0 && part.strokeWidth > 0.0) {
        QPainterPathStroker stroker;
        stroker.setWidth(part.strokeWidth);
        stroker.setCapStyle(Qt::FlatCap);
        stroker.setJoinStyle(Qt::MiterJoin);
        const QPainterPath strokeRegion = stroker.createStroke(part.path);
        if (!strokeRegion.isEmpty()) {
          maskPainter.setOpacity(part.channels.strokeAlpha);
          maskPainter.setBrush(Qt::white);
          maskPainter.drawPath(strokeRegion);
        }
      }
    }
  }
  if (shadow.sigma > 0.0) {
    // Separable gaussian with standard deviation sigma (CSS drop-shadow
    // blur radius maps to stdDeviation = radius / 2).
    const int radius = qMax(1, qCeil(3.0 * shadow.sigma));
    QVector<qreal> kernel(2 * radius + 1, 0.0);
    for (int i = 0; i <= radius; ++i) {
      const qreal gaussian =
          std::exp(-(i * i) / (2.0 * shadow.sigma * shadow.sigma + 1e-9));
      kernel[radius - i] = kernel[radius + i] = gaussian;
    }
    const qreal sum = std::accumulate(kernel.cbegin(), kernel.cend(), 0.0);
    for (qreal& tap : kernel) tap /= sum;
    const auto blurPass = [&](bool horizontal) {
      QImage source = mask;
      const int w = mask.width(), h = mask.height();
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          qreal accumulated = 0.0;
          for (int tap = -radius; tap <= radius; ++tap) {
            const int sx = horizontal ? qBound(0, x + tap, w - 1) : x;
            const int sy = horizontal ? y : qBound(0, y + tap, h - 1);
            accumulated += source.scanLine(sy)[sx] * kernel.at(tap + radius);
          }
          mask.scanLine(y)[x] = static_cast<unsigned char>(
              qBound(0, qRound(accumulated), 255));
        }
      }
    };
    blurPass(true);
    blurPass(false);
  }
  QImage layer(mask.size(), QImage::Format_ARGB32_Premultiplied);
  layer.fill(Qt::transparent);
  const QColor tint = shadow.tint.isValid() ? shadow.tint : QColor(Qt::black);
  for (int y = 0; y < mask.height(); ++y) {
    const unsigned char* row = mask.scanLine(y);
    QRgb* target = reinterpret_cast<QRgb*>(layer.scanLine(y));
    for (int x = 0; x < mask.width(); ++x) {
      // Coverage × the tint's own alpha (e.g. redux-dark's flat white
      // feDropShadow carries flood-opacity 0.06). qPremultiply applies the
      // final alpha to the channels — keep RGB at the full tint color so
      // soft edge pixels stay the shadow color (not alpha-squared).
      const int alpha = tint.alpha() * row[x] / 255;
      target[x] = qPremultiply(qRgba(tint.red(), tint.green(), tint.blue(),
                                     alpha));
    }
  }
  painter.drawImage(padded.translated(shadow.dx, shadow.dy).topLeft(), layer);
}

void paintLabel(QPainter& painter, const flowchart::FlowLabelDocument& document,
                const QRectF& bounds, const QString& family, qreal size,
                qreal lineHeight, const QColor& textColor, bool clip = true) {
  if (document.text.isEmpty()) return;
  painter.save();
  if (clip) painter.setClipRect(bounds);
  flowchart::paintFlowLabel(painter, document, bounds, family, size, lineHeight,
                            textColor, true);
  painter.restore();
}

// rectWithTitle text stack (upstream shape contract, browser-verified): the
// label group starts 3px below the rect top, the title leads in its own
// line box, the divider line sits at titleHeight + padding/2, and the
// description rows follow in ONE foreignObject (line-height 1.5 line boxes)
// at titleHeight + padding/2 + 5 — no per-line clipping. The description
// foreignObject is its own element: a display:none / visibility:hidden rule
// skips the rows while the title keeps painting.
void paintNodeLabel(QPainter& painter, const StateSceneNode& node,
                    const StateSceneStyle& style, const QColor& textColor,
                    const QString& family, qreal size,
                    bool descriptionPaints = true,
                    const QString& descFamily = QString(),
                    qreal descSize = 0.0, const QColor& descColor = QColor()) {
  if (node.label.isEmpty()) return;
  const qreal lineHeight = size * 1.5;
  if (node.descriptions.isEmpty()) {
    paintLabel(painter, node.labelDocument, node.bounds, family, size, lineHeight,
               textColor);
    return;
  }
  const qreal top = node.bounds.top() + 3.0;
  QRectF title(node.bounds.left(), top, node.bounds.width(),
               node.titleHeight > 0.0 ? node.titleHeight : lineHeight);
  paintLabel(painter, node.labelDocument, title, family, size, lineHeight, textColor);
  if (!descriptionPaints) return;
  // The description rows are their OWN <p> (the second fo): their font/color
  // are their own computed channels (falling back to the title's p when the
  // slot carries nothing).
  const QString rowsFamily = !descFamily.isEmpty() ? descFamily : family;
  const qreal rowsSize = descSize > 0.0 ? descSize : size;
  const qreal rowsLineHeight = rowsSize * 1.5;
  qreal y = top + node.titleHeight + 9.0;
  for (const auto& document : node.descriptionDocuments) {
    QRectF line(node.bounds.left(), y, node.bounds.width(), rowsLineHeight);
    paintLabel(painter, document, line, rowsFamily, rowsSize, rowsLineHeight,
               descColor.isValid() ? descColor : textColor, false);
    y += rowsLineHeight;
  }
}

// The upstream marker is a concave barb, not a solid triangle:
//   classic `M 19,7 L9,13 L14,7 L9,1 Z` — refX 19 (tip AT the line end)
//   neo     `M 19,7 L11,14 L13,7 L11,0 Z` — the -margin clone edges reference
//           has refX 17, so the tip extends 2px past the end (a deliberate gap
//           for the stroke to show).
// Both markers fill AND stroke with the GLOBAL transitionColor
// (`defs [id$="-barbEnd"]`, stroke-width 1) — not the per-edge stroke, so
// themeCSS restyling one transition never recolors the arrowheads.
void paintArrow(QPainter& painter, const StateSceneEdge& edge,
                const StateSceneStyle& style, const StateElementCss& markerCss,
                const PaintSlot& fill, const PaintSlot& stroke) {
  if (edge.markerEnd.isEmpty() || edge.markerEnd == QLatin1String("none") ||
      edge.points.size() < 2) return;
  if (!edge.shapeCss.displayed || !edge.shapeCss.painted) return;
  // The marker content itself is themeCSS-addressable (`defs marker path`):
  // display:none / visibility:hidden remove the arrowhead, opacity and the
  // per-channel fill-/stroke-opacity scale it, and a declared stroke-width
  // replaces the 1px default.
  if (!markerCss.displayed || !markerCss.painted) return;
  const QPointF end = edge.points.last();
  const QPointF before = edge.points.at(edge.points.size() - 2);
  const qreal angle = std::atan2(end.y() - before.y(), end.x() - before.x());
  const qreal c = std::cos(angle);
  const qreal s = std::sin(angle);
  const bool neo = style.neo;
  // Path points relative to the (refX, 7) anchor; neo anchors at x=17. The
  // second wing mirrors ONLY the vertical component (classic upper wing
  // (9,1) = tip + (-10,-6), not tip - wingBack which would give (29,1)).
  const QPointF anchor = neo ? QPointF(17.0, 7.0) : QPointF(19.0, 7.0);
  const QPointF wingBack = neo ? QPointF(-8.0, 7.0) : QPointF(-10.0, 6.0);
  const QPointF wingUp = neo ? QPointF(-8.0, -7.0) : QPointF(-10.0, -6.0);
  const QPointF notch = neo ? QPointF(-6.0, 0.0) : QPointF(-5.0, 0.0);
  const auto place = [&](const QPointF& local) {
    const QPointF p = local - anchor;
    return QPointF(end.x() + p.x() * c - p.y() * s,
                   end.y() + p.x() * s + p.y() * c);
  };
  const QPointF tip(19.0, 7.0);
  QPolygonF barb{place(tip), place(tip + wingBack), place(tip + notch),
                 place(tip + wingUp)};
  // Each paint channel resolves independently: `fill:green;stroke:none`
  // paints the barb with no outline. The marker's own css (not the edge's)
  // carries the channel factors — color alpha × effective opacity × the
  // channel's own factor, once each.
  QColor fillColor = fill.value;
  fillColor.setAlphaF(fillColor.alphaF() * markerCss.opacity * markerCss.fillOpacity);
  QColor strokeColor = stroke.value;
  strokeColor.setAlphaF(strokeColor.alphaF() * markerCss.opacity * markerCss.strokeOpacity);
  const qreal markerStrokeWidth =
      markerCss.strokeWidthSet ? markerCss.strokeWidthPx : 1.0;
  painter.setPen(stroke.none || markerStrokeWidth <= 0.0
                     ? QPen(Qt::NoPen) : QPen(strokeColor, markerStrokeWidth));
  painter.setBrush(fill.none ? QBrush(Qt::NoBrush) : QBrush(fillColor));
  painter.drawPolygon(barb);
}

// Rough-drawn shapes keep a fill path AND a separate stroke path (rough.js
// rectangle/polygon output); each carries its own presentation attrs, so
// themeCSS resolves them as independent elements — including per-path
// display/visibility.
struct RoughPaint {
  PaintSlot fill;
  PaintSlot stroke;
  qreal strokeWidth;
};
void paintRoughShape(QPainter& painter, const RoughPaint& paint, qreal strokeWidth,
                     const QPainterPath& path, bool fillPaints,
                     bool strokePaints) {
  painter.setPen(!strokePaints || paint.stroke.none || strokeWidth <= 0.0
                     ? Qt::NoPen : QPen(paint.stroke.value, strokeWidth));
  painter.setBrush(!fillPaints || paint.fill.none ? QBrush(Qt::NoBrush)
                                                  : QBrush(paint.fill.value));
  if (!path.isEmpty()) painter.drawPath(path);
}
QPainterPath roughNodePath(const StateSceneNode& node) {
  if (node.shape == QLatin1String("choice")) {
    const QPointF center = node.bounds.center();
    QPolygonF diamond{QPointF(center.x(), node.bounds.top()),
        QPointF(node.bounds.right(), center.y()),
        QPointF(center.x(), node.bounds.bottom()),
        QPointF(node.bounds.left(), center.y())};
    QPainterPath path;
    path.addPolygon(diamond);
    path.closeSubpath();
    return path;
  }
  QPainterPath path;
  path.addRect(node.bounds);
  return path;
}
}

void paintStateScene(const StateScene& scene, QPainter& painter,
                     const MermaidPaintOptions& options) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const QColor inherited = color(scene.style.textColor);
  const NeoShadow neoShadow = parseNeoShadow(scene.style.shadowCss);
  const auto shapeShadow = [&](const QPainterPath& path, qreal strokeWidth,
                               const ShadowChannels& channels) {
    if (scene.style.neo)
      paintNeoShadow(painter, {{path, strokeWidth, channels}}, neoShadow);
  };
  // The raster arrowhead takes the marker defs' global color slots.
  const PaintSlot markerFill = paintSlot(scene.markerCss.fill,
      scene.style.transitionColor, mermaid::color::SvgPaintKind::Fill, inherited);
  const PaintSlot markerStroke = paintSlot(scene.markerCss.stroke,
      scene.style.transitionColor, mermaid::color::SvgPaintKind::Stroke, inherited);
  for (const StateSceneNode& cluster : scene.clusters) {
    if (!mermaidPrimitiveIsVisible(
            scene.handDrawn && cluster.paintedBounds.isValid()
                ? cluster.paintedBounds : cluster.bounds,
            options)) continue;
    if (cluster.shape == QLatin1String("noteGroup")) continue;
    RoughPaint clusterPaint;
    clusterPaint.fill = paintSlot(cluster.shapeCss.fill, cluster.fill,
        mermaid::color::SvgPaintKind::Fill, inherited);
    clusterPaint.stroke = paintSlot(cluster.shapeCss.stroke, cluster.stroke,
        mermaid::color::SvgPaintKind::Stroke, inherited);
    // A DECLARED stroke-width of 0 disables the pen (it must not fall back to
    // the theme width) — strokeWidthSet separates that from "not declared".
    clusterPaint.strokeWidth = cluster.shapeCss.strokeWidthSet
        ? cluster.shapeCss.strokeWidthPx : cluster.strokeWidth;
    clusterPaint.fill.value = withFillOpacity(clusterPaint.fill.value, cluster.shapeCss);
    clusterPaint.stroke.value = withStrokeOpacity(clusterPaint.stroke.value, cluster.shapeCss);
    if (cluster.shape == QLatin1String("divider")) {
      // `--` partition: one square dashed rect (10/10, butt caps) — a single
      // DOM element, so its own display/visibility gate the whole partition.
      if (!cluster.shapeCss.displayed || !cluster.shapeCss.painted) continue;
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : cluster.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable,
              clusterPaint.fill.none ? QBrush(Qt::NoBrush)
                                     : QBrush(clusterPaint.fill.value),
              clusterPaint.stroke.none || clusterPaint.strokeWidth <= 0.0
                  ? QPen(Qt::NoPen)
                  : dashedPen(clusterPaint.stroke.value, clusterPaint.strokeWidth,
                              QStringLiteral("5,5")),
              clusterPaint.fill.none ? QPen(Qt::NoPen)
                                     : QPen(clusterPaint.fill.value,
                                            clusterPaint.strokeWidth));
      } else {
        painter.setPen(clusterPaint.stroke.none || clusterPaint.strokeWidth <= 0.0
                           ? QPen(Qt::NoPen)
                           : dashedPen(clusterPaint.stroke.value,
                                       clusterPaint.strokeWidth,
                                       QStringLiteral("10,10")));
        painter.setBrush(clusterPaint.fill.none ? QBrush(Qt::NoBrush)
                                                : QBrush(clusterPaint.fill.value));
        painter.drawRect(cluster.bounds);
      }
      continue;
    }
    // Composite cluster = THREE sibling elements: rect.outer, the
    // cluster-label span, and rect.inner. Each hides independently —
    // `rect.outer { display:none }` removes only the outer frame; the title
    // and the inner body keep painting exactly like the browser.
    const bool outerPaints = cluster.shapeCss.displayed && cluster.shapeCss.painted;
    const qreal radius = scene.style.neo && scene.style.radius > 0.0
        ? scene.style.radius : 5.0;
    // rect.inner is ONE element carrying both its fill and its stroke — the
    // innerCss slot (the node-level innerStrokeCss pair has no cluster
    // counterpart). Both channels compose their own opacity factors.
    const PaintSlot innerFill = paintSlot(cluster.innerCss.fill, cluster.innerFill,
        mermaid::color::SvgPaintKind::Fill, inherited);
    const PaintSlot innerStroke = paintSlot(cluster.innerCss.stroke,
        cluster.stroke,
        mermaid::color::SvgPaintKind::Stroke, inherited);
    const qreal innerStrokeWidth = cluster.innerCss.strokeWidthSet
        ? cluster.innerCss.strokeWidthPx : cluster.strokeWidth;
    const QColor innerFillColor = withFillOpacity(innerFill.value, cluster.innerCss);
    const QColor innerStrokeColor = withStrokeOpacity(innerStroke.value,
                                                      cluster.innerCss);
    const bool innerPaints = cluster.innerBounds.isValid() &&
        cluster.innerCss.displayed && cluster.innerCss.painted;
    if (scene.handDrawn) {
      for (qsizetype i = 0; i < cluster.roughDrawables.size(); ++i) {
        const rough::Drawable& drawable = cluster.roughDrawables.at(i);
        // Drawable 0 is the outer rect, 1 the inner body: each is its own
        // DOM element with its own display/visibility/paint channels.
        if (i == 0 && !outerPaints) continue;
        if (i == 1 && !innerPaints) continue;
        const bool outer = i == 0;
        const QColor fill = outer ? clusterPaint.fill.value
                                  : (innerFill.none ? QColor() : innerFillColor);
        const QColor stroke = outer
            ? clusterPaint.stroke.value
            : (innerStroke.none ? QColor() : innerStrokeColor);
        const qreal width = outer ? clusterPaint.strokeWidth : innerStrokeWidth;
        rough::drawRoughDrawable(
            painter, drawable, fill,
            (outer ? clusterPaint.stroke.none : innerStroke.none) || width <= 0.0
                ? QPen(Qt::NoPen) : QPen(stroke, width),
            (outer ? clusterPaint.fill.none : innerFill.none)
                ? QPen(Qt::NoPen)
                : QPen(fill, drawable.options.fillWeight >= 0.0
                               ? drawable.options.fillWeight : width));
      }
    } else {
      if (outerPaints) {
        painter.setPen(clusterPaint.stroke.none || clusterPaint.strokeWidth <= 0.0
                           ? Qt::NoPen
                           : QPen(clusterPaint.stroke.value, clusterPaint.strokeWidth));
        painter.setBrush(clusterPaint.fill.none ? QBrush(Qt::NoBrush)
                                                : QBrush(clusterPaint.fill.value));
        QPainterPath outerPath;
        outerPath.addRoundedRect(cluster.bounds, radius, radius);
        // `[data-look="neo"].statediagram-cluster rect.outer { filter: ... }`
        // The filter input is the rect's own rendering: hidden element or
        // none-paint channels leave no shadow.
        if (scene.style.neo) {
          ShadowChannels channels;
          channels.fillAlpha = shadowChannelAlpha(clusterPaint.fill, true);
          channels.strokeAlpha = shadowChannelAlpha(clusterPaint.stroke, true);
          shapeShadow(outerPath, clusterPaint.strokeWidth, channels);
        }
        painter.drawPath(outerPath);
      }
      if (cluster.innerBounds.isValid()) {
        // display:none / visibility:hidden hide the inner rect ENTIRELY —
        // the brush must not keep painting the body when only the pen was
        // gated (the browser treats rect.inner as one element).
        painter.setBrush(!innerPaints || innerFill.none
                             ? QBrush(Qt::NoBrush) : QBrush(innerFillColor));
        painter.setPen(!innerPaints || innerStroke.none ||
                               innerStrokeWidth <= 0.0
                           ? Qt::NoPen
                           : QPen(innerStrokeColor, innerStrokeWidth));
        painter.drawRect(cluster.innerBounds);
      }
    }
    if (!cluster.label.isEmpty()) {
      // Composite title: the label group is translate(.., y+1) upstream — a
      // line box of the measured title height just below the top edge. The
      // text lives in the <p>: its computed font/color (folding the span)
      // is the used style, and the p's display/visibility hide the text.
      const QString family = !cluster.labelTextCss.fontFamily.isEmpty()
          ? cluster.labelTextCss.fontFamily
          : (!cluster.labelCss.fontFamily.isEmpty() ? cluster.labelCss.fontFamily
                                                    : scene.style.fontFamily);
      const qreal size = cluster.labelTextCss.fontSize > 0.0
          ? cluster.labelTextCss.fontSize
          : (cluster.labelCss.fontSize > 0.0 ? cluster.labelCss.fontSize
                                             : scene.style.fontSize);
      QRectF title = cluster.bounds.adjusted(8.0, 1.0, -8.0, 0.0);
      title.setHeight(cluster.titleHeight > 0.0
                          ? cluster.titleHeight : scene.style.lineHeight);
      if (cluster.labelCss.displayed && cluster.labelCss.painted &&
          cluster.labelTextCss.displayed && cluster.labelTextCss.painted)
        paintLabel(painter, cluster.labelDocument, title, family, size,
                   size * 1.5,
                   withElementOpacity(
                       paintSlot(cluster.labelTextCss.color.isEmpty()
                                     ? cluster.labelCss.color
                                     : cluster.labelTextCss.color,
                                 cluster.textColor,
                                 mermaid::color::SvgPaintKind::Text, inherited).value,
                       cluster.labelTextCss));
    }
  }
  for (const StateSceneEdge& edge : scene.edges) {
    if (!mermaidPrimitiveIsVisible(
            edge.pathBounds.isValid() ? edge.pathBounds : scene.bounds,
            options))
      continue;
    if (!edge.shapeCss.displayed || !edge.shapeCss.painted) continue;
    // Edge paint: state transitions carry no linkStyle/classDef channel, so
    // the slots are theme values refined by per-element themeCSS.
    PaintSlot edgePaint = paintSlot(edge.shapeCss.stroke,
        edge.stroke.isEmpty() ? scene.style.transitionColor : edge.stroke,
        mermaid::color::SvgPaintKind::Stroke, inherited);
    edgePaint.value = withStrokeOpacity(edgePaint.value, edge.shapeCss);
    qreal edgeWidth = scene.style.strokeWidth;
    // A DECLARED stroke-width of 0 removes the line (QPen with width 0 would
    // draw a cosmetic hairline) — strokeWidthSet separates declared-0 from
    // not-declared.
    if (edge.shapeCss.strokeWidthSet) edgeWidth = edge.shapeCss.strokeWidthPx;
    else if (!edge.strokeWidth.isEmpty()) {
      QString widthToken = edge.strokeWidth;
      if (widthToken.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) widthToken.chop(2);
      bool ok = false;
      const qreal resolved = widthToken.trimmed().toDouble(&ok);
      if (ok && resolved >= 0.0) edgeWidth = resolved;
    }
    // `stroke: none` (or a declared width of 0) removes the line entirely —
    // QPen with an invalid color or width 0 would draw a black cosmetic
    // hairline instead. The note connector's 5,5 dasharray applies to BOTH
    // renderers (`.note-edge { stroke-dasharray: 5 }` styles the rough line
    // path exactly like the smooth one).
    const QPen edgePen = edgePaint.none || edgeWidth <= 0.0
        ? QPen(Qt::NoPen)
        : (edge.strokeDasharray.isEmpty()
               ? QPen(edgePaint.value, edgeWidth)
               : dashedPen(edgePaint.value, edgeWidth, edge.strokeDasharray));
    painter.setPen(edgePen);
    painter.setBrush(Qt::NoBrush);
    if (scene.handDrawn)
      rough::drawRoughDrawable(painter, edge.roughDrawable, Qt::NoBrush,
                               edgePen, Qt::NoPen);
    else
      painter.drawPath(edgePath(edge));
    if (options.paintEdgeMarkers)
      paintArrow(painter, edge, scene.style, scene.markerCss, markerFill,
                 markerStroke);
  }
  for (const StateSceneEdge& edge : scene.edges) {
    if (edge.label.isEmpty() || !edge.labelPosition) continue;
    // The label <p> hides the WHOLE label (span > p > text) — its own
    // display/visibility gate rides on the p slot, not the span's.
    if (!edge.labelCss.displayed || !edge.labelCss.painted) continue;
    if (!edge.labelBackgroundCss.displayed || !edge.labelBackgroundCss.painted)
      continue;
    const QSizeF size = edge.labelSize.isValid()
        ? edge.labelSize
        : flowchart::measureFlowLabel(edge.labelDocument,
              scene.style.fontFamily, scene.style.fontSize,
              scene.style.lineHeight);
    const QRectF bounds(*edge.labelPosition - QPointF(size.width() / 2.0,
                                                       size.height() / 2.0), size);
    if (!mermaidPrimitiveIsVisible(
            edge.labelBounds.isValid() ? edge.labelBounds : bounds, options))
      continue;
    // `.edgeLabel p { background-color: edgeLabelBackground }` — the html
    // label paints its background ONLY on the <p> (the span/div backgrounds
    // are computed-only upstream). `transparent` (the CSS initial, e.g.
    // themeCSS `background-color: transparent`) or `none` paints nothing.
    const PaintSlot background = paintSlot(QString(),
        edge.labelBackground.isEmpty() ? scene.style.edgeLabelBackground
                                       : edge.labelBackground,
        mermaid::color::SvgPaintKind::Fill, inherited);
    if (background.paints()) {
      QColor labelBackground = background.value;
      // The background is the <p>'s — its effective opacity (which folds the
      // span and the whole ancestor chain) is the p slot's.
      labelBackground.setAlphaF(
          labelBackground.alphaF() * edge.labelBackgroundCss.opacity);
      painter.fillRect(bounds, labelBackground);
    }
    // The text glyphs live INSIDE the <p>: their used font/color/size are
    // the p's computed values (which fold the span chain by inheritance).
    const QString family = !edge.labelBackgroundCss.fontFamily.isEmpty()
        ? edge.labelBackgroundCss.fontFamily
        : (!edge.labelCss.fontFamily.isEmpty() ? edge.labelCss.fontFamily
                                               : scene.style.fontFamily);
    const qreal size2 = edge.labelBackgroundCss.fontSize > 0.0
        ? edge.labelBackgroundCss.fontSize
        : (edge.labelCss.fontSize > 0.0 ? edge.labelCss.fontSize
                                        : scene.style.fontSize);
    paintLabel(painter, edge.labelDocument, bounds, family, size2,
               size2 * 1.5,
               withElementOpacity(
                   paintSlot(!edge.labelBackgroundCss.color.isEmpty()
                                 ? edge.labelBackgroundCss.color
                                 : edge.labelCss.color,
                             scene.style.transitionLabelColor.isEmpty()
                                 ? scene.style.textColor
                                 : scene.style.transitionLabelColor,
                             mermaid::color::SvgPaintKind::Text, inherited).value,
                   // Their used opacity is likewise the p's effective value.
                   edge.labelBackgroundCss));
  }
  for (const StateSceneNode& node : scene.nodes) {
    if (!mermaidPrimitiveIsVisible(node.bounds, options)) continue;
    const QString shape = node.shape;
    const QPointF center = node.bounds.center();
    // Rough option defaults per shape (userNodeOverrides): note/choice take
    // mainBkg/nodeBorder (the scene's stateFill/stateStroke slots), fork and
    // the stateEnd ring take lineColor (= transitionColor), the end inner
    // dot `stateBorder ?? nodeBorder`. A user `style` overrides these via
    // applyStateNodeStyles (node.fill/node.stroke).
    const bool lineColorShape = shape == QLatin1String("fork") ||
        shape == QLatin1String("join") || shape == QLatin1String("stateEnd");
    // Rough shapes carry TWO elements (fill path + stroke path); rect /
    // circle shapes are single elements whose stroke resolves on the same
    // element as the fill. Under handDrawn, plain rect and rectWithTitle
    // also become path pairs; the titled exception keeps class `node`.
    const bool roughShape = shape == QLatin1String("note") ||
        shape == QLatin1String("choice") || shape == QLatin1String("fork") ||
        shape == QLatin1String("join") || shape == QLatin1String("stateEnd") ||
        (scene.handDrawn && (shape == QLatin1String("rect") ||
                             shape == QLatin1String("rectWithTitle")));
    const StateElementCss& strokeElement = roughShape ? node.shapeStrokeCss
                                                      : node.shapeCss;
    RoughPaint nodePaint;
    nodePaint.fill = paintSlot(node.shapeCss.fill,
        shape == QLatin1String("stateStart")
            ? (scene.style.specialStateColor.isEmpty()
                   ? scene.style.transitionColor : scene.style.specialStateColor)
            : node.fill,
        mermaid::color::SvgPaintKind::Fill, inherited);
    // Under the handDrawn look every rough pair's fill path paints the
    // HACHURE through its STROKE (fill:none;stroke:<fill>;width 4 — the
    // rough pair contract), so the scene's fill channel resolves the fill
    // path's computed stroke, exactly what the browser renders — including
    // its STROKE-opacity channel.
    const bool hachureStroke = scene.handDrawn && roughShape;
    if (hachureStroke)
      nodePaint.fill = paintSlot(node.shapeCss.stroke, node.fill,
                                 mermaid::color::SvgPaintKind::Stroke, inherited);
    nodePaint.stroke = paintSlot(strokeElement.stroke,
        lineColorShape ? scene.style.transitionColor : node.stroke,
        mermaid::color::SvgPaintKind::Stroke, inherited);
    nodePaint.fill.value = hachureStroke
        ? withStrokeOpacity(nodePaint.fill.value, node.shapeCss)
        : withFillOpacity(nodePaint.fill.value, node.shapeCss);
    nodePaint.stroke.value = withStrokeOpacity(nodePaint.stroke.value,
                                               strokeElement);
    // A declared `stroke-width: 0` disables the pen — only an UNSET width
    // keeps the shape's theme/user default.
    const qreal nodeWidth = strokeElement.strokeWidthSet
        ? strokeElement.strokeWidthPx : node.strokeWidth;
    // The label text lives INSIDE the <p>: its computed font/color (folding
    // the span chain) is the used style; the p's display/visibility hide the
    // text while the frame keeps painting.
    const QString family = !node.labelTextCss.fontFamily.isEmpty()
        ? node.labelTextCss.fontFamily
        : (!node.labelCss.fontFamily.isEmpty() ? node.labelCss.fontFamily
                                               : scene.style.fontFamily);
    const qreal labelSize = node.labelTextCss.fontSize > 0.0
        ? node.labelTextCss.fontSize
        : (node.labelCss.fontSize > 0.0 ? node.labelCss.fontSize
                                        : scene.style.fontSize);
    const QColor textColour = withElementOpacity(
        paintSlot(node.labelTextCss.color.isEmpty() ? node.labelCss.color
                                                    : node.labelTextCss.color,
                  node.textColor,
                  mermaid::color::SvgPaintKind::Text, inherited).value,
        node.labelTextCss);
    // The description rows' own <p>: font/color/size and gates of their own.
    const QString descFamily = !node.descriptionTextCss.fontFamily.isEmpty()
        ? node.descriptionTextCss.fontFamily : family;
    const qreal descSize = node.descriptionTextCss.fontSize > 0.0
        ? node.descriptionTextCss.fontSize : labelSize;
    const QColor descColor = withElementOpacity(
        paintSlot(node.descriptionTextCss.color, node.textColor,
                  mermaid::color::SvgPaintKind::Text, inherited).value,
        node.descriptionTextCss);
    const bool labelPaints = node.labelCss.displayed && node.labelCss.painted &&
        node.labelTextCss.displayed && node.labelTextCss.painted;
    const bool descriptionPaints = node.descriptionCss.displayed &&
        node.descriptionCss.painted && node.descriptionTextCss.displayed &&
        node.descriptionTextCss.painted;
    // `.node rect { display:none }`: rect-bearing shapes (plain rect,
    // rectWithTitle, note) lose their box; the label still paints.
    const bool rectShape = shape.isEmpty() || shape == QLatin1String("rect") ||
        shape == QLatin1String("rectWithTitle") || shape == QLatin1String("note");
    const bool shapeHidden = (!node.shapeVisible || !node.shapeCss.displayed ||
                              !node.shapeCss.painted) && rectShape;
    // Rough shapes hide per path element (fill path vs stroke path), and
    // the start circle / end ring carry their own display/visibility.
    const bool fillPaints = node.shapeCss.displayed && node.shapeCss.painted;
    const bool strokePaints = strokeElement.displayed && strokeElement.painted;
    if (shapeHidden && shape != QLatin1String("rectWithTitle") &&
        shape != QLatin1String("note")) {
      // plain rect: label-only node
      if (labelPaints)
        paintNodeLabel(painter, node, scene.style, textColour, family, labelSize,
                       descriptionPaints);
      continue;
    }
    // 11.16 rendering-util shapes: `.node circle.state-start` paints
    // fill+stroke specialStateColor; stateEnd is a rough pair whose ring takes
    // the node fill + lineColor stroke and whose inner dot is fill+stroke
    // `stateBorder ?? nodeBorder`.
    if (shape == QLatin1String("stateStart")) {
      if (!fillPaints && !strokePaints) continue;  // display:none circle: 0x0
      const PaintSlot startFill = nodePaint.fill;
      PaintSlot startStroke = paintSlot(node.shapeCss.stroke,
          scene.style.specialStateColor.isEmpty()
              ? scene.style.transitionColor : scene.style.specialStateColor,
          mermaid::color::SvgPaintKind::Stroke, inherited);
      startStroke.value = withStrokeOpacity(startStroke.value, node.shapeCss);
      const qreal circleWidth = strokeElement.strokeWidthSet
          ? strokeElement.strokeWidthPx : 1.0;
      const QPen startPen = !strokePaints || startStroke.none ||
                                    circleWidth <= 0.0
                                ? QPen(Qt::NoPen)
                                : QPen(startStroke.value, circleWidth);
      if (scene.handDrawn) {
        // The hachure fill path strokes at the drawable's fillWeight (4px
        // for nodeOptions), not the outline width.
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable,
              !fillPaints || startFill.none ? QBrush(Qt::NoBrush)
                                            : QBrush(startFill.value),
              startPen,
              !fillPaints || startFill.none
                  ? QPen(Qt::NoPen)
                  : QPen(startFill.value,
                         drawable.options.fillWeight >= 0.0
                             ? drawable.options.fillWeight : circleWidth));
      } else {
        QPainterPath circlePath;
        circlePath.addEllipse(center, 7.0, 7.0);
        ShadowChannels channels;
        channels.fillAlpha = shadowChannelAlpha(startFill, fillPaints);
        channels.strokeAlpha = shadowChannelAlpha(startStroke, strokePaints);
        shapeShadow(circlePath, circleWidth, channels);
        painter.setPen(startPen);
        painter.setBrush(!fillPaints || startFill.none ? QBrush(Qt::NoBrush)
                                                       : QBrush(startFill.value));
        painter.drawEllipse(center, 7.0, 7.0);
      }
      continue;
    }
    if (shape == QLatin1String("stateEnd")) {
      // Each rc.circle pair path is its own element: the ring (fill+stroke
      // paths) and the inner dot hide independently under themeCSS.
      const RoughPaint& outer = nodePaint;
      const qreal outerWidth = strokeElement.strokeWidthSet
          ? strokeElement.strokeWidthPx : 2.0;
      PaintSlot innerFill = paintSlot(node.innerCss.fill,
          scene.style.endInnerFill.isEmpty()
              ? scene.style.transitionColor : scene.style.endInnerFill,
          mermaid::color::SvgPaintKind::Fill, inherited);
      PaintSlot innerStroke = paintSlot(node.innerStrokeCss.stroke,
          scene.style.endInnerFill.isEmpty()
              ? scene.style.transitionColor : scene.style.endInnerFill,
          mermaid::color::SvgPaintKind::Stroke, inherited);
      innerFill.value = withFillOpacity(innerFill.value, node.innerCss);
      innerStroke.value = withStrokeOpacity(innerStroke.value,
                                            node.innerStrokeCss);
      const bool innerPaints = node.innerCss.displayed && node.innerCss.painted;
      const bool innerStrokePaints = node.innerStrokeCss.displayed &&
          node.innerStrokeCss.painted;
      const qreal innerWidth = node.innerStrokeCss.strokeWidthSet
          ? node.innerStrokeCss.strokeWidthPx : 2.0;
      if (scene.handDrawn) {
        // Two drawables (ring ellipse, dot ellipse), each carrying a fill
        // and a stroke opset — the DOM's fill path and stroke path pair per
        // circle. Each channel gates independently per element.
        for (qsizetype i = 0; i < node.roughDrawables.size(); ++i) {
          const bool ring = i == 0;
          const PaintSlot& fillSlot = ring ? outer.fill : innerFill;
          const PaintSlot& strokeSlot = ring ? outer.stroke : innerStroke;
          const bool fillGates = ring ? fillPaints : innerPaints;
          const bool strokeGates = ring ? strokePaints : innerStrokePaints;
          const qreal width = ring ? outerWidth : innerWidth;
          rough::drawRoughDrawable(
              painter, node.roughDrawables.at(i),
              !fillGates || fillSlot.none ? QBrush(Qt::NoBrush)
                                          : QBrush(fillSlot.value),
              !strokeGates || strokeSlot.none || width <= 0.0
                  ? QPen(Qt::NoPen) : QPen(strokeSlot.value, width),
              !fillGates || fillSlot.none ? QPen(Qt::NoPen)
                                          : QPen(fillSlot.value,
                                                 node.roughDrawables.at(i)
                                                         .options.fillWeight >= 0.0
                                                     ? node.roughDrawables.at(i)
                                                           .options.fillWeight
                                                     : width));
        }
      } else {
        QPainterPath ringPath;
        ringPath.addEllipse(center, 7.0, 7.0);
        QPainterPath dotPath;
        dotPath.addEllipse(center, 2.5, 2.5);
        // The g.outer-path filter input is the GROUP's actual rendering: the
        // ring contributes ONLY its own regions (ring fill disk + stroke
        // band), the dot only its disk — a transparent ring with a visible
        // dot casts a DOT shadow in the browser, not a full ring silhouette.
        ShadowChannels ringChannels;
        ringChannels.fillAlpha = shadowChannelAlpha(outer.fill, fillPaints);
        ringChannels.strokeAlpha = shadowChannelAlpha(outer.stroke, strokePaints);
        ShadowChannels dotChannels;
        dotChannels.fillAlpha = shadowChannelAlpha(innerFill, innerPaints);
        dotChannels.strokeAlpha = shadowChannelAlpha(innerStroke,
                                                     innerStrokePaints);
        if (scene.style.neo)
          paintNeoShadow(painter,
                         {{ringPath, outerWidth, ringChannels},
                          {dotPath, innerWidth, dotChannels}},
                         neoShadow);
        painter.setPen(!strokePaints || outer.stroke.none || outerWidth <= 0.0
                           ? Qt::NoPen : QPen(outer.stroke.value, outerWidth));
        painter.setBrush(!fillPaints || outer.fill.none ? QBrush(Qt::NoBrush)
                                                        : QBrush(outer.fill.value));
        painter.drawEllipse(center, 7.0, 7.0);
        painter.setPen(!innerStrokePaints || innerStroke.none ||
                               innerWidth <= 0.0
                           ? Qt::NoPen : QPen(innerStroke.value, innerWidth));
        painter.setBrush(!innerPaints || innerFill.none ? QBrush(Qt::NoBrush)
                                                        : QBrush(innerFill.value));
        painter.drawEllipse(center, 2.5, 2.5);
      }
      continue;
    }
    if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable,
              !fillPaints || nodePaint.fill.none ? QBrush(Qt::NoBrush)
                                                 : QBrush(nodePaint.fill.value),
              !strokePaints || nodePaint.stroke.none || nodeWidth <= 0.0
                  ? QPen(Qt::NoPen)
                  : QPen(nodePaint.stroke.value, nodeWidth),
              !fillPaints || nodePaint.fill.none ? QPen(Qt::NoPen)
                                                 : QPen(nodePaint.fill.value,
                                                        drawable.options.fillWeight >= 0.0
                                                            ? drawable.options.fillWeight
                                                            : nodeWidth));
      } else {
        QPainterPath forkPath;
        forkPath.addRect(node.bounds);
        paintRoughShape(painter, nodePaint, nodeWidth, forkPath,
                        fillPaints, strokePaints);
      }
      continue;
    }
    if (shape == QLatin1String("choice")) {
      const QPainterPath diamond = roughNodePath(node);
      if (scene.handDrawn) {
        for (const rough::Drawable& drawable : node.roughDrawables)
          rough::drawRoughDrawable(
              painter, drawable,
              !fillPaints || nodePaint.fill.none ? QBrush(Qt::NoBrush)
                                                 : QBrush(nodePaint.fill.value),
              !strokePaints || nodePaint.stroke.none || nodeWidth <= 0.0
                  ? QPen(Qt::NoPen)
                  : QPen(nodePaint.stroke.value, nodeWidth),
              !fillPaints || nodePaint.fill.none ? QPen(Qt::NoPen)
                                                 : QPen(nodePaint.fill.value,
                                                        drawable.options.fillWeight >= 0.0
                                                            ? drawable.options.fillWeight
                                                            : nodeWidth));
      } else {
        paintRoughShape(painter, nodePaint, nodeWidth, diamond,
                        fillPaints, strokePaints);
      }
      continue;
    }
    if (shape == QLatin1String("note")) {
      if (scene.handDrawn)
        rough::drawRoughDrawable(
            painter, node.roughDrawables.value(0),
            !fillPaints || nodePaint.fill.none ? QBrush(Qt::NoBrush)
                                               : QBrush(nodePaint.fill.value),
            !strokePaints || nodePaint.stroke.none || nodeWidth <= 0.0
                ? QPen(Qt::NoPen)
                : QPen(nodePaint.stroke.value, nodeWidth),
            !fillPaints || nodePaint.fill.none ? QPen(Qt::NoPen)
                                               : QPen(nodePaint.fill.value,
                                                      node.roughDrawables.value(0)
                                                              .options.fillWeight >= 0.0
                                                          ? node.roughDrawables.value(0)
                                                                .options.fillWeight
                                                          : nodeWidth));
      else {
        // The note's rough pair is two independent path elements: the fill
        // path hiding does NOT remove the stroke path (the box keeps its
        // outline), so per-channel gates — no whole-shape fold.
        QPainterPath notePath;
        notePath.addRect(node.bounds);
        ShadowChannels channels;
        channels.fillAlpha = shadowChannelAlpha(nodePaint.fill, fillPaints);
        channels.strokeAlpha = shadowChannelAlpha(nodePaint.stroke, strokePaints);
        shapeShadow(notePath, nodeWidth, channels);
        paintRoughShape(painter, nodePaint, nodeWidth, notePath,
                        fillPaints, strokePaints);
      }
      if (labelPaints)
        paintNodeLabel(painter, node, scene.style, textColour, family, labelSize,
                       descriptionPaints);
      continue;
    }
    // rect / rectWithTitle: rounded 5px (CSS `.statediagram-state
    // rect.basic`/`.title-state`; probe shows plain rects keep rx 5 under
    // neo too — only clusters take the theme radius).
    if (scene.handDrawn)
      rough::drawRoughDrawable(
          painter, node.roughDrawables.value(0),
          !fillPaints || nodePaint.fill.none ? QBrush(Qt::NoBrush)
                                             : QBrush(nodePaint.fill.value),
          !strokePaints || nodePaint.stroke.none || nodeWidth <= 0.0
              ? QPen(Qt::NoPen)
              : QPen(nodePaint.stroke.value, nodeWidth),
          !fillPaints || nodePaint.fill.none ? QPen(Qt::NoPen)
                                             : QPen(nodePaint.fill.value,
                                                    node.roughDrawables.value(0)
                                                            .options.fillWeight >= 0.0
                                                        ? node.roughDrawables.value(0)
                                                              .options.fillWeight
                                                        : nodeWidth));
    else if (!shapeHidden) {
      QPainterPath rectPath;
      rectPath.addRoundedRect(node.bounds, 5.0, 5.0);
      ShadowChannels channels;
      channels.fillAlpha = shadowChannelAlpha(nodePaint.fill, fillPaints);
      channels.strokeAlpha = shadowChannelAlpha(nodePaint.stroke, strokePaints);
      shapeShadow(rectPath, nodeWidth, channels);
      painter.setPen(nodePaint.stroke.none || nodeWidth <= 0.0
                         ? Qt::NoPen : QPen(nodePaint.stroke.value, nodeWidth));
      painter.setBrush(nodePaint.fill.none ? QBrush(Qt::NoBrush)
                                           : QBrush(nodePaint.fill.value));
      painter.drawPath(rectPath);
    }
    if (!node.descriptions.isEmpty()) {
      // line.divider at titleHeight + padding/2 below the rect top — its
      // OWN DOM element: `.statediagram-state .divider { stroke:
      // stateBorder }` plus whatever themeCSS resolves onto the line.
      const qreal dividerY = node.bounds.top() +
          (node.titleHeight > 0.0 ? node.titleHeight : scene.style.lineHeight) +
          4.0;
      const PaintSlot dividerStroke = paintSlot(node.dividerCss.stroke,
          node.stroke, mermaid::color::SvgPaintKind::Stroke, inherited);
      const qreal dividerWidth = node.dividerCss.strokeWidthSet
          ? node.dividerCss.strokeWidthPx : 1.0;
      const bool dividerPaints = node.dividerCss.displayed &&
          node.dividerCss.painted;
      if (scene.handDrawn)
        rough::drawRoughDrawable(
            painter, node.roughDrawables.value(1), Qt::NoBrush,
            !dividerPaints || dividerStroke.none || dividerWidth <= 0.0
                ? QPen(Qt::NoPen)
                : QPen(withStrokeOpacity(dividerStroke.value, node.dividerCss),
                       dividerWidth),
            Qt::NoPen);
      else if (dividerPaints && dividerStroke.paints() && dividerWidth > 0.0) {
        painter.setPen(QPen(withStrokeOpacity(dividerStroke.value,
                                              node.dividerCss),
                            dividerWidth));
        painter.drawLine(node.bounds.left(), dividerY,
                         node.bounds.right(), dividerY);
      }
    }
    if (labelPaints)
      paintNodeLabel(painter, node, scene.style, textColour, family, labelSize,
                     descriptionPaints, descFamily, descSize, descColor);
  }
}

QImage renderStateSceneToImage(const StateScene& scene, qreal dpr, qreal padding) {
  const qreal width = std::max(1.0, scene.bounds.width() + 2.0 * padding);
  const qreal height = std::max(1.0, scene.bounds.height() + 2.0 * padding);
  // Chromium element screenshots snap the fractional client box to the
  // NEAREST device pixel (131.125 css px @1.25 -> 131; 108.67 @1 -> 109);
  // qCeil inflated every fractional box by one pixel.
  QImage image(qRound(width * dpr), qRound(height * dpr),
               QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(dpr, dpr);
  painter.translate(padding - scene.bounds.left(), padding - scene.bounds.top());
  paintStateScene(scene, painter);
  painter.end();
  image.setDevicePixelRatio(dpr);
  return image;
}

void StateScene::paint(QPainter& painter, const MermaidPaintOptions& options) const {
  paintStateScene(*this, painter, options);
}

}  // namespace muffin::mermaid::state
