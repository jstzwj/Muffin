#include "app/MainWindowActionBinder.h"

#include "app/MainWindow.h"
#include "app/SidebarWidget.h"
#include "document/MarkdownTypes.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTextCursor>
#include <QUrl>

void muffin::MainWindowActionBinder::bindCommands(MainWindow& window) {
  window.commands_.bind(QStringLiteral("file.new"), [&window] {
    window.editorController_.clearHistoryAndSelection();
    window.fileController_.newFile(window.session_, &window);
  });
  window.commands_.bind(QStringLiteral("file.new_window"), [&window] { window.openNewWindow(); });
  window.commands_.bind(QStringLiteral("file.open_folder"), [&window] { window.openFolder(); });
  window.commands_.bind(QStringLiteral("file.open"), [&window] {
    if (window.fileController_.open(window.session_, &window)) {
      window.editorController_.clearHistoryAndSelection();
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.save"), [&window] {
    if (window.fileController_.save(window.session_, &window)) {
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.save_as"), [&window] {
    if (window.fileController_.saveAs(window.session_, &window)) {
      window.addRecentFile(window.session_.filePath());
    }
  });
  window.commands_.bind(QStringLiteral("file.properties"), [&window] { window.showDocumentProperties(); });
  window.commands_.bind(QStringLiteral("file.preferences"), [&window] { window.showPreferences(); });
  window.commands_.bind(QStringLiteral("file.print"), [&window] { window.printDocument(); });
  window.commands_.bind(QStringLiteral("file.reveal"), [&window] { window.revealCurrentFile(); });
  window.commands_.bind(QStringLiteral("file.close"), [&window] { window.close(); });
  window.commands_.bind(QStringLiteral("file.move_to"), [&window] { window.moveToFile(); });
  window.commands_.bind(QStringLiteral("file.save_all"), [&window] { window.saveAllOpenFiles(); });
  window.commands_.bind(QStringLiteral("file.sidebar"), [&window] { window.showInSidebar(); });
  window.commands_.bind(QStringLiteral("file.delete"), [&window] { window.deleteFile(); });

  window.commands_.bind(QStringLiteral("edit.undo"), [&window] { window.undoEdit(); });
  window.commands_.bind(QStringLiteral("edit.redo"), [&window] { window.redoEdit(); });
  window.commands_.bind(QStringLiteral("edit.cut"), [&window] { window.backend_->cut(); });
  window.commands_.bind(QStringLiteral("edit.copy"), [&window] { window.backend_->copy(); });
  window.commands_.bind(QStringLiteral("edit.paste"), [&window] { window.backend_->paste(); });
  window.commands_.bind(QStringLiteral("edit.select_all"), [&window] { window.backend_->selectAll(); });
  window.commands_.bind(QStringLiteral("edit.delete"), [&window] { window.backend_->deleteForward(); });
  window.commands_.bind(QStringLiteral("edit.copy_plain"), [&window] { window.backend_->copyAsPlainText(); });
  window.commands_.bind(QStringLiteral("edit.copy_markdown"), [&window] { window.backend_->copyAsMarkdown(); });
  window.commands_.bind(QStringLiteral("edit.copy_html"), [&window] { window.backend_->copyAsHtml(); });
  window.commands_.bind(QStringLiteral("edit.paste_plain"), [&window] { window.backend_->pasteAsPlainText(); });
  window.commands_.bind(QStringLiteral("edit.select_line"), [&window] { window.backend_->selectLine(); });
  window.commands_.bind(QStringLiteral("edit.select_format"), [&window] { window.backend_->selectFormatSpan(); });
  window.commands_.bind(QStringLiteral("edit.move_line_up"), [&window] { window.backend_->moveLineUp(); });
  window.commands_.bind(QStringLiteral("edit.move_line_down"), [&window] { window.backend_->moveLineDown(); });

  window.commands_.bind(QStringLiteral("edit.find"), [&window] { window.showFindBar(); });
  window.commands_.bind(QStringLiteral("edit.replace"), [&window] { window.showReplaceBar(); });
  window.commands_.bind(QStringLiteral("edit.find_next"), [&window] {
    if (!window.findBar_ || !window.findBar_->isVisible()) {
      window.showFindBar();
    }
    window.performFindNext();
  });
  window.commands_.bind(QStringLiteral("edit.find_previous"), [&window] {
    if (!window.findBar_ || !window.findBar_->isVisible()) {
      window.showFindBar();
    }
    window.performFindPrevious();
  });

  window.commands_.bind(QStringLiteral("format.bold"), [&window] { window.backend_->toggleBold(); });
  window.commands_.bind(QStringLiteral("format.italic"), [&window] { window.backend_->toggleItalic(); });
  window.commands_.bind(QStringLiteral("format.code"), [&window] { window.backend_->toggleCode(); });
  window.commands_.bind(QStringLiteral("format.link"), [&window] { window.backend_->insertLink(); });

  window.commands_.bind(QStringLiteral("table.insert_row_before"), [&window] { window.renderCommands_.insertRowBefore(); });
  window.commands_.bind(QStringLiteral("table.insert_row_after"), [&window] { window.renderCommands_.insertRowAfter(); });
  window.commands_.bind(QStringLiteral("table.delete_row"), [&window] { window.renderCommands_.deleteCurrentRow(); });
  window.commands_.bind(QStringLiteral("table.move_row_up"), [&window] { window.renderCommands_.moveCurrentRowUp(); });
  window.commands_.bind(QStringLiteral("table.move_row_down"), [&window] { window.renderCommands_.moveCurrentRowDown(); });
  window.commands_.bind(QStringLiteral("table.insert_column_before"), [&window] { window.renderCommands_.insertColumnBefore(); });
  window.commands_.bind(QStringLiteral("table.insert_column_after"), [&window] { window.renderCommands_.insertColumnAfter(); });
  window.commands_.bind(QStringLiteral("table.delete_column"), [&window] { window.renderCommands_.deleteCurrentColumn(); });
  window.commands_.bind(QStringLiteral("table.move_column_left"), [&window] { window.renderCommands_.moveCurrentColumnLeft(); });
  window.commands_.bind(QStringLiteral("table.move_column_right"), [&window] { window.renderCommands_.moveCurrentColumnRight(); });
  window.commands_.bind(QStringLiteral("table.align_left"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Left); });
  window.commands_.bind(QStringLiteral("table.align_center"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Center); });
  window.commands_.bind(QStringLiteral("table.align_right"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::Right); });
  window.commands_.bind(QStringLiteral("table.align_none"), [&window] { window.renderCommands_.setCurrentColumnAlignment(TableAlignment::None); });
  window.commands_.bind(QStringLiteral("table.copy_table"), [&window] { window.renderCommands_.copyCurrentTable(); });
  window.commands_.bind(QStringLiteral("table.format_source"), [&window] { window.renderCommands_.formatCurrentTableSource(); });
  window.commands_.bind(QStringLiteral("table.delete_table"), [&window] { window.renderCommands_.deleteCurrentTable(); });
  window.commands_.bind(QStringLiteral("table.insert_table"), [&window] { window.insertTableWithDialog(); });
  window.commands_.bind(QStringLiteral("paragraph.yaml"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Yaml); });
  window.commands_.bind(QStringLiteral("paragraph.toml"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Toml); });
  window.commands_.bind(QStringLiteral("paragraph.json"), [&window] { window.renderCommands_.insertFrontMatter(FrontMatterFormat::Json); });

  for (int level = 1; level <= 6; ++level) {
    window.commands_.bind(QStringLiteral("paragraph.heading_%1").arg(level), [&window, level] { window.renderCommands_.setHeadingLevel(level); });
  }
  window.commands_.bind(QStringLiteral("paragraph.paragraph"), [&window] { window.renderCommands_.setHeadingLevel(0); });
  window.commands_.bind(QStringLiteral("paragraph.promote_heading"), [&window] { window.renderCommands_.promoteHeading(); });
  window.commands_.bind(QStringLiteral("paragraph.demote_heading"), [&window] { window.renderCommands_.demoteHeading(); });

  window.commands_.bind(QStringLiteral("paragraph.math_block"), [&window] { window.renderCommands_.toggleFormulaBlock(); });
  window.commands_.bind(QStringLiteral("paragraph.code_block"), [&window] { window.renderCommands_.toggleCodeBlock(); });
  window.commands_.bind(QStringLiteral("paragraph.insert_before"), [&window] { window.renderCommands_.insertParagraphBefore(); });
  window.commands_.bind(QStringLiteral("paragraph.insert_after"), [&window] { window.renderCommands_.insertParagraphAfter(); });
  window.commands_.bind(QStringLiteral("paragraph.link_ref"), [&window] { window.renderCommands_.insertLinkReference(); });
  window.commands_.bind(QStringLiteral("paragraph.footnote"), [&window] { window.renderCommands_.insertFootnoteDefinition(); });
  window.commands_.bind(QStringLiteral("paragraph.hr"), [&window] { window.renderCommands_.insertHorizontalRule(); });

  window.commands_.bind(QStringLiteral("paragraph.quote"), [&window] { window.renderCommands_.toggleQuote(); });
  window.commands_.bind(QStringLiteral("paragraph.ordered_list"), [&window] { window.renderCommands_.convertToOrderedList(); });
  window.commands_.bind(QStringLiteral("paragraph.unordered_list"), [&window] { window.renderCommands_.convertToUnorderedList(); });
  window.commands_.bind(QStringLiteral("paragraph.task_list"), [&window] { window.renderCommands_.convertToTaskList(); });

  window.commands_.bind(QStringLiteral("code.enter_edit"), [&window] { window.renderCommands_.enterCodeEditMode(); });
  window.commands_.bind(QStringLiteral("code.exit_edit"), [&window] { window.renderCommands_.exitCodeEditMode(); });
  window.commands_.bind(QStringLiteral("code.set_language"), [&window] {
    bool ok = false;
    const QString language = QInputDialog::getText(
        &window, muffin::MainWindow::tr("Code Language"), muffin::MainWindow::tr("Language:"), QLineEdit::Normal, QString(), &ok);
    if (ok) {
      window.renderCommands_.setCodeLanguage(language);
    }
  });

  window.commands_.bind(QStringLiteral("html.enter_edit"), [&window] { window.renderCommands_.enterHtmlEditMode(); });
  window.commands_.bind(QStringLiteral("html.exit_edit"), [&window] { window.renderCommands_.exitHtmlEditMode(); });
  window.commands_.bind(QStringLiteral("html.set_source"), [&window] {
    bool ok = false;
    const QString html = QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("HTML Source"), muffin::MainWindow::tr("HTML:"), QString(), &ok);
    if (ok) {
      window.renderCommands_.setHtmlSource(html);
    }
  });

  window.commands_.bind(QStringLiteral("math.enter_edit"), [&window] { window.renderCommands_.enterMathEditMode(); });
  window.commands_.bind(QStringLiteral("math.exit_edit"), [&window] { window.renderCommands_.exitMathEditMode(); });
  window.commands_.bind(QStringLiteral("math.set_tex"), [&window] {
    bool ok = false;
    const QString tex = QInputDialog::getMultiLineText(&window, muffin::MainWindow::tr("Math TeX"), muffin::MainWindow::tr("TeX:"), QString(), &ok);
    if (ok) {
      window.renderCommands_.setMathTex(tex);
    }
  });

  window.commands_.bind(QStringLiteral("view.word_wrap"), [&window] {
    const bool enabled = window.commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
    window.editor_->setWordWrapEnabled(enabled);
    QSettings().setValue(QStringLiteral("view/wordWrap"), enabled);
  });

  window.commands_.bind(QStringLiteral("edit.linebreak_crlf"), [&window] {
    window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), true);
    window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 1);
  });
  window.commands_.bind(QStringLiteral("edit.linebreak_lf"), [&window] {
    window.commands_.setChecked(QStringLiteral("edit.linebreak_lf"), true);
    window.commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 0);
  });
  window.commands_.bind(QStringLiteral("edit.trailing_newline"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("edit.trailing_newline"))->isChecked();
    QSettings().setValue(QStringLiteral("editor/trailingNewline"), checked);
  });

  window.commands_.bind(QStringLiteral("view.sidebar"), [&window] {
    window.updateSidebarMode();
    QSettings().setValue(QStringLiteral("view/sidebarVisible"),
        window.commands_.action(QStringLiteral("view.sidebar"))->isChecked());
  });
  window.commands_.bind(QStringLiteral("view.outline"), [&window] { window.setSidebarPanel(SidebarWidget::Panel::Outline); });
  window.commands_.bind(QStringLiteral("view.file_tree"), [&window] { window.setSidebarPanel(SidebarWidget::Panel::Files); });
  window.commands_.bind(QStringLiteral("view.source_mode"), [&window] {
    window.updateViewMode();
    QSettings().setValue(QStringLiteral("view/sourceMode"),
        window.commands_.action(QStringLiteral("view.source_mode"))->isChecked());
  });
  window.commands_.bind(QStringLiteral("view.status_bar"), [&window] {
    const bool visible = window.commands_.action(QStringLiteral("view.status_bar"))->isChecked();
    window.setStatusBarVisible(visible);
    window.saveAppearanceStatusBarVisible(visible);
  });
  window.commands_.bind(QStringLiteral("view.fullscreen"), [&window] {
    window.isFullScreen() ? window.showNormal() : window.showFullScreen();
  });
  window.commands_.bind(QStringLiteral("view.focus"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.focus"))->isChecked();
    window.setFocusMode(checked);
    window.saveAppearanceFocusMode(checked);
  });
  window.commands_.bind(QStringLiteral("view.typewriter"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.typewriter"))->isChecked();
    window.setTypewriterMode(checked);
    window.saveAppearanceTypewriterMode(checked);
  });
  window.commands_.bind(QStringLiteral("view.always_on_top"), [&window] {
    const bool checked = window.commands_.action(QStringLiteral("view.always_on_top"))->isChecked();
    window.setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    window.show();
    QSettings().setValue(QStringLiteral("view/alwaysOnTop"), checked);
  });
  window.commands_.bind(QStringLiteral("view.zoom_in"), [&window] {
    window.setZoomPercent(window.zoomPercent_ + 10);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });
  window.commands_.bind(QStringLiteral("view.zoom_out"), [&window] {
    window.setZoomPercent(window.zoomPercent_ - 10);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });
  window.commands_.bind(QStringLiteral("view.actual_size"), [&window] {
    window.setZoomPercent(100);
    window.saveAppearanceZoomPercent(window.zoomPercent_);
  });

  window.commands_.bind(QStringLiteral("help.website"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin")));
  });
  window.commands_.bind(QStringLiteral("help.changelog"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/blob/main/CHANGELOG.md")));
  });
  window.commands_.bind(QStringLiteral("help.feedback"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/issues")));
  });
  window.commands_.bind(QStringLiteral("help.about"), [&window] {
    QMessageBox::about(
        &window,
        muffin::MainWindow::tr("About Muffin"),
        muffin::MainWindow::tr("Muffin %1\n\nA fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets.")
            .arg(QApplication::applicationVersion()));
  });

  window.commands_.bind(QStringLiteral("theme.github"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("github"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.newsprint"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("newsprint"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.night"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("night"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.pixyll"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("pixyll"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
  window.commands_.bind(QStringLiteral("theme.whitey"), [&window] {
    if (window.themeManager_.setTheme(QStringLiteral("whitey"))) {
      window.saveAppearanceTheme(window.themeManager_.currentThemeName());
    }
  });
}

void muffin::MainWindowActionBinder::restorePersistentActionStates(MainWindow& window) {
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

void muffin::MainWindowActionBinder::updateFileActions(MainWindow& window) {
  const bool hasFile = !window.session_.filePath().isEmpty();
  window.commands_.setEnabled(QStringLiteral("file.properties"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.reveal"), hasFile);
  if (window.reopenEncodingMenu_) {
    window.reopenEncodingMenu_->setEnabled(hasFile);
  }
  window.commands_.setEnabled(QStringLiteral("file.move_to"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.save_all"), true);
  window.commands_.setEnabled(QStringLiteral("file.sidebar"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.delete"), hasFile);
  window.commands_.setEnabled(QStringLiteral("file.print"), hasFile);
}

void muffin::MainWindowActionBinder::updateEditActions(MainWindow& window) {
  const bool src = window.backend_->isSourceMode();
  const bool hasCursor = src ? true : window.editorController_.selection().hasCursor();
  const bool hasSelection = src
      ? window.editor_->editor()->textCursor().hasSelection()
      : hasCursor && !window.editorController_.selection().selection().isCollapsed();

  window.commands_.setEnabled(QStringLiteral("edit.delete"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.copy_plain"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.copy_markdown"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.copy_html"), hasSelection);
  window.commands_.setEnabled(QStringLiteral("edit.paste_plain"), true);
  window.commands_.setEnabled(QStringLiteral("edit.select_line"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.select_format"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.move_line_up"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.move_line_down"), hasCursor);
  window.commands_.setEnabled(QStringLiteral("edit.find"), true);
  window.commands_.setEnabled(QStringLiteral("edit.replace"), true);
  window.commands_.setEnabled(QStringLiteral("edit.find_next"), true);
  window.commands_.setEnabled(QStringLiteral("edit.find_previous"), true);
}

void muffin::MainWindowActionBinder::updateTableActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool enabled = !sourceMode && window.renderCommands_.hasCurrentTableCell();
  const QStringList ids = {
      QStringLiteral("table.insert_row_before"),
      QStringLiteral("table.insert_row_after"),
      QStringLiteral("table.delete_row"),
      QStringLiteral("table.move_row_up"),
      QStringLiteral("table.move_row_down"),
      QStringLiteral("table.insert_column_before"),
      QStringLiteral("table.insert_column_after"),
      QStringLiteral("table.delete_column"),
      QStringLiteral("table.move_column_left"),
      QStringLiteral("table.move_column_right"),
      QStringLiteral("table.align_left"),
      QStringLiteral("table.align_center"),
      QStringLiteral("table.align_right"),
      QStringLiteral("table.align_none"),
      QStringLiteral("table.copy_table"),
      QStringLiteral("table.format_source"),
      QStringLiteral("table.delete_table"),
  };
  for (const QString& id : ids) {
    window.commands_.setEnabled(id, enabled);
  }
  window.commands_.setEnabled(QStringLiteral("table.insert_table"), !sourceMode);
}

void muffin::MainWindowActionBinder::updateParagraphActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool editable = !sourceMode && window.renderCommands_.isOnEditableParagraphBlock();
  const int headingLevel = sourceMode ? -1 : window.renderCommands_.currentHeadingLevel();

  for (int level = 1; level <= 6; ++level) {
    const QString id = QStringLiteral("paragraph.heading_%1").arg(level);
    window.commands_.setEnabled(id, editable);
    window.commands_.setChecked(id, headingLevel == level);
  }

  window.commands_.setEnabled(QStringLiteral("paragraph.paragraph"), editable);
  window.commands_.setChecked(QStringLiteral("paragraph.paragraph"), headingLevel == 0 && editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.promote_heading"), editable && headingLevel > 1);
  window.commands_.setEnabled(QStringLiteral("paragraph.demote_heading"), editable && headingLevel > 0 && headingLevel < 6);

  const bool wysiwyg = !sourceMode;
  const bool inCodeBlock = wysiwyg && window.renderCommands_.isInCodeBlock();
  const bool inMathBlock = wysiwyg && window.renderCommands_.isInMathBlock();
  window.commands_.setEnabled(QStringLiteral("paragraph.math_block"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.code_block"), wysiwyg);
  window.commands_.setChecked(QStringLiteral("paragraph.code_block"), inCodeBlock);
  window.commands_.setChecked(QStringLiteral("paragraph.math_block"), inMathBlock);
  window.commands_.setEnabled(QStringLiteral("paragraph.insert_before"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.insert_after"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.link_ref"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.footnote"), wysiwyg);
  window.commands_.setEnabled(QStringLiteral("paragraph.hr"), wysiwyg);

  window.commands_.setEnabled(QStringLiteral("paragraph.quote"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.ordered_list"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.unordered_list"), editable);
  window.commands_.setEnabled(QStringLiteral("paragraph.task_list"), editable);
}

void muffin::MainWindowActionBinder::updateCodeActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool codeActive = !sourceMode && window.renderCommands_.isInCodeBlock();
  const bool codeEditing = !sourceMode && window.renderCommands_.isEditingCodeBlock();
  window.commands_.setEnabled(QStringLiteral("code.enter_edit"), codeActive);
  window.commands_.setEnabled(QStringLiteral("code.exit_edit"), codeEditing);
  window.commands_.setEnabled(QStringLiteral("code.set_language"), codeActive || codeEditing);
}

void muffin::MainWindowActionBinder::updateHtmlActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool htmlActive = !sourceMode && window.renderCommands_.isInHtmlBlock();
  const bool htmlEditing = !sourceMode && window.renderCommands_.isEditingHtmlBlock();
  window.commands_.setEnabled(QStringLiteral("html.enter_edit"), htmlActive);
  window.commands_.setEnabled(QStringLiteral("html.exit_edit"), htmlEditing);
  window.commands_.setEnabled(QStringLiteral("html.set_source"), htmlActive || htmlEditing);
}

void muffin::MainWindowActionBinder::updateMathActions(MainWindow& window) {
  const bool sourceMode = window.backend_->isSourceMode();
  const bool mathActive = !sourceMode && window.renderCommands_.isInMathBlock();
  const bool mathEditing = !sourceMode && window.renderCommands_.isEditingMathBlock();
  window.commands_.setEnabled(QStringLiteral("math.enter_edit"), mathActive);
  window.commands_.setEnabled(QStringLiteral("math.exit_edit"), mathEditing);
  window.commands_.setEnabled(QStringLiteral("math.set_tex"), mathActive || mathEditing);
}

void muffin::MainWindowActionBinder::updateContextActions(MainWindow& window) {
  updateEditActions(window);
  updateTableActions(window);
  updateParagraphActions(window);
  updateCodeActions(window);
  updateHtmlActions(window);
  updateMathActions(window);
}

void muffin::MainWindowActionBinder::updateThemeActions(MainWindow& window) {
  const QString current = window.themeManager_.currentThemeName();
  window.commands_.setChecked(QStringLiteral("theme.github"), current == QStringLiteral("github"));
  window.commands_.setChecked(QStringLiteral("theme.newsprint"), current == QStringLiteral("newsprint"));
  window.commands_.setChecked(QStringLiteral("theme.night"), current == QStringLiteral("night"));
  window.commands_.setChecked(QStringLiteral("theme.pixyll"), current == QStringLiteral("pixyll"));
  window.commands_.setChecked(QStringLiteral("theme.whitey"), current == QStringLiteral("whitey"));
}
