#pragma once

#include "app/PreferencesPage.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace muffin {

class PrefsImagePage final : public PreferencesPage {
  Q_OBJECT

public:
  explicit PrefsImagePage(QWidget* parent = nullptr);

  void retranslateUi() override;

private:
  void loadSettings();
  // Show/hide the context-only rows: the custom-folder row (only for the
  // "copy to specified path" insert action) and the command row (only for the
  // "custom command" upload service).
  void updateConditionalRows();
  void browseCustomFolder();
  void testUploader();

  QLabel* insertLabel_ = nullptr;
  QComboBox* insertCombo_ = nullptr;
  QWidget* customFolderRow_ = nullptr;
  QLineEdit* customFolderEdit_ = nullptr;
  QPushButton* customFolderBrowse_ = nullptr;
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
  QWidget* commandRow_ = nullptr;
  QLineEdit* commandEdit_ = nullptr;
  QPushButton* testButton_ = nullptr;
};

}  // namespace muffin
