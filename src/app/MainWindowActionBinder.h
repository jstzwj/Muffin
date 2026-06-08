#pragma once

namespace muffin {

class MainWindow;

class MainWindowActionBinder final {
public:
  static void bindCommands(MainWindow& window);
  static void restorePersistentActionStates(MainWindow& window);
  static void updateFileActions(MainWindow& window);
  static void updateEditActions(MainWindow& window);
  static void updateTableActions(MainWindow& window);
  static void updateParagraphActions(MainWindow& window);
  static void updateCodeActions(MainWindow& window);
  static void updateHtmlActions(MainWindow& window);
  static void updateMathActions(MainWindow& window);
  static void updateContextActions(MainWindow& window);
  static void updateThemeActions(MainWindow& window);

private:
  MainWindowActionBinder() = delete;
};

}  // namespace muffin
