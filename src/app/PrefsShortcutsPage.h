#pragma once

#include "app/PreferencesPage.h"

class QKeySequenceEdit;
class QTreeWidget;

namespace muffin {
class PrefsShortcutsPage final : public PreferencesPage {
  Q_OBJECT
public:
  explicit PrefsShortcutsPage(QWidget* parent = nullptr);
  void retranslateUi() override;

private:
  void rebuild();
  void selectionChanged();
  void validate();
  void apply();
  QStringList conflicts() const;
  QLineEdit* filter_ = nullptr;
  QTreeWidget* commands_ = nullptr;
  QKeySequenceEdit* sequence_ = nullptr;
  QLabel* title_ = nullptr;
  QLabel* explanation_ = nullptr;
  QLabel* selectedCommand_ = nullptr;
  QLabel* message_ = nullptr;
  QPushButton* apply_ = nullptr;
  QPushButton* clear_ = nullptr;
  QPushButton* restore_ = nullptr;
  QPushButton* reset_ = nullptr;
};
} // namespace muffin
