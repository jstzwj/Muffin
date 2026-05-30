#include "MainWindow.h"

#include <QFileDialog>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QTextStream>
#include <QCloseEvent>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setWindowTitle("Muffin");
    resize(900, 650);

    setupMenus();
    setupStatusBar();
}

void MainWindow::setupMenus() {
    auto *fileMenu = menuBar()->addMenu("&File");

    fileMenu->addAction("&New", this, [this]() {
        // TODO: check unsaved changes
        MainWindow *w = new MainWindow;
        w->show();
    }, QKeySequence::New);

    fileMenu->addAction("&Open...", this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Open Markdown File",
            {}, "Markdown Files (*.md *.markdown *.txt);;All Files (*)");
        if (!path.isEmpty())
            openFile(path);
    }, QKeySequence::Open);

    fileMenu->addAction("&Save", this, [this]() {
        // TODO: implement save
    }, QKeySequence::Save);

    fileMenu->addAction("Save &As...", this, [this]() {
        // TODO: implement save as
    }, QKeySequence::SaveAs);

    fileMenu->addSeparator();
    fileMenu->addAction("&Close", this, &QWidget::close, QKeySequence::Close);
    fileMenu->addAction("&Quit", this, &QWidget::close, QKeySequence::Quit);

    auto *editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction("&Undo", this, []() {}, QKeySequence::Undo);
    editMenu->addAction("&Redo", this, []() {}, QKeySequence::Redo);
    editMenu->addSeparator();
    editMenu->addAction("Cu&t", this, []() {}, QKeySequence::Cut);
    editMenu->addAction("&Copy", this, []() {}, QKeySequence::Copy);
    editMenu->addAction("&Paste", this, []() {}, QKeySequence::Paste);
    editMenu->addSeparator();
    editMenu->addAction("Select &All", this, []() {}, QKeySequence::SelectAll);
}

void MainWindow::setupStatusBar() {
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel, 1);
}

bool MainWindow::openFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    QString content = in.readAll();

    m_filePath = path;
    updateTitle();

    // TODO: parse markdown and render
    m_statusLabel->setText(QString("Loaded: %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::updateTitle() {
    if (m_filePath.isEmpty())
        setWindowTitle("Muffin");
    else
        setWindowTitle(QString("%1 - Muffin").arg(QFileInfo(m_filePath).fileName()));
}
