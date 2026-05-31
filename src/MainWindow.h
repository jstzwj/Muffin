#pragma once

#include <QMainWindow>

class QTextEdit;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);

private:
  void setupMenuBar();

  QTextEdit *editor_ = nullptr;
};
