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
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QUrl>

namespace muffin {
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

}  // namespace

int countWords(const QString& text) {
  int count = 0;
  bool inWord = false;
  for (const QChar ch : text) {
    const bool wordChar = ch.isLetterOrNumber() || ch == QLatin1Char('_');
    if (wordChar && !inWord) {
      ++count;
    }
    inWord = wordChar;
  }
  return count;
}

QString zoneName(HitTestResult::Zone zone) {
  switch (zone) {
    case HitTestResult::Zone::Text:
      return QStringLiteral("text");
    case HitTestResult::Zone::Marker:
      return QStringLiteral("marker");
    case HitTestResult::Zone::TableCell:
      return QStringLiteral("table");
    case HitTestResult::Zone::Code:
      return QStringLiteral("code");
    case HitTestResult::Zone::Math:
      return QStringLiteral("math");
    case HitTestResult::Zone::Html:
      return QStringLiteral("html");
    case HitTestResult::Zone::FrontMatter:
      return QStringLiteral("front matter");
    case HitTestResult::Zone::Block:
      return QStringLiteral("block");
    case HitTestResult::Zone::BlockAfter:
      return QStringLiteral("block after");
    case HitTestResult::Zone::None:
    default:
      return QStringLiteral("none");
  }
}

void MainWindow::setupConnections() {
  editorController_.attach(&session_, renderView_);

  connect(editor_, &SourceEditorWidget::textEdited, &session_, &DocumentSession::updateFromEditor);
  connect(editor_, &SourceEditorWidget::cursorPositionChanged, this, [this](int line, int column) {
    updateCursorStatus(line, column);
    if (typewriterMode_ && sourceModeEnabled()) {
      editor_->editor()->centerCursor();
    }
  });
  connect(editor_, &SourceEditorWidget::cursorPositionChanged, this, [this](int, int) { updateEditActions(); });
  connect(&editorController_, &EditorController::cursorChanged, this, [this](const HitTestResult& hit) {
    updateRenderCursorStatus(hit);
    if (typewriterMode_ && !sourceModeEnabled()) {
      renderView_->scrollToCursorCentered();
    }
  });
  connect(renderView_, &EditorView::codeLanguageCommitted, this, [this](NodeId codeId, const QString& language) {
    if (sourceModeEnabled()) {
      return;
    }
    editorController_.codeFenceController().setLanguageFor(codeId, language);
  });
  connect(renderView_, &EditorView::tableResizeRequested, this, [this](int rows, int columns) {
    if (!sourceModeEnabled()) {
      editorController_.tableController().resizeCurrentTable(rows, columns);
    }
  });
  connect(renderView_, &EditorView::tableColumnAlignmentRequested, this, [this](TableAlignment alignment) {
    if (!sourceModeEnabled()) {
      editorController_.tableController().setCurrentColumnAlignment(alignment);
    }
  });
  connect(renderView_, &EditorView::tableDeleteRequested, this, [this] {
    if (!sourceModeEnabled()) {
      editorController_.tableController().deleteCurrentTable();
    }
  });
  connect(renderView_, &EditorView::tableMoreActionsRequested, this, [this](QPoint globalPos) {
    if (sourceModeEnabled()) {
      return;
    }
    updateTableActions();
    updateParagraphActions();
    QMenu menu(this);
    const QStringList ids = {
        QStringLiteral("table.insert_row_before"),
        QStringLiteral("table.insert_row_after"),
        QStringLiteral("table.delete_row"),
        QStringLiteral("table.insert_column_before"),
        QStringLiteral("table.insert_column_after"),
        QStringLiteral("table.delete_column"),
        QStringLiteral("table.move_row_up"),
        QStringLiteral("table.move_row_down"),
        QStringLiteral("table.move_column_left"),
        QStringLiteral("table.move_column_right"),
        QStringLiteral("table.align_none"),
        QStringLiteral("table.delete_table"),
    };
    for (const QString& id : ids) {
      if (id == QStringLiteral("table.insert_column_before") || id == QStringLiteral("table.move_row_up") || id == QStringLiteral("table.align_none") ||
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
    if (sourceModeEnabled()) {
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
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateStatus);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateEditActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateTableActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateParagraphActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateCodeActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateHtmlActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateMathActions);
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
  commands_.bind(QStringLiteral("edit.cut"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->cut();
    } else {
      editorController_.clipboardController().cut();
    }
  });
  commands_.bind(QStringLiteral("edit.copy"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->copy();
    } else {
      editorController_.clipboardController().copy();
    }
  });
  commands_.bind(QStringLiteral("edit.paste"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->paste();
    } else {
      editorController_.clipboardController().paste();
    }
  });
  commands_.bind(QStringLiteral("edit.select_all"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->selectAll();
    } else {
      editorController_.selectAll();
    }
  });

  commands_.bind(QStringLiteral("edit.delete"), [this] {
    if (sourceModeEnabled()) {
      QTextCursor cursor = editor_->editor()->textCursor();
      if (cursor.hasSelection()) {
        cursor.removeSelectedText();
      } else {
        cursor.deleteChar();
      }
      editor_->editor()->setTextCursor(cursor);
    } else {
      editorController_.inputController().deleteForward();
    }
  });

  commands_.bind(QStringLiteral("edit.copy_plain"), [this] {
    if (sourceModeEnabled()) {
      const QTextCursor cursor = editor_->editor()->textCursor();
      if (cursor.hasSelection()) {
        QApplication::clipboard()->setText(cursor.selectedText());
      }
    } else {
      editorController_.clipboardController().copyAsPlainText();
    }
  });

  commands_.bind(QStringLiteral("edit.copy_markdown"), [this] {
    if (sourceModeEnabled()) {
      const QTextCursor cursor = editor_->editor()->textCursor();
      if (cursor.hasSelection()) {
        auto* mimeData = new QMimeData();
        mimeData->setText(cursor.selectedText());
        mimeData->setData(QStringLiteral("text/markdown"), cursor.selectedText().toUtf8());
        QApplication::clipboard()->setMimeData(mimeData);
      }
    } else {
      editorController_.clipboardController().copyAsMarkdown();
    }
  });

  commands_.bind(QStringLiteral("edit.copy_html"), [this] {
    if (sourceModeEnabled()) {
      const QTextCursor cursor = editor_->editor()->textCursor();
      if (cursor.hasSelection()) {
        const QString html = SelectionSerializer::renderMarkdownToHtml(cursor.selectedText());
        if (!html.isEmpty()) {
          auto* mimeData = new QMimeData();
          mimeData->setHtml(html);
          mimeData->setText(html);
          QApplication::clipboard()->setMimeData(mimeData);
        }
      }
    } else {
      editorController_.clipboardController().copyAsHtml();
    }
  });

  commands_.bind(QStringLiteral("edit.paste_plain"), [this] {
    if (sourceModeEnabled()) {
      const QString text = QApplication::clipboard()->text();
      if (!text.isEmpty()) {
        editor_->editor()->insertPlainText(text);
      }
    } else {
      editorController_.clipboardController().pasteAsPlainText();
    }
  });

  commands_.bind(QStringLiteral("edit.select_line"), [this] {
    if (sourceModeEnabled()) {
      QTextCursor cursor = editor_->editor()->textCursor();
      cursor.movePosition(QTextCursor::StartOfBlock);
      cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
      editor_->editor()->setTextCursor(cursor);
    } else {
      editorController_.selectCurrentBlock();
    }
  });

  commands_.bind(QStringLiteral("edit.select_format"), [this] {
    if (sourceModeEnabled()) {
      QTextCursor cursor = editor_->editor()->textCursor();
      cursor.select(QTextCursor::WordUnderCursor);
      editor_->editor()->setTextCursor(cursor);
    } else {
      editorController_.selectCurrentFormatSpan();
    }
  });

  commands_.bind(QStringLiteral("edit.move_line_up"), [this] {
    if (sourceModeEnabled()) {
      moveSourceLineUp();
    } else {
      editorController_.moveBlockUp();
    }
  });

  commands_.bind(QStringLiteral("edit.move_line_down"), [this] {
    if (sourceModeEnabled()) {
      moveSourceLineDown();
    } else {
      editorController_.moveBlockDown();
    }
  });

  commands_.bind(QStringLiteral("edit.find"), [this] {
    showFindBar();
  });

  connect(findBar_, &FindBarWidget::findRequested, this, &MainWindow::performFind);
  connect(findBar_, &FindBarWidget::closed, this, &MainWindow::hideFindBar);
  connect(findBar_, &FindBarWidget::replaceRequested, this, &MainWindow::performReplace);
  connect(findBar_, &FindBarWidget::replaceAllRequested, this, &MainWindow::performReplaceAll);

  commands_.bind(QStringLiteral("format.bold"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("****"));
    } else {
      editorController_.stylizeController().toggleBold();
    }
  });
  commands_.bind(QStringLiteral("format.italic"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("**"));
    } else {
      editorController_.stylizeController().toggleItalic();
    }
  });
  commands_.bind(QStringLiteral("format.code"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("``"));
    } else {
      editorController_.stylizeController().toggleCode();
    }
  });
  commands_.bind(QStringLiteral("format.link"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("[](url)"));
    } else {
      editorController_.stylizeController().insertLink();
    }
  });

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
  commands_.bind(QStringLiteral("table.delete_table"), [this] { editorController_.tableController().deleteCurrentTable(); });
  commands_.bind(QStringLiteral("table.insert_table"), [this] { editorController_.tableController().insertTable(); });
  commands_.bind(QStringLiteral("paragraph.yaml"), [this] { editorController_.frontMatterController().insertFrontMatter(FrontMatterFormat::Yaml); });
  commands_.bind(QStringLiteral("paragraph.toml"), [this] { editorController_.frontMatterController().insertFrontMatter(FrontMatterFormat::Toml); });
  commands_.bind(QStringLiteral("paragraph.json"), [this] { editorController_.frontMatterController().insertFrontMatter(FrontMatterFormat::Json); });

  // Heading commands
  for (int level = 1; level <= 6; ++level) {
    commands_.bind(QStringLiteral("paragraph.heading_%1").arg(level), [this, level] {
      if (!sourceModeEnabled()) editorController_.paragraphController().setHeadingLevel(level);
    });
  }
  commands_.bind(QStringLiteral("paragraph.paragraph"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().setHeadingLevel(0);
  });
  commands_.bind(QStringLiteral("paragraph.promote_heading"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().promoteHeading();
  });
  commands_.bind(QStringLiteral("paragraph.demote_heading"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().demoteHeading();
  });

  // Block insert commands
  commands_.bind(QStringLiteral("paragraph.math_block"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().insertFormulaBlock();
  });
  commands_.bind(QStringLiteral("paragraph.code_block"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().insertCodeBlock();
  });
  commands_.bind(QStringLiteral("paragraph.insert_before"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().insertParagraphBefore();
  });
  commands_.bind(QStringLiteral("paragraph.insert_after"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().insertParagraphAfter();
  });
  commands_.bind(QStringLiteral("paragraph.link_ref"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().insertLinkReference();
  });

  // Block conversion commands
  commands_.bind(QStringLiteral("paragraph.quote"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().toggleQuote();
  });
  commands_.bind(QStringLiteral("paragraph.ordered_list"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().convertToOrderedList();
  });
  commands_.bind(QStringLiteral("paragraph.unordered_list"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().convertToUnorderedList();
  });
  commands_.bind(QStringLiteral("paragraph.task_list"), [this] {
    if (!sourceModeEnabled()) editorController_.paragraphController().convertToTaskList();
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

  commands_.bind(QStringLiteral("html.enter_edit"), [this] { editorController_.htmlBlockController().enterEditMode(); });
  commands_.bind(QStringLiteral("html.exit_edit"), [this] { editorController_.htmlBlockController().exitEditMode(); });
  commands_.bind(QStringLiteral("html.set_source"), [this] {
    bool ok = false;
    const QString html = QInputDialog::getMultiLineText(this, tr("HTML Source"), tr("HTML:"), QString(), &ok);
    if (ok) {
      editorController_.htmlBlockController().setHtml(html);
    }
  });

  commands_.bind(QStringLiteral("math.enter_edit"), [this] { editorController_.mathBlockController().enterEditMode(); });
  commands_.bind(QStringLiteral("math.exit_edit"), [this] { editorController_.mathBlockController().exitEditMode(); });
  commands_.bind(QStringLiteral("math.set_tex"), [this] {
    bool ok = false;
    const QString tex = QInputDialog::getMultiLineText(this, tr("Math TeX"), tr("TeX:"), QString(), &ok);
    if (ok) {
      editorController_.mathBlockController().setTex(tex);
    }
  });

  commands_.bind(QStringLiteral("view.word_wrap"), [this] {
    editor_->setWordWrapEnabled(commands_.action(QStringLiteral("view.word_wrap"))->isChecked());
  });
  commands_.bind(QStringLiteral("view.sidebar"), [this] { updateSidebarMode(); });
  commands_.bind(QStringLiteral("view.outline"), [this] { setSidebarPanel(SidebarWidget::Panel::Outline); });
  commands_.bind(QStringLiteral("view.document_list"), [this] { setSidebarPanel(SidebarWidget::Panel::Files); });
  commands_.bind(QStringLiteral("view.file_tree"), [this] { setSidebarPanel(SidebarWidget::Panel::Files); });
  commands_.bind(QStringLiteral("view.source_mode"), [this] { updateViewMode(); });
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
  updateEditActions();
  updateTableActions();
  updateParagraphActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  rebuildRecentFilesMenu();
  refreshSidebarDocumentInfo();
  refreshSidebarOutline();
  updateSidebarMode();
  updateViewMode();
  applyTheme(themeManager_.currentThemeName());
  renderView_->setDocument(session_.document());
}

void MainWindow::updateRenderCursorStatus(const HitTestResult& hit) {
  if (!hit.isValid()) {
    renderCursorStatus_.clear();
  } else if (hit.zone == HitTestResult::Zone::TableCell) {
    renderCursorStatus_ = tr("table %1:%2 offset %3").arg(hit.tableRow + 1).arg(hit.tableColumn + 1).arg(hit.textOffset);
  } else {
    renderCursorStatus_ = QStringLiteral("%1 %2 offset %3")
                              .arg(zoneName(hit.zone), hit.blockId.toString())
                              .arg(hit.textOffset);
  }
  updateEditActions();
  updateTableActions();
  updateParagraphActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  updateStatus();
}

void MainWindow::updateEditActions() {
  const bool src = sourceModeEnabled();
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
}

void MainWindow::updateTableActions() {
  const bool enabled = !sourceModeEnabled() && editorController_.tableController().currentCell().isValid();
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
      QStringLiteral("table.delete_table"),
  };
  for (const QString& id : ids) {
    commands_.setEnabled(id, enabled);
  }
  commands_.setEnabled(QStringLiteral("table.insert_table"), !sourceModeEnabled());
}

void MainWindow::updateParagraphActions() {
  const bool sourceMode = sourceModeEnabled();
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
  commands_.setEnabled(QStringLiteral("paragraph.math_block"), wysiwyg);
  commands_.setEnabled(QStringLiteral("paragraph.code_block"), wysiwyg);
  commands_.setEnabled(QStringLiteral("paragraph.insert_before"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.insert_after"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.link_ref"), wysiwyg);

  // Block conversions: enabled when on editable block
  commands_.setEnabled(QStringLiteral("paragraph.quote"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.ordered_list"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.unordered_list"), editable);
  commands_.setEnabled(QStringLiteral("paragraph.task_list"), editable);
}

void MainWindow::updateCodeActions() {
  const bool codeActive = !sourceModeEnabled() && editorController_.codeFenceController().currentCodeFenceId().isValid();
  commands_.setEnabled(QStringLiteral("code.enter_edit"), codeActive);
  commands_.setEnabled(QStringLiteral("code.exit_edit"), !sourceModeEnabled() && editorController_.codeFenceController().isEditing());
  commands_.setEnabled(QStringLiteral("code.set_language"), codeActive || (!sourceModeEnabled() && editorController_.codeFenceController().isEditing()));
}

void MainWindow::updateHtmlActions() {
  const bool htmlActive = !sourceModeEnabled() && editorController_.htmlBlockController().currentHtmlBlockId().isValid();
  commands_.setEnabled(QStringLiteral("html.enter_edit"), htmlActive);
  commands_.setEnabled(QStringLiteral("html.exit_edit"), !sourceModeEnabled() && editorController_.htmlBlockController().isEditing());
  commands_.setEnabled(QStringLiteral("html.set_source"), htmlActive || (!sourceModeEnabled() && editorController_.htmlBlockController().isEditing()));
}

void MainWindow::updateMathActions() {
  const bool mathActive = !sourceModeEnabled() && editorController_.mathBlockController().currentMathBlockId().isValid();
  commands_.setEnabled(QStringLiteral("math.enter_edit"), mathActive);
  commands_.setEnabled(QStringLiteral("math.exit_edit"), !sourceModeEnabled() && editorController_.mathBlockController().isEditing());
  commands_.setEnabled(QStringLiteral("math.set_tex"), mathActive || (!sourceModeEnabled() && editorController_.mathBlockController().isEditing()));
}

void MainWindow::syncSourceEditorIfNeeded() {
  if (!editor_ || !sourceEditorDirty_) {
    return;
  }
  editor_->setText(session_.markdownText());
  sourceEditorDirty_ = false;
}

void MainWindow::scheduleWordCountUpdate() {
  wordCountDirty_ = true;
  if (wordCountTimer_ && !wordCountTimer_->isActive()) {
    wordCountTimer_->start();
  }
}

void MainWindow::updateWordCountNow() {
  if (!wordsLabel_ || !wordCountDirty_) {
    return;
  }
  wordsLabel_->setText(tr("%1 words").arg(countWords(session_.markdownText())));
  wordCountDirty_ = false;
}

bool MainWindow::sourceModeEnabled() const {
  const QAction* action = commands_.action(QStringLiteral("view.source_mode"));
  return action && action->isChecked();
}

void MainWindow::undoEdit() {
  if (sourceModeEnabled()) {
    if (editor_->editor()->document()->isUndoAvailable()) {
      editor_->editor()->undo();
    }
    return;
  }
  if (!editorController_.canUndo()) {
    return;
  }
  editorController_.undo();
}

void MainWindow::redoEdit() {
  if (sourceModeEnabled()) {
    if (editor_->editor()->document()->isRedoAvailable()) {
      editor_->editor()->redo();
    }
    return;
  }
  if (!editorController_.canRedo()) {
    return;
  }
  editorController_.redo();
}

}  // namespace muffin
