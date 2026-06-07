#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;

namespace muffin {

class PrefsImagePage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsImagePage(QWidget* parent = nullptr);

  void retranslateUi() override;

private:
  void loadSettings();

  QLabel* insertLabel_ = nullptr;
  QComboBox* insertCombo_ = nullptr;
  QCheckBox* applyToLocalCheck_ = nullptr;
  QCheckBox* applyToNetworkCheck_ = nullptr;
  QCheckBox* allowYamlUploadCheck_ = nullptr;

  QLabel* syntaxLabel_ = nullptr;
  QCheckBox* preferRelativePathCheck_ = nullptr;
  QCheckBox* addLeadingSlashCheck_ = nullptr;
  QCheckBox* escapeImageUrlCheck_ = nullptr;

  QLabel* uploadLabel_ = nullptr;
  QLabel* uploadServiceLabel_ = nullptr;
  QComboBox* uploadServiceCombo_ = nullptr;
};

}  // namespace muffin
