#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace muffin {

class PrefsFilesPage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsFilesPage(QWidget* parent = nullptr);

  void retranslateUi() override;

signals:
  void clearRecentFilesRequested();

private:
  void buildUi();
  void loadSettings();

  QLabel* startupLabel_ = nullptr;
  QComboBox* startupCombo_ = nullptr;
  QLabel* outlineLabel_ = nullptr;
  QCheckBox* outlineFoldableCheck_ = nullptr;
  QLabel* defaultExtLabel_ = nullptr;
  QComboBox* defaultExtCombo_ = nullptr;
  QLabel* saveLabel_ = nullptr;
  QCheckBox* autoSaveCheck_ = nullptr;
  QCheckBox* autoSaveSwitchCheck_ = nullptr;
  QPushButton* restoreDraftButton_ = nullptr;
  QLabel* recentLabel_ = nullptr;
  QCheckBox* recordHistoryCheck_ = nullptr;
  QPushButton* clearHistoryButton_ = nullptr;
  QLabel* dropLabel_ = nullptr;
  QLabel* dropFolderLabel_ = nullptr;
  QComboBox* dropFolderCombo_ = nullptr;
  QLabel* dropMdLabel_ = nullptr;
  QComboBox* dropMdCombo_ = nullptr;
  QLabel* dropImportLabel_ = nullptr;
  QComboBox* dropImportCombo_ = nullptr;
};

}  // namespace muffin
