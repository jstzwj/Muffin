#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTextStream>

namespace Muffin {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_document(std::make_unique<Document>())
{
    setMinimumSize(800, 600);
    resize(1024, 768);

    setupEditor();
    setupMenuBar();
    setupStatusBar();
    connectSignals();
    applyTheme(m_themePreset);
    updateWindowTitle();
}

void MainWindow::setupEditor()
{
    m_editor = new MarkdownEditor(this);
    setCentralWidget(m_editor);
}

void MainWindow::setupMenuBar()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    QAction* newAction = fileMenu->addAction(QStringLiteral("新建(&N)"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onFileNew);

    QAction* openAction = fileMenu->addAction(QStringLiteral("打开...(&O)"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onFileOpen);

    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction(QStringLiteral("保存(&S)"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onFileSave);

    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("另存为...(&A)"));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onFileSaveAs);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction(QStringLiteral("退出(&X)"));
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    editMenu->addAction(QStringLiteral("撤销(&U)"))->setEnabled(false);
    editMenu->addAction(QStringLiteral("重做(&R)"))->setEnabled(false);

    QMenu* paragraphMenu = menuBar()->addMenu(QStringLiteral("段落(&P)"));
    paragraphMenu->addAction(QStringLiteral("普通文本"))->setEnabled(false);

    QMenu* formatMenu = menuBar()->addMenu(QStringLiteral("格式(&O)"));
    formatMenu->addAction(QStringLiteral("加粗"))->setEnabled(false);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));

    m_toggleViewAction = viewMenu->addAction(QStringLiteral("源代码模式"));
    m_toggleViewAction->setShortcut(QKeySequence(Qt::Key_F9));
    m_toggleViewAction->setCheckable(true);
    connect(m_toggleViewAction, &QAction::triggered, this, &MainWindow::onToggleViewMode);

    viewMenu->addSeparator();

    QAction* wrapAction = viewMenu->addAction(QStringLiteral("自动换行"));
    wrapAction->setCheckable(true);
    wrapAction->setChecked(true);
    connect(wrapAction, &QAction::toggled, m_editor->sourceEditor(),
        [this](bool checked) {
            m_editor->sourceEditor()->setLineWrapMode(
                checked ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        });

    QMenu* themeMenu = menuBar()->addMenu(QStringLiteral("主题(&T)"));
    m_themeActionGroup = new QActionGroup(this);
    m_themeActionGroup->setExclusive(true);

    const ThemePreset presets[] = {
        ThemePreset::Github,
        ThemePreset::Newsprint,
        ThemePreset::Night,
        ThemePreset::Pixyll,
        ThemePreset::Whitey
    };

    for (ThemePreset preset : presets) {
        QAction* action = themeMenu->addAction(Theme::displayName(preset));
        action->setCheckable(true);
        action->setData(static_cast<int>(preset));
        if (preset == m_themePreset) {
            action->setChecked(true);
        }
        action->setIconVisibleInMenu(true);
        m_themeActionGroup->addAction(action);
    }

    connect(m_themeActionGroup, &QActionGroup::triggered,
            this, &MainWindow::onThemeChanged);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    QAction* aboutAction = helpMenu->addAction(QStringLiteral("关于 Muffin"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, tr("About Muffin"),
            tr("Muffin v0.1.0\n\n"
               "A fast, lightweight Markdown WYSIWYG editor.\n"
               "Built with Qt 6 and cmark-gfm."));
    });
}

void MainWindow::setupStatusBar()
{
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setContentsMargins(0, 0, 0, 0);

    QLabel* outlineIcon = new QLabel(QStringLiteral("  ○"), this);
    outlineIcon->setObjectName(QStringLiteral("statusIcon"));
    outlineIcon->setFixedWidth(28);
    outlineIcon->setAlignment(Qt::AlignCenter);
    QLabel* sourceIcon = new QLabel(QStringLiteral("</>"), this);
    sourceIcon->setObjectName(QStringLiteral("statusIcon"));
    sourceIcon->setFixedWidth(34);
    sourceIcon->setAlignment(Qt::AlignCenter);

    m_wordCountLabel = new QLabel(QStringLiteral("0 词  "), this);
    m_wordCountLabel->setObjectName(QStringLiteral("wordCount"));
    m_wordCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    statusBar()->addWidget(outlineIcon);
    statusBar()->addWidget(sourceIcon);
    statusBar()->addPermanentWidget(m_wordCountLabel, 1);
}

void MainWindow::connectSignals()
{
    connect(m_document.get(), &Document::documentRendered,
            this, &MainWindow::onDocumentRendered);

    connect(m_editor, &MarkdownEditor::modeChanged, this, [this](MarkdownEditor::Mode mode) {
        m_toggleViewAction->setChecked(mode == MarkdownEditor::SourceMode);
    });
}

bool MainWindow::openFile(const QString& filePath)
{
    return loadFromFile(filePath);
}

void MainWindow::onFileNew()
{
    if (maybeSave()) {
        m_document->setMarkdown({});
        m_document->setFilePath({});
        m_currentFile.clear();
        m_modified = false;
        m_editor->setSourceText({});
        m_editor->setRenderedDocument(nullptr);
        updateWindowTitle();
    }
}

void MainWindow::onFileOpen()
{
    if (maybeSave()) {
        QString filePath = QFileDialog::getOpenFileName(this,
            tr("Open Markdown File"), {},
            tr("Markdown Files (*.md *.markdown *.mdown *.mkd);;All Files (*)"));

        if (!filePath.isEmpty()) {
            loadFromFile(filePath);
        }
    }
}

bool MainWindow::onFileSave()
{
    if (m_currentFile.isEmpty()) {
        return onFileSaveAs();
    }
    return saveToFile(m_currentFile);
}

bool MainWindow::onFileSaveAs()
{
    QString filePath = QFileDialog::getSaveFileName(this,
        tr("Save Markdown File"), {},
        tr("Markdown Files (*.md *.markdown);;All Files (*)"));

    if (filePath.isEmpty())
        return false;

    return saveToFile(filePath);
}

void MainWindow::onToggleViewMode()
{
    if (m_editor->mode() == MarkdownEditor::RenderedMode) {
        // Switch to source — update source text from document
        m_editor->setSourceText(m_document->markdown());
        m_editor->setMode(MarkdownEditor::SourceMode);
    } else {
        // Switch to rendered — apply source edits back to document
        QString source = m_editor->sourceText();
        m_document->setMarkdown(source);
        m_editor->setMode(MarkdownEditor::RenderedMode);
    }
}

void MainWindow::onThemeChanged(QAction* action)
{
    applyTheme(static_cast<ThemePreset>(action->data().toInt()));
}

void MainWindow::applyTheme(ThemePreset preset)
{
    m_themePreset = preset;
    Theme theme = Theme::preset(preset);
    m_document->setTheme(theme);
    m_editor->applyTheme(theme);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, theme.chromeBackground);
    pal.setColor(QPalette::WindowText, theme.foreground);
    setPalette(pal);

    menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar { background: %1; color: %2; border: none; }"
        "QMenuBar::item { padding: 4px 8px; background: transparent; }"
        "QMenuBar::item:selected, QMenuBar::item:pressed { background: rgba(128, 128, 128, 30); border-radius: 3px; }"
        "QMenu { background: %3; color: %4; border: 1px solid rgba(128, 128, 128, 42); border-radius: 8px; padding: 6px 0; }"
        "QMenu::item { padding: 6px 34px 6px 30px; }"
        "QMenu::item:selected { background: rgba(128, 128, 128, 30); }"
        "QMenu::indicator { width: 18px; height: 18px; left: 7px; }"
        "QMenu::indicator:checked { image: none; }"
        "QMenu::indicator:checked:enabled { color: %5; }")
        .arg(theme.chromeBackground.name(), theme.foreground.name(),
             theme.menuBackground.name(), theme.menuForeground.name(),
             theme.accentColor.name()));

    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar { background: %1; color: rgba(80, 80, 80, 170); border: none; font-size: 12px; }"
        "QStatusBar::item { border: none; }"
        "QLabel#statusIcon { color: rgba(40, 40, 40, 190); padding-bottom: 1px; }"
        "QLabel#wordCount { color: rgba(40, 40, 40, 190); padding-right: 4px; }")
        .arg(theme.statusBackground.name()));
}

void MainWindow::onDocumentRendered()
{
    QTextDocument* doc = m_document->textDocument();
    if (doc) {
        // Clone the document so QTextBrowser doesn't take ownership
        auto clone = doc->clone(m_editor);
        m_editor->setRenderedDocument(clone);
    }
}

void MainWindow::updateWindowTitle()
{
    QString plain = m_document->markdown();
    int wordCount = plain.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    if (m_wordCountLabel) {
        m_wordCountLabel->setText(QStringLiteral("%1 词  ").arg(wordCount));
    }

    QString title = m_currentFile.isEmpty()
        ? tr("Untitled")
        : QFileInfo(m_currentFile).fileName();

    if (m_modified)
        title += "*";

    title += " - Muffin";
    setWindowTitle(title);
}

bool MainWindow::saveToFile(const QString& filePath)
{
    QString content;
    if (m_editor->mode() == MarkdownEditor::SourceMode) {
        content = m_editor->sourceText();
    } else {
        content = m_document->markdown();
    }

    if (!m_fileManager.writeFile(filePath, content)) {
        QMessageBox::warning(this, tr("Save Error"),
            tr("Cannot save file."));
        return false;
    }

    m_currentFile = filePath;
    m_modified = false;
    updateWindowTitle();
    return true;
}

bool MainWindow::loadFromFile(const QString& filePath)
{
    QString content = m_fileManager.readFile(filePath);
    if (content.isNull()) {
        QMessageBox::warning(this, tr("Open Error"),
            tr("Cannot open file."));
        return false;
    }

    m_document->setMarkdown(content);
    m_document->setFilePath(filePath);
    m_currentFile = filePath;
    m_modified = false;
    m_editor->setMode(MarkdownEditor::RenderedMode);
    updateWindowTitle();
    return true;
}

bool MainWindow::maybeSave()
{
    if (!m_modified && !m_document->isModified())
        return true;

    auto result = QMessageBox::question(this, tr("Unsaved Changes"),
        tr("The document has been modified.\nDo you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

    if (result == QMessageBox::Save)
        return onFileSave();
    if (result == QMessageBox::Cancel)
        return false;
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

} // namespace Muffin
