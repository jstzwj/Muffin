#include "app/MainWindow.h"

#include "editor/EditorView.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
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
#include <QVBoxLayout>

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
    case HitTestResult::Zone::Block:
      return QStringLiteral("block");
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

  sidebar_ = new QWidget(centralSplitter_);
  sidebar_->setMinimumWidth(220);
  sidebar_->setMaximumWidth(360);
  sidebar_->setStyleSheet(QStringLiteral("background:#fafafa; border-right:1px solid #eeeeee;"));
  auto* sidebarLayout = new QVBoxLayout(sidebar_);
  sidebarLayout->setContentsMargins(16, 12, 16, 12);
  sidebarLayout->setSpacing(8);
  sidebarLayout->addStretch(1);

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
  normalizeComplexBlockMenuText();
}

void MainWindow::setupStatusBar() {
  statusBar()->setSizeGripEnabled(false);

  sidebarButton_ = new QToolButton(this);
  sidebarButton_->setText(QStringLiteral("○"));
  sidebarButton_->setCheckable(true);
  sidebarButton_->setToolTip(QStringLiteral("显示 / 隐藏侧边栏"));

  sourceModeButton_ = new QToolButton(this);
  sourceModeButton_->setText(QStringLiteral("</>"));
  sourceModeButton_->setCheckable(true);
  sourceModeButton_->setToolTip(QStringLiteral("切换源码 / 渲染模式"));

  parseLabel_ = new QLabel(this);
  cursorLabel_ = new QLabel(this);
  wordsLabel_ = new QLabel(this);
  wordCountTimer_ = new QTimer(this);
  wordCountTimer_->setSingleShot(true);
  wordCountTimer_->setInterval(250);
  connect(wordCountTimer_, &QTimer::timeout, this, &MainWindow::updateWordCountNow);

  statusBar()->addWidget(sidebarButton_);
  statusBar()->addWidget(sourceModeButton_);
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
  connect(&session_, &DocumentSession::modifiedChanged, this, &MainWindow::updateTitle);
  connect(&session_, &DocumentSession::modifiedChanged, this, &MainWindow::updateStatus);
  connect(&session_, &DocumentSession::parsed, this, [this] {
    PerfTimer perf("main.parsed.consumer");
    if (!session_.lastParseWasLocalEdit()) {
      renderView_->setDocument(session_.document());
    }
    scheduleWordCountUpdate();
    updateStatus();
  });
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateStatus);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateTableActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateCodeActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateHtmlActions);
  connect(&editorController_, &EditorController::stateChanged, this, &MainWindow::updateMathActions);
  connect(&themeManager_, &ThemeManager::themeChanged, this, [this](const QString& name) {
    applyTheme(name);
  });

  commands_.bind(QStringLiteral("file.new"), [this] {
    editorController_.clearHistoryAndSelection();
    fileController_.newFile(session_, this);
  });
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
  commands_.bind(QStringLiteral("edit.select_all"), [this] { editor_->editor()->selectAll(); });

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
  commands_.bind(QStringLiteral("table.insert_table"), [this] { editorController_.insertTable(); });

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
        tr("Muffin\n\nA fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets."));
  });

  commands_.bind(QStringLiteral("theme.github"), [this] { themeManager_.setTheme(QStringLiteral("github")); });
  commands_.bind(QStringLiteral("theme.newsprint"), [this] { themeManager_.setTheme(QStringLiteral("newsprint")); });
  commands_.bind(QStringLiteral("theme.night"), [this] { themeManager_.setTheme(QStringLiteral("night")); });

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

  updateFileActions();
  updateTableActions();
  updateCodeActions();
  updateHtmlActions();
  updateMathActions();
  rebuildRecentFilesMenu();
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

void MainWindow::normalizeComplexBlockMenuText() {
  for (QAction* menuAction : menuBar()->actions()) {
    if (!menuAction || !menuAction->menu()) {
      continue;
    }
    QMenu* menu = menuAction->menu();
    if (menu->actions().contains(commands_.action(QStringLiteral("math.enter_edit")))) {
      menuAction->setText(QString::fromUtf8("\xe5\x85\xac\xe5\xbc\x8f(&M)"));
    }
    if (menu->actions().contains(commands_.action(QStringLiteral("html.enter_edit")))) {
      menuAction->setText(QStringLiteral("HTML(&H)"));
    }
  }

  if (QAction* action = commands_.action(QStringLiteral("math.enter_edit"))) {
    action->setText(QString::fromUtf8("\xe8\xbf\x9b\xe5\x85\xa5\xe7\xbc\x96\xe8\xbe\x91"));
  }
  if (QAction* action = commands_.action(QStringLiteral("math.exit_edit"))) {
    action->setText(QString::fromUtf8("\xe9\x80\x80\xe5\x87\xba\xe7\xbc\x96\xe8\xbe\x91"));
  }
  if (QAction* action = commands_.action(QStringLiteral("math.set_tex"))) {
    action->setText(QString::fromUtf8("\xe8\xae\xbe\xe7\xbd\xae TeX..."));
  }
  if (QAction* action = commands_.action(QStringLiteral("html.enter_edit"))) {
    action->setText(QString::fromUtf8("\xe8\xbf\x9b\xe5\x85\xa5\xe7\xbc\x96\xe8\xbe\x91"));
  }
  if (QAction* action = commands_.action(QStringLiteral("html.exit_edit"))) {
    action->setText(QString::fromUtf8("\xe9\x80\x80\xe5\x87\xba\xe7\xbc\x96\xe8\xbe\x91"));
  }
  if (QAction* action = commands_.action(QStringLiteral("html.set_source"))) {
    action->setText(QString::fromUtf8("\xe8\xae\xbe\xe7\xbd\xae HTML..."));
  }
}

void MainWindow::setupFileMenu() {
  QMenu* file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
  addAction(file, QStringLiteral("file.new"), QStringLiteral("新建"), QKeySequence::New);
  addAction(file, QStringLiteral("file.new_window"), QStringLiteral("新建窗口"), QKeySequence(QStringLiteral("Ctrl+Shift+N")), false);
  addAction(file, QStringLiteral("file.open"), QStringLiteral("打开..."), QKeySequence::Open);
  addAction(file, QStringLiteral("file.open_folder"), QStringLiteral("打开文件夹..."), {}, false);
  addAction(file, QStringLiteral("file.quick_open"), QStringLiteral("快速打开..."), {}, false);
  recentFilesMenu_ = file->addMenu(QStringLiteral("打开最近文件"));
  file->addSeparator();
  addAction(file, QStringLiteral("file.reopen_encoding"), QStringLiteral("选择编码重新打开"), {}, false);
  addAction(file, QStringLiteral("file.save"), QStringLiteral("保存"), QKeySequence::Save);
  addAction(file, QStringLiteral("file.save_as"), QStringLiteral("另存为..."), QKeySequence::SaveAs);
  addAction(file, QStringLiteral("file.move_to"), QStringLiteral("移动到..."), {}, false);
  addAction(file, QStringLiteral("file.save_all"), QStringLiteral("保存全部打开的文件..."), {}, false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.properties"), QStringLiteral("属性..."));
  addAction(file, QStringLiteral("file.reveal"), QStringLiteral("打开文件位置..."));
  addAction(file, QStringLiteral("file.sidebar"), QStringLiteral("在侧边栏中显示"), {}, false);
  addAction(file, QStringLiteral("file.delete"), QStringLiteral("删除..."), {}, false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.import"), QStringLiteral("导入..."), {}, false);
  addDisabledMenu(file, QStringLiteral("导出"));
  addAction(file, QStringLiteral("file.print"), QStringLiteral("打印..."), QKeySequence(QStringLiteral("Alt+Shift+P")), false);
  file->addSeparator();
  addAction(file, QStringLiteral("file.preferences"), QStringLiteral("偏好设置..."), QKeySequence(QStringLiteral("Ctrl+,")), false);
  addAction(file, QStringLiteral("file.close"), QStringLiteral("关闭"), QKeySequence::Close);
}

void MainWindow::setupEditMenu() {
  QMenu* edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
  addAction(edit, QStringLiteral("edit.undo"), QStringLiteral("撤销"), QKeySequence::Undo);
  addAction(edit, QStringLiteral("edit.redo"), QStringLiteral("重做"), QKeySequence::Redo);
  edit->addSeparator();
  addAction(edit, QStringLiteral("edit.cut"), QStringLiteral("剪切"), QKeySequence::Cut);
  addAction(edit, QStringLiteral("edit.copy"), QStringLiteral("复制"), QKeySequence::Copy);
  addAction(edit, QStringLiteral("edit.copy_image"), QStringLiteral("拷贝图片"), {}, false);
  addAction(edit, QStringLiteral("edit.paste"), QStringLiteral("粘贴"), QKeySequence::Paste);
  addAction(edit, QStringLiteral("edit.copy_plain"), QStringLiteral("复制为纯文本"), {}, false);
  addAction(edit, QStringLiteral("edit.copy_markdown"), QStringLiteral("复制为 Markdown"), QKeySequence(QStringLiteral("Ctrl+Shift+C")), false);
  addAction(edit, QStringLiteral("edit.copy_html"), QStringLiteral("复制为 HTML 代码"), {}, false);
  addAction(edit, QStringLiteral("edit.paste_plain"), QStringLiteral("粘贴为纯文本"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), false);
  edit->addSeparator();
  QMenu* select = edit->addMenu(QStringLiteral("选择"));
  addAction(select, QStringLiteral("edit.select_all"), QStringLiteral("全选"), QKeySequence::SelectAll);
  addAction(select, QStringLiteral("edit.select_line"), QStringLiteral("选择当前行"), {}, false);
  addAction(select, QStringLiteral("edit.select_format"), QStringLiteral("选择当前格式文本"), {}, false);
  edit->addSeparator();
  addAction(edit, QStringLiteral("edit.move_line_up"), QStringLiteral("上移该行"), QKeySequence(QStringLiteral("Alt+Up")), false);
  addAction(edit, QStringLiteral("edit.move_line_down"), QStringLiteral("下移该行"), QKeySequence(QStringLiteral("Alt+Down")), false);
  addAction(edit, QStringLiteral("edit.delete"), QStringLiteral("删除"), {}, false);
  edit->addSeparator();
  addDisabledMenu(edit, QStringLiteral("删除范围"));
  addDisabledMenu(edit, QStringLiteral("数学工具"));
  addDisabledMenu(edit, QStringLiteral("智能标点"));
  addDisabledMenu(edit, QStringLiteral("换行符"));
  addDisabledMenu(edit, QStringLiteral("空格与换行"));
  addAction(edit, QStringLiteral("edit.spellcheck"), QStringLiteral("拼写检查..."), {}, false);
  addAction(edit, QStringLiteral("edit.find"), QStringLiteral("查找和替换"), QKeySequence::Find, false);
  addAction(edit, QStringLiteral("edit.symbols"), QStringLiteral("表情与符号"), QKeySequence(QStringLiteral("Ctrl+.")), false);
}

void MainWindow::setupParagraphMenu() {
  QMenu* paragraph = menuBar()->addMenu(QStringLiteral("段落(&P)"));
  for (int level = 1; level <= 6; ++level) {
    addAction(
        paragraph,
        QStringLiteral("paragraph.heading_%1").arg(level),
        QStringLiteral("%1级标题").arg(level),
        QKeySequence(QStringLiteral("Ctrl+%1").arg(level)),
        false);
  }
  addCheckAction(paragraph, QStringLiteral("paragraph.paragraph"), QStringLiteral("段落"), QKeySequence(QStringLiteral("Ctrl+0")), true, false);
  addAction(paragraph, QStringLiteral("paragraph.promote_heading"), QStringLiteral("提升标题级别"), QKeySequence(QStringLiteral("Ctrl+-")), false);
  addAction(paragraph, QStringLiteral("paragraph.demote_heading"), QStringLiteral("降低标题级别"), QKeySequence(QStringLiteral("Ctrl+=")), false);
  paragraph->addSeparator();
  addDisabledMenu(paragraph, QStringLiteral("表格"));
  addAction(paragraph, QStringLiteral("paragraph.math_block"), QStringLiteral("公式块"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), false);
  addAction(paragraph, QStringLiteral("paragraph.code_block"), QStringLiteral("代码块"), QKeySequence(QStringLiteral("Ctrl+Shift+K")), false);
  addDisabledMenu(paragraph, QStringLiteral("代码工具"));
  addDisabledMenu(paragraph, QStringLiteral("警告框"));
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.quote"), QStringLiteral("引用"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")), false);
  addAction(paragraph, QStringLiteral("paragraph.ordered_list"), QStringLiteral("有序列表"), QKeySequence(QStringLiteral("Ctrl+Shift+[")), false);
  addAction(paragraph, QStringLiteral("paragraph.unordered_list"), QStringLiteral("无序列表"), QKeySequence(QStringLiteral("Ctrl+Shift+]")), false);
  addAction(paragraph, QStringLiteral("paragraph.task_list"), QStringLiteral("任务列表"), QKeySequence(QStringLiteral("Ctrl+Shift+X")), false);
  addDisabledMenu(paragraph, QStringLiteral("任务状态"));
  addDisabledMenu(paragraph, QStringLiteral("列表缩进"));
  paragraph->addSeparator();
  addAction(paragraph, QStringLiteral("paragraph.insert_before"), QStringLiteral("在上方插入段落"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.insert_after"), QStringLiteral("在下方插入段落"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.link_ref"), QStringLiteral("链接引用"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.footnote"), QStringLiteral("脚注"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.hr"), QStringLiteral("水平分割线"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.toc"), QStringLiteral("内容目录"), {}, false);
  addAction(paragraph, QStringLiteral("paragraph.yaml"), QStringLiteral("YAML Front Matter"), {}, false);
}

void MainWindow::setupFormatMenu() {
  QMenu* format = menuBar()->addMenu(QStringLiteral("格式(&O)"));
  addCheckAction(format, QStringLiteral("format.bold"), QStringLiteral("加粗"), QKeySequence::Bold);
  addCheckAction(format, QStringLiteral("format.italic"), QStringLiteral("斜体"), QKeySequence::Italic);
  addCheckAction(format, QStringLiteral("format.underline"), QStringLiteral("下划线"), QKeySequence::Underline, false, false);
  addCheckAction(format, QStringLiteral("format.code"), QStringLiteral("代码"), QKeySequence(QStringLiteral("Ctrl+Shift+`")));
  addCheckAction(format, QStringLiteral("format.strike"), QStringLiteral("删除线"), QKeySequence(QStringLiteral("Alt+Shift+5")), false, false);
  format->addSeparator();
  addAction(format, QStringLiteral("format.comment"), QStringLiteral("注释"), {}, false);
  addAction(format, QStringLiteral("format.link"), QStringLiteral("超链接"), QKeySequence(QStringLiteral("Ctrl+K")));
  addDisabledMenu(format, QStringLiteral("链接操作"));
  addDisabledMenu(format, QStringLiteral("图像"));
  addAction(format, QStringLiteral("format.clear"), QStringLiteral("清除样式"), QKeySequence(QStringLiteral("Ctrl+\\")), false);
}

void MainWindow::setupTableMenu() {
  QMenu* table = menuBar()->addMenu(QStringLiteral("表格(&B)"));
  addAction(table, QStringLiteral("table.insert_table"), QStringLiteral("插入表格"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.insert_row_before"), QStringLiteral("在上方插入行"));
  addAction(table, QStringLiteral("table.insert_row_after"), QStringLiteral("在下方插入行"));
  addAction(table, QStringLiteral("table.delete_row"), QStringLiteral("删除行"));
  addAction(table, QStringLiteral("table.move_row_up"), QStringLiteral("上移行"));
  addAction(table, QStringLiteral("table.move_row_down"), QStringLiteral("下移行"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.insert_column_before"), QStringLiteral("在左侧插入列"));
  addAction(table, QStringLiteral("table.insert_column_after"), QStringLiteral("在右侧插入列"));
  addAction(table, QStringLiteral("table.delete_column"), QStringLiteral("删除列"));
  addAction(table, QStringLiteral("table.move_column_left"), QStringLiteral("左移列"));
  addAction(table, QStringLiteral("table.move_column_right"), QStringLiteral("右移列"));
  table->addSeparator();
  addAction(table, QStringLiteral("table.align_left"), QStringLiteral("左对齐"));
  addAction(table, QStringLiteral("table.align_center"), QStringLiteral("居中对齐"));
  addAction(table, QStringLiteral("table.align_right"), QStringLiteral("右对齐"));
  addAction(table, QStringLiteral("table.align_none"), QStringLiteral("清除对齐"));
}

void MainWindow::setupCodeMenu() {
  QMenu* code = menuBar()->addMenu(QStringLiteral("代码(&C)"));
  addAction(code, QStringLiteral("code.enter_edit"), QStringLiteral("进入编辑"));
  addAction(code, QStringLiteral("code.exit_edit"), QStringLiteral("退出编辑"));
  addAction(code, QStringLiteral("code.set_language"), QStringLiteral("设置语言..."));
}

void MainWindow::setupMathMenu() {
  QMenu* math = menuBar()->addMenu(QStringLiteral("å…¬å¼(&M)"));
  addAction(math, QStringLiteral("math.enter_edit"), QStringLiteral("è¿›å…¥ç¼–è¾‘"));
  addAction(math, QStringLiteral("math.exit_edit"), QStringLiteral("é€€å‡ºç¼–è¾‘"));
  addAction(math, QStringLiteral("math.set_tex"), QStringLiteral("è®¾ç½® TeX..."));
}

void MainWindow::setupHtmlMenu() {
  QMenu* html = menuBar()->addMenu(QStringLiteral("HTML(&H)"));
  addAction(html, QStringLiteral("html.enter_edit"), QStringLiteral("è¿›å…¥ç¼–è¾‘"));
  addAction(html, QStringLiteral("html.exit_edit"), QStringLiteral("é€€å‡ºç¼–è¾‘"));
  addAction(html, QStringLiteral("html.set_source"), QStringLiteral("è®¾ç½® HTML..."));
}

void MainWindow::setupViewMenu() {
  QMenu* view = menuBar()->addMenu(QStringLiteral("视图(&V)"));
  addCheckAction(view, QStringLiteral("view.sidebar"), QStringLiteral("显示 / 隐藏侧边栏"), QKeySequence(QStringLiteral("Ctrl+Shift+L")), false);
  addAction(view, QStringLiteral("view.outline"), QStringLiteral("大纲"), QKeySequence(QStringLiteral("Ctrl+Shift+1")), false);
  addAction(view, QStringLiteral("view.document_list"), QStringLiteral("文档列表"), QKeySequence(QStringLiteral("Ctrl+Shift+2")), false);
  addAction(view, QStringLiteral("view.file_tree"), QStringLiteral("文件树"), QKeySequence(QStringLiteral("Ctrl+Shift+3")), false);
  addAction(view, QStringLiteral("view.search"), QStringLiteral("搜索"), QKeySequence(QStringLiteral("Ctrl+Shift+F")), false);
  view->addSeparator();
  addCheckAction(view, QStringLiteral("view.source_mode"), QStringLiteral("源代码模式"), QKeySequence(QStringLiteral("Ctrl+/")), false);
  addCheckAction(view, QStringLiteral("view.word_wrap"), QStringLiteral("自动换行"), {}, true);
  addCheckAction(view, QStringLiteral("view.focus"), QStringLiteral("专注模式"), QKeySequence(QStringLiteral("F8")), false, false);
  addCheckAction(view, QStringLiteral("view.typewriter"), QStringLiteral("打字机模式"), QKeySequence(QStringLiteral("F9")), false, false);
  addCheckAction(view, QStringLiteral("view.status_bar"), QStringLiteral("显示状态栏"), {}, true);
  view->addSeparator();
  addAction(view, QStringLiteral("view.word_count"), QStringLiteral("字数统计窗口"), {}, false);
  addAction(view, QStringLiteral("view.diagnostics"), QStringLiteral("渲染诊断"), {}, false);
  addAction(view, QStringLiteral("view.fullscreen"), QStringLiteral("切换全屏"), QKeySequence(QStringLiteral("F11")));
  addCheckAction(view, QStringLiteral("view.always_on_top"), QStringLiteral("保持窗口在最前端"), {}, false, false);
  view->addSeparator();
  addAction(view, QStringLiteral("view.actual_size"), QStringLiteral("实际大小"), QKeySequence(QStringLiteral("Ctrl+Shift+9")));
  addAction(view, QStringLiteral("view.zoom_in"), QStringLiteral("放大"), QKeySequence(QStringLiteral("Ctrl+Shift+=")));
  addAction(view, QStringLiteral("view.zoom_out"), QStringLiteral("缩小"), QKeySequence(QStringLiteral("Ctrl+Shift+-")));
  addAction(view, QStringLiteral("view.window_switch"), QStringLiteral("应用内窗口切换"), QKeySequence(QStringLiteral("Ctrl+Tab")), false);
  addAction(view, QStringLiteral("view.devtools"), QStringLiteral("开发者工具"), QKeySequence(QStringLiteral("Shift+F12")), false);
}

void MainWindow::setupThemeMenu() {
  QMenu* theme = menuBar()->addMenu(QStringLiteral("主题(&T)"));
  addCheckAction(theme, QStringLiteral("theme.github"), QStringLiteral("Github"), {}, true);
  addCheckAction(theme, QStringLiteral("theme.newsprint"), QStringLiteral("Newsprint"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.night"), QStringLiteral("Night"), {}, false);
  addCheckAction(theme, QStringLiteral("theme.pixyll"), QStringLiteral("Pixyll"), {}, false, false);
  addCheckAction(theme, QStringLiteral("theme.whitey"), QStringLiteral("Whitey"), {}, false, false);
  theme->addSeparator();
  addAction(theme, QStringLiteral("theme.open_folder"), QStringLiteral("打开主题文件夹"), {}, false);
  addAction(theme, QStringLiteral("theme.get"), QStringLiteral("获取主题"), {}, false);
}

void MainWindow::setupHelpMenu() {
  QMenu* help = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
  addAction(help, QStringLiteral("help.whats_new"), QStringLiteral("What's New..."), {}, false);
  addAction(help, QStringLiteral("help.quick_start"), QStringLiteral("Quick Start"), {}, false);
  addAction(help, QStringLiteral("help.markdown_ref"), QStringLiteral("Markdown Reference"), {}, false);
  addAction(help, QStringLiteral("help.pandoc"), QStringLiteral("Install and Use Pandoc"), {}, false);
  addAction(help, QStringLiteral("help.custom_themes"), QStringLiteral("Custom Themes"), {}, false);
  addAction(help, QStringLiteral("help.images"), QStringLiteral("Use Images in Muffin"), {}, false);
  addAction(help, QStringLiteral("help.recovery"), QStringLiteral("Data Recovery and Version Control"), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.more_themes"), QStringLiteral("更多主题..."), {}, false);
  addAction(help, QStringLiteral("help.acknowledgements"), QStringLiteral("鸣谢"), {}, false);
  addAction(help, QStringLiteral("help.changelog"), QStringLiteral("更新日志"), {}, false);
  addAction(help, QStringLiteral("help.privacy"), QStringLiteral("隐私条款"), {}, false);
  addAction(help, QStringLiteral("help.website"), QStringLiteral("官方网站"), {}, false);
  addAction(help, QStringLiteral("help.feedback"), QStringLiteral("反馈"), {}, false);
  addAction(help, QStringLiteral("help.update"), QStringLiteral("检查更新..."), {}, false);
  addAction(help, QStringLiteral("help.license"), QStringLiteral("我的许可证..."), {}, false);
  help->addSeparator();
  addAction(help, QStringLiteral("help.about"), QStringLiteral("关于"));
}

void MainWindow::updateTitle() {
  const QString marker = session_.document().isModified() ? QStringLiteral(" *") : QString();
  setWindowTitle(QStringLiteral("%1%2 - Muffin").arg(session_.displayName(), marker));
}

void MainWindow::updateStatus() {
  parseLabel_->setText(QStringLiteral("解析 %1 ms").arg(session_.lastParseElapsedMs()));
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
    renderCursorStatus_ = QStringLiteral("表格 %1:%2 offset %3").arg(hit.tableRow + 1).arg(hit.tableColumn + 1).arg(hit.textOffset);
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
  wordsLabel_->setText(QStringLiteral("%1 词").arg(countWords(session_.markdownText())));
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
    QAction* clearAction = recentFilesMenu_->addAction(QStringLiteral("清除最近文件"));
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
