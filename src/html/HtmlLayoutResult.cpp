#include "html/HtmlLayoutResult.h"
#include "render/ImageDecoder.h"
#include "render/ImageLoader.h"
#include "render/ImagePlaceholder.h"

#include <QFontMetricsF>
#include <QDir>
#include <QPainterPath>
#include <QPen>

#include <utility>

namespace muffin::html {
namespace {

void paintKeyboardSpanRects(
    QPainter& painter,
    const QTextLayout& layout,
    const std::vector<TextFormatSpan>& spans,
    QPointF origin) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);

  for (const TextFormatSpan& span : spans) {
    if (!span.keyboard || span.length <= 0) {
      continue;
    }

    for (int i = 0; i < layout.lineCount(); ++i) {
      const QTextLine line = layout.lineAt(i);
      if (!line.isValid()) {
        continue;
      }
      const int lineStart = line.textStart();
      const int lineEnd = lineStart + line.textLength();
      const int rangeStart = qMax(lineStart, span.start);
      const int rangeEnd = qMin(lineEnd, span.start + span.length);
      if (rangeStart >= rangeEnd) {
        continue;
      }

      const qreal x1 = line.cursorToX(rangeStart);
      const qreal x2 = line.cursorToX(rangeEnd);
      const QRectF rect(
          origin.x() + qMin(x1, x2) - 4.0,
          origin.y() + line.y() + 1.0,
          qAbs(x2 - x1) + 8.0,
          qMax<qreal>(1.0, line.height() - 3.0));

      painter.setPen(QPen(QColor(196, 201, 209), 1.0));
      painter.setBrush(QColor(250, 251, 252));
      painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 2.0, 2.0);

      painter.setPen(QPen(QColor(181, 186, 194), 1.0));
      painter.drawLine(rect.bottomLeft() + QPointF(2.0, -0.5), rect.bottomRight() + QPointF(-2.0, -0.5));
    }
  }

  painter.restore();
}

}  // namespace

HtmlLayoutResult::HtmlLayoutResult() = default;
HtmlLayoutResult::~HtmlLayoutResult() = default;

HtmlLayoutResult::HtmlLayoutResult(HtmlLayoutResult&&) noexcept = default;
HtmlLayoutResult& HtmlLayoutResult::operator=(HtmlLayoutResult&&) noexcept = default;

bool HtmlLayoutResult::valid() const { return static_cast<bool>(root_); }
QSizeF HtmlLayoutResult::size() const { return size_; }
QString HtmlLayoutResult::error() const { return error_; }
QString HtmlLayoutResult::baseDirectory() const { return baseDirectory_; }
const HtmlBox* HtmlLayoutResult::root() const { return root_.get(); }

void HtmlLayoutResult::setBaseDirectory(QString directory) { baseDirectory_ = std::move(directory); }
void HtmlLayoutResult::setRoot(std::unique_ptr<HtmlBox> root) { root_ = std::move(root); }
void HtmlLayoutResult::setTextLayouts(std::vector<std::unique_ptr<HtmlTextLayout>> layouts) {
  textLayouts_ = std::move(layouts);
}
void HtmlLayoutResult::setSize(QSizeF size) { size_ = size; }
void HtmlLayoutResult::setError(QString error) { error_ = std::move(error); }

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

HtmlLayoutResult::HitResult HtmlLayoutResult::hitTestBox(const HtmlBox& box, QPointF localPos, QPointF origin) const {
  if (!box.style().visible) {
    return {};
  }

  const auto& geo = box.geometry();
  const QPointF boxOrigin = origin + QPointF(geo.left, geo.top);
  const QRectF boxRect(boxOrigin, QSizeF(geo.width, geo.height));
  if (!boxRect.contains(localPos)) {
    return {};
  }

  if (box.tag() == HtmlTag::Image) {
    return HitResult{QString(), box.src()};
  }

  if (box.ownsTextLayout()) {
    const QRectF contentRect = boxRect.marginsRemoved(box.style().borderWidth + box.style().padding);
    const QString href = linkHrefAtTextLayout(box, localPos - contentRect.topLeft());
    if (!href.isEmpty()) {
      return HitResult{href, QString()};
    }
  }

  for (const auto& child : box.children()) {
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
  const auto& style = box.style();
  const QPointF boxOrigin = origin + QPointF(geo.left, geo.top);
  const QRectF boxRect(boxOrigin, QSizeF(geo.width, geo.height));

  // Content area (inside border + padding)
  const QRectF contentRect = boxRect.marginsRemoved(style.borderWidth + style.padding);

  // Determine if we can use rounded-rect path for background + border
  const bool hasBorder = style.borderWidth.top() > 0 || style.borderWidth.bottom() > 0 ||
                         style.borderWidth.left() > 0 || style.borderWidth.right() > 0;
  const bool hasRoundedCorners = style.borderRadius > 0;

  if (hasRoundedCorners && (style.backgroundColor.isValid() || hasBorder)) {
    // Paint background + border using QPainterPath for rounded corners
    const QRectF bgRect = boxRect.marginsRemoved(style.borderWidth);
    const qreal r = style.borderRadius;
    QPainterPath path;
    path.addRoundedRect(bgRect, r, r);

    if (style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0) {
      painter.fillPath(path, style.backgroundColor);
    }
    if (hasBorder && style.borderStyle != HtmlBorderStyle::None) {
      QColor borderColor = style.borderColor.isValid() ? style.borderColor : QColor(204, 204, 204);
      Qt::PenStyle penStyle = Qt::SolidLine;
      if (style.borderStyle == HtmlBorderStyle::Dashed) penStyle = Qt::DashLine;
      else if (style.borderStyle == HtmlBorderStyle::Dotted) penStyle = Qt::DotLine;
      // Use uniform border width for the rounded-rect stroke
      qreal bw = (style.borderWidth.top() + style.borderWidth.bottom() +
                  style.borderWidth.left() + style.borderWidth.right()) / 4.0;
      painter.save();
      painter.setPen(QPen(borderColor, bw, penStyle));
      painter.drawPath(path);
      painter.restore();
    }
  } else {
    // Paint background (no rounded corners)
    if (style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0) {
      painter.fillRect(boxRect.marginsRemoved(style.borderWidth), style.backgroundColor);
    }

    // Paint border (no rounded corners)
    if (hasBorder && style.borderStyle != HtmlBorderStyle::None) {
      painter.save();
      QColor borderColor = style.borderColor.isValid() ? style.borderColor : QColor(204, 204, 204);
      Qt::PenStyle penStyle = Qt::SolidLine;
      if (style.borderStyle == HtmlBorderStyle::Dashed) penStyle = Qt::DashLine;
      else if (style.borderStyle == HtmlBorderStyle::Dotted) penStyle = Qt::DotLine;
      if (style.borderWidth.top() > 0) {
        painter.setPen(QPen(borderColor, style.borderWidth.top(), penStyle));
        painter.drawLine(boxRect.topLeft(), boxRect.topRight());
      }
      if (style.borderWidth.right() > 0) {
        painter.setPen(QPen(borderColor, style.borderWidth.right(), penStyle));
        painter.drawLine(boxRect.topRight(), boxRect.bottomRight());
      }
      if (style.borderWidth.bottom() > 0) {
        painter.setPen(QPen(borderColor, style.borderWidth.bottom(), penStyle));
        painter.drawLine(boxRect.bottomLeft(), boxRect.bottomRight());
      }
      if (style.borderWidth.left() > 0) {
        painter.setPen(QPen(borderColor, style.borderWidth.left(), penStyle));
        painter.drawLine(boxRect.topLeft(), boxRect.bottomLeft());
      }
      painter.restore();
    }
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

      for (const auto& child : box.children()) {
        paintBox(painter, *child, boxOrigin);
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
  paintKeyboardSpanRects(painter, *textLayout->layout, textLayout->formatSpans, origin);
  textLayout->layout->draw(&painter, origin);
  painter.restore();
}

void HtmlLayoutResult::paintListMarker(QPainter& painter, const HtmlBox& box, const QRectF& contentRect) const {
  painter.save();
  painter.setFont(box.style().font);
  painter.setPen(box.style().color.isValid() ? box.style().color : QColor(31, 35, 40));
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
  QColor color = box.style().borderColor.isValid() ? box.style().borderColor : QColor(204, 204, 204);
  painter.setPen(QPen(color, 1));
  const qreal y = contentRect.center().y();
  painter.drawLine(QPointF(contentRect.left(), y), QPointF(contentRect.right(), y));
  painter.restore();
}

void HtmlLayoutResult::paintImage(QPainter& painter, const HtmlBox& box, QPointF origin) const {
  if (box.src().isEmpty()) {
    // No src — draw placeholder with icon
    const auto& geo = box.geometry();
    const qreal w = geo.width > 0 ? geo.width : 100;
    const qreal h = geo.height > 0 ? geo.height : 80;
    painter.save();
    painter.setPen(QColor(204, 204, 204));
    painter.setBrush(QColor(248, 248, 248));
    painter.drawRect(QRectF(origin, QSizeF(w, h)));
    // Center a placeholder icon inside the box
    constexpr qreal kIconSize = 24.0;
    const QImage icon = image_placeholder::loading(QSizeF(kIconSize, kIconSize));
    if (!icon.isNull()) {
      const qreal ix = origin.x() + (w - kIconSize) / 2.0;
      const qreal iy = origin.y() + (h - kIconSize) / 2.0;
      painter.drawImage(QRectF(ix, iy, kIconSize, kIconSize), icon);
    } else {
      painter.setPen(QColor(150, 150, 150));
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
    const qreal w = geo.width > 0 ? geo.width : 100;
    const qreal h = geo.height > 0 ? geo.height : 80;
    painter.save();
    painter.setPen(QColor(204, 204, 204));
    painter.setBrush(QColor(248, 248, 248));
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

  const auto& geo = box.geometry();
  const qreal maxW = geo.width > 0 ? geo.width : image.width();
  const qreal maxH = geo.height > 0 ? geo.height : image.height();

  // Scale to fit
  qreal scale = qMin(maxW / image.width(), maxH / image.height());
  if (scale > 1.0) scale = 1.0;
  const qreal drawW = image.width() * scale;
  const qreal drawH = image.height() * scale;

  painter.save();
  painter.drawImage(QRectF(origin, QSizeF(drawW, drawH)), image);
  painter.restore();
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

  // Local file path — fall back to ImageDecoder for SVG, WebP, AVIF
  QImage img(src);
  if (img.isNull()) {
    img = image_decoder::decodeFileFallback(src);
  }
  it = imageCache_.insert(src, std::move(img));
  return it.value();
}

}  // namespace muffin::html
