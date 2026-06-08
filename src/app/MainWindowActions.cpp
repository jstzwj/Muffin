#include "app/MainWindow.h"

#include "app/MainWindowActionBinder.h"
#include "app/MainWindowSignalBinder.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>

namespace {

QString zoneName(muffin::HitTestResult::Zone zone) {
  switch (zone) {
    case muffin::HitTestResult::Zone::Text:
      return QStringLiteral("text");
    case muffin::HitTestResult::Zone::Marker:
      return QStringLiteral("marker");
    case muffin::HitTestResult::Zone::TableCell:
      return QStringLiteral("table");
    case muffin::HitTestResult::Zone::Code:
      return QStringLiteral("code");
    case muffin::HitTestResult::Zone::Math:
      return QStringLiteral("math");
    case muffin::HitTestResult::Zone::Html:
      return QStringLiteral("html");
    case muffin::HitTestResult::Zone::FrontMatter:
      return QStringLiteral("front matter");
    case muffin::HitTestResult::Zone::Block:
      return QStringLiteral("block");
    case muffin::HitTestResult::Zone::BlockAfter:
      return QStringLiteral("block after");
    case muffin::HitTestResult::Zone::None:
    default:
      return QStringLiteral("none");
  }
}

}  // namespace

void muffin::MainWindow::setupConnections() {
  editorController_.attach(&session_, renderView_);

  MainWindowSignalBinder::connectEditorSignals(*this);
  MainWindowSignalBinder::connectRenderSignals(*this);
  MainWindowSignalBinder::connectSessionSignals(*this);
  MainWindowSignalBinder::connectApplicationSignals(*this);

  MainWindowActionBinder::bindCommands(*this);

  MainWindowSignalBinder::connectFindBarSignals(*this);
  MainWindowSignalBinder::connectChromeSignals(*this);
  MainWindowSignalBinder::connectSidebarSignals(*this);

  MainWindowActionBinder::updateFileActions(*this);
  MainWindowActionBinder::updateContextActions(*this);
  rebuildRecentFilesMenu();
  refreshSidebarDocumentInfo();
  refreshSidebarOutline();
  updateSidebarMode();
  updateViewMode();
  applyTheme(themeManager_.currentThemeName());
  renderView_->setDocument(session_.document());
}

void muffin::MainWindow::updateRenderCursorStatus(const HitTestResult& hit) {
  if (!hit.isValid()) {
    renderCursorStatus_.clear();
  } else if (hit.zone == HitTestResult::Zone::TableCell) {
    renderCursorStatus_ = tr("table %1:%2 offset %3").arg(hit.tableRow + 1).arg(hit.tableColumn + 1).arg(hit.textOffset);
  } else {
    renderCursorStatus_ = QStringLiteral("%1 %2 offset %3")
                              .arg(zoneName(hit.zone), hit.blockId.toString())
                              .arg(hit.textOffset);
  }
  MainWindowActionBinder::updateContextActions(*this);
  updateStatus();
}

void muffin::MainWindow::syncSourceEditorIfNeeded() {
  if (!editor_ || !sourceEditorDirty_) {
    return;
  }
  editor_->setText(session_.markdownText());
  sourceEditorDirty_ = false;
}

void muffin::MainWindow::scheduleWordCountUpdate() {
  wordCountDirty_ = true;
  if (wordCountTimer_ && !wordCountTimer_->isActive()) {
    wordCountTimer_->start();
  }
}

void muffin::MainWindow::updateWordCountNow() {
  if (!wordsLabel_ || !wordCountDirty_) {
    return;
  }
  wordsLabel_->setText(tr("%1 words").arg(MainWindow::countWords(session_.markdownText())));
  wordCountDirty_ = false;
}

void muffin::MainWindow::insertTableWithDialog() {
  if (backend_->isSourceMode()) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert Table"));

  auto* layout = new QFormLayout(&dialog);
  layout->setContentsMargins(18, 16, 18, 14);
  layout->setSpacing(10);

  auto* rowSpin = new QSpinBox(&dialog);
  rowSpin->setRange(1, 99);
  rowSpin->setValue(2);
  rowSpin->setAccelerated(true);

  auto* columnSpin = new QSpinBox(&dialog);
  columnSpin->setRange(1, 99);
  columnSpin->setValue(2);
  columnSpin->setAccelerated(true);

  layout->addRow(tr("Rows:"), rowSpin);
  layout->addRow(tr("Columns:"), columnSpin);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  rowSpin->selectAll();
  rowSpin->setFocus(Qt::OtherFocusReason);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  renderCommands_.insertTable(rowSpin->value(), columnSpin->value());
}

void muffin::MainWindow::undoEdit() {
  if (backend_->canUndo()) {
    backend_->undo();
  }
}

void muffin::MainWindow::redoEdit() {
  if (backend_->canRedo()) {
    backend_->redo();
  }
}
