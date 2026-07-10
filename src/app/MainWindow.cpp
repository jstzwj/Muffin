#include "app/MainWindow.h"

#include "app/LanguageManager.h"
#include "app/MarkdownSettings.h"
#include "app/RenderEditorBackend.h"
#include "app/SidebarWidget.h"
#include "app/SourceEditorBackend.h"
#include "app/StatusBarWidget.h"
#include "app/UpdateChecker.h"
#include "document/MarkdownNode.h"
#include "document/OutlineBuilder.h"
#include "document/SourceRangeUtil.h"
#include "editor/EditorView.h"
#include "spellcheck/SpellChecker.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "theme/ChromeStyleSheet.h"
#include "theme/ThemeDefinition.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFontDatabase>
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
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QGraphicsOpacityEffect>
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

#if defined(Q_OS_WIN)
// Dark-mode title bar: the OS draws the caption (title text + min/max/close
// buttons), so Qt style sheets cannot reach it. Toggling the DWM attribute
// below makes that caption follow the theme's brightness. NOMINMAX /
// WIN32_LEAN_AND_MEAN keep <windows.h> from clashing with Qt/std symbols.
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <dwmapi.h>
#  pragma comment(lib, "dwmapi")
#  ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#    define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#  endif
#endif

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

}  // namespace

muffin::MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), renderCommands_(editorController_, [this] { return backend_ && backend_->isSourceMode(); }) {
  setupUi();
  setupMenuBar();
  setupStatusBar();
  setupConnections();
  loadAppearanceSettings();
  showBlockSourceEnabled_ = QSettings().value(QStringLiteral("editor/showBlockSource"), false).toBool();
  // Apply markdown parse preferences (markdown/autoLink, markdown/inlineMath, ...) before the first
  // real parse. parseOptions_ defaults to all-on, so this is a no-op re-parse unless the user has
  // disabled an extension.
  session_.setParseOptions(markdownParseOptions());
  muffin::UpdateChecker::instance().maybeAutoCheck();
}

bool muffin::MainWindow::openFile(QString path) {
  if (fileController_.open(session_, this, path)) {
    draftKey_ = DraftRecovery::createDraftKey();
    editorController_.clearHistoryAndSelection();
    addRecentFile(session_.filePath());
    return true;
  }
  return false;
}

bool muffin::MainWindow::startNewDocument() {
  if (!fileController_.newFile(session_, this)) {
    return false;
  }
  draftKey_ = DraftRecovery::createDraftKey();
  editorController_.clearHistoryAndSelection();
  return true;
}

void muffin::MainWindow::closeEvent(QCloseEvent* event) {
  // Auto-save before the discard prompt: persist a pathed, modified document
  // silently so the "unsaved changes" dialog only concerns untitled documents.
  performAutoSave();
  // Ensure the latest content is snapshotted for crash recovery before exit.
  snapshotDraft();
  if (maybeSaveChanges()) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    event->accept();
  } else {
    event->ignore();
  }
}

void muffin::MainWindow::quitApplication() {
  // Single quit path behind File → Quit and macOS Cmd+Q. Closing every top-level
  // window lets each one run its closeEvent (auto-save + unsaved-changes prompt);
  // Qt leaves the event loop once the last window closes.
  QApplication::closeAllWindows();
}

void muffin::MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  // Retranslation on a language change is handled by the LanguageManager::
  // languageChanged signal (connected in MainWindowSignalBinder), which fires
  // synchronously during setLanguage() before Qt delivers the posted LanguageChange
  // events. Intentionally not retranslating here keeps all menu work out of Qt's
  // LanguageChange delivery (retranslateUi() updates menus in place, but running
  // it mid-delivery is unnecessary and best avoided).
}

void muffin::MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  // DWM dark-mode often only takes hold once the window is actually shown, so
  // re-apply on reveal (cheap; one Win32 call) to cover the startup path.
  applyNativeTitleBarDarkMode(themeManager_.currentDefinition().colors.isDark);
}

void muffin::MainWindow::applyNativeTitleBarDarkMode(bool dark) {
#if defined(Q_OS_WIN)
  if (HWND hwnd = reinterpret_cast<HWND>(winId())) {
    const BOOL value = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
  }
#else
  Q_UNUSED(dark);
#endif
}

void muffin::MainWindow::setupUi() {
  centralSplitter_ = new QSplitter(Qt::Horizontal, this);
  centralSplitter_->setChildrenCollapsible(false);
  centralSplitter_->setHandleWidth(1);

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

  // Sidebar show/hide width transition. min+max move in lockstep so the splitter tracks the value
  // exactly (a lone maximumWidth wouldn't pull the widget wider than its sizeHint). On finish we
  // restore the 220–360 drag range — the live width already sits inside it, so there's no snap.
  sidebarAnimation_ = new QVariantAnimation(this);
  sidebarAnimation_->setEasingCurve(QEasingCurve::OutCubic);
  sidebarAnimation_->setDuration(180);
  connect(sidebarAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
    if (!sidebar_) {
      return;
    }
    const int w = value.toInt();
    sidebar_->setMinimumWidth(w);
    sidebar_->setMaximumWidth(w);
  });
  connect(sidebarAnimation_, &QVariantAnimation::finished, this, [this] {
    if (!sidebar_) {
      return;
    }
    if (sidebar_->maximumWidth() == 0) {
      sidebar_->setVisible(false);
    }
    sidebar_->setMinimumWidth(220);
    sidebar_->setMaximumWidth(360);
  });
}

void muffin::MainWindow::setupMenuBar() {
  commands_.clearActions();
  menuBar()->clear();
  qDeleteAll(actionGroups_);
  actionGroups_.clear();
  buildMenus();
}

void muffin::MainWindow::setupStatusBar() {
  statusBar()->setSizeGripEnabled(false);

  statusBar_ = new StatusBarWidget(this);
  // Reuse the widget's internal buttons so every existing reference (icon/checked/
  // tooltip/click) keeps working without rewiring.
  sidebarButton_ = statusBar_->sidebarButton();
  sourceModeButton_ = statusBar_->sourceModeButton();
  statusBar_->setSpellLanguage(SpellChecker::instance().language(), SpellChecker::instance().isEnabled());
  const int lineBreak = QSettings().value(QStringLiteral("editor/defaultLineBreak"), 1).toInt();
  statusBar_->setEncodingLineEnding(QStringLiteral("UTF-8 | %1").arg(
      lineBreak == 1 ? QStringLiteral("CRLF") : QStringLiteral("LF")));
  wordCountTimer_ = new QTimer(this);
  wordCountTimer_->setSingleShot(true);
  wordCountTimer_->setInterval(250);
  connect(wordCountTimer_, &QTimer::timeout, this, &muffin::MainWindow::updateWordCountNow);

  // Only heading-index revisions schedule this debounce; paragraph typing does no outline work.
  // Full parses refresh immediately.
  outlineTimer_ = new QTimer(this);
  outlineTimer_->setSingleShot(true);
  outlineTimer_->setInterval(200);
  connect(outlineTimer_, &QTimer::timeout, this, &muffin::MainWindow::refreshSidebarOutline);

  // Auto-save: debounced silent write of a pathed, modified document. Untitled
  // documents (no filePath) are covered by draft-recovery snapshots, not here.
  autoSaveTimer_ = new QTimer(this);
  autoSaveTimer_->setSingleShot(true);
  autoSaveTimer_->setInterval(1500);
  connect(autoSaveTimer_, &QTimer::timeout, this, &muffin::MainWindow::performAutoSave);

  // Crash-recovery: debounce-snapshot the current document so a forced exit can
  // be restored next launch. See DraftRecovery / offerDraftRecovery.
  draftTimer_ = new QTimer(this);
  draftTimer_->setSingleShot(true);
  draftTimer_->setInterval(3000);
  connect(draftTimer_, &QTimer::timeout, this, &muffin::MainWindow::snapshotDraft);

  statusBar()->addWidget(statusBar_, 1);
}

void muffin::MainWindow::updateTitle() {
  const QString marker = session_.document().isModified() ? QStringLiteral(" *") : QString();
  setWindowTitle(QStringLiteral("%1%2 - Muffin").arg(session_.displayName(), marker));
}

void muffin::MainWindow::updateStatus() {
  if (!statusBar_) {
    return;
  }
  if (!backend_->isSourceMode() && !renderCursorStatus_.isEmpty()) {
    statusBar_->setCursorStatus(renderCursorStatus_);
  } else {
    statusBar_->setCursorStatus(QStringLiteral("%1:%2").arg(cursorLine_).arg(cursorColumn_));
  }
  const TextFileFormat& format = session_.fileFormat();
  QString lineEnding;
  if (!format.existingFile && session_.filePath().isEmpty()) {
    lineEnding = QSettings().value(QStringLiteral("editor/defaultLineBreak"), 1).toInt() == 1
        ? QStringLiteral("CRLF") : QStringLiteral("LF");
  } else {
    switch (format.lineEnding) {
      case TextLineEnding::Crlf: lineEnding = QStringLiteral("CRLF"); break;
      case TextLineEnding::Cr: lineEnding = QStringLiteral("CR"); break;
      case TextLineEnding::Lf: lineEnding = QStringLiteral("LF"); break;
    }
  }
  statusBar_->setEncodingLineEnding(
      QStringLiteral("%1%2 | %3")
          .arg(format.encodingName,
               format.writeBom ? QStringLiteral(" BOM") : QString(),
               lineEnding));
  // The block-source preview is render-mode only; clear any stale text when editing source.
  if (backend_ && backend_->isSourceMode()) {
    statusBar_->setBlockSource(QString(), QString());
  }
}

void muffin::MainWindow::updateCursorStatus(int line, int column) {
  cursorLine_ = line;
  cursorColumn_ = column;
  scheduleEditorStateRefresh();
}

void muffin::MainWindow::updateSidebarMode() {
  if (!sidebar_ || !sidebarButton_) {
    return;
  }
  const QAction* action = commands_.action(QStringLiteral("view.sidebar"));
  const bool sidebarVisible = action && action->isChecked();
  sidebarButton_->setChecked(sidebarVisible);
  if (!sidebarVisible) {
    if (outlineTimer_) {
      outlineTimer_->stop();
    }
    sidebar_->clearOutline();
    outlineDirty_ = true;
  }

  // No transition at startup (window not shown yet) or when reduced motion is requested.
  if (!isVisible() || reducedMotion() || !sidebarAnimation_) {
    sidebar_->setMinimumWidth(220);
    sidebar_->setMaximumWidth(360);
    sidebar_->setVisible(sidebarVisible);
    if (sidebarVisible && sidebar_->panel() == SidebarWidget::Panel::Outline) {
      refreshSidebarOutline();
    }
    return;
  }

  if (sidebarVisible) {
    sidebar_->setMinimumWidth(0);
    sidebar_->setMaximumWidth(0);
    sidebar_->setVisible(true);
    sidebarTargetWidth_ = qBound(220, sidebarTargetWidth_, 360);
    animateSidebarWidth(0, sidebarTargetWidth_);
    if (sidebar_->panel() == SidebarWidget::Panel::Outline) {
      refreshSidebarOutline();
    }
  } else {
    const int currentWidth = qBound(0, sidebar_->width(), 360);
    sidebarTargetWidth_ = qBound(220, currentWidth, 360);  // remember for next show
    animateSidebarWidth(currentWidth, 0);
  }
}

bool muffin::MainWindow::reducedMotion() const {
  return QSettings().value(QStringLiteral("appearance/reducedMotion"), false).toBool();
}

void muffin::MainWindow::animateSidebarWidth(int from, int to) {
  if (!sidebarAnimation_) {
    return;
  }
  sidebarAnimation_->stop();
  sidebarAnimation_->setStartValue(from);
  sidebarAnimation_->setEndValue(to);
  sidebarAnimation_->start();
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
  if (panel == SidebarWidget::Panel::Outline) {
    refreshSidebarOutline();
  } else {
    outlineTimer_->stop();
    sidebar_->clearOutline();
    outlineDirty_ = true;
  }
}

void muffin::MainWindow::refreshSidebarDocumentInfo() {
  if (!sidebar_) {
    return;
  }
  sidebar_->setCurrentDocument(session_.displayName(), session_.filePath(), session_.document().isModified());
}

void muffin::MainWindow::refreshSidebarOutline() {
  const QAction* sidebarAction = commands_.action(QStringLiteral("view.sidebar"));
  if (!sidebar_ || !outlineDirty_ || !sidebarAction || !sidebarAction->isChecked() ||
      sidebar_->panel() != SidebarWidget::Panel::Outline) {
    return;
  }
  sidebar_->setOutline(session_.document().outline());
  sidebarOutlineRevision_ = session_.document().outlineRevision();
  outlineDirty_ = false;
}

void muffin::MainWindow::openFolder() {
  const QString initialPath = sidebarFolderRoot_.isEmpty() ? QFileInfo(session_.filePath()).absolutePath() : sidebarFolderRoot_;
  const QString path = QFileDialog::getExistingDirectory(this, tr("Open Folder"), initialPath);
  if (path.isEmpty()) {
    return;
  }
  openFolderAtPath(path);
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
  // Snapshot the outgoing page just before the swap, then crossfade it out over the new page.
  QPixmap outgoingSnapshot;
  if (isVisible() && !reducedMotion() && viewStack_->currentWidget() != nullptr) {
    outgoingSnapshot = viewStack_->currentWidget()->grab();
  }
  viewStack_->setCurrentWidget(sourceMode ? static_cast<QWidget*>(editor_) : static_cast<QWidget*>(renderView_));
  if (!outgoingSnapshot.isNull()) {
    showViewSwitchOverlay(outgoingSnapshot);
  }
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

void muffin::MainWindow::showViewSwitchOverlay(const QPixmap& snapshot) {
  if (!viewStack_ || snapshot.isNull()) {
    return;
  }
  // A reusable QLabel overlaid on the stack, showing a grab() of the outgoing page and faded out
  // over 120ms. The QGraphicsOpacityEffect is attached to this lightweight label only — never to
  // the live EditorView/QPlainTextEdit, which would force an offscreen backing store every frame.
  if (!viewSwitchOverlay_) {
    viewSwitchOverlay_ = new QLabel(viewStack_);
    viewSwitchOverlay_->setAlignment(Qt::AlignCenter);
    auto* opacity = new QGraphicsOpacityEffect(viewSwitchOverlay_);
    opacity->setOpacity(1.0);
    viewSwitchOverlay_->setGraphicsEffect(opacity);
    overlayOpacityAnimation_ = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), this);
    overlayOpacityAnimation_->setDuration(120);
    overlayOpacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(overlayOpacityAnimation_, &QPropertyAnimation::finished, this, [this] {
      if (viewSwitchOverlay_) {
        viewSwitchOverlay_->hide();
      }
    });
  }
  viewSwitchOverlay_->setPixmap(snapshot);
  viewSwitchOverlay_->setGeometry(viewStack_->rect());
  viewSwitchOverlay_->raise();
  viewSwitchOverlay_->show();
  overlayOpacityAnimation_->stop();
  overlayOpacityAnimation_->setStartValue(1.0);
  overlayOpacityAnimation_->setEndValue(0.0);
  overlayOpacityAnimation_->start();
}

void muffin::MainWindow::printDocument() {
  if (session_.filePath().isEmpty()) {
    return;
  }

  QPrintDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  paintDocumentToPrinter(dialog.printer());
}

void muffin::MainWindow::paintDocumentToPrinter(QPrinter* printer) {
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

  for (const BlockLayout* block : layout.promotedBlocks()) {
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
    renderView_->setTypewriterCursorMiddle(QSettings().value(QStringLiteral("editor/typewriterCursorMiddle"), true).toBool());
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
#ifdef Q_OS_DARWIN
  // macOS renders body text small at the Windows-tuned 16px default: 16px → 12pt body, which
  // reads cramped next to the system's 13pt UI and native mac editors that default larger.
  // 20px (≈15pt) matches comfortable mac reading size. Only affects users who never set their
  // own size; a persisted appearance/fontSizePx still wins.
  constexpr int kDefaultFontSizePx = 20;
#else
  constexpr int kDefaultFontSizePx = 16;
#endif
  setFontSizePx(settings.value(QStringLiteral("appearance/fontSizePx"), kDefaultFontSizePx).toInt());

  restorePersistentActionStates();

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
  if (renderView_) {
    renderView_->setTypewriterCursorMiddle(settings.value(QStringLiteral("editor/typewriterCursorMiddle"), true).toBool());
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
  const ThemeDefinition def = themeManager_.definition(name);
  renderView_->setTheme(theme);
  editor_->setTheme(theme);
  if (sidebar_) {
    sidebar_->applyTheme(def);
  }
  updateThemeActions();
  updateThemeChecks();

  // Chrome (menu bar, menus, tool buttons, splitter handle) now derives
  // entirely from the theme definition — no more hard-coded night/light branch,
  // so every theme (incl. the warm newsprint palette) tints the chrome.
  setStyleSheet(mainWindowStyleSheet(def));
  if (centralSplitter_) {
    centralSplitter_->setStyleSheet(
        QStringLiteral("QSplitter::handle { background:%1; width:1px; }").arg(def.colors.border.name(QColor::HexRgb)));
  }

  // The painted status bar recolors itself (background, text, icons) from the theme.
  // The top separator uses the chrome border (a guaranteed-valid hairline), NOT
  // the code-border colour: that's a document/code token which is invalid on
  // themes with no code-border rule, and an invalid QPen renders solid black.
  if (statusBar_) {
    statusBar_->applyThemeColors(theme.backgroundColor(), theme.textColor(), theme.mutedTextColor(),
                                 def.colors.border);
  }

  applyNativeTitleBarDarkMode(def.colors.isDark);
}
