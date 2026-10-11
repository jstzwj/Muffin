#include "render/TextLayout.h"

#include <QImage>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>

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
  if (backend == TextBackend::Fractional) {
    if (font.pixelSize() > 0) font.setPointSizeF(font.pixelSize() * .75);
    // Qt scales absolute letter spacing with font DPI, but word spacing is
    // already in device units. Keep this conversion at the shaping boundary.
    font.setWordSpacing(font.wordSpacing() * textBackendScale(backend));
  }
  return font;
}
QList<QTextLayout::FormatRange> deviceFormats(QList<QTextLayout::FormatRange> formats, const QFont& base, TextBackend backend) {
  if (backend == TextBackend::Native) return formats;
  for (auto& range : formats) {
    auto& format = range.format;
    if (format.hasProperty(QTextFormat::FontPixelSize)) {
      format.setFontPointSize(format.doubleProperty(QTextFormat::FontPixelSize) * .75);
      format.clearProperty(QTextFormat::FontPixelSize);
    }
    if (format.hasProperty(QTextFormat::FontWordSpacing))
      format.setFontWordSpacing(format.fontWordSpacing() * textBackendScale(backend));
    const auto spacingType = format.hasProperty(QTextFormat::FontLetterSpacingType) ? format.fontLetterSpacingType() : base.letterSpacingType();
    if (spacingType == QFont::AbsoluteSpacing && format.hasProperty(QTextFormat::FontLetterSpacing))
      format.setFontLetterSpacing(std::round(format.fontLetterSpacing() * 64) / 64);
  }
  return formats;
}
bool isCjkWordSpacingCharacter(QChar character) {
  switch (character.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Bopomofo:
      return true;
    default:
      return false;
  }
}
qreal formatValueAt(const QList<QTextLayout::FormatRange>& formats,
                    int position, QTextFormat::Property property,
                    qreal fallback) {
  qreal value = fallback;
  for (const auto& range : formats) {
    if (position < range.start || position >= range.start + range.length ||
        !range.format.hasProperty(property))
      continue;
    if (property == QTextFormat::FontWordSpacing)
      value = range.format.fontWordSpacing();
    else if (property == QTextFormat::FontLetterSpacing)
      value = range.format.fontLetterSpacing();
  }
  return value;
}
QList<QTextLayout::FormatRange> cjkWordSpacingFormats(
  const QString& text, const QFont& base,
    const QList<QTextLayout::FormatRange>& formats) {
  QList<QTextLayout::FormatRange> result;
  if (text.isEmpty()) return result;
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i) != QLatin1Char(' ') ||
        (i > 0 && text.at(i - 1) == QLatin1Char(' ')))
      continue;
    int end = i + 1;
    while (end < text.size() && text.at(end) == QLatin1Char(' ')) ++end;
    const int last = end - 1;
    const bool cjkAdjacent =
        (i > 0 && isCjkWordSpacingCharacter(text.at(i - 1))) ||
        (end < text.size() && isCjkWordSpacingCharacter(text.at(end)));
    if (!cjkAdjacent) continue;
    const qreal wordSpacing =
        formatValueAt(formats, last, QTextFormat::FontWordSpacing,
                      base.wordSpacing());
    if (wordSpacing == 0.0) continue;
    if (!qFuzzyIsNull(base.letterSpacing())) continue;
    const qreal letterSpacing =
        formatValueAt(formats, last, QTextFormat::FontLetterSpacing,
                      base.letterSpacing());
    QTextCharFormat correction;
    // Qt classifies spaces in Han/Hiragana/Katakana/Bopomofo shaping items as
    // inter-character opportunities, so QFont::wordSpacing is silently
    // omitted.  Add the missing amount to the final space in the run.  This
    // keeps the browser/CSS rule (one addition per collapsed space run) while
    // reusing Qt's normal glyph placement, caret and wrapping machinery.
    correction.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    correction.setFontLetterSpacing(letterSpacing + wordSpacing);
    result.append({last, 1, correction});
  }
  return result;
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
void setTextLetterSpacing(QFont& font, qreal pixels, TextBackend backend) {
  // QFont truncates to 1/64 logical px before Qt applies DPI. Round explicitly
  // for the experimental backend; keep the native compatibility contract.
  font.setLetterSpacing(QFont::AbsoluteSpacing, backend == TextBackend::Fractional ? std::round(pixels * 64) / 64 : pixels);
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
      layout_(std::make_unique<QTextLayout>(text, deviceFont(font, backend), shapingDevice(backend))) {
  // Apply the same spacing correction even when a caller has no explicit
  // character formats.  The default QTextLayout path otherwise bypasses
  // setFormats(), which is precisely where Han-script word spacing is fixed.
  setFormats({});
}
TextLayout::~TextLayout() = default;
void TextLayout::setTextOption(const QTextOption& option) {
  option_ = option;
  // Qt already applies the layout device's DPI to tab stops and absolute
  // letter spacing. Word spacing is converted separately in deviceFont.
  layout_->setTextOption(option);
}
void TextLayout::setFormats(const QList<FormatRange>& formats) {
  formats_ = formats;
  auto device = deviceFormats(formats, font_, backend_);
  if (backend_ == TextBackend::Fractional) {
    const auto corrections = cjkWordSpacingFormats(layout_->text(), font_, formats);
    const auto deviceCorrections = deviceFormats(corrections, font_, backend_);
    device.append(deviceCorrections);
  }
  layout_->setFormats(device);
}
QRectF TextLayout::boundingRect() const { return scaledRect(layout_->boundingRect(), 1 / scale_); }
void TextLayout::draw(QPainter* painter, QPointF origin, const QList<FormatRange>& selections, QRectF clip) const {
  if (scale_ == 1) {
    layout_->draw(painter, origin, selections, clip);
    return;
  }
  painter->save();
  painter->scale(1 / scale_, 1 / scale_);
  layout_->draw(painter, origin * scale_, deviceFormats(selections, font_, backend_), scaledRect(clip, scale_));
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
