#include "editor/SourceEditorWidget.h"

#include "theme/RenderTheme.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QPainter>
#include <QFont>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPaintEvent>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <cmath>
#include <utility>

namespace {

constexpr int kContentWidth = 860;
constexpr int kHorizontalInset = 64;
constexpr int kLineNumberWidth = 48;
constexpr qreal kSourceLineSpacingScale = 1.7;
constexpr QChar kZeroWidthSpace(0x200b);

QTextCharFormat sourceFormat(QColor foreground, bool bold = false, double pointSize = 0.0) {
  QTextCharFormat result;
  result.setForeground(foreground);
  if (bold) {
    result.setFontWeight(QFont::Bold);
  }
  if (pointSize > 0.0) {
    result.setFontPointSize(pointSize);
  }
  return result;
}

class MarkdownSourceHighlighter final : public QSyntaxHighlighter {
public:
  explicit MarkdownSourceHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {
    rebuildFormats();
  }

  void setBasePointSize(double pointSize) {
    basePointSize_ = pointSize;
    rebuildFormats();
    rehighlight();
  }

protected:
  void highlightBlock(const QString& text) override {
    if (text.startsWith(QStringLiteral("```")) || text.startsWith(QStringLiteral("~~~"))) {
      setFormat(0, text.size(), fenceFormat_);
      applyZeroWidthSpaces(text);
      return;
    }

    static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})(\\s.*)?$"));
    QRegularExpressionMatch heading = headingRe.match(text);
    if (heading.hasMatch()) {
      const int level = qBound(1, heading.capturedLength(1), 6);
      setFormat(0, text.size(), headingFormats_[level - 1]);
      applyZeroWidthSpaces(text);
      return;
    }

    static const QRegularExpression quoteRe(QStringLiteral("^\\s*>+"));
    QRegularExpressionMatch quote = quoteRe.match(text);
    if (quote.hasMatch()) {
      setFormat(quote.capturedStart(0), quote.capturedLength(0), quoteFormat_);
    }

    static const QRegularExpression listRe(QStringLiteral("^\\s*(?:[-+*]|\\d+[.)])\\s+"));
    QRegularExpressionMatch list = listRe.match(text);
    if (list.hasMatch()) {
      setFormat(list.capturedStart(0), list.capturedLength(0), markerFormat_);
    }

    static const QRegularExpression codeRe(QStringLiteral("`[^`]*`"));
    applyRegex(text, codeRe, codeFormat_);
    applyLinks(text);
    static const QRegularExpression emphasisRe(QStringLiteral("(\\*\\*|__|\\*|_|~~)"));
    applyRegex(text, emphasisRe, emphasisFormat_);
    static const QRegularExpression tableRe(QStringLiteral("\\|"));
    applyRegex(text, tableRe, tableFormat_);
    applyZeroWidthSpaces(text);
  }

private:
  void rebuildFormats() {
    const QColor headingColor(QStringLiteral("#e34f8b"));
    headingFormats_[0] = sourceFormat(headingColor, true, basePointSize_ * 1.78);
    headingFormats_[1] = sourceFormat(headingColor, true, basePointSize_ * 1.36);
    headingFormats_[2] = sourceFormat(headingColor, true, basePointSize_ * 1.18);
    headingFormats_[3] = sourceFormat(headingColor, true, basePointSize_);
    headingFormats_[4] = sourceFormat(headingColor, true, basePointSize_);
    headingFormats_[5] = sourceFormat(headingColor, true, basePointSize_);
    markerFormat_ = sourceFormat(QColor(QStringLiteral("#c27a00")));
    linkLabelFormat_ = sourceFormat(QColor(QStringLiteral("#c77700")));
    linkTargetFormat_ = sourceFormat(QColor(QStringLiteral("#6f7fbf")));
    codeFormat_ = sourceFormat(QColor(QStringLiteral("#1f2328")));
    codeFormat_.setBackground(QColor(QStringLiteral("#eef2f7")));
    fenceFormat_ = sourceFormat(QColor(QStringLiteral("#8a5a00")));
    quoteFormat_ = sourceFormat(QColor(QStringLiteral("#7a7a7a")));
    emphasisFormat_ = sourceFormat(QColor(QStringLiteral("#a00070")));
    tableFormat_ = sourceFormat(QColor(QStringLiteral("#1a60a8")));
    zeroWidthFormat_ = sourceFormat(QColor(QStringLiteral("#d14f7f")), true);
    zeroWidthFormat_.setBackground(QColor(QStringLiteral("#fff0f6")));
  }

  void applyZeroWidthSpaces(const QString& text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
      if (text.at(i) == kZeroWidthSpace) {
        setFormat(static_cast<int>(i), 1, zeroWidthFormat_);
      }
    }
  }

private:
  void applyRegex(const QString& text, const QRegularExpression& regex, const QTextCharFormat& textFormat) {
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), textFormat);
    }
  }

  void applyLinks(const QString& text) {
    static const QRegularExpression linkRe(QStringLiteral("!?\\[([^\\]]*)\\]\\(([^\\)]*)\\)"));
    QRegularExpressionMatchIterator it = linkRe.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), linkTargetFormat_);
      if (match.capturedStart(1) >= 0) {
        setFormat(match.capturedStart(1) - 1, match.capturedLength(1) + 2, linkLabelFormat_);
      }
      if (match.capturedStart(2) >= 0) {
        setFormat(match.capturedStart(2), match.capturedLength(2), linkTargetFormat_);
      }
    }
  }

  double basePointSize_ = 12.5;
  QTextCharFormat headingFormats_[6];
  QTextCharFormat markerFormat_;
  QTextCharFormat linkLabelFormat_;
  QTextCharFormat linkTargetFormat_;
  QTextCharFormat codeFormat_;
  QTextCharFormat fenceFormat_;
  QTextCharFormat quoteFormat_;
  QTextCharFormat emphasisFormat_;
  QTextCharFormat tableFormat_;
  QTextCharFormat zeroWidthFormat_;
};

}  // namespace

namespace muffin {

class LineNumberArea final : public QWidget {
public:
  explicit LineNumberArea(MarkdownSourceEdit* editor);

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  MarkdownSourceEdit* editor_ = nullptr;
};

class MarkdownSourceEdit final : public QPlainTextEdit {
public:
  explicit MarkdownSourceEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
    lineNumberFont_ = font();
    lineNumberFont_.setPointSizeF(10.0);
    setBackgroundVisible(false);
    document()->setDocumentMargin(0.0);
    highlighter_ = new MarkdownSourceHighlighter(document());
    lineNumberArea_ = new LineNumberArea(this);
    updateLineNumberAreaWidth();
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
      updateLineNumberAreaWidth();
      lineNumberArea_->update();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, &MarkdownSourceEdit::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
      updateCurrentLineSelection();
      viewport()->update();
      lineNumberArea_->update();
    });
  }

  void setTheme(RenderTheme theme) {
    theme_ = std::move(theme);
    updateCurrentLineSelection();
    viewport()->update();
  }

  void setLineNumberFont(QFont font) {
    lineNumberFont_ = std::move(font);
    lineNumberArea_->update();
  }

  void setSourceFont(QFont font) {
    setFont(font);
    if (highlighter_) {
      highlighter_->setBasePointSize(font.pointSizeF());
    }
    setLineSpacingForFont(font);
  }

  int lineNumberAreaWidth() const {
    return kLineNumberWidth;
  }

  void paintLineNumberArea(QPaintEvent* event) {
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), palette().base());
    painter.setFont(lineNumberFont_);
    painter.setPen(QColor(QStringLiteral("#c9cdd3")));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    while (block.isValid()) {
      const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
      if (blockRect.top() > viewport()->height()) {
        break;
      }
      if (blockRect.bottom() >= 0) {
        const int visibleLineNumber = blockNumber + 1;
        painter.drawText(QRectF(0, blockRect.top(), lineNumberAreaWidth() - 14, blockRect.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(visibleLineNumber));
      }
      block = block.next();
      ++blockNumber;
    }
  }

private:
  void updateCurrentLineSelection() {
    QTextEdit::ExtraSelection currentLine;
    currentLine.format.setBackground(QColor(QStringLiteral("#f3f3f3")));
    currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
    currentLine.cursor = textCursor();
    currentLine.cursor.clearSelection();
    setExtraSelections({currentLine});
  }

  void setLineSpacingForFont(const QFont& font) {
    const qreal lineHeight = qMax<qreal>(14.0, std::ceil(QFontMetricsF(font).lineSpacing() * kSourceLineSpacingScale));
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(lineHeight, QTextBlockFormat::MinimumHeight);
    QTextCursor cursor(document());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(blockFormat);
  }

protected:
  void resizeEvent(QResizeEvent* event) override {
    QPlainTextEdit::resizeEvent(event);
    const QRect contentRect = contentsRect();
    lineNumberArea_->setGeometry(QRect(contentRect.left(), contentRect.top(), lineNumberAreaWidth(), contentRect.height()));
  }

  void paintEvent(QPaintEvent* event) override {
    QPlainTextEdit::paintEvent(event);
    QPainter painter(viewport());
    paintZeroWidthSpaces(painter);
  }

  void paintZeroWidthSpaces(QPainter& painter) {
    painter.save();
    painter.setPen(QColor(QStringLiteral("#d14f7f")));
    painter.setBrush(QColor(QStringLiteral("#fff0f6")));
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
      const QRectF blockRect = blockBoundingGeometry(block).translated(contentOffset());
      if (blockRect.top() > viewport()->height()) {
        break;
      }
      const QString text = block.text();
      for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != kZeroWidthSpace) {
          continue;
        }
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + static_cast<int>(i));
        const QRect rect = cursorRect(cursor);
        const QRectF marker(rect.left() + 1.0, rect.center().y() - 5.0, 10.0, 10.0);
        painter.drawRoundedRect(marker, 3.0, 3.0);
        painter.drawText(marker, Qt::AlignCenter, QStringLiteral("Z"));
      }
      block = block.next();
    }
    painter.restore();
  }

  void updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  }

  void updateLineNumberArea(const QRect& rect, int dy) {
    if (dy != 0) {
      lineNumberArea_->scroll(0, dy);
    } else {
      lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) {
      updateLineNumberAreaWidth();
    }
  }

  RenderTheme theme_ = RenderTheme::github();
  MarkdownSourceHighlighter* highlighter_ = nullptr;
  LineNumberArea* lineNumberArea_ = nullptr;
  QFont lineNumberFont_;
};

LineNumberArea::LineNumberArea(MarkdownSourceEdit* editor) : QWidget(editor), editor_(editor) {
}

QSize LineNumberArea::sizeHint() const {
  return QSize(editor_->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
  editor_->paintLineNumberArea(event);
}

}  // namespace muffin

muffin::SourceEditorWidget::SourceEditorWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 30, 0, 0);
  layout->setSpacing(0);

  editor_ = new MarkdownSourceEdit(this);
  editor_->setFrameShape(QFrame::NoFrame);
  editor_->setMinimumWidth(0);
  retranslateUi();
  editor_->setTabStopDistance(32);
  editor_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  layout->addStretch(1);
  layout->addWidget(editor_, 0);
  layout->addStretch(1);

  setupStyle();
  updateEditorWidth();

  connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
    emit textEdited(editor_->toPlainText());
  });
  connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &SourceEditorWidget::emitCursorPosition);
}

QString muffin::SourceEditorWidget::text() const {
  return editor_->toPlainText();
}

void muffin::SourceEditorWidget::setText(const QString& text) {
  const QSignalBlocker blocker(editor_);
  editor_->setPlainText(text);
  editor_->setSourceFont(editor_->font());
  emitCursorPosition();
}

void muffin::SourceEditorWidget::setWordWrapEnabled(bool enabled) {
  editor_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void muffin::SourceEditorWidget::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
  applyFontSize();
}

void muffin::SourceEditorWidget::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
  applyFontSize();
}

void muffin::SourceEditorWidget::setTheme(const RenderTheme& theme) {
  setStyleSheet(QStringLiteral(
                    "SourceEditorWidget { background:%1; }"
                    "QPlainTextEdit {"
                    "  background:#ffffff;"
                    "  color:%2;"
                    "  selection-background-color:%3;"
                    "  padding:0 0 56px 0;"
                    "}")
                    .arg(
                        theme.backgroundColor().name(QColor::HexRgb),
                        theme.textColor().name(QColor::HexRgb),
                        theme.selectionColor().name(QColor::HexRgb)));
}

QPlainTextEdit* muffin::SourceEditorWidget::editor() {
  return editor_;
}

const QPlainTextEdit* muffin::SourceEditorWidget::editor() const {
  return editor_;
}

void muffin::SourceEditorWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateEditorWidth();
}

void muffin::SourceEditorWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QWidget::changeEvent(event);
}

void muffin::SourceEditorWidget::setupStyle() {
  applyFontSize();
  setTheme(RenderTheme::github());
}

void muffin::SourceEditorWidget::retranslateUi() {
  if (editor_) {
    editor_->setPlaceholderText(QCoreApplication::translate("muffin::SourceEditorWidget", "Start writing..."));
  }
}

void muffin::SourceEditorWidget::applyFontSize() {
  const QSignalBlocker blocker(editor_);
  QFont font = editor_->font();
  const qreal scale = static_cast<qreal>(zoomPercent_) / 100.0 * static_cast<qreal>(fontSizePx_) / 16.0;
  font.setPointSizeF(qMax(8.0, 13.0 * scale));
  editor_->setSourceFont(font);
  QFont lineNumberFont = font;
  lineNumberFont.setPointSizeF(qMax(8.0, 10.0 * scale));
  editor_->setLineNumberFont(lineNumberFont);
}

void muffin::SourceEditorWidget::updateEditorWidth() {
  if (!editor_) {
    return;
  }
  const int availableWidth = qMax(0, width() - kHorizontalInset * 2);
  const int targetWidth = qMin(kContentWidth, availableWidth > 0 ? availableWidth : kContentWidth);
  editor_->setFixedWidth(targetWidth);
}

void muffin::SourceEditorWidget::emitCursorPosition() {
  const QTextCursor cursor = editor_->textCursor();
  emit cursorPositionChanged(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
}
