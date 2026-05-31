#include "editor/SourceEditorWidget.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTextBlock>

namespace muffin {
namespace {

constexpr int kContentWidth = 800;
constexpr int kHorizontalInset = 64;

}  // namespace

SourceEditorWidget::SourceEditorWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 38, 0, 0);
  layout->setSpacing(0);

  editor_ = new QPlainTextEdit(this);
  editor_->setFrameShape(QFrame::NoFrame);
  editor_->setMinimumWidth(0);
  editor_->setPlaceholderText(tr("Start writing..."));
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

QString SourceEditorWidget::text() const {
  return editor_->toPlainText();
}

void SourceEditorWidget::setText(const QString& text) {
  const QSignalBlocker blocker(editor_);
  editor_->setPlainText(text);
  emitCursorPosition();
}

void SourceEditorWidget::setWordWrapEnabled(bool enabled) {
  editor_->setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
}

void SourceEditorWidget::setZoomPercent(int percent) {
  QFont font = editor_->font();
  font.setPointSizeF(qMax(8.0, 13.0 * percent / 100.0));
  editor_->setFont(font);
}

QPlainTextEdit* SourceEditorWidget::editor() {
  return editor_;
}

const QPlainTextEdit* SourceEditorWidget::editor() const {
  return editor_;
}

void SourceEditorWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateEditorWidth();
}

void SourceEditorWidget::setupStyle() {
  QFont font(QStringLiteral("Microsoft YaHei UI"));
  font.setPointSizeF(13.0);
  editor_->setFont(font);
  setStyleSheet(QStringLiteral(
      "SourceEditorWidget { background: #ffffff; }"
      "QPlainTextEdit {"
      "  background: #ffffff;"
      "  color: #222222;"
      "  selection-background-color: #cfe6ff;"
      "  padding: 0 0 56px 0;"
      "}"));
}

void SourceEditorWidget::updateEditorWidth() {
  if (!editor_) {
    return;
  }
  const int availableWidth = qMax(0, width() - kHorizontalInset * 2);
  const int targetWidth = qMin(kContentWidth, availableWidth > 0 ? availableWidth : kContentWidth);
  editor_->setFixedWidth(targetWidth);
}

void SourceEditorWidget::emitCursorPosition() {
  const QTextCursor cursor = editor_->textCursor();
  emit cursorPositionChanged(cursor.blockNumber() + 1, cursor.positionInBlock() + 1);
}

}  // namespace muffin
