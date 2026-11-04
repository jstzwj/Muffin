#pragma once
#include "app/PreferencesPage.h"
#include "export/ExportPreset.h"
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QPushButton;
namespace muffin {
class PrefsExportPage final : public PreferencesPage {
  Q_OBJECT
public:
  explicit PrefsExportPage(QWidget* parent = nullptr);
  void retranslateUi() override;

private:
  void rebuildPage();
  void refreshPandocStatus();
  QVariant setting(const QString& key, const QVariant& fallback = {}) const;
  void saveSetting(const QString& key, const QVariant& value);
  void addPreset(bool duplicate);
  void renamePreset();
  void removePreset();
  void movePreset(int direction);
  void updatePresetButtons();
  QVector<ExportPreset> presets_;
  QPushButton* add_ = nullptr;
  QPushButton* duplicate_ = nullptr;
  QPushButton* rename_ = nullptr;
  QPushButton* remove_ = nullptr;
  QPushButton* up_ = nullptr;
  QPushButton* down_ = nullptr;
  QListWidget* formatList_ = nullptr;
  QLabel* sectionLabel_ = nullptr;
  QScrollArea* content_ = nullptr;
  QLineEdit* pandocPathEdit_ = nullptr;
  QLabel* pandocStatus_ = nullptr;
};
} // namespace muffin
