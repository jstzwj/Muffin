#include "MainWindow.h"
#include "editor/WysiwygEditor.h"
#include "renderer/Theme.h"

#include <QFileDialog>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QTextStream>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QLocale>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("Muffin");
    resize(900, 650);

    setupEditor();
    setupMenuBar();
    setupStatusBar();
    applyTheme();
}

void MainWindow::setupEditor() {
    m_editor = new Md::WysiwygEditor(this);
    setCentralWidget(m_editor);
}

void MainWindow::setupMenuBar() {
    // ── File Menu ──
    auto *fileMenu = menuBar()->addMenu(QString::fromUtf8("文件(&F)"));

    fileMenu->addAction(QString::fromUtf8("新建"), this, &MainWindow::onFileNew,
                        QKeySequence::New);
    fileMenu->addAction(QString::fromUtf8("新建窗口"), this, &MainWindow::onFileNewWindow,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    fileMenu->addSeparator();

    fileMenu->addAction(QString::fromUtf8("打开..."), this, &MainWindow::onFileOpen,
                        QKeySequence::Open);

    auto *openFolder = fileMenu->addAction(QString::fromUtf8("打开文件夹..."));
    openFolder->setEnabled(false);

    auto *quickOpen = fileMenu->addAction(QString::fromUtf8("快速打开..."));
    quickOpen->setEnabled(false);

    m_recentMenu = fileMenu->addMenu(QString::fromUtf8("打开最近文件"));

    auto *reopenEncoding = fileMenu->addMenu(QString::fromUtf8("选择编码重新打开"));
    reopenEncoding->setEnabled(false);
    fileMenu->addSeparator();

    fileMenu->addAction(QString::fromUtf8("保存"), this, &MainWindow::onFileSave,
                        QKeySequence::Save);
    fileMenu->addAction(QString::fromUtf8("另存为..."), this, &MainWindow::onFileSaveAs,
                        QKeySequence::SaveAs);

    auto *moveTo = fileMenu->addAction(QString::fromUtf8("移动到..."));
    moveTo->setEnabled(false);

    auto *saveAll = fileMenu->addAction(QString::fromUtf8("保存全部打开的文件..."));
    saveAll->setEnabled(false);
    fileMenu->addSeparator();

    fileMenu->addAction(QString::fromUtf8("属性..."), this, &MainWindow::onFileProperties);
    fileMenu->addAction(QString::fromUtf8("打开文件位置..."), this, &MainWindow::onOpenFileLocation);

    auto *showInSidebar = fileMenu->addAction(QString::fromUtf8("在侧边栏中显示"));
    showInSidebar->setEnabled(false);

    auto *deleteFile = fileMenu->addAction(QString::fromUtf8("删除..."));
    deleteFile->setEnabled(false);
    fileMenu->addSeparator();

    auto *importAction = fileMenu->addAction(QString::fromUtf8("导入..."));
    importAction->setEnabled(false);

    auto *exportMenu = fileMenu->addMenu(QString::fromUtf8("导出"));
    exportMenu->setEnabled(false);

    fileMenu->addAction(QString::fromUtf8("打印..."), this, &MainWindow::onFilePrint,
                        QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_P));
    fileMenu->addSeparator();

    auto *prefs = fileMenu->addAction(QString::fromUtf8("偏好设置..."));
    prefs->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    prefs->setEnabled(false);
    fileMenu->addSeparator();

    fileMenu->addAction(QString::fromUtf8("关闭"), this, &QWidget::close,
                        QKeySequence::Close);

    // ── Edit Menu ──
    auto *editMenu = menuBar()->addMenu(QString::fromUtf8("编辑(&E)"));

    editMenu->addAction(QString::fromUtf8("撤消"), this, []() {},
                        QKeySequence::Undo);
    editMenu->addAction(QString::fromUtf8("重做"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    editMenu->addSeparator();

    editMenu->addAction(QString::fromUtf8("剪切"), this, []() {},
                        QKeySequence::Cut);
    editMenu->addAction(QString::fromUtf8("复制"), this, []() {},
                        QKeySequence::Copy);
    editMenu->addAction(QString::fromUtf8("拷贝图片"))->setEnabled(false);
    editMenu->addAction(QString::fromUtf8("粘贴"), this, []() {},
                        QKeySequence::Paste);
    editMenu->addSeparator();

    editMenu->addAction(QString::fromUtf8("复制为纯文本"))->setEnabled(false);

    auto *copyMd = editMenu->addAction(QString::fromUtf8("复制为 Markdown"));
    copyMd->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    copyMd->setEnabled(false);

    editMenu->addAction(QString::fromUtf8("复制为 HTML 代码"))->setEnabled(false);
    editMenu->addAction(QString::fromUtf8("复制内容并简化格式"))->setEnabled(false);
    editMenu->addSeparator();

    auto *pastePlain = editMenu->addAction(QString::fromUtf8("粘贴为纯文本"));
    pastePlain->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
    pastePlain->setEnabled(false);
    editMenu->addSeparator();

    auto *selectMenu = editMenu->addMenu(QString::fromUtf8("选择"));
    selectMenu->addAction(QString::fromUtf8("全选"), this, []() {},
                          QKeySequence::SelectAll);
    selectMenu->addAction(QString::fromUtf8("选择当前行"))->setEnabled(false);
    selectMenu->addAction(QString::fromUtf8("选择当前格式文本"))->setEnabled(false);

    auto *moveUp = editMenu->addAction(QString::fromUtf8("上移该行"));
    moveUp->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    moveUp->setEnabled(false);

    auto *moveDown = editMenu->addAction(QString::fromUtf8("下移该行"));
    moveDown->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    moveDown->setEnabled(false);
    editMenu->addSeparator();

    editMenu->addAction(QString::fromUtf8("删除"))->setEnabled(false);

    auto *deleteRange = editMenu->addMenu(QString::fromUtf8("删除范围"));
    deleteRange->setEnabled(false);
    editMenu->addSeparator();

    editMenu->addMenu(QString::fromUtf8("数学工具"))->setEnabled(false);
    editMenu->addSeparator();

    editMenu->addMenu(QString::fromUtf8("智能标点"))->setEnabled(false);
    editMenu->addMenu(QString::fromUtf8("换行符"))->setEnabled(false);
    editMenu->addMenu(QString::fromUtf8("空格与换行"))->setEnabled(false);
    editMenu->addAction(QString::fromUtf8("拼写检查..."))->setEnabled(false);
    editMenu->addSeparator();

    editMenu->addAction(QString::fromUtf8("查找和替换"), this, &MainWindow::onFindReplace,
                        QKeySequence::Find);
    editMenu->addSeparator();

    auto *emoji = editMenu->addAction(QString::fromUtf8("表情与符号"));
    emoji->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Period));
    emoji->setEnabled(false);

    // ── Paragraph Menu ──
    auto *paraMenu = menuBar()->addMenu(QString::fromUtf8("段落(&P)"));

    auto *headingGroup = new QActionGroup(this);
    for (int i = 1; i <= 6; i++) {
        auto *act = paraMenu->addAction(
            QString::fromUtf8("%1级标题").arg(QString::number(i)));
        act->setCheckable(true);
        act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0 + i));
        act->setData(i);
        headingGroup->addAction(act);
        act->setEnabled(false);
    }
    paraMenu->addSeparator();

    auto *paraAct = paraMenu->addAction(QString::fromUtf8("段落"));
    paraAct->setCheckable(true);
    paraAct->setChecked(true);
    paraAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    headingGroup->addAction(paraAct);
    paraAct->setEnabled(false);
    paraMenu->addSeparator();

    auto *promote = paraMenu->addAction(QString::fromUtf8("提升标题级别"));
    promote->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    promote->setEnabled(false);

    auto *demote = paraMenu->addAction(QString::fromUtf8("降低标题级别"));
    demote->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus));
    demote->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addMenu(QString::fromUtf8("表格"))->setEnabled(false);

    auto *mathBlock = paraMenu->addAction(QString::fromUtf8("公式块"));
    mathBlock->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    mathBlock->setEnabled(false);

    auto *codeBlock = paraMenu->addAction(QString::fromUtf8("代码块"));
    codeBlock->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K));
    codeBlock->setEnabled(false);

    paraMenu->addAction(QString::fromUtf8("代码工具"))->setEnabled(false);
    paraMenu->addMenu(QString::fromUtf8("警告框"))->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addAction(QString::fromUtf8("引用"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q))->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addAction(QString::fromUtf8("有序列表"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("无序列表"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("任务列表"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X))->setEnabled(false);

    paraMenu->addMenu(QString::fromUtf8("任务状态"))->setEnabled(false);
    paraMenu->addMenu(QString::fromUtf8("列表缩进"))->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addAction(QString::fromUtf8("在上方插入段落"))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("在下方插入段落"))->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addAction(QString::fromUtf8("链接引用"))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("脚注"))->setEnabled(false);
    paraMenu->addSeparator();

    paraMenu->addAction(QString::fromUtf8("水平分割线"))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("内容目录"))->setEnabled(false);
    paraMenu->addAction(QString::fromUtf8("YAML Front Matter"))->setEnabled(false);

    // ── Format Menu ──
    auto *fmtMenu = menuBar()->addMenu(QString::fromUtf8("格式(&O)"));

    auto *bold = fmtMenu->addAction(QString::fromUtf8("加粗"), this, []() {},
                                    QKeySequence::Bold);
    bold->setCheckable(true);
    bold->setEnabled(false);

    auto *italic = fmtMenu->addAction(QString::fromUtf8("斜体"), this, []() {},
                                      QKeySequence::Italic);
    italic->setCheckable(true);
    italic->setEnabled(false);

    auto *underline = fmtMenu->addAction(QString::fromUtf8("下划线"), this, []() {},
                                         QKeySequence::Underline);
    underline->setCheckable(true);
    underline->setEnabled(false);

    auto *inlineCode = fmtMenu->addAction(QString::fromUtf8("代码"), this, []() {},
                                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_QuoteLeft));
    inlineCode->setCheckable(true);
    inlineCode->setEnabled(false);

    auto *strike = fmtMenu->addAction(QString::fromUtf8("删除线"), this, []() {},
                                      QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_5));
    strike->setCheckable(true);
    strike->setEnabled(false);
    fmtMenu->addSeparator();

    fmtMenu->addAction(QString::fromUtf8("注释"))->setEnabled(false);
    fmtMenu->addSeparator();

    fmtMenu->addAction(QString::fromUtf8("超链接"), this, []() {},
                       QKeySequence(Qt::CTRL | Qt::Key_K))->setEnabled(false);

    fmtMenu->addMenu(QString::fromUtf8("链接操作"))->setEnabled(false);
    fmtMenu->addMenu(QString::fromUtf8("图像"))->setEnabled(false);
    fmtMenu->addSeparator();

    auto *clearFmt = fmtMenu->addAction(QString::fromUtf8("清除样式"));
    clearFmt->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash));
    clearFmt->setEnabled(false);

    // ── View Menu ──
    auto *viewMenu = menuBar()->addMenu(QString::fromUtf8("视图(&V)"));

    viewMenu->addAction(QString::fromUtf8("显示 / 隐藏侧边栏"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L))->setEnabled(false);
    viewMenu->addAction(QString::fromUtf8("大纲"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_1))->setEnabled(false);
    viewMenu->addAction(QString::fromUtf8("文档列表"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_2))->setEnabled(false);
    viewMenu->addAction(QString::fromUtf8("文件树"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_3))->setEnabled(false);
    viewMenu->addAction(QString::fromUtf8("搜索"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F))->setEnabled(false);
    viewMenu->addSeparator();

    m_actionSourceMode = viewMenu->addAction(QString::fromUtf8("源代码模式"), this,
                                             &MainWindow::onToggleViewMode,
                                             QKeySequence(Qt::CTRL | Qt::Key_Slash));
    m_actionSourceMode->setCheckable(true);

    m_actionWordWrap = viewMenu->addAction(QString::fromUtf8("自动换行"));
    m_actionWordWrap->setCheckable(true);
    m_actionWordWrap->setChecked(true);
    connect(m_actionWordWrap, &QAction::toggled, this, [this](bool on) {
        m_editor->setLineWrapMode(on ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
    });
    viewMenu->addSeparator();

    viewMenu->addAction(QString::fromUtf8("专注模式"), this, []() {},
                        QKeySequence(Qt::Key_F8))->setEnabled(false);
    viewMenu->addAction(QString::fromUtf8("打字机模式"), this, []() {},
                        QKeySequence(Qt::Key_F9))->setEnabled(false);
    viewMenu->addSeparator();

    m_actionStatusBar = viewMenu->addAction(QString::fromUtf8("显示状态栏"));
    m_actionStatusBar->setCheckable(true);
    m_actionStatusBar->setChecked(true);
    connect(m_actionStatusBar, &QAction::toggled, this, [this](bool on) {
        statusBar()->setVisible(on);
    });
    viewMenu->addSeparator();

    viewMenu->addAction(QString::fromUtf8("字数统计窗口"), this,
                        &MainWindow::onShowWordCount);
    viewMenu->addAction(QString::fromUtf8("渲染诊断"), this,
                        &MainWindow::onShowRenderDiagnostics);
    viewMenu->addSeparator();

    m_actionFullscreen = viewMenu->addAction(QString::fromUtf8("切换全屏"), this,
                                             &MainWindow::onToggleFullscreen,
                                             QKeySequence(Qt::Key_F11));
    m_actionFullscreen->setCheckable(true);

    m_actionStayOnTop = viewMenu->addAction(QString::fromUtf8("保持窗口在最前端"), this,
                                            &MainWindow::onToggleStayOnTop);
    m_actionStayOnTop->setCheckable(true);
    viewMenu->addSeparator();

    m_actionActualSize = viewMenu->addAction(QString::fromUtf8("实际大小"), this,
                                             &MainWindow::onZoomActualSize,
                                             QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_9));
    m_actionActualSize->setCheckable(true);
    m_actionActualSize->setChecked(true);

    viewMenu->addAction(QString::fromUtf8("放大"), this, &MainWindow::onZoomIn,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal));
    viewMenu->addAction(QString::fromUtf8("缩小"), this, &MainWindow::onZoomOut,
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus));
    viewMenu->addSeparator();

    viewMenu->addAction(QString::fromUtf8("应用内窗口切换"), this, []() {},
                        QKeySequence(Qt::CTRL | Qt::Key_Tab))->setEnabled(false);
    viewMenu->addSeparator();

    viewMenu->addAction(QString::fromUtf8("开发者工具"), this, []() {},
                        QKeySequence(Qt::SHIFT | Qt::Key_F12))->setEnabled(false);

    // ── Theme Menu ──
    auto *themeMenu = menuBar()->addMenu(QString::fromUtf8("主题(&T)"));
    m_themeGroup = new QActionGroup(this);

    QStringList themeNames = {
        QString::fromUtf8("Github"),
        QString::fromUtf8("Newsprint"),
        QString::fromUtf8("Night"),
        QString::fromUtf8("Pixyll"),
        QString::fromUtf8("Whitey"),
    };
    for (int i = 0; i < themeNames.size(); i++) {
        auto *act = themeMenu->addAction(themeNames[i]);
        act->setCheckable(true);
        act->setData(i);
        m_themeGroup->addAction(act);
        if (i == 0) act->setChecked(true);
        connect(act, &QAction::triggered, this, &MainWindow::onThemeChanged);
    }
    themeMenu->addSeparator();
    themeMenu->addAction(QString::fromUtf8("打开主题文件夹"))->setEnabled(false);
    themeMenu->addAction(QString::fromUtf8("获取主题"))->setEnabled(false);

    // ── Help Menu ──
    auto *helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助(&H)"));

    helpMenu->addAction(QString::fromUtf8("What's New..."))->setEnabled(false);
    helpMenu->addSeparator();
    helpMenu->addAction(QString::fromUtf8("Quick Start"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("Markdown Reference"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("Install and Use Pandoc"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("Custom Themes"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("Use Images in Muffin"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("Data Recovery and Version Control"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("更多主题..."))->setEnabled(false);
    helpMenu->addSeparator();
    helpMenu->addAction(QString::fromUtf8("鸣谢"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("更新日志"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("隐私条款"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("官方网站"))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("反馈"))->setEnabled(false);
    helpMenu->addSeparator();
    helpMenu->addAction(QString::fromUtf8("检查更新..."))->setEnabled(false);
    helpMenu->addAction(QString::fromUtf8("我的许可证..."))->setEnabled(false);
    helpMenu->addSeparator();

    helpMenu->addAction(QString::fromUtf8("关于"), this, [this]() {
        QMessageBox::about(this, QString::fromUtf8("关于 Muffin"),
                           QString::fromUtf8("Muffin v0.1.0\n\n"
                                             "一个所见即所得的 Markdown 编辑器。\n\n"
                                             "基于 cmark-gfm 和 Qt 6 构建。"));
    });
}

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel(QString::fromUtf8("就绪"));
    m_statusLabel->setContentsMargins(4, 0, 4, 0);
    statusBar()->addWidget(m_statusLabel, 1);

    m_wordCountLabel = new QLabel;
    m_wordCountLabel->setContentsMargins(4, 0, 10, 0);
    statusBar()->addPermanentWidget(m_wordCountLabel);
}

// ── File Actions ──

void MainWindow::onFileNew() {
    MainWindow *w = new MainWindow;
    w->show();
}

void MainWindow::onFileNewWindow() {
    MainWindow *w = new MainWindow;
    w->show();
}

void MainWindow::onFileOpen() {
    QString path = QFileDialog::getOpenFileName(this,
        QString::fromUtf8("打开 Markdown 文件"), {},
        QString::fromUtf8("Markdown 文件 (*.md *.markdown *.txt);;所有文件 (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onFileSave() {
    // TODO: Phase 3 — implement save
}

void MainWindow::onFileSaveAs() {
    // TODO: Phase 3 — implement save as
}

void MainWindow::onFileProperties() {
    if (m_filePath.isEmpty()) return;
    QFileInfo fi(m_filePath);
    QString msg = QString::fromUtf8(
        "文件: %1\n"
        "大小: %2 bytes\n"
        "修改时间: %3")
        .arg(fi.fileName())
        .arg(fi.size())
        .arg(QLocale().toString(fi.lastModified(), QLocale::LongFormat));
    QMessageBox::information(this, QString::fromUtf8("文件属性"), msg);
}

void MainWindow::onOpenFileLocation() {
    if (m_filePath.isEmpty()) return;
    QFileInfo fi(m_filePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

void MainWindow::onFilePrint() {
    // TODO: requires Qt6::PrintSupport module
}

// ── Edit Actions ──

void MainWindow::onFindReplace() {
    // TODO: Phase 3 — find/replace dialog
}

// ── View Actions ──

void MainWindow::onToggleViewMode() {
    // TODO: Phase 3 — source code mode toggle
}

void MainWindow::onToggleFullscreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::onToggleStayOnTop() {
    Qt::WindowFlags flags = windowFlags();
    if (m_actionStayOnTop->isChecked())
        setWindowFlags(flags | Qt::WindowStaysOnTopHint);
    else
        setWindowFlags(flags & ~Qt::WindowStaysOnTopHint);
    show();
}

void MainWindow::onZoomIn() {
    m_zoomFactor = qMin(m_zoomFactor + 0.1, 3.0);
    m_editor->setFontPointSize(m_editor->font().pointSizeF());
    // TODO: proper zoom via stylesheet or font scaling
}

void MainWindow::onZoomOut() {
    m_zoomFactor = qMax(m_zoomFactor - 0.1, 0.3);
    // TODO: proper zoom via stylesheet or font scaling
}

void MainWindow::onZoomActualSize() {
    m_zoomFactor = kDefaultZoom;
    // TODO: proper zoom via stylesheet or font scaling
}

void MainWindow::onShowWordCount() {
    // TODO: word count dialog
}

void MainWindow::onShowRenderDiagnostics() {
    // TODO: render diagnostics
}

// ── Theme ──

void MainWindow::onThemeChanged() {
    auto *checked = m_themeGroup->checkedAction();
    if (!checked) return;
    m_currentThemeIndex = checked->data().toInt();
    applyTheme();
}

void MainWindow::applyTheme() {
    Md::Theme theme = Md::Theme::preset(m_currentThemeIndex);
    m_editor->setTheme(theme);

    // Apply chrome styling
    QString menuBg = theme.menuBackground().name();
    QString menuFg = theme.menuForeground().name();
    QString chromeBg = theme.chromeBackground().name();
    QString fg = theme.foreground().name();
    QString statusBg = theme.statusBackground().name();

    QString ss = QString(
        "QMainWindow { background: %1; }"
        "QMenuBar { background: %1; color: %2; font-size: 13px; padding: 0px 2px; spacing: 0px; }"
        "QMenuBar::item { padding: 5px 8px; background: transparent; }"
        "QMenuBar::item:selected { background: #e8e8e8; }"
        "QMenu { background: %3; color: %4; border: 1px solid rgba(128,128,128,42); "
        "  padding: 4px 0px; }"
        "QMenu::item { padding: 5px 30px 5px 22px; }"
        "QMenu::item:selected { background: #e8e8e8; }"
        "QMenu::item:disabled { color: rgba(120,120,120,86); }"
        "QMenu::separator { height: 1px; background: rgba(128,128,128,38); margin: 4px 14px; }"
        "QStatusBar { background: %5; color: rgba(80,80,80,170); font-size: 12px; border-top: none; }"
        "QStatusBar::item { border: none; }"
    ).arg(chromeBg, fg, menuBg, menuFg, statusBg);

    // Editor page background
    QString pageBg = theme.pageBackground().name();
    ss += QString(
        "QTextEdit { background: %1; border: none; selection-background-color: #b8d7ff; "
        "  selection-color: #222222; }"
        "QTextEdit QScrollBar:vertical { background: transparent; width: 8px; margin: 0px; }"
        "QTextEdit QScrollBar::handle:vertical { background: #b9b9b9; min-height: 64px; }"
        "QTextEdit QScrollBar::add-line:vertical, QTextEdit QScrollBar::sub-line:vertical { height: 0px; }"
        "QTextEdit QScrollBar::add-page:vertical, QTextEdit QScrollBar::sub-page:vertical { background: transparent; }"
    ).arg(pageBg);

    setStyleSheet(ss);
}

// ── Helpers ──

bool MainWindow::openFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();

    m_filePath = path;
    updateTitle();

    m_editor->loadMarkdown(content);
    m_statusLabel->setText(QString::fromUtf8("已加载: %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::updateTitle() {
    if (m_filePath.isEmpty())
        setWindowTitle("Muffin");
    else
        setWindowTitle(QString("%1 - Muffin").arg(QFileInfo(m_filePath).fileName()));
}

void MainWindow::updateRecentMenu() {
    m_recentMenu->clear();
    // TODO: implement recent files from QSettings
}

void MainWindow::addRecentFile(const QString &path) {
    Q_UNUSED(path)
    // TODO: persist recent files in QSettings
}
