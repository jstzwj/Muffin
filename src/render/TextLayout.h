#pragma once

#include <QFontMetricsF>
#include <QGlyphRun>
#include <QTextLayout>
#include <algorithm>
#include <limits>
#include <memory>

namespace muffin {

// Immutable for the lifetime of a process: changing a font device underneath a
// retained layout would invalidate its geometry and all dependent caches.
enum class TextBackend { Native, Fractional };
TextBackend documentTextBackend();
qreal textBackendScale(TextBackend backend);

// Every coordinate exposed here is in logical document pixels. No caller may
// observe the larger shaping device used by the experimental backend.
class TextLine {
 public:
  TextLine() = default;
  bool isValid() const { return line_.isValid(); }
  int textStart() const { return line_.textStart(); }
  int textLength() const { return line_.textLength(); }
  qreal x() const { return line_.x() / scale_; }
  qreal y() const { return line_.y() / scale_; }
  qreal ascent() const { return line_.ascent() / scale_; }
  qreal descent() const { return line_.descent() / scale_; }
  qreal height() const { return line_.height() / scale_; }
  qreal width() const { return line_.width() / scale_; }
  qreal naturalTextWidth() const { return line_.naturalTextWidth() / scale_; }
  qreal horizontalAdvance() const { return line_.horizontalAdvance() / scale_; }
  QRectF naturalTextRect() const;
  QRectF rect() const;
  QPointF position() const { return line_.position() / scale_; }
  void setPosition(QPointF position) { line_.setPosition(position * scale_); }
  void setLineWidth(qreal width) {
    // Qt stores dimensions in a signed 26.6 fixed-point number. A native
    // renderer's "infinite" width must not overflow after device scaling.
    constexpr qreal maximum = (std::numeric_limits<int>::max() - 64.) / 64.;
    line_.setLineWidth(std::clamp(width * scale_, qreal(0), maximum));
  }
  qreal cursorToX(int cursor, QTextLine::Edge edge = QTextLine::Leading) const { return line_.cursorToX(cursor, edge) / scale_; }
  int xToCursor(qreal x, QTextLine::CursorPosition mode = QTextLine::CursorBetweenCharacters) const {
    return line_.xToCursor(x * scale_, mode);
  }

 private:
  friend class TextLayout;
  TextLine(QTextLine line, qreal scale) : line_(line), scale_(scale) {}
  QTextLine line_;
  qreal scale_ = 1;
};

class TextLayout {
 public:
  using FormatRange = QTextLayout::FormatRange;
  TextLayout(const QString& text, const QFont& font, TextBackend backend = documentTextBackend());
  ~TextLayout();
  TextLayout(const TextLayout&) = delete;
  TextLayout& operator=(const TextLayout&) = delete;
  TextBackend backend() const { return backend_; }
  QString text() const { return layout_->text(); }
  QFont font() const { return font_; }
  void setCacheEnabled(bool enabled) { layout_->setCacheEnabled(enabled); }
  void setTextOption(const QTextOption& option);
  QTextOption textOption() const { return option_; }
  void setFormats(const QList<FormatRange>& formats);
  QList<FormatRange> formats() const { return formats_; }
  void beginLayout() { layout_->beginLayout(); }
  void endLayout() { layout_->endLayout(); }
  void clearLayout() { layout_->clearLayout(); }
  TextLine createLine() { return TextLine(layout_->createLine(), scale_); }
  TextLine lineAt(int i) const { return TextLine(layout_->lineAt(i), scale_); }
  TextLine lineForTextPosition(int pos) const { return TextLine(layout_->lineForTextPosition(pos), scale_); }
  int lineCount() const { return layout_->lineCount(); }
  QRectF boundingRect() const;
  bool isValidCursorPosition(int pos) const { return layout_->isValidCursorPosition(pos); }
  int nextCursorPosition(int pos, QTextLayout::CursorMode mode = QTextLayout::SkipCharacters) const {
    return layout_->nextCursorPosition(pos, mode);
  }
  int previousCursorPosition(int pos, QTextLayout::CursorMode mode = QTextLayout::SkipCharacters) const {
    return layout_->previousCursorPosition(pos, mode);
  }
  void draw(QPainter* painter, QPointF origin, const QList<FormatRange>& selections = {}, QRectF clip = {}) const;
  void drawCursor(QPainter* painter, QPointF origin, int pos, qreal width = 1) const;
  int hitTest(QPointF point, QTextLine::CursorPosition mode = QTextLine::CursorBetweenCharacters) const;
  QRectF cursorRect(int pos) const;
  QList<QRectF> selectionRects(int start, int end) const;
  // Diagnostics only. Positions and raw font sizes are in backend units.
  QList<QGlyphRun> backendGlyphRuns() const { return layout_->glyphRuns(); }

 private:
  TextBackend backend_;
  qreal scale_;
  QFont font_;
  QTextOption option_;
  QList<FormatRange> formats_;
  std::unique_ptr<QTextLayout> layout_;
};

// Metrics, placeholders and line boxes must use the same device as shaping.
class TextFontMetrics {
 public:
  explicit TextFontMetrics(const QFont& font, TextBackend backend = documentTextBackend());
  qreal ascent() const { return metrics_.ascent() / scale_; }
  qreal descent() const { return metrics_.descent() / scale_; }
  qreal height() const { return metrics_.height() / scale_; }
  qreal leading() const { return metrics_.leading() / scale_; }
  qreal lineSpacing() const { return metrics_.lineSpacing() / scale_; }
  qreal xHeight() const { return metrics_.xHeight() / scale_; }
  qreal averageCharWidth() const { return metrics_.averageCharWidth() / scale_; }
  qreal horizontalAdvance(const QString& text) const { return metrics_.horizontalAdvance(text) / scale_; }
  qreal horizontalAdvance(QChar text) const { return metrics_.horizontalAdvance(text) / scale_; }
  QRectF boundingRect(const QString& text) const;
  QRectF boundingRect(QRectF rect, int flags, const QString& text) const;
  QString elidedText(const QString& text, Qt::TextElideMode mode, qreal width) const {
    return metrics_.elidedText(text, mode, width * scale_);
  }

 private:
  qreal scale_;
  QFontMetricsF metrics_;
};

// Baseline-positioned text (markers and small labels) uses the same shaper as
// document runs; QPainter::drawText would silently reintroduce native metrics.
void drawDocumentText(QPainter& painter, QPointF baseline, const QString& text);
void drawDocumentText(QPainter& painter, QRectF rect, const QString& text, const QTextOption& option);
void drawDocumentText(QPainter& painter, QRectF rect, int flags, const QString& text);

}  // namespace muffin
