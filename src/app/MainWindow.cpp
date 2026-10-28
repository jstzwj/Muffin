#include "app/MainWindow.h"
#include "editor/ShortcutSettings.h"
#include "app/FileOpenConfirmation.h"
#include "diagnostics/ScopedPerfProbe.h"

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
#include <QShortcut>
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
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QStatusBar>
#include <QSvgRenderer>
#include <QPrintDialog>
#include <QPrinter>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#if defined(Q_OS_WIN)
// Dark-mode title bar: the OS draws the caption (title text + min/max/close
// buttons), so Qt style sheets cannot reach it. Toggling the DWM attribute
// below makes that caption follow the theme's brightness. NOMINMAX /
// WIN32_LEAN_AND_MEAN keep <windows.h> from clashing with Qt/std symbols.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi")
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
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

} // namespace

muffin::MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
  setupMenuBar();
  setupStatusBar();
  setupConnections();
  sessionSaveTimer_ = new QTimer(this);
  sessionSaveTimer_->setSingleShot(true);
  sessionSaveTimer_->setInterval(100);
  connect(sessionSaveTimer_, &QTimer::timeout, this, &MainWindow::saveSessionState);
  connect(tabs_->tabBar(), &QTabBar::tabMoved, this, [this] {
    normalizePinnedTabs();
    scheduleSessionSave();
  });
  connect(&draftWriter_, &DraftSnapshotWriter::snapshotFinished, this,
          [this](const QString& key, quint64 revision, bool saved) {
            if (!saved)
              return;
            for (auto* document : documentTabs())
              if (document->draftKey == key && document->session.document().isModified())
                document->lastDraftSnapshotRevision = qMax(document->lastDraftSnapshotRevision, revision);
          });
  updateTitle();
  loadAppearanceSettings();
  showBlockSourceEnabled_ = QSettings().value(QStringLiteral("editor/showBlockSource"), false).toBool();
  // Apply markdown parse preferences (markdown/autoLink, markdown/inlineMath, ...) before the first
  // real parse. parseOptions_ defaults to all-on, so this is a no-op re-parse unless the user has
  // disabled an extension.
  tab_->session.setParseOptions(markdownParseOptions());
  muffin::UpdateChecker::instance().maybeAutoCheck();
  sessionUpdatesPaused_ = false;
}

muffin::MainWindow::~MainWindow() {
  for (auto* document : documentTabs())
    if (!document->session.document().isModified())
      draftWriter_.markClean(document->session.filePath(), document->draftKey);
  draftWriter_.waitForDone();
  // Destroy pages while shared window services (drafts, commands, emoji provider) still exist.
  disconnect(tabs_, nullptr, this, nullptr);
  tabs_->tabBar()->removeEventFilter(this);
  delete centralSplitter_;
}

void muffin::MainWindow::closeEvent(QCloseEvent* event) {
  if (workspaceWriteInProgress_) {
    event->ignore();
    return;
  }
  const QScopedValueRollback<bool> switchGuard(suppressSaveOnTabSwitch_, true);
  // Prompting visits every page; those temporary activations must not replace
  // the user's active tab in the persisted session, including on Cancel.
  const QScopedValueRollback<bool> sessionGuard(sessionUpdatesPaused_, true);
  DocumentTab* original = tab_;
  for (DocumentTab* document : documentTabs()) {
    tabs_->setCurrentWidget(document);
    performAutoSave(document);
    snapshotDraft(document);
    if (!maybeSaveChanges()) {
      tabs_->setCurrentWidget(original);
      event->ignore();
      return;
    }
  }
  QSettings settings;
  tabs_->setCurrentWidget(original);
  sessionUpdatesPaused_ = false;
  saveSessionState();
  settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
  for (DocumentTab* document : documentTabs()) {
    document->autoSaveTimer->stop();
    document->draftTimer->stop();
    clearDraftSnapshot(document->session.filePath(), document->draftKey);
  }
  event->accept();
}

void muffin::MainWindow::quitApplication() {
  // Single quit path behind File → Quit and macOS Cmd+Q. Closing every top-level
  // window lets each one run its closeEvent (auto-save + unsaved-changes prompt);
  // Qt leaves the event loop once the last window closes.
  QApplication::closeAllWindows();
}

void muffin::MainWindow::cycleFocus(bool forward) {
  if (!tab_->viewStack || !sidebar_) {
    return;
  }
  QWidget* targets[3] = {tab_->viewStack->currentWidget(), sidebar_->activeTreeWidget(),
                         tab_->findBar && tab_->findBar->isVisible() ? tab_->findBar : nullptr};
  if (!targets[0]) {
    return;
  }

  // Which region currently owns focus (default: the editor).
  int current = 0;
  if (QWidget* focus = QApplication::focusWidget()) {
    if (sidebar_->isAncestorOf(focus)) {
      current = 1;
    } else if (tab_->findBar && tab_->findBar->isVisible() && tab_->findBar->isAncestorOf(focus)) {
      current = 2;
    }
  }

  for (int step = 0; step < 3; ++step) {
    const int next = (current + (forward ? 1 : 3 - 1) + 3) % 3;
    if (next == 0) {
      targets[0]->setFocus();
      return;
    }
    if (next == 1 && targets[1]) {
      targets[1]->setFocus();
      return;
    }
    if (next == 2 && targets[2]) {
      tab_->findBar->activateFind();
      return;
    }
    current = next;
  }
  targets[0]->setFocus();
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
  QTimer::singleShot(0, this, [this] { restoreTabViewState(tab_); });
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

  tabs_ = new QTabWidget(centralSplitter_);
  tabs_->setObjectName(QStringLiteral("DocumentTabs"));
  tabs_->setDocumentMode(true);
  tabs_->setTabBarAutoHide(true);
  tabs_->tabBar()->setDrawBase(false);
  tabs_->tabBar()->setExpanding(false);
  tabs_->setMovable(true);
  // setupTabButton owns the close/pin control. Qt's automatic close button
  // uses the platform's side (left on macOS), duplicating our right control.
  tabs_->setTabsClosable(false);
  tabs_->setElideMode(Qt::ElideMiddle);
  tabs_->setUsesScrollButtons(true);
  tabs_->tabBar()->installEventFilter(this);
  tab_ = new DocumentTab(tabs_);
  tabs_->addTab(tab_, tab_->session.displayName());
  setupTabButton(tab_);
  auto* newTabButton = new QToolButton(tabs_);
  newTabButton->setObjectName(QStringLiteral("NewTabButton"));
  newTabButton->setText(QStringLiteral("+"));
  newTabButton->setAutoRaise(true);
  newTabButton->setFixedSize(28, 28);
  tabs_->setCornerWidget(newTabButton, Qt::TopRightCorner);
  updateTabBarVisibility();
  connect(newTabButton, &QToolButton::clicked, this, &MainWindow::startNewDocument);
  retranslateTabs();
  connect(&ShortcutSettings::instance(), &ShortcutSettings::changed, this, &MainWindow::retranslateTabs);
  connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::activateTab);
  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

  centralSplitter_->addWidget(sidebar_);
  centralSplitter_->addWidget(tabs_);
  centralSplitter_->setStretchFactor(0, 0);
  centralSplitter_->setStretchFactor(1, 1);
  sidebar_->setVisible(false);
  setCentralWidget(centralSplitter_);

  // F6 pane cycling between the main regions (editor / sidebar / find bar). The editor canvas
  // consumes Tab for content, so this is the keyboard escape hatch that does not depend on it.
  {
    auto* nextPane = new QShortcut(QKeySequence(Qt::Key_F6), this);
    connect(nextPane, &QShortcut::activated, this, [this] { cycleFocus(true); });
    auto* previousPane = new QShortcut(QKeySequence(Qt::ShiftModifier | Qt::Key_F6), this);
    connect(previousPane, &QShortcut::activated, this, [this] { cycleFocus(false); });
  }

  // Sidebar show/hide width transition. min+max move in lockstep so the splitter tracks the value
  // exactly (a lone maximumWidth wouldn't pull the widget wider than its sizeHint). On finish we
  // restore the shared drag range without changing the current panel's width.
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
    sidebar_->setMinimumWidth(SidebarWidget::kMinimumWidth);
    sidebar_->setMaximumWidth(SidebarWidget::kMaximumWidth);
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
  statusBar_->setEncodingLineEnding(
      QStringLiteral("UTF-8 | %1").arg(lineBreak == 1 ? QStringLiteral("CRLF") : QStringLiteral("LF")));
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

  statusBar()->addWidget(statusBar_, 1);
}

void muffin::MainWindow::updateTitle() {
  updateTabLabel(tab_);
  const QString marker = tab_->session.document().isModified() ? QStringLiteral(" *") : QString();
  const QString name =
      tab_->pendingOpenPath.isEmpty() ? tab_->session.displayName() : QFileInfo(tab_->pendingOpenPath).fileName();
  // 锁定编辑时，状态标记紧跟标题栏的 “Muffin”。
  const QString lockMarker = editingLocked_ ? QStringLiteral(" (已锁定编辑)") : QString();
  setWindowTitle(QStringLiteral("%1%2 - Muffin%3").arg(name, marker, lockMarker));
}

void muffin::MainWindow::setEditingLocked(bool locked) {
  if (editingLocked_ == locked) {
    return;
  }
  editingLocked_ = locked;
  applyEditingLockedTo(tab_);
  if (QAction* action = commands_.action(QStringLiteral("edit.lock_editing"))) {
    action->setChecked(locked);
    action->setText(locked ? tr("解锁编辑") : tr("锁定编辑"));
  }
  updateTitle();
}

void muffin::MainWindow::toggleEditingLocked() {
  setEditingLocked(!editingLocked_);
}

// Push the window-level lock onto one tab: the rendered view hides its caret and
// inline popups, the source view goes read-only, and the session rejects every
// incremental edit (typing, paste, format, undo).
void muffin::MainWindow::applyEditingLockedTo(DocumentTab* document) const {
  if (!document) {
    return;
  }
  if (document->renderView) {
    document->renderView->setEditingLocked(editingLocked_);
  }
  if (document->editor) {
    document->editor->setReadOnly(editingLocked_);
  }
  document->session.setEditingReadOnly(editingLocked_);
}

void muffin::MainWindow::updateStatus() {
  if (!statusBar_) {
    return;
  }
  if (!tab_->backend->isSourceMode() && !tab_->renderCursorStatus.isEmpty()) {
    statusBar_->setCursorStatus(tab_->renderCursorStatus);
  } else {
    statusBar_->setCursorStatus(QStringLiteral("%1:%2").arg(tab_->cursorLine).arg(tab_->cursorColumn));
  }
  const TextFileFormat& format = tab_->session.fileFormat();
  QString lineEnding;
  if (!format.existingFile && tab_->session.filePath().isEmpty()) {
    lineEnding = QSettings().value(QStringLiteral("editor/defaultLineBreak"), 1).toInt() == 1 ? QStringLiteral("CRLF")
                                                                                              : QStringLiteral("LF");
  } else {
    switch (format.lineEnding) {
    case TextLineEnding::Crlf:
      lineEnding = QStringLiteral("CRLF");
      break;
    case TextLineEnding::Cr:
      lineEnding = QStringLiteral("CR");
      break;
    case TextLineEnding::Lf:
      lineEnding = QStringLiteral("LF");
      break;
    }
  }
  statusBar_->setEncodingLineEnding(
      QStringLiteral("%1%2 | %3")
          .arg(format.encodingName, format.writeBom ? QStringLiteral(" BOM") : QString(), lineEnding));
  // The block-source preview is render-mode only; clear any stale text when editing source.
  if (tab_->backend && tab_->backend->isSourceMode()) {
    statusBar_->setBlockSource(QString(), QString());
  }
}

void muffin::MainWindow::updateCursorStatus(int line, int column) {
  tab_->cursorLine = line;
  tab_->cursorColumn = column;
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
    tab_->outlineDirty = true;
  }

  // No transition at startup (window not shown yet) or when reduced motion is requested.
  if (!isVisible() || reducedMotion() || !sidebarAnimation_) {
    sidebar_->setMinimumWidth(SidebarWidget::kMinimumWidth);
    sidebar_->setMaximumWidth(SidebarWidget::kMaximumWidth);
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
    sidebarTargetWidth_ = qBound(SidebarWidget::kMinimumWidth, sidebarTargetWidth_, SidebarWidget::kMaximumWidth);
    animateSidebarWidth(0, sidebarTargetWidth_);
    if (sidebar_->panel() == SidebarWidget::Panel::Outline) {
      refreshSidebarOutline();
    }
  } else {
    const int currentWidth = qBound(0, sidebar_->width(), SidebarWidget::kMaximumWidth);
    sidebarTargetWidth_ = qBound(SidebarWidget::kMinimumWidth, currentWidth, SidebarWidget::kMaximumWidth);
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
    const bool opening = !action->isChecked();
    action->setChecked(true);
    if (opening)
      updateSidebarMode();
  }
  if (panel == SidebarWidget::Panel::Outline) {
    refreshSidebarOutline();
  } else {
    outlineTimer_->stop();
    sidebar_->clearOutline();
    tab_->outlineDirty = true;
  }
}

void muffin::MainWindow::refreshSidebarDocumentInfo() {
  // Restoration selects its final document before restoring folder expansion.
  // Save/close prompts also visit tabs temporarily; neither should reveal files
  // or change the user's folder state during those intermediate activations.
  if (!sidebar_ || sessionUpdatesPaused_) {
    return;
  }
  sidebar_->setCurrentDocument(tab_->session.displayName(), tab_->session.filePath(),
                               tab_->session.document().isModified());
}

void muffin::MainWindow::refreshSidebarOutline() {
  static const QLoggingCategory category("muffin.perf.detail", QtWarningMsg);
  muffin::diag::ScopedPerfProbe perf("main.outline", category);
  const QAction* sidebarAction = commands_.action(QStringLiteral("view.sidebar"));
  if (!sidebar_ || !tab_->outlineDirty || !sidebarAction || !sidebarAction->isChecked() ||
      sidebar_->panel() != SidebarWidget::Panel::Outline) {
    return;
  }
  sidebar_->setOutline(tab_->session.document().outline());
  tab_->sidebarOutlineRevision = tab_->session.document().outlineRevision();
  tab_->outlineDirty = false;
}

void muffin::MainWindow::openFolder() {
  const QString initialPath =
      sidebarFolderRoot_.isEmpty() ? QFileInfo(tab_->session.filePath()).absolutePath() : sidebarFolderRoot_;
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
  if (tab_->backend->isSourceMode()) {
    syncSourceEditorIfNeeded();
    tab_->editor->setCursorPosition(sourceRange.byteStart);
    tab_->editor->centerCursor();
    tab_->editor->setFocus(Qt::OtherFocusReason);
    return;
  }
  tab_->renderView->scrollToNode(nodeId);
  tab_->renderView->setFocus(Qt::OtherFocusReason);
}

void muffin::MainWindow::updateViewMode() {
  if (!tab_->viewStack || !tab_->renderView || !tab_->editor) {
    return;
  }
  const QAction* action = commands_.action(QStringLiteral("view.source_mode"));
  const bool sourceMode = action && action->isChecked();

  // Switch backend
  if (sourceMode) {
    tab_->backend = std::make_unique<SourceEditorBackend>(tab_->editor);
  } else {
    tab_->backend = std::make_unique<RenderEditorBackend>(tab_->editorController, tab_->session, tab_->renderView);
  }

  if (sourceMode) {
    syncSourceEditorIfNeeded();

    // Render → Source: sync cursor/viewport position
    qsizetype targetOffset = -1;
    if (tab_->editorController.selection().hasCursor()) {
      targetOffset = tab_->editorController.selection().cursorPosition().text.sourceOffset;
    }
    if (targetOffset < 0) {
      const BlockLayout* topBlock = tab_->renderView->blockAtViewportPos(QPointF(0, 0));
      if (topBlock) {
        MarkdownNode* node = tab_->session.document().node(topBlock->nodeId());
        if (node) {
          targetOffset = node->sourceRange().byteStart;
        }
      }
    }
    if (targetOffset >= 0) {
      tab_->editor->setCursorPosition(targetOffset);
      tab_->editor->centerCursor();
    }
  }
  // Snapshot the outgoing page just before the swap, then crossfade it out over the new page.
  QPixmap outgoingSnapshot;
  if (isVisible() && !reducedMotion() && tab_->viewStack->currentWidget() != nullptr) {
    outgoingSnapshot = tab_->viewStack->currentWidget()->grab();
  }
  tab_->viewStack->setCurrentWidget(sourceMode ? static_cast<QWidget*>(tab_->editor)
                                               : static_cast<QWidget*>(tab_->renderView));
  if (!outgoingSnapshot.isNull()) {
    showViewSwitchOverlay(outgoingSnapshot);
  }
  if (sourceModeButton_) {
    sourceModeButton_->setChecked(sourceMode);
  }
  if (!sourceMode) {
    if (tab_->renderViewDirty) {
      tab_->renderView->setDocument(tab_->session.document(), tab_->session.filePath());
      tab_->renderViewDirty = false;
    }
    // Source → Render: scroll to the block at the source cursor position
    tab_->renderView->refreshPendingResources();
    const qsizetype sourcePos = tab_->editor->cursorPosition();
    MarkdownNode* block = tab_->session.document().topLevelBlockAtOffset(sourcePos);
    if (block) {
      tab_->renderView->scrollToNode(block->id());
    }
    tab_->renderView->setFocus(Qt::OtherFocusReason);
  }
  updateStatus();
}

void muffin::MainWindow::showViewSwitchOverlay(const QPixmap& snapshot) {
  if (!tab_->viewStack || snapshot.isNull()) {
    return;
  }
  // A reusable QLabel overlaid on the stack, showing a grab() of the outgoing page and faded out
  // over 120ms. The QGraphicsOpacityEffect is attached to this lightweight label only — never to
  // the live editor views, which would force an offscreen backing store every frame.
  if (!tab_->viewSwitchOverlay) {
    tab_->viewSwitchOverlay = new QLabel(tab_->viewStack);
    tab_->viewSwitchOverlay->setAlignment(Qt::AlignCenter);
    auto* opacity = new QGraphicsOpacityEffect(tab_->viewSwitchOverlay);
    opacity->setOpacity(1.0);
    tab_->viewSwitchOverlay->setGraphicsEffect(opacity);
    tab_->overlayOpacityAnimation = new QPropertyAnimation(opacity, QByteArrayLiteral("opacity"), tab_);
    tab_->overlayOpacityAnimation->setDuration(120);
    tab_->overlayOpacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(tab_->overlayOpacityAnimation, &QPropertyAnimation::finished, tab_,
            [document = tab_] { document->viewSwitchOverlay->hide(); });
  }
  tab_->viewSwitchOverlay->setPixmap(snapshot);
  tab_->viewSwitchOverlay->setGeometry(tab_->viewStack->rect());
  tab_->viewSwitchOverlay->raise();
  tab_->viewSwitchOverlay->show();
  tab_->overlayOpacityAnimation->stop();
  tab_->overlayOpacityAnimation->setStartValue(1.0);
  tab_->overlayOpacityAnimation->setEndValue(0.0);
  tab_->overlayOpacityAnimation->start();
}

void muffin::MainWindow::printDocument() {
  if (tab_->session.filePath().isEmpty()) {
    return;
  }

  QPrintDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  paintDocumentToPrinter(dialog.printer());
}

bool muffin::MainWindow::paintDocumentToPrinter(QPrinter* printer) {
  if (!printer) {
    return false;
  }

  const RenderTheme theme = themeManager_.currentTheme(zoomPercent_, fontSizePx_);
  const QRectF page = printer->pageRect(QPrinter::DevicePixel);

  DocumentLayout layout;
  // Reuse the editor's mermaid render cache in sync mode so diagrams render into
  // the print/PDF output via the same BlockLayout paint path (milestone I-4).
  if (tab_->renderView && tab_->renderView->mermaidRenderCache()) {
    layout.setMermaidRenderCache(tab_->renderView->mermaidRenderCache());
    layout.setMermaidSyncMode(true);
  }
  layout.rebuild(tab_->session.document(), theme, page.width(), tab_->session.filePath());

  QPainter painter(printer);
  if (!painter.isActive()) {
    return false;
  }
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.translate(page.topLeft());

  const qreal pageHeight = page.height();
  const qreal pageWidth = page.width();
  qreal pageTop = 0;

  for (const BlockLayout* block : layout.promotedBlocks()) {
    const QRectF r = block->rect();
    // A block that starts below the current page's bottom edge begins a fresh page
    // (anchored at its top) instead of painting entirely above the page origin.
    if (r.top() >= pageTop + pageHeight) {
      printer->newPage();
      pageTop = r.top();
    }
    // Paint the block clipped to the current page window; a block that straddles the
    // page boundary — or is taller than one page (a large diagram or long code fence) —
    // repeats on subsequent pages with the window sliding down, so its lower part is
    // reprinted instead of being clipped away by the printer and lost.
    for (;;) {
      painter.save();
      painter.setClipRect(QRectF(0.0, 0.0, pageWidth, pageHeight));
      block->paint(painter, theme, pageTop);
      painter.restore();
      if (r.bottom() <= pageTop + pageHeight) {
        break;
      }
      printer->newPage();
      pageTop += pageHeight;
    }
  }

  painter.end();
  return true;
}

int muffin::MainWindow::zoomPercent() const {
  return zoomPercent_;
}

void muffin::MainWindow::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
  for (DocumentTab* document : documentTabs()) {
    document->editor->setZoomPercent(zoomPercent_);
    document->renderView->setZoomPercent(zoomPercent_);
  }
  updateStatus();
}

int muffin::MainWindow::fontSizePx() const {
  return fontSizePx_;
}

void muffin::MainWindow::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
  for (DocumentTab* document : documentTabs()) {
    document->editor->setFontSizePx(fontSizePx_);
    document->renderView->setFontSizePx(fontSizePx_);
  }
  updateStatus();
}

int muffin::MainWindow::contentWidthPx() const {
  return contentWidthPx_;
}

void muffin::MainWindow::setContentWidthPx(int px) {
  if (px == -1) {
    contentWidthPx_ = -1;
  } else if (px > 0) {
    contentWidthPx_ = qBound(640, px, 2400);
  } else {
    contentWidthPx_ = 0;
  }
  for (DocumentTab* document : documentTabs()) {
    document->renderView->setContentWidthPx(contentWidthPx_);
  }
}

void muffin::MainWindow::setStatusBarVisible(bool visible) {
  if (QAction* action = commands_.action(QStringLiteral("view.status_bar"))) {
    action->setChecked(visible);
  }
  statusBar()->setVisible(visible);
}

void muffin::MainWindow::setFocusMode(bool enabled) {
  focusMode_ = enabled;

  for (DocumentTab* document : documentTabs()) {
    document->renderView->setFocusMode(enabled);
  }

  if (QAction* action = commands_.action(QStringLiteral("view.focus"))) {
    action->setChecked(enabled);
  }
}

void muffin::MainWindow::setTypewriterMode(bool enabled) {
  typewriterMode_ = enabled;

  for (DocumentTab* document : documentTabs()) {
    document->renderView->setTypewriterMode(enabled);
    document->renderView->setTypewriterCursorMiddle(
        QSettings().value(QStringLiteral("editor/typewriterCursorMiddle"), true).toBool());
  }

  if (QAction* action = commands_.action(QStringLiteral("view.typewriter"))) {
    action->setChecked(enabled);
  }

  // Apply immediately if just enabled
  if (enabled && tab_->backend) {
    tab_->backend->centerCursor();
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

  const QString themeName =
      settings.value(QStringLiteral("appearance/themeName"), themeManager_.currentThemeName()).toString();
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
  setContentWidthPx(settings.value(QStringLiteral("appearance/contentWidthPx"), 0).toInt());

  restorePersistentActionStates();

  if (QAction* action = commands_.action(QStringLiteral("view.word_wrap")); action && !action->isChecked()) {
    tab_->editor->setWordWrapEnabled(false);
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
  if (tab_->renderView) {
    tab_->renderView->setTypewriterCursorMiddle(
        settings.value(QStringLiteral("editor/typewriterCursorMiddle"), true).toBool());
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

void muffin::MainWindow::saveAppearanceContentWidthPx(int px) const {
  QSettings settings;
  settings.setValue(QStringLiteral("appearance/contentWidthPx"), px);
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
  static const QLoggingCategory themePerf("muffin.perf.detail", QtWarningMsg);
  muffin::diag::ScopedPerfProbe perf("theme.apply", themePerf);
  const RenderTheme theme = [&] {
    muffin::diag::ScopedPerfProbe snapshotPerf("theme.snapshot", themePerf);
    return themeManager_.currentTheme(zoomPercent_, fontSizePx_);
  }();
  const ThemeDefinition def = themeManager_.definition(name);
  for (DocumentTab* document : documentTabs()) {
    document->openConfirmation->setTheme(theme);
    if (document != tab_) {
      document->pendingTheme = theme;
      continue;
    }
    document->pendingTheme.reset();
    muffin::diag::ScopedPerfProbe viewsPerf("theme.activeViews", themePerf);
    document->renderView->setTheme(theme);
    document->editor->setTheme(theme);
  }
  muffin::diag::ScopedPerfProbe chromePerf("theme.chrome", themePerf);
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
    statusBar_->applyThemeColors(theme.backgroundColor(), theme.textColor(), theme.mutedTextColor(), def.colors.border);
  }

  applyNativeTitleBarDarkMode(def.colors.isDark);
}
