#pragma once

#include "editor/MarkdownEditor.h"
#include "core/Document.h"
#include "core/FileManager.h"
#include "theme/Theme.h"
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <memory>

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

    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    bool maybeSave();

    MarkdownEditor* m_editor;
    QLabel* m_wordCountLabel;
    QAction* m_toggleViewAction = nullptr;
    QAction* m_fullscreenAction = nullptr;
    QAction* m_stayOnTopAction = nullptr;
    QAction* m_actualSizeAction = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    QActionGroup* m_themeActionGroup = nullptr;
    ThemePreset m_themePreset = ThemePreset::Github;
    int m_sourceZoomSteps = 0;
    double m_zoomFactor = 1.0;

    std::unique_ptr<Document> m_document;
    FileManager m_fileManager;

    QString m_currentFile;
    bool m_modified = false;
};

} // namespace Muffin
