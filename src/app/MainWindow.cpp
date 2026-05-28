#include "MainWindow.h"
#include "editor/EditorSelectionMapper.h"
#include "editor/MarkdownEditEngine.h"
#include "renderer/SourceBlockData.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>
#include <QTextBlock>
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
    m_sourceSyncTimer.setSingleShot(true);
    m_sourceSyncTimer.setInterval(150);
    connect(&m_sourceSyncTimer, &QTimer::timeout, this, &MainWindow::flushPendingSourceSync);
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
    connect(undoAction, &QAction::triggered, m_document->undoStack(), &QUndoStack::undo);

    QAction* redoAction = editMenu->addAction(QStringLiteral("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, m_document->undoStack(), &QUndoStack::redo);
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

    QAction* findReplaceAction = editMenu->addAction(QStringLiteral("查找和替换"));
    findReplaceAction->setShortcut(QKeySequence::Find);
    connect(findReplaceAction, &QAction::triggered, this, &MainWindow::onFindReplace);
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
        QAction* action = paragraphMenu->addAction(headingNames[i]);
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, i]() { applyHeadingLevel(i + 1); });
        paragraphGroup->addAction(action);
        m_singleBlockParagraphActions.append(action);
    }
    paragraphMenu->addSeparator();

    QAction* paragraphAction = paragraphMenu->addAction(QStringLiteral("段落"));
    paragraphAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    paragraphAction->setCheckable(true);
    paragraphAction->setChecked(true);
    connect(paragraphAction, &QAction::triggered, this, &MainWindow::onApplyParagraph);
    paragraphGroup->addAction(paragraphAction);
    m_singleBlockParagraphActions.append(paragraphAction);
    paragraphMenu->addSeparator();

    QAction* promoteHeadingAction = disabledAction(paragraphMenu, QStringLiteral("提升标题级别"), QKeySequence(QStringLiteral("Ctrl+-")));
    QAction* demoteHeadingAction = disabledAction(paragraphMenu, QStringLiteral("降低标题级别"), QKeySequence(QStringLiteral("Ctrl++")));
    m_singleBlockParagraphActions.append(promoteHeadingAction);
    m_singleBlockParagraphActions.append(demoteHeadingAction);
    paragraphMenu->addSeparator();

    m_tableMenu = paragraphMenu->addMenu(QStringLiteral("表格"));
    m_tableMenu->setEnabled(false);
    disabledAction(paragraphMenu, QStringLiteral("公式块"), QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    disabledAction(paragraphMenu, QStringLiteral("代码块"), QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    disabledAction(paragraphMenu, QStringLiteral("代码工具"));
    QMenu* alertMenu = paragraphMenu->addMenu(QStringLiteral("警告框"));
    alertMenu->setEnabled(false);
    paragraphMenu->addSeparator();

    QAction* quoteAction = paragraphMenu->addAction(QStringLiteral("引用"));
    quoteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Q")));
    connect(quoteAction, &QAction::triggered, this, &MainWindow::onApplyQuote);
    paragraphMenu->addSeparator();

    QAction* orderedListAction = paragraphMenu->addAction(QStringLiteral("有序列表"));
    orderedListAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+[")));
    connect(orderedListAction, &QAction::triggered, this, &MainWindow::onApplyOrderedList);

    QAction* unorderedListAction = paragraphMenu->addAction(QStringLiteral("无序列表"));
    unorderedListAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+]")));
    connect(unorderedListAction, &QAction::triggered, this, &MainWindow::onApplyUnorderedList);

    QAction* taskListAction = paragraphMenu->addAction(QStringLiteral("任务列表"));
    taskListAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
    connect(taskListAction, &QAction::triggered, this, &MainWindow::onApplyTaskList);
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
    QAction* boldAction = formatMenu->addAction(QStringLiteral("加粗"));
    boldAction->setShortcut(QKeySequence::Bold);
    boldAction->setCheckable(true);
    connect(boldAction, &QAction::triggered, this, &MainWindow::onToggleBold);

    QAction* italicAction = formatMenu->addAction(QStringLiteral("斜体"));
    italicAction->setShortcut(QKeySequence::Italic);
    italicAction->setCheckable(true);
    connect(italicAction, &QAction::triggered, this, &MainWindow::onToggleItalic);

    QAction* underlineAction = formatMenu->addAction(QStringLiteral("下划线"));
    underlineAction->setShortcut(QKeySequence::Underline);
    underlineAction->setCheckable(true);
    connect(underlineAction, &QAction::triggered, this, &MainWindow::onToggleUnderline);

    QAction* codeAction = formatMenu->addAction(QStringLiteral("代码"));
    codeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+`")));
    codeAction->setCheckable(true);
    connect(codeAction, &QAction::triggered, this, &MainWindow::onToggleInlineCode);
    formatMenu->addSeparator();

    disabledAction(formatMenu, QStringLiteral("删除线"), QKeySequence(QStringLiteral("Alt+Shift+5")));
    disabledAction(formatMenu, QStringLiteral("注释"));
    formatMenu->addSeparator();

    QAction* linkAction = formatMenu->addAction(QStringLiteral("超链接"));
    linkAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    connect(linkAction, &QAction::triggered, this, &MainWindow::onInsertLink);
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

    m_commandTargetLabel = new QLabel(this);
    m_commandTargetLabel->setObjectName(QStringLiteral("commandTarget"));
    m_commandTargetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_wordCountLabel = new QLabel(QStringLiteral("0 词  "), this);
    m_wordCountLabel->setObjectName(QStringLiteral("wordCount"));
    m_wordCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    statusBar()->addWidget(outlineIcon);
    statusBar()->addWidget(sourceIcon);
    statusBar()->addWidget(m_commandTargetLabel, 1);
    statusBar()->addPermanentWidget(m_wordCountLabel);
}

void MainWindow::connectSignals()
{
    connect(m_document.get(), &Document::documentRendered,
            this, &MainWindow::onDocumentRendered);

    connect(m_document.get(), &Document::cursorSourceOffsetRequested, this, [this](int offset) {
        if (m_editor->mode() == MarkdownEditor::SourceMode) {
            QTextCursor cursor = m_editor->sourceEditor()->textCursor();
            cursor.setPosition(qBound(0, offset, m_editor->sourceText().size()));
            m_editor->sourceEditor()->setTextCursor(cursor);
        } else {
            m_pendingRenderedCursorSourceOffset = offset;
        }
    });

    connect(m_document->undoStack(), &QUndoStack::cleanChanged, this, [this](bool clean) {
        m_modified = !clean;
        updateWindowTitle();
    });

    connect(m_editor, &MarkdownEditor::modeChanged, this, [this](MarkdownEditor::Mode mode) {
        m_toggleViewAction->setChecked(mode == MarkdownEditor::SourceMode);
    });

    connect(m_editor->sourceEditor(), &QPlainTextEdit::textChanged,
            this, &MainWindow::onSourceTextChanged);

    connect(m_editor, &MarkdownEditor::renderedSourceRangeClicked,
            this, &MainWindow::onRenderedSourceRangeClicked);

    connect(m_editor, &MarkdownEditor::renderedInlineTextSelected,
            this, &MainWindow::onRenderedInlineTextSelected);

    connect(m_editor, &MarkdownEditor::renderedEditRequested,
            this, &MainWindow::onRenderedEditRequested);
}

bool MainWindow::openFile(const QString& filePath)
{
    return loadFromFile(filePath);
}

void MainWindow::onFileNew()
{
    if (maybeSave()) {
        m_sourceSyncTimer.stop();
        m_pendingSourceText.clear();
        m_pendingSourceCursorOffset = -1;
        m_document->setMarkdown({});
        m_document->setFilePath({});
        m_currentFile.clear();
        m_modified = false;
        m_document->undoStack()->setClean();
        m_editor->setSourceText({});
        m_editor->setRenderedDocument(nullptr);
        clearRenderedCommandTarget();
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

void MainWindow::onSourceTextChanged()
{
    if (m_editor->mode() != MarkdownEditor::SourceMode || m_updatingSourceFromDocument) {
        return;
    }

    clearRenderedCommandTarget();
    scheduleSourceSync();
}

void MainWindow::onRenderedSourceRangeClicked(SourceRange range)
{
    m_lastRenderedSourceRange = range;
    m_lastRenderedSelectedText.clear();
    updateSingleBlockCommandState();
    m_editor->clearRenderedSourceRangeHighlight();
    if (m_commandTargetLabel) {
        if (range.endLine > range.startLine) {
            m_commandTargetLabel->setText(tr("Selected source lines %1-%2").arg(range.startLine).arg(range.endLine));
        } else {
            m_commandTargetLabel->setText(tr("Selected source line %1").arg(range.startLine));
        }
    }
}

void MainWindow::onRenderedInlineTextSelected(SourceRange range, const QString& text)
{
    m_lastRenderedSourceRange = range;
    m_lastRenderedSelectedText = text;
    updateSingleBlockCommandState();
    m_editor->clearRenderedSourceRangeHighlight();
    if (m_commandTargetLabel) {
        const QString compactText = text.simplified();
        m_commandTargetLabel->setText(tr("Selected text on source line %1: %2")
            .arg(range.startLine)
            .arg(compactText.left(40)));
    }
}

void MainWindow::onRenderedEditRequested(RenderedEdit edit)
{
    PatchResult result = MarkdownEditEngine::applyRenderedEdit(m_document->markdown(), m_document->sourceMap(), m_document->blocks(), edit);
    if (!result.ok) {
        statusBar()->showMessage(result.error, 3000);
        return;
    }

    if (!result.changed) {
        if (result.cursorSourceOffset >= 0) {
            m_pendingRenderedCursorSourceOffset = result.cursorSourceOffset;
        }
        return;
    }

    m_pendingRenderedCursorSourceOffset = result.cursorSourceOffset;
    m_document->applyMarkdownEdit(result.text, result.cursorSourceOffset, tr("Edit Markdown"));
}

void MainWindow::onToggleViewMode()
{
    if (m_editor->mode() == MarkdownEditor::RenderedMode) {
        flushPendingSourceSync();
        m_updatingSourceFromDocument = true;
        m_editor->setSourceText(m_document->markdown());
        m_updatingSourceFromDocument = false;
        m_editor->setMode(MarkdownEditor::SourceMode);
    } else {
        syncSourceToDocument();
        m_editor->setMode(MarkdownEditor::RenderedMode);
    }
}

void MainWindow::ensureSourceMode()
{
    if (m_editor->mode() != MarkdownEditor::SourceMode) {
        flushPendingSourceSync();
        m_updatingSourceFromDocument = true;
        m_editor->setSourceText(m_document->markdown());
        m_updatingSourceFromDocument = false;
        m_editor->setMode(MarkdownEditor::SourceMode);
    }
}

void MainWindow::moveSourceCursorToRange(SourceRange range, bool selectRange)
{
    EditorSelectionMapper::moveSourceCursorToRange(m_editor->sourceEditor(), range, selectRange);
}

bool MainWindow::moveSourceCursorToInlineText(SourceRange range, const QString& text)
{
    return EditorSelectionMapper::moveSourceCursorToInlineText(m_editor->sourceEditor(), range, text);
}

bool MainWindow::hasRenderedCommandTarget() const
{
    return m_lastRenderedSourceRange.startLine > 0;
}

bool MainWindow::warnIfMissingRenderedCommandTarget()
{
    if (hasRenderedCommandTarget()) {
        return false;
    }

    statusBar()->showMessage(tr("Click a rendered block first, then apply a formatting command."), 3000);
    return true;
}

void MainWindow::clearRenderedCommandTarget()
{
    m_lastRenderedSourceRange = {};
    m_lastRenderedSelectedText.clear();
    m_editor->clearRenderedSourceRangeHighlight();
    if (m_commandTargetLabel) {
        m_commandTargetLabel->clear();
    }
    updateSingleBlockCommandState();
}

void MainWindow::updateSingleBlockCommandState()
{
    const bool isMultiBlockTarget = m_lastRenderedSourceRange.startLine > 0
        && m_lastRenderedSourceRange.endLine > m_lastRenderedSourceRange.startLine;
    const bool enabled = !isMultiBlockTarget;

    for (QAction* action : m_singleBlockParagraphActions) {
        if (action) {
            action->setEnabled(enabled);
        }
    }
    if (m_tableMenu) {
        m_tableMenu->setEnabled(false);
    }
}

void MainWindow::applyMarkdownCommand(MarkdownCommandResult (*command)(const QString&, SourceSelection))
{
    const bool wasRenderedMode = m_editor->mode() == MarkdownEditor::RenderedMode;
    const QString markdown = wasRenderedMode ? m_document->markdown() : m_editor->sourceText();
    MarkdownCommandResult result;

    if (wasRenderedMode) {
        if (warnIfMissingRenderedCommandTarget()) {
            return;
        }
        result = MarkdownEditEngine::applyInlineCommandToRenderedTarget(
            markdown, m_document->blocks(), {m_lastRenderedSourceRange, m_lastRenderedSelectedText}, command);
    } else {
        ensureSourceMode();
        result = MarkdownEditEngine::applyInlineCommand(
            markdown, EditorSelectionMapper::sourceSelectionForEditor(m_editor->sourceEditor()), command);
    }

    if (!result.changed) {
        return;
    }

    const int cursorOffset = result.selection.normalizedStart();
    if (wasRenderedMode) {
        m_pendingRenderedCursorSourceOffset = cursorOffset;
        m_document->applyMarkdownEdit(result.markdown, cursorOffset, tr("Format Markdown"));
        clearRenderedCommandTarget();
    } else {
        m_document->applyMarkdownEdit(result.markdown, cursorOffset, tr("Format Markdown"));
    }
}

void MainWindow::applyMarkdownListCommand(MarkdownCommand::ListType type)
{
    const bool wasRenderedMode = m_editor->mode() == MarkdownEditor::RenderedMode;
    const QString markdown = wasRenderedMode ? m_document->markdown() : m_editor->sourceText();
    MarkdownCommandResult result;

    if (wasRenderedMode) {
        if (warnIfMissingRenderedCommandTarget()) {
            return;
        }
        result = MarkdownEditEngine::applyListCommandToRenderedTarget(
            markdown, m_document->blocks(), {m_lastRenderedSourceRange, m_lastRenderedSelectedText}, type);
    } else {
        ensureSourceMode();
        result = MarkdownEditEngine::applyListCommand(
            markdown, EditorSelectionMapper::sourceSelectionForEditor(m_editor->sourceEditor()), type);
    }

    if (!result.changed) {
        return;
    }
    const int cursorOffset = result.selection.normalizedStart();
    if (wasRenderedMode) {
        m_pendingRenderedCursorSourceOffset = cursorOffset;
        clearRenderedCommandTarget();
    }
    m_document->applyMarkdownEdit(result.markdown, cursorOffset, tr("Format Markdown"));
}

void MainWindow::applyHeadingLevel(int level)
{
    const bool wasRenderedMode = m_editor->mode() == MarkdownEditor::RenderedMode;
    const QString markdown = wasRenderedMode ? m_document->markdown() : m_editor->sourceText();
    MarkdownCommandResult result;

    if (wasRenderedMode) {
        if (warnIfMissingRenderedCommandTarget()) {
            return;
        }
        result = MarkdownEditEngine::applyHeadingCommandToRenderedTarget(
            markdown, m_document->blocks(), {m_lastRenderedSourceRange, m_lastRenderedSelectedText}, level);
    } else {
        ensureSourceMode();
        result = MarkdownEditEngine::applyHeadingCommand(
            markdown, EditorSelectionMapper::sourceSelectionForEditor(m_editor->sourceEditor()), level);
    }

    if (!result.changed) {
        return;
    }
    const int cursorOffset = result.selection.normalizedStart();
    if (wasRenderedMode) {
        m_pendingRenderedCursorSourceOffset = cursorOffset;
        clearRenderedCommandTarget();
    }
    m_document->applyMarkdownEdit(result.markdown, cursorOffset, tr("Format Markdown"));
}

void MainWindow::onToggleBold()
{
    applyMarkdownCommand(&MarkdownCommand::toggleBold);
}

void MainWindow::onToggleItalic()
{
    applyMarkdownCommand(&MarkdownCommand::toggleItalic);
}

void MainWindow::onToggleUnderline()
{
    applyMarkdownCommand(&MarkdownCommand::toggleUnderline);
}

void MainWindow::onToggleInlineCode()
{
    applyMarkdownCommand(&MarkdownCommand::toggleInlineCode);
}

void MainWindow::onInsertLink()
{
    applyMarkdownCommand(&MarkdownCommand::insertLink);
}

void MainWindow::onApplyHeading1() { applyHeadingLevel(1); }
void MainWindow::onApplyHeading2() { applyHeadingLevel(2); }
void MainWindow::onApplyHeading3() { applyHeadingLevel(3); }
void MainWindow::onApplyHeading4() { applyHeadingLevel(4); }
void MainWindow::onApplyHeading5() { applyHeadingLevel(5); }
void MainWindow::onApplyHeading6() { applyHeadingLevel(6); }

void MainWindow::onApplyParagraph()
{
    applyMarkdownCommand(&MarkdownCommand::applyParagraph);
}

void MainWindow::onApplyQuote()
{
    applyMarkdownCommand(&MarkdownCommand::applyQuote);
}

void MainWindow::onApplyOrderedList()
{
    applyMarkdownListCommand(MarkdownCommand::ListType::Ordered);
}

void MainWindow::onApplyUnorderedList()
{
    applyMarkdownListCommand(MarkdownCommand::ListType::Unordered);
}

void MainWindow::onApplyTaskList()
{
    applyMarkdownListCommand(MarkdownCommand::ListType::Task);
}

void MainWindow::onFindReplace()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Find and Replace"));

    QLineEdit* findEdit = new QLineEdit(&dialog);
    QLineEdit* replaceEdit = new QLineEdit(&dialog);
    replaceEdit->setEnabled(m_editor->mode() == MarkdownEditor::SourceMode);

    QFormLayout* layout = new QFormLayout(&dialog);
    layout->addRow(tr("Find:"), findEdit);
    layout->addRow(tr("Replace:"), replaceEdit);

    QDialogButtonBox* buttons = new QDialogButtonBox(&dialog);
    QPushButton* findButton = buttons->addButton(tr("Find Next"), QDialogButtonBox::ActionRole);
    QPushButton* replaceButton = buttons->addButton(tr("Replace"), QDialogButtonBox::ActionRole);
    QPushButton* closeButton = buttons->addButton(QDialogButtonBox::Close);
    replaceButton->setEnabled(m_editor->mode() == MarkdownEditor::SourceMode);
    layout->addWidget(buttons);

    connect(findButton, &QPushButton::clicked, &dialog, [this, findEdit]() {
        const QString text = findEdit->text();
        if (text.isEmpty()) {
            return;
        }

        if (m_editor->mode() == MarkdownEditor::SourceMode) {
            if (!m_editor->sourceEditor()->find(text)) {
                QTextCursor cursor = m_editor->sourceEditor()->textCursor();
                cursor.movePosition(QTextCursor::Start);
                m_editor->sourceEditor()->setTextCursor(cursor);
                m_editor->sourceEditor()->find(text);
            }
        } else {
            m_updatingSourceFromDocument = true;
            m_editor->setSourceText(m_document->markdown());
            m_updatingSourceFromDocument = false;
            m_editor->setMode(MarkdownEditor::SourceMode);
            m_editor->sourceEditor()->find(text);
        }
    });

    connect(replaceButton, &QPushButton::clicked, &dialog, [this, findEdit, replaceEdit]() {
        if (m_editor->mode() != MarkdownEditor::SourceMode || findEdit->text().isEmpty()) {
            return;
        }

        QTextCursor cursor = m_editor->sourceEditor()->textCursor();
        if (!cursor.hasSelection() || cursor.selectedText() != findEdit->text()) {
            if (!m_editor->sourceEditor()->find(findEdit->text())) {
                return;
            }
            cursor = m_editor->sourceEditor()->textCursor();
        }
        cursor.insertText(replaceEdit->text());
    });

    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
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
    if (filePath.isEmpty() || !maybeSave()) {
        return;
    }

    if (!QFileInfo::exists(filePath)) {
        QStringList files = recentFiles();
        files.removeAll(filePath);
        setRecentFiles(files);
        QMessageBox::warning(this, tr("Open Recent File"), tr("The file no longer exists."));
        return;
    }

    loadFromFile(filePath);
}

void MainWindow::onClearRecentFiles()
{
    setRecentFiles({});
    updateRecentFilesMenu();
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
        "QMenu::item { padding: 6px 34px 6px 30px; color: %4; }"
        "QMenu::item:selected:enabled { background: rgba(128, 128, 128, 30); }"
        "QMenu::item:disabled { color: rgba(120, 120, 120, 86); background: transparent; }"
        "QMenu::item:selected:disabled { color: rgba(120, 120, 120, 86); background: transparent; }"
        "QMenu::separator { height: 1px; background: rgba(128, 128, 128, 38); margin: 5px 14px; }"
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
        "QLabel#commandTarget { color: rgba(40, 40, 40, 150); padding-left: 4px; }"
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

    QStringList existingFiles;
    for (const QString& filePath : files) {
        if (QFileInfo::exists(filePath)) {
            existingFiles.append(filePath);
        }
    }
    if (existingFiles != files) {
        setRecentFiles(existingFiles);
    }

    if (existingFiles.isEmpty()) {
        QAction* emptyAction = m_recentFilesMenu->addAction(QStringLiteral("无最近文件"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& filePath : existingFiles) {
        QAction* action = m_recentFilesMenu->addAction(QFileInfo(filePath).fileName());
        action->setToolTip(QDir::toNativeSeparators(filePath));
        action->setData(filePath);
        connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }

    m_recentFilesMenu->addSeparator();
    QAction* clearAction = m_recentFilesMenu->addAction(QStringLiteral("清除最近文件"));
    connect(clearAction, &QAction::triggered, this, &MainWindow::onClearRecentFiles);
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

QString MainWindow::currentMarkdownText() const
{
    return m_editor->mode() == MarkdownEditor::SourceMode
        ? m_editor->sourceText()
        : m_document->markdown();
}

int MainWindow::wordCount() const
{
    return currentMarkdownText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
}

int MainWindow::characterCount() const
{
    return currentMarkdownText().size();
}

int MainWindow::lineCount() const
{
    const QString text = currentMarkdownText();
    if (text.isEmpty()) {
        return 0;
    }
    return text.count(QChar('\n')) + 1;
}

void MainWindow::scheduleSourceSync()
{
    m_pendingSourceText = m_editor->sourceText();
    m_pendingSourceCursorOffset = m_editor->sourceEditor()->textCursor().position();
    m_sourceSyncTimer.start();
}

void MainWindow::flushPendingSourceSync()
{
    if (m_pendingSourceCursorOffset < 0) {
        return;
    }

    const QString text = std::move(m_pendingSourceText);
    const int cursorOffset = m_pendingSourceCursorOffset;
    m_pendingSourceText.clear();
    m_pendingSourceCursorOffset = -1;
    m_sourceSyncTimer.stop();
    m_document->applyMarkdownEdit(text, cursorOffset, tr("Edit Source"));
}

void MainWindow::syncSourceToDocument()
{
    flushPendingSourceSync();
    if (m_document->markdown() != m_editor->sourceText()) {
        m_document->applyMarkdownEdit(m_editor->sourceText(), m_editor->sourceEditor()->textCursor().position(), tr("Edit Source"));
    }
}

void MainWindow::onDocumentRendered()
{
    if (m_editor->mode() == MarkdownEditor::SourceMode && m_editor->sourceText() != m_document->markdown()) {
        const int cursorPosition = m_editor->sourceEditor()->textCursor().position();
        m_updatingSourceFromDocument = true;
        m_editor->setSourceText(m_document->markdown());
        m_updatingSourceFromDocument = false;
        QTextCursor cursor = m_editor->sourceEditor()->textCursor();
        cursor.setPosition(qBound(0, cursorPosition, m_editor->sourceText().size()));
        m_editor->sourceEditor()->setTextCursor(cursor);
    }

    QTextDocument* doc = m_document->textDocument();
    if (doc) {
        auto clone = doc->clone(m_editor);
        QTextBlock sourceBlock = doc->begin();
        QTextBlock clonedBlock = clone->begin();
        while (sourceBlock.isValid() && clonedBlock.isValid()) {
            auto* sourceData = dynamic_cast<SourceBlockData*>(sourceBlock.userData());
            if (sourceData && sourceData->isValid()) {
                clonedBlock.setUserData(new SourceBlockData(sourceData->range()));
            }
            sourceBlock = sourceBlock.next();
            clonedBlock = clonedBlock.next();
        }
        m_editor->setRenderedDocument(clone);
        if (m_pendingRenderedCursorSourceOffset) {
            std::optional<int> renderedPosition = m_document->sourceMap().renderedPositionForSourceOffset(
                *m_pendingRenderedCursorSourceOffset, RenderSourceMap::Bias::Backward);
            if (renderedPosition) {
                m_editor->setRenderedCursorPosition(*renderedPosition);
            }
            m_pendingRenderedCursorSourceOffset.reset();
        }
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

    if (!m_document->undoStack()->isClean())
        title += "*";

    title += " - Muffin";
    setWindowTitle(title);
}

bool MainWindow::saveToFile(const QString& filePath)
{
    if (m_editor->mode() == MarkdownEditor::SourceMode) {
        syncSourceToDocument();
    }

    QString content = m_document->markdown();

    if (!m_fileManager.writeFile(filePath, content)) {
        QMessageBox::warning(this, tr("Save Error"),
            tr("Cannot save file."));
        return false;
    }

    m_currentFile = filePath;
    m_modified = false;
    m_document->undoStack()->setClean();
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

    m_sourceSyncTimer.stop();
    m_pendingSourceText.clear();
    m_pendingSourceCursorOffset = -1;
    m_document->setMarkdown(content);
    m_document->setFilePath(filePath);
    m_currentFile = filePath;
    m_modified = false;
    m_document->undoStack()->setClean();
    m_editor->setMode(MarkdownEditor::RenderedMode);
    clearRenderedCommandTarget();
    addRecentFile(filePath);
    updateWindowTitle();
    return true;
}

bool MainWindow::maybeSave()
{
    if (m_document->undoStack()->isClean() && !m_document->isModified())
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
