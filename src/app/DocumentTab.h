#pragma once

#include "app/DraftRecovery.h"
#include "app/EditorBackend.h"
#include "app/RenderCommandFacade.h"
#include "document/DocumentSession.h"
#include "editor/EditorController.h"
#include "editor/DocumentSearch.h"
#include "io/FileController.h"
#include "theme/RenderTheme.h"
#include "export/ExportPreset.h"

#include <QWidget>
#include <QPointer>
#include <QVariantMap>
#include <memory>
#include <functional>

class QLabel;
class QPropertyAnimation;
class QStackedWidget;
class QSplitter;
class QToolButton;
class QTimer;
class QAction;

namespace muffin {

class EditorView;
class FindBarWidget;
class FileOpenConfirmation;
class SourceEditorWidget;

enum class DocumentViewMode { Rendered, Source, Split };

struct ExportRecord {
  ExportPreset preset;
  QString target;
  RenderTheme theme;
  QString themeCss;
};

// A tab owns the entire editing lifetime, including undo stacks and both viewports.
// Switching documents never rebinds a view or replaces another document's buffer.
class DocumentTab final : public QWidget {
  Q_OBJECT
public:
  explicit DocumentTab(QWidget* parent = nullptr);
  ~DocumentTab() override;
  void ensureRenderViewCurrent();
  bool refreshPreviewAfterLocalEdit();
  DocumentViewMode viewMode() const { return viewMode_; }
  bool hasSourcePane() const { return viewMode_ != DocumentViewMode::Rendered; }
  bool hasRenderPane() const { return viewMode_ != DocumentViewMode::Source; }
  DocumentViewMode singleViewMode() const { return singleViewMode_; }
  void setViewMode(DocumentViewMode mode);
  void restoreViewMode(const QVariantMap& state);
  bool previewActive() const { return viewMode_ == DocumentViewMode::Split && previewActive_; }
  void focusEditor();
  void synchronizeViewports(bool fromSource);
  bool synchronizedScrolling() const;
  void setSynchronizedScrolling(bool enabled);
  QList<int> splitSizes() const;
  Qt::Orientation splitOrientation() const;
  bool previewFirst() const;
  void setSplitLayout(Qt::Orientation orientation, bool previewFirst);
  void setSplitTheme(const RenderTheme& theme);
  void setFocusMode(bool enabled);
  void setTypewriterMode(bool enabled, bool cursorMiddle);
  // The focus target is separate from the layout: preview commands can copy a
  // rendered selection without making the source buffer read-only.
  std::function<void()> commandTargetChanged;

  // Replaced when the page moves between windows; deleting this context also
  // cancels queued callbacks that still refer to the previous window.
  QObject* windowConnections = new QObject(this);
  DocumentSession session;
  FileController fileController;
  EditorController editorController;
  std::unique_ptr<EditorBackend> backend;
  RenderCommandFacade renderCommands;
  QString draftKey = DraftRecovery::createDraftKey();
  QStackedWidget* contentStack = nullptr;
  FileOpenConfirmation* openConfirmation = nullptr;
  QString pendingOpenPath;
  bool pendingImportCopy = false;
  QPointer<DocumentTab> openReturnTab;
  std::optional<QString> pendingRecoveredDraftText;
  QString pendingRecoveredDraftKey;
  QStackedWidget* viewStack = nullptr;
  QSplitter* viewSplitter = nullptr;
  EditorView* renderView = nullptr;
  SourceEditorWidget* editor = nullptr;
  FindBarWidget* findBar = nullptr;
  QTimer* autoSaveTimer = nullptr;
  QTimer* draftTimer = nullptr;
  QLabel* viewSwitchOverlay = nullptr;
  QPropertyAnimation* overlayOpacityAnimation = nullptr;
  quint64 lastDraftSnapshotRevision = 0;
  int cursorLine = 1;
  int cursorColumn = 1;
  QString renderCursorStatus;
  // External changes wait with their tab until it becomes active. Keeping the
  // path also prevents a deferred question from applying to a replacement file.
  QString pendingExternalFileChangePath;
  bool lastFindRegularExpression = false;
  bool lastFindCaseSensitive = false;
  QString lastFindText;
  std::optional<QByteArray> pendingSearchFingerprint;
  qsizetype pendingSearchOffset = 0;
  qsizetype pendingSearchLength = 0;
  bool searchSourceFallback = false;
  qsizetype lastFindOffset = -1;
  qsizetype lastFindLength = 0;
  quint64 findRevision = 0;
  bool findCacheValid = false;
  SearchResults findCache;
  DocumentSearch::Cancellation findCancellation;
  bool renderViewDirty = false;
  bool renderViewBatchDirty = false;
  bool pinned = false;
  // Only successful exports are repeatable; records follow their document tab.
  std::optional<ExportRecord> lastExport;
  // 锁定编辑（独立标签页作用范围）：每个页签自己记住状态，切换页签互不影响。
  bool editingLocked = false;
  // Preserve the serialized state until both parsing and first activation have
  // completed. Saving/closing an unopened background page must not replace it
  // with the temporary empty layout or startup caret.
  std::optional<QVariantMap> pendingViewState;
  // Keep only the latest snapshot until this document becomes active.
  std::optional<RenderTheme> pendingTheme;
  bool wordCountDirty = true;
  bool outlineDirty = true;
  quint64 sidebarOutlineRevision = 0;

private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;
  void retranslateSplitUi();
  void updateCommandTarget(bool preview);
  void applyWritingModes();
  void scheduleViewportSynchronization();
  QWidget* splitPage_ = nullptr;
  QWidget* sourcePane_ = nullptr;
  QWidget* previewPane_ = nullptr;
  QToolButton* synchronizeButton_ = nullptr;
  QToolButton* splitLayoutButton_ = nullptr;
  QAction* sideBySideAction_ = nullptr;
  QAction* topAndBottomAction_ = nullptr;
  QAction* swapPanesAction_ = nullptr;
  DocumentViewMode viewMode_ = DocumentViewMode::Rendered;
  DocumentViewMode singleViewMode_ = DocumentViewMode::Rendered;
  bool previewActive_ = false;
  bool synchronizing_ = false;
  bool synchronizationQueued_ = false;
  bool scrollFromSource_ = true;
  bool focusMode_ = false;
  bool typewriterMode_ = false;
  bool typewriterCursorMiddle_ = true;
  QList<int> splitSizes_{1, 1};
};

} // namespace muffin
