#include "app/MainWindow.h"

#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMenu>
#include <QSettings>
#include <QTimer>

namespace muffin {

Q_LOGGING_CATEGORY(actionProbe, "muffin.perf", QtWarningMsg)

// Every command's handler comes from the declaration table (CommandDeclarations.cpp);
// bindCommands just wires each one to its id. The handler is captured by value —
// it is a stateless std::function that receives the window — so it survives as
// long as the CommandRegistry entry does.
void MainWindow::bindCommands() {
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (decl.handler) {
      auto handler = decl.handler;
      commands_.bind(decl.id, [this, handler = std::move(handler)] { handler(*this); });
    }
  }
}

// ---- composite editor-state queries ----------------------------------------
// These back the declaration predicates. They were formerly inlined across the
// per-domain update*Actions; centralizing them here lets the table evaluate
// enable/checked state without private access.

bool MainWindow::commandHasCursor() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.hasCursor;
  }
  return backend_->isSourceMode() ? true : editorController_.selection().hasCursor();
}

bool MainWindow::commandHasSelection() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.hasSelection;
  }
  if (backend_->isSourceMode()) {
    return editor_->hasSelection();
  }
  return commandHasCursor() && !editorController_.selection().selection().isCollapsed();
}

bool MainWindow::commandOnEditableParagraph() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onEditableParagraph;
  }
  return !backend_->isSourceMode() && renderCommands_.isOnEditableParagraphBlock();
}

int MainWindow::commandHeadingLevel() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.headingLevel;
  }
  return backend_->isSourceMode() ? -1 : renderCommands_.currentHeadingLevel();
}

bool MainWindow::commandInlineFormatEnabled() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.inlineFormatEnabled;
  }
  return !backend_->isSourceMode() && renderCommands_.isInlineFormatEnabled();
}

bool MainWindow::commandInTableCell() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.inTableCell;
  }
  return !backend_->isSourceMode() && renderCommands_.hasCurrentTableCell();
}

bool MainWindow::commandOnImage() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onImage;
  }
  return !backend_->isSourceMode() && renderCommands_.isOnImage();
}

bool MainWindow::commandOnLocalImage() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onLocalImage;
  }
  if (!commandOnImage()) {
    return false;
  }
  const QString src = renderCommands_.imageSrcAtCursor();
  return !src.isEmpty() && QFileInfo(src).isFile();
}

MainWindow::CommandContextSnapshot MainWindow::captureCommandContext() const {
  CommandContextSnapshot snapshot;
  const bool sourceMode = backend_->isSourceMode();
  snapshot.hasCursor = sourceMode || editorController_.selection().hasCursor();
  snapshot.hasSelection = sourceMode
      ? editor_->hasSelection()
      : snapshot.hasCursor && !editorController_.selection().selection().isCollapsed();
  if (sourceMode) {
    return snapshot;
  }
  snapshot.onEditableParagraph = renderCommands_.isOnEditableParagraphBlock();
  snapshot.headingLevel = renderCommands_.currentHeadingLevel();
  snapshot.inlineFormatEnabled = renderCommands_.isInlineFormatEnabled();
  snapshot.inTableCell = renderCommands_.hasCurrentTableCell();
  snapshot.onImage = renderCommands_.isOnImage();
  if (snapshot.onImage) {
    const QString src = renderCommands_.imageSrcAtCursor();
    snapshot.onLocalImage = !src.isEmpty() && QFileInfo(src).isFile();
  }
  return snapshot;
}

// ---- action state refresh (table-driven) -----------------------------------

void MainWindow::updateActionsForCategory(CommandCategory category) {
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (decl.category != category) {
      continue;
    }
    QAction* action = commands_.action(decl.id);
    if (!action) {
      continue;
    }
    if (decl.enabled) {
      QElapsedTimer t;
      t.start();
      const bool enabled = decl.enabled(*this);
      const double ms = t.nsecsElapsed() / 1000000.0;
      if (ms > 5.0) {
        qCDebug(actionProbe).nospace() << "actionProbe.slowEnabled id=" << decl.id << " " << ms << " ms";
      }
      action->setEnabled(enabled);
    }
    if (decl.checked) {
      QElapsedTimer t;
      t.start();
      const bool checked = decl.checked(*this);
      const double ms = t.nsecsElapsed() / 1000000.0;
      if (ms > 5.0) {
        qCDebug(actionProbe).nospace() << "actionProbe.slowChecked id=" << decl.id << " " << ms << " ms";
      }
      action->setChecked(checked);
    }
  }
}

void MainWindow::updateAllActions() {
  commandContextSnapshot_ = captureCommandContext();
  commandContextSnapshotActive_ = true;
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (QAction* action = commands_.action(decl.id)) {
      if (decl.enabled) {
        action->setEnabled(decl.enabled(*this));
      }
      if (decl.checked) {
        action->setChecked(decl.checked(*this));
      }
    }
  }
  commandContextSnapshotActive_ = false;
}

void MainWindow::updateFileActions() {
  updateActionsForCategory(CommandCategory::File);
  // The Reopen-with-Encoding submenu is not a registered action; it mirrors file
  // presence directly, as the old procedural updateFileActions did.
  if (reopenEncodingMenu_) {
    reopenEncodingMenu_->setEnabled(!session_.filePath().isEmpty());
  }
}
void MainWindow::updateEditActions() { updateActionsForCategory(CommandCategory::Edit); }
void MainWindow::updateTableActions() { updateActionsForCategory(CommandCategory::Table); }
void MainWindow::updateParagraphActions() { updateActionsForCategory(CommandCategory::Paragraph); }
void MainWindow::updateCodeActions() { updateActionsForCategory(CommandCategory::Code); }
void MainWindow::updateHtmlActions() { updateActionsForCategory(CommandCategory::Html); }
void MainWindow::updateMathActions() { updateActionsForCategory(CommandCategory::Math); }
void MainWindow::updateImageActions() { updateActionsForCategory(CommandCategory::Image); }
void MainWindow::updateFormatActions() { updateActionsForCategory(CommandCategory::Format); }
void MainWindow::updateThemeActions() { updateActionsForCategory(CommandCategory::Theme); }

void MainWindow::updateContextActions() {
  commandContextSnapshot_ = captureCommandContext();
  commandContextSnapshotActive_ = true;
  for (const CommandDeclaration& decl : commandDeclarations()) {
    switch (decl.category) {
      case CommandCategory::Edit:
      case CommandCategory::Table:
      case CommandCategory::Paragraph:
      case CommandCategory::Code:
      case CommandCategory::Html:
      case CommandCategory::Math:
      case CommandCategory::Image:
      case CommandCategory::Format:
        break;
      default:
        continue;
    }
    QAction* action = commands_.action(decl.id);
    if (!action) {
      continue;
    }
    if (decl.enabled) {
      action->setEnabled(decl.enabled(*this));
    }
    if (decl.checked) {
      action->setChecked(decl.checked(*this));
    }
  }
  commandContextSnapshotActive_ = false;
}

void MainWindow::scheduleEditorStateRefresh() {
  if (editorStateRefreshScheduled_) {
    return;
  }
  editorStateRefreshScheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    editorStateRefreshScheduled_ = false;
    updateBlockSourceLabel(editorController_.selection().currentHit());
    updateStatus();
    updateContextActions();
  });
}

// Restore persisted checkable action states (line break, trailing newline, word
// wrap, sidebar/source/focus/typewriter/always-on-top, image insert action) from
// QSettings at startup. Unchanged by the table refactor — these are settings, not
// command declarations.
void MainWindow::restorePersistentActionStates() {
  auto& window = *this;
  QSettings settings;

  const int lineBreak = settings.value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  if (QAction* crlf = window.commands_.action(QStringLiteral("edit.linebreak_crlf"))) {
    crlf->setChecked(lineBreak == 1);
  }
  if (QAction* lf = window.commands_.action(QStringLiteral("edit.linebreak_lf"))) {
    lf->setChecked(lineBreak == 0);
  }
  if (QAction* trailingNewline = window.commands_.action(QStringLiteral("edit.trailing_newline"))) {
    trailingNewline->setChecked(settings.value(QStringLiteral("editor/trailingNewline"), true).toBool());
  }

  const bool wordWrap = settings.value(QStringLiteral("view/wordWrap"), true).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.word_wrap"))) {
    action->setChecked(wordWrap);
  }
  const bool sidebarVisible = settings.value(QStringLiteral("view/sidebarVisible"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(sidebarVisible);
  }
  const bool sourceMode = settings.value(QStringLiteral("view/sourceMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.source_mode"))) {
    action->setChecked(sourceMode);
  }
  const bool focusMode = settings.value(QStringLiteral("appearance/focusMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(focusMode);
  }
  const bool typewriterMode = settings.value(QStringLiteral("appearance/typewriterMode"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(typewriterMode);
  }
  const bool alwaysOnTop = settings.value(QStringLiteral("view/alwaysOnTop"), false).toBool();
  if (QAction* action = window.commands_.action(QStringLiteral("view.always_on_top"))) {
    action->setChecked(alwaysOnTop);
    window.setWindowFlag(Qt::WindowStaysOnTopHint, alwaysOnTop);
    window.show();
  }
}

}  // namespace muffin
