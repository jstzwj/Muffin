#include "app/MainWindow.h"

#include "app/LanguageManager.h"
#include "app/SidebarWidget.h"
#include "document/MarkdownTypes.h"
#include "document/SelectionSerializer.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

namespace {

Q_LOGGING_CATEGORY(mainWindowPerf, "muffin.perf", QtWarningMsg)

class PerfTimer {
public:
  explicit PerfTimer(const char* label) : label_(label), enabled_(mainWindowPerf().isDebugEnabled()) {
    if (enabled_) {
      timer_.start();
    }
  }

  ~PerfTimer() {
    if (enabled_) {
      qCDebug(mainWindowPerf).nospace() << label_ << " " << timer_.nsecsElapsed() / 1000000.0 << " ms";
    }
  }

private:
  const char* label_;
  bool enabled_ = false;
  QElapsedTimer timer_;
};

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

  connect(editor_, &SourceEditorWidget::textEdited, &session_, &DocumentSession::updateFromEditor);
  connect(editor_, &SourceEditorWidget::cursorPositionChanged, this, [this](int line, int column) {
    updateCursorStatus(line, column);
    if (typewriterMode_ && backend_->isSourceMode()) {
      backend_->centerCursor();
    }
  });
  connect(editor_, &SourceEditorWidget::cursorPositionChanged, this, [this](int, int) { updateEditActions(); });
  connect(&editorController_, &EditorController::cursorChanged, this, [this](const HitTestResult& hit) {
    updateRenderCursorStatus(hit);
    if (typewriterMode_ && !backend_->isSourceMode()) {
      renderView_->scrollToCursorCenteredAnimated();
    }
  });
  connect(renderView_, &EditorView::codeLanguageCommitted, this, [this](NodeId codeId, const QString& language) {
    if (backend_->isSourceMode()) {
      return;
    }
    editorController_.codeFenceController().setLanguageFor(codeId, language);
  });
  connect(renderView_, &EditorView::tableResizeRequested, this, [this](int rows, int columns) {
    if (!backend_->isSourceMode()) {
      editorController_.tableController().resizeCurrentTable(rows, columns);
    }
  });
  connect(renderView_, &EditorView::tableColumnAlignmentRequested, this, [this](TableAlignment alignment) {
    if (!backend_->isSourceMode()) {
      editorController_.tableController().setCurrentColumnAlignment(alignment);
    }
  });
  connect(renderView_, &EditorView::tableDeleteRequested, this, [this] {
    if (!backend_->isSourceMode()) {
      editorController_.tableController().deleteCurrentTable();
    }
  });
  connect(renderView_, &EditorView::tableMoreActionsRequested, this, [this](QPoint globalPos) {
    if (backend_->isSourceMode()) {
      return;
    }
    updateTableActions();
    updateParagraphActions();
    QMenu menu(this);
    const QStringList ids = {
        QStringLiteral("table.insert_table"),
        QStringLiteral("table.insert_row_before"),
        QStringLiteral("table.insert_row_after"),
        QStringLiteral("table.insert_column_before"),
        QStringLiteral("table.insert_column_after"),
        QStringLiteral("table.move_row_up"),
        QStringLiteral("table.move_row_down"),
        QStringLiteral("table.move_column_left"),
        QStringLiteral("table.move_column_right"),
        QStringLiteral("table.delete_row"),
        QStringLiteral("table.delete_column"),
        QStringLiteral("table.copy_table"),
        QStringLiteral("table.format_source"),
        QStringLiteral("table.align_none"),
        QStringLiteral("table.delete_table"),
    };
    for (const QString& id : ids) {
      if (id == QStringLiteral("table.insert_row_before") || id == QStringLiteral("table.insert_column_before") || id == QStringLiteral("table.move_row_up") ||
          id == QStringLiteral("table.delete_row") || id == QStringLiteral("table.copy_table") || id == QStringLiteral("table.align_none") ||
          id == QStringLiteral("table.delete_table")) {
        menu.addSeparator();
      }
      if (QAction* action = commands_.action(id)) {
        menu.addAction(action);
      }
    }
    menu.exec(globalPos);
  });

  connect(&session_, &DocumentSession::documentTextChanged, this, [this](const QString& text) {
    PerfTimer perf("main.documentTextChanged.consumer");
    if (backend_->isSourceMode()) {
      editor_->setText(text);
      sourceEditorDirty_ = false;
      return;
    }
    sourceEditorDirty_ = true;
  });
  connect(&session_, &DocumentSession::documentLocallyEdited, this, [this](qsizetype, qsizetype, const QString&) {
    PerfTimer perf("main.documentLocallyEdited.consumer");
    sourceEditorDirty_ = true;
  });
  connect(&session_, &DocumentSession::filePathChanged, this, &MainWindow::updateTitle);
  connect(&session_, &DocumentSession::filePathChanged, this, &MainWindow::updateFileActions);
  connect(&session_, &DocumentSession::filePathChanged, this, &MainWindow::refreshSidebarDocumentInfo);
  connect(&session_, &DocumentSession::modifiedChanged, this, &MainWindow::updateTitle);
  connect(&session_, &DocumentSession::modifiedChanged, this, &MainWindow::updateStatus);
  connect(&session_, &DocumentSession::modifiedChanged, this, &MainWindow::refreshSidebarDocumentInfo);
  connect(&session_, &DocumentSession::parsed, this, [this] {
    PerfTimer perf("main.parsed.consumer");
    if (!session_.lastParseWasLocalEdit()) {
      renderView_->setDocument(session_.document());
    }
    scheduleWordCountUpdate();
    updateStatus();
    refreshSidebarOutline();
  });
  connect(&editorController_, &EditorController::stateChanged, this, [this] {
    updateStatus();
    updateContextActions();
  });
  connect(&themeManager_, &ThemeManager::themeChanged, this, [this](const QString& name) {
    applyTheme(name);
  });
  connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this] {
    retranslateUi();
    if (sidebar_) {
      sidebar_->retranslateUi();
    }
  });

  commands_.bind(QStringLiteral("file.new"), [this] {
    editorController_.clearHistoryAndSelection();
    fileController_.newFile(session_, this);
  });
  commands_.bind(QStringLiteral("file.new_window"), [this] { openNewWindow(); });
  commands_.bind(QStringLiteral("file.open_folder"), [this] { openFolder(); });
  commands_.bind(QStringLiteral("file.open"), [this] {
    if (fileController_.open(session_, this)) {
      editorController_.clearHistoryAndSelection();
      addRecentFile(session_.filePath());
    }
  });
  commands_.bind(QStringLiteral("file.save"), [this] {
    if (fileController_.save(session_, this)) {
      addRecentFile(session_.filePath());
    }
  });
  commands_.bind(QStringLiteral("file.save_as"), [this] {
    if (fileController_.saveAs(session_, this)) {
      addRecentFile(session_.filePath());
    }
  });
  commands_.bind(QStringLiteral("file.properties"), [this] { showDocumentProperties(); });
  commands_.bind(QStringLiteral("file.preferences"), [this] { showPreferences(); });
  commands_.bind(QStringLiteral("file.print"), [this] { printDocument(); });
  commands_.bind(QStringLiteral("file.reveal"), [this] { revealCurrentFile(); });
  commands_.bind(QStringLiteral("file.close"), [this] {
    close();
  });
  commands_.bind(QStringLiteral("file.move_to"), [this] { moveToFile(); });
  commands_.bind(QStringLiteral("file.save_all"), [this] { saveAllOpenFiles(); });
  commands_.bind(QStringLiteral("file.sidebar"), [this] { showInSidebar(); });
  commands_.bind(QStringLiteral("file.delete"), [this] { deleteFile(); });

  commands_.bind(QStringLiteral("edit.undo"), [this] { undoEdit(); });
  commands_.bind(QStringLiteral("edit.redo"), [this] { redoEdit(); });
  commands_.bind(QStringLiteral("edit.cut"), [this] { backend_->cut(); });
  commands_.bind(QStringLiteral("edit.copy"), [this] { backend_->copy(); });
  commands_.bind(QStringLiteral("edit.paste"), [this] { backend_->paste(); });
  commands_.bind(QStringLiteral("edit.select_all"), [this] { backend_->selectAll(); });
  commands_.bind(QStringLiteral("edit.delete"), [this] { backend_->deleteForward(); });
  commands_.bind(QStringLiteral("edit.copy_plain"), [this] { backend_->copyAsPlainText(); });
  commands_.bind(QStringLiteral("edit.copy_markdown"), [this] { backend_->copyAsMarkdown(); });
  commands_.bind(QStringLiteral("edit.copy_html"), [this] { backend_->copyAsHtml(); });
  commands_.bind(QStringLiteral("edit.paste_plain"), [this] { backend_->pasteAsPlainText(); });
  commands_.bind(QStringLiteral("edit.select_line"), [this] { backend_->selectLine(); });
  commands_.bind(QStringLiteral("edit.select_format"), [this] { backend_->selectFormatSpan(); });
  commands_.bind(QStringLiteral("edit.move_line_up"), [this] { backend_->moveLineUp(); });
  commands_.bind(QStringLiteral("edit.move_line_down"), [this] { backend_->moveLineDown(); });

  commands_.bind(QStringLiteral("edit.find"), [this] {
    showFindBar();
  });

  commands_.bind(QStringLiteral("edit.replace"), [this] {
    showReplaceBar();
  });

  commands_.bind(QStringLiteral("edit.find_next"), [this] {
    if (!findBar_ || !findBar_->isVisible()) {
      showFindBar();
    }
    performFindNext();
  });

  commands_.bind(QStringLiteral("edit.find_previous"), [this] {
    if (!findBar_ || !findBar_->isVisible()) {
      showFindBar();
    }
    performFindPrevious();
  });

  connect(findBar_, &FindBarWidget::findRequested, this, &MainWindow::performFind);
  connect(findBar_, &FindBarWidget::closed, this, &MainWindow::hideFindBar);
  connect(findBar_, &FindBarWidget::replaceRequested, this, &MainWindow::performReplace);
  connect(findBar_, &FindBarWidget::replaceAllRequested, this, &MainWindow::performReplaceAll);

  commands_.bind(QStringLiteral("format.bold"), [this] { backend_->toggleBold(); });
  commands_.bind(QStringLiteral("format.italic"), [this] { backend_->toggleItalic(); });
  commands_.bind(QStringLiteral("format.code"), [this] { backend_->toggleCode(); });
  commands_.bind(QStringLiteral("format.link"), [this] { backend_->insertLink(); });

  commands_.bind(QStringLiteral("table.insert_row_before"), [this] { editorController_.tableController().insertRowBefore(); });
  commands_.bind(QStringLiteral("table.insert_row_after"), [this] { editorController_.tableController().insertRowAfter(); });
  commands_.bind(QStringLiteral("table.delete_row"), [this] { editorController_.tableController().deleteCurrentRow(); });
  commands_.bind(QStringLiteral("table.move_row_up"), [this] { editorController_.tableController().moveCurrentRowUp(); });
  commands_.bind(QStringLiteral("table.move_row_down"), [this] { editorController_.tableController().moveCurrentRowDown(); });
  commands_.bind(QStringLiteral("table.insert_column_before"), [this] { editorController_.tableController().insertColumnBefore(); });
  commands_.bind(QStringLiteral("table.insert_column_after"), [this] { editorController_.tableController().insertColumnAfter(); });
  commands_.bind(QStringLiteral("table.delete_column"), [this] { editorController_.tableController().deleteCurrentColumn(); });
  commands_.bind(QStringLiteral("table.move_column_left"), [this] { editorController_.tableController().moveCurrentColumnLeft(); });
  commands_.bind(QStringLiteral("table.move_column_right"), [this] { editorController_.tableController().moveCurrentColumnRight(); });
  commands_.bind(QStringLiteral("table.align_left"), [this] { editorController_.tableController().setCurrentColumnAlignment(TableAlignment::Left); });
  commands_.bind(QStringLiteral("table.align_center"), [this] { editorController_.tableController().setCurrentColumnAlignment(TableAlignment::Center); });
  commands_.bind(QStringLiteral("table.align_right"), [this] { editorController_.tableController().setCurrentColumnAlignment(TableAlignment::Right); });
  commands_.bind(QStringLiteral("table.align_none"), [this] { editorController_.tableController().setCurrentColumnAlignment(TableAlignment::None); });
  commands_.bind(QStringLiteral("table.copy_table"), [this] { editorController_.tableController().copyCurrentTable(); });
  commands_.bind(QStringLiteral("table.format_source"), [this] { editorController_.tableController().formatCurrentTableSource(); });
  commands_.bind(QStringLiteral("table.delete_table"), [this] { editorController_.tableController().deleteCurrentTable(); });
  commands_.bind(QStringLiteral("table.insert_table"), [this] { insertTableWithDialog(); });
  commands_.bind(QStringLiteral("paragraph.yaml"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Yaml); });
  commands_.bind(QStringLiteral("paragraph.toml"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Toml); });
  commands_.bind(QStringLiteral("paragraph.json"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Json); });

  // Heading commands
  for (int level = 1; level <= 6; ++level) {
    commands_.bind(QStringLiteral("paragraph.heading_%1").arg(level), [this, level] {
      if (!backend_->isSourceMode()) editorController_.paragraphController().setHeadingLevel(level);
    });
  }
  commands_.bind(QStringLiteral("paragraph.paragraph"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().setHeadingLevel(0);
  });
  commands_.bind(QStringLiteral("paragraph.promote_heading"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().promoteHeading();
  });
  commands_.bind(QStringLiteral("paragraph.demote_heading"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().demoteHeading();
  });

  // Toggle code/math block commands
  commands_.bind(QStringLiteral("paragraph.math_block"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().toggleFormulaBlock();
  });
  commands_.bind(QStringLiteral("paragraph.code_block"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().toggleCodeBlock();
  });
  commands_.bind(QStringLiteral("paragraph.insert_before"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().insertParagraphBefore();
  });
  commands_.bind(QStringLiteral("paragraph.insert_after"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().insertParagraphAfter();
  });
  commands_.bind(QStringLiteral("paragraph.link_ref"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().insertLinkReference();
  });
  commands_.bind(QStringLiteral("paragraph.footnote"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().insertFootnoteDefinition();
  });
  commands_.bind(QStringLiteral("paragraph.hr"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().insertHorizontalRule();
  });

  // Block conversion commands
  commands_.bind(QStringLiteral("paragraph.quote"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().toggleQuote();
  });
  commands_.bind(QStringLiteral("paragraph.ordered_list"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().convertToOrderedList();
  });
  commands_.bind(QStringLiteral("paragraph.unordered_list"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().convertToUnorderedList();
  });
  commands_.bind(QStringLiteral("paragraph.task_list"), [this] {
    if (!backend_->isSourceMode()) editorController_.paragraphController().convertToTaskList();
  });

  commands_.bind(QStringLiteral("code.enter_edit"), [this] { editorController_.codeFenceController().enterEditMode(); });
  commands_.bind(QStringLiteral("code.exit_edit"), [this] { editorController_.codeFenceController().exitEditMode(); });
  commands_.bind(QStringLiteral("code.set_language"), [this] {
    bool ok = false;
    const QString language = QInputDialog::getText(this, tr("Code Language"), tr("Language:"), QLineEdit::Normal, QString(), &ok);
    if (ok) {
      editorController_.codeFenceController().setLanguage(language);
    }
  });

  commands_.bind(QStringLiteral("html.enter_edit"), [this] { editorController_.htmlLiteral().enterEditMode(); });
  commands_.bind(QStringLiteral("html.exit_edit"), [this] { editorController_.htmlLiteral().exitEditMode(); });
  commands_.bind(QStringLiteral("html.set_source"), [this] {
    bool ok = false;
    const QString html = QInputDialog::getMultiLineText(this, tr("HTML Source"), tr("HTML:"), QString(), &ok);
    if (ok) {
      editorController_.htmlLiteral().setContent(html);
    }
  });

  commands_.bind(QStringLiteral("math.enter_edit"), [this] { editorController_.mathLiteral().enterEditMode(); });
  commands_.bind(QStringLiteral("math.exit_edit"), [this] { editorController_.mathLiteral().exitEditMode(); });
  commands_.bind(QStringLiteral("math.set_tex"), [this] {
    bool ok = false;
    const QString tex = QInputDialog::getMultiLineText(this, tr("Math TeX"), tr("TeX:"), QString(), &ok);
    if (ok) {
      editorController_.mathLiteral().setContent(tex);
    }
  });

  commands_.bind(QStringLiteral("view.word_wrap"), [this] {
    const bool enabled = commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
    editor_->setWordWrapEnabled(enabled);
    QSettings().setValue(QStringLiteral("view/wordWrap"), enabled);
  });

  commands_.bind(QStringLiteral("edit.linebreak_crlf"), [this] {
    commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), true);
    commands_.setChecked(QStringLiteral("edit.linebreak_lf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 1);
  });
  commands_.bind(QStringLiteral("edit.linebreak_lf"), [this] {
    commands_.setChecked(QStringLiteral("edit.linebreak_lf"), true);
    commands_.setChecked(QStringLiteral("edit.linebreak_crlf"), false);
    QSettings().setValue(QStringLiteral("editor/defaultLineBreak"), 0);
  });
  commands_.bind(QStringLiteral("edit.trailing_newline"), [this] {
    const bool checked = commands_.action(QStringLiteral("edit.trailing_newline"))->isChecked();
    QSettings().setValue(QStringLiteral("editor/trailingNewline"), checked);
  });

  commands_.bind(QStringLiteral("view.sidebar"), [this] {
    updateSidebarMode();
    QSettings().setValue(QStringLiteral("view/sidebarVisible"),
        commands_.action(QStringLiteral("view.sidebar"))->isChecked());
  });
  commands_.bind(QStringLiteral("view.outline"), [this] { setSidebarPanel(SidebarWidget::Panel::Outline); });
  commands_.bind(QStringLiteral("view.file_tree"), [this] { setSidebarPanel(SidebarWidget::Panel::Files); });
  commands_.bind(QStringLiteral("view.source_mode"), [this] {
    updateViewMode();
    QSettings().setValue(QStringLiteral("view/sourceMode"),
        commands_.action(QStringLiteral("view.source_mode"))->isChecked());
  });
  commands_.bind(QStringLiteral("view.status_bar"), [this] {
    const bool visible = commands_.action(QStringLiteral("view.status_bar"))->isChecked();
    setStatusBarVisible(visible);
    saveAppearanceStatusBarVisible(visible);
  });
  commands_.bind(QStringLiteral("view.fullscreen"), [this] {
    isFullScreen() ? showNormal() : showFullScreen();
  });
  commands_.bind(QStringLiteral("view.focus"), [this] {
    const bool checked = commands_.action(QStringLiteral("view.focus"))->isChecked();
    setFocusMode(checked);
    saveAppearanceFocusMode(checked);
  });
  commands_.bind(QStringLiteral("view.typewriter"), [this] {
    const bool checked = commands_.action(QStringLiteral("view.typewriter"))->isChecked();
    setTypewriterMode(checked);
    saveAppearanceTypewriterMode(checked);
  });
  commands_.bind(QStringLiteral("view.zoom_in"), [this] {
    setZoomPercent(zoomPercent_ + 10);
    saveAppearanceZoomPercent(zoomPercent_);
  });
  commands_.bind(QStringLiteral("view.zoom_out"), [this] {
    setZoomPercent(zoomPercent_ - 10);
    saveAppearanceZoomPercent(zoomPercent_);
  });
  commands_.bind(QStringLiteral("view.actual_size"), [this] {
    setZoomPercent(100);
    saveAppearanceZoomPercent(zoomPercent_);
  });

  commands_.bind(QStringLiteral("help.website"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin")));
  });

  commands_.bind(QStringLiteral("help.changelog"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/blob/main/CHANGELOG.md")));
  });

  commands_.bind(QStringLiteral("help.feedback"), [] {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/jstzwj/Muffin/issues")));
  });

  commands_.bind(QStringLiteral("help.about"), [this] {
    QMessageBox::about(
        this,
        tr("About Muffin"),
        tr("Muffin %1\n\nA fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets.")
            .arg(QApplication::applicationVersion()));
  });

  commands_.bind(QStringLiteral("theme.github"), [this] {
    if (themeManager_.setTheme(QStringLiteral("github"))) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  commands_.bind(QStringLiteral("theme.newsprint"), [this] {
    if (themeManager_.setTheme(QStringLiteral("newsprint"))) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  commands_.bind(QStringLiteral("theme.night"), [this] {
    if (themeManager_.setTheme(QStringLiteral("night"))) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  commands_.bind(QStringLiteral("theme.pixyll"), [this] {
    if (themeManager_.setTheme(QStringLiteral("pixyll"))) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });
  commands_.bind(QStringLiteral("theme.whitey"), [this] {
    if (themeManager_.setTheme(QStringLiteral("whitey"))) {
      saveAppearanceTheme(themeManager_.currentThemeName());
    }
  });

  connect(sidebarButton_, &QToolButton::clicked, this, [this] {
    if (QAction* action = commands_.action(QStringLiteral("view.sidebar"))) {
      action->trigger();
    }
  });
  connect(sourceModeButton_, &QToolButton::clicked, this, [this] {
    if (QAction* action = commands_.action(QStringLiteral("view.source_mode"))) {
      action->trigger();
    }
  });
  connect(sidebar_, &SidebarWidget::newFileRequested, this, [this] {
    if (QAction* action = commands_.action(QStringLiteral("file.new"))) {
      action->trigger();
    }
  });
  connect(sidebar_, &SidebarWidget::newWindowRequested, this, [this] {
    if (QAction* action = commands_.action(QStringLiteral("file.new_window"))) {
      action->trigger();
    }
  });
  connect(sidebar_, &SidebarWidget::openFolderRequested, this, [this] {
    if (QAction* action = commands_.action(QStringLiteral("file.open_folder"))) {
      action->trigger();
    }
  });
  connect(sidebar_, &SidebarWidget::fileOpenRequested, this, [this](const QString& path) {
    openFile(path);
  });
  connect(sidebar_, &SidebarWidget::outlineActivated, this, &MainWindow::activateOutlineNode);

  updateFileActions();
  updateContextActions();
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
  updateContextActions();
  updateStatus();
}

void muffin::MainWindow::updateEditActions() {
  const bool src = backend_->isSourceMode();
  const bool hasCursor = src ? true : editorController_.selection().hasCursor();
  const bool hasSelection = src
      ? editor_->editor()->textCursor().hasSelection()
      : hasCursor && !editorController_.selection().selection().isCollapsed();

  commands_.setEnabled(QStringLiteral("edit.delete"), hasCursor);
  commands_.setEnabled(QStringLiteral("edit.copy_plain"), hasSelection);
  commands_.setEnabled(QStringLiteral("edit.copy_markdown"), hasSelection);
  commands_.setEnabled(QStringLiteral("edit.copy_html"), hasSelection);
  commands_.setEnabled(QStringLiteral("edit.paste_plain"), true);
  commands_.setEnabled(QStringLiteral("edit.select_line"), hasCursor);
  commands_.setEnabled(QStringLiteral("edit.select_format"), hasCursor);
  commands_.setEnabled(QStringLiteral("edit.move_line_up"), hasCursor);
  commands_.setEnabled(QStringLiteral("edit.move_line_down"), hasCursor);
  commands_.setEnabled(QStringLiteral("edit.find"), true);
  commands_.setEnabled(QStringLiteral("edit.replace"), true);
  commands_.setEnabled(QStringLiteral("edit.find_next"), true);
  commands_.setEnabled(QStringLiteral("edit.find_previous"), true);
}

void muffin::MainWindow::updateTableActions() {
  const bool enabled = !backend_->isSourceMode() && editorController_.tableController().currentCell().isValid();
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
    commands_.setEnabled(id, enabled);
  }
  commands_.setEnabled(QStringLiteral("table.insert_table"), !backend_->isSourceMode());
}

void muffin::MainWindow::updateParagraphActions() {
  const bool sourceMode = backend_->isSourceMode();
  const bool editable = !sourceMode && editorController_.paragraphController().isOnEditableBlock();
  const int headingLevel = sourceMode ? -1 : editorController_.paragraphController().currentHeadingLevel();

  // Heading commands: enabled when on an editable block (paragraph or heading)
  for (int level = 1; level <= 6; ++level) {
    const QString id = QStringLiteral("paragraph.heading_%1").arg(level);
    commands_.setEnabled(id, editable);
    commands_.setChecked(id, headingLevel == level);
  }

  // Paragraph (Ctrl+0): always enabled in WYSIWYG mode, checked when not a heading
  commands_.setEnabled(QStringLiteral("paragraph.paragraph"), editable);
  commands_.setChecked(QStringLiteral("paragraph.paragraph"), headingLevel == 0 && editable);

  // Promote: enabled only on headings > level 1
  commands_.setEnabled(QStringLiteral("paragraph.promote_heading"), editable && headingLevel > 1);
  // Demote: enabled only on headings < level 6
  commands_.setEnabled(QStringLiteral("paragraph.demote_heading"), editable && headingLevel > 0 && headingLevel < 6);

  // Block insert commands: enabled in any WYSIWYG mode
  const bool wysiwyg = !sourceMode;
  const bool inCodeBlock = wysiwyg && editorController_.codeFenceController().currentCodeFenceId().isValid();
  const bool inMathBlock = wysiwyg && editorController_.mathLiteral().currentBlockId().isValid();
  commands_.setEnabled(QStringLiteral("paragraph.math_block"), wysiwyg);
  commands_.setEnabled(QStringLiteral("paragraph.code_block"), wysiwyg);
  commands_.setChecked(QStringLiteral("paragraph.code_block"), inCodeBlock);
  commands_.setChecked(QStringLiteral("paragraph.math_block"), inMathBlock);
  commands_.setEnabled(QStringLiteral("paragraph.insert_before"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.insert_after"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.link_ref"), wysiwyg);
  commands_.setEnabled(QStringLiteral("paragraph.footnote"), wysiwyg);
  commands_.setEnabled(QStringLiteral("paragraph.hr"), wysiwyg);

  // Block conversions: enabled when on editable block
  commands_.setEnabled(QStringLiteral("paragraph.quote"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.ordered_list"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.unordered_list"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.task_list"), editable);
}

void muffin::MainWindow::updateCodeActions() {
  const bool codeActive = !backend_->isSourceMode() && editorController_.codeFenceController().currentCodeFenceId().isValid();
  commands_.setEnabled(QStringLiteral("code.enter_edit"), codeActive);
  commands_.setEnabled(QStringLiteral("code.exit_edit"), !backend_->isSourceMode() && editorController_.codeFenceController().isEditing());
  commands_.setEnabled(QStringLiteral("code.set_language"), codeActive || (!backend_->isSourceMode() && editorController_.codeFenceController().isEditing()));
}

void muffin::MainWindow::updateHtmlActions() {
  const bool htmlActive = !backend_->isSourceMode() && editorController_.htmlLiteral().currentBlockId().isValid();
  commands_.setEnabled(QStringLiteral("html.enter_edit"), htmlActive);
  commands_.setEnabled(QStringLiteral("html.exit_edit"), !backend_->isSourceMode() && editorController_.htmlLiteral().isEditing());
  commands_.setEnabled(QStringLiteral("html.set_source"), htmlActive || (!backend_->isSourceMode() && editorController_.htmlLiteral().isEditing()));
}

void muffin::MainWindow::updateMathActions() {
  const bool mathActive = !backend_->isSourceMode() && editorController_.mathLiteral().currentBlockId().isValid();
  commands_.setEnabled(QStringLiteral("math.enter_edit"), mathActive);
  commands_.setEnabled(QStringLiteral("math.exit_edit"), !backend_->isSourceMode() && editorController_.mathLiteral().isEditing());
  commands_.setEnabled(QStringLiteral("math.set_tex"), mathActive || (!backend_->isSourceMode() && editorController_.mathLiteral().isEditing()));
}

void muffin::MainWindow::syncSourceEditorIfNeeded() {
  if (!editor_ || !sourceEditorDirty_) {
    return;
  }
  editor_->setText(session_.markdownText());
  sourceEditorDirty_ = false;
}

void muffin::MainWindow::updateContextActions() {
  updateEditActions();
  updateTableActions();
  updateParagraphActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
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

  editorController_.tableController().insertTable(rowSpin->value(), columnSpin->value());
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
