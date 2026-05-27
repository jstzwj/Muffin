#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QTextStream>
#include <QUrl>

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
    auto disabledAction = [](QMenu* menu, const QString& text, const QKeySequence& shortcut = {}) {
        QAction* action = menu->addAction(text);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        action->setEnabled(false);
        return action;
    };

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));

    QAction* newAction = fileMenu->addAction(QStringLiteral("新建"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onFileNew);

    QAction* newWindowAction = fileMenu->addAction(QStringLiteral("新建窗口"));
    newWindowAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    connect(newWindowAction, &QAction::triggered, this, &MainWindow::onFileNewWindow);
    fileMenu->addSeparator();

    QAction* openAction = fileMenu->addAction(QStringLiteral("打开..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onFileOpen);

    disabledAction(fileMenu, QStringLiteral("打开文件夹..."));
    fileMenu->addSeparator();

    disabledAction(fileMenu, QStringLiteral("快速打开..."), QKeySequence(QStringLiteral("Ctrl+P")));
    m_recentFilesMenu = fileMenu->addMenu(QStringLiteral("打开最近文件"));
    connect(m_recentFilesMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentFilesMenu);
    QMenu* reopenMenu = fileMenu->addMenu(QStringLiteral("选择编码重新打开"));
    reopenMenu->setEnabled(false);
    fileMenu->addSeparator();

    QAction* saveAction = fileMenu->addAction(QStringLiteral("保存"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onFileSave);

    QAction* saveAsAction = fileMenu->addAction(QStringLiteral("另存为..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onFileSaveAs);

    disabledAction(fileMenu, QStringLiteral("移动到..."));
    disabledAction(fileMenu, QStringLiteral("保存全部打开的文件..."));
    fileMenu->addSeparator();

    QAction* propertiesAction = fileMenu->addAction(QStringLiteral("属性..."));
    connect(propertiesAction, &QAction::triggered, this, &MainWindow::onFileProperties);

    QAction* openLocationAction = fileMenu->addAction(QStringLiteral("打开文件位置..."));
    connect(openLocationAction, &QAction::triggered, this, &MainWindow::onOpenFileLocation);
    disabledAction(fileMenu, QStringLiteral("在侧边栏中显示"));
    disabledAction(fileMenu, QStringLiteral("删除..."));
    fileMenu->addSeparator();

    disabledAction(fileMenu, QStringLiteral("导入..."));
    QMenu* exportMenu = fileMenu->addMenu(QStringLiteral("导出"));
    exportMenu->setEnabled(false);
    QAction* printAction = fileMenu->addAction(QStringLiteral("打印..."));
    printAction->setShortcut(QKeySequence(QStringLiteral("Alt+Shift+P")));
    connect(printAction, &QAction::triggered, this, &MainWindow::onFilePrint);
    fileMenu->addSeparator();

    disabledAction(fileMenu, QStringLiteral("偏好设置..."), QKeySequence(QStringLiteral("Ctrl+Comma")));
    fileMenu->addSeparator();

    QAction* closeAction = fileMenu->addAction(QStringLiteral("关闭"));
    closeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("编辑(&E)"));

    QAction* undoAction = editMenu->addAction(QStringLiteral("撤消"));
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::undo);

    QAction* redoAction = editMenu->addAction(QStringLiteral("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::redo);
    editMenu->addSeparator();

    QAction* cutAction = editMenu->addAction(QStringLiteral("剪切"));
    cutAction->setShortcut(QKeySequence::Cut);
    connect(cutAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::cut);

    QAction* copyAction = editMenu->addAction(QStringLiteral("复制"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::copy);

    disabledAction(editMenu, QStringLiteral("拷贝图片"));

    QAction* pasteAction = editMenu->addAction(QStringLiteral("粘贴"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::paste);
    editMenu->addSeparator();

    disabledAction(editMenu, QStringLiteral("复制为纯文本"));
    disabledAction(editMenu, QStringLiteral("复制为 Markdown"), QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    disabledAction(editMenu, QStringLiteral("复制为 HTML 代码"));
    disabledAction(editMenu, QStringLiteral("复制内容并简化格式"));
    editMenu->addSeparator();

    disabledAction(editMenu, QStringLiteral("粘贴为纯文本"), QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    editMenu->addSeparator();

    QMenu* selectMenu = editMenu->addMenu(QStringLiteral("选择"));
    QAction* selectAllAction = selectMenu->addAction(QStringLiteral("全选"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, m_editor->sourceEditor(), &QPlainTextEdit::selectAll);
    disabledAction(selectMenu, QStringLiteral("选择当前行"));
    disabledAction(selectMenu, QStringLiteral("选择当前格式文本"));

    disabledAction(editMenu, QStringLiteral("上移该行"), QKeySequence(QStringLiteral("Alt+Up")));
    disabledAction(editMenu, QStringLiteral("下移该行"), QKeySequence(QStringLiteral("Alt+Down")));
    editMenu->addSeparator();

    disabledAction(editMenu, QStringLiteral("删除"));
    QMenu* deleteRangeMenu = editMenu->addMenu(QStringLiteral("删除范围"));
    deleteRangeMenu->setEnabled(false);
    editMenu->addSeparator();

    QMenu* mathMenu = editMenu->addMenu(QStringLiteral("数学工具"));
    mathMenu->setEnabled(false);
    editMenu->addSeparator();

    QMenu* smartPunctuationMenu = editMenu->addMenu(QStringLiteral("智能标点"));
    smartPunctuationMenu->setEnabled(false);
    QMenu* lineEndingMenu = editMenu->addMenu(QStringLiteral("换行符"));
    lineEndingMenu->setEnabled(false);
    QMenu* whitespaceMenu = editMenu->addMenu(QStringLiteral("空格与换行"));
    whitespaceMenu->setEnabled(false);
    disabledAction(editMenu, QStringLiteral("拼写检查..."));
    editMenu->addSeparator();

    QMenu* findMenu = editMenu->addMenu(QStringLiteral("查找和替换"));
    findMenu->setEnabled(false);
    editMenu->addSeparator();

    disabledAction(editMenu, QStringLiteral("表情与符号"), QKeySequence(QStringLiteral("Win+Period")));

    QMenu* paragraphMenu = menuBar()->addMenu(QStringLiteral("段落(&P)"));
    QActionGroup* paragraphGroup = new QActionGroup(this);
    paragraphGroup->setExclusive(true);

    const QStringList headingNames = {
        QStringLiteral("一级标题"), QStringLiteral("二级标题"), QStringLiteral("三级标题"),
        QStringLiteral("四级标题"), QStringLiteral("五级标题"), QStringLiteral("六级标题")
    };
    for (int i = 0; i < headingNames.size(); ++i) {
        QAction* action = disabledAction(paragraphMenu, headingNames[i], QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        action->setCheckable(true);
        paragraphGroup->addAction(action);
    }
    paragraphMenu->addSeparator();

    QAction* paragraphAction = disabledAction(paragraphMenu, QStringLiteral("段落"), QKeySequence(QStringLiteral("Ctrl+0")));
    paragraphAction->setCheckable(true);
    paragraphAction->setChecked(true);
    paragraphGroup->addAction(paragraphAction);
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("提升标题级别"), QKeySequence(QStringLiteral("Ctrl+-")));
    disabledAction(paragraphMenu, QStringLiteral("降低标题级别"), QKeySequence(QStringLiteral("Ctrl++")));
    paragraphMenu->addSeparator();

    QMenu* tableMenu = paragraphMenu->addMenu(QStringLiteral("表格"));
    tableMenu->setEnabled(false);
    disabledAction(paragraphMenu, QStringLiteral("公式块"), QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    disabledAction(paragraphMenu, QStringLiteral("代码块"), QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    disabledAction(paragraphMenu, QStringLiteral("代码工具"));
    QMenu* alertMenu = paragraphMenu->addMenu(QStringLiteral("警告框"));
    alertMenu->setEnabled(false);
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("引用"), QKeySequence(QStringLiteral("Ctrl+Shift+Q")));
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("有序列表"), QKeySequence(QStringLiteral("Ctrl+Shift+[")));
    disabledAction(paragraphMenu, QStringLiteral("无序列表"), QKeySequence(QStringLiteral("Ctrl+Shift+]")));
    disabledAction(paragraphMenu, QStringLiteral("任务列表"), QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    QMenu* taskStatusMenu = paragraphMenu->addMenu(QStringLiteral("任务状态"));
    taskStatusMenu->setEnabled(false);
    QMenu* listIndentMenu = paragraphMenu->addMenu(QStringLiteral("列表缩进"));
    listIndentMenu->setEnabled(false);
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("在上方插入段落"));
    disabledAction(paragraphMenu, QStringLiteral("在下方插入段落"));
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("链接引用"));
    disabledAction(paragraphMenu, QStringLiteral("脚注"));
    paragraphMenu->addSeparator();

    disabledAction(paragraphMenu, QStringLiteral("水平分割线"));
    disabledAction(paragraphMenu, QStringLiteral("内容目录"));
    disabledAction(paragraphMenu, QStringLiteral("YAML Front Matter"));

    QMenu* formatMenu = menuBar()->addMenu(QStringLiteral("格式(&O)"));
    disabledAction(formatMenu, QStringLiteral("加粗"), QKeySequence::Bold)->setCheckable(true);
    disabledAction(formatMenu, QStringLiteral("斜体"), QKeySequence::Italic)->setCheckable(true);
    disabledAction(formatMenu, QStringLiteral("下划线"), QKeySequence::Underline)->setCheckable(true);
    disabledAction(formatMenu, QStringLiteral("代码"), QKeySequence(QStringLiteral("Ctrl+Shift+`")))->setCheckable(true);
    formatMenu->addSeparator();

    disabledAction(formatMenu, QStringLiteral("删除线"), QKeySequence(QStringLiteral("Alt+Shift+5")));
    disabledAction(formatMenu, QStringLiteral("注释"));
    formatMenu->addSeparator();

    disabledAction(formatMenu, QStringLiteral("超链接"), QKeySequence(QStringLiteral("Ctrl+K")));
    QMenu* linkMenu = formatMenu->addMenu(QStringLiteral("链接操作"));
    linkMenu->setEnabled(false);
    QMenu* imageMenu = formatMenu->addMenu(QStringLiteral("图像"));
    imageMenu->setEnabled(false);
    formatMenu->addSeparator();

    disabledAction(formatMenu, QStringLiteral("清除样式"), QKeySequence(QStringLiteral("Ctrl+\\")));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    disabledAction(viewMenu, QStringLiteral("显示 / 隐藏侧边栏"), QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    disabledAction(viewMenu, QStringLiteral("大纲"), QKeySequence(QStringLiteral("Ctrl+Shift+1")));
    disabledAction(viewMenu, QStringLiteral("文档列表"), QKeySequence(QStringLiteral("Ctrl+Shift+2")));
    disabledAction(viewMenu, QStringLiteral("文件树"), QKeySequence(QStringLiteral("Ctrl+Shift+3")));
    disabledAction(viewMenu, QStringLiteral("搜索"), QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    viewMenu->addSeparator();

    m_toggleViewAction = viewMenu->addAction(QStringLiteral("源代码模式"));
    m_toggleViewAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
    m_toggleViewAction->setCheckable(true);
    connect(m_toggleViewAction, &QAction::triggered, this, &MainWindow::onToggleViewMode);

    QAction* wrapAction = viewMenu->addAction(QStringLiteral("自动换行"));
    wrapAction->setCheckable(true);
    wrapAction->setChecked(true);
    connect(wrapAction, &QAction::toggled, m_editor->sourceEditor(),
        [this](bool checked) {
            m_editor->sourceEditor()->setLineWrapMode(
                checked ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
        });
    viewMenu->addSeparator();

    disabledAction(viewMenu, QStringLiteral("专注模式"), QKeySequence(QStringLiteral("F8")));
    disabledAction(viewMenu, QStringLiteral("打字机模式"), QKeySequence(QStringLiteral("F9")));
    viewMenu->addSeparator();

    QAction* statusBarAction = viewMenu->addAction(QStringLiteral("显示状态栏"));
    statusBarAction->setCheckable(true);
    statusBarAction->setChecked(true);
    connect(statusBarAction, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

    QAction* wordCountAction = viewMenu->addAction(QStringLiteral("字数统计窗口"));
    connect(wordCountAction, &QAction::triggered, this, &MainWindow::onShowWordCount);
    viewMenu->addSeparator();

    m_fullscreenAction = viewMenu->addAction(QStringLiteral("切换全屏"));
    m_fullscreenAction->setShortcut(QKeySequence(QStringLiteral("F11")));
    m_fullscreenAction->setCheckable(true);
    connect(m_fullscreenAction, &QAction::triggered, this, &MainWindow::onToggleFullscreen);

    m_stayOnTopAction = viewMenu->addAction(QStringLiteral("保持窗口在最前端"));
    m_stayOnTopAction->setCheckable(true);
    connect(m_stayOnTopAction, &QAction::toggled, this, &MainWindow::onToggleStayOnTop);
    viewMenu->addSeparator();

    m_actualSizeAction = viewMenu->addAction(QStringLiteral("实际大小"));
    m_actualSizeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+9")));
    m_actualSizeAction->setCheckable(true);
    m_actualSizeAction->setChecked(true);
    connect(m_actualSizeAction, &QAction::triggered, this, &MainWindow::onZoomActualSize);

    QAction* zoomInAction = viewMenu->addAction(QStringLiteral("放大"));
    zoomInAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+=")));
    connect(zoomInAction, &QAction::triggered, this, &MainWindow::onZoomIn);

    QAction* zoomOutAction = viewMenu->addAction(QStringLiteral("缩小"));
    zoomOutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+-")));
    connect(zoomOutAction, &QAction::triggered, this, &MainWindow::onZoomOut);
    viewMenu->addSeparator();

    disabledAction(viewMenu, QStringLiteral("应用内窗口切换"), QKeySequence(QStringLiteral("Ctrl+Tab")));
    viewMenu->addSeparator();

    disabledAction(viewMenu, QStringLiteral("开发者工具"), QKeySequence(QStringLiteral("Shift+F12")));

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
    themeMenu->addSeparator();
    disabledAction(themeMenu, QStringLiteral("打开主题文件夹"));
    disabledAction(themeMenu, QStringLiteral("获取主题"));

    connect(m_themeActionGroup, &QActionGroup::triggered,
            this, &MainWindow::onThemeChanged);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    disabledAction(helpMenu, QStringLiteral("What's New..."));
    helpMenu->addSeparator();
    disabledAction(helpMenu, QStringLiteral("Quick Start"));
    disabledAction(helpMenu, QStringLiteral("Markdown Reference"));
    disabledAction(helpMenu, QStringLiteral("Install and Use Pandoc"));
    disabledAction(helpMenu, QStringLiteral("Custom Themes"));
    disabledAction(helpMenu, QStringLiteral("Use Images in Typora"));
    disabledAction(helpMenu, QStringLiteral("Data Recovery and Version Control"));
    disabledAction(helpMenu, QStringLiteral("更多主题..."));
    helpMenu->addSeparator();
    disabledAction(helpMenu, QStringLiteral("鸣谢"));
    disabledAction(helpMenu, QStringLiteral("更新日志"));
    disabledAction(helpMenu, QStringLiteral("隐私条款"));
    disabledAction(helpMenu, QStringLiteral("官方网站"));
    disabledAction(helpMenu, QStringLiteral("反馈"));
    helpMenu->addSeparator();
    disabledAction(helpMenu, QStringLiteral("检查更新..."));
    disabledAction(helpMenu, QStringLiteral("我的许可证..."));

    QAction* aboutAction = helpMenu->addAction(QStringLiteral("关于"));
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

void MainWindow::onFileNewWindow()
{
    auto* window = new MainWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
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

void MainWindow::onOpenFileLocation()
{
    if (m_currentFile.isEmpty()) {
        QMessageBox::information(this, tr("Open File Location"), tr("This document has not been saved yet."));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentFile).absolutePath()));
}

void MainWindow::onFileProperties()
{
    const QString path = m_currentFile.isEmpty() ? tr("Unsaved document") : QDir::toNativeSeparators(m_currentFile);
    const QFileInfo info(m_currentFile);
    const QString size = info.exists() ? QString::number(info.size()) + tr(" bytes") : tr("Not saved");
    const QString modified = info.exists() ? info.lastModified().toString(Qt::ISODate) : tr("Not saved");

    QMessageBox::information(this, tr("Properties"),
        tr("Path: %1\nSize: %2\nModified: %3\nWords: %4\nCharacters: %5\nLines: %6")
            .arg(path, size, modified)
            .arg(wordCount())
            .arg(characterCount())
            .arg(lineCount()));
}

void MainWindow::onFilePrint()
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QTextDocument* doc = m_document->textDocument();
    if (doc) {
        doc->print(&printer);
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

void MainWindow::onShowWordCount()
{
    QMessageBox::information(this, tr("Word Count"),
        tr("Words: %1\nCharacters: %2\nLines: %3")
            .arg(wordCount())
            .arg(characterCount())
            .arg(lineCount()));
}

void MainWindow::onToggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        m_fullscreenAction->setChecked(false);
    } else {
        showFullScreen();
        m_fullscreenAction->setChecked(true);
    }
}

void MainWindow::onToggleStayOnTop(bool checked)
{
    Qt::WindowFlags flags = windowFlags();
    if (checked) {
        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    setWindowFlags(flags);
    show();
}

void MainWindow::onZoomActualSize()
{
    m_zoomFactor = 1.0;
    m_editor->renderedDocument()->setDefaultFont(m_document->theme().bodyFont);
    if (m_sourceZoomSteps > 0) {
        m_editor->sourceEditor()->zoomOut(m_sourceZoomSteps);
    } else if (m_sourceZoomSteps < 0) {
        m_editor->sourceEditor()->zoomIn(-m_sourceZoomSteps);
    }
    m_sourceZoomSteps = 0;
    m_actualSizeAction->setChecked(true);
}

void MainWindow::onZoomIn()
{
    m_zoomFactor = qMin(2.5, m_zoomFactor + 0.1);
    QFont font = m_document->theme().bodyFont;
    font.setPointSizeF(font.pointSizeF() * m_zoomFactor);
    m_editor->renderedDocument()->setDefaultFont(font);
    m_editor->sourceEditor()->zoomIn(1);
    m_sourceZoomSteps++;
    m_actualSizeAction->setChecked(qFuzzyCompare(m_zoomFactor, 1.0));
}

void MainWindow::onZoomOut()
{
    m_zoomFactor = qMax(0.5, m_zoomFactor - 0.1);
    QFont font = m_document->theme().bodyFont;
    font.setPointSizeF(font.pointSizeF() * m_zoomFactor);
    m_editor->renderedDocument()->setDefaultFont(font);
    m_editor->sourceEditor()->zoomOut(1);
    m_sourceZoomSteps--;
    m_actualSizeAction->setChecked(qFuzzyCompare(m_zoomFactor, 1.0));
}

void MainWindow::onOpenRecentFile()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QString filePath = action->data().toString();
    if (!filePath.isEmpty() && maybeSave()) {
        loadFromFile(filePath);
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

void MainWindow::updateRecentFilesMenu()
{
    if (!m_recentFilesMenu) {
        return;
    }

    m_recentFilesMenu->clear();
    const QStringList files = recentFiles();
    if (files.isEmpty()) {
        QAction* emptyAction = m_recentFilesMenu->addAction(QStringLiteral("无最近文件"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& filePath : files) {
        QAction* action = m_recentFilesMenu->addAction(QDir::toNativeSeparators(filePath));
        action->setData(filePath);
        connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }
}

void MainWindow::addRecentFile(const QString& filePath)
{
    QStringList files = recentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 10) {
        files.removeLast();
    }
    setRecentFiles(files);
}

QStringList MainWindow::recentFiles() const
{
    QSettings settings;
    return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void MainWindow::setRecentFiles(const QStringList& files)
{
    QSettings settings;
    settings.setValue(QStringLiteral("recentFiles"), files);
}

int MainWindow::wordCount() const
{
    return m_document->markdown().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

int MainWindow::characterCount() const
{
    return m_document->markdown().size();
}

int MainWindow::lineCount() const
{
    if (m_document->markdown().isEmpty()) {
        return 0;
    }
    return m_document->markdown().count(QChar('\n')) + 1;
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
    if (m_wordCountLabel) {
        m_wordCountLabel->setText(QStringLiteral("%1 词  ").arg(wordCount()));
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
    addRecentFile(filePath);
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
    addRecentFile(filePath);
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
