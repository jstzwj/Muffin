#pragma once

#include "editor/MarkdownCommand.h"
#include "editor/MarkdownEditor.h"
#include "core/Document.h"
#include "core/FileManager.h"
#include "theme/Theme.h"
#include <QMainWindow>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>
#include <optional>

class QAction;
class QActionGroup;
class QLabel;

namespace Muffin {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool openFile(const QString& filePath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onFileNew();
    void onFileNewWindow();
    void onFileOpen();
    void onOpenFileLocation();
    void onFileProperties();
    void onFilePrint();
    bool onFileSave();
    bool onFileSaveAs();
    void onShowWordCount();
    void onToggleFullscreen();
    void onToggleStayOnTop(bool checked);
    void onZoomActualSize();
    void onZoomIn();
    void onZoomOut();
    void onOpenRecentFile();
    void onClearRecentFiles();
    void onSourceTextChanged();
    void onRenderedSourceRangeClicked(SourceRange range);
    void onRenderedInlineTextSelected(SourceRange range, const QString& text);
    void onRenderedEditRequested(RenderedEdit edit);
    void onFindReplace();
    void onToggleBold();
    void onToggleItalic();
    void onToggleUnderline();
    void onToggleInlineCode();
    void onInsertLink();
    void onApplyHeading1();
    void onApplyHeading2();
    void onApplyHeading3();
    void onApplyHeading4();
    void onApplyHeading5();
    void onApplyHeading6();
    void onApplyParagraph();
    void onApplyQuote();
    void onApplyOrderedList();
    void onApplyUnorderedList();
    void onApplyTaskList();
    void onToggleViewMode();
    void onThemeChanged(QAction* action);
    void onDocumentRendered();
    void updateWindowTitle();

private:
    void setupMenuBar();
    void setupStatusBar();
    void setupEditor();
    void connectSignals();
    void applyTheme(ThemePreset preset);
    void updateRecentFilesMenu();
    void addRecentFile(const QString& filePath);
    QStringList recentFiles() const;
    void setRecentFiles(const QStringList& files);
    int wordCount() const;
    int characterCount() const;
    int lineCount() const;
    QString currentMarkdownText() const;
    void scheduleSourceSync();
    void flushPendingSourceSync();
    void syncSourceToDocument();
    void ensureSourceMode();
    void moveSourceCursorToRange(SourceRange range, bool selectRange);
    bool moveSourceCursorToInlineText(SourceRange range, const QString& text);
    bool hasRenderedCommandTarget() const;
    bool warnIfMissingRenderedCommandTarget();
    void clearRenderedCommandTarget();
    void updateSingleBlockCommandState();
    void applyMarkdownCommand(MarkdownCommandResult (*command)(const QString&, SourceSelection));
    void applyMarkdownListCommand(MarkdownCommand::ListType type);
    void applyHeadingLevel(int level);

    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    bool maybeSave();

    MarkdownEditor* m_editor;
    QLabel* m_commandTargetLabel = nullptr;
    QLabel* m_wordCountLabel;
    QAction* m_toggleViewAction = nullptr;
    QAction* m_fullscreenAction = nullptr;
    QAction* m_stayOnTopAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    QActionGroup* m_themeActionGroup = nullptr;
    QVector<QAction*> m_singleBlockParagraphActions;
    QMenu* m_tableMenu = nullptr;
    ThemePreset m_themePreset = ThemePreset::Github;
    int m_sourceZoomSteps = 0;
    double m_zoomFactor = 1.0;

    std::unique_ptr<Document> m_document;
    FileManager m_fileManager;

    QString m_currentFile;
    SourceRange m_lastRenderedSourceRange;
    QString m_lastRenderedSelectedText;
    QTimer m_sourceSyncTimer;
    QString m_pendingSourceText;
    int m_pendingSourceCursorOffset = -1;
    std::optional<int> m_pendingRenderedCursorSourceOffset;
    bool m_updatingSourceFromDocument = false;
    bool m_modified = false;
};

} // namespace Muffin
