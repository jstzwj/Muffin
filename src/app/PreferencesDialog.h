#pragma once

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace muffin {

class PreferencesDialog final : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

protected:
  void changeEvent(QEvent* event) override;

private:
  void retranslateUi();
  void populateLanguages();
  QWidget* makePage(QWidget* parent);
  QLabel* makeSectionLabel(QWidget* parent) const;
  QLabel* makeMutedLabel(QWidget* parent) const;
  QPushButton* makeButton(QWidget* parent) const;
  void addSectionRow(QGridLayout* grid, int row, QLabel* label, QWidget* field);
  void addPlaceholderPage();

  QLineEdit* searchEdit_ = nullptr;
  QListWidget* categoryList_ = nullptr;
  QStackedWidget* contentStack_ = nullptr;
  QVector<QLabel*> pageTitleLabels_;
  QVector<QLabel*> placeholderLabels_;

  QLabel* generalTitleLabel_ = nullptr;
  QLabel* languageLabel_ = nullptr;
  QLabel* languageRestartLabel_ = nullptr;
  QComboBox* languageCombo_ = nullptr;
  QLabel* updateLabel_ = nullptr;
  QPushButton* checkUpdateButton_ = nullptr;
  QCheckBox* autoUpdateCheck_ = nullptr;
  QCheckBox* betaUpdateCheck_ = nullptr;
  QLabel* advancedLabel_ = nullptr;
  QCheckBox* debugModeCheck_ = nullptr;
  QCheckBox* telemetryCheck_ = nullptr;
  QPushButton* openAdvancedButton_ = nullptr;
  QPushButton* resetAdvancedButton_ = nullptr;
};

}  // namespace muffin
