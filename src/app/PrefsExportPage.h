#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace muffin {

class PrefsExportPage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsExportPage(QWidget* parent = nullptr);

  void retranslateUi() override;

private:
  void loadSettings();

  // Left sidebar: format list
  QListWidget* formatList_ = nullptr;

  // Right content card
  QLabel* defaultFolderLabel_ = nullptr;
  QComboBox* defaultFolderCombo_ = nullptr;
  QLabel* pandocLabel_ = nullptr;
  QLineEdit* pandocPathEdit_ = nullptr;
  QPushButton* pandocBrowseButton_ = nullptr;
  QLabel* afterExportLabel_ = nullptr;
  QCheckBox* openAfterExportCheck_ = nullptr;
};

}  // namespace muffin
