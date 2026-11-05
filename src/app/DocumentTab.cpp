#include "app/DocumentTab.h"

#include "app/RenderEditorBackend.h"
#include "app/SourceEditorBackend.h"
#include "app/FileOpenConfirmation.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"
#include "editor/VirtualSourceEdit.h"

#include <QApplication>
#include <QActionGroup>
#include <QMenu>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace {
QIcon splitHeaderIcon(const char* path, const QColor& ink) {
  QFile file(QString::fromLatin1(path));
  if (!file.open(QIODevice::ReadOnly))
    return {};
  auto svg = file.readAll();
  svg.replace("#000000", ink.name(QColor::HexRgb).toUtf8());
  QSvgRenderer renderer(svg);
  QIcon result;
  for (const int size : {16, 20, 24, 32, 40, 48, 64}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    result.addPixmap(pixmap);
  }
  return result;
}

QColor headerBlend(const QColor& background, const QColor& ink, qreal amount) {
  return QColor::fromRgbF(background.redF() * (1 - amount) + ink.redF() * amount,
                          background.greenF() * (1 - amount) + ink.greenF() * amount,
                          background.blueF() * (1 - amount) + ink.blueF() * amount);
}
} // namespace

muffin::DocumentTab::DocumentTab(QWidget* parent)
    : QWidget(parent), renderCommands(editorController, [this] { return hasSourcePane(); }) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  contentStack = new QStackedWidget(this);
  layout->addWidget(contentStack);
  auto* editorPage = new QWidget(contentStack);
  contentStack->addWidget(editorPage);
  openConfirmation = new FileOpenConfirmation(contentStack);
  contentStack->addWidget(openConfirmation);
  layout = new QVBoxLayout(editorPage);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  viewStack = new QStackedWidget(this);
  renderView = new EditorView(viewStack);
  editor = new SourceEditorWidget(viewStack);
  editor->bindSession(&session);
  viewStack->addWidget(renderView);
  viewStack->addWidget(editor);
  splitPage_ = new QWidget(viewStack);
  auto* splitLayout = new QVBoxLayout(splitPage_);
  splitLayout->setContentsMargins(0, 0, 0, 0);
  splitLayout->setSpacing(0);
  viewSplitter = new QSplitter(Qt::Horizontal, splitPage_);
  viewSplitter->setObjectName(QStringLiteral("DocumentViewSplitter"));
  viewSplitter->setChildrenCollapsible(false);
  viewSplitter->setHandleWidth(5);
  const auto makePane = [this](const QString& paneName, const QString& headerName, const QString& iconName,
                               const QString& labelName, const QString& title) {
    auto* pane = new QWidget(viewSplitter);
    pane->setObjectName(paneName);
    auto* paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    auto* header = new QWidget(pane);
    header->setObjectName(headerName);
    header->setFixedHeight(32);
    auto font = header->font();
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    header->setFont(font);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 10, 0);
    headerLayout->setSpacing(8);
    auto* icon = new QLabel(header);
    icon->setObjectName(iconName);
    icon->setFixedSize(16, 16);
    headerLayout->addWidget(icon);
    auto* label = new QLabel(title, header);
    label->setObjectName(labelName);
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(label, 1);
    paneLayout->addWidget(header);
    viewSplitter->addWidget(pane);
    return pane;
  };
  sourcePane_ = makePane(QStringLiteral("SplitSourcePane"), QStringLiteral("SplitSourceHeader"),
                         QStringLiteral("SplitSourceIcon"), QStringLiteral("SplitSourceLabel"), tr("Markdown source"));
  previewPane_ =
      makePane(QStringLiteral("SplitPreviewPane"), QStringLiteral("SplitPreviewHeader"),
               QStringLiteral("SplitPreviewIcon"), QStringLiteral("SplitPreviewLabel"), tr("Read-only preview"));
  auto* previewHeader = previewPane_->findChild<QWidget*>(QStringLiteral("SplitPreviewHeader"));
  synchronizeButton_ = new QToolButton(previewHeader);
  synchronizeButton_->setObjectName(QStringLiteral("SplitSynchronizeScrolling"));
  synchronizeButton_->setText(tr("Synchronized scrolling"));
  synchronizeButton_->setToolTip(synchronizeButton_->text());
  synchronizeButton_->setAccessibleName(synchronizeButton_->text());
  synchronizeButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  synchronizeButton_->setIconSize(QSize(16, 16));
  synchronizeButton_->setFixedSize(26, 26);
  synchronizeButton_->setCursor(Qt::PointingHandCursor);
  synchronizeButton_->setCheckable(true);
  synchronizeButton_->setChecked(true);
  synchronizeButton_->setAutoRaise(true);
  synchronizeButton_->setFocusPolicy(Qt::NoFocus);
  previewHeader->layout()->addWidget(synchronizeButton_);
  splitLayoutButton_ = new QToolButton(previewHeader);
  splitLayoutButton_->setObjectName(QStringLiteral("SplitLayoutOptions"));
  splitLayoutButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  splitLayoutButton_->setIconSize(QSize(16, 16));
  splitLayoutButton_->setFixedSize(26, 26);
  splitLayoutButton_->setAutoRaise(true);
  splitLayoutButton_->setCursor(Qt::PointingHandCursor);
  splitLayoutButton_->setFocusPolicy(Qt::NoFocus);
  splitLayoutButton_->setPopupMode(QToolButton::InstantPopup);
  auto* layoutMenu = new QMenu(splitLayoutButton_);
  layoutMenu->setObjectName(QStringLiteral("SplitLayoutMenu"));
  auto* orientations = new QActionGroup(layoutMenu);
  sideBySideAction_ = layoutMenu->addAction(tr("Side by side"));
  topAndBottomAction_ = layoutMenu->addAction(tr("Top and bottom"));
  sideBySideAction_->setObjectName(QStringLiteral("SplitSideBySide"));
  topAndBottomAction_->setObjectName(QStringLiteral("SplitTopAndBottom"));
  for (auto* action : {sideBySideAction_, topAndBottomAction_}) {
    action->setCheckable(true);
    orientations->addAction(action);
  }
  sideBySideAction_->setChecked(true);
  layoutMenu->addSeparator();
  swapPanesAction_ = layoutMenu->addAction(tr("Swap source and preview"));
  swapPanesAction_->setObjectName(QStringLiteral("SplitSwapPanes"));
  connect(sideBySideAction_, &QAction::triggered, this, [this] { setSplitLayout(Qt::Horizontal, previewFirst()); });
  connect(topAndBottomAction_, &QAction::triggered, this, [this] { setSplitLayout(Qt::Vertical, previewFirst()); });
  connect(swapPanesAction_, &QAction::triggered, this, [this] { setSplitLayout(splitOrientation(), !previewFirst()); });
  splitLayoutButton_->setMenu(layoutMenu);
  previewHeader->layout()->addWidget(splitLayoutButton_);
  retranslateSplitUi();
  viewSplitter->setStretchFactor(0, 1);
  viewSplitter->setStretchFactor(1, 1);
  splitLayout->addWidget(viewSplitter, 1);
  viewStack->addWidget(splitPage_);
  splitPage_->setFocusProxy(editor);
  backend = std::make_unique<RenderEditorBackend>(editorController, session, renderView);
  findBar = new FindBarWidget(this);
  findBar->hide();
  layout->addWidget(findBar);
  layout->addWidget(viewStack, 1);
  autoSaveTimer = new QTimer(this);
  autoSaveTimer->setSingleShot(true);
  autoSaveTimer->setInterval(1500);
  draftTimer = new QTimer(this);
  draftTimer->setSingleShot(true);
  draftTimer->setInterval(3000);
  editorController.attach(&session, renderView);
  connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* current) {
    if (viewMode_ != DocumentViewMode::Split || !current)
      return;
    if (current == renderView || renderView->isAncestorOf(current))
      updateCommandTarget(true);
    else if (current == editor || editor->isAncestorOf(current))
      updateCommandTarget(false);
  });
  connect(renderView, &EditorView::layoutBusyChanged, this, [this](bool busy) {
    // Cooperative preview layout must never suspend typing in the source pane.
    session.setPresentationBusy(viewMode_ == DocumentViewMode::Rendered && busy);
  });
  connect(viewSplitter, &QSplitter::splitterMoved, this, [this] { splitSizes_ = viewSplitter->sizes(); });
  auto* sourceView = editor->findChild<VirtualSourceEdit*>();
  for (auto* target : {static_cast<QWidget*>(sourceView), static_cast<QWidget*>(renderView), sourceView->viewport(),
                       renderView->viewport(), static_cast<QWidget*>(sourceView->verticalScrollBar()),
                       static_cast<QWidget*>(renderView->verticalScrollBar())})
    target->installEventFilter(this);
  connect(sourceView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
    if (scrollFromSource_)
      scheduleViewportSynchronization();
  });
  connect(renderView, &EditorView::viewportPositionChanged, this, [this] {
    if (!scrollFromSource_)
      scheduleViewportSynchronization();
  });
  connect(sourceView, &VirtualSourceEdit::editApplied, this, &DocumentTab::scheduleViewportSynchronization);
  connect(&session, &DocumentSession::sourceTextChanged, renderView, &EditorView::mapPreviewReadingPosition);
  renderView->setReadingPositionResolver(
      [this](qsizetype offset) { return editorController.cursorForSourceOffset(offset); });
  connect(synchronizeButton_, &QToolButton::toggled, this, [this](bool enabled) {
    if (enabled)
      synchronizeViewports(scrollFromSource_);
  });
  connect(renderView, &EditorView::layoutReady, this, &DocumentTab::scheduleViewportSynchronization);
}

muffin::DocumentTab::~DocumentTab() {
  commandTargetChanged = {};
  disconnect(qApp, nullptr, this, nullptr);
  if (findCancellation)
    findCancellation->store(true);
  // Views refer to the session/controller; destroy them while those members are alive.
  delete windowConnections;
  disconnect(&editorController, nullptr, this, nullptr);
  disconnect(&session, nullptr, this, nullptr);
  editorController.detach();
  backend.reset();
  delete findBar;
  delete viewStack;
  delete contentStack;
}

void muffin::DocumentTab::ensureRenderViewCurrent() {
  if (!std::exchange(renderViewDirty, false))
    return;
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  const bool preview = viewMode_ == DocumentViewMode::Split;
  // Full parses retire all node pointers. The last presented frame carries a
  // source anchor that remains safe to resolve against the replacement AST.
  const auto position = preview ? (renderView->isLayoutPending() ? renderView->viewportPosition()
                                                                 : renderView->lastPresentedViewportPosition())
                                : ViewportPosition{};
  renderView->setDocument(session.document(), session.filePath(), std::exchange(renderViewBatchDirty, false));
  if (preview)
    renderView->restoreViewportPosition(position, editorController.cursorForSourceOffset(position.sourceOffset));
}

bool muffin::DocumentTab::refreshPreviewAfterLocalEdit() {
  if (viewMode_ != DocumentViewMode::Split || renderViewDirty || !session.lastParseWasLocalEdit() ||
      session.lastChangeWasBatch())
    return false;
  {
    // Layout can clamp and re-pin scrollbars while splicing the changed range.
    // Only the completed view may drive the other pane.
    const QScopedValueRollback<bool> guard(synchronizing_, true);
    if (!renderView->refreshTopLevelRange(session.lastLocalTopLevelRangeChange(), session.document(), true))
      return false;
  }
  scheduleViewportSynchronization();
  return true;
}

void muffin::DocumentTab::scheduleViewportSynchronization() {
  if (synchronizing_ || synchronizationQueued_ || viewMode_ != DocumentViewMode::Split)
    return;
  synchronizationQueued_ = true;
  QTimer::singleShot(0, this, [this] {
    synchronizationQueued_ = false;
    synchronizeViewports(scrollFromSource_);
  });
}

void muffin::DocumentTab::updateCommandTarget(bool preview) {
  previewActive_ = viewMode_ == DocumentViewMode::Split && preview;
  scrollFromSource_ = !previewActive_;
  const bool source = hasSourcePane() && !previewActive_;
  if (backend && backend->isSourceMode() == source)
    return;
  if (source)
    backend = std::make_unique<SourceEditorBackend>(editor);
  else
    backend = std::make_unique<RenderEditorBackend>(editorController, session, renderView);
  if (commandTargetChanged)
    commandTargetChanged();
}

void muffin::DocumentTab::setViewMode(DocumentViewMode mode) {
  if (mode == viewMode_)
    return;
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  if (viewMode_ == DocumentViewMode::Split) {
    splitSizes_ = viewSplitter->sizes();
    viewStack->addWidget(editor);
    viewStack->addWidget(renderView);
  } else {
    singleViewMode_ = viewMode_;
  }
  viewMode_ = mode;
  renderView->setLivePreview(mode == DocumentViewMode::Split);
  previewActive_ = false;
  if (mode == DocumentViewMode::Split) {
    viewStack->removeWidget(editor);
    viewStack->removeWidget(renderView);
    static_cast<QVBoxLayout*>(sourcePane_->layout())->addWidget(editor, 1);
    static_cast<QVBoxLayout*>(previewPane_->layout())->addWidget(renderView, 1);
    editor->show();
    renderView->show();
    viewSplitter->setSizes(splitSizes_);
    viewStack->setCurrentWidget(splitPage_);
  } else {
    singleViewMode_ = mode;
    viewStack->setCurrentWidget(mode == DocumentViewMode::Source ? static_cast<QWidget*>(editor)
                                                                 : static_cast<QWidget*>(renderView));
  }
  renderView->setEditingLocked(session.editingReadOnly() || mode == DocumentViewMode::Split);
  session.setPresentationBusy(mode == DocumentViewMode::Rendered && renderView->isLayoutPending());
  updateCommandTarget(false);
  applyWritingModes();
}

void muffin::DocumentTab::restoreViewMode(const QVariantMap& state) {
  const auto mode = state.value(QStringLiteral("splitView")).toBool()    ? DocumentViewMode::Split
                    : state.value(QStringLiteral("sourceMode")).toBool() ? DocumentViewMode::Source
                                                                         : DocumentViewMode::Rendered;
  setSplitLayout(state.value(QStringLiteral("splitVertical")).toBool() ? Qt::Vertical : Qt::Horizontal,
                 state.value(QStringLiteral("splitPreviewFirst")).toBool());
  setViewMode(mode);
  const auto sizes = state.value(QStringLiteral("splitSizes")).toList();
  if (sizes.size() == 2 && sizes[0].toInt() > 0 && sizes[1].toInt() > 0)
    splitSizes_ = {sizes[0].toInt(), sizes[1].toInt()};
  singleViewMode_ =
      state.value(QStringLiteral("splitReturnSource")).toBool() ? DocumentViewMode::Source : DocumentViewMode::Rendered;
  if (mode == DocumentViewMode::Split)
    viewSplitter->setSizes(splitSizes_);
  setSynchronizedScrolling(state.value(QStringLiteral("synchronizedScrolling"), true).toBool());
}

void muffin::DocumentTab::focusEditor() {
  updateCommandTarget(false);
  (hasSourcePane() ? static_cast<QWidget*>(editor) : static_cast<QWidget*>(renderView))->setFocus(Qt::OtherFocusReason);
}

bool muffin::DocumentTab::synchronizedScrolling() const {
  return synchronizeButton_->isChecked();
}

void muffin::DocumentTab::setFocusMode(bool enabled) {
  focusMode_ = enabled;
  applyWritingModes();
}

void muffin::DocumentTab::setTypewriterMode(bool enabled, bool cursorMiddle) {
  typewriterMode_ = enabled;
  typewriterCursorMiddle_ = cursorMiddle;
  applyWritingModes();
  if (hasSourcePane() && !previewActive_)
    scheduleViewportSynchronization();
}

void muffin::DocumentTab::applyWritingModes() {
  // Writing aids belong to the editable pane. A split preview has no writing
  // caret: its reading position is controlled only by explicit navigation and
  // the synchronization policy, never by a stale rendered selection.
  {
    const QScopedValueRollback<bool> guard(synchronizing_, true);
    const bool rendered = viewMode_ == DocumentViewMode::Rendered;
    renderView->setFocusMode(focusMode_ && rendered);
    if (!rendered)
      renderView->setTypewriterMode(false);
    renderView->setTypewriterCursorMiddle(typewriterCursorMiddle_);
    renderView->setTypewriterMode(typewriterMode_ && rendered);
    editor->setFocusMode(focusMode_ && !rendered);
    editor->setTypewriterMode(typewriterMode_ && !rendered, typewriterCursorMiddle_);
  }
}

void muffin::DocumentTab::setSynchronizedScrolling(bool enabled) {
  synchronizeButton_->setChecked(enabled);
}

QList<int> muffin::DocumentTab::splitSizes() const {
  return viewMode_ == DocumentViewMode::Split ? viewSplitter->sizes() : splitSizes_;
}

Qt::Orientation muffin::DocumentTab::splitOrientation() const {
  return viewSplitter->orientation();
}

bool muffin::DocumentTab::previewFirst() const {
  return viewSplitter->indexOf(previewPane_) == 0;
}

void muffin::DocumentTab::setSplitLayout(Qt::Orientation orientation, bool previewFirst) {
  if (orientation == splitOrientation() && previewFirst == this->previewFirst())
    return;
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  const auto sourcePosition = editor->viewportPosition();
  const auto previewPosition = renderView->viewportPosition();
  const auto oldSizes = splitSizes();
  const bool swap = previewFirst != this->previewFirst();
  viewSplitter->setOrientation(orientation);
  viewSplitter->insertWidget(0, previewFirst ? previewPane_ : sourcePane_);
  // Ratios belong to panes, so swapping keeps each pane's relative size.
  splitSizes_ = swap ? QList<int>{oldSizes[1], oldSizes[0]} : oldSizes;
  viewSplitter->setSizes(splitSizes_);
  sideBySideAction_->setChecked(orientation == Qt::Horizontal);
  topAndBottomAction_->setChecked(orientation == Qt::Vertical);
  editor->restoreViewportPosition(sourcePosition);
  renderView->restoreViewportPosition(previewPosition, {});
}

void muffin::DocumentTab::synchronizeViewports(bool fromSource) {
  if (viewMode_ != DocumentViewMode::Split || !synchronizedScrolling() || synchronizing_ || !isVisible() ||
      session.isOpenInProgress() || renderViewDirty)
    return;
  // Scrollbar callbacks follow the last input pane; explicit navigation can
  // choose a new driver. Preserve the other pane's horizontal position/caret.
  if (!fromSource && renderView->isLayoutPending())
    return;
  const QScopedValueRollback<bool> guard(synchronizing_, true);
  scrollFromSource_ = fromSource;
  auto position = fromSource ? editor->viewportPosition() : renderView->viewportPosition();
  if (fromSource) {
    renderView->restoreViewportPosition(position, editorController.cursorForSourceOffset(position.sourceOffset));
  } else {
    position.horizontal = editor->viewportPosition().horizontal;
    editor->restoreViewportPosition(position);
  }
}

bool muffin::DocumentTab::eventFilter(QObject* watched, QEvent* event) {
  if (viewMode_ == DocumentViewMode::Split &&
      (event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress ||
       event->type() == QEvent::KeyPress)) {
    auto* widget = qobject_cast<QWidget*>(watched);
    scrollFromSource_ = widget && editor->isAncestorOf(widget);
  }
  return QWidget::eventFilter(watched, event);
}

void muffin::DocumentTab::retranslateSplitUi() {
  splitPage_->findChild<QLabel*>(QStringLiteral("SplitSourceLabel"))->setText(tr("Markdown source"));
  splitPage_->findChild<QLabel*>(QStringLiteral("SplitPreviewLabel"))->setText(tr("Read-only preview"));
  synchronizeButton_->setText(tr("Synchronized scrolling"));
  synchronizeButton_->setToolTip(synchronizeButton_->text());
  synchronizeButton_->setAccessibleName(synchronizeButton_->text());
  splitLayoutButton_->setText(tr("Split layout"));
  splitLayoutButton_->setToolTip(splitLayoutButton_->text());
  splitLayoutButton_->setAccessibleName(splitLayoutButton_->text());
  sideBySideAction_->setText(tr("Side by side"));
  topAndBottomAction_->setText(tr("Top and bottom"));
  swapPanesAction_->setText(tr("Swap source and preview"));
}

void muffin::DocumentTab::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::LanguageChange)
    retranslateSplitUi();
}

void muffin::DocumentTab::setSplitTheme(const RenderTheme& theme) {
  auto colors = splitPage_->palette();
  colors.setColor(QPalette::Window, theme.backgroundColor());
  colors.setColor(QPalette::WindowText, theme.mutedTextColor());
  colors.setColor(QPalette::ButtonText, theme.textColor());
  colors.setColor(QPalette::Button, theme.backgroundColor());
  splitPage_->setPalette(colors);
  splitPage_->setAutoFillBackground(true);
  const auto header = headerBlend(theme.backgroundColor(), theme.textColor(), 0.025);
  const auto border = headerBlend(theme.backgroundColor(), theme.textColor(), 0.12);
  const auto headerStyle =
      QStringLiteral(
          "QWidget#SplitSourceHeader, QWidget#SplitPreviewHeader { background: %1; border-bottom: 1px solid %2; }"
          "QLabel { color: %3; background: transparent; border: none; }")
          .arg(header.name(QColor::HexRgb), border.name(QColor::HexRgb), theme.mutedTextColor().name(QColor::HexRgb));
  for (const auto* name : {"SplitSourceHeader", "SplitPreviewHeader"})
    splitPage_->findChild<QWidget*>(QLatin1String(name))->setStyleSheet(headerStyle);
  const auto setHeaderIcon = [this, &theme](const char* label, const char* resource) {
    splitPage_->findChild<QLabel*>(QLatin1String(label))
        ->setPixmap(splitHeaderIcon(resource, theme.mutedTextColor()).pixmap(QSize(16, 16), devicePixelRatioF()));
  };
  setHeaderIcon("SplitSourceIcon", ":/icons/statusbar/source-code.svg");
  setHeaderIcon("SplitPreviewIcon", ":/icons/ui/read-only.svg");
  QIcon syncIcon;
  const auto linked = splitHeaderIcon(":/icons/ui/scroll-linked.svg", theme.linkColor());
  const auto unlinked = splitHeaderIcon(":/icons/ui/scroll-unlinked.svg", theme.mutedTextColor());
  for (const int size : {16, 20, 24, 32, 40, 48, 64}) {
    syncIcon.addPixmap(linked.pixmap(size, size), QIcon::Normal, QIcon::On);
    syncIcon.addPixmap(unlinked.pixmap(size, size), QIcon::Normal, QIcon::Off);
  }
  synchronizeButton_->setIcon(syncIcon);
  splitLayoutButton_->setIcon(splitHeaderIcon(":/icons/ui/split-layout.svg", theme.mutedTextColor()));
  const auto divider = headerBlend(theme.backgroundColor(), theme.mutedTextColor(), 80.0 / 255.0);
  viewSplitter->setStyleSheet(
      QStringLiteral(
          "QSplitter#DocumentViewSplitter::handle { background: %1; }"
          "QSplitter#DocumentViewSplitter::handle:horizontal { width: 5px; border-left: 2px solid %2; border-right: 2px solid %2; }"
          "QSplitter#DocumentViewSplitter::handle:vertical { height: 5px; border-top: 2px solid %2; border-bottom: 2px solid %2; }"
          "QSplitter#DocumentViewSplitter::handle:hover { background: %3; }")
          .arg(divider.name(QColor::HexRgb), theme.backgroundColor().name(QColor::HexRgb),
               theme.linkColor().name(QColor::HexRgb)));
  auto hover = theme.textColor();
  hover.setAlpha(16);
  auto checked = theme.linkColor();
  checked.setAlpha(22);
  synchronizeButton_->setStyleSheet(
      QStringLiteral(
          "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 0; }"
          "QToolButton:hover { background: %1; } QToolButton:checked { background: %2; }"
          "QToolButton:checked:hover { border-color: %3; }")
          .arg(hover.name(QColor::HexArgb), checked.name(QColor::HexArgb), theme.linkColor().name(QColor::HexRgb)));
  splitLayoutButton_->setStyleSheet(synchronizeButton_->styleSheet() +
                                    QStringLiteral("QToolButton::menu-indicator { image:none; width:0; height:0; }"));
}
