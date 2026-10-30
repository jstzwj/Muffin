#pragma once

#include "app/DraftRecovery.h"
#include "app/EditorBackend.h"
#include "app/RenderCommandFacade.h"
#include "document/DocumentSession.h"
#include "editor/EditorController.h"
#include "io/FileController.h"
#include "theme/RenderTheme.h"

#include <QWidget>
#include <QPointer>
#include <QVariantMap>
#include <memory>

class QLabel;
class QPropertyAnimation;
class QStackedWidget;
class QTimer;

namespace muffin {

class EditorView;
class FindBarWidget;
class FileOpenConfirmation;
class SourceEditorWidget;

// A tab owns the entire editing lifetime, including undo stacks and both viewports.
// Switching documents never rebinds a view or replaces another document's buffer.
class DocumentTab final : public QWidget {
public:
  explicit DocumentTab(QWidget* parent = nullptr);
  ~DocumentTab() override;

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
  QPointer<DocumentTab> openReturnTab;
  std::optional<QString> pendingRecoveredDraftText;
  QString pendingRecoveredDraftKey;
  QStackedWidget* viewStack = nullptr;
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
  bool renderViewDirty = false;
  bool pinned = false;
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
};

} // namespace muffin
