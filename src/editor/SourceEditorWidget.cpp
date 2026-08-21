#include "editor/SourceEditorWidget.h"

#include "editor/VirtualSourceEdit.h"
#include "theme/RenderTheme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QResizeEvent>

namespace {

constexpr int kDefaultContentWidth = 860;
constexpr int kHorizontalInset = 64;

}  // namespace

muffin::SourceEditorWidget::SourceEditorWidget(QWidget* parent) : QWidget(parent) {
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 30, 0, 0);
  layout->setSpacing(0);

  editor_ = new VirtualSourceEdit(this);
  editor_->setMinimumWidth(0);
  setFocusProxy(editor_);
  layout->addStretch(1);
  layout->addWidget(editor_, 0);
  layout->addStretch(1);

  connect(editor_, &VirtualSourceEdit::cursorPositionChanged,
          this, &SourceEditorWidget::cursorPositionChanged);
  connect(editor_, &VirtualSourceEdit::editApplied,
          this, &SourceEditorWidget::editApplied);
  setupStyle();
  retranslateUi();
  updateEditorWidth();
}

void muffin::SourceEditorWidget::bindSession(DocumentSession* session) {
  editor_->bindSession(session);
}

void muffin::SourceEditorWidget::syncFromSession(bool preserveCursor) {
  editor_->syncFromSession(preserveCursor);
}

void muffin::SourceEditorWidget::notifyDocumentChanged() {
  editor_->notifyDocumentChanged();
}

QString muffin::SourceEditorWidget::text() const { return editor_->text(); }
void muffin::SourceEditorWidget::setText(const QString& text) { editor_->setStandaloneText(text); }
qsizetype muffin::SourceEditorWidget::cursorPosition() const { return editor_->cursorPosition(); }
qsizetype muffin::SourceEditorWidget::anchorPosition() const { return editor_->anchorPosition(); }
qsizetype muffin::SourceEditorWidget::selectionStart() const { return editor_->selectionStart(); }
qsizetype muffin::SourceEditorWidget::selectionEnd() const { return editor_->selectionEnd(); }
bool muffin::SourceEditorWidget::hasSelection() const { return editor_->hasSelection(); }
QString muffin::SourceEditorWidget::selectedText() const { return editor_->selectedText(); }
void muffin::SourceEditorWidget::setCursorPosition(qsizetype position, bool keepAnchor) { editor_->setCursorPosition(position, keepAnchor); }
void muffin::SourceEditorWidget::setSelection(qsizetype start, qsizetype end) { editor_->setSelection(start, end); }
void muffin::SourceEditorWidget::selectAll() { editor_->selectAll(); }
void muffin::SourceEditorWidget::selectLine() { editor_->selectLine(); }
void muffin::SourceEditorWidget::selectWord() { editor_->selectWord(); }
void muffin::SourceEditorWidget::insertText(const QString& text) { editor_->insertText(text); }
void muffin::SourceEditorWidget::replaceSelection(const QString& text) { editor_->replaceSelection(text); }
void muffin::SourceEditorWidget::deleteForward() { editor_->deleteForward(); }
void muffin::SourceEditorWidget::deleteBackward() { editor_->deleteBackward(); }
void muffin::SourceEditorWidget::deleteWord() { editor_->deleteWord(); }
void muffin::SourceEditorWidget::deleteLineContent() { editor_->deleteLineContent(); }
void muffin::SourceEditorWidget::deleteWholeLine() { editor_->deleteWholeLine(); }
void muffin::SourceEditorWidget::moveCurrentLineUp() { editor_->moveCurrentLineUp(); }
void muffin::SourceEditorWidget::moveCurrentLineDown() { editor_->moveCurrentLineDown(); }
bool muffin::SourceEditorWidget::canUndo() const { return editor_->canUndo(); }
bool muffin::SourceEditorWidget::canRedo() const { return editor_->canRedo(); }
void muffin::SourceEditorWidget::undo() { editor_->undo(); }
void muffin::SourceEditorWidget::redo() { editor_->redo(); }
void muffin::SourceEditorWidget::moveDocumentStart() { editor_->moveDocumentStart(); }
void muffin::SourceEditorWidget::moveDocumentEnd() { editor_->moveDocumentEnd(); }
void muffin::SourceEditorWidget::moveLineStart() { editor_->moveLineStart(); }
void muffin::SourceEditorWidget::moveLineEnd() { editor_->moveLineEnd(); }
void muffin::SourceEditorWidget::moveLineVertical(int delta) { editor_->moveLineVertical(delta); }
void muffin::SourceEditorWidget::selectNextOccurrence() { editor_->selectNextOccurrence(); }
qsizetype muffin::SourceEditorWidget::findText(QStringView text, qsizetype from) const { return editor_->findText(text, from); }
qsizetype muffin::SourceEditorWidget::findTextBackward(QStringView text, qsizetype from) const { return editor_->findTextBackward(text, from); }
void muffin::SourceEditorWidget::ensureCursorVisible() { editor_->ensureCursorVisible(); }
void muffin::SourceEditorWidget::centerCursor() { editor_->centerCursor(); }
int muffin::SourceEditorWidget::cursorLine() const { return editor_->cursorLine(); }
int muffin::SourceEditorWidget::cursorColumn() const { return editor_->cursorColumn(); }

void muffin::SourceEditorWidget::setDocumentPath(const QString& path) {
  editor_->setDocumentPath(path);
}

void muffin::SourceEditorWidget::setReadOnly(bool readOnly) {
  editor_->setReadOnly(readOnly);
}

bool muffin::SourceEditorWidget::isReadOnly() const { return editor_->isReadOnly(); }

void muffin::SourceEditorWidget::setWordWrapEnabled(bool enabled) {
  editor_->setWordWrapEnabled(enabled);
}

void muffin::SourceEditorWidget::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
  applyFontSize();
}

void muffin::SourceEditorWidget::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
  applyFontSize();
}

void muffin::SourceEditorWidget::setContentWidthPx(int px) {
  if (px == -1) {
    contentWidthPx_ = -1;
  } else if (px > 0) {
    contentWidthPx_ = qBound(640, px, 2400);
  } else {
    contentWidthPx_ = 0;
  }
  updateEditorWidth();
}

void muffin::SourceEditorWidget::setTheme(const RenderTheme& theme) {
  const SourceEditorColors colors = SourceEditorColors::fromTheme(theme);
  editor_->setColors(colors);
  setStyleSheet(QStringLiteral("muffin--SourceEditorWidget { background:%1; }")
                    .arg(colors.background.name(QColor::HexRgb)));
}

void muffin::SourceEditorWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateEditorWidth();
}

void muffin::SourceEditorWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslateUi();
  QWidget::changeEvent(event);
}

void muffin::SourceEditorWidget::setupStyle() {
  applyFontSize();
  setTheme(RenderTheme::github());
}

void muffin::SourceEditorWidget::retranslateUi() {
  editor_->setPlaceholderText(
      QCoreApplication::translate("muffin::SourceEditorWidget", "Start writing..."));
}

void muffin::SourceEditorWidget::applyFontSize() {
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setStyleHint(QFont::Monospace);
  font.setFixedPitch(true);
  const qreal scale = static_cast<qreal>(zoomPercent_) / 100.0 *
      static_cast<qreal>(fontSizePx_) / 16.0;
  font.setPointSizeF(qMax(8.0, 13.0 * scale));
  editor_->setSourceFont(font);
}

void muffin::SourceEditorWidget::updateEditorWidth() {
  if (!editor_) return;
  const int availableWidth = qMax(0, width() - kHorizontalInset * 2);
  const int preferredWidth = contentWidthPx_ == -1
      ? availableWidth
      : (contentWidthPx_ > 0 ? contentWidthPx_ : kDefaultContentWidth);
  const int targetWidth = qMin(preferredWidth,
                               availableWidth > 0 ? availableWidth : preferredWidth);
  editor_->setFixedWidth(targetWidth);
}
