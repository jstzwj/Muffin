#include "app/MainWindow.h"

#include "app/LanguageManager.h"
#include "app/MainWindowActionBinder.h"
#include "app/RenderEditorBackend.h"
#include "app/SidebarWidget.h"
#include "app/SourceEditorBackend.h"
#include "document/OutlineBuilder.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLoggingCategory>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

Q_LOGGING_CATEGORY(mainWindowPerf, "muffin.perf", QtWarningMsg)

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

enum class StatusBarIconKind { Sidebar, SourceMode };

QIcon statusBarIcon(StatusBarIconKind kind, const QColor& ink) {
  const char* resourcePath = nullptr;
  switch (kind) {
    case StatusBarIconKind::Sidebar:
      resourcePath = ":/icons/statusbar/sidebar-toggle.svg";
      break;
    case StatusBarIconKind::SourceMode:
      resourcePath = ":/icons/statusbar/code-brackets.svg";
      break;
  }

  // Load SVG bytes and recolor
  QFile svgFile(QString::fromLatin1(resourcePath));
  if (!svgFile.open(QIODevice::ReadOnly)) {
    return QIcon();
  }
  QByteArray svgData = svgFile.readAll();
  svgFile.close();
  svgData.replace("#000000", ink.name(QColor::HexRgb).toUtf8());

  // Render SVG via QSvgRenderer directly — no imageformat plugin needed
  QSvgRenderer renderer(svgData);
  if (!renderer.isValid()) {
    return QIcon();
  }

  const qreal dpr = qApp->devicePixelRatio();
  QIcon icon;
  constexpr int sizes[] = {16, 24, 32};
  for (const int sz : sizes) {
    QPixmap px(static_cast<int>(sz * dpr), static_cast<int>(sz * dpr));
    px.setDevicePixelRatio(dpr);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p);
    p.end();
    icon.addPixmap(px);
  }
  return icon;
}

QColor statusBarIconInk(const QString& themeName) {
  if (themeName == QStringLiteral("night")) {
    return QColor(0x9a, 0xa4, 0xaf);
  }
  return QColor(0x55, 0x55, 0x55);
}

}  // namespace

muffin::MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), renderCommands_(editorController_, [this] { return backend_ && backend_->isSourceMode(); }) {
  setupUi();
  setupMenuBar();
  setupStatusBar();
  setupConnections();
  applyTyporaLikeChrome();
  loadAppearanceSettings();
}

bool muffin::MainWindow::openFile(QString path) {
  if (fileController_.open(session_, this, path)) {
    editorController_.clearHistoryAndSelection();
    addRecentFile(session_.filePath());
    return true;
  }
  return false;
}

void muffin::MainWindow::closeEvent(QCloseEvent* event) {
  if (maybeSaveChanges()) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    event->accept();
  } else {
    event->ignore();
  }
}

void muffin::MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    retranslateUi();
  }
  QMainWindow::changeEvent(event);
}

void muffin::MainWindow::setupUi() {
  centralSplitter_ = new QSplitter(Qt::Horizontal, this);
  centralSplitter_->setChildrenCollapsible(false);
  centralSplitter_->setHandleWidth(1);
  centralSplitter_->setStyleSheet(QStringLiteral("QSplitter::handle { background:#f0f0f0; width:1px; }"));

  sidebar_ = new SidebarWidget(centralSplitter_);

  auto* editorContainer = new QWidget(this);
  auto* editorLayout = new QVBoxLayout(editorContainer);
  editorLayout->setContentsMargins(0, 0, 0, 0);
  editorLayout->setSpacing(0);

  viewStack_ = new QStackedWidget(editorContainer);
  renderView_ = new EditorView(viewStack_);
  editor_ = new SourceEditorWidget(editorContainer);
  viewStack_->addWidget(renderView_);
  viewStack_->addWidget(editor_);

  // Initialize backend to render mode (default)
  backend_ = std::make_unique<RenderEditorBackend>(editorController_, session_, renderView_);

  findBar_ = new FindBarWidget(editorContainer);
  findBar_->setVisible(false);

  editorLayout->addWidget(findBar_);
  editorLayout->addWidget(viewStack_, 1);

  centralSplitter_->addWidget(sidebar_);
  centralSplitter_->addWidget(editorContainer);
  centralSplitter_->setStretchFactor(0, 0);
  centralSplitter_->setStretchFactor(1, 1);
  sidebar_->setVisible(false);
  setCentralWidget(centralSplitter_);
}

void muffin::MainWindow::setupMenuBar() {
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

void muffin::MainWindow::setupStatusBar() {
  statusBar()->setSizeGripEnabled(false);

  const QColor ink = statusBarIconInk(themeManager_.currentThemeName());

  sidebarButton_ = new QToolButton(this);
  sidebarButton_->setIcon(statusBarIcon(StatusBarIconKind::Sidebar, ink));
  sidebarButton_->setIconSize(QSize(16, 16));
  sidebarButton_->setCheckable(true);
  sidebarButton_->setAutoRaise(true);

  sourceModeButton_ = new QToolButton(this);
  sourceModeButton_->setIcon(statusBarIcon(StatusBarIconKind::SourceMode, ink));
  sourceModeButton_->setIconSize(QSize(16, 16));
  sourceModeButton_->setCheckable(true);
  sourceModeButton_->setAutoRaise(true);

  cursorLabel_ = new QLabel(this);
  wordsLabel_ = new QLabel(this);
  wordCountTimer_ = new QTimer(this);
  wordCountTimer_->setSingleShot(true);
  wordCountTimer_->setInterval(250);
  connect(wordCountTimer_, &QTimer::timeout, this, &muffin::MainWindow::updateWordCountNow);

  statusBar()->addWidget(sidebarButton_);
  statusBar()->addWidget(sourceModeButton_);
  statusBar()->addPermanentWidget(cursorLabel_);
  statusBar()->addPermanentWidget(wordsLabel_);
}

void muffin::MainWindow::applyTyporaLikeChrome() {
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
      "QStatusBar QLabel { padding: 0 8px; }"
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

void muffin::MainWindow::updateTitle() {
  const QString marker = session_.document().isModified() ? QStringLiteral(" *") : QString();
  setWindowTitle(QStringLiteral("%1%2 - Muffin").arg(session_.displayName(), marker));
}

void muffin::MainWindow::updateStatus() {
  if (!backend_->isSourceMode() && !renderCursorStatus_.isEmpty()) {
    cursorLabel_->setText(renderCursorStatus_);
  } else {
    cursorLabel_->setText(QStringLiteral("%1:%2").arg(cursorLine_).arg(cursorColumn_));
  }
}

void muffin::MainWindow::updateCursorStatus(int line, int column) {
  cursorLine_ = line;
  cursorColumn_ = column;
  updateStatus();
}

void muffin::MainWindow::updateSidebarMode() {
  if (!sidebar_ || !sidebarButton_) {
    return;
  }
  const QAction* action = commands_.action(QStringLiteral("view.sidebar"));
  const bool sidebarVisible = action && action->isChecked();
  sidebar_->setVisible(sidebarVisible);
  sidebarButton_->setChecked(sidebarVisible);
}

void muffin::MainWindow::setSidebarPanel(SidebarWidget::Panel panel) {
  if (!sidebar_) {
    return;
  }
  sidebar_->setPanel(panel);
  if (QAction* action = commands_.action(QStringLiteral("view.sidebar"))) {
    action->setChecked(true);
  }
  updateSidebarMode();
}

void muffin::MainWindow::refreshSidebarDocumentInfo() {
  if (!sidebar_) {
    return;
  }
  sidebar_->setCurrentDocument(session_.displayName(), session_.filePath(), session_.document().isModified());
}

void muffin::MainWindow::refreshSidebarOutline() {
  if (!sidebar_) {
    return;
  }
  sidebar_->setOutline(buildOutline(session_.document()));
}

void muffin::MainWindow::openFolder() {
  const QString initialPath = sidebarFolderRoot_.isEmpty() ? QFileInfo(session_.filePath()).absolutePath() : sidebarFolderRoot_;
  const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), initialPath);
  if (path.isEmpty()) {
    return;
  }
  sidebarFolderRoot_ = path;
  sidebar_->setFolderRoot(path);
  setSidebarPanel(SidebarWidget::Panel::Files);
}

void muffin::MainWindow::openNewWindow() {
  auto* window = new MainWindow();
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->show();
}

void muffin::MainWindow::activateOutlineNode(NodeId nodeId, SourceRange sourceRange) {
  if (backend_->isSourceMode()) {
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

void muffin::MainWindow::updateViewMode() {
  if (!viewStack_ || !renderView_ || !editor_) {
    return;
  }
  const QAction* action = commands_.action(QStringLiteral("view.source_mode"));
  const bool sourceMode = action && action->isChecked();

  // Switch backend
  if (sourceMode) {
    backend_ = std::make_unique<SourceEditorBackend>(editor_);
  } else {
    backend_ = std::make_unique<RenderEditorBackend>(editorController_, session_, renderView_);
  }

  if (sourceMode) {
    syncSourceEditorIfNeeded();

    // Render → Source: sync cursor/viewport position
    qsizetype targetOffset = -1;
    if (editorController_.selection().hasCursor()) {
      targetOffset = editorController_.selection().cursorPosition().text.sourceOffset;
    }
    if (targetOffset < 0) {
      const BlockLayout* topBlock = renderView_->blockAtViewportPos(QPointF(0, 0));
      if (topBlock) {
        MarkdownNode* node = session_.document().node(topBlock->nodeId());
        if (node) {
          targetOffset = node->sourceRange().byteStart;
        }
      }
    }
    if (targetOffset >= 0) {
      QTextCursor cursor = editor_->editor()->textCursor();
      const int maxPos = qMax(0, editor_->editor()->document()->characterCount() - 1);
      cursor.setPosition(qBound(0, static_cast<int>(targetOffset), maxPos));
      editor_->editor()->setTextCursor(cursor);
      editor_->editor()->centerCursor();
    }
  }
  viewStack_->setCurrentWidget(sourceMode ? static_cast<QWidget*>(editor_) : static_cast<QWidget*>(renderView_));
  if (sourceModeButton_) {
    sourceModeButton_->setChecked(sourceMode);
  }
  if (!sourceMode) {
    // Source → Render: scroll to the block at the source cursor position
    const int sourcePos = editor_->editor()->textCursor().position();
    MarkdownNode* block = session_.document().topLevelBlockAtOffset(static_cast<qsizetype>(sourcePos));
    if (block) {
      renderView_->scrollToNode(block->id());
    }
    renderView_->setFocus(Qt::OtherFocusReason);
  }
  updateStatus();
}

void muffin::MainWindow::printDocument() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  QPrintDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QPrinter* printer = dialog.printer();
  if (!printer) {
    return;
  }

  const RenderTheme theme = themeManager_.currentTheme(zoomPercent_, fontSizePx_);
  const QRectF page = printer->pageRect(QPrinter::DevicePixel);

  DocumentLayout layout;
  layout.rebuild(session_.document(), theme, page.width());

  QPainter painter(printer);
  if (!painter.isActive()) {
    return;
  }
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.translate(page.topLeft());

  const qreal pageHeight = page.height();
  qreal pageTop = 0;

  for (const auto& block : layout.blocks()) {
    if (block->rect().bottom() > pageTop + pageHeight && block->rect().top() > pageTop) {
      printer->newPage();
      pageTop = block->rect().top();
    }
    block->paint(painter, theme, pageTop);
  }

  painter.end();
}

int muffin::MainWindow::zoomPercent() const {
  return zoomPercent_;
}

void muffin::MainWindow::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
  editor_->setZoomPercent(zoomPercent_);
  renderView_->setZoomPercent(zoomPercent_);
  updateStatus();
}

int muffin::MainWindow::fontSizePx() const {
  return fontSizePx_;
}

void muffin::MainWindow::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
  editor_->setFontSizePx(fontSizePx_);
  renderView_->setFontSizePx(fontSizePx_);
  updateStatus();
}

void muffin::MainWindow::setStatusBarVisible(bool visible) {
  if (QAction* action = commands_.action(QStringLiteral("view.status_bar"))) {
    action->setChecked(visible);
  }
  statusBar()->setVisible(visible);
}

void muffin::MainWindow::setFocusMode(bool enabled) {
  focusMode_ = enabled;

  if (renderView_) {
    renderView_->setFocusMode(enabled);
  }

  if (QAction* action = commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(enabled);
  }
}

void muffin::MainWindow::setTypewriterMode(bool enabled) {
  typewriterMode_ = enabled;

  if (renderView_) {
    renderView_->setTypewriterMode(enabled);
  }

  if (QAction* action = commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(enabled);
  }

  // Apply immediately if just enabled
  if (enabled && backend_) {
    backend_->centerCursor();
  }
}

void muffin::MainWindow::loadAppearanceSettings() {
  QSettings settings;

  // Restore window geometry (never fullscreen)
  const QByteArray geo = settings.value(QStringLiteral("window/geometry")).toByteArray();
  if (!geo.isEmpty()) {
    restoreGeometry(geo);
    if (isFullScreen()) {
      showNormal();
    }
  }

  const QString themeName = settings.value(QStringLiteral("appearance/themeName"), themeManager_.currentThemeName()).toString();
  themeManager_.setTheme(themeName);
  setStatusBarVisible(settings.value(QStringLiteral("appearance/showStatusBar"), true).toBool());
  setZoomPercent(settings.value(QStringLiteral("appearance/zoomPercent"), 100).toInt());
  setFontSizePx(settings.value(QStringLiteral("appearance/fontSizePx"), 16).toInt());

  MainWindowActionBinder::restorePersistentActionStates(*this);

  if (QAction* action = commands_.action(QStringLiteral("view.word_wrap")); action && !action->isChecked()) {
    editor_->setWordWrapEnabled(false);
  }

  if (QAction* action = commands_.action(QStringLiteral("view.sidebar")); action && action->isChecked()) {
    updateSidebarMode();
  }

  if (QAction* action = commands_.action(QStringLiteral("view.source_mode")); action && action->isChecked()) {
    updateViewMode();
  }

  if (QAction* action = commands_.action(QStringLiteral("view.typewriter")); action && action->isChecked()) {
    setTypewriterMode(true);
  }
}

void muffin::MainWindow::saveAppearanceTheme(const QString& name) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/themeName"), name);
}

void muffin::MainWindow::saveAppearanceStatusBarVisible(bool visible) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/showStatusBar"), visible);
}

void muffin::MainWindow::saveAppearanceZoomPercent(int percent) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/zoomPercent"), qBound(60, percent, 200));
}

void muffin::MainWindow::saveAppearanceFontSizePx(int px) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/fontSizePx"), qBound(12, px, 24));
}

void muffin::MainWindow::saveAppearanceFocusMode(bool enabled) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/focusMode"), enabled);
}

void muffin::MainWindow::saveAppearanceTypewriterMode(bool enabled) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/typewriterMode"), enabled);
}

void muffin::MainWindow::applyTheme(QString name) {
  const RenderTheme theme = themeManager_.currentTheme(zoomPercent_, fontSizePx_);
  renderView_->setTheme(theme);
  editor_->setTheme(theme);
  if (sidebar_) {
    sidebar_->applyThemeName(name);
  }
  MainWindowActionBinder::updateThemeActions(*this);

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
        "QStatusBar QLabel { padding:0 8px; }"
        "QToolButton { background:transparent; border:0; color:#9aa4af; padding:0 8px; min-width:22px; min-height:18px; font-size:12px; }"
        "QToolButton:hover { background:#2b3138; }"
        "QToolButton:checked { color:#e6edf3; background:#30363d; }"));
  } else {
    applyTyporaLikeChrome();
  }

  const QColor ink = statusBarIconInk(name);
  if (sidebarButton_) {
    sidebarButton_->setIcon(statusBarIcon(StatusBarIconKind::Sidebar, ink));
  }
  if (sourceModeButton_) {
    sourceModeButton_->setIcon(statusBarIcon(StatusBarIconKind::SourceMode, ink));
  }
}

int muffin::MainWindow::countWords(const QString& text) {
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
