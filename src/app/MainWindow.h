#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QActionGroup>

namespace Md { class WysiwygEditor; }

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

private:
    void setupEditor();
    void setupMenuBar();
    void setupStatusBar();
    void updateTitle();
    void applyTheme();

    // File
    void onFileNew();
    void onFileNewWindow();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileProperties();
    void onOpenFileLocation();
    void onFilePrint();

    // Edit
    void onFindReplace();

    // View
    void onToggleViewMode();
    void onToggleFullscreen();
    void onToggleStayOnTop();
    void onZoomIn();
    void onZoomOut();
    void onZoomActualSize();
    void onShowWordCount();
    void onShowRenderDiagnostics();

    // Theme
    void onThemeChanged();

    // Helpers
    void updateRecentMenu();
    void addRecentFile(const QString &path);

    Md::WysiwygEditor *m_editor = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_wordCountLabel = nullptr;

    QString m_filePath;
    int m_currentThemeIndex = 0;

    QAction *m_actionSourceMode = nullptr;
    QAction *m_actionStatusBar = nullptr;
    QAction *m_actionWordWrap = nullptr;
    QAction *m_actionFullscreen = nullptr;
    QAction *m_actionStayOnTop = nullptr;
    QAction *m_actionActualSize = nullptr;
    QActionGroup *m_themeGroup = nullptr;
    QMenu *m_recentMenu = nullptr;

    static constexpr double kDefaultZoom = 1.0;
    double m_zoomFactor = kDefaultZoom;
};
