#include "app/MainWindow.h"
#include "diagnostics/ScopedPerfProbe.h"

#include "document/MarkdownNode.h"
#include "document/ImageSyntaxOps.h"
#include "document/SourceRangeUtil.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"
#include "document/TextStats.h"
#include "image/ImageInsertionPolicy.h"
#include "app/StatusBarWidget.h"

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
Q_LOGGING_CATEGORY(actionsPerf, "muffin.perf.detail", QtWarningMsg)

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

} // namespace

void muffin::MainWindow::setupConnections() {
  tab_->editorController.inputController().setEmojiProvider(&emojiProvider_);

  connectEditorSignals(tab_);
  connectRenderSignals(tab_);
  connectSessionSignals(tab_);
  connectApplicationSignals();

  bindCommands();

  connectFindBarSignals(tab_);
  connectChromeSignals();
  connectSidebarSignals();

  updateFileActions();
  updateContextActions();
  rebuildRecentFilesMenu();
  rebuildThemesMenu();
  buildReopenEncodingMenu();
  refreshSidebarDocumentInfo();
  refreshSidebarOutline();
  updateSidebarMode();
  updateViewMode();
  applyTheme(themeManager_.currentThemeName());
  tab_->renderView->setDocument(tab_->session.document(), tab_->session.filePath());
}

void muffin::MainWindow::updateRenderCursorStatus(const HitTestResult& hit) {
  if (!hit.isValid()) {
    tab_->renderCursorStatus.clear();
  } else {
    // Render mode has no QTextCursor; report the source line:column derived from
    // the hit's text offset (matching the source-mode "line:col" display) instead
    // of the verbose zone/block-id/offset detail.
    const auto& offsets = tab_->session.document().lineOffsets();
    const int line = offsets.lineForOffset(hit.textOffset);
    const qsizetype lineStart = offsets.offsetForLineColumn(line, 1);
    const int column = lineStart >= 0 ? static_cast<int>(hit.textOffset - lineStart) + 1 : 1;
    tab_->renderCursorStatus = QStringLiteral("%1:%2").arg(line).arg(column);
  }
  scheduleEditorStateRefresh();
}

void muffin::MainWindow::updateBlockSourceLabel(const HitTestResult& hit) {
  if (!statusBar_) {
    return;
  }
  if (!showBlockSourceEnabled_ || !tab_->backend || tab_->backend->isSourceMode() || !hit.isValid()) {
    statusBar_->setBlockSource(QString(), QString());
    return;
  }
  MarkdownNode* node = tab_->session.document().node(hit.blockId);
  if (!node) {
    statusBar_->setBlockSource(QString(), QString());
    return;
  }
  // Show the whole construct the caret sits in (list/table/code), not just the innermost text
  // node.
  node = node->topLevelBlock();
  const SourceRange range = fullBlockSourceRange(*node, tab_->session.markdownText());
  if (range.byteStart < 0 || range.byteEnd <= range.byteStart) {
    statusBar_->setBlockSource(QString(), QString());
    return;
  }
  const PieceTable& markdown = tab_->session.markdownText();
  const QString raw = markdown.mid(range.byteStart, range.byteEnd - range.byteStart);
  // Flatten to a single status-bar line so multi-line blocks stay readable.
  QString flat = raw;
  flat.replace(QLatin1Char('\n'), QStringLiteral(" → "));
  flat = flat.trimmed();
  if (flat.size() > 120) {
    flat = flat.left(117) + QStringLiteral("...");
  }
  statusBar_->setBlockSource(flat, raw.trimmed());
}

void muffin::MainWindow::syncSourceEditorIfNeeded() {
  if (!tab_->editor)
    return;
  tab_->editor->syncFromSession();
}

void muffin::MainWindow::scheduleWordCountUpdate() {
  tab_->wordCountDirty = true;
  if (wordCountTimer_) {
    // PieceTable maintains this count incrementally. The debounce coalesces status repaints; the
    // callback itself is O(1), independent of document size.
    wordCountTimer_->start();
  }
}

void muffin::MainWindow::updateWordCountNow() {
  muffin::diag::ScopedPerfProbe perf("main.wordCount", actionsPerf());
  if (!statusBar_ || !tab_->wordCountDirty) {
    return;
  }
  const int words = text_stats::countWords(tab_->session.markdownText());
  statusBar_->setWordCount(words);
  tab_->wordCountDirty = false;
}

void muffin::MainWindow::insertTableWithDialog() {
  if (tab_->backend->isSourceMode()) {
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

  tab_->renderCommands.insertTable(rowSpin->value(), columnSpin->value());
}

void muffin::MainWindow::insertImageWithDialog() {
  if (tab_->backend->isSourceMode()) {
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

  const QString markdown = image_syntax::markdownImage(alt, url, title);

  tab_->editorController.inputController().insertText(markdown);
}

void muffin::MainWindow::insertLocalImageWithDialog() {
  if (tab_->backend->isSourceMode()) {
    return;
  }

  const QString filter =
      tr("Images (%1)").arg(QStringLiteral("*.png *.jpg *.jpeg *.gif *.svg *.webp *.bmp *.ico *.tiff *.tif"));
  const QString filePath = QFileDialog::getOpenFileName(this, tr("Select Image"), QString(), filter);
  if (filePath.isEmpty()) {
    return;
  }

  // Route through the centralized insertion policy so the picked file honours the
  // configured insert action (copy to ./assets, upload, …) and syntax checkboxes.
  muffin::ImageInsertRequest req;
  req.sourcePath = filePath;
  req.documentPath = tab_->session.filePath();
  req.documentText = tab_->session.markdownText().toString();
  req.alt = QFileInfo(filePath).completeBaseName();
  QSettings settings;
  const muffin::ImageInsertResult res = muffin::ImageInsertionPolicy::resolveHref(req, settings, this);
  if (!res.ok) {
    return;
  }

  const QString alt = res.alt.isEmpty() ? QStringLiteral("image") : res.alt;
  const QString markdown = image_syntax::markdownImage(alt, res.href);
  tab_->editorController.inputController().insertText(markdown);
}

void muffin::MainWindow::undoEdit() {
  if (tab_->backend->canUndo()) {
    tab_->backend->undo();
  }
}

void muffin::MainWindow::redoEdit() {
  if (tab_->backend->canRedo()) {
    tab_->backend->redo();
  }
}
