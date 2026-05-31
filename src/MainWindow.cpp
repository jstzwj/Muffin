#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  editor_ = new QTextEdit(this);
  editor_->setAcceptRichText(false);
  editor_->setPlaceholderText("Start writing...");
  setCentralWidget(editor_);

  setupMenuBar();
  statusBar()->showMessage("Ready");

  resize(1100, 760);
  setWindowTitle("Muffin");
}

void MainWindow::setupMenuBar() {
  auto *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
  fileMenu->addAction(tr("新建"), QKeySequence::New);
  fileMenu->addAction(tr("打开..."), QKeySequence::Open);
  fileMenu->addAction(tr("保存"), QKeySequence::Save);
  fileMenu->addAction(tr("另存为..."), QKeySequence::SaveAs);
  fileMenu->addSeparator();
  fileMenu->addAction(tr("关闭"), this, &QWidget::close, QKeySequence::Close);

  auto *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
  editMenu->addAction(tr("撤销"), editor_, &QTextEdit::undo, QKeySequence::Undo);
  editMenu->addAction(tr("重做"), editor_, &QTextEdit::redo, QKeySequence::Redo);
  editMenu->addSeparator();
  editMenu->addAction(tr("剪切"), editor_, &QTextEdit::cut, QKeySequence::Cut);
  editMenu->addAction(tr("复制"), editor_, &QTextEdit::copy, QKeySequence::Copy);
  editMenu->addAction(tr("粘贴"), editor_, &QTextEdit::paste, QKeySequence::Paste);

  auto *paragraphMenu = menuBar()->addMenu(tr("段落(&P)"));
  for (int level = 1; level <= 6; ++level) {
    auto *action = paragraphMenu->addAction(tr("%1级标题").arg(level));
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(level)));
    action->setEnabled(false);
  }

  auto *formatMenu = menuBar()->addMenu(tr("格式(&O)"));
  formatMenu->addAction(tr("加粗"), QKeySequence::Bold)->setEnabled(false);
  formatMenu->addAction(tr("斜体"), QKeySequence::Italic)->setEnabled(false);
  formatMenu->addAction(tr("代码"))->setEnabled(false);

  auto *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
  auto *wordWrapAction = viewMenu->addAction(tr("自动换行"));
  wordWrapAction->setCheckable(true);
  wordWrapAction->setChecked(true);
  connect(wordWrapAction, &QAction::toggled, this, [this](bool enabled) {
    editor_->setLineWrapMode(enabled ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);
  });

  auto *themeMenu = menuBar()->addMenu(tr("主题(&T)"));
  themeMenu->addAction(tr("Github"))->setEnabled(false);
  themeMenu->addAction(tr("Newsprint"))->setEnabled(false);
  themeMenu->addAction(tr("Night"))->setEnabled(false);

  auto *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
  helpMenu->addAction(tr("关于"), qApp, &QApplication::aboutQt);
}
