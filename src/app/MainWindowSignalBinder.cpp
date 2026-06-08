#include "app/MainWindowSignalBinder.h"

#include "app/LanguageManager.h"
#include "app/MainWindow.h"
#include "app/MainWindowActionBinder.h"
#include "app/SidebarWidget.h"
#include "editor/EditorView.h"
#include "editor/FindBarWidget.h"
#include "editor/SourceEditorWidget.h"

#include <QAction>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMenu>
#include <QToolButton>

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

void muffin::MainWindowSignalBinder::connectEditorSignals(MainWindow& window) {
  QObject::connect(window.editor_, &SourceEditorWidget::textEdited, &window.session_, &DocumentSession::updateFromEditor);
  QObject::connect(window.editor_, &SourceEditorWidget::cursorPositionChanged, &window, [&window](int line, int column) {
    window.updateCursorStatus(line, column);
    if (window.typewriterMode_ && window.backend_->isSourceMode()) {
      window.backend_->centerCursor();
    }
  });
  QObject::connect(window.editor_, &SourceEditorWidget::cursorPositionChanged, &window, [&window](int, int) {
    MainWindowActionBinder::updateEditActions(window);
  });
}

void muffin::MainWindowSignalBinder::connectRenderSignals(MainWindow& window) {
  QObject::connect(&window.editorController_, &EditorController::cursorChanged, &window, [&window](const HitTestResult& hit) {
    window.updateRenderCursorStatus(hit);
    if (window.typewriterMode_ && !window.backend_->isSourceMode()) {
      window.renderView_->scrollToCursorCenteredAnimated();
    }
  });
  QObject::connect(window.renderView_, &EditorView::codeLanguageCommitted, &window, [&window](NodeId codeId, const QString& language) {
    if (window.backend_->isSourceMode()) {
      return;
    }
    window.renderCommands_.setCodeLanguageFor(codeId, language);
  });
  QObject::connect(window.renderView_, &EditorView::tableResizeRequested, &window, [&window](int rows, int columns) {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.resizeCurrentTable(rows, columns);
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableColumnAlignmentRequested, &window, [&window](TableAlignment alignment) {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.setCurrentColumnAlignment(alignment);
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableDeleteRequested, &window, [&window] {
    if (!window.backend_->isSourceMode()) {
      window.renderCommands_.deleteCurrentTable();
    }
  });
  QObject::connect(window.renderView_, &EditorView::tableMoreActionsRequested, &window, [&window](QPoint globalPos) {
    if (window.backend_->isSourceMode()) {
      return;
    }
    MainWindowActionBinder::updateTableActions(window);
    MainWindowActionBinder::updateParagraphActions(window);
    QMenu menu(&window);
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
      if (QAction* action = window.commands_.action(id)) {
        menu.addAction(action);
      }
    }
    menu.exec(globalPos);
  });
}

void muffin::MainWindowSignalBinder::connectSessionSignals(MainWindow& window) {
  QObject::connect(&window.session_, &DocumentSession::documentTextChanged, &window, [&window](const QString& text) {
    PerfTimer perf("main.documentTextChanged.consumer");
    if (window.backend_->isSourceMode()) {
      window.editor_->setText(text);
      window.sourceEditorDirty_ = false;
      return;
    }
    window.sourceEditorDirty_ = true;
  });
  QObject::connect(&window.session_, &DocumentSession::documentLocallyEdited, &window, [&window](qsizetype, qsizetype, const QString&) {
    PerfTimer perf("main.documentLocallyEdited.consumer");
    window.sourceEditorDirty_ = true;
  });
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, &MainWindow::updateTitle);
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, [&window] {
    MainWindowActionBinder::updateFileActions(window);
  });
  QObject::connect(&window.session_, &DocumentSession::filePathChanged, &window, &MainWindow::refreshSidebarDocumentInfo);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::updateTitle);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::updateStatus);
  QObject::connect(&window.session_, &DocumentSession::modifiedChanged, &window, &MainWindow::refreshSidebarDocumentInfo);
  QObject::connect(&window.session_, &DocumentSession::parsed, &window, [&window] {
    PerfTimer perf("main.parsed.consumer");
    if (!window.session_.lastParseWasLocalEdit()) {
      window.renderView_->setDocument(window.session_.document());
    }
    window.scheduleWordCountUpdate();
    window.updateStatus();
    window.refreshSidebarOutline();
  });
}

void muffin::MainWindowSignalBinder::connectApplicationSignals(MainWindow& window) {
  QObject::connect(&window.editorController_, &EditorController::stateChanged, &window, [&window] {
    window.updateStatus();
    MainWindowActionBinder::updateContextActions(window);
  });
  QObject::connect(&window.themeManager_, &ThemeManager::themeChanged, &window, [&window](const QString& name) {
    window.applyTheme(name);
  });
  QObject::connect(&LanguageManager::instance(), &LanguageManager::languageChanged, &window, [&window] {
    window.retranslateUi();
    if (window.sidebar_) {
      window.sidebar_->retranslateUi();
    }
  });
}

void muffin::MainWindowSignalBinder::connectFindBarSignals(MainWindow& window) {
  QObject::connect(window.findBar_, &FindBarWidget::findRequested, &window, &MainWindow::performFind);
  QObject::connect(window.findBar_, &FindBarWidget::closed, &window, &MainWindow::hideFindBar);
  QObject::connect(window.findBar_, &FindBarWidget::replaceRequested, &window, &MainWindow::performReplace);
  QObject::connect(window.findBar_, &FindBarWidget::replaceAllRequested, &window, &MainWindow::performReplaceAll);
}

void muffin::MainWindowSignalBinder::connectChromeSignals(MainWindow& window) {
  QObject::connect(window.sidebarButton_, &QToolButton::clicked, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("view.sidebar"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sourceModeButton_, &QToolButton::clicked, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("view.source_mode"))) {
      action->trigger();
    }
  });
}

void muffin::MainWindowSignalBinder::connectSidebarSignals(MainWindow& window) {
  QObject::connect(window.sidebar_, &SidebarWidget::newFileRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.new"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::newWindowRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.new_window"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::openFolderRequested, &window, [&window] {
    if (QAction* action = window.commands_.action(QStringLiteral("file.open_folder"))) {
      action->trigger();
    }
  });
  QObject::connect(window.sidebar_, &SidebarWidget::fileOpenRequested, &window, [&window](const QString& path) {
    window.openFile(path);
  });
  QObject::connect(window.sidebar_, &SidebarWidget::outlineActivated, &window, &MainWindow::activateOutlineNode);
}
