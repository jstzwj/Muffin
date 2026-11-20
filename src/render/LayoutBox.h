#pragma once

#include "theme/ThemeDefinition.h"
#include <QFont>
#include "render/TextLayout.h"
#include "render/Filter.h"
#include <QRectF>
#include <QHash>
#include <QTransform>
#include <memory>
#include <functional>

class QPainter;

namespace muffin {

struct CssPaintRecipe;
class CssPaintContext;

// Used values and geometry belong to the layout snapshot. Painting and input
// must not query CSS again or reconstruct content coordinates from theme tokens.
struct LayoutBox {
  QString hostKey;
  ThemeElementStyle style;
  ThemeElementPaintStyle hoverPaint, focusPaint;
  ThemeElementBoxStyle usedBox;
  QFont font;
  qreal lineHeight = 0;
  QRectF flowRect;
  QRectF borderBox;
  QRectF paddingBox;
  QRectF contentBox;
  QPointF inlineTextOrigin;
  QRectF visualOverflow;
  bool valid = false;
  std::shared_ptr<const CssPaintRecipe> paintRecipe;

  static QMarginsF borders(const ThemeElementBoxStyle& box) {
    return QMarginsF(box.borderLeftWidth, box.borderTopWidth, box.borderRightWidth, box.borderBottomWidth);
  }
  static QMarginsF insets(const ThemeElementBoxStyle& box) { return box.padding + borders(box); }
  // A wrapped inline has edges only at its authored start/end (box-decoration-break:
  // slice). Use the run's font and the shared text baseline, not the tallest glyph
  // or atom elsewhere in the line, for its painted box.
  static LayoutBox inlineFragment(const LayoutBox& run, qreal left, qreal right, qreal baseline, bool first,
                                  bool last) {
    auto used = run.usedBox;
    if (!first && !run.style.cloneInlineDecoration) {
      used.padding.setLeft(0);
      used.borderLeftWidth = 0;
    }
    if (!last && !run.style.cloneInlineDecoration) {
      used.padding.setRight(0);
      used.borderRightWidth = 0;
    }
    const auto inset = insets(used);
    const TextFontMetrics metrics(run.font);
    const QRectF rect(qMin(left, right) - inset.left(), baseline - metrics.ascent() - inset.top(),
                      qAbs(right - left) + inset.left() + inset.right(),
                      metrics.height() + inset.top() + inset.bottom());
    auto fragment = place(run.hostKey, run.style, used, rect, run.font);
    fragment.paintRecipe = run.paintRecipe;
    return fragment;
  }
  static qreal borderWidth(const ThemeElementBoxStyle& box, qreal containingWidth, qreal intrinsicWidth = -1) {
    const auto inset = insets(box);
    const qreal extra = box.borderBox ? 0 : inset.left() + inset.right();
    qreal width = box.widthLength.status == CssLengthStatus::Valid ? box.widthLength.used(containingWidth) + extra
                                                                   : containingWidth;
    if (box.widthFitContent && intrinsicWidth >= 0)
      width = qMin(width, intrinsicWidth + inset.left() + inset.right());
    if (box.maxWidthLength.status == CssLengthStatus::Valid)
      width = qMin(width, box.maxWidthLength.used(containingWidth) + extra);
    if (box.minWidthLength.status == CssLengthStatus::Valid)
      width = qMax(width, box.minWidthLength.used(containingWidth) + extra);
    return qMax<qreal>(1, width);
  }
  static qreal borderHeight(const ThemeElementBoxStyle& box, qreal intrinsicHeight) {
    const auto inset = insets(box);
    const qreal extra = box.borderBox ? 0 : inset.top() + inset.bottom();
    qreal height = intrinsicHeight + inset.top() + inset.bottom();
    if (box.heightLength.status == CssLengthStatus::Valid && !box.heightLength.hasPercentage)
      height = box.heightLength.px + extra;
    if (box.maxHeightLength.status == CssLengthStatus::Valid && !box.maxHeightLength.hasPercentage)
      height = qMin(height, box.maxHeightLength.px + extra);
    if (box.minHeightLength.status == CssLengthStatus::Valid && !box.minHeightLength.hasPercentage)
      height = qMax(height, box.minHeightLength.px + extra);
    return qMax<qreal>(0, height);
  }
  static LayoutBox place(QString key, ThemeElementStyle style, ThemeElementBoxStyle used, QRectF rect,
                         QFont font = {}) {
    LayoutBox box;
    box.hostKey = std::move(key);
    box.style = std::move(style);
    box.paintRecipe = box.style.paintRecipe;
    box.usedBox = std::move(used);
    box.font = std::move(font);
    box.flowRect = box.borderBox = rect;
    box.paddingBox = rect.marginsRemoved(borders(box.usedBox));
    box.contentBox = box.paddingBox.marginsRemoved(box.usedBox.padding);
    box.inlineTextOrigin = box.contentBox.topLeft();
    const qreal overflow =
        qMax(box.style.paint.boxShadowBlur + box.style.paint.boxShadowSpread, box.style.paint.filterBlur + 2);
    box.visualOverflow = rect.adjusted(-overflow, -overflow, overflow, overflow);
    const auto scale = box.style.text.fontSizePx > 0 && box.font.pointSizeF() > 0
                           ? box.font.pointSizeF() * 96 / 72 / box.style.text.fontSizePx
                           : 1;
    box.visualOverflow = box.visualOverflow.united(rect.marginsAdded(cssFilterOutsets(box.style.paint, scale)));
    box.valid = true;
    return box;
  }
};

enum class BoxBorders { Paint, Skip };
void paintLayoutBox(QPainter& painter, const LayoutBox& box, QPointF offset = {},
                    const ThemeElementStyle* style = nullptr, BoxBorders borders = BoxBorders::Paint);
void paintBorderSide(QPainter& painter, QRectF area, const QColor& color, const QString& style, bool horizontal);
QTransform elementPaintTransform(const ThemeElementPaintStyle& style, const QRectF& rect, qreal zoom = 1);
// Wrap the complete painted element so opacity and filters composite its
// background and glyphs together. The callback receives neutral layer effects.
void paintCssElement(QPainter& painter, QRectF borderBox, QRectF visualBounds, const ThemeElementPaintStyle& style,
                     qreal zoom, CssPaintContext* context,
                     const std::function<void(QPainter&, const ThemeElementPaintStyle&)>& contents,
                     const LayoutBox* maskGeometry = nullptr);

// One immutable used-style snapshot can serve every equivalent inline run in a
// layout generation. Positioned fragments keep their own geometry separately.
class LayoutStyleCache {
public:
  std::shared_ptr<const LayoutBox> snapshot(const ThemeElementStyle& style, const ThemeElementBoxStyle& used,
                                            const QFont& font, qreal containingWidth) {
    const auto key = style.key + QLatin1Char('/') + QString::number(style.fingerprint) + QLatin1Char('/') + font.key() +
                     QLatin1Char('/') + QString::number(containingWidth, 'g', 17) + '/' +
                     QString::number(reinterpret_cast<quintptr>(style.paintRecipe.get()));
    if (const auto found = entries_.constFind(key); found != entries_.cend())
      return found.value();
    auto value = std::make_shared<const LayoutBox>(LayoutBox::place(style.key, style, used, {}, font));
    entries_.insert(key, value);
    return value;
  }
  void clear() { entries_.clear(); }

private:
  QHash<QString, std::shared_ptr<const LayoutBox>> entries_;
};

} // namespace muffin
