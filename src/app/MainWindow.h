#pragma once

#include "editor/MarkdownEditor.h"
#include "core/Document.h"
#include "core/FileManager.h"
#include "theme/Theme.h"
#include <QMainWindow>
#include <QString>
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

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onFileNew();
    void onFileOpen();
    bool onFileSave();
    bool onFileSaveAs();
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

    bool saveToFile(const QString& filePath);
    bool loadFromFile(const QString& filePath);
    bool maybeSave();

    MarkdownEditor* m_editor;
    QLabel* m_wordCountLabel;
    QAction* m_toggleViewAction;
    QActionGroup* m_themeActionGroup;
    ThemePreset m_themePreset = ThemePreset::Github;

    std::unique_ptr<Document> m_document;
    FileManager m_fileManager;

    QString m_currentFile;
    bool m_modified = false;
};

} // namespace Muffin
