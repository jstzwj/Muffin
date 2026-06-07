#include "app/MainWindow.h"
#include "editor/EditorView.h"

#include <QMenu>
#include <QMenuBar>
#include <QToolButton>

namespace {

QMenu* addDisabledMenu(QMenu* parent, const QString& title) {
  QMenu* menu = parent->addMenu(title);
  menu->setEnabled(false);
  return menu;
}

}  // namespace

QAction* muffin::MainWindow::addAction(
    QMenu* menu,
    const QString& id,
    const QString& text,
    const QKeySequence& shortcut,
    bool enabled) {
  QAction* action = menu->addAction(text);
  if (!shortcut.isEmpty()) {
    action->setShortcut(shortcut);
  }
  action->setEnabled(enabled);
  commands_.registerAction(id, action);
  return action;
}

QAction* muffin::MainWindow::addCheckAction(
    QMenu* menu,
    const QString& id,
    const QString& text,
    const QKeySequence& shortcut,
    bool checked,
    bool enabled) {
  QAction* action = addAction(menu, id, text, shortcut, enabled);
  action->setCheckable(true);
  action->setChecked(checked);
  return action;
}

void muffin::MainWindow::retranslateUi() {
  const bool sidebarChecked = commands_.action(QStringLiteral("view.sidebar")) && commands_.action(QStringLiteral("view.sidebar"))->isChecked();
  const bool sourceChecked = commands_.action(QStringLiteral("view.source_mode")) && commands_.action(QStringLiteral("view.source_mode"))->isChecked();
  const bool wordWrapChecked = !commands_.action(QStringLiteral("view.word_wrap")) || commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
  const bool statusBarChecked = !commands_.action(QStringLiteral("view.status_bar")) || commands_.action(QStringLiteral("view.status_bar"))->isChecked();
  const bool focusChecked = commands_.action(QStringLiteral("view.focus")) && commands_.action(QStringLiteral("view.focus"))->isChecked();
  const bool typewriterChecked = commands_.action(QStringLiteral("view.typewriter")) && commands_.action(QStringLiteral("view.typewriter"))->isChecked();

  setupMenuBar();

  if (QAction* action = commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(sidebarChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.source_mode"))) {
    action->setChecked(sourceChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.word_wrap"))) {
    action->setChecked(wordWrapChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.status_bar"))) {
    action->setChecked(statusBarChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(focusChecked);
  }
  if (QAction* action = commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(typewriterChecked);
  }

  // Re-apply focus/typewriter mode state after menu rebuild
  setFocusMode(focusChecked);
  focusMode_ = focusChecked;
  setTypewriterMode(typewriterChecked);

  if (sidebarButton_) {
    sidebarButton_->setToolTip(tr("Show / Hide Sidebar"));
  }
  if (sourceModeButton_) {
    sourceModeButton_->setToolTip(tr("Toggle source / rendered mode"));
  }

  updateFileActions();
  updateTableActions();
  updateParagraphActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  updateThemeActions();
  rebuildRecentFilesMenu();
  buildReopenEncodingMenu();
  if (renderView_) {
    renderView_->setDocument(session_.document());
  }
  updateStatus();
  wordCountDirty_ = true;
  updateWordCountNow();
}

void muffin::MainWindow::setupFileMenu() {
  QMenu* file = menuBar()->addMenu(tr("File"));
  addAction(file, QStringLiteral("file.new"), tr("New"), QKeySequence::New);
  addAction(file, QStringLiteral("file.new_window"), tr("New Window"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
  addAction(file, QStringLiteral("file.open"), tr("Open..."), QKeySequence::Open);
  addAction(file, QStringLiteral("file.open_folder"), tr("Open Folder..."));
  addAction(file, QStringLiteral("file.quick_open"), tr("Quick Open..."), {}, false);
  recentFilesMenu_ = file->addMenu(tr("Open Recent"));
  file->addSeparator();
  reopenEncodingMenu_ = file->addMenu(tr("Reopen with Encoding"));
  reopenEncodingMenu_->setEnabled(false);
  addAction(file, QStringLiteral("file.save"), tr("Save"), QKeySequence::Save);
  addAction(file, QStringLiteral("file.save_as"), tr("Save As..."), QKeySequence::SaveAs);
  addAction(file, QStringLiteral("file.move_to"), tr("Move To..."), {}, false);
  addAction(file, QStringLiteral("file.save_all"), tr("Save All Open Files..."), {}, false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.properties"), tr("Properties"));
  addAction(file, QStringLiteral("file.reveal"), tr("Show in File Manager..."));
  addAction(file, QStringLiteral("file.sidebar"), tr("Show in Sidebar"), {}, false);
  addAction(file, QStringLiteral("file.delete"), tr("Delete..."), {}, false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.import"), tr("Import..."), {}, false);
  addDisabledMenu(file, tr("Export"));
  addAction(file, QStringLiteral("file.print"), tr("Print..."), QKeySequence(QStringLiteral("Alt+Shift+P")));
  file->addSeparator();
  addAction(file, QStringLiteral("file.preferences"), tr("Preferences..."), QKeySequence(QStringLiteral("Ctrl+,")));
  addAction(file, QStringLiteral("file.close"), tr("Close"), QKeySequence::Close);
}

void muffin::MainWindow::setupEditMenu() {
  QMenu* edit = menuBar()->addMenu(tr("Edit"));
  addAction(edit, QStringLiteral("edit.undo"), tr("Undo"), QKeySequence::Undo);
  addAction(edit, QStringLiteral("edit.redo"), tr("Redo"), QKeySequence::Redo);
  edit->addSeparator();
  addAction(edit, QStringLiteral("edit.cut"), tr("Cut"), QKeySequence::Cut);
  addAction(edit, QStringLiteral("edit.copy"), tr("Copy"), QKeySequence::Copy);
  addAction(edit, QStringLiteral("edit.copy_image"), tr("Copy Image"), {}, false);
  addAction(edit, QStringLiteral("edit.paste"), tr("Paste"), QKeySequence::Paste);
  addAction(edit, QStringLiteral("edit.copy_plain"), tr("Copy as Plain Text"), {}, false);
  addAction(edit, QStringLiteral("edit.copy_markdown"), tr("Copy as Markdown"), QKeySequence(QStringLiteral("Ctrl+Shift+C")), false);
  addAction(edit, QStringLiteral("edit.copy_html"), tr("Copy as HTML"), {}, false);
  addAction(edit, QStringLiteral("edit.paste_plain"), tr("Paste as Plain Text"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), false);
  edit->addSeparator();
  QMenu* select = edit->addMenu(tr("Select"));
  addAction(select, QStringLiteral("edit.select_all"), tr("Select All"), QKeySequence::SelectAll);
  addAction(select, QStringLiteral("edit.select_line"), tr("Select Current Line"), {}, false);
  addAction(select, QStringLiteral("edit.select_format"), tr("Select Current Format Text"), {}, false);
  edit->addSeparator();
  addAction(edit, QStringLiteral("edit.move_line_up"), tr("Move Line Up"), QKeySequence(QStringLiteral("Alt+Up")), false);
  addAction(edit, QStringLiteral("edit.move_line_down"), tr("Move Line Down"), QKeySequence(QStringLiteral("Alt+Down")), false);
  addAction(edit, QStringLiteral("edit.delete"), tr("Delete"), {}, false);
  edit->addSeparator();
  addDisabledMenu(edit, tr("Delete Range"));
  addDisabledMenu(edit, tr("Math Tools"));
  addDisabledMenu(edit, tr("Smart Punctuation"));
  QMenu* lineBreaks = edit->addMenu(tr("Line Breaks"));
  addCheckAction(lineBreaks, QStringLiteral("edit.linebreak_crlf"), tr("Windows (CRLF)"), {}, true);
  addCheckAction(lineBreaks, QStringLiteral("edit.linebreak_lf"), tr("Unix (LF)"), {}, false);
  lineBreaks->addSeparator();
  addCheckAction(lineBreaks, QStringLiteral("edit.trailing_newline"), tr("Ensure Trailing Newline on Save"), {}, true);
  addAction(edit, QStringLiteral("edit.spellcheck"), tr("Spell Check..."), {}, false);
  QMenu* findMenu = edit->addMenu(tr("Find and Replace"));
  addAction(findMenu, QStringLiteral("edit.find"), tr("Find..."), QKeySequence::Find);
  addAction(findMenu, QStringLiteral("edit.replace"), tr("Replace..."), QKeySequence(QStringLiteral("Ctrl+H")));
  findMenu->addSeparator();
  addAction(findMenu, QStringLiteral("edit.find_next"), tr("Find Next"), QKeySequence(QStringLiteral("F3")));
  addAction(findMenu, QStringLiteral("edit.find_previous"), tr("Find Previous"), QKeySequence(QStringLiteral("Shift+F3")));
  addAction(edit, QStringLiteral("edit.symbols"), tr("Emoji and Symbols"), QKeySequence(QStringLiteral("Ctrl+.")), false);
}

void muffin::MainWindow::setupParagraphMenu() {
  QMenu* paragraph = menuBar()->addMenu(tr("Paragraph"));
  for (int level = 1; level <= 6; ++level) {
    addCheckAction(
        paragraph,
        QStringLiteral("paragraph.heading_%1").arg(level),
        tr("Heading %1").arg(level),
        QKeySequence(QStringLiteral("Ctrl+%1").arg(level)),
        false,
        false);
  }
  addCheckAction(paragraph, QStringLiteral("paragraph.paragraph"), tr("Paragraph"), QKeySequence(QStringLiteral("Ctrl+0")), true, false);
  addAction(paragraph, QStringLiteral("paragraph.promote_heading"), tr("Promote Heading"), QKeySequence(QStringLiteral("Ctrl+-")), false);
  addAction(paragraph, QStringLiteral("paragraph.demote_heading"), tr("Demote Heading"), QKeySequence(QStringLiteral("Ctrl+=")), false);
  paragraph->addSeparator();
  addDisabledMenu(paragraph, tr("Table"));
  addCheckAction(paragraph, QStringLiteral("paragraph.math_block"), tr("Formula Block"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), false, false);
  addCheckAction(paragraph, QStringLiteral("paragraph.code_block"), tr("Code Block"), QKeySequence(QStringLiteral("Ctrl+Shift+K")), false, false);
  addDisabledMenu(paragraph, tr("Code Tools"));
  addDisabledMenu(paragraph, tr("Alert"));
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.quote"), tr("Quote"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")), false);
  addAction(paragraph, QStringLiteral("paragraph.ordered_list"), tr("Ordered List"), QKeySequence(QStringLiteral("Ctrl+Shift+[")), false);
  addAction(paragraph, QStringLiteral("paragraph.unordered_list"), tr("Unordered List"), QKeySequence(QStringLiteral("Ctrl+Shift+]")), false);
  addAction(paragraph, QStringLiteral("paragraph.task_list"), tr("Task List"), QKeySequence(QStringLiteral("Ctrl+Shift+X")), false);
  addDisabledMenu(paragraph, tr("Task Status"));
  addDisabledMenu(paragraph, tr("List Indent"));
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.insert_before"), tr("Insert Paragraph Before"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.insert_after"), tr("Insert Paragraph After"), {}, false);
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.link_ref"), tr("Link Reference"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.footnote"), tr("Footnote"), {}, false);
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.hr"), tr("Horizontal Rule"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.toc"), tr("Table of Contents"), {}, false);
  QMenu* frontMatter = paragraph->addMenu(tr("Front Matter"));
  addAction(frontMatter, QStringLiteral("paragraph.yaml"), tr("YAML"));
  addAction(frontMatter, QStringLiteral("paragraph.toml"), tr("TOML"));
  addAction(frontMatter, QStringLiteral("paragraph.json"), tr("JSON"));
}

void muffin::MainWindow::setupFormatMenu() {
  QMenu* format = menuBar()->addMenu(tr("Format"));
  addCheckAction(format, QStringLiteral("format.bold"), tr("Bold"), QKeySequence::Bold);
  addCheckAction(format, QStringLiteral("format.italic"), tr("Italic"), QKeySequence::Italic);
  addCheckAction(format, QStringLiteral("format.underline"), tr("Underline"), QKeySequence::Underline, false, false);
  addCheckAction(format, QStringLiteral("format.code"), tr("Inline Code"), QKeySequence(QStringLiteral("Ctrl+Shift+`")));
  addCheckAction(format, QStringLiteral("format.strike"), tr("Strikethrough"), QKeySequence(QStringLiteral("Alt+Shift+5")), false, false);
  format->addSeparator();
  addAction(format, QStringLiteral("format.comment"), tr("Comment"), {}, false);
  addAction(format, QStringLiteral("format.link"), tr("Hyperlink"), QKeySequence(QStringLiteral("Ctrl+K")));
  addDisabledMenu(format, tr("Link Actions"));
  addDisabledMenu(format, tr("Image"));
  addAction(format, QStringLiteral("format.clear"), tr("Clear Style"), QKeySequence(QStringLiteral("Ctrl+\\")), false);
}

void muffin::MainWindow::setupTableMenu() {
  QMenu* table = menuBar()->addMenu(tr("Table"));
  table->menuAction()->setVisible(false);
  addAction(table, QStringLiteral("table.insert_table"), tr("Insert Table"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.insert_row_before"), tr("Insert Row Above"));
  addAction(table, QStringLiteral("table.insert_row_after"), tr("Insert Row Below"));
  addAction(table, QStringLiteral("table.delete_row"), tr("Delete Row"));
  addAction(table, QStringLiteral("table.move_row_up"), tr("Move Row Up"));
  addAction(table, QStringLiteral("table.move_row_down"), tr("Move Row Down"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.insert_column_before"), tr("Insert Column Left"));
  addAction(table, QStringLiteral("table.insert_column_after"), tr("Insert Column Right"));
  addAction(table, QStringLiteral("table.delete_column"), tr("Delete Column"));
  addAction(table, QStringLiteral("table.move_column_left"), tr("Move Column Left"));
  addAction(table, QStringLiteral("table.move_column_right"), tr("Move Column Right"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.align_left"), tr("Align Left"));
  addAction(table, QStringLiteral("table.align_center"), tr("Align Center"));
  addAction(table, QStringLiteral("table.align_right"), tr("Align Right"));
  addAction(table, QStringLiteral("table.align_none"), tr("Clear Alignment"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.delete_table"), tr("Delete Table"));
}

void muffin::MainWindow::setupCodeMenu() {
  QMenu* code = menuBar()->addMenu(tr("Code"));
  code->menuAction()->setVisible(false);
  addAction(code, QStringLiteral("code.enter_edit"), tr("Enter Edit"));
  addAction(code, QStringLiteral("code.exit_edit"), tr("Exit Edit"));
  addAction(code, QStringLiteral("code.set_language"), tr("Set Language..."));
}

void muffin::MainWindow::setupMathMenu() {
  QMenu* math = menuBar()->addMenu(tr("Math"));
  math->menuAction()->setVisible(false);
  addAction(math, QStringLiteral("math.enter_edit"), tr("Enter Edit"));
  addAction(math, QStringLiteral("math.exit_edit"), tr("Exit Edit"));
  addAction(math, QStringLiteral("math.set_tex"), tr("Set TeX..."));
}

void muffin::MainWindow::setupHtmlMenu() {
  QMenu* html = menuBar()->addMenu(QStringLiteral("HTML(&H)"));
  html->menuAction()->setVisible(false);
  addAction(html, QStringLiteral("html.enter_edit"), tr("Enter Edit"));
  addAction(html, QStringLiteral("html.exit_edit"), tr("Exit Edit"));
  addAction(html, QStringLiteral("html.set_source"), tr("Set HTML..."));
}

void muffin::MainWindow::setupViewMenu() {
  QMenu* view = menuBar()->addMenu(tr("View"));
  addCheckAction(view, QStringLiteral("view.sidebar"), tr("Show / Hide Sidebar"), QKeySequence(QStringLiteral("Ctrl+Shift+L")), false);
  addAction(view, QStringLiteral("view.outline"), tr("Outline"), QKeySequence(QStringLiteral("Ctrl+Shift+1")));
  addAction(view, QStringLiteral("view.file_tree"), tr("File Tree"), QKeySequence(QStringLiteral("Ctrl+Shift+3")));
  addAction(view, QStringLiteral("view.search"), tr("Search"), QKeySequence(QStringLiteral("Ctrl+Shift+F")), false);
  view->addSeparator();
  addCheckAction(view, QStringLiteral("view.source_mode"), tr("Source Code Mode"), QKeySequence(QStringLiteral("Ctrl+/")), false);
  addCheckAction(view, QStringLiteral("view.word_wrap"), tr("Word Wrap"), {}, true);
  addCheckAction(view, QStringLiteral("view.focus"), tr("Focus Mode"), QKeySequence(QStringLiteral("F8")), false);
  addCheckAction(view, QStringLiteral("view.typewriter"), tr("Typewriter Mode"), QKeySequence(QStringLiteral("F9")), false);
  addCheckAction(view, QStringLiteral("view.status_bar"), tr("Show Status Bar"), {}, true);
  view->addSeparator();
  addAction(view, QStringLiteral("view.word_count"), tr("Word Count Window"), {}, false);
  addAction(view, QStringLiteral("view.fullscreen"), tr("Toggle Full Screen"), QKeySequence(QStringLiteral("F11")));
  addCheckAction(view, QStringLiteral("view.always_on_top"), tr("Always on Top"), {}, false, false);
  view->addSeparator();
  addAction(view, QStringLiteral("view.actual_size"), tr("Actual Size"), QKeySequence(QStringLiteral("Ctrl+Shift+9")));
  addAction(view, QStringLiteral("view.zoom_in"), tr("Zoom In"), QKeySequence(QStringLiteral("Ctrl+Shift+=")));
  addAction(view, QStringLiteral("view.zoom_out"), tr("Zoom Out"), QKeySequence(QStringLiteral("Ctrl+Shift+-")));
  addAction(view, QStringLiteral("view.window_switch"), tr("Switch Windows"), QKeySequence(QStringLiteral("Ctrl+Tab")), false);
}

void muffin::MainWindow::setupThemeMenu() {
  QMenu* theme = menuBar()->addMenu(tr("Theme"));
  addCheckAction(theme, QStringLiteral("theme.github"), QStringLiteral("Github"), {}, true);
  addCheckAction(theme, QStringLiteral("theme.newsprint"), QStringLiteral("Newsprint"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.night"), QStringLiteral("Night"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.pixyll"), QStringLiteral("Pixyll"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.whitey"), QStringLiteral("Whitey"), {}, false);
}

void muffin::MainWindow::setupHelpMenu() {
  QMenu* help = menuBar()->addMenu(tr("Help"));
  addAction(help, QStringLiteral("help.quick_start"), tr("Quick Start"), {}, false);
  addAction(help, QStringLiteral("help.markdown_ref"), tr("Markdown Reference"), {}, false);
  addAction(help, QStringLiteral("help.custom_themes"), tr("Custom Themes"), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.acknowledgements"), tr("Acknowledgements"), {}, false);
  addAction(help, QStringLiteral("help.changelog"), tr("Changelog"));
  addAction(help, QStringLiteral("help.website"), tr("Official Website"));
  addAction(help, QStringLiteral("help.feedback"), tr("Feedback"));
  addAction(help, QStringLiteral("help.update"), tr("Check for Updates..."), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.about"), tr("About"));
}
