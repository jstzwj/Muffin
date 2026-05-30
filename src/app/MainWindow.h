#pragma once

#include <QMainWindow>
#include <QLabel>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool openFile(const QString &path);

private:
    void setupMenus();
    void setupStatusBar();
    void updateTitle();

    QString m_filePath;
    QLabel *m_statusLabel = nullptr;
};
