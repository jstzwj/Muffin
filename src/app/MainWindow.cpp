#include "app/MainWindow.h"

#include "app/LanguageManager.h"
#include "app/PreferencesDialog.h"
#include "app/SidebarWidget.h"
#include "document/MarkdownTypes.h"
#include "document/OutlineBuilder.h"
#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QLoggingCategory>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
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

QString elidedPath(const QString& path) {
  const QString nativePath = QDir::toNativeSeparators(path);
  constexpr qsizetype maxLength = 72;
  if (nativePath.size() <= maxLength) {
    return nativePath;
  }
  return QStringLiteral("...") + nativePath.right(maxLength - 3);
}

QMenu* addDisabledMenu(QMenu* parent, const QString& title) {
  QMenu* menu = parent->addMenu(title);
  menu->setEnabled(false);
  return menu;
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

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
  setupMenuBar();
  setupStatusBar();
  setupConnections();
  applyTyporaLikeChrome();
  retranslateUi();
  updateTitle();
  updateStatus();
  resize(1250, 900);
}

bool MainWindow::openFile(QString path) {
  if (path.isEmpty()) {
    return false;
  }
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) {
    QMessageBox::warning(this, tr("Open Failed"), tr("File does not exist:\n%1").arg(path));
    return false;
  }

  const bool opened = fileController_.open(session_, this, info.absoluteFilePath());
  if (opened) {
    addRecentFile(session_.filePath());
  }
  return opened;
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (maybeSaveChanges()) {
    event->accept();
  } else {
    event->ignore();
  }
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QMainWindow::changeEvent(event);
}

QAction* MainWindow::addAction(
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

QAction* MainWindow::addCheckAction(
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

void MainWindow::setupUi() {
  centralSplitter_ = new QSplitter(Qt::Horizontal, this);
  centralSplitter_->setChildrenCollapsible(false);
  centralSplitter_->setHandleWidth(1);
  centralSplitter_->setStyleSheet(QStringLiteral("QSplitter::handle { background:#f0f0f0; width:1px; }"));

  sidebar_ = new SidebarWidget(centralSplitter_);

  viewStack_ = new QStackedWidget(this);
  renderView_ = new EditorView(viewStack_);
  editor_ = new SourceEditorWidget(this);
  viewStack_->addWidget(renderView_);
  viewStack_->addWidget(editor_);

  centralSplitter_->addWidget(sidebar_);
  centralSplitter_->addWidget(viewStack_);
  centralSplitter_->setStretchFactor(0, 0);
  centralSplitter_->setStretchFactor(1, 1);
  sidebar_->setVisible(false);
  setCentralWidget(centralSplitter_);
}

void MainWindow::setupMenuBar() {
  commands_.clearActions();
  menuBar()->clear();
  setupFileMenu();
  setupEditMenu();
  setupParagraphMenu();
  setupTableMenu();
  setupCodeMenu();
  setupHtmlMenu();
  setupMathMenu();
  setupFormatMenu();
  setupViewMenu();
  setupThemeMenu();
  setupHelpMenu();
}

void MainWindow::setupStatusBar() {
  statusBar()->setSizeGripEnabled(false);

  sidebarButton_ = new QToolButton(this);
  sidebarButton_->setText(QStringLiteral("O"));
  sidebarButton_->setCheckable(true);

  sourceModeButton_ = new QToolButton(this);
  sourceModeButton_->setText(QStringLiteral("</>"));
  sourceModeButton_->setCheckable(true);

  parseLabel_ = new QLabel(this);
  cursorLabel_ = new QLabel(this);
  wordsLabel_ = new QLabel(this);
  wordCountTimer_ = new QTimer(this);
  wordCountTimer_->setSingleShot(true);
  wordCountTimer_->setInterval(250);
  connect(wordCountTimer_, &QTimer::timeout, this, &MainWindow::updateWordCountNow);

  statusBar()->addWidget(sidebarButton_);
  statusBar()->addWidget(sourceModeButton_);
  parseLabel_->setVisible(false);
  cursorLabel_->setVisible(false);
  statusBar()->addPermanentWidget(parseLabel_);
  statusBar()->addPermanentWidget(cursorLabel_);
  statusBar()->addPermanentWidget(wordsLabel_);
}
void MainWindow::setupConnections() {
  editorController_.attach(&session_, renderView_);

  connect(editor_, &SourceEditorWidget::textEdited, &session_, &DocumentSession::updateFromEditor);
  connect(editor_, &SourceEditorWidget::cursorPositionChanged, this, &MainWindow::updateCursorStatus);
  connect(&editorController_, &EditorController::cursorChanged, this, &MainWindow::updateRenderCursorStatus);
  connect(renderView_, &EditorView::codeLanguageCommitted, this, [this](NodeId codeId, const QString& language) {
    if (sourceModeEnabled()) {
      return;
    }
    editorController_.setCodeFenceLanguage(codeId, language);
  });
  connect(renderView_, &EditorView::tableResizeRequested, this, [this](int rows, int columns) {
    if (!sourceModeEnabled()) {
      editorController_.resizeTable(rows, columns);
    }
  });
  connect(renderView_, &EditorView::tableColumnAlignmentRequested, this, [this](TableAlignment alignment) {
    if (!sourceModeEnabled()) {
      editorController_.setTableColumnAlignment(alignment);
    }
  });
  connect(renderView_, &EditorView::tableDeleteRequested, this, [this] {
    if (!sourceModeEnabled()) {
      editorController_.deleteTable();
    }
  });
  connect(renderView_, &EditorView::tableMoreActionsRequested, this, [this](QPoint globalPos) {
    if (sourceModeEnabled()) {
      return;
    }
    updateTableActions();
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
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateTableActions);
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

  commands_.bind(QStringLiteral("edit.undo"), [this] { undoEdit(); });
  commands_.bind(QStringLiteral("edit.redo"), [this] { redoEdit(); });
  commands_.bind(QStringLiteral("edit.cut"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->cut();
    } else {
      editorController_.cut();
    }
  });
  commands_.bind(QStringLiteral("edit.copy"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->copy();
    } else {
      editorController_.copy();
    }
  });
  commands_.bind(QStringLiteral("edit.paste"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->paste();
    } else {
      editorController_.paste();
    }
  });
  commands_.bind(QStringLiteral("edit.select_all"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->selectAll();
    } else {
      editorController_.selectAll();
    }
  });

  commands_.bind(QStringLiteral("format.bold"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("****"));
    } else {
      editorController_.toggleBold();
    }
  });
  commands_.bind(QStringLiteral("format.italic"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("**"));
    } else {
      editorController_.toggleItalic();
    }
  });
  commands_.bind(QStringLiteral("format.code"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("``"));
    } else {
      editorController_.toggleCode();
    }
  });
  commands_.bind(QStringLiteral("format.link"), [this] {
    if (sourceModeEnabled()) {
      editor_->editor()->insertPlainText(QStringLiteral("[](url)"));
    } else {
      editorController_.insertLink();
    }
  });

  commands_.bind(QStringLiteral("table.insert_row_before"), [this] { editorController_.insertTableRowBefore(); });
  commands_.bind(QStringLiteral("table.insert_row_after"), [this] { editorController_.insertTableRowAfter(); });
  commands_.bind(QStringLiteral("table.delete_row"), [this] { editorController_.deleteTableRow(); });
  commands_.bind(QStringLiteral("table.move_row_up"), [this] { editorController_.moveTableRowUp(); });
  commands_.bind(QStringLiteral("table.move_row_down"), [this] { editorController_.moveTableRowDown(); });
  commands_.bind(QStringLiteral("table.insert_column_before"), [this] { editorController_.insertTableColumnBefore(); });
  commands_.bind(QStringLiteral("table.insert_column_after"), [this] { editorController_.insertTableColumnAfter(); });
  commands_.bind(QStringLiteral("table.delete_column"), [this] { editorController_.deleteTableColumn(); });
  commands_.bind(QStringLiteral("table.move_column_left"), [this] { editorController_.moveTableColumnLeft(); });
  commands_.bind(QStringLiteral("table.move_column_right"), [this] { editorController_.moveTableColumnRight(); });
  commands_.bind(QStringLiteral("table.align_left"), [this] { editorController_.setTableColumnAlignment(TableAlignment::Left); });
  commands_.bind(QStringLiteral("table.align_center"), [this] { editorController_.setTableColumnAlignment(TableAlignment::Center); });
  commands_.bind(QStringLiteral("table.align_right"), [this] { editorController_.setTableColumnAlignment(TableAlignment::Right); });
  commands_.bind(QStringLiteral("table.align_none"), [this] { editorController_.setTableColumnAlignment(TableAlignment::None); });
  commands_.bind(QStringLiteral("table.delete_table"), [this] { editorController_.deleteTable(); });
  commands_.bind(QStringLiteral("table.insert_table"), [this] { editorController_.insertTable(); });
  commands_.bind(QStringLiteral("paragraph.yaml"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Yaml); });
  commands_.bind(QStringLiteral("paragraph.toml"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Toml); });
  commands_.bind(QStringLiteral("paragraph.json"), [this] { editorController_.insertFrontMatter(FrontMatterFormat::Json); });

  commands_.bind(QStringLiteral("code.enter_edit"), [this] { editorController_.enterCodeFenceEditMode(); });
  commands_.bind(QStringLiteral("code.exit_edit"), [this] { editorController_.exitCodeFenceEditMode(); });
  commands_.bind(QStringLiteral("code.set_language"), [this] {
    bool ok = false;
    const QString language = QInputDialog::getText(this, tr("Code Language"), tr("Language:"), QLineEdit::Normal, QString(), &ok);
    if (ok) {
      editorController_.setCodeFenceLanguage(language);
    }
  });

  commands_.bind(QStringLiteral("html.enter_edit"), [this] { editorController_.enterHtmlBlockEditMode(); });
  commands_.bind(QStringLiteral("html.exit_edit"), [this] { editorController_.exitHtmlBlockEditMode(); });
  commands_.bind(QStringLiteral("html.set_source"), [this] {
    bool ok = false;
    const QString html = QInputDialog::getMultiLineText(this, tr("HTML Source"), tr("HTML:"), QString(), &ok);
    if (ok) {
      editorController_.setHtmlBlockSource(html);
    }
  });

  commands_.bind(QStringLiteral("math.enter_edit"), [this] { editorController_.enterMathBlockEditMode(); });
  commands_.bind(QStringLiteral("math.exit_edit"), [this] { editorController_.exitMathBlockEditMode(); });
  commands_.bind(QStringLiteral("math.set_tex"), [this] {
    bool ok = false;
    const QString tex = QInputDialog::getMultiLineText(this, tr("Math TeX"), tr("TeX:"), QString(), &ok);
    if (ok) {
      editorController_.setMathBlockTex(tex);
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
    statusBar()->setVisible(commands_.action(QStringLiteral("view.status_bar"))->isChecked());
  });
  commands_.bind(QStringLiteral("view.fullscreen"), [this] {
    isFullScreen() ? showNormal() : showFullScreen();
  });
  commands_.bind(QStringLiteral("view.zoom_in"), [this] {
    zoomPercent_ = qMin(200, zoomPercent_ + 10);
    editor_->setZoomPercent(zoomPercent_);
    renderView_->setZoomPercent(zoomPercent_);
    renderView_->setDocument(session_.document());
    updateStatus();
  });
  commands_.bind(QStringLiteral("view.zoom_out"), [this] {
    zoomPercent_ = qMax(60, zoomPercent_ - 10);
    editor_->setZoomPercent(zoomPercent_);
    renderView_->setZoomPercent(zoomPercent_);
    renderView_->setDocument(session_.document());
    updateStatus();
  });
  commands_.bind(QStringLiteral("view.actual_size"), [this] {
    zoomPercent_ = 100;
    editor_->setZoomPercent(zoomPercent_);
    renderView_->setZoomPercent(zoomPercent_);
    renderView_->setDocument(session_.document());
    updateStatus();
  });

  commands_.bind(QStringLiteral("help.about"), [this] {
    QMessageBox::about(
        this,
        tr("About Muffin"),
        tr("Muffin %1\n\nA fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets.")
            .arg(QApplication::applicationVersion()));
  });

  commands_.bind(QStringLiteral("theme.github"), [this] { themeManager_.setTheme(QStringLiteral("github")); });
  commands_.bind(QStringLiteral("theme.newsprint"), [this] { themeManager_.setTheme(QStringLiteral("newsprint")); });
  commands_.bind(QStringLiteral("theme.night"), [this] { themeManager_.setTheme(QStringLiteral("night")); });
  commands_.bind(QStringLiteral("theme.pixyll"), [this] { themeManager_.setTheme(QStringLiteral("pixyll")); });
  commands_.bind(QStringLiteral("theme.whitey"), [this] { themeManager_.setTheme(QStringLiteral("whitey")); });

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
  updateTableActions();
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

void MainWindow::applyTyporaLikeChrome() {
  setStyleSheet(QStringLiteral(
      "QMainWindow { background: #ffffff; }"
      "QMenuBar { background: #ffffff; color: #111111; padding: 0; }"
      "QMenuBar::item { padding: 4px 9px; background: transparent; }"
      "QMenuBar::item:selected { background: #e9e9e9; }"
      "QMenu { background: #ffffff; border: 1px solid #cfcfcf; padding: 4px 0; }"
      "QMenu::item { padding: 5px 34px 5px 24px; }"
      "QMenu::item:selected { background: #e7f1ff; }"
      "QMenu::item:disabled { color: #999999; }"
      "QStatusBar { background: #ffffff; color: #555555; border: 0; font-size: 12px; }"
      "QStatusBar::item { border: 0; }"
      "QToolButton {"
      "  background: transparent;"
      "  border: 0;"
      "  color: #555555;"
      "  padding: 0 8px;"
      "  min-width: 22px;"
      "  min-height: 18px;"
      "  font-size: 12px;"
      "}"
      "QToolButton:hover { background: #eeeeee; }"
      "QToolButton:checked { color: #111111; background: #e9e9e9; }"));
}

void MainWindow::retranslateUi() {
  const bool sidebarChecked = commands_.action(QStringLiteral("view.sidebar")) && commands_.action(QStringLiteral("view.sidebar"))->isChecked();
  const bool sourceChecked = commands_.action(QStringLiteral("view.source_mode")) && commands_.action(QStringLiteral("view.source_mode"))->isChecked();
  const bool wordWrapChecked = !commands_.action(QStringLiteral("view.word_wrap")) || commands_.action(QStringLiteral("view.word_wrap"))->isChecked();
  const bool statusBarChecked = !commands_.action(QStringLiteral("view.status_bar")) || commands_.action(QStringLiteral("view.status_bar"))->isChecked();

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

  if (sidebarButton_) {
    sidebarButton_->setToolTip(tr("Show / Hide Sidebar"));
  }
  if (sourceModeButton_) {
    sourceModeButton_->setToolTip(tr("Toggle source / rendered mode"));
  }

  updateFileActions();
  updateTableActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  updateThemeActions();
  rebuildRecentFilesMenu();
  if (renderView_) {
    renderView_->setDocument(session_.document());
  }
  updateStatus();
  wordCountDirty_ = true;
  updateWordCountNow();
}
void MainWindow::setupFileMenu() {
  QMenu* file = menuBar()->addMenu(tr("File"));
  addAction(file, QStringLiteral("file.new"), tr("New"), QKeySequence::New);
  addAction(file, QStringLiteral("file.new_window"), tr("New Window"), QKeySequence(QStringLiteral("Ctrl+Shift+N")));
  addAction(file, QStringLiteral("file.open"), tr("Open..."), QKeySequence::Open);
  addAction(file, QStringLiteral("file.open_folder"), tr("Open Folder..."));
  addAction(file, QStringLiteral("file.quick_open"), tr("Quick Open..."), {}, false);
  recentFilesMenu_ = file->addMenu(tr("Open Recent"));
  file->addSeparator();
  addAction(file, QStringLiteral("file.reopen_encoding"), tr("Reopen with Encoding"), {}, false);
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
  addAction(file, QStringLiteral("file.print"), tr("Print..."), QKeySequence(QStringLiteral("Alt+Shift+P")), false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.preferences"), tr("Preferences..."), QKeySequence(QStringLiteral("Ctrl+,")));
  addAction(file, QStringLiteral("file.close"), tr("Close"), QKeySequence::Close);
}
void MainWindow::setupEditMenu() {
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
  addDisabledMenu(edit, tr("Line Breaks"));
  addDisabledMenu(edit, tr("Spaces and Line Breaks"));
  addAction(edit, QStringLiteral("edit.spellcheck"), tr("Spell Check..."), {}, false);
  addAction(edit, QStringLiteral("edit.find"), tr("Find and Replace"), QKeySequence::Find, false);
  addAction(edit, QStringLiteral("edit.symbols"), tr("Emoji and Symbols"), QKeySequence(QStringLiteral("Ctrl+.")), false);
}
void MainWindow::setupParagraphMenu() {
  QMenu* paragraph = menuBar()->addMenu(tr("Paragraph"));
  for (int level = 1; level <= 6; ++level) {
    addAction(
        paragraph,
        QStringLiteral("paragraph.heading_%1").arg(level),
        tr("Heading %1").arg(level),
        QKeySequence(QStringLiteral("Ctrl+%1").arg(level)),
        false);
  }
  addCheckAction(paragraph, QStringLiteral("paragraph.paragraph"), tr("Paragraph"), QKeySequence(QStringLiteral("Ctrl+0")), true, false);
  addAction(paragraph, QStringLiteral("paragraph.promote_heading"), tr("Promote Heading"), QKeySequence(QStringLiteral("Ctrl+-")), false);
  addAction(paragraph, QStringLiteral("paragraph.demote_heading"), tr("Demote Heading"), QKeySequence(QStringLiteral("Ctrl+=")), false);
  paragraph->addSeparator();
  addDisabledMenu(paragraph, tr("Table"));
  addAction(paragraph, QStringLiteral("paragraph.math_block"), tr("Formula Block"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), false);
  addAction(paragraph, QStringLiteral("paragraph.code_block"), tr("Code Block"), QKeySequence(QStringLiteral("Ctrl+Shift+K")), false);
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
  addAction(paragraph, QStringLiteral("paragraph.link_ref"), tr("Link Reference"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.footnote"), tr("Footnote"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.hr"), tr("Horizontal Rule"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.toc"), tr("Table of Contents"), {}, false);
  QMenu* frontMatter = paragraph->addMenu(tr("Front Matter"));
  addAction(frontMatter, QStringLiteral("paragraph.yaml"), tr("YAML"));
  addAction(frontMatter, QStringLiteral("paragraph.toml"), tr("TOML"));
  addAction(frontMatter, QStringLiteral("paragraph.json"), tr("JSON"));
}
void MainWindow::setupFormatMenu() {
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
void MainWindow::setupTableMenu() {
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
void MainWindow::setupCodeMenu() {
  QMenu* code = menuBar()->addMenu(tr("Code"));
  code->menuAction()->setVisible(false);
  addAction(code, QStringLiteral("code.enter_edit"), tr("Enter Edit"));
  addAction(code, QStringLiteral("code.exit_edit"), tr("Exit Edit"));
  addAction(code, QStringLiteral("code.set_language"), tr("Set Language..."));
}
void MainWindow::setupMathMenu() {
  QMenu* math = menuBar()->addMenu(tr("Math"));
  math->menuAction()->setVisible(false);
  addAction(math, QStringLiteral("math.enter_edit"), tr("Enter Edit"));
  addAction(math, QStringLiteral("math.exit_edit"), tr("Exit Edit"));
  addAction(math, QStringLiteral("math.set_tex"), tr("Set TeX..."));
}
void MainWindow::setupHtmlMenu() {
  QMenu* html = menuBar()->addMenu(QStringLiteral("HTML(&H)"));
  html->menuAction()->setVisible(false);
  addAction(html, QStringLiteral("html.enter_edit"), tr("Enter Edit"));
  addAction(html, QStringLiteral("html.exit_edit"), tr("Exit Edit"));
  addAction(html, QStringLiteral("html.set_source"), tr("Set HTML..."));
}
void MainWindow::setupViewMenu() {
  QMenu* view = menuBar()->addMenu(tr("View"));
  addCheckAction(view, QStringLiteral("view.sidebar"), tr("Show / Hide Sidebar"), QKeySequence(QStringLiteral("Ctrl+Shift+L")), false);
  addAction(view, QStringLiteral("view.outline"), tr("Outline"), QKeySequence(QStringLiteral("Ctrl+Shift+1")));
  addAction(view, QStringLiteral("view.document_list"), tr("Document List"), QKeySequence(QStringLiteral("Ctrl+Shift+2")));
  addAction(view, QStringLiteral("view.file_tree"), tr("File Tree"), QKeySequence(QStringLiteral("Ctrl+Shift+3")));
  addAction(view, QStringLiteral("view.search"), tr("Search"), QKeySequence(QStringLiteral("Ctrl+Shift+F")), false);
  view->addSeparator();
  addCheckAction(view, QStringLiteral("view.source_mode"), tr("Source Code Mode"), QKeySequence(QStringLiteral("Ctrl+/")), false);
  addCheckAction(view, QStringLiteral("view.word_wrap"), tr("Word Wrap"), {}, true);
  addCheckAction(view, QStringLiteral("view.focus"), tr("Focus Mode"), QKeySequence(QStringLiteral("F8")), false, false);
  addCheckAction(view, QStringLiteral("view.typewriter"), tr("Typewriter Mode"), QKeySequence(QStringLiteral("F9")), false, false);
  addCheckAction(view, QStringLiteral("view.status_bar"), tr("Show Status Bar"), {}, true);
  view->addSeparator();
  addAction(view, QStringLiteral("view.word_count"), tr("Word Count Window"), {}, false);
  addAction(view, QStringLiteral("view.diagnostics"), tr("Render Diagnostics"), {}, false);
  addAction(view, QStringLiteral("view.fullscreen"), tr("Toggle Full Screen"), QKeySequence(QStringLiteral("F11")));
  addCheckAction(view, QStringLiteral("view.always_on_top"), tr("Always on Top"), {}, false, false);
  view->addSeparator();
  addAction(view, QStringLiteral("view.actual_size"), tr("Actual Size"), QKeySequence(QStringLiteral("Ctrl+Shift+9")));
  addAction(view, QStringLiteral("view.zoom_in"), tr("Zoom In"), QKeySequence(QStringLiteral("Ctrl+Shift+=")));
  addAction(view, QStringLiteral("view.zoom_out"), tr("Zoom Out"), QKeySequence(QStringLiteral("Ctrl+Shift+-")));
  addAction(view, QStringLiteral("view.window_switch"), tr("Switch Windows"), QKeySequence(QStringLiteral("Ctrl+Tab")), false);
  addAction(view, QStringLiteral("view.devtools"), tr("Developer Tools"), QKeySequence(QStringLiteral("Shift+F12")), false);
}
void MainWindow::setupThemeMenu() {
  QMenu* theme = menuBar()->addMenu(tr("Theme"));
  addCheckAction(theme, QStringLiteral("theme.github"), QStringLiteral("Github"), {}, true);
  addCheckAction(theme, QStringLiteral("theme.newsprint"), QStringLiteral("Newsprint"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.night"), QStringLiteral("Night"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.pixyll"), QStringLiteral("Pixyll"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.whitey"), QStringLiteral("Whitey"), {}, false);
}
void MainWindow::setupHelpMenu() {
  QMenu* help = menuBar()->addMenu(tr("Help"));
  addAction(help, QStringLiteral("help.whats_new"), tr("What's New..."), {}, false);
  addAction(help, QStringLiteral("help.quick_start"), tr("Quick Start"), {}, false);
  addAction(help, QStringLiteral("help.markdown_ref"), tr("Markdown Reference"), {}, false);
  addAction(help, QStringLiteral("help.pandoc"), tr("Install and Use Pandoc"), {}, false);
  addAction(help, QStringLiteral("help.custom_themes"), tr("Custom Themes"), {}, false);
  addAction(help, QStringLiteral("help.images"), tr("Use Images in Muffin"), {}, false);
  addAction(help, QStringLiteral("help.recovery"), tr("Data Recovery and Version Control"), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.more_themes"), tr("More Themes..."), {}, false);
  addAction(help, QStringLiteral("help.acknowledgements"), tr("Acknowledgements"), {}, false);
  addAction(help, QStringLiteral("help.changelog"), tr("Changelog"), {}, false);
  addAction(help, QStringLiteral("help.privacy"), tr("Privacy Policy"), {}, false);
  addAction(help, QStringLiteral("help.website"), tr("Official Website"), {}, false);
  addAction(help, QStringLiteral("help.feedback"), tr("Feedback"), {}, false);
  addAction(help, QStringLiteral("help.update"), tr("Check for Updates..."), {}, false);
  addAction(help, QStringLiteral("help.license"), tr("My License..."), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.about"), tr("About"));
}
void MainWindow::updateTitle() {
  const QString marker = session_.document().isModified() ? QStringLiteral(" *") : QString();
  setWindowTitle(QStringLiteral("%1%2 - Muffin").arg(session_.displayName(), marker));
}

void MainWindow::updateStatus() {
  parseLabel_->setText(tr("Parse %1 ms").arg(session_.lastParseElapsedMs()));
  if (!sourceModeEnabled() && !renderCursorStatus_.isEmpty()) {
    cursorLabel_->setText(renderCursorStatus_);
  } else {
    cursorLabel_->setText(QStringLiteral("%1:%2").arg(cursorLine_).arg(cursorColumn_));
  }
}

void MainWindow::updateCursorStatus(int line, int column) {
  cursorLine_ = line;
  cursorColumn_ = column;
  updateStatus();
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
  updateTableActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  updateStatus();
}

void MainWindow::updateSidebarMode() {
  if (!sidebar_ || !sidebarButton_) {
    return;
  }
  const QAction* action = commands_.action(QStringLiteral("view.sidebar"));
  const bool sidebarVisible = action && action->isChecked();
  sidebar_->setVisible(sidebarVisible);
  sidebarButton_->setChecked(sidebarVisible);
}

void MainWindow::setSidebarPanel(SidebarWidget::Panel panel) {
  if (!sidebar_) {
    return;
  }
  sidebar_->setPanel(panel);
  if (QAction* action = commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(true);
  }
  updateSidebarMode();
}

void MainWindow::refreshSidebarDocumentInfo() {
  if (!sidebar_) {
    return;
  }
  sidebar_->setCurrentDocument(session_.displayName(), session_.filePath(), session_.document().isModified());
}

void MainWindow::refreshSidebarOutline() {
  if (!sidebar_) {
    return;
  }
  sidebar_->setOutline(buildOutline(session_.document()));
}

void MainWindow::openFolder() {
  const QString initialPath = sidebarFolderRoot_.isEmpty() ? QFileInfo(session_.filePath()).absolutePath() : sidebarFolderRoot_;
  const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), initialPath);
  if (path.isEmpty()) {
    return;
  }
  sidebarFolderRoot_ = path;
  sidebar_->setFolderRoot(path);
  setSidebarPanel(SidebarWidget::Panel::Files);
}

void MainWindow::openNewWindow() {
  auto* window = new MainWindow();
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->show();
}

void MainWindow::activateOutlineNode(NodeId nodeId, SourceRange sourceRange) {
  if (sourceModeEnabled()) {
    syncSourceEditorIfNeeded();
    QTextCursor cursor = editor_->editor()->textCursor();
    const int position = qBound(0, static_cast<int>(sourceRange.byteStart), editor_->editor()->document()->characterCount() - 1);
    cursor.setPosition(position);
    editor_->editor()->setTextCursor(cursor);
    editor_->editor()->centerCursor();
    editor_->editor()->setFocus(Qt::OtherFocusReason);
    return;
  }
  renderView_->scrollToNode(nodeId);
  renderView_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::updateViewMode() {
  if (!viewStack_ || !renderView_ || !editor_) {
    return;
  }
  const bool sourceMode = sourceModeEnabled();
  if (sourceMode) {
    syncSourceEditorIfNeeded();
  }
  viewStack_->setCurrentWidget(sourceMode ? static_cast<QWidget*>(editor_) : static_cast<QWidget*>(renderView_));
  if (sourceModeButton_) {
    sourceModeButton_->setChecked(sourceMode);
  }
  if (!sourceMode) {
    renderView_->setFocus(Qt::OtherFocusReason);
  }
  updateStatus();
}

void MainWindow::updateFileActions() {
  const bool hasFile = !session_.filePath().isEmpty();
  commands_.setEnabled(QStringLiteral("file.properties"), hasFile);
  commands_.setEnabled(QStringLiteral("file.reveal"), hasFile);
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
    editor_->editor()->undo();
    return;
  }

  if (!editorController_.canUndo()) {
    return;
  }
  editorController_.undo();
}

void MainWindow::redoEdit() {
  if (sourceModeEnabled()) {
    editor_->editor()->redo();
    return;
  }

  if (!editorController_.canRedo()) {
    return;
  }
  editorController_.redo();
}

void MainWindow::applyTheme(QString name) {
  const RenderTheme theme = themeManager_.currentTheme(zoomPercent_);
  renderView_->setTheme(theme);
  editor_->setTheme(theme);
  if (sidebar_) {
    sidebar_->applyThemeName(name);
  }
  updateThemeActions();

  if (name == QStringLiteral("night")) {
    setStyleSheet(QStringLiteral(
        "QMainWindow { background:#1f2328; }"
        "QMenuBar { background:#1f2328; color:#e6edf3; padding:0; }"
        "QMenuBar::item { padding:4px 9px; background:transparent; }"
        "QMenuBar::item:selected { background:#2b3138; }"
        "QMenu { background:#242a31; color:#e6edf3; border:1px solid #3d444d; padding:4px 0; }"
        "QMenu::item { padding:5px 34px 5px 24px; }"
        "QMenu::item:selected { background:#264f78; }"
        "QMenu::item:disabled { color:#6e7681; }"
        "QStatusBar { background:#1f2328; color:#9aa4af; border:0; font-size:12px; }"
        "QStatusBar::item { border:0; }"
        "QToolButton { background:transparent; border:0; color:#9aa4af; padding:0 8px; min-width:22px; min-height:18px; font-size:12px; }"
        "QToolButton:hover { background:#2b3138; }"
        "QToolButton:checked { color:#e6edf3; background:#30363d; }"));
  } else {
    applyTyporaLikeChrome();
  }
}

void MainWindow::updateThemeActions() {
  const QString current = themeManager_.currentThemeName();
  commands_.setChecked(QStringLiteral("theme.github"), current == QStringLiteral("github"));
  commands_.setChecked(QStringLiteral("theme.newsprint"), current == QStringLiteral("newsprint"));
  commands_.setChecked(QStringLiteral("theme.night"), current == QStringLiteral("night"));
  commands_.setChecked(QStringLiteral("theme.pixyll"), current == QStringLiteral("pixyll"));
  commands_.setChecked(QStringLiteral("theme.whitey"), current == QStringLiteral("whitey"));
}

void MainWindow::rebuildRecentFilesMenu() {
  if (!recentFilesMenu_) {
    return;
  }

  recentFilesMenu_->clear();
  const QStringList paths = recentFiles();
  recentFilesMenu_->setEnabled(!paths.isEmpty());

  for (const QString& path : paths) {
    QAction* action = recentFilesMenu_->addAction(elidedPath(path));
    action->setToolTip(QDir::toNativeSeparators(path));
    connect(action, &QAction::triggered, this, [this, path] {
      openFile(path);
    });
  }

  if (!paths.isEmpty()) {
    recentFilesMenu_->addSeparator();
    QAction* clearAction = recentFilesMenu_->addAction(tr("Clear Recent Files"));
    connect(clearAction, &QAction::triggered, this, [this] {
      setRecentFiles({});
      rebuildRecentFilesMenu();
    });
  }
}

void MainWindow::addRecentFile(QString path) {
  if (path.isEmpty()) {
    return;
  }
  path = QFileInfo(path).absoluteFilePath();

  QStringList paths = recentFiles();
  paths.removeAll(path);
  paths.prepend(path);
  while (paths.size() > 10) {
    paths.removeLast();
  }
  setRecentFiles(paths);
  rebuildRecentFilesMenu();
}

QStringList MainWindow::recentFiles() const {
  QSettings settings;
  return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void MainWindow::setRecentFiles(const QStringList& paths) const {
  QSettings settings;
  settings.setValue(QStringLiteral("recentFiles"), paths);
}

void MainWindow::showDocumentProperties() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  const QFileInfo info(session_.filePath());
  const QString message = tr(
      "Name: %1\n"
      "Location: %2\n"
      "Size: %3 bytes\n"
      "Modified: %4\n"
      "Words: %5\n"
      "Parse time: %6 ms")
                              .arg(
                                  info.fileName(),
                                  QDir::toNativeSeparators(info.absolutePath()),
                                  QString::number(info.size()),
                                  QLocale().toString(info.lastModified(), QLocale::ShortFormat),
                                  QString::number(countWords(session_.markdownText())),
                                  QString::number(session_.lastParseElapsedMs()));
  QMessageBox::information(this, tr("Properties"), message);
}

void MainWindow::showPreferences() {
  PreferencesDialog dialog(this);
  dialog.exec();
}

void MainWindow::revealCurrentFile() {
  if (session_.filePath().isEmpty()) {
    return;
  }
  const QFileInfo info(session_.filePath());
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

bool MainWindow::maybeSaveChanges() {
  if (!session_.document().isModified()) {
    return true;
  }

  const QMessageBox::StandardButton choice = QMessageBox::warning(
      this,
      tr("Muffin"),
      tr("The current document has unsaved changes."),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);

  if (choice == QMessageBox::Cancel) {
    return false;
  }
  if (choice == QMessageBox::Save) {
    return fileController_.save(session_, this);
  }
  return true;
}

}  // namespace muffin
