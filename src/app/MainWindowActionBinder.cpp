#include "app/MainWindow.h"
#include "diagnostics/ScopedPerfProbe.h"

#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMenu>
#include <QSettings>
#include <QScopedValueRollback>
#include <QTimer>

namespace muffin {

Q_LOGGING_CATEGORY(actionProbe, "muffin.perf", QtWarningMsg)
Q_LOGGING_CATEGORY(actionDetailProbe, "muffin.perf.detail", QtWarningMsg)

namespace {
void updateCommandHint(QAction* action, const CommandDeclaration& declaration, const MainWindow& window) {
  if (declaration.requiresEditableDocument && !action->isEnabled()) {
    const auto reason = window.commandEditingDisabledReason();
    if (!reason.isEmpty()) {
      action->setToolTip(reason);
      action->setStatusTip(reason);
      return;
    }
  }
  if (!declaration.disabledReason && !declaration.requiresEditableDocument)
    return;
  const auto reason =
      action->isEnabled() || !declaration.disabledReason ? QString() : declaration.disabledReason(window);
  action->setToolTip(reason);
  action->setStatusTip(reason);
}
}

// Every command's handler comes from the declaration table (CommandDeclarations.cpp);
// bindCommands just wires each one to its id. The handler is captured by value —
// it is a stateless std::function that receives the window — so it survives as
// long as the CommandRegistry entry does.
void MainWindow::bindCommands() {
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (decl.handler) {
      auto handler = decl.handler;
      const bool requiresEditable = decl.requiresEditableDocument;
      commands_.bind(decl.id, [this, requiresEditable, handler = std::move(handler)] {
        if (!requiresEditable || tab_->session.isEditingAllowed())
          handler(*this);
      });
    }
  }
}

bool MainWindow::commandEnabled(const CommandDeclaration& declaration) const {
  if (declaration.requiresEditableDocument && !tab_->session.isEditingAllowed())
    return false;
  return declaration.enabled ? declaration.enabled(*this) : declaration.enabledInitial;
}

// ---- composite editor-state queries ----------------------------------------
// These back the declaration predicates. They were formerly inlined across the
// per-domain update*Actions; centralizing them here lets the table evaluate
// enable/checked state without private access.

bool MainWindow::commandHasCursor() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.hasCursor;
  }
  return tab_->backend->isSourceMode() ? true : tab_->editorController.selection().hasCursor();
}

bool MainWindow::commandHasSelection() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.hasSelection;
  }
  if (tab_->backend->isSourceMode()) {
    return tab_->editor->hasSelection();
  }
  return commandHasCursor() && !tab_->editorController.selection().selection().isCollapsed();
}

bool MainWindow::commandOnEditableParagraph() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onEditableParagraph;
  }
  return !tab_->backend->isSourceMode() && tab_->renderCommands.isOnEditableParagraphBlock();
}

int MainWindow::commandHeadingLevel() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.headingLevel;
  }
  return tab_->backend->isSourceMode() ? -1 : tab_->renderCommands.currentHeadingLevel();
}

bool MainWindow::commandInlineFormatEnabled() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.inlineFormatEnabled;
  }
  return !tab_->backend->isSourceMode() && tab_->renderCommands.isInlineFormatEnabled();
}

bool MainWindow::commandSingleInlineSelection() const {
  if (commandContextSnapshotActive_)
    return commandContextSnapshot_.singleInlineSelection;
  return !tab_->session.isOpenInProgress() && !tab_->backend->isSourceMode() &&
         tab_->renderCommands.isSingleInlineSelection();
}

bool MainWindow::commandInTableCell() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.inTableCell;
  }
  return !tab_->backend->isSourceMode() && tab_->renderCommands.hasCurrentTableCell();
}

bool MainWindow::commandOnImage() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onImage;
  }
  return !tab_->backend->isSourceMode() && tab_->renderCommands.isOnImage();
}

bool MainWindow::commandOnLocalImage() const {
  if (commandContextSnapshotActive_) {
    return commandContextSnapshot_.onLocalImage;
  }
  if (!commandOnImage()) {
    return false;
  }
  const QString src = tab_->renderCommands.imageSrcAtCursor();
  return !src.isEmpty() && QFileInfo(src).isFile();
}

MainWindow::CommandContextSnapshot MainWindow::captureCommandContext() const {
  diag::ScopedPerfProbe perf("main.captureCommandContext", actionDetailProbe());
  CommandContextSnapshot snapshot;
  const bool sourceMode = tab_->backend->isSourceMode();
  snapshot.hasCursor = sourceMode || tab_->editorController.selection().hasCursor();
  snapshot.hasSelection = sourceMode
                              ? tab_->editor->hasSelection()
                              : snapshot.hasCursor && !tab_->editorController.selection().selection().isCollapsed();
  if (sourceMode) {
    return snapshot;
  }
  snapshot.onEditableParagraph = tab_->renderCommands.isOnEditableParagraphBlock();
  snapshot.headingLevel = tab_->renderCommands.currentHeadingLevel();
  snapshot.inlineFormatEnabled = tab_->renderCommands.isInlineFormatEnabled();
  snapshot.singleInlineSelection = !tab_->session.isOpenInProgress() && tab_->renderCommands.isSingleInlineSelection();
  const auto cursorState = tab_->renderCommands.currentCursorState();
  if (snapshot.inlineFormatEnabled)
    snapshot.inlineFormats = cursorState.formats;
  snapshot.inTableCell = tab_->renderCommands.hasCurrentTableCell();
  snapshot.onImage = !cursorState.imageSource.isEmpty();
  if (snapshot.onImage) {
    snapshot.onLocalImage = QFileInfo(cursorState.imageSource).isFile();
  }
  return snapshot;
}

CursorFormatState MainWindow::commandInlineFormats() const {
  return commandContextSnapshotActive_ ? commandContextSnapshot_.inlineFormats
                                       : tab_->renderCommands.currentInlineFormats();
}

// ---- action state refresh (table-driven) -----------------------------------

void MainWindow::updateActionsForCategory(CommandCategory category) {
  const bool needsContext = category != CommandCategory::File && category != CommandCategory::Theme;
  const QScopedValueRollback<CommandContextSnapshot> context(
      commandContextSnapshot_, needsContext ? captureCommandContext() : commandContextSnapshot_);
  const QScopedValueRollback<bool> active(commandContextSnapshotActive_, needsContext);
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (decl.category != category) {
      continue;
    }
    QAction* action = commands_.action(decl.id);
    if (!action) {
      continue;
    }
    if (decl.enabled || decl.requiresEditableDocument) {
      QElapsedTimer t;
      t.start();
      const bool enabled = commandEnabled(decl);
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
    updateCommandHint(action, decl, *this);
  }
}

void MainWindow::updateAllActions() {
  commandContextSnapshot_ = captureCommandContext();
  commandContextSnapshotActive_ = true;
  for (const CommandDeclaration& decl : commandDeclarations()) {
    if (QAction* action = commands_.action(decl.id)) {
      if (decl.enabled || decl.requiresEditableDocument) {
        action->setEnabled(commandEnabled(decl));
      }
      if (decl.checked) {
        action->setChecked(decl.checked(*this));
      }
      updateCommandHint(action, decl, *this);
    }
  }
  commandContextSnapshotActive_ = false;
}

void MainWindow::updateFileActions() {
  updateActionsForCategory(CommandCategory::File);
  // The Reopen-with-Encoding submenu is not a registered action; it mirrors file
  // presence directly, as the old procedural updateFileActions did.
  if (reopenEncodingMenu_) {
    reopenEncodingMenu_->setEnabled(!tab_->session.filePath().isEmpty());
  }
}
void MainWindow::updateEditActions() {
  updateActionsForCategory(CommandCategory::Edit);
}
void MainWindow::updateTableActions() {
  updateActionsForCategory(CommandCategory::Table);
}
void MainWindow::updateParagraphActions() {
  updateActionsForCategory(CommandCategory::Paragraph);
}
void MainWindow::updateCodeActions() {
  updateActionsForCategory(CommandCategory::Code);
}
void MainWindow::updateHtmlActions() {
  updateActionsForCategory(CommandCategory::Html);
}
void MainWindow::updateMathActions() {
  updateActionsForCategory(CommandCategory::Math);
}
void MainWindow::updateImageActions() {
  updateActionsForCategory(CommandCategory::Image);
}
void MainWindow::updateFormatActions() {
  updateActionsForCategory(CommandCategory::Format);
}
void MainWindow::updateThemeActions() {
  updateActionsForCategory(CommandCategory::Theme);
}

void MainWindow::updateContextActions() {
  diag::ScopedPerfProbe perf("main.contextActions", actionDetailProbe());
  commandContextSnapshot_ = captureCommandContext();
  commandContextSnapshotActive_ = true;
  diag::ScopedPerfProbe predicatesPerf("main.actionPredicates", actionDetailProbe());
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
    if (decl.enabled || decl.requiresEditableDocument) {
      action->setEnabled(commandEnabled(decl));
    }
    if (decl.checked) {
      action->setChecked(decl.checked(*this));
    }
    updateCommandHint(action, decl, *this);
  }
  commandContextSnapshotActive_ = false;
}

void MainWindow::scheduleEditorStateRefresh() {
  if (editorStateRefreshScheduled_) {
    return;
  }
  editorStateRefreshScheduled_ = true;
  QTimer::singleShot(0, this, [this] {
    diag::ScopedPerfProbe perf("main.editorState", actionDetailProbe());
    editorStateRefreshScheduled_ = false;
    {
      diag::ScopedPerfProbe labelPerf("main.blockSourceLabel", actionDetailProbe());
      updateBlockSourceLabel(tab_->editorController.selection().currentHit());
    }
    {
      diag::ScopedPerfProbe statusPerf("main.status", actionDetailProbe());
      updateStatus();
    }
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
    const bool wasVisible = window.isVisible();
    window.setWindowFlag(Qt::WindowStaysOnTopHint, alwaysOnTop);
    // main() shows the window after session restoration. Changing a window
    // flag during construction must not expose its unfinished startup view.
    if (wasVisible)
      window.show();
  }
}

} // namespace muffin
