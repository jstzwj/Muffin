#include "app/MainWindow.h"

#include "document/MarkdownNode.h"
#include "document/SourceRangeUtil.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include "image/ImageInsertionPolicy.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
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
  editorController_.inputController().setEmojiProvider(&emojiProvider_);

  connectEditorSignals();
  connectRenderSignals();
  connectSessionSignals();
  connectApplicationSignals();

  bindCommands();

  connectFindBarSignals();
  connectChromeSignals();
  connectSidebarSignals();

  updateFileActions();
  updateContextActions();
  rebuildRecentFilesMenu();
  buildReopenEncodingMenu();
  refreshSidebarDocumentInfo();
  refreshSidebarOutline();
  updateSidebarMode();
  updateViewMode();
  applyTheme(themeManager_.currentThemeName());
  renderView_->setDocument(session_.document(), session_.filePath());
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
  updateBlockSourceLabel(hit);
  updateContextActions();
  updateStatus();
}

void muffin::MainWindow::updateBlockSourceLabel(const HitTestResult& hit) {
  if (!blockSourceLabel_) {
    return;
  }
  const bool enabled = QSettings().value(QStringLiteral("editor/showBlockSource"), false).toBool();
  if (!enabled || !backend_ || backend_->isSourceMode() || !hit.isValid()) {
    blockSourceLabel_->clear();
    blockSourceLabel_->setToolTip(QString());
    return;
  }
  MarkdownNode* node = session_.document().node(hit.blockId);
  if (!node) {
    blockSourceLabel_->clear();
    blockSourceLabel_->setToolTip(QString());
    return;
  }
  // Walk up to the top-level block so the preview shows the whole construct (list/table/code),
  // not just the innermost text node the caret sits in.
  while (node->parent() && node->parent()->type() != BlockType::Document) {
    node = node->parent();
  }
  const SourceRange range = fullBlockSourceRange(*node, session_.markdownText());
  if (range.byteStart < 0 || range.byteEnd <= range.byteStart) {
    blockSourceLabel_->clear();
    blockSourceLabel_->setToolTip(QString());
    return;
  }
  const QString& markdown = session_.markdownText();
  const QString raw = markdown.mid(range.byteStart, range.byteEnd - range.byteStart);
  blockSourceLabel_->setToolTip(raw.trimmed());
  // Flatten to a single status-bar line so multi-line blocks stay readable.
  QString flat = raw;
  flat.replace(QLatin1Char('\n'), QStringLiteral(" → "));
  flat = flat.trimmed();
  if (flat.size() > 120) {
    flat = flat.left(117) + QStringLiteral("...");
  }
  blockSourceLabel_->setText(flat);
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

void muffin::MainWindow::insertImageWithDialog() {
  if (backend_->isSourceMode()) {
    return;
  }

  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert Image"));

  auto* layout = new QFormLayout(&dialog);
  layout->setContentsMargins(18, 16, 18, 14);
  layout->setSpacing(10);

  auto* urlEdit = new QLineEdit(&dialog);
  urlEdit->setPlaceholderText(tr("https://example.com/image.png"));

  auto* altEdit = new QLineEdit(&dialog);
  altEdit->setPlaceholderText(tr("Alternative text"));

  auto* titleEdit = new QLineEdit(&dialog);
  titleEdit->setPlaceholderText(tr("Title (optional)"));

  layout->addRow(tr("URL:"), urlEdit);
  layout->addRow(tr("Alt Text:"), altEdit);
  layout->addRow(tr("Title:"), titleEdit);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  urlEdit->setFocus(Qt::OtherFocusReason);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString url = urlEdit->text().trimmed();
  if (url.isEmpty()) {
    return;
  }

  const QString alt = altEdit->text().trimmed();
  const QString title = titleEdit->text().trimmed();

  QString markdown;
  if (title.isEmpty()) {
    markdown = QStringLiteral("![%1](%2)").arg(alt, url);
  } else {
    markdown = QStringLiteral("![%1](%2 \"%3\")").arg(alt, url, title);
  }

  editorController_.inputController().insertText(markdown);
}

void muffin::MainWindow::insertLocalImageWithDialog() {
  if (backend_->isSourceMode()) {
    return;
  }

  const QString filter = tr("Images (%1)")
                             .arg(QStringLiteral("*.png *.jpg *.jpeg *.gif *.svg *.webp *.bmp *.ico *.tiff *.tif"));
  const QString filePath = QFileDialog::getOpenFileName(this, tr("Select Image"), QString(), filter);
  if (filePath.isEmpty()) {
    return;
  }

  // Route through the centralized insertion policy so the picked file honours the
  // configured insert action (copy to ./assets, upload, …) and syntax checkboxes.
  muffin::ImageInsertRequest req;
  req.sourcePath = filePath;
  req.documentPath = session_.filePath();
  req.documentText = session_.markdownText();
  req.alt = QFileInfo(filePath).completeBaseName();
  QSettings settings;
  const muffin::ImageInsertResult res = muffin::ImageInsertionPolicy::resolveHref(req, settings, this);
  if (!res.ok) {
    return;
  }

  const QString alt = res.alt.isEmpty() ? QStringLiteral("image") : res.alt;
  const QString markdown = QStringLiteral("![%1](%2)").arg(alt, res.href);
  editorController_.inputController().insertText(markdown);
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
