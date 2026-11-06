#include "editor/SourceEditorWidget.h"

#include "editor/VirtualSourceEdit.h"
#include "theme/FontRendering.h"
#include "theme/RenderTheme.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>

muffin::SourceEditorWidget::SourceEditorWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_StyledBackground, true);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 16, 0, 0);
  layout->setSpacing(0);

  editor_ = new VirtualSourceEdit(this);
  editor_->setMinimumWidth(0);
  setFocusProxy(editor_);
  // Source editing uses the available workspace, independently of the
  // rendered document's reading width. Keep its scrollbar at the outer edge.
  layout->addWidget(editor_, 1);

  connect(editor_, &VirtualSourceEdit::cursorPositionChanged, this, &SourceEditorWidget::cursorPositionChanged);
  connect(editor_, &VirtualSourceEdit::editApplied, this, &SourceEditorWidget::editApplied);
  setupStyle();
  retranslateUi();
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

QString muffin::SourceEditorWidget::text() const {
  return editor_->text();
}
void muffin::SourceEditorWidget::setText(const QString& text) {
  editor_->setStandaloneText(text);
}
qsizetype muffin::SourceEditorWidget::cursorPosition() const {
  return editor_->cursorPosition();
}
qsizetype muffin::SourceEditorWidget::anchorPosition() const {
  return editor_->anchorPosition();
}
qsizetype muffin::SourceEditorWidget::selectionStart() const {
  return editor_->selectionStart();
}
qsizetype muffin::SourceEditorWidget::selectionEnd() const {
  return editor_->selectionEnd();
}
bool muffin::SourceEditorWidget::hasSelection() const {
  return editor_->hasSelection();
}
void muffin::SourceEditorWidget::toggleInlineMarkup(const QString& prefix, const QString& suffix) {
  editor_->toggleInlineMarkup(prefix, suffix);
}

QString muffin::SourceEditorWidget::selectedText() const {
  return editor_->selectedText();
}
void muffin::SourceEditorWidget::setCursorPosition(qsizetype position, bool keepAnchor) {
  editor_->setCursorPosition(position, keepAnchor);
}
void muffin::SourceEditorWidget::setSelection(qsizetype start, qsizetype end) {
  editor_->setSelection(start, end);
}
void muffin::SourceEditorWidget::selectAll() {
  editor_->selectAll();
}
void muffin::SourceEditorWidget::selectLine() {
  editor_->selectLine();
}
void muffin::SourceEditorWidget::selectWord() {
  editor_->selectWord();
}
void muffin::SourceEditorWidget::insertText(const QString& text) {
  editor_->insertText(text);
}
void muffin::SourceEditorWidget::replaceAllWithUndo(const QString& text) {
  editor_->replaceAllWithUndo(text);
}
void muffin::SourceEditorWidget::replaceSelection(const QString& text) {
  editor_->replaceSelection(text);
}
void muffin::SourceEditorWidget::deleteForward() {
  editor_->deleteForward();
}
void muffin::SourceEditorWidget::deleteBackward() {
  editor_->deleteBackward();
}
void muffin::SourceEditorWidget::deleteWord() {
  editor_->deleteWord();
}
void muffin::SourceEditorWidget::deleteLineContent() {
  editor_->deleteLineContent();
}
void muffin::SourceEditorWidget::deleteWholeLine() {
  editor_->deleteWholeLine();
}
void muffin::SourceEditorWidget::moveCurrentLineUp() {
  editor_->moveCurrentLineUp();
}
void muffin::SourceEditorWidget::moveCurrentLineDown() {
  editor_->moveCurrentLineDown();
}
bool muffin::SourceEditorWidget::canUndo() const {
  return editor_->canUndo();
}
bool muffin::SourceEditorWidget::canRedo() const {
  return editor_->canRedo();
}
void muffin::SourceEditorWidget::undo() {
  editor_->undo();
}
void muffin::SourceEditorWidget::redo() {
  editor_->redo();
}
void muffin::SourceEditorWidget::moveDocumentStart() {
  editor_->moveDocumentStart();
}
void muffin::SourceEditorWidget::moveDocumentEnd() {
  editor_->moveDocumentEnd();
}
void muffin::SourceEditorWidget::moveLineStart() {
  editor_->moveLineStart();
}
void muffin::SourceEditorWidget::moveLineEnd() {
  editor_->moveLineEnd();
}
void muffin::SourceEditorWidget::moveLineVertical(int delta) {
  editor_->moveLineVertical(delta);
}
void muffin::SourceEditorWidget::selectNextOccurrence() {
  editor_->selectNextOccurrence();
}
qsizetype muffin::SourceEditorWidget::findText(QStringView text, qsizetype from) const {
  return editor_->findText(text, from);
}
qsizetype muffin::SourceEditorWidget::findTextBackward(QStringView text, qsizetype from) const {
  return editor_->findTextBackward(text, from);
}
void muffin::SourceEditorWidget::ensureCursorVisible() {
  editor_->ensureCursorVisible();
}
void muffin::SourceEditorWidget::centerCursor() {
  editor_->centerCursor();
}

void muffin::SourceEditorWidget::setFocusMode(bool enabled) {
  editor_->setFocusMode(enabled);
}

void muffin::SourceEditorWidget::setTypewriterMode(bool enabled, bool cursorMiddle) {
  editor_->setTypewriterMode(enabled, cursorMiddle);
}
muffin::ViewportPosition muffin::SourceEditorWidget::viewportPosition() const {
  return editor_->viewportPosition();
}
void muffin::SourceEditorWidget::restoreViewportPosition(const ViewportPosition& position) {
  editor_->restoreViewportPosition(position);
}
int muffin::SourceEditorWidget::cursorLine() const {
  return editor_->cursorLine();
}
int muffin::SourceEditorWidget::cursorColumn() const {
  return editor_->cursorColumn();
}

void muffin::SourceEditorWidget::setDocumentPath(const QString& path) {
  editor_->setDocumentPath(path);
}

void muffin::SourceEditorWidget::setReadOnly(bool readOnly) {
  editor_->setReadOnly(readOnly);
}

bool muffin::SourceEditorWidget::isReadOnly() const {
  return editor_->isReadOnly();
}

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

void muffin::SourceEditorWidget::setTheme(const RenderTheme& theme) {
  typography_ = theme.typographyOverrides();
  auto snapshot = theme.detachedPrototype();
  snapshot.setZoomPercent(100);
  snapshot.updateForViewport(qMax(1, width()));
  themeCodeFont_ = snapshot.codeFont();
  if (themeCodeFont_.pointSizeF() > 0)
    themeCodeFont_.setPointSizeF(themeCodeFont_.pointSizeF() * 16.0 / snapshot.fontSizePx());
  applyFontSize();
  const SourceEditorColors colors = SourceEditorColors::fromTheme(theme);
  editor_->setColors(colors);
  setStyleSheet(
      QStringLiteral("muffin--SourceEditorWidget { background:%1; }").arg(colors.background.name(QColor::HexRgb)));
}

void muffin::SourceEditorWidget::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange)
    retranslateUi();
  QWidget::changeEvent(event);
}

void muffin::SourceEditorWidget::setupStyle() {
  applyFontSize();
  setTheme(RenderTheme::github());
}

void muffin::SourceEditorWidget::retranslateUi() {
  editor_->setPlaceholderText(QCoreApplication::translate("muffin::SourceEditorWidget", "Start writing..."));
}

void muffin::SourceEditorWidget::applyFontSize() {
  QFont font = themeCodeFont_.pointSizeF() > 0 ? themeCodeFont_ : font_rendering::sourceEditorFont();
  const qreal scale = static_cast<qreal>(zoomPercent_) / 100.0 * static_cast<qreal>(fontSizePx_) / 16.0;
  font.setPointSizeF(qMax(.001, font.pointSizeF() * scale));
  if (typography_.codeSize || typography_.sourceSize)
    font.setPointSizeF((typography_.sourceSize ? typography_.sourceSize : typography_.codeSize) * .75 * scale);
  editor_->setSourceFont(font);
}
