#include "html/HtmlLayoutResult.h"
#include "render/ImageDecoder.h"
#include "render/ImageLoader.h"
#include "render/ImagePlaceholder.h"

#include <QFontMetricsF>
#include <QDir>
#include <QPainterPath>
#include <QPen>
#include <QScopeGuard>

#include <utility>
#include <limits>
#include <algorithm>

namespace muffin::html {
HtmlLayoutResult::HtmlLayoutResult() = default;
HtmlLayoutResult::~HtmlLayoutResult() = default;

HtmlLayoutResult::HtmlLayoutResult(HtmlLayoutResult&&) noexcept = default;
HtmlLayoutResult& HtmlLayoutResult::operator=(HtmlLayoutResult&&) noexcept = default;

bool HtmlLayoutResult::valid() const { return static_cast<bool>(root_); }
QSizeF HtmlLayoutResult::size() const { return size_; }
QString HtmlLayoutResult::error() const { return error_; }
QString HtmlLayoutResult::baseDirectory() const { return baseDirectory_; }
const HtmlBox* HtmlLayoutResult::root() const { return root_.get(); }

bool HtmlLayoutResult::hasVisibleContent() const {
  if (!root_) {
    return false;
  }
  // The root is the engine's synthetic <body> wrapper; the style resolver paints it with the
  // canvas background (palette.background), which is the page surface, not content. Treat it as
  // a pure container and ask whether any of its descendants actually draws something.
  for (const auto& child : root_->children()) {
    if (boxHasVisibleContent(*child)) {
      return true;
    }
  }
  return false;
}

void HtmlLayoutResult::setBaseDirectory(QString directory) { baseDirectory_ = std::move(directory); }
void HtmlLayoutResult::setRoot(std::unique_ptr<HtmlBox> root) { root_ = std::move(root); }
void HtmlLayoutResult::setTextLayouts(std::vector<std::unique_ptr<HtmlTextLayout>> layouts) {
  textLayouts_ = std::move(layouts);
}
void HtmlLayoutResult::setSize(QSizeF size) { size_ = size; }
void HtmlLayoutResult::setError(QString error) { error_ = std::move(error); }
void HtmlLayoutResult::setPalette(HtmlColorPalette palette) { palette_ = std::move(palette); }

void HtmlLayoutResult::paint(QPainter& painter, QPointF origin) const {
  if (!root_) {
    return;
  }
  paintBox(painter, *root_, origin);
}

HtmlLayoutResult::HitResult HtmlLayoutResult::hitTest(QPointF localPos) const {
  if (!root_) {
    return {};
  }
  return hitTestBox(*root_, localPos, QPointF());
}

void HtmlLayoutResult::visitTextLayouts(const HtmlBox& box, QPointF parent,
                                        const std::function<void(const HtmlTextLayout&, QPointF)>& visitor) const {
  if (!box.style().visible || box.style().display == HtmlDisplay::None) return;
  const auto origin = parent + QPointF(box.geometry().left, box.geometry().top);
  if (box.ownsTextLayout() && box.textLayoutIndex() >= 0 && box.textLayoutIndex() < int(textLayouts_.size())) {
    const auto& text = *textLayouts_[box.textLayoutIndex()];
    visitor(text, box.layoutBox.contentBox.translated(parent).topLeft());
    for (const auto& atom : text.atoms) visitTextLayouts(*atom.box, origin, visitor);
  } else {
    for (const auto& child : box.children()) {
      if (box.tag() == HtmlTag::Details && !box.detailsOpen() && child->tag() != HtmlTag::Summary) continue;
      visitTextLayouts(*child, origin, visitor);
    }
  }
}

int HtmlLayoutResult::textOffsetAtPoint(QPointF point) const {
  int result = 0;
  qreal best = std::numeric_limits<qreal>::max();
  if (root_)
    visitTextLayouts(*root_, {}, [&](const HtmlTextLayout& text, QPointF origin) {
      if (!text.layout) return;
      for (const auto& span : text.sourceSpans)
        for (int i = 0; i < text.layout->lineCount(); ++i) {
          const auto line = text.layout->lineAt(i);
          const int first = qMax(span.start, line.textStart()), last = qMin(span.start + span.length, line.textStart() + line.textLength());
          if (first >= last) continue;
          const auto left = line.cursorToX(first), right = line.cursorToX(last);
          const QRectF rect = QRectF(qMin(left, right), line.y(), qAbs(right - left), line.height()).translated(origin);
          const auto dx = std::max({rect.left() - point.x(), qreal(0), point.x() - rect.right()});
          const auto dy = std::max({rect.top() - point.y(), qreal(0), point.y() - rect.bottom()});
          const auto distance = dx * dx + dy * dy;
          if (distance < best) {
            best = distance;
            result = span.textStart + qBound(first, line.xToCursor(point.x() - origin.x()), last) - span.start;
          }
        }
    });
  return result;
}

QRectF HtmlLayoutResult::cursorRectForTextOffset(int offset) const {
  QRectF result;
  bool exactStart = false;
  if (root_)
    visitTextLayouts(*root_, {}, [&](const HtmlTextLayout& text, QPointF origin) {
      if (!text.layout) return;
      for (const auto& span : text.sourceSpans) {
        if (offset < span.textStart || offset > span.textStart + span.length) continue;
        if (exactStart || (!result.isNull() && offset != span.textStart)) continue;
        const auto position = span.start + offset - span.textStart;
        const auto line = text.layout->lineForTextPosition(position);
        if (line.isValid()) {
          result = QRectF(line.cursorToX(position), line.y(), 1, line.height()).translated(origin);
          exactStart = offset == span.textStart;
        }
      }
    });
  return result;
}

QVector<QRectF> HtmlLayoutResult::selectionRects(int start, int end) const {
  QVector<QRectF> result;
  if (root_)
    visitTextLayouts(*root_, {}, [&](const HtmlTextLayout& text, QPointF origin) {
      if (!text.layout) return;
      for (const auto& span : text.sourceSpans)
        for (int i = 0; i < text.layout->lineCount(); ++i) {
          const auto line = text.layout->lineAt(i);
          const int first = std::max({span.start + qMin(start, end) - span.textStart, span.start, line.textStart()});
          const int last =
              std::min({span.start + qMax(start, end) - span.textStart, span.start + span.length, line.textStart() + line.textLength()});
          if (first >= last) continue;
          const auto left = line.cursorToX(first), right = line.cursorToX(last);
          result.push_back(QRectF(qMin(left, right), line.y(), qAbs(right - left), line.height()).translated(origin));
        }
    });
  return result;
}

bool HtmlLayoutResult::boxHasVisibleContent(const HtmlBox& box) const {
  // display:none is flattened to style().visible = false in HtmlBoxBuilder, and paintBox()
  // returns early on it without recursing — so a hidden ancestor suppresses its whole subtree.
  // Mirror that here: once a box does not paint, neither do its descendants.
  if (!box.style().visible) {
    return false;
  }
  const auto& style = box.style();

  // A non-transparent background paints a filled box (e.g. a coloured spacer is real content).
  if (style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0) {
    return true;
  }
  // A non-none border paints pixels on at least one side.
  const bool hasBorder = style.borderWidth.top() > 0 || style.borderWidth.bottom() > 0 ||
                         style.borderWidth.left() > 0 || style.borderWidth.right() > 0;
  if (hasBorder && style.borderStyle != HtmlBorderStyle::None) {
    return true;
  }

  // Tag-specific painting done in paintBox() / paintHr() / paintImage() / paintListMarker().
  switch (box.tag()) {
    case HtmlTag::Hr:
      return true;
    case HtmlTag::Image:
      return true;
    case HtmlTag::ListItem:
      if (!box.listMarker().isEmpty()) {
        return true;
      }
      break;
    case HtmlTag::TextRun:
      if (!box.text().trimmed().isEmpty()) {
        return true;
      }
      break;
    default:
      break;
  }

  for (const auto& child : box.children()) {
    if (boxHasVisibleContent(*child)) {
      return true;
    }
  }
  return false;
}

HtmlLayoutResult::HitResult HtmlLayoutResult::hitTestBox(const HtmlBox& box, QPointF localPos, QPointF origin) const {
  if (!box.style().visible || box.style().display == HtmlDisplay::None) {
    return {};
  }

  const auto& geo = box.geometry();
  const QPointF boxOrigin = origin + QPointF(geo.left, geo.top);
  const QRectF boxRect(boxOrigin, QSizeF(geo.width, geo.height));
  const auto& overflow = box.style().computed.layout;
  const auto clip = box.layoutBox.paddingBox.translated(origin);
  if ((overflow.clipsX() && (localPos.x() < clip.left() || localPos.x() > clip.right())) ||
      (overflow.clipsY() && (localPos.y() < clip.top() || localPos.y() > clip.bottom()))) {
    return {};
  }

  if (box.tag() == HtmlTag::Image && boxRect.contains(localPos)) {
    return HitResult{QString(), box.src()};
  }

  if (box.ownsTextLayout() && boxRect.contains(localPos)) {
    const QRectF contentRect = box.layoutBox.contentBox.translated(origin);
    const QString href = linkHrefAtTextLayout(box, localPos - contentRect.topLeft());
    if (!href.isEmpty()) {
      return HitResult{href, QString()};
    }
  }

  // Hit testing must agree with painting (paintBox draws only the summary of a collapsed
  // <details>): hidden children keep whatever stale/borrowed geometry earlier layouts left on
  // them, and walking into them lets an invisible link intercept clicks meant for visible
  // content on top of it.
  const bool collapsedDetails = box.tag() == HtmlTag::Details && !box.detailsOpen();
  const bool formatting = box.style().computed.layout.establishesFormattingContext();
  for (size_t i = 0; i < box.children().size(); ++i) {
    const auto index = formatting && !box.formattingPaintOrder.empty() ? box.formattingPaintOrder[box.children().size() - 1 - i] : i;
    const auto& child = box.children()[index];
    if (collapsedDetails && child->tag() != HtmlTag::Summary) {
      continue;
    }
    HitResult childHit = hitTestBox(*child, localPos, boxOrigin);
    if (!childHit.linkHref.isEmpty() || !childHit.imageSrc.isEmpty()) {
      return childHit;
    }
  }

  return {};
}

QString HtmlLayoutResult::linkHrefAtTextLayout(const HtmlBox& box, QPointF localPos) const {
  const int index = box.textLayoutIndex();
  if (index < 0 || index >= static_cast<int>(textLayouts_.size())) {
    return {};
  }
  const auto& textLayout = textLayouts_.at(static_cast<size_t>(index));
  if (!textLayout || !textLayout->layout || textLayout->linkSpans.empty()) {
    return {};
  }

  int cursor = -1;
  for (int i = 0; i < textLayout->layout->lineCount(); ++i) {
    const QTextLine line = textLayout->layout->lineAt(i);
    if (localPos.y() >= line.y() && localPos.y() <= line.y() + line.height()) {
      cursor = line.xToCursor(localPos.x(), QTextLine::CursorOnCharacter);
      break;
    }
  }
  if (cursor < 0) {
    return {};
  }

  for (const auto& span : textLayout->linkSpans) {
    if (cursor >= span.start && cursor < span.start + span.length) {
      return span.href;
    }
  }
  return {};
}

void HtmlLayoutResult::paintBox(QPainter& painter, const HtmlBox& box, QPointF origin) const {
  if (!box.style().visible) {
    return;
  }

  const auto& geo = box.geometry();
  const QPointF boxOrigin = origin + QPointF(geo.left, geo.top);
  const QRectF boxRect(boxOrigin, QSizeF(geo.width, geo.height));

  // Content area (inside border + padding)
  const QRectF contentRect = box.layoutBox.contentBox.translated(origin);

  paintLayoutBox(painter, box.layoutBox, origin);
  painter.save();
  const auto restorePainter = qScopeGuard([&] { painter.restore(); });
  const auto& overflow = box.style().computed.layout;
  if (overflow.clipsX() || overflow.clipsY()) {
    auto clip = box.layoutBox.paddingBox.translated(origin);
    if (!overflow.clipsX()) clip.setLeft(-1e9), clip.setRight(1e9);
    if (!overflow.clipsY()) clip.setTop(-1e9), clip.setBottom(1e9);
    painter.setClipRect(clip, Qt::IntersectClip);
  }

  if (box.tag() == HtmlTag::ListItem && !box.listMarker().isEmpty()) {
    paintListMarker(painter, box, contentRect);
  }

  // Paint content based on tag
  switch (box.tag()) {
    case HtmlTag::Hr:
      paintHr(painter, box, contentRect);
      break;

    case HtmlTag::Image:
      paintImage(painter, box, contentRect.topLeft());
      break;

    case HtmlTag::Details:
      // Only paint summary when collapsed; paint all children when open
      if (box.detailsOpen()) {
        for (const auto& child : box.children()) {
          paintBox(painter, *child, boxOrigin);
        }
      } else {
        for (const auto& child : box.children()) {
          if (child->tag() == HtmlTag::Summary) {
            paintBox(painter, *child, boxOrigin);
          }
        }
      }
      break;

    default:
      if (box.ownsTextLayout()) {
        paintInlineContent(painter, box, contentRect.topLeft());
        break;
      }

      for (size_t i = 0; i < box.children().size(); ++i) {
        const auto index = box.formattingPaintOrder.empty() ? i : box.formattingPaintOrder[i];
        paintBox(painter, *box.children()[index], boxOrigin);
      }
      break;
  }
}

void HtmlLayoutResult::paintInlineContent(QPainter& painter, const HtmlBox& box, QPointF origin) const {
  paintTextRun(painter, box, origin);
}

void HtmlLayoutResult::paintTextRun(QPainter& painter, const HtmlBox& box, QPointF origin) const {
  const int index = box.textLayoutIndex();
  if (index < 0 || index >= static_cast<int>(textLayouts_.size())) {
    return;
  }
  const auto& textLayout = textLayouts_.at(static_cast<size_t>(index));
  if (!textLayout || !textLayout->layout) {
    return;
  }

  painter.save();
  for (const auto& fragment : textLayout->inlineBoxes) paintLayoutBox(painter, fragment, origin);
  textLayout->layout->draw(&painter, origin);
  for (const auto& atom : textLayout->atoms) {
    // Geometry already includes the text owner's content inset.
    const auto& geo = atom.box->geometry();
    paintBox(painter, *atom.box, origin + atom.rect.topLeft() - QPointF(geo.left, geo.top));
  }
  painter.restore();
}

void HtmlLayoutResult::paintListMarker(QPainter& painter, const HtmlBox& box, const QRectF& contentRect) const {
  painter.save();
  painter.setFont(box.style().font);
  painter.setPen(box.style().color.isValid() ? box.style().color : palette_.text);
  QFontMetricsF metrics(box.style().font);
  // Place the marker inside the list's left padding (40px from <ul>/<ol> defaults),
  // right-aligned just before the content area.
  const qreal markerWidth = 24.0;
  const qreal markerGap = 8.0;
  const QRectF markerRect(
      contentRect.left() - markerWidth - markerGap,
      contentRect.top(),
      markerWidth,
      qMax<qreal>(contentRect.height(), metrics.height()));
  painter.drawText(markerRect, Qt::AlignRight | Qt::AlignTop, box.listMarker());
  painter.restore();
}

void HtmlLayoutResult::paintHr(QPainter& painter, const HtmlBox& box, const QRectF& contentRect) const {
  painter.save();
  QColor color = box.style().borderColor.isValid() ? box.style().borderColor : palette_.tableBorder;
  painter.setPen(QPen(color, 1));
  const qreal y = contentRect.center().y();
  painter.drawLine(QPointF(contentRect.left(), y), QPointF(contentRect.right(), y));
  painter.restore();
}

void HtmlLayoutResult::paintImage(QPainter& painter, const HtmlBox& box, QPointF origin) const {
  if (box.src().isEmpty()) {
    // No src — draw placeholder with icon
    const auto& geo = box.geometry();
    const qreal w = box.layoutBox.valid ? box.layoutBox.contentBox.width() : geo.width;
    const qreal h = box.layoutBox.valid ? box.layoutBox.contentBox.height() : geo.height;
    painter.save();
    painter.setPen(palette_.codeBorder);
    painter.setBrush(palette_.codeBackground);
    painter.drawRect(QRectF(origin, QSizeF(w, h)));
    // Center a placeholder icon inside the box
    constexpr qreal kIconSize = 24.0;
    const QImage icon = image_placeholder::loading(QSizeF(kIconSize, kIconSize));
    if (!icon.isNull()) {
      const qreal ix = origin.x() + (w - kIconSize) / 2.0;
      const qreal iy = origin.y() + (h - kIconSize) / 2.0;
      painter.drawImage(QRectF(ix, iy, kIconSize, kIconSize), icon);
    } else {
      painter.setPen(palette_.muted);
      painter.drawText(QRectF(origin, QSizeF(w, h)), Qt::AlignCenter,
                       box.alt().isEmpty() ? QStringLiteral("[image]") : box.alt());
    }
    painter.restore();
    return;
  }

  const QImage& image = cachedImage(box.src());
  if (image.isNull()) {
    // Failed to load — draw broken-image icon inside the box
    const auto& geo = box.geometry();
    const qreal w = box.layoutBox.valid ? box.layoutBox.contentBox.width() : geo.width;
    const qreal h = box.layoutBox.valid ? box.layoutBox.contentBox.height() : geo.height;
    painter.save();
    painter.setPen(palette_.codeBorder);
    painter.setBrush(palette_.codeBackground);
    painter.drawRect(QRectF(origin, QSizeF(w, h)));
    constexpr qreal kIconSize = 24.0;
    const QImage icon = image_placeholder::broken(QSizeF(kIconSize, kIconSize));
    if (!icon.isNull()) {
      const qreal ix = origin.x() + (w - kIconSize) / 2.0;
      const qreal iy = origin.y() + (h - kIconSize) / 2.0;
      painter.drawImage(QRectF(ix, iy, kIconSize, kIconSize), icon);
    } else {
      painter.setPen(QColor(200, 50, 50));
      painter.drawText(QRectF(origin, QSizeF(w, h)), Qt::AlignCenter,
                       box.alt().isEmpty() ? QStringLiteral("[broken image]") : box.alt());
    }
    painter.restore();
    return;
  }

  // Layout has already resolved natural size, ratio and constraints. The
  // default CSS object fitting fills that content box, including enlargement.
  const QSizeF size = box.layoutBox.valid ? box.layoutBox.contentBox.size() : QSizeF(box.geometry().width, box.geometry().height);
  painter.drawImage(QRectF(origin, size), image);
}

const QImage& HtmlLayoutResult::cachedImage(const QString& src) const {
  auto it = imageCache_.constFind(src);
  if (it != imageCache_.constEnd()) {
    return it.value();
  }

  // Remote URL: consult async ImageLoader singleton
  if (src.startsWith(QLatin1String("http://")) ||
      src.startsWith(QLatin1String("https://"))) {
    QImage cached = ImageLoader::instance().cached(src);
    if (!cached.isNull()) {
      it = imageCache_.insert(src, std::move(cached));
      return it.value();
    }
    // Not yet downloaded — request async; imageReady signal triggers a rebuild
    ImageLoader::instance().request(src);
    it = imageCache_.insert(src, QImage());
    return it.value();
  }

  // Inline data: URI (RFC 2397, base64 or percent-encoded) — decode synchronously.
  if (src.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
    QImage img = image_decoder::decodeDataUri(src);
    it = imageCache_.insert(src, std::move(img));
    return it.value();
  }

  // Local file path — fall back to ImageDecoder for SVG, WebP, AVIF
  QImage img(src);
  if (img.isNull()) {
    img = image_decoder::decodeFileFallback(src);
  }
  it = imageCache_.insert(src, std::move(img));
  return it.value();
}

}  // namespace muffin::html
