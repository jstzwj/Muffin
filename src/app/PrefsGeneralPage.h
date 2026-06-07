#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace muffin {

class PrefsGeneralPage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsGeneralPage(QWidget* parent = nullptr);

  void retranslateUi() override;

private:
  void populateLanguages();

  QLabel* languageLabel_ = nullptr;
  QComboBox* languageCombo_ = nullptr;
  QLabel* updateLabel_ = nullptr;
  QPushButton* checkUpdateButton_ = nullptr;
  QCheckBox* autoUpdateCheck_ = nullptr;
  QLabel* advancedLabel_ = nullptr;
  QCheckBox* debugModeCheck_ = nullptr;
  QCheckBox* telemetryCheck_ = nullptr;
  QPushButton* openAdvancedButton_ = nullptr;
  QPushButton* resetAdvancedButton_ = nullptr;
};

}  // namespace muffin
