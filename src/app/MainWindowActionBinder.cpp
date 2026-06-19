#include "app/MainWindow.h"

#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QFileInfo>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>

namespace muffin {

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
  return backend_->isSourceMode() ? true : editorController_.selection().hasCursor();
}

bool MainWindow::commandHasSelection() const {
  if (backend_->isSourceMode()) {
    return editor_->editor()->textCursor().hasSelection();
  }
  return commandHasCursor() && !editorController_.selection().selection().isCollapsed();
}

bool MainWindow::commandOnEditableParagraph() const {
  return !backend_->isSourceMode() && renderCommands_.isOnEditableParagraphBlock();
}

int MainWindow::commandHeadingLevel() const {
  return backend_->isSourceMode() ? -1 : renderCommands_.currentHeadingLevel();
}

bool MainWindow::commandInlineFormatEnabled() const {
  return !backend_->isSourceMode() && renderCommands_.isInlineFormatEnabled();
}

bool MainWindow::commandInTableCell() const {
  return !backend_->isSourceMode() && renderCommands_.hasCurrentTableCell();
}

bool MainWindow::commandOnImage() const {
  return !backend_->isSourceMode() && renderCommands_.isOnImage();
}

bool MainWindow::commandOnLocalImage() const {
  if (!commandOnImage()) {
    return false;
  }
  const QString src = renderCommands_.imageSrcAtCursor();
  return !src.isEmpty() && QFileInfo(src).isFile();
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
      action->setEnabled(decl.enabled(*this));
    }
    if (decl.checked) {
      action->setChecked(decl.checked(*this));
    }
  }
}

void MainWindow::updateAllActions() {
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
  updateEditActions();
  updateTableActions();
  updateParagraphActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  updateImageActions();
  updateFormatActions();
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
