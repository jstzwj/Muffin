#include "render/TextLayout.h"

#include <QImage>
#include <QPainter>
#include <QTransform>
#include <algorithm>

namespace muffin {
namespace {
QPaintDevice* shapingDevice(TextBackend backend) {
  if (backend == TextBackend::Native) return nullptr;
  // Shared immutable device, rather than allocating an image for every metric
  // query. It outlives all layouts and is never used as a painting destination.
  // Retained static layouts can be destroyed after function-local statics.
  // This tiny process-lifetime device must survive those layout destructors.
  static QImage* device = [] {
    auto* result = new QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
    result->setDotsPerMeterX(qRound(96 * textBackendScale(TextBackend::Fractional) / .0254));
    result->setDotsPerMeterY(result->dotsPerMeterX());
    return result;
  }();
  return device;
}
QFont deviceFont(QFont font, TextBackend backend) {
  // Pixel fonts ignore device DPI. Convert to points only on the fractional
  // device, preserving the old backend and the author's logical pixel size.
  if (backend == TextBackend::Fractional && font.pixelSize() > 0) font.setPointSizeF(font.pixelSize() * .75);
  return font;
}
QRectF scaledRect(QRectF rect, qreal factor) { return QTransform::fromScale(factor, factor).mapRect(rect); }
}  // namespace

TextBackend documentTextBackend() {
  static const auto backend = qgetenv("MUFFIN_TEXT_LAYOUT_BACKEND") == "fractional" ? TextBackend::Fractional : TextBackend::Native;
  return backend;
}
qreal textBackendScale(TextBackend backend) { return backend == TextBackend::Fractional ? 64 : 1; }
void setTextPixelSize(QFont& font, qreal pixels, TextBackend backend) {
  if (backend == TextBackend::Fractional)
    font.setPointSizeF(std::max<qreal>(pixels, 1. / 64.) * .75);
  else
    font.setPixelSize(std::max(1, qRound(pixels)));
}
qreal textFontPixelSize(const QFont& font) { return font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() / .75; }
QRectF TextLine::naturalTextRect() const { return scaledRect(line_.naturalTextRect(), 1 / scale_); }
QRectF TextLine::rect() const { return scaledRect(line_.rect(), 1 / scale_); }
QList<QGlyphRun> TextLine::glyphRuns(int from, int length, QTextLayout::GlyphRunRetrievalFlags flags) const {
  auto runs = line_.glyphRuns(from, length, flags);
  if (scale_ == 1) return runs;
  for (auto& run : runs) {
    const QRectF bounds = scaledRect(run.boundingRect(), 1 / scale_);
    auto positions = run.positions();
    for (auto& position : positions) position /= scale_;
    auto font = run.rawFont();
    // QRawFont accepts qreal pixel sizes; unlike QFont::setPixelSize this
    // retains the fractional size when callers paint prepared glyphs/paths.
    font.setPixelSize(font.pixelSize() / scale_);
    run.setRawFont(font);
    run.setPositions(positions);
    run.setBoundingRect(bounds);
  }
  return runs;
}

TextLayout::TextLayout(const QString& text, const QFont& font, TextBackend backend)
    : backend_(backend),
      scale_(textBackendScale(backend)),
      font_(font),
      layout_(std::make_unique<QTextLayout>(text, deviceFont(font, backend), shapingDevice(backend))) {}
TextLayout::~TextLayout() = default;
void TextLayout::setTextOption(const QTextOption& option) {
  option_ = option;
  // Qt already applies the layout device's DPI to tab stops, just as it does
  // to absolute letter/word spacing. Multiplying here would scale them twice.
  layout_->setTextOption(option);
}
void TextLayout::setFormats(const QList<FormatRange>& formats) {
  formats_ = formats;
  auto deviceFormats = formats;
  if (scale_ != 1) {
    for (auto& range : deviceFormats) {
      if (range.format.hasProperty(QTextFormat::FontPixelSize)) {
        range.format.setFontPointSize(range.format.intProperty(QTextFormat::FontPixelSize) * .75);
        range.format.clearProperty(QTextFormat::FontPixelSize);
      }
    }
  }
  layout_->setFormats(deviceFormats);
}
QRectF TextLayout::boundingRect() const { return scaledRect(layout_->boundingRect(), 1 / scale_); }
void TextLayout::draw(QPainter* painter, QPointF origin, const QList<FormatRange>& selections, QRectF clip) const {
  if (scale_ == 1) {
    layout_->draw(painter, origin, selections, clip);
    return;
  }
  painter->save();
  painter->scale(1 / scale_, 1 / scale_);
  layout_->draw(painter, origin * scale_, selections, scaledRect(clip, scale_));
  painter->restore();
}
int TextLayout::hitTest(QPointF point, QTextLine::CursorPosition mode) const {
  if (!lineCount()) return 0;
  for (int i = 0; i < lineCount(); ++i) {
    const auto line = lineAt(i);
    if (point.y() < line.y() + line.height() || i + 1 == lineCount()) return line.xToCursor(point.x(), mode);
  }
  return 0;
}
void TextLayout::drawCursor(QPainter* painter, QPointF origin, int pos, qreal width) const {
  if (scale_ == 1) {
    layout_->drawCursor(painter, origin, pos, int(width));
    return;
  }
  auto rect = cursorRect(pos).translated(origin);
  rect.setWidth(width);
  painter->fillRect(rect, painter->pen().color());
}
QRectF TextLayout::cursorRect(int pos) const {
  if (!lineCount()) return {};
  pos = std::clamp(pos, 0, int(text().size()));
  for (int i = 0; i < lineCount(); ++i) {
    const auto line = lineAt(i);
    if (pos < line.textStart() + line.textLength() || i + 1 == lineCount()) return {line.cursorToX(pos), line.y(), 1, line.height()};
  }
  return {};
}
QList<QRectF> TextLayout::selectionRects(int start, int end) const {
  QList<QRectF> result;
  if (start > end) std::swap(start, end);
  for (int i = 0; i < lineCount(); ++i) {
    const auto line = lineAt(i);
    const int first = std::max(start, line.textStart()), last = std::min(end, line.textStart() + line.textLength());
    if (first >= last) continue;
    const qreal x1 = line.cursorToX(first), x2 = line.cursorToX(last);
    result.append({std::min(x1, x2), line.y(), std::abs(x2 - x1), line.height()});
  }
  return result;
}
TextFontMetrics::TextFontMetrics(const QFont& font, TextBackend backend)
    : scale_(textBackendScale(backend)), metrics_(deviceFont(font, backend), shapingDevice(backend)) {}
QRectF TextFontMetrics::boundingRect(const QString& text) const { return scaledRect(metrics_.boundingRect(text), 1 / scale_); }
QRectF TextFontMetrics::tightBoundingRect(const QString& text) const { return scaledRect(metrics_.tightBoundingRect(text), 1 / scale_); }
QRectF TextFontMetrics::boundingRect(QRectF rect, int flags, const QString& text) const {
  return scaledRect(metrics_.boundingRect(scaledRect(rect, scale_), flags, text), 1 / scale_);
}
void drawDocumentText(QPainter& painter, QPointF baseline, const QString& text) {
  if (documentTextBackend() == TextBackend::Native) {
    painter.drawText(baseline, text);
    return;
  }
  TextLayout layout(text, painter.font());
  layout.beginLayout();
  auto line = layout.createLine();
  if (line.isValid()) {
    line.setLineWidth(1e6);
    line.setPosition({0, -line.ascent()});
  }
  layout.endLayout();
  layout.draw(&painter, baseline);
}
void drawDocumentText(QPainter& painter, QRectF rect, const QString& text, const QTextOption& option) {
  if (documentTextBackend() == TextBackend::Native) {
    painter.drawText(rect, text, option);
    return;
  }
  TextLayout layout(text, painter.font());
  layout.setTextOption(option);
  layout.beginLayout();
  qreal height = 0;
  for (;;) {
    auto line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(std::max<qreal>(0, rect.width()));
    line.setPosition({0, height});
    height += line.height();
  }
  layout.endLayout();
  QPointF origin = rect.topLeft();
  if (option.alignment() & Qt::AlignVCenter)
    origin.ry() += (rect.height() - height) / 2;
  else if (option.alignment() & Qt::AlignBottom)
    origin.ry() += rect.height() - height;
  layout.draw(&painter, origin);
}
void drawDocumentText(QPainter& painter, QRectF rect, int flags, const QString& text) {
  if (documentTextBackend() == TextBackend::Native) {
    painter.drawText(rect, flags, text);
    return;
  }
  QTextOption option;
  option.setAlignment(Qt::Alignment(flags & int(Qt::AlignHorizontal_Mask | Qt::AlignVertical_Mask)));
  option.setWrapMode(flags & Qt::TextWordWrap ? QTextOption::WordWrap : QTextOption::NoWrap);
  drawDocumentText(painter, rect, text, option);
}
}  // namespace muffin
