#pragma once

namespace muffin {

class MainWindow;

class MainWindowSignalBinder final {
public:
  static void connectEditorSignals(MainWindow& window);
  static void connectRenderSignals(MainWindow& window);
  static void connectSessionSignals(MainWindow& window);
  static void connectApplicationSignals(MainWindow& window);
  static void connectFindBarSignals(MainWindow& window);
  static void connectChromeSignals(MainWindow& window);
  static void connectSidebarSignals(MainWindow& window);

private:
  MainWindowSignalBinder() = delete;
};

}  // namespace muffin
