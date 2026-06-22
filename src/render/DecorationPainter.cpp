#include "render/DecorationPainter.h"

#include "render/GradientPainter.h"

#include <QBrush>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QSvgRenderer>
#include <QtMath>

#include <memory>

namespace muffin {
namespace DecorationPainter {

namespace {

// Process-wide SVG cache keyed by SVG byte data (content-addressed; themes share).
std::shared_ptr<QSvgRenderer> svgIcon(const QByteArray& data) {
  static QHash<QByteArray, std::shared_ptr<QSvgRenderer>> cache;
  if (data.isEmpty()) { return nullptr; }
  const auto it = cache.constFind(data);
  if (it != cache.constEnd()) { return *it; }
  auto r = std::make_shared<QSvgRenderer>(data);
  if (!r->isValid()) { r.reset(); }
  cache.insert(data, r);
  return r;
}

const ElementBackground* elementBackground(const RenderTheme& theme, const QString& host) {
  for (const ElementBackground& eb : theme.decorations().backgrounds) {
    if (eb.host == host) { return &eb; }
  }
  return nullptr;
}

const PseudoElementRule* pseudoRule(const RenderTheme& theme, const QString& host, const QString& pseudo) {
  for (const PseudoElementRule& r : theme.decorations().pseudos) {
    if (r.host == host && r.pseudo == pseudo) { return &r; }
  }
  return nullptr;
}

const HoverEffect* hoverEffectFor(const RenderTheme& theme, const QString& host) {
  for (const HoverEffect& he : theme.decorations().hoverEffects) {
    if (he.host == host) { return &he; }
  }
  return nullptr;
}

}  // namespace

void paintIcon(QPainter& painter, const QByteArray& svgData, const QRectF& target,
               const QColor& tint, bool recolour) {
  const auto icon = svgIcon(svgData);
  if (!icon) { return; }
  if (!recolour || !tint.isValid()) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    icon->render(&painter, target);
    painter.restore();
    return;
  }
  // Mask recolour: render the SVG (an alpha shape — phycat's mask icons carry no
  // fill) into a temp, then keep `tint` where the shape has alpha.
  const int w = qMax(1, int(qCeil(target.width())));
  const int h = qMax(1, int(qCeil(target.height())));
  QImage shape(w, h, QImage::Format_ARGB32_Premultiplied);
  shape.fill(Qt::transparent);
  { QPainter sp(&shape);
    sp.setRenderHint(QPainter::SmoothPixmapTransform, true);
    icon->render(&sp, QRectF(0, 0, w, h));
  }
  QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  { QPainter op(&out);
    op.fillRect(out.rect(), tint);
    op.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    op.drawImage(0, 0, shape);
  }
  painter.save();
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(target, out);
  painter.restore();
}

void paintElementBackground(QPainter& painter, const RenderTheme& theme, const QString& host, const QRectF& rect) {
  const ElementBackground* eb = elementBackground(theme, host);
  if (!eb || eb->gradient.kind == GradientSpec::Kind::None) { return; }
  painter.save();
  if (eb->color.isValid()) { painter.fillRect(rect, eb->color); }
  painter.setOpacity(eb->opacity);
  painter.fillRect(rect, GradientPainter::makeBrush(eb->gradient, rect));
  painter.restore();
}

bool hasElementBackground(const RenderTheme& theme, const QString& host) {
  const ElementBackground* eb = elementBackground(theme, host);
  return eb && eb->gradient.kind != GradientSpec::Kind::None;
}

void paintHrGradient(QPainter& painter, const RenderTheme& theme, const QRectF& rect) {
  const ElementBackground* eb = elementBackground(theme, QStringLiteral("hr"));
  if (!eb) { return; }
  const qreal h = qMax<qreal>(2.0, rect.height() * 0.08);
  const QRectF bar(rect.left(), rect.center().y() - h / 2.0, rect.width(), h);
  painter.save();
  painter.fillRect(bar, GradientPainter::makeBrush(eb->gradient, bar));
  painter.restore();
}

void paintPseudoDecorations(QPainter& painter, const RenderTheme& theme, const QString& host,
                            const QRectF& rect, const PaintContext& ctx) {
  const bool isHeading = ctx.headingLevel >= 1 && ctx.headingLevel <= 6;
  const QFontMetricsF fm(ctx.font);
  const qreal em = fm.height();

  if (const PseudoElementRule* before = pseudoRule(theme, host, QStringLiteral("before"))) {
    if (!before->svgData.isEmpty()) {
      const QPointF anchor = ctx.textStart.x() >= 0 ? ctx.textStart : rect.topLeft();
      const qreal s = (before->size.width() > 0 ? before->size.width() : em);
      const QColor tint = before->color.isValid() ? before->color : before->backgroundColor;
      paintIcon(painter, before->svgData, QRectF(anchor.x() - s - 2.0, anchor.y(), s, s), tint, before->svgFromMask);
    } else if (!before->content.isEmpty() && host == QStringLiteral("blockquote")) {
      painter.save();
      painter.setFont(ctx.font);
      painter.setPen(before->color.isValid() ? before->color : theme.textColor());
      painter.drawText(QPointF(rect.left() + 4.0, rect.top() + fm.ascent() + 2.0), before->content);
      painter.restore();
    }
  }

  if (const PseudoElementRule* after = pseudoRule(theme, host, QStringLiteral("after"))) {
    if (!after->svgData.isEmpty() && isHeading) {
      const QPointF anchor = ctx.textEnd.x() >= 0 ? ctx.textEnd : QPointF(rect.right(), rect.top());
      const qreal s = (after->size.width() > 0 ? after->size.width() : em);
      const QColor tint = after->color.isValid() ? after->color : after->backgroundColor;
      paintIcon(painter, after->svgData, QRectF(anchor.x() + 4.0, anchor.y() + (em - s) / 2.0, s, s), tint, after->svgFromMask);
    } else if ((after->background.kind != GradientSpec::Kind::None || after->backgroundColor.isValid() ||
                (after->borderBottomColor.isValid() && after->borderBottomWidth > 0.0)) && isHeading) {
      // ::after underline bar. Width/height come from the rule (e.g. Whitey's
      // h2::after border-bottom: 100px centred; phycat's h1::after gradient bar).
      const qreal borderW = after->borderBottomWidth > 0.0 ? after->borderBottomWidth : 0.0;
      const qreal barH = (after->size.height() > 0 ? after->size.height() : qMax<qreal>(2.0, borderW));
      qreal barW = (after->size.width() > 0 ? after->size.width() : (ctx.textBounds.isValid() ? ctx.textBounds.width() : rect.width()));
      barW = qMin(barW, rect.width());
      const qreal textMid = ctx.textBounds.isValid() ? ctx.textBounds.center().x()
                            : (ctx.textStart.x() >= 0 && ctx.textEnd.x() >= 0
                                   ? (ctx.textStart.x() + ctx.textEnd.x()) / 2.0
                                   : rect.center().x());
      const QRectF bar(textMid - barW / 2.0, rect.bottom() - barH, barW, barH);
      painter.save();
      painter.setOpacity(after->opacity);
      if (after->background.kind != GradientSpec::Kind::None) {
        painter.fillRect(bar, GradientPainter::makeBrush(after->background, bar));
      } else if (after->backgroundColor.isValid()) {
        painter.fillRect(bar, after->backgroundColor);
      }
      if (after->borderBottomColor.isValid() && after->borderBottomWidth > 0.0) {
        painter.setPen(QPen(after->borderBottomColor, after->borderBottomWidth));
        painter.drawLine(bar.bottomLeft(), bar.bottomRight());
      }
      painter.restore();
    }
  }
}

void paintWriteTexture(QPainter& painter, const RenderTheme& theme, const QRectF& pageRect) {
  const PseudoElementRule* rule = pseudoRule(theme, QStringLiteral("#write"), QStringLiteral("before"));
  if (!rule || rule->maskPattern.kind == GradientSpec::Kind::None) { return; }
  const qreal tileW = qBound(2.0, rule->maskTile.width(), 256.0);
  const qreal tileH = qBound(2.0, rule->maskTile.height(), 256.0);
  // Recolour the mask gradient stops to the tint (a mask is colour-agnostic; the
  // ::before background-colour supplies the visible tint).
  GradientSpec tinted = rule->maskPattern;
  const QColor tint = rule->maskTint.isValid() ? rule->maskTint : theme.textColor();
  for (GradientStop& s : tinted.stops) {
    if (s.color != QColor(Qt::transparent)) { s.color = tint; }
  }
  QImage tile(int(qCeil(tileW)), int(qCeil(tileH)), QImage::Format_ARGB32_Premultiplied);
  tile.fill(Qt::transparent);
  { QPainter tp(&tile);
    tp.fillRect(tile.rect(), GradientPainter::makeBrush(tinted, QRectF(0, 0, tileW, tileH)));
  }
  QBrush pattern(QPixmap::fromImage(tile));  // QBrush(QPixmap) → TexturePattern, tiles
  painter.save();
  painter.setOpacity(rule->opacity);
  painter.fillRect(pageRect, pattern);
  painter.restore();
}

void paintGlow(QPainter& painter, const QRectF& rect, const QColor& color, qreal blur, qreal alpha) {
  if (!color.isValid() || blur <= 0.0 || alpha <= 0.0) { return; }
  painter.save();
  painter.setPen(Qt::NoPen);
  QColor base = color;
  base.setAlpha(qMin(base.alpha(), 60));  // cap the peak so the shell sum stays soft
  constexpr int kLayers = 8;
  for (int i = kLayers; i >= 1; --i) {
    const qreal grow = blur * (i / qreal(kLayers));
    QColor shell = base;
    shell.setAlphaF(base.alphaF() * alpha / qreal(kLayers));
    painter.setBrush(shell);
    painter.drawRoundedRect(rect.adjusted(-grow, -grow, grow, grow), grow, grow);
  }
  painter.restore();
}

void paintBlockHoverGlow(QPainter& painter, const RenderTheme& theme, const QString& host,
                         const QRectF& rect, qreal phase) {
  const HoverEffect* he = hoverEffectFor(theme, host);
  if (!he) { return; }
  paintGlow(painter, rect, he->glowColor, he->glowBlur, phase);
}

}  // namespace DecorationPainter
}  // namespace muffin
